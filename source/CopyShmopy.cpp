#include <Windows.h>
#include <math.h>
#include "ipp.h"

#include "MMGlobals.h"
#include "casphinx.h"
#include "MModel.h"
#include "CopyShmopy.h"
#include "WorkerThread.h"

bool flag_sphinx_legacy=false; // Используем ли старый кривоватый алгоритм Сфинкса для вычисления DCT?
int g_add_mouse_speed=0;

#ifdef MM_SUPERUSER
extern HWND exported_ogl_hwnd;
#endif

extern volatile bool flag_training_mode;
extern volatile long training_frame_counter;
extern volatile long training_mfcc_recorded;
extern volatile int training_sound;
extern void StopTrainingMode();

extern MModel model;
LONG volatile sounds_found[6]={0}; // самая главная переменная во всей программе

float CopyShmopy_minmax[13][2]={0};
static bool flag_minmax_reset=true;

extern bool f_reading_file; // определена в InputThread
extern bool flag_pause; // определена в WAVLoader
extern float super_prev; // для pre-emphasis при зацикливании

static float InputDoubleBuffer[MM_SOUND_BUFFER_LEN*2]={0};
static float OutputDoubleBuffer[MM_SOUND_BUFFER_LEN*2]={0};
static int input_offset=MM_SOUND_BUFFER_LEN;
static int output_offset=0;

static Ipp32f buf_in[DFT_SIZE]={0}; // Теперь это не комплексные числа!!!
static Ipp32f buf_out_balovstvo[DFT_SIZE+2]={0}; // Чтобы сюда inline преобразовать (формат CCS)
static Ipp32f buf_out_mfcc[DFT_SIZE+2]={0}; // один буфер - для баловства со сдвигом спектра, другой - для mfcc

static float buf_cas_MEL[40]={0}; // для представления спектра мощности в MEL-координатах
static float buf_cas_cep[13]={0}; // для 13 кепстральных коэффициентов

mfcc_t mfcc_buffer1[MM_MFCC_BUFFER_LEN]={0};
mfcc_t mfcc_buffer2[MM_MFCC_BUFFER_LEN]={0};
bool volatile flag_mfcc_buffer2_empty=true;
static int mfcc_pointer=0;
mfcc_t master_mfcc;

// 1. Указатели на буферы, которые мы потом будем использовать в IPP
static   IppsDFTSpec_R_32f *pDFTSpec=0;
static	Ipp8u  *pDFTInitBuf, *pDFTWorkBuf;

static IppsDCTFwdSpec_32f *ppDCTSpec=0; // Бред какой-то с DCT
static	Ipp8u  *pDCTInitBuf, *pDCTWorkBuf, *pDCTSpec;


bool CopyShmopy::initialized=false;

//-------------------------------------------------------------------------------------------------
// Тута чиста посмотреть на этот ваш пре-эмфасис
//-------------------------------------------------------------------------------------------------
static const float pre_emphasis_alpha=0.97f;

float cas_pre_emphasis(float *frame, float prev_emp)
{
    int i;
	float new_prev_emp=frame[159];
	
    // Тут пришлось перебирать в обратном порядке, чтобы оперировать неизменёнными данными (ибо работаем в одном буфере)
    for (i = MM_SOUND_BUFFER_LEN-1; i >0; i--)
	{
        frame[i] -= frame[i - 1] * pre_emphasis_alpha;
	}
	frame[0] -= prev_emp * pre_emphasis_alpha;
	return new_prev_emp;

}

