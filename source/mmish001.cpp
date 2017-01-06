#include <Windows.h>
#include "resource.h"
#include "InputThread.h"
#include "OutputThread.h"
#include "WorkerThread.h"
#include "Indicators.h"
#include "MModel.h"
#include "KChFstate.h"
#include "WAVDump.h"

extern MModel model;

unsigned long   iNumDevs; // Эту переменную иcпользуем при InputThread::Start,чтобы понять, что выбрали файл, а не устройство
HINSTANCE GZInst; // Присваивается в начале программы и используется затем всеми
HWND hdwnd;

HBITMAP hbm_exclaim=0, hbm_up, hbm_left, hbm_down, hbm_right;
bool flag_model_changed=false;

int SUWindow(); // дополнительное окно в режиме суперпользователя

// Потом это вынесем куда-нибудь. Этим пользуются все - и WorkerThread, и CopyShmopy, и Indicators 
volatile bool flag_training_mode=false;
volatile long training_frame_counter=0; //17-DEC
volatile long training_mfcc_recorded=0;
volatile int training_sound=0;
volatile bool flag_move_mouse=false;

volatile bool flag_keep_silence=true; // [27-DEC] - отмена отключения устройства воспроизведения. звук нужен для кликов. теперь обнуляем буфер, когда нужна тишина.

volatile bool flag_wav_dump=false; // [29-DEC]


volatile int current_device_num=0; // Для записи в файл конфигурации

#define MM_NUM_TRAINING_BUTTONS 6
static int training_buttons[MM_NUM_TRAINING_BUTTONS]={IDC_BUTTON_TRAIN0, IDC_BUTTON_TRAIN1,
IDC_BUTTON_TRAIN2, IDC_BUTTON_TRAIN3 , IDC_BUTTON_TRAIN4 , IDC_BUTTON_TRAIN5 };
static int exclaim_controls[MM_NUM_TRAINING_BUTTONS]={IDC_STATIC_BMP0, IDC_STATIC_BMP1,
IDC_STATIC_BMP2, IDC_STATIC_BMP3 , IDC_STATIC_BMP4 , IDC_STATIC_BMP5 };

//============================================================================================
// Если что-то не натренировано, рисует восклицательные знаки
// Вызывать после загрузки и тренировки
// Возвращает признак того, что модель может работать (используется при запуске программы)
//============================================================================================
bool UpdateExclaim(bool hide=false)
{
	int i,sounds_ready=0;

	// Первый раз загружаем битмап
	if(!hbm_exclaim) hbm_exclaim=(HBITMAP)LoadImage(GZInst,MAKEINTRESOURCE(IDB_BITMAP1),IMAGE_BITMAP,0,0,LR_DEFAULTSIZE);
	
	// Проходим по всем звукам и проверяем наличие звуков в списке
	for(i=0;i<MM_NUM_TRAINING_BUTTONS;i++)
	{
		if((model.IsSoundFilled(i))||hide)
		{
			sounds_ready++;
			// восклицательный знак не нужен
			SendDlgItemMessage(hdwnd,exclaim_controls[i], STM_SETIMAGE, IMAGE_BITMAP, NULL); 
		}
		else
		{
			// Тут нужен восклицательный знак
			SendDlgItemMessage(hdwnd,exclaim_controls[i], STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hbm_exclaim); 
		}
	}

	// Резюмирующий восклицательный знак и подпись
	if(sounds_ready<MM_NUM_TRAINING_BUTTONS)
	{
		// Тут нужен восклицательный знак
		SendDlgItemMessage(hdwnd,IDC_STATIC_BMP6, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hbm_exclaim);
		// В спец. области выводим подсказку
		SetDlgItemText(hdwnd, IDC_STATIC_GROM, L"Программа ещё не обучена некоторым звукам. Для обучения нажмите кнопку “Тренировать” и, выждав паузу, несколько раз произнесите в микрофон нужный звук.");
		return false;
	}
	else
	{
		// убираем лишнее
		SendDlgItemMessage(hdwnd,IDC_STATIC_BMP6, STM_SETIMAGE, IMAGE_BITMAP, NULL);
		// В спец. области убираем подсказку
		SetDlgItemText(hdwnd, IDC_STATIC_GROM, L"");
		return true;
	}

}


void StopTrainingMode()
{
	int i;
	flag_training_mode=false;

	// Показать все кнопки кроме той, что с номером _sound. Ей поменять надпись на "Тренировать"
	for(i=0;i<MM_NUM_TRAINING_BUTTONS;i++)
	{
		if(i==training_sound) SetDlgItemText(hdwnd, training_buttons[i], L"Тренировать");
		else EnableWindow( GetDlgItem( hdwnd, training_buttons[i] ), TRUE);
	}

	// В спец. области убираем подсказку
	SetDlgItemText(hdwnd, IDC_STATIC_GROM, L"");

	// Возможно, нужны восклицательные знаки
	UpdateExclaim();

	// Перерисовать диалог, остались рудименты
	InvalidateRect(hdwnd,NULL,TRUE);
}

