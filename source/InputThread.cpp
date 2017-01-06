#include <Windows.h>
#include <process.h>
#include "InputThread.h"
#include "WorkerThread.h"
#include "WAVLoader.h"
#include "OutputThread.h"

HWAVEIN InputThread::device=NULL;
uintptr_t InputThread::input_thread_handle=NULL;
WAVEHDR InputThread::WaveHeader[MM_NUM_INPUT_BUFFERS]={0};
short InputThread::buf[MM_NUM_INPUT_BUFFERS][MM_SOUND_BUFFER_LEN]; // это ~25 мс при 16кГц

static volatile bool flag_ShutDownInputThread=false; // флаг информирует поток о том, что нужно выключиться
static volatile bool flag_DoNotAddBuffers=false; // флаг информирует поток о том, что устройство отсоединяется, буферы не добавлять!!!
static volatile HANDLE hInputSoundDataReady=NULL; // Событие с автосбросом

extern unsigned long   iNumDevs; // Эту переменную импользуем при InputThread::Start,чтобы понять, что выбрали файл
bool f_reading_file=false;
TCHAR *LoadWavFile(TCHAR *_filename, HWND hdwnd); // прототип из WAVLoader.cpp
extern short samples; // оттуда же, загруженный звук

extern volatile HANDLE hInput2WorkerDataReady; // Определено в WorkerThread

extern volatile bool flag_keep_silence; // [27-DEC] - отмена отключения устройства воспроизведения. звук нужен для кликов. теперь обнуляем буфер, когда нужна тишина.
//==================================================
// Поток, обрабатывающий сигналы от звуковой карты
//==================================================
unsigned __stdcall WaveInThread(void *p)
{
	while(!flag_ShutDownInputThread)
	{
		WaitForSingleObject(hInputSoundDataReady,INFINITE);
		InputThread::OnSoundData();
	}
	return 0;
}

//===============================================================================
// Начинает заглатывать блоки данных с устройства записи звука
// Номер устройства получен при помощи waveInGetNumDevs(), waveInGetDevCaps(...)
// HWND нужно для привязки сообщений об ошибках к родительскому окну
//===============================================================================
int InputThread::Start(UINT device_num, HWND hdwnd)
{
	// 1. Запустим поток, если не сделали это раньше..
	// 1.1. Событие создадим
	if(!hInputSoundDataReady)
	{
		hInputSoundDataReady=CreateEvent(0, FALSE, FALSE, 0);
	}

	// 1.2. Сам поток
	if(!input_thread_handle)
	{
		input_thread_handle=_beginthreadex(NULL,0,WaveInThread,0,0,NULL);
	}

	// 2. Если использовали устройство - закончить его использовать
	if(device)
	{
		CleanUpDevice(hdwnd);
		device=NULL; // CleanUpDevice это делает, но перестрахуемся...
	}

	// 2.5. Если читали файл, то перестаём это делать
	// Тут бы хорошо подождать, пока очистится буфер, ну да ладно, новый буфер появится ещё не скоро.
	// Пока диалог открытия файла вылезет, пока то да сё...
	// 21-12-2016 В Release-версии устройство воспроизведение выключается, если работаем не с файлом
	// Если опять выберем файл, оно включится.

	flag_keep_silence=true; // выключаем звук на время переключения

	if(f_reading_file)
	{
/*#ifndef _DEBUG
		OutputThread::Pause(hdwnd);
#endif */
		f_reading_file=false;
	}

	// 3. Сызнова подключаемся к звуковой карте в нужном формате или открываем файл
	if(device_num==iNumDevs)
	{
		if(MMWAVLoader::LoadWavFile(NULL,hdwnd)) // возвращает имя файла, если тот действительно был загружен
		{
			f_reading_file=true;
			flag_keep_silence=false; 
			

/*#ifndef _DEBUG
			OutputThread::Start(hdwnd);
#endif*/
			SetEvent(hInput2WorkerDataReady); // Надо дёрнуть стартёр, потому что WorkerThread::PullData() не отработает, если буфер пуст
			return 0;
		}
		else return 1;
	}
	else
	{
		// обычное устройство
		// Теперь воспроизведение через звуковую карту бывает только для файлов в Release-версии
#ifdef _DEBUG
		flag_keep_silence=false;
#else
		flag_keep_silence=true;
#endif
		return OpenDevice(device_num, hdwnd);
	}
}