void CopyShmopy::Process(short *dest, short *src)
{
	int i;
	float *magic_data; //, divide_by_N=1.0f;
	static float prev=0.0f;
	bool frame_border;

	//=====================================================================================
	// Пока оставим здесь такую затычку
	//if(MM_SOUND_BUFFER_LEN==size) memcpy(dest,src,size*sizeof(short));
	//memcpy(dest,src,size*sizeof(short));
	//return;
	//=====================================================================================

	
	// 1. Кусок по приёму со сдвигом 160 см. 24-nov-16
	// 1.1. Копируем во вторую половину буфера (заодно конвертируем из short во float):
	// 1.1.1. [пока под вопросом] Если ввод НЕ с микрофона, нужно поделить всё на DFT_SIZE !!! Иначе будет слишком громко !!!
	//if(f_reading_file) divide_by_N=(float)DFT_SIZE; else divide_by_N=1.0f;
	for(i=0;i<MM_SOUND_BUFFER_LEN;i++)
	{
		InputDoubleBuffer[MM_SOUND_BUFFER_LEN+i]=(float)src[i]; ///divide_by_N;
	}

	// 1.2. Разгребаем, пока есть что 
	while(input_offset<=MM_SOUND_BUFFER_LEN)
	{
		// Дополнение от [13-DEC]
		if(MM_SOUND_BUFFER_LEN==input_offset)
		{
			prev=super_prev; // берём реальное значение из предыдущего фрейма, а не из себя зацикленного [14-DEC]
			frame_border=true;
		}
		else frame_border=false;

		// 1.3. Отфурьируем
		// Вот здесь была ошибка. У нас Фурье обрабатывает DFT_SIZE отсчётов, а мы давали ему указатель на 
		// буфер на 102 отсчета меньше. Пока со спектром не баловались - это было не заметно, а потом - вылезло.
		// magic_data=DoMagic(&InputDoubleBuffer[input_offset]);

		memcpy(buf_in,&InputDoubleBuffer[input_offset],MM_SOUND_BUFFER_LEN*sizeof(float));

		// пре-эмфасис - ну, так, низких частот поменьше становится.
		prev=cas_pre_emphasis(buf_in, prev);

		// Ещё добавим окно Хемминга!!!
		for(i=0;i<MM_SOUND_BUFFER_LEN;i++)
			buf_in[i]*=(0.53836-0.46164*cos(2*3.14159*i/(MM_SOUND_BUFFER_LEN-1)));

		// Один из двух вариантов должен быть закомментирован
		// Вариант 1 - сдвинуть спектр (для баловства) и вычислить MFCC (для дела) 
		// В случае зацикливания звука (flag_pause) и начала блока (frame_border) вычисленные MFCC-коэффициенты
		// запоминаем в master_mfcc
		magic_data=DoMagic(buf_in, frame_border && flag_pause);

		// Вариант 2
		// просто копируем то, что преэмфасили и отхемминговали, но усиливаем в 512 раз!!!
		// !!! Это не могло работать !!! так как magic_data - массив FLOAT!!!
		/*for(i=0;i<MM_SOUND_BUFFER_LEN;i++)
			buf_in[i]*=512.0f;
		magic_data=buf_in;*/

		// 1.4. Наложим слой в выходной буфер (если всё испеклось, то там заполним и dest)
		IntegrateMagic160(magic_data, dest);
	
		// 1.5. Сдвинем на 160
		input_offset+=160;
	}
	// 1.6. Сдвигаем вторую половину буфера в первую часть, смещение тоже корректируем
	memcpy(&InputDoubleBuffer[0],&InputDoubleBuffer[MM_SOUND_BUFFER_LEN],MM_SOUND_BUFFER_LEN*sizeof(float));
	input_offset-=MM_SOUND_BUFFER_LEN;

}

//====================================================================================================
// Накладывает блоки по схеме 90-70-90-70-90 (24-nov-16)
// Если достаточно наложил, выплёвываем готовый блок
//====================================================================================================
void CopyShmopy::IntegrateMagic160(float *magic_data, short *dest)
{
	int i;

	// Восходящий участок 90-160
	for(i=90;i<160;i++)
	{
		OutputDoubleBuffer[output_offset+i]+=magic_data[i]*(i-90)/70.0f;
	}
	// Нисходящий участок 250-320
	for(i=250;i<320;i++)
	{
		OutputDoubleBuffer[output_offset+i]+=magic_data[i]*(320-i)/70.0f;
	}
	// Ровный участок 160-250
	for(i=160;i<250;i++)
	{
		OutputDoubleBuffer[output_offset+i]=magic_data[i];
	}

	// В следующий раз надо бы писать сюда..
	output_offset+=160;
	// Однако, выдаём результат, если смещение >=410, сдвигаем и уменьшаем смещение
	if(output_offset>=MM_SOUND_BUFFER_LEN)
	{
		// !!! Здесь округление переделать !!!
		for(i=0;i<MM_SOUND_BUFFER_LEN;i++)
		{
			dest[i]=OutputDoubleBuffer[i]/(Ipp32f)DFT_SIZE;
		}
		
		// Сдвигаем вторую половину буфера в первую часть, смещение тоже корректируем
		memcpy(&OutputDoubleBuffer[0],&OutputDoubleBuffer[MM_SOUND_BUFFER_LEN],MM_SOUND_BUFFER_LEN*sizeof(float));
		output_offset-=MM_SOUND_BUFFER_LEN;

		// Важно! Обнуляем вторую половину буфера!
		ZeroMemory(&OutputDoubleBuffer[MM_SOUND_BUFFER_LEN],MM_SOUND_BUFFER_LEN*sizeof(float));
	}
}


