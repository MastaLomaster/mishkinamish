#ifndef __MM_WORKER_THREAD
#define __MM_WORKER_THREAD

#include "MMGlobals.h"

#define MM_NUM_WORKER_OUTPUT_BUFFERS 4

class WorkerThread
{
public:
	static void Start(HWND hdwnd=0); // Создаёт поток и ждёт блоков данных от InputThread
	static void Halt(HWND hdwnd=0); // Чистим за собой
	static void Work(); // отдать в переработку
	

	static void PushData(short *_buf); // Скармливаем worker thread порцию данных
	static int PullData(short *_buf); // Отдаёт данные, если готовы (готовы returns 0)
	
	static volatile int indicator_value; // принимает значение от 0 до 5 в зависимости от уровния сигнала, выводится индикатором
	static volatile int training_silence_indicator; // какой была мощность в паузе перед тренировкой?
protected:

	static uintptr_t worker_thread_handle;

	static short input_buf[MM_SOUND_BUFFER_LEN];
	static char flag_input_buffer_ready;

	static short output_buf[MM_NUM_WORKER_OUTPUT_BUFFERS][MM_SOUND_BUFFER_LEN];

	static int last_read_buffer;
	static int last_write_buffer;


};

#endif