#include <Windows.h>
#include <process.h>
#include <math.h>
#include "OutputThread.h"
#include "WorkerThread.h"

#define PI 3.1415926535

HWAVEOUT OutputThread::device=NULL;
uintptr_t OutputThread::output_thread_handle=NULL;
WAVEHDR OutputThread::WaveHeader[MM_NUM_OUTPUT_BUFFERS]={0};
short OutputThread::buf[MM_NUM_OUTPUT_BUFFERS][MM_SOUND_BUFFER_LEN]; // это ~25 мс при 16кГц

static volatile bool flag_ShutDownOutputThread=false; // флаг информирует поток о том, что нужно выключиться
static volatile bool flag_DoNotWriteBuffers=false; // флаг информирует поток о том, что устройство отсоединяется, больше писать не надо!!!
static volatile HANDLE hOutputSoundDataReady=NULL; // Событие с автосбросом

//==================================================
// Поток, выводящий звук на звуковую карту
//==================================================
unsigned __stdcall WaveOutThread(void *p)
{
	while(!flag_ShutDownOutputThread)
	{
		WaitForSingleObject(hOutputSoundDataReady,INFINITE);
		OutputThread::OnSoundData();
	}
	return 0;
}

//===============================================================================
// Создаёт поток и начинает выкладывать блоки данных на устройство воспроизведения по умолчанию
// HWND нужно для привязки сообщений об ошибках к родительскому окну
//===============================================================================
int OutputThread::Start( HWND hdwnd)
{
	// 1. Запустим поток, если не сделали это раньше..
	// 1.1. Событие создадим
	if(!hOutputSoundDataReady)
	{
		hOutputSoundDataReady=CreateEvent(0, FALSE, FALSE, 0);
	}

	// 1.2. Сам поток
	if(!output_thread_handle)
	{
		output_thread_handle=_beginthreadex(NULL,0,WaveOutThread,0,0,NULL);
	}

	// 2. Если использовали устройство - закончить его использовать (быть того не может!)
	if(device)
	{
		CleanUpDevice(hdwnd);
		device=NULL; // CleanUpDevice это делает, но перестрахуемся...
	}

	// 3. Сызнова подключаемся к звуковой карте в нужном формате
	return OpenDevice(hdwnd);
	
}


//===============================================================================
// Начинаем работу со звуковой картой (в смысле воспроизведения)
// HWND нужно для привязки сообщений об ошибках к родительскому окну
//===============================================================================
int OutputThread::OpenDevice(HWND hdwnd)
{
	int i, k=0;
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
	mres = waveOutOpen(&device, 0, &WaveFormat, (DWORD)hOutputSoundDataReady, 0, CALLBACK_EVENT);
	if (mres) { MessageBox(hdwnd,L"Формат воспроизведения 16кГц 16 бит НЕ подддерживается!",L"Поддержка формата",MB_OK);	return 1;}

	// 2. Добавляем буферы
	for(i=0;i<MM_NUM_OUTPUT_BUFFERS;i++)
	{
		// 2.1. Сначала заполняем заголовок
		WaveHeader[i].lpData=(LPSTR)buf[i];
		WaveHeader[i].dwBufferLength=MM_SOUND_BUFFER_LEN*sizeof(short);
		
		mres=waveOutPrepareHeader(device, &WaveHeader[i], sizeof(WAVEHDR));
		if (mres) 
		{
			MessageBox(hdwnd,L"не отработал waveOutPrepareHeader",L"не отработал waveOutPrepareHeader",MB_OK);
			return 1;
		}

//#ifdef _DEBUG
		// 2.15 Для проверки звука
		// buf[i][1]=0x01;
		// buf[i][1]=0x10;

/*		// 2.16 Для проверки звука
		int j;
		
		for(j=0;j<MM_SOUND_BUFFER_LEN;j++)
		{
			buf[i][j]=30000*sin(38*PI*k/MM_SOUND_BUFFER_LEN*2);
			k++;
		}
*/		
//#endif
	}

	for(i=0;i<MM_NUM_OUTPUT_BUFFERS;i++)
	{
		// 2.2. А уж теперь воспроизводим наши буферы (оба пустые)
		mres=waveOutWrite(device, &WaveHeader[i], sizeof(WAVEHDR));
		if (mres)
		{
			// Тут некий глюк - при повторном
			MessageBox(hdwnd,L"не отработал waveOutWrite",L"не отработал waveOutWrite",MB_OK);
			return 1;
		}
	}

	// С лёгким паром!
	return 0;
}