//====================================================================================================
// Обрабатывает порцию в 410 отсчетов с наложением 160 (24-nov-16)
// то есть нам выдают уже в чистом виде данные для обработки
// Часть для MFCC была отработана в casphinx
// Если мы попали на границу фрейма, можно обновить master_mfcc [13-DEC]
//====================================================================================================
float *CopyShmopy::DoMagic(float *input_data, bool remember_master_mfcc)
{
	int i,k;
	IppStatus ippstatus;

	// 0. Проверка инициализации библиотеки Intel PPM
	if(!initialized) Init();
		
	// 1. Отфурьируем
	ippstatus=ippsDFTFwd_RToCCS_32f(input_data, buf_out_balovstvo, pDFTSpec, pDFTWorkBuf);

	// 2. Получим Power Spectrum
	for(i=0;i<=DFT_SIZE/2;i++)
		buf_out_mfcc[i]=buf_out_balovstvo[i*2]*buf_out_balovstvo[i*2]+buf_out_balovstvo[i*2+1]*buf_out_balovstvo[i*2+1];
	
	// 3. MEL-спектр!!!
	cas_mel_spec(buf_cas_MEL, buf_out_mfcc);
	
	// 4. Уже кепстр уже - теперь через Intel IPP
	// cas_mel_cep(buf_cas_MEL, buf_cas_cep);
	CS_mel_cep(buf_cas_MEL, buf_cas_cep);


/*	Это всё вынесено в функцию CS_mel_cep, т.к. этот код вызывается ещё и в конструкторе модели
	// 4.1 Логарифм
	for (i = 0; i < MEL_NUM_FILTERS; i++)
	{
        buf_cas_MEL[i] = (float)(log(buf_cas_MEL[i] + 1e-4)); // #define LOG_FLOOR 1e-4 - так было в безвременно ушедшем Сфинксе
    }
	// 4.1.5 Для legacy:
	if(flag_sphinx_legacy) buf_cas_MEL[0]/=2.0f;

	// 4.2 DCT в тот же буфер
	ippsDCTFwd_32f_I(buf_cas_MEL,  ppDCTSpec, pDCTWorkBuf);
	
	// 4.3 Копируем на выход только 13
	for (i = 0; i < MEL_NUM_CEPSTRA; i++)
	{
		buf_cas_cep[i]=buf_cas_MEL[i];
		// Для legacy домножаем
		if(flag_sphinx_legacy)
		{
			buf_cas_cep[i]/=sqrt (2.0*MEL_NUM_FILTERS);
			if(0==i) buf_cas_cep[i]*=sqrt(2.0);
		}
	}
*/
	// 4.5. Заполняем master_mfcc [13-DEC]
	if(remember_master_mfcc) master_mfcc=*((mfcc_t *)buf_cas_cep); // Те же 13 float, но тип другой

	// 4.6. Либо тренируемся, либо определяем, наш ли это звук?
	if(flag_training_mode)
	{
		if(training_frame_counter>=40)
		{
			if((WorkerThread::training_silence_indicator+1<WorkerThread::indicator_value)) // Пришла пора записывать, если уровень достаточен (тишина +2 деления)
			{
				// Нужно создать (!) mfcc, скопировать в него значения, а потом уж добавить в модель
				mfcc_t *new_mfcc = new mfcc_t;
				*new_mfcc = *((mfcc_t *)buf_cas_cep);
				model.AddSound(0,training_sound,0,new_mfcc);

				training_mfcc_recorded++;
				// Для звуков "к" и "Ч" тренировка короче (сейчас одинакова для обоих, но может быть разная)
				if((training_mfcc_recorded>MM_NUM_MFCC_IN_TRAINING)
					||((training_sound==4)&&(training_mfcc_recorded>100))
					||((training_sound==5)&&(training_mfcc_recorded>100)))
				{
					// Место эпического завершения тренировки
					StopTrainingMode();
				}
			}
		}
	}
	else // Проверка на попадание
	{
		//i=model.WhichSound((mfcc_t *)buf_cas_cep);
		i=model.WhichSound((mfcc_t *)buf_cas_cep, true); // Если попало в 2 области - берём первую попавшуюся
		if(i>=0)
		{
			InterlockedIncrement(&sounds_found[i]); // Ибо другой поток сбрасывает в 0 (Indicators::Draw - какая насмешка! Плебей принимает решение!)
			// [06.04.2019]
			if(i<4) // к и ч не считаются
			{
				g_add_mouse_speed+=1;
				if(g_add_mouse_speed>20) g_add_mouse_speed=20;
			}
		}
		else
		{
			g_add_mouse_speed-=5;
			if(g_add_mouse_speed<0) g_add_mouse_speed=0;
		}
			

	}

	// 5. Накапливаем 7 отсчетов в буфере №1
	mfcc_buffer1[mfcc_pointer]=*((mfcc_t *)buf_cas_cep); // Те же 13 float, но тип другой
	mfcc_pointer++;
	if(mfcc_pointer >= MM_MFCC_BUFFER_LEN)
	{
		// Это на следующий раз - тут всё понятно
		mfcc_pointer=0;
		// А вот можно ли перезаписать буфер №2?
		if(flag_mfcc_buffer2_empty)
		{
			flag_mfcc_buffer2_empty=false;
			CopyMemory(mfcc_buffer2, mfcc_buffer1, sizeof(mfcc_buffer2));
			// Корректируем минимальные и максимальные значения
			// Для начала посмотрим, не взятые ли от балды нули являются пределами?
			if(flag_minmax_reset)
			{
				// да, занесём в пределы реальные значения из первой попавшейся точки
				flag_minmax_reset=false;
				for(k=0;k<13;k++) 
				{
					CopyShmopy_minmax[k][0]= mfcc_buffer2[0].coeff[k];
					CopyShmopy_minmax[k][1]= mfcc_buffer2[0].coeff[k];
				}
			}

			for(i=0;i<MM_MFCC_BUFFER_LEN; i++) // Все MFCC в подборке
			{
				for(k=0;k<13;k++) // Все коэффициенты
				{
					if(mfcc_buffer2[i].coeff[k] < CopyShmopy_minmax[k][0]) CopyShmopy_minmax[k][0]= mfcc_buffer2[i].coeff[k];
					if(mfcc_buffer2[i].coeff[k] > CopyShmopy_minmax[k][1]) CopyShmopy_minmax[k][1]= mfcc_buffer2[i].coeff[k];
				}
			}
			//CopyShmopy_minmax
#ifdef MM_SUPERUSER
			InvalidateRect(exported_ogl_hwnd,NULL,FALSE); // Рисование в отладочном окне
#endif
		}
		else
		{
			// Здесь можно держать счётчик пропущенных кадров
		}
	}


	//============== Оставим немного баловства со сдвигом спектра и голосом Чипа и Дейла ==========
	/*
	// X.6. М А Г И Я !!! увеличиваем тональность звука !!!
	for(i=DFT_SIZE-1;i>31;i--)
	{
		buf_out_balovstvo[i]=buf_out_balovstvo[i-26];
	}
	for(i=31;i>1;i--)
	{
		buf_out_balovstvo[i]=0;
	}
	//
	//X.7. Превратим снова в сигнал !
    ippstatus=ippsDFTInv_CCSToR_32f(buf_out_balovstvo, buf_out_balovstvo, pDFTSpec, pDFTWorkBuf);
	*/
	// Отключили баловство - обратное преобразование Фурье - и сэкономили 400Kбайт в размере EXE
	// Не говоря уже об экономии процессорных мощностей
	for(i=0;i<MM_SOUND_BUFFER_LEN;i++)
			buf_out_balovstvo[i]=input_data[i]*512.0f;

	return buf_out_balovstvo;
}