void StartTrainingMode(int _sound)
{
	int i;
	RECT r;

	// Для кнопок 4 и 5 уводить курсор в начало окна
	if((4==_sound)||(5==_sound))
	{
		GetWindowRect(hdwnd,&r);
		SetCursorPos(r.left+5,r.top+5);
	}


	// Модель будет изменена...
	flag_model_changed=true;

	// обнулить ветку модели
	model.Capture(); // Стираем безопасно
	model.EmptySound(_sound);
	model.Release();

	// Спрятать все кнопки кроме той, что с номером _sound. Ей поменять надпись на "Прекратить"
	for(i=0;i<MM_NUM_TRAINING_BUTTONS;i++)
	{
		if(i==_sound) SetDlgItemText(hdwnd, training_buttons[i], L"Прекратить");
		else EnableWindow( GetDlgItem( hdwnd, training_buttons[i] ), FALSE);
	}

	training_frame_counter=0; 
	training_mfcc_recorded=0;
	training_sound=_sound;
	flag_training_mode=true; // Важен порядок, ибо другой поток сначала читает flag...

	// Прячем восклицательные знаки и затираем свою подсказку
	UpdateExclaim(true);

	// В спец. области выводим подсказку
	SetDlgItemText(hdwnd, IDC_STATIC_GROM, L"Говорите громче уровня шума (отмечен красной полоской)");

	// Перерисовать диалог, остались рудименты
	InvalidateRect(hdwnd,NULL,TRUE);
}

// Стартует или прекращает training_mode
void TrainingButton(int _sound)
{
	if(flag_training_mode) StopTrainingMode();
	else StartTrainingMode(_sound);
}