//===============================================================================
// Когда звуковая карта больше не нужна для воспроизведения
// HWND нужно для привязки сообщений об ошибках к родительскому окну
//===============================================================================
void OutputThread::CleanUpDevice(HWND hdwnd)
{
	MMRESULT mres;
	int i;

	if(device)
	{
		// Первым делом нужно, чтобы поток, который выстреливает звук, перестал это делать!
		flag_DoNotWriteBuffers=true;

		// The waveInReset function stops input on the given waveform-audio input device and resets the current position to zero. 
		// All pending buffers are marked as done and returned to the application.
		mres=waveOutReset(device);
		if (mres) MessageBox(hdwnd,L"не отработал waveOutReset",L"не отработал waveOutReset",MB_OK);

		// Буферов 2, но... вдруг больше будет
		for(i=0;i<MM_NUM_OUTPUT_BUFFERS;i++)
		{
			mres=waveOutUnprepareHeader(device, &WaveHeader[i], sizeof(WAVEHDR));
			if (mres) MessageBox(hdwnd,L"не отработал waveOutUnprepareHeader",L"не отработал waveOutUnprepareHeader",MB_OK);
			ZeroMemory(&WaveHeader[i],sizeof(WaveHeader[i])); // Вот без этого повторная инициализация глючила, потому что
			// Event приходил ещё до того, как были подготовлены буферы, и OnSoundData поперёк батьки делала waveOutWrite()
		}

		mres=waveOutClose(device);
		if (mres) MessageBox(hdwnd,L"не отработал waveOutClose",L"не отработал waveOutClose",MB_OK);

		// Теперь уже можно снова добавлять
		flag_DoNotWriteBuffers=false;

		device=NULL;
	}
}


//===============================================================================
// Здесь будет самое главное
//===============================================================================
void OutputThread::OnSoundData()
{
	int i, duplex=0;
	static int count=0;
	MMRESULT mres;

	for(i=0;i<MM_NUM_OUTPUT_BUFFERS;i++)
	{
		if(WHDR_DONE&WaveHeader[i].dwFlags)
		{
			
#ifdef _DEBUG
			duplex++;
			if(2==duplex) OutputDebugString(L"-= D U P L E X =-\r\n"); // Сразу два буфера готовы

			// Для отладки будем писать каждую секунду слово -sec-
			count++;
			if(count>=40)
			{
				count=0;
				//OutputDebugString(L"-OSec-\r\n");
			}
#endif


			// Вынуть данные из предыдущего потока
			if(WorkerThread::PullData(buf[i]))
			{
				// Данные не готовы.... заполним буфер нулями?
#ifdef _DEBUG
				 OutputDebugString(L"-O0000000000000000-\r\n");
#endif
			}

			// Снова добавить буфер в рециркулятор (но только если мы не завершаемся)
			if(!flag_DoNotWriteBuffers)
			{
				mres=waveOutWrite(device, &WaveHeader[i], sizeof(WAVEHDR));
#ifdef _DEBUG
				if (mres) OutputDebugString(L"-M.R.E.S.-\r\n");
				//if(0==i)  OutputDebugString(L"o"); else OutputDebugString(L"i");
#endif			
			}

		}
	}

#ifdef _DEBUG
	// А был ли буфер?
	if(0==duplex)
		OutputDebugString(L"BAD FLAGS\r\n");
#endif
}

//===============================================================================
// Выключить все электроприборы перед выходом из программы
//===============================================================================
void OutputThread::Halt(HWND hdwnd)
{
	// 1. Закончим получать данные от звуковой карты
	CleanUpDevice(hdwnd);

	// 2. Завершим поток
	flag_ShutDownOutputThread=true;
	// А вдруг он ждёт своего события?
	SetEvent(hOutputSoundDataReady);
	
	// 3. Отключим хендл
	CloseHandle(hOutputSoundDataReady);
	hOutputSoundDataReady=NULL;

	// 4. Для очистки совести дождёмся окончания потока?
	WaitForSingleObject((HANDLE)output_thread_handle,0);
	output_thread_handle=NULL;

}