//====================================================================================================
// Готовимся делать прямое и обратное преобразование Фурье с буфером 512 
//====================================================================================================
void CopyShmopy::Init()
{
	if(initialized) return;

	// 1. Инициализируем библиотеку Intel IPP и сфинкса
	ippInit();
	// cas_init(); RIP

	// 2. Скока вешать в граммах?
	int sizeDFTSpec,sizeDFTInitBuf,sizeDFTWorkBuf;
	ippsDFTGetSize_C_32fc(DFT_SIZE, IPP_FFT_NODIV_BY_ANY, ippAlgHintAccurate, &sizeDFTSpec, &sizeDFTInitBuf, &sizeDFTWorkBuf);
	
	// Отвешиваем
	pDFTSpec    = (IppsDFTSpec_R_32f*)ippsMalloc_8u(sizeDFTSpec);
	pDFTInitBuf = ippsMalloc_8u(sizeDFTInitBuf);
	pDFTWorkBuf = ippsMalloc_8u(sizeDFTWorkBuf);
	
	// 3. Инициализация
	ippsDFTInit_R_32f(DFT_SIZE, IPP_FFT_NODIV_BY_ANY, ippAlgHintNone, pDFTSpec, pDFTInitBuf);
	if (pDFTInitBuf) ippFree(pDFTInitBuf); // Этот уже сразу после инициализации не нужен

	// 4. Аналогично для DCT
	int sizeDCTSpec,sizeDCTInitBuf,sizeDCTWorkBuf;
	ippsDCTFwdGetSize_32f(MEL_NUM_FILTERS,ippAlgHintNone,&sizeDCTSpec,&sizeDCTInitBuf,&sizeDCTWorkBuf);

	// Отвешиваем
	pDCTSpec    = ippsMalloc_8u(sizeDCTSpec);
	pDCTInitBuf = ippsMalloc_8u(sizeDCTInitBuf); // Чушь какая-то. sizeDCTInitBuf возвращает ноль
	pDCTWorkBuf = ippsMalloc_8u(sizeDCTWorkBuf);

	// Инициализация DCT
	ippsDCTFwdInit_32f(&ppDCTSpec, MEL_NUM_FILTERS, ippAlgHintNone, pDCTSpec, pDCTWorkBuf);
	if (pDCTInitBuf) ippFree(pDCTInitBuf); // Этот уже сразу после инициализации не нужен

	// X. Скажем всем, что всё чики-пуки
	initialized=true;

}

