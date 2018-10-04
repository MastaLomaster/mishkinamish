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
int KCWindow(); // дополнительное окно в режиме суперпользователя

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

// Это заимствовано из MHook. 
typedef struct
{
	TCHAR *stroka;
	WORD value;
} MHWORDChar;

#define MH_NUM_SCANCODES 105

// Здесь нет PrtScr,Pause
MHWORDChar dlg_scancodes[MH_NUM_SCANCODES]=
{
	{L"<ничего>",0xFFFF}, // 0
	{L"вверх",0xE048},{L"вправо",0xE04D},{L"вниз",0xE050},{L"влево",0xE04B}, // 1-4
	{L"A",0x1E},{L"B",0x30},{L"C",0x2E},{L"D",0x20},{L"E",0x12}, // 5-9
	{L"F",0x21},{L"G",0x22},{L"H",0x23},{L"I",0x17},{L"J",0x24}, // 10-14
	{L"K",0x25},{L"L",0x26},{L"M",0x32},{L"N",0x31},{L"O",0x18}, // 15-19
	{L"P",0x19},{L"Q",0x10},{L"R",0x13},{L"S",0x1F},{L"T",0x14}, // 20-24
	{L"U",0x16},{L"V",0x2F},{L"W",0x11},{L"X",0x2D},{L"Y",0x15}, // 25-29
	{L"Z",0x2C},{L"0",0x0B},{L"1",0x02},{L"2",0x03},{L"3",0x04}, // 30-34
	{L"4",0x05},{L"5",0x06},{L"6",0x07},{L"7",0x08},{L"8",0x09}, // 35-39
	{L"9",0x0A},{L"~",0x29},{L"-",0x0C},{L"=",0x0D},{L"\\",0x2B}, // 40-44
	{L"[",0x1A},{L"]",0x1B},{L";",0x27},{L"'",0x28},{L",",0x33}, //45-49
	{L".",0x34},{L"/",0x35},{L"Backspace",0x0E},{L"пробел",0x39},{L"TAB",0x0F}, // 50-54
	{L"Caps Lock",0x3A},{L"Левый Shift",0x2A},{L"Левый Ctrl",0x1D},{L"Левый Alt",0x38},{L"Левый Win",0xE05B}, // 55-59
	{L"Правый Shift",0x36},{L"Правый Ctrl",0xE01D},{L"Правый Alt",0xE038},{L"Правый WIN",0xE05C},{L"Menu",0xE05D}, // 60-64
	{L"Enter",0x1C},{L"Esc",0x01},{L"F1",0x3B},{L"F2",0x3C},{L"F3",0x3D}, // 65-69
	{L"F4",0x3E},{L"F5",0x3F},{L"F6",0x40},{L"F7",0x41},{L"(F8 - запрещена) ",0xFFFF}, // 70-74
	{L"F9",0x43},{L"F10",0x44},{L"F11",0x57},{L"F12",0x58},{L"Scroll Lock",0x46}, // 75-79
	{L"Insert",0xE052},{L"(Delete - запрещена)",0xE053},{L"Home",0xE047},{L"End",0xE04F},{L"PgUp",0xE049}, // 80-84
	{L"PgDn",0xE051},{L"Num Lock",0x45},{L"Num /",0xE035},{L"Num *",0x37},{L"Num -",0x4A}, // 85-89
	{L"Num +",0x4E},{L"Num Enter",0xE01C},{L"(Num . - запрещена)",0xFFFF},{L"Num 0",0x52},{L"Num 1",0x4F}, // 90-94
	{L"Num 2",0x50},{L"Num 3",0x51},{L"Num 4",0x4B},{L"Num 5",0x4C},{L"Num 6",0x4D}, // 95-99
	{L"Num 7",0x47},{L"Num 8",0x48},{L"Num 9",0x49}, // 100-102
	{L"Левая мышь",0xFF00},{L"Правая мышь",0xFF01}
}; 