//================================================================
// Оконная процедура диалога
//================================================================
static BOOL CALLBACK DlgWndProc(HWND hdwnd,
						   UINT uMsg,
						   WPARAM wparam,
						   LPARAM lparam )
{
	HDC hdc;
	PAINTSTRUCT ps;
	WAVEINCAPS     wic;
	unsigned long i;

	switch(uMsg)
	{
	case WM_TIMER: // Рисуем индикатор уровня и прочее
		hdc=GetDC(hdwnd);
		Indicators::Draw(hdc);
		ReleaseDC(hdwnd, hdc);
		return 1;

	case WM_PAINT:
		hdc=BeginPaint(hdwnd,&ps);
		Indicators::Draw(hdc);
		EndPaint(hdwnd,&ps);
		return 0; // Если вернуть не 0, то будет плохо.

	case WM_INITDIALOG:
		// 1. Уходим из верхнего левого угла
		SetWindowPos(hdwnd,HWND_TOP,300,50,0,0,SWP_NOSIZE | SWP_NOREDRAW);

		// 1.5 Индикаторам требуется наш hdwnd для инициализации
		Indicators::Init(hdwnd);

		// 2. Посмотрим, какие есть устройства ввода
		iNumDevs = waveInGetNumDevs();
		// 2.1 Проверить, есть ли вообще звуковые устройства. Ибо если нет - немедленно выпить... то есть выйти
		if(0==iNumDevs)
		{
			MessageBox(hdwnd,L"Для работы программы необходимо, чтобы в компьютере работало устройство для записи звука!",L"Не найдено ни одного звукового устройства",MB_OK);
			EndDialog(hdwnd,3);
			return 1;
		}

		for(i=0;i<iNumDevs;i++)
		{
			waveInGetDevCaps(i, &wic, sizeof(WAVEINCAPS));
			SendDlgItemMessage(hdwnd,IDC_COMBO_MIC, CB_ADDSTRING, 0, (LPARAM)(wic.szPname));
		}
		SendDlgItemMessage(hdwnd,IDC_COMBO_MIC, CB_ADDSTRING, 0, (LPARAM)L"Чтение из файла");
		SendDlgItemMessage(hdwnd,IDC_COMBO_MIC, CB_SETCURSEL, 0, 0L); // Иначе там ничего не выбрано

		// 2.5
		if(KChFstate::flag_kc_anytime) // Корректируем галочку в диалоге
				SendDlgItemMessage(hdwnd, IDC_CHECK_IGNORE_KC_INLINE, BM_SETCHECK, BST_UNCHECKED, 0);
			else 
				SendDlgItemMessage(hdwnd, IDC_CHECK_IGNORE_KC_INLINE, BM_SETCHECK, BST_CHECKED, 0);

		// 2.6. Загружаем битмапы стрелочек
		// Вверх
		hbm_up=(HBITMAP)LoadImage(GZInst,MAKEINTRESOURCE(IDB_BITMAP_UP),IMAGE_BITMAP,0,0,LR_DEFAULTSIZE);
		SendDlgItemMessage(hdwnd,IDC_STATIC_BMP_UP, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hbm_up);
		// Влево
		hbm_left=(HBITMAP)LoadImage(GZInst,MAKEINTRESOURCE(IDB_BITMAP_LEFT),IMAGE_BITMAP,0,0,LR_DEFAULTSIZE);
		SendDlgItemMessage(hdwnd,IDC_STATIC_BMP_LEFT, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hbm_left);
		// Вниз
		hbm_down=(HBITMAP)LoadImage(GZInst,MAKEINTRESOURCE(IDB_BITMAP_DOWN),IMAGE_BITMAP,0,0,LR_DEFAULTSIZE);
		SendDlgItemMessage(hdwnd,IDC_STATIC_BMP_DOWN, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hbm_down);
		// Вправо
		hbm_right=(HBITMAP)LoadImage(GZInst,MAKEINTRESOURCE(IDB_BITMAP_RIGHT),IMAGE_BITMAP,0,0,LR_DEFAULTSIZE);
		SendDlgItemMessage(hdwnd,IDC_STATIC_BMP_RIGHT, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hbm_right);


		// 3. Стартуем нулевое устройство (первое в списке)
		// Теперь воспроизведение через звуковую карту бывает только для файлов в Release-версии
		// [27-DEC] - отмена. звук нужен для кликов. теперь обнуляем буфер, когда нужна тишина.
//#ifdef _DEBUG
		OutputThread::Start(hdwnd);
//#endif
		//Sleep(1000);
		WorkerThread::Start();
		//Sleep(1000);
		InputThread::Start(0,hdwnd);

		

		// 4. Включаем таймер #1 для индикаторов
		SetTimer(hdwnd,1,100,NULL); // 10 раз в секунду
		
		return 1;

	case WM_COMMAND:
		switch(LOWORD(wparam))
		{
		case IDCANCEL:
			int exit_result;
			// Вопрос задаём, только если были изменения. То есть любая тренировка после сохранения или загрузки
			// Иначе просто выходим
			if(!flag_model_changed)
			{
				PostQuitMessage(0); 
				return (1);
			}

			// были изменения, возможно, их лучше сохранить
			exit_result = MessageBox(NULL, L"Сохранить натренированное?", L"Сохранить натренированное?",  MB_YESNOCANCEL | MB_ICONQUESTION);
			switch(exit_result)
			{
			case IDYES:
				model.Save(false,hdwnd,L"default.MM1");
				PostQuitMessage(0); // Ибо мы теперь немодальные
				break;

			case IDNO:
				PostQuitMessage(0); // Ибо мы теперь немодальные
				break;

			case IDCANCEL:
				break;
			}
			// Рудимент от модального диалога
			//EndDialog(hdwnd,2);
			return 1;

		case IDOK:
			// Активация / деактивация
			if(flag_move_mouse)
			{
				flag_move_mouse=false;
				SetDlgItemText(hdwnd, IDOK, L"Запустить");
			}
			else
			{
				flag_move_mouse=true;
				SetDlgItemText(hdwnd, IDOK, L"Стоп");
			}
			return 1;

		case IDC_COMBO_MIC:
			if(CBN_SELCHANGE==HIWORD(wparam)) // Выбрали другой микрофон
			{
				// MessageBox(hdwnd,L"Выбрали другой микрофон!",L"Выбрали другой микрофон",MB_OK);
				// Открываем выбранное устройство
				current_device_num=SendDlgItemMessage(hdwnd,IDC_COMBO_MIC, CB_GETCURSEL, 0, 0L); // Кто теперь царь горы?
				InputThread::Start(current_device_num,hdwnd);
				return 1;
			}
			else return 0;

		case IDC_BUTTON_TRAIN0:
			TrainingButton(0);
			break;

		case IDC_BUTTON_TRAIN1:
			TrainingButton(1);
			break;

		case IDC_BUTTON_TRAIN2:
			TrainingButton(2);
			break;

		case IDC_BUTTON_TRAIN3:
			TrainingButton(3);
			break;

		case IDC_BUTTON_TRAIN4:
			TrainingButton(4);
			break;

		case IDC_BUTTON_TRAIN5:
			TrainingButton(5);
			break;
			
		case IDC_BUTTON_SAVE:
			model.Save(false,hdwnd,L"default.MM1");
			break;

		case IDC_BUTTON_SAVE_AS:
			// Сбивается рабочий каталог и сохранять по умолчанию бессмысленно
			if(model.Save(true,hdwnd))
			{
				flag_model_changed=false;
				EnableWindow( GetDlgItem( hdwnd, IDC_BUTTON_SAVE ), FALSE);
			}
			break;

		case IDC_BUTTON_LOAD:
			// Сбивается рабочий каталог и сохранять по умолчанию бессмысленно
			if(model.Load(hdwnd))
			{
				flag_model_changed=false;
				if(KChFstate::flag_kc_anytime) // Корректируем галочку в диалоге
					SendDlgItemMessage(hdwnd, IDC_CHECK_IGNORE_KC_INLINE, BM_SETCHECK, BST_UNCHECKED, 0);
				else 
					SendDlgItemMessage(hdwnd, IDC_CHECK_IGNORE_KC_INLINE, BM_SETCHECK, BST_CHECKED, 0);
				InputThread::Start(current_device_num,hdwnd); // выставить устройство, прописанное в файле
				EnableWindow( GetDlgItem( hdwnd, IDC_BUTTON_SAVE ), FALSE);
			}
			break;

		case IDC_CHECK_IGNORE_KC_INLINE:
			flag_model_changed=true;
			// Тут можно было как-то wparam контролировать, но не было под рукой справки
			if(BST_CHECKED==SendDlgItemMessage(hdwnd, IDC_CHECK_IGNORE_KC_INLINE, BM_GETCHECK, 0, 0))
				KChFstate::flag_kc_anytime=false;
			else
				KChFstate::flag_kc_anytime=true;

			break;

		case IDC_BUTTON_WAV_DUMP:
			// Start и Stop меняют надпись на кнопке. Для этого hdwnd
			if(flag_wav_dump)
			{
				flag_wav_dump=false;
				MMWAVDump::Stop(); // Именно в таком порядке
			}
			else
			{
				MMWAVDump::Start(hdwnd);
				flag_wav_dump=true; // Именно в таком порядке
			}
			break;
		
		default:
			return 0;
		}
		break;



	default:
		return 0;
	}
	return 0;
}

//=======================================================================
// программа
//=======================================================================
int WINAPI WinMain(HINSTANCE hInst,HINSTANCE,LPSTR cline,INT)
// Командную строку не обрабатываем
{
	static MSG msg; // Сообщение
	GZInst=hInst;


#ifdef MM_SUPERUSER
	SUWindow(); // Это не для всех
#endif

	// Диалог теперь - немодальный. Ибо в режиме суперпользователя появляется ещё одно окно
	//DialogBox(hInst,MAKEINTRESOURCE(IDD_DIALOG1),NULL,(DLGPROC)DlgWndProc);
	hdwnd=CreateDialog(hInst, MAKEINTRESOURCE(IDD_DIALOG1),NULL,(DLGPROC)DlgWndProc);
	ShowWindow(hdwnd,SW_SHOWNORMAL);

	// Грузим настройку по умолчанию
	if(model.Load(NULL,L"default.MM1"))
	{
		if(KChFstate::flag_kc_anytime) // Корректируем галочку в диалоге
			SendDlgItemMessage(hdwnd, IDC_CHECK_IGNORE_KC_INLINE, BM_SETCHECK, BST_UNCHECKED, 0);
		else 
			SendDlgItemMessage(hdwnd, IDC_CHECK_IGNORE_KC_INLINE, BM_SETCHECK, BST_CHECKED, 0);
		SendDlgItemMessage(hdwnd,IDC_COMBO_MIC, CB_SETCURSEL, current_device_num, 0L); // Скорректируем выбранное устройство в списке диалога
		InputThread::Start(current_device_num,hdwnd); // выставить устройство, прописанное в файле
	}

	// При необходимости напечатать восклицательные знаки
	// Если же модель полностью натренирована, разрешаем работу и пишем на кнопке "Старт"
	if(UpdateExclaim())
	{
		flag_move_mouse=true;
		SetDlgItemText(hdwnd, IDOK, L"Стоп");
	}

	//Цикл обработки сообщений
	while(GetMessage(&msg,NULL,0,0)) 
    {
		if(!IsDialogMessage(hdwnd, &msg))
		{
			TranslateMessage( &msg );
			DispatchMessage( &msg );
		}
	}// while !WM_QUIT

	// Дамп звука аккуратно закроем
	if(flag_wav_dump)
	{
		flag_wav_dump=false;
		MMWAVDump::Stop(); // Именно в таком порядке
	}

	// Чистим за собой
	KillTimer(hdwnd,1);
	WorkerThread::Halt(0);
	InputThread::Halt(0);
	OutputThread::Halt(0);
	Indicators::Halt();
}