//================================================================
// Чистим за собой. Не факт, что это кто-то оценит...
//================================================================
void CopyShmopy::Halt()
{
	// Чистим за собой...
	if (pDFTWorkBuf) ippFree(pDFTWorkBuf);
	if (pDFTSpec) ippFree(pDFTSpec);

	if (pDCTWorkBuf) ippFree(pDCTWorkBuf);
	if (pDCTSpec) ippFree(pDCTSpec);
}


//=====================================================================
// Замена cas_mel_cep(buf_cas_MEL, buf_cas_cep);
//=====================================================================
void CopyShmopy::CS_mel_cep(float *mfspec, float *mfcep)
{
	int i;

	if(!initialized) Init();

	// 4.1 Логарифм
	for (i = 0; i < MEL_NUM_FILTERS; i++)
	{
        mfspec[i] = (float)(log(mfspec[i] + 1e-4)); // #define LOG_FLOOR 1e-4 - так было в безвременно ушедшем Сфинксе
    }
	// 4.1.5 Для legacy:
	if(flag_sphinx_legacy) mfspec[0]/=2.0f;

	// 4.2 DCT в тот же буфер
	ippsDCTFwd_32f_I(mfspec,  ppDCTSpec, pDCTWorkBuf);
	
	// 4.3 Копируем на выход только 13
	for (i = 0; i < MEL_NUM_CEPSTRA; i++)
	{
		mfcep[i]=mfspec[i];
		// Для legacy домножаем
		if(flag_sphinx_legacy)
		{
			mfcep[i]/=sqrt (2.0*MEL_NUM_FILTERS);
			if(0==i) mfcep[i]*=sqrt(2.0);
		}
	}
}