#ifndef __MM_OUTPUT_THREAD
#define __MM_OUTPUT_THREAD

#include "MMGlobals.h"

#define MM_NUM_OUTPUT_BUFFERS 4

class OutputThread
{
public:
	static int Start(HWND hdwnd); // Создаёт поток и начинает выкладывать блоки данных на устройство воспроизведения по умолчанию
	static void Halt(HWND hdwnd); // Чистим за собой
	static void OnSoundData(); // В поток пришла повесточка от звуковой карты
	static void Pause(HWND hdwnd) {CleanUpDevice(hdwnd);}
protected:
	static int OpenDevice(HWND hdwnd); // Начинаем работу со звуковой картой
	static void CleanUpDevice(HWND hdwnd); // Освобождаем звуковую карту
	
	static HWAVEOUT device;
	static WAVEHDR WaveHeader[MM_NUM_OUTPUT_BUFFERS];
	static short buf[MM_NUM_OUTPUT_BUFFERS][MM_SOUND_BUFFER_LEN];
	
	static uintptr_t output_thread_handle;
};

#endif