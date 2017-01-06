#ifndef __MM_INPUT_THREAD
#define __MM_INPUT_THREAD


#include "MMGlobals.h"

#define MM_NUM_INPUT_BUFFERS 2

class InputThread
{
public:
	static int Start(UINT device_num, HWND hdwnd); // Создаёт поток и начинает заглатывать блоки данных с устройства записи звука
	static void Halt(HWND hdwnd); // Чистим за собой
	static void OnSoundData(); // В поток пришла повесточка от звуковой карты

protected:
	static int OpenDevice(UINT device_num, HWND hdwnd); // Начинаем работу со звуковой картой
	static void CleanUpDevice(HWND hdwnd); // Освобождаем звуковую карту
	
	static HWAVEIN device;
	static WAVEHDR WaveHeader[MM_NUM_INPUT_BUFFERS];
	static short buf[MM_NUM_INPUT_BUFFERS][MM_SOUND_BUFFER_LEN];
	
	static uintptr_t input_thread_handle;
};

#endif