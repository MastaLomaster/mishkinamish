#include <Windows.h>
#include <process.h>
#include <math.h>
#include "CopyShmopy.h"
#include "WorkerThread.h"
#include "WAVLoader.h"
#include "KChFstate.h"
#include "ClickSound.h"
#include "WAVDump.h"

#define MM_WORKER_BUFFERS_IN_CIRCLE 4

extern bool f_reading_file; // определена в InputThread

extern volatile bool flag_wav_dump; // [29-DEC]
extern volatile bool flag_training_mode;
extern volatile long training_frame_counter;
int  volatile WorkerThread::training_silence_indicator;

static volatile bool flag_ShutDownWorkerThread=false; // флаг информирует поток о том, что нужно выключиться
volatile HANDLE hInput2WorkerDataReady=NULL; // Событие с автосбросом

static volatile LONG num_free_buf=MM_WORKER_BUFFERS_IN_CIRCLE; // количество свободных буферов в кольце

int WorkerThread::last_read_buffer=0;
int WorkerThread::last_write_buffer=0;

int  volatile WorkerThread::indicator_value; // принимает значение от 0 до 5 в зависимости от уровния сигнала, выводится индикатором

uintptr_t WorkerThread::worker_thread_handle=NULL;

short WorkerThread::input_buf[MM_SOUND_BUFFER_LEN];
char WorkerThread::flag_input_buffer_ready=true;

short WorkerThread::output_buf[MM_NUM_WORKER_OUTPUT_BUFFERS][MM_SOUND_BUFFER_LEN];

// Выставляется в другом месте, а мы внедряем клик(и) в поток [27-DEC]
int volatile g_click_sound=0;
extern volatile bool flag_keep_silence; // [27-DEC] - отмена отключения устройства воспроизведения. звук нужен для кликов. теперь обнуляем буфер, когда нужна тишина.

//==================================================
// Рабоче-крестьянский поток
//==================================================
unsigned __stdcall FuncWorkerThread(void *p)
{
	while(!flag_ShutDownWorkerThread)
	{
		WaitForSingleObject(hInput2WorkerDataReady,INFINITE);
		WorkerThread::Work();
	}
	return 0;
}

//===============================================================================
// HWND нужно для привязки сообщений об ошибках к родительскому окну
//===============================================================================
void WorkerThread::Start(HWND hdwnd)
{
	// 1. Запустим поток, если не сделали это раньше..
	// 1.1. Событие создадим
	if(!hInput2WorkerDataReady)
	{
		hInput2WorkerDataReady=CreateEvent(0, FALSE, FALSE, 0);
	}

	// 1.2. Сам поток
	if(!worker_thread_handle)
	{
		worker_thread_handle=_beginthreadex(NULL,0,FuncWorkerThread,0,0,NULL);
	}
}

//===============================================================================
// Выключить все электроприборы перед выходом из программы
//===============================================================================
void WorkerThread::Halt(HWND hdwnd)
{
	// 1. Завершим поток
	flag_ShutDownWorkerThread=true;
	// А вдруг он ждёт своего события?
	SetEvent(hInput2WorkerDataReady);
	
	// 2. Отключим хендл
	CloseHandle(hInput2WorkerDataReady);
	hInput2WorkerDataReady=NULL;

	// 3. Для очистки совести дождёмся окончания потока?
	WaitForSingleObject((HANDLE)worker_thread_handle,0);
	worker_thread_handle=NULL;
}