//===============================================================================
// Начинаем работу со звуковой картой 
// HWND нужно для привязки сообщений об ошибках к родительскому окну
//===============================================================================
int InputThread::OpenDevice(UINT device_num, HWND hdwnd)
{
	int i;
	MMRESULT mres;
	WAVEFORMATEX WaveFormat;

	WaveFormat.wFormatTag      = WAVE_FORMAT_PCM;
	WaveFormat.nChannels       = 1; 
	WaveFormat.nSamplesPerSec  = 16000;
	WaveFormat.wBitsPerSample  = 16;
	WaveFormat.nAvgBytesPerSec = 32000;
	WaveFormat.nBlockAlign     = 2;
	WaveFormat.cbSize          = 0;

	// 1. Открываем выбранное устройство
	mres = waveInOpen(&device, device_num, &WaveFormat, (DWORD)hInputSoundDataReady, 0, CALLBACK_EVENT);
	if (mres) { MessageBox(hdwnd,L"Формат записи 16кГц 16 бит НЕ подддерживается!",L"Поддержка формата",MB_OK);	return 1;}

	// 2. Добавляем буферы
	for(i=0;i<MM_NUM_INPUT_BUFFERS;i++)
	{
		// 2.1. Сначала заполняем заголовок
		WaveHeader[i].lpData=(LPSTR)buf[i];
		WaveHeader[i].dwBufferLength=MM_SOUND_BUFFER_LEN*sizeof(short);
		
		mres=waveInPrepareHeader(device, &WaveHeader[i], sizeof(WAVEHDR));
		if (mres) 
		{
			MessageBox(hdwnd,L"не отработал waveInPrepareHeader",L"не отработал waveInPrepareHeader",MB_OK);
			return 1;
		}

		// 2.2. А уж теперь добавляем
		mres=waveInAddBuffer(device, &WaveHeader[i], sizeof(WAVEHDR));
		if (mres)
		{
			MessageBox(hdwnd,L"не отработал waveInAddBuffer",L"не отработал waveInAddBuffer",MB_OK);
			return 1;
		}
	}

	// 3. Погнали наши городских!
	mres = waveInStart(device);
	if (mres) { MessageBox(hdwnd,L"waveInStart!",L"waveInStart",MB_OK); return 1;}
	
	// С лёгким паром!
	return 0;
}

//===============================================================================
// Когда звуковая карта больше не нужна для записи
// HWND нужно для привязки сообщений об ошибках к родительскому окну
//===============================================================================
void InputThread::CleanUpDevice(HWND hdwnd)
{
	MMRESULT mres;
	int i;

	if(device)
	{
		// Первым делом нужно, чтобы поток, который возвращает буферы в работу, перестал это делать!
		flag_DoNotAddBuffers=true;

		// The waveInReset function stops input on the given waveform-audio input device and resets the current position to zero. 
		// All pending buffers are marked as done and returned to the application.
		mres=waveInReset(device);
		if (mres) MessageBox(hdwnd,L"не отработал waveInReset",L"не отработал waveInReset",MB_OK);

		// waveInStop(device);

		// Буферов 2, но... вдруг больше будет
		for(i=0;i<MM_NUM_INPUT_BUFFERS;i++)
		{
			mres=waveInUnprepareHeader(device, &WaveHeader[i], sizeof(WAVEHDR));
			if (mres) MessageBox(hdwnd,L"не отработал waveInUnprepareHeader",L"не отработал waveInUnprepareHeader",MB_OK);
		}

		mres=waveInClose(device);
		if (mres) MessageBox(hdwnd,L"не отработал waveInClose",L"не отработал waveInClose",MB_OK);

		// Теперь уже можно снова добавлять
		flag_DoNotAddBuffers=false;

		device=NULL;
	}
}

//===============================================================================
// Здесь будет самое главное
//===============================================================================
void InputThread::OnSoundData()
{
	int i, duplex=0;
	static int count=0;

	for(i=0;i<MM_NUM_INPUT_BUFFERS;i++)
	{
		if(WHDR_DONE&WaveHeader[i].dwFlags)
		{
			
#ifdef _DEBUG
			duplex++;
			if(2==duplex) OutputDebugString(L"D U P L E X\r\n"); // Сразу два буфера готовы

			// Формально тут можно проверить, что число считанных байт = 820
			if(WaveHeader[i].dwBytesRecorded!=MM_SOUND_BUFFER_LEN*sizeof(short))
			{
				OutputDebugString(L"***\r\n"); 
			}

			// Для отладки будем писать каждую секунду слово -sec-
			count++;
			if(count>=40)
			{
				count=0;
				//OutputDebugString(L"-Sec-\r\n");
			}
#endif

			// Скопировать в буфер следующего потока
			WorkerThread::PushData(buf[i]);

			// Снова добавить буфер в рециркулятор (но только если мы не завершаемся)
			if(!flag_DoNotAddBuffers)
				waveInAddBuffer(device, &WaveHeader[i], sizeof(WAVEHDR));

		}
	}
}

//===============================================================================
// Выключить все электроприборы перед выходом из программы
//===============================================================================
void InputThread::Halt(HWND hdwnd)
{
	// 1. Закончим получать данные от звуковой карты
	CleanUpDevice(hdwnd);

	// 2. Завершим поток
	flag_ShutDownInputThread=true;
	// А вдруг он ждёт своего события?
	SetEvent(hInputSoundDataReady);
	
	// 3. Отключим хендл
	CloseHandle(hInputSoundDataReady);
	hInputSoundDataReady=NULL;

	// 4. Для очистки совести дождёмся окончания потока?
	WaitForSingleObject((HANDLE)input_thread_handle,0);
	input_thread_handle=NULL;

}