//============================================================================================
// Выводит на экран то, что будет нажато
//============================================================================================
static void SetKeysInDialogue()
{
	int listbox[6]={IDC_KBD0, IDC_KBD1, IDC_KBD2, IDC_KBD3, IDC_KBD4, IDC_KBD5};
	int listbox2[4]={IDC_CHECK_REPEAT0, IDC_CHECK_REPEAT1, IDC_CHECK_REPEAT2, IDC_CHECK_REPEAT3};
	int i,j;
	bool found;

	for(i=0;i<6;i++)
	{
		found=false;
		for(j=0;j<MH_NUM_SCANCODES;j++)
		{
			if(KChFstate::key_to_press[i]==dlg_scancodes[j].value)
			{
				found=true;
				SendDlgItemMessage(hdwnd,listbox[i], CB_SETCURSEL, j, 0L); // Скорректируем выбранное устройство в списке диалога
				break;
			}
		}
		if(!found) // Такой клавиши нет в списке возможных, сбросить её в 0xffff
		{
			KChFstate::SetKeyToPress(i,0xffff);
		}
	}

	// Теперь чекбоксы "повтор"
	for(i=0;i<4;i++)
	{
		if(KChFstate::repeat_key[i]) // Корректируем галочку в диалоге
			SendDlgItemMessage(hdwnd, listbox2[i], BM_SETCHECK, BST_CHECKED, 0);
		else 
			SendDlgItemMessage(hdwnd, listbox2[i], BM_SETCHECK, BST_UNCHECKED, 0);
	}
}

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

		// 2.7. Загружаем списки кнопок клавиатуры
		// 2. Клавиши
		for(i=0;i<MH_NUM_SCANCODES;i++)
		{
			SendDlgItemMessage(hdwnd,IDC_KBD0, CB_ADDSTRING, 0, (LPARAM)(dlg_scancodes[i].stroka));
			SendDlgItemMessage(hdwnd,IDC_KBD1, CB_ADDSTRING, 0, (LPARAM)(dlg_scancodes[i].stroka));
			SendDlgItemMessage(hdwnd,IDC_KBD2, CB_ADDSTRING, 0, (LPARAM)(dlg_scancodes[i].stroka));
			SendDlgItemMessage(hdwnd,IDC_KBD3, CB_ADDSTRING, 0, (LPARAM)(dlg_scancodes[i].stroka));
			SendDlgItemMessage(hdwnd,IDC_KBD4, CB_ADDSTRING, 0, (LPARAM)(dlg_scancodes[i].stroka));
			SendDlgItemMessage(hdwnd,IDC_KBD5, CB_ADDSTRING, 0, (LPARAM)(dlg_scancodes[i].stroka));
		}
		SendDlgItemMessage(hdwnd,IDC_KBD0, CB_SETCURSEL, 0, 0L);
		SendDlgItemMessage(hdwnd,IDC_KBD1, CB_SETCURSEL, 0, 0L);
		SendDlgItemMessage(hdwnd,IDC_KBD2, CB_SETCURSEL, 0, 0L);
		SendDlgItemMessage(hdwnd,IDC_KBD3, CB_SETCURSEL, 0, 0L);
		SendDlgItemMessage(hdwnd,IDC_KBD4, CB_SETCURSEL, 0, 0L);
		SendDlgItemMessage(hdwnd,IDC_KBD5, CB_SETCURSEL, 0, 0L);

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
			if(model.Save(false,hdwnd,L"default.MM1")) flag_model_changed=false;
			break;

		case IDC_BUTTON_SAVE_AS:
			// Сбивается рабочий каталог и сохранять по умолчанию бессмысленно
			/*
			if(model.Save(true,hdwnd))
			{
				flag_model_changed=false;
				EnableWindow( GetDlgItem( hdwnd, IDC_BUTTON_SAVE ), FALSE);
			}*/
			break;

		case IDC_BUTTON_LOAD:
			// Сбивается рабочий каталог и сохранять по умолчанию бессмысленно
			/*if(model.Load(hdwnd))
			{
				flag_model_changed=false;
				if(KChFstate::flag_kc_anytime) // Корректируем галочку в диалоге
					SendDlgItemMessage(hdwnd, IDC_CHECK_IGNORE_KC_INLINE, BM_SETCHECK, BST_UNCHECKED, 0);
				else 
					SendDlgItemMessage(hdwnd, IDC_CHECK_IGNORE_KC_INLINE, BM_SETCHECK, BST_CHECKED, 0);
				InputThread::Start(current_device_num,hdwnd); // выставить устройство, прописанное в файле
				EnableWindow( GetDlgItem( hdwnd, IDC_BUTTON_SAVE ), FALSE);
			}*/
			break;

		case IDC_CHECK_IGNORE_KC_INLINE:
			flag_model_changed=true;
			// Тут можно было как-то wparam контролировать, но не было под рукой справки
			if(BST_CHECKED==SendDlgItemMessage(hdwnd, IDC_CHECK_IGNORE_KC_INLINE, BM_GETCHECK, 0, 0))
				KChFstate::flag_kc_anytime=false;
			else
				KChFstate::flag_kc_anytime=true;

			break;

			//===== чекбоксы повтора

		case IDC_CHECK_REPEAT0:
			flag_model_changed=true;
			// Тут можно было как-то wparam контролировать, но не было под рукой справки
			if(BST_CHECKED==SendDlgItemMessage(hdwnd, IDC_CHECK_REPEAT0, BM_GETCHECK, 0, 0))
				KChFstate::SetRepeatKey(0,1);
			else
				KChFstate::SetRepeatKey(0,0);

			break;

		case IDC_CHECK_REPEAT1:
			flag_model_changed=true;
			// Тут можно было как-то wparam контролировать, но не было под рукой справки
			if(BST_CHECKED==SendDlgItemMessage(hdwnd, IDC_CHECK_REPEAT1, BM_GETCHECK, 0, 0))
				KChFstate::SetRepeatKey(1,1);
			else
				KChFstate::SetRepeatKey(1,0);

			break;

		case IDC_CHECK_REPEAT2:
			flag_model_changed=true;
			// Тут можно было как-то wparam контролировать, но не было под рукой справки
			if(BST_CHECKED==SendDlgItemMessage(hdwnd, IDC_CHECK_REPEAT2, BM_GETCHECK, 0, 0))
				KChFstate::SetRepeatKey(2,1);
			else
				KChFstate::SetRepeatKey(2,0);

			break;

		case IDC_CHECK_REPEAT3:
			flag_model_changed=true;
			// Тут можно было как-то wparam контролировать, но не было под рукой справки
			if(BST_CHECKED==SendDlgItemMessage(hdwnd, IDC_CHECK_REPEAT3, BM_GETCHECK, 0, 0))
				KChFstate::SetRepeatKey(3,1);
			else
				KChFstate::SetRepeatKey(3,0);

			break;
			//=====

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
		
		case IDC_KBD0:
			if(CBN_SELCHANGE==HIWORD(wparam)) // Выбрали другую клавишу
			{
				KChFstate::SetKeyToPress(0,dlg_scancodes[SendDlgItemMessage(hdwnd,IDC_KBD0, CB_GETCURSEL, 0, 0L)].value); 
				flag_model_changed=true;
				return 1;
			}
			else return 0;
			break; // для поддержания общего стиля

		case IDC_KBD1:
			if(CBN_SELCHANGE==HIWORD(wparam)) // Выбрали другую клавишу
			{
				KChFstate::SetKeyToPress(1,dlg_scancodes[SendDlgItemMessage(hdwnd,IDC_KBD1, CB_GETCURSEL, 0, 0L)].value); 
				flag_model_changed=true;
				return 1;
			}
			else return 0;
			break; // для поддержания общего стиля

		case IDC_KBD2:
			if(CBN_SELCHANGE==HIWORD(wparam)) // Выбрали другую клавишу
			{
				KChFstate::SetKeyToPress(2,dlg_scancodes[SendDlgItemMessage(hdwnd,IDC_KBD2, CB_GETCURSEL, 0, 0L)].value); 
				flag_model_changed=true;
				return 1;
			}
			else return 0;
			break; // для поддержания общего стиля

		case IDC_KBD3:
			if(CBN_SELCHANGE==HIWORD(wparam)) // Выбрали другую клавишу
			{
				KChFstate::SetKeyToPress(3,dlg_scancodes[SendDlgItemMessage(hdwnd,IDC_KBD3, CB_GETCURSEL, 0, 0L)].value); 
				flag_model_changed=true;
				return 1;
			}
			else return 0;
			break; // для поддержания общего стиля

		case IDC_KBD4:
			if(CBN_SELCHANGE==HIWORD(wparam)) // Выбрали другую клавишу
			{
				KChFstate::SetKeyToPress(4,dlg_scancodes[SendDlgItemMessage(hdwnd,IDC_KBD4, CB_GETCURSEL, 0, 0L)].value); 
				flag_model_changed=true;
				return 1;
			}
			else return 0;
			break; // для поддержания общего стиля

		case IDC_KBD5:
			if(CBN_SELCHANGE==HIWORD(wparam)) // Выбрали другую клавишу
			{
				KChFstate::SetKeyToPress(5,dlg_scancodes[SendDlgItemMessage(hdwnd,IDC_KBD5, CB_GETCURSEL, 0, 0L)].value); 
				flag_model_changed=true;
				return 1;
			}
			else return 0;
			break; // для поддержания общего стиля

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
	KCWindow(); // Это тоже
#endif

	// Диалог теперь - немодальный. Ибо в режиме суперпользователя появляется ещё одно окно
	//DialogBox(hInst,MAKEINTRESOURCE(IDD_DIALOG1),NULL,(DLGPROC)DlgWndProc);
	hdwnd=CreateDialog(hInst, MAKEINTRESOURCE(IDD_DIALOG1),NULL,(DLGPROC)DlgWndProc);
	ShowWindow(hdwnd,SW_SHOWNORMAL);

	// Грузим настройку по умолчанию
	if(model.Load(NULL,L"default.MM1"))
	{
		// Отображаем, какие клавиши будут нажаты 
		SetKeysInDialogue();

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