//===============================================================================
// ВСкармливаем worker thread порцию данных
// Вызывается со стороны InputThread
//===============================================================================
void WorkerThread::PushData(short *_buf)
{
	if(flag_input_buffer_ready) // место свободно
	{
		// 1. Забираем данные себе
		memcpy(input_buf,_buf,MM_SOUND_BUFFER_LEN*sizeof(short));

		// 2. Сами себе запрещаем ещё раз заполнять буфер
		// Пока Work() не сделает всё до конца
		flag_input_buffer_ready=false;

		// 3. Открываем шлагбаум потоку
		if(hInput2WorkerDataReady)
			SetEvent(hInput2WorkerDataReady);
	}
	else //увеличить счётчик пропущенных блоков
	{
#ifdef _DEBUG
		OutputDebugString(L"-I.N>P>U>T>=SKIPPED-\r\n");
#endif
		// !!! Вот здесь был косяк. [21-DEC]
		// Если поток WorkerThread инициализировался уже после того, как  было сгенерировано первое событие SetEvent(hInput2WorkerDataReady),
		// то он никогда не выходил из ступора!!!
		// Вставил сюда SetEvent(hInput2WorkerDataReady), хотя достаточно поменять порядок запуска потоков
		SetEvent(hInput2WorkerDataReady);
	}
}

//===============================================================================
// Забираем из worker thread порцию данных
// Вызывается со стороны OutputThread
//===============================================================================
int WorkerThread::PullData(short *_buf)
{
	if(MM_WORKER_BUFFERS_IN_CIRCLE > num_free_buf) // какие-то данные в буфере есть, можно забирать
	{
		// А свой личный указатель на записанную ячейку - не атомарно, ибо только мы его изменяем
		last_read_buffer++;
		if(last_read_buffer>=MM_NUM_WORKER_OUTPUT_BUFFERS)
			last_read_buffer=0;

		// 2. Отдаём данные, куда там вам нужно...
		memcpy(_buf, output_buf[last_read_buffer], MM_SOUND_BUFFER_LEN*sizeof(short));

		// 3 - перенесено из 1!!!
		// Ибо Work() мог захотеть записать данные ещё до того,
		// как мы реально освободили буфер !!!!
		// Атомарно увеличиваем num_free_buf (гарантированно не превышая MM_WORKER_BUFFERS_IN_CIRCLE,
		// ибо другой поток может только уменьшать num_free_buf)
		InterlockedIncrement(&num_free_buf);

		// 2.5. Поддержка чтения из файла
		if(f_reading_file) 
			SetEvent(hInput2WorkerDataReady);

		// 3. подтверждаем, что скопировали
		return 0;
	}
	else // ещё потом увеличить счётчик пропущенных блоков
		return 1;

	
}

//===============================================================================
// worker thread обрабатывает порцию данных
// Вызывается после того, как поднялся шлагбаум прихода нового звука
// Ну и, естественно, когда не занят обработкой предыдущего кадра
//===============================================================================
void WorkerThread::Work()
{
	// когда-нибудь здесь будет что-нибудь более осмысленное
	// а пока просто копируем из входного буфера в выходной [набор буферов]
	// Правда, если выходной буфер уже опустошен...

	// Записать можно лишь тогда, когда количество свободных буферов >0
	// (здесь не нужно атомарное считывание, ибо только мы сами [Эта ф-ция вызывается из WorkerThread] можем уменьшать количество свободных буферов,
	// а другой поток [ф-ция PullData вызывается из OutputThread] может лишь увеличивать это число)

	// 21.12.2016 Когда выходной поток остановлен, обработка звука прекращалась. Поэтому теперь в любом случае пишем в буфер,
	// просто перезаписывая последнюю область output_buf[last_write_buffer], хотя это и некрасиво (при ВРЕМЕННЫХ тормозах вывода может исказить звук?)
	bool skipped=true; // пишем в старое место, если буфер забит
	if(num_free_buf >0) // ой, нет, не забит!
	{
		skipped=false; 
		// А свой личный указатель на записанную ячейку - не атомарно, ибо только мы его изменяем
		last_write_buffer++;
		if(last_write_buffer>=MM_NUM_WORKER_OUTPUT_BUFFERS)
			last_write_buffer=0;
	}
		
		
		// 2. Теперь не просто копируем!!! А обрабатываем в CopyShmopy!!!
		if(f_reading_file) MMWAVLoader::FillBuffer(input_buf); // При работе с файлом вручную заполняем input_buf
		
		// 3. Находим максимальный уровень сигнала для индикатора
		//long maxvalue=0;
		// Теперь считаем логарифм среднего квадрата (не спрашивай, почему. где-то что-то слышал, вроде работает).
		double average_value=0.0;
		for(int i=0;i<MM_SOUND_BUFFER_LEN;i++)
		{
			average_value+=input_buf[i]*input_buf[i];
			//if(abs(input_buf[i])>maxvalue) maxvalue=abs(input_buf[i]);
		}
		average_value/=MM_SOUND_BUFFER_LEN;

		// реальное значение для вывода на экран должно быть от 0 до 7
		// А логарафм log(SHRT_MAX^2) ~= 20.8
		
		double l=log((double)average_value); // от 1 до 20.8

		// Мы показываем только 8 верхних из 11 (превращенные в 0..7)
		indicator_value=(int)(l/1.9)-3; 
		if(indicator_value<0) indicator_value=0;
		if(indicator_value>7) indicator_value=7; // страховка

		// 3.1 При трейнинге нужно сбросить в 0 training_silence_indicator в начале тренировки
		// 17-DEC
		if(flag_training_mode)
		{
			if(0==training_frame_counter) 
				training_silence_indicator=0;

			// Четверть секунды просто ждём, когда утихнет клик мышиный...
			// А потом вникаем в то, что есть уровень тишины. (3/4 секунды слушаем тишину)
			// Были глюки (всплески). Пришлось считать среднее
			if((training_frame_counter>=10)&&(training_frame_counter<40)) 
			{
				//if(training_silence_indicator<indicator_value) 
					training_silence_indicator+=indicator_value;
			}
			if(40==training_frame_counter) // Пора подбить бабки в вычислении среднего!
				training_silence_indicator/=30;

			// Это единственное место, где увеличивается счетчик фреймов для тренировки
			training_frame_counter++;
		}

		// 3.2. Отфильтровывание коротких звуков ("К" и "Ч") [18-DEC]
		KChFstate::NewFrame(indicator_value);

		// 3.3. Дамп в файл
		if(flag_wav_dump) flag_wav_dump=MMWAVDump::DumpBuffer(input_buf,sizeof(input_buf)); // [29-DEC]

		// 4. Скармливаем обработчику, который ещё и разбивает на куски со сдвигом 160
		CopyShmopy::Process(output_buf[last_write_buffer],input_buf);	
		// Было так до появления обработчика CopyShmopy- 2. просто копируем
		//memcpy(output_buf[last_write_buffer],input_buf,MM_SOUND_BUFFER_LEN*sizeof(short));	

		// 4.1. [27-DEC] Новая концепция - устройство воспроизведения не отключается (из-за необходимости звуков кликов)
		if(flag_keep_silence) ZeroMemory(output_buf[last_write_buffer],sizeof(output_buf[last_write_buffer]));

		// 4.5. [27-DEC] Добавляем звук нажатия на мышь
		if(g_click_sound)
		{
			ClickSound::AddSound(output_buf[last_write_buffer],g_click_sound);
			g_click_sound=0;
		}

	if(!skipped) // Начиная с 21.12.2016 буфер может быть перезаписан, и тогда num_free_buf не уменьшается
	{
		// 5. Перенесено из 1. Ибо OutputThread мог захотеть считать данные ещё до того,
		// как мы реально их записали в output_buf !!!!
		// Корректируем счётчики. num_free_buf - атомарно
		InterlockedDecrement(&num_free_buf);
	}
	else // увеличиваем счетчик пропущенных блоков
	{
#ifdef _DEBUG
				OutputDebugString(L"-W.O.R.K.E.R==SKIPPED-\r\n");
#endif
	}

	// в любом случае разрешаем записывать данные во входной буфер
	flag_input_buffer_ready=true;
	
	// При чтении из файла и наличии свободного места - сами себе разрешаем запуститься ещё раз
	if(f_reading_file && (num_free_buf >0)) SetEvent(hInput2WorkerDataReady);
}