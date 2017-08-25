#include <Windows.h>
#include <stdio.h>
#include <gl/GL.h>
#include "MModel.h"
//#include "casphinx.h"
#include "CopyShmopy.h"
#include "KChFstate.h"
#include "resource.h"

extern volatile int current_device_num; // Для записи в файл конфигурации
extern unsigned long   iNumDevs; // Эту переменную иcпользуем при InputThread::Start,чтобы понять, что выбрали файл, а не устройство

MModel model;


MModel::MModel():global_min_max_values(),min_max_values(), decision_matrix(), hplane()
{
	// Временно
	LoadTestData();

	// А вот это - надолго
	InitializeCriticalSection(&belongs_to_me);

	// [22-DEC]
	// hplane[6][0] вычислим в конструкторе модели. Этот элемент не меняется.
	// По крайней мере, если мы не меняем алгоритм вычисления MFCC
	// Берём MEL-спектр тишины (40 нулей) и вычисляем его кепстр
	float buf_cas_mel[40]={0};
	CopyShmopy::Init();
	CopyShmopy::CS_mel_cep(buf_cas_mel, (float *)(&hplane[6][0].base_point));
	hplane[6][0].defined=true;

}

MModel::~MModel()
{
	// Чистим за собой
	DeleteCriticalSection(&belongs_to_me);
}

//======================================================================
// Добавляет в соответствующий список ещё одни коэффициенты
// Тут же обновить максимальные и минимальные значения по каждой из осей
//======================================================================
void MModel::AddSound(int person, int sound, int modifier, mfcc_t *_mfcc)
{
	if((person>=MM_Model_Num_Persons)||(sound>=MM_Model_Num_Sounds)||(modifier>=MM_Model_Num_Modifiers)||
		(person<0)||(sound<0)||(modifier<0))
	{
		// Потом здесь печатать ошибку, а пока просто возврат
		return;		
	}

	// Добавляем в список
	snd_matrix[person][sound][modifier].Add(_mfcc);

	// Пересчитаем всё!
	UpdateMinMax();
}

//===================================================================================
// [24-DEC] См. тетрадь. Вычисляет t(basepoint) не в середине отрезка, как раньше,
// а пропорционально границам звуков
//===================================================================================
float MModel::ClaculateBasePoint(int from, int to)
{
	int i;
	GZSListable<mfcc_t> *p_sm;
	float t,t1=-1e10f,t2=1e10f,result,numerator,denumerator,A,B,D;
	mfcc_t * mc; // Для удобства дальнейших записей
	float sigma_kvadrat, sigma_kvadrat_ref;

	// 1. Для начала переберём все точки звука 'from' и найдём t1
	p_sm=snd_matrix[0][from][0].first;
	while(NULL!=p_sm)
	{
		mc=p_sm->element; 
		sigma_kvadrat=0.0f;	
		// Интересуют только точки в пределах дисперсии
		for(i=0;i<13;i++)
		{
			sigma_kvadrat+=(mc->coeff[i]-hplane[from][0].base_point.coeff[i])*(mc->coeff[i]-hplane[from][0].base_point.coeff[i]);
		}
		if(sigma_kvadrat>hplane[from][0].sigma_kvadrat) {p_sm=p_sm->next; continue;} // Слишком далеки вы от центра, милейший. Не вам приделы устанавливать.

		numerator=0.0f; denumerator=0.0f; 
		for(i=0;i<13;i++)
		{
			// формула выведена в тетради [24-DEC]
			// см. картинку [24-DEC]
			D=mc->coeff[i];
			A=hplane[from][0].base_point.coeff[i];
			B=hplane[to][0].base_point.coeff[i];
			numerator+=(B-A)*(D-A);
			denumerator+=(B-A)*(B-A);
		}
		if(denumerator!=0.0f) result=numerator/denumerator;
		else result=0.1f; // Этого никогда не должно случиться, B!=A всегда, но перестрахуемся.

		// Ищем максимальный t1
		if(result>t1) t1=result;
		p_sm=p_sm->next;
	}
	// Если была только одна точка, сравнение с дисперсией может дать неожиданный результат
	if(t1<0.0f) t1=0.0f;

	// 2. Затем переберём все точки звука 'to' и найдём t2
	if(6!=to) // У тишины нет точек
	{
		p_sm=snd_matrix[0][to][0].first;
		while(NULL!=p_sm)
		{
			mc=p_sm->element; 
			sigma_kvadrat=0.0f;

			// Интересуют только точки в пределах дисперсии
			for(i=0;i<13;i++)
			{
				sigma_kvadrat+=(mc->coeff[i]-hplane[to][0].base_point.coeff[i])*(mc->coeff[i]-hplane[to][0].base_point.coeff[i]);
			}
			if(sigma_kvadrat>hplane[to][0].sigma_kvadrat) {p_sm=p_sm->next; continue;} // Слишком далеки вы от центра, милейший. Не вам приделы устанавливать.
			
			numerator=0.0f; denumerator=0.0f; 
			for(i=0;i<13;i++)
			{
				// формула выведена в тетради [24-DEC]
				// см. картинку [24-DEC]
				D=mc->coeff[i];
				A=hplane[from][0].base_point.coeff[i];
				B=hplane[to][0].base_point.coeff[i];
				numerator+=(B-A)*(D-A);
				denumerator+=(B-A)*(B-A);
			}
			if(denumerator!=0.0f) result=numerator/denumerator;
			else result=0.1f; // Этого никогда не должно случиться, B!=A всегда, но перестрахуемся.

			// Ищем МИНИМАЛЬНЫЙ t2
			if(result<t2) t2=result;
			p_sm=p_sm->next;
		}
		// Если была только одна точка, сравнение с дисперсией может дать неожиданный результат
		if(t2>1.0f) t2=1.0f;
	}
	else
	{
		t2=t1; // тишина
	}
	
	// Тут поставить точку останова и последить за t1 и t2
	// [24-DEC.2]
	t=t1/(1+t1-t2);

	return t;
}

//======================================================================
// Обновляет минимальные и максимальные значения, чтобы правильно
// выставить матрицу ModelView
//======================================================================
void MModel::UpdateMinMax()
{
	int i,j,x,count;
	GZSListable<mfcc_t> *p_sm;
	mfcc_t * mc; // Для удобства дальнейших записей
	mfcc_t average;
	float t;

	// Обнуляем текущую статистику по всем данным
	for(i=0;i<13;i++)
	{
		global_min_max_values[i][0]=0.0f; // min
		global_min_max_values[i][1]=0.0f; // max
	}

	int p,s,m;
	bool first_mfcc_in_model=true; // Чтобы перейти от нулей в min/max к координатам первой точки
	for(s=0;s<MM_Model_Num_Sounds;s++)
	{
		// Обнуляем текущую статистику по данному звуку всех модификаций
		for(i=0;i<13;i++)
		{
			decision_matrix[s][i][0]=0.0f; // min
			decision_matrix[s][i][1]=0.0f; // max
		}
		bool first_mfcc_in_general_sound=true; // Чтобы перейти от нулей в min/max к координатам первой точки
		for(p=0;p<MM_Model_Num_Persons;p++)
		{
			for(m=0;m<MM_Model_Num_Modifiers;m++)
			{
				// Обнуляем текущую статистику по данному звуку конкретной модификации и человека
				for(i=0;i<13;i++)
				{
					min_max_values[p][s][m][i][0]=0.0f;
					min_max_values[p][s][m][i][1]=0.0f;
				}

				// Перебираем все данные в списке mfcc данной модификации звука
				p_sm=snd_matrix[p][s][m].first;
				// Просматриваем весь список и пересчитываем min/max
				bool first_mfcc_in_this_sound=true;
				while(NULL!=p_sm)
				{
					mc=p_sm->element; 
					for(i=0;i<13;i++)
					{
						// конкретно по одному звуку
						if((min_max_values[p][s][m][i][0]>mc->coeff[i])||first_mfcc_in_this_sound) min_max_values[p][s][m][i][0]=mc->coeff[i]; // Корректировка минимума
						if((min_max_values[p][s][m][i][1]<mc->coeff[i])||first_mfcc_in_this_sound) min_max_values[p][s][m][i][1]=mc->coeff[i]; // Корректировка максимума

						// По всем вариантам звука всех людей
						if((decision_matrix[s][i][0]>mc->coeff[i])||first_mfcc_in_general_sound) decision_matrix[s][i][0]=mc->coeff[i]; // Корректировка минимума
						if((decision_matrix[s][i][1]<mc->coeff[i])||first_mfcc_in_general_sound) decision_matrix[s][i][1]=mc->coeff[i]; // Корректировка максимума

						// По всей модели
						if((global_min_max_values[i][0]>mc->coeff[i])||first_mfcc_in_model) global_min_max_values[i][0]=mc->coeff[i];// Корректировка минимума
						if((global_min_max_values[i][1]<mc->coeff[i])||first_mfcc_in_model) global_min_max_values[i][1]=mc->coeff[i];// Корректировка максимума
					}	
					first_mfcc_in_model=false; // Первая точка в модели инициирует пределы
					first_mfcc_in_general_sound=false;
					first_mfcc_in_this_sound=false;
					p_sm=p_sm->next;
				}
			} // for m
		} // for p
	} //for s
	
	// [22-DEC] ищем гиперплоскости. Только для person=0, modifier=0
	// 22.1.Берём звук, считаем среднюю и направление – это нулевой столбец матрицы, №6 – это точка нуля.
	// [24-DEC]		Также ищем среднеквадратичное отклонение
	for(s=0;s<MM_Model_Num_Sounds;s++)
	{
		p_sm=snd_matrix[0][s][0].first;
		count=0;
		ZeroMemory(&average,sizeof(average));
		while(NULL!=p_sm)
		{
			mc=p_sm->element; 
			for(i=0;i<13;i++)
			{
				average.coeff[i]+=mc->coeff[i];
			}
			count++;
			p_sm=p_sm->next;
		}
		// Есть что усреднять?
		if(count>0)
		{
			for(i=0;i<13;i++)
			{
				average.coeff[i]/=count;
			}
			// Заполним элемент в нулевом столбце hplane
			hplane[s][0].base_point=average;
			hplane[s][0].defined=true;
			hplane[s][0].sigma_kvadrat=0.0f;

			// [24-DEC] теперь ещё ищем среднеквадратичное отклонение
			// для этого придётся пройтись по списку ещё раз
			p_sm=snd_matrix[0][s][0].first;
			while(NULL!=p_sm)
			{
				mc=p_sm->element; 
				for(i=0;i<13;i++)
				{
					hplane[s][0].sigma_kvadrat+=(mc->coeff[i]-average.coeff[i])*(mc->coeff[i]-average.coeff[i]);
				}
				p_sm=p_sm->next;
			}
			hplane[s][0].sigma_kvadrat/=count;
		} // count > 0
		else
		{
			hplane[s][0].defined=false; // Этот звук ещё не наполнен
		}
	}
	// hplane[6][0] вычислим в конструкторе модели. Этот элемент не меняется.
	// По крайней мере, если мы не меняем алгоритм вычисления MFCC

	// 22.2 Берём нулевую строку. Вычитаем нулевой звук последовательно из 1..6 – это direction, 
	// складываем с 1..6 и делим пополам – это base_point
	// [24-DEC] base_point теперь зависит от того, насколько разбросаны точки от среднего
	// Потом первую строку – с 2..6, и т.д.
	for(i=0;i<MM_Model_Num_Sounds;i++)
	{
		for(j=i+1;j<MM_Model_Num_Sounds+1;j++)
		{
			if((true==hplane[i][0].defined)&&(true==hplane[j][0].defined)) // есть что вычислять
			{
				// [24-DEC]
				t=ClaculateBasePoint(i,j);
				for(x=0;x<13;x++) // каждая координата должна быть обработана
				{
					// Вычитаем себя из других - это direction
					hplane[i][j].direction.coeff[x]=hplane[j][0].base_point.coeff[x]-hplane[i][0].base_point.coeff[x];
					// Вычисляем середину отрезка - это base_point
					// hplane[i][j].base_point.coeff[x]=(hplane[j][0].base_point.coeff[x]+hplane[i][0].base_point.coeff[x])/2.0f;
					// [24-DEC] уже не середина
					hplane[i][j].base_point.coeff[x]=hplane[j][0].base_point.coeff[x]*t+hplane[i][0].base_point.coeff[x]*(1-t);

					// !!! Временно берём только 0.1 от тишины, просто, чтобы посмотреть на работу !!!
					//if(6==j)
					//	hplane[i][j].base_point.coeff[x]=(hplane[j][0].base_point.coeff[x]*0.1f+hplane[i][0].base_point.coeff[x]*0.9f);
				}
				hplane[i][j].defined=true;
			}
			else // нет ингридиентов для вычисления
			{
				hplane[i][j].defined=false;
			}
		} // for j
	} // for i
}

//===============================================================================
// Рисовать точками данный звук
// Возможно, в будущем будем рисовать и окантовку! 
//===============================================================================
void MModel::OGLDraw(int person, int sound, int modifier, int mx, int my, int mz)
{
	if((person>=MM_Model_Num_Persons)||(sound>=MM_Model_Num_Sounds)||(modifier>=MM_Model_Num_Modifiers)||
		(person<0)||(sound<0)||(modifier<0)||
		(mx<0)||(mx>12)||(my<0)||(my>12)||(mz<0)||(mz>12))

	{
		// Потом печатать здесь ошибку, если захочешь..
		return;
	}

#ifdef MM_SUPERUSER

	// Перебираем все данные в списке mfcc данной модификации звука
	GZSListable<mfcc_t> *p_sm=snd_matrix[person][sound][modifier].first;
	// Просматриваем весь список и пересчитываем min/max
	mfcc_t * mс; // Для удобства дальнейших записей
	
	glBegin(GL_POINTS);
	while(NULL!=p_sm)
	{
		mс=p_sm->element; 
		
		// Ура! Отрисовка!
		// Рисуем обычной фигнёй (без массивов)
		glVertex3f(mс->coeff[mx],  mс->coeff[my], mс->coeff[mz]);
	
		p_sm=p_sm->next;
	} // while p_sm
	glEnd();

#endif

}

//=================================================================================
// Проверка, что за звук
// Возвращаем от 0 до MM_Model_Num_Sounds или 
// -1, если звук не найден
// -2, если найдено более одного звука
// Нельзя вызывать во время обновления модели
// Но проверку CriticalSection сюда не включал, это должно решаться административно
//==================================================================================
int MModel::WhichSound(mfcc_t *test, bool first_found)
{
	int s,x,sound_num=-1;
	bool matches;
	float x_min, x_max;

	// [22-DEC]
	int j;
	float scalar,direction;
	hyperplane_t * hc;

	for(s=0; s<MM_Model_Num_Sounds; s++) // Из строк перебираем только 6
	{
		// Результат возможен, если hplane[s][0].defined==true;
		if(false==hplane[s][0].defined) continue;
//!!! Вот где была ошибка!!! Не там сбрасывался scalar !!!		
		matches=true; // до первого положительного скалярного произведения
		for(j=0;j<MM_Model_Num_Sounds+1;j++) // ...а столбцов на 1 больше
		{
			// нет гиперплоскости, отделяющей точку саму от себя, поэтому диагональ матрицы пропускаем
			if(s==j) continue;
			// direction(i,j)=-direction(j,i)
//!!! Здесь переделать! если j>s, то это скалярное произведение уже вычислялось, просто взять его с обратным знаком
// если дело дойдёт до оптимизации			
			if(j>s)
			{
				hc=&hplane[s][j];
				direction=1.0f;
			}
			else
			{
				hc=&hplane[j][s];
				direction=-1.0f;
			}
			if(hc->defined) // Возможно, этот звук ещё не определён, и гиперплоскость, отделяющая нас от неё, не определена
			{
				scalar=0.0f;
				for(x=0;x<13;x++)
				{
					// вычисляем скалярное произведение
					scalar+=direction*hc->direction.coeff[x]*(test->coeff[x] - hc->base_point.coeff[x]);
				}
				// если хоть одно скалярное произведение положительное - мы не попали в нашу область
				if(scalar>0)
				{
					matches=false;
					break;
				}

			} // hc->defined

		} // for j - столбцы матрицы
		// Момент истины - все скалярные произведения были меньше нуля. Угол тупой. Мы попали.
		if(matches) return s;

	} // for s - строки матрицы

	//========================================================================================================
	return -1;
	//========================================================================================================
	// Старый вариант, до 22-DEC
	for(s=0; s<MM_Model_Num_Sounds; s++)
	{
		// Проверяем все 13 координат
		matches=true;
		for(x=0;x<13;x++)
		{
//#ifdef MM_SUPERUSER
			// В отладочном режиме на лету правим пределы
			x_min=min_max_values[0][s][0][x][0];
			x_max=min_max_values[0][s][0][x][1];
//#else
			// Поедет на тест к Макарчуку с жестко прошитыми пределами
			//x_min=decision_matrix[s][x][0];
			//x_max=decision_matrix[s][x][1];;
//#endif
			if((test->coeff[x]<x_min) || (test->coeff[x]>x_max)) // мимо
			{
				matches=false; 
				break;
			}
		}
		// Если прошли все 13 ограничений, то это кандидат на найденный звук
		// Но если это уже второй подходящий звук, то... дело дрянь
		if(matches)
		{
			if(-1==sound_num) // первый найденный
			{
				sound_num=s;
			}
			else
			{
				return -2; // второй подходящий...
			}
		}
	}

	// Всё просмотрели, вот вам результат
	return sound_num;
}

//====================== Работа с файлами ======================================
// Содрано из GZ64a-Model
//==============================================================================

// Переменные для диалога
static wchar_t *filter_GZM=L"файлы MM1\0*.MM1\0\0";
static wchar_t filename[1258]={0};
static wchar_t filetitle[1258]={0};

//=========================================================================
// Сохраняет модель в файл
// Если такового нет, или явно указано saveas, то спрашивает новое
// Возвращает имя файла для печати в заголовке окон
//=========================================================================
wchar_t *MModel::Save(bool saveas, HWND hdwnd, wchar_t *_filename)
{
	wchar_t error_msg[1024];

	if(NULL==_filename)
	{
		// 1. При необходимости выводим диалог
		if((0==filename[0])||saveas)
		{
			OPENFILENAME ofn=
			{
				sizeof(OPENFILENAME),
				NULL,
				NULL, // в данном конкретном случае игнорируется
				filter_GZM,
				NULL,
				0, // Не используем custom filter
				0, // -"-
				filename,
				1256,
				filetitle,
				1256,
				NULL,
				L"Сохранить файл MM1 как",
				OFN_OVERWRITEPROMPT,
				0,
				0,
				L"MM1",
				0,0,0
			};	

			// Диалог запроса имени файла
			if(0==GetSaveFileName(&ofn))
			{
				return NULL;
			}
		}

		_filename=filename;
	}

	FILE *fout=NULL;
	_wfopen_s(&fout,_filename,L"wb");

	// 1. Открываем файл в binary mode
	if(NULL==fout)
	{
		wcscpy_s(error_msg,L"Не могу записать файл '");
		wcsncat_s(error_msg,filename,1000);
		wcsncat_s(error_msg,L"'",2);
		//GZReportError(error_msg);
		MessageBox(hdwnd,error_msg,L"Ошибка",MB_OK);
		return NULL;
	}

	// 2. Пошло реальное сохранение
	// 2.1. // [28-DEC] Сигнатура "MM002 ", [23-AUG-2017] Сигнатура "MM003 " - запоминает клавиши для нажатия
	// [25-AUG-2017] Сигнатура "MM004 " - запоминает повторение клавиш
	fprintf(fout,"MM004 ");
	
	// Перебираем все данные в списке mfcc данной модификации звука
	int person,sound,modifier,num_mfcc;
	GZSListable<mfcc_t> *p_sm;
	mfcc_t * mc; // Для удобства дальнейших записей

	// Пока пишем - никому ничего не трогать !!!
	Capture();

	for(person=0; person<MM_Model_Num_Persons; person++)
		for(sound=0; sound<MM_Model_Num_Sounds; sound++)
			for(modifier=0; modifier<MM_Model_Num_Modifiers; modifier++)
			{
				num_mfcc=snd_matrix[person][sound][modifier].num_elements;
				// пишем количество элементов в этой ветке ( а веток всего 2x6x4=48 )
				fwrite(&num_mfcc,sizeof(num_mfcc),1,fout); 

				p_sm=snd_matrix[person][sound][modifier].first;
				while(NULL!=p_sm)
				{
					mc=p_sm->element; 
					fwrite(mc,sizeof(mfcc_t),1,fout);
					p_sm=p_sm->next;
				}
			}
	
	// Теперь можете что хотите делать с моделью
	Release();
	fwrite((void *)&current_device_num, sizeof(current_device_num),1,fout);

	// [28-DEC] 
	fwrite((void *)&KChFstate::flag_kc_anytime, sizeof(KChFstate::flag_kc_anytime),1,fout); 

	//[23-AUG-2017]
	fwrite((void *)&KChFstate::key_to_press, 6*sizeof(WORD),1,fout);

	//[25-AUG-2017]
	fwrite((void *)&KChFstate::repeat_key, 6*sizeof(char),1,fout);

	fclose(fout);

	return filetitle;
}


//==================================================================================================================================
// Загружает модель из файла 
// Содрано оттуда же
//==================================================================================================================================
wchar_t *MModel::Load(HWND hdwnd, wchar_t *_filename)
{
	wchar_t error_msg[1024];
	int version=1;

/*	// -1. Спросим, а не жалко ли уничтожить то, что есть?
	if(!IsEmpty())
	{
		if(IDYES!=MessageBox(NULL,"В памяти есть модель.\nВсё же грузим новую?",filetitle,
			MB_YESNO|MB_ICONQUESTION))
		{
			if(filetitle[0]) return filetitle; 
			else return NULL;
		}
	}
*/
	if(NULL==_filename)
	{
		// 0. Показываем диалог
		OPENFILENAME ofn=
		{
			sizeof(OPENFILENAME),
			NULL,
			NULL, // в данном конкретном случае игнорируется
			filter_GZM,
			NULL,
			0, // Не используем custom filter
			0, // -"-
			filename,
			1256,
			filetitle,
			1256,
			NULL,
			L"Открыть файл MM1",
			OFN_FILEMUSTEXIST | OFN_HIDEREADONLY ,
			0,
			0,
			L"MM1",
			0,0,0
		};

		// Диалог запроса имени файла
		if(0==GetOpenFileName(&ofn))
		{
			return NULL;
		}

		_filename=filename;
	}

	FILE *fin=NULL;
	_wfopen_s(&fin,_filename,L"rb");

	// 1. Открываем файл в binary mode
	if(NULL==fin)
	{
		if(hdwnd) // Иногда нам не нужно видеть сообщения об ошибках
		{
			wcscpy_s(error_msg,L"Не могу открыть файл '");
			wcsncat_s(error_msg,filename,1000);
			wcsncat_s(error_msg,L"'", 2);
			//GZReportError(error_msg);
			MessageBox(hdwnd,error_msg,L"Ошибка",MB_OK);
		}
		return NULL;
	}
	
	// 2. А вот здесь пойдёт загрузка, пора обнулять модель
	Capture(); // берём модель в монопольное владение
	EmptyModel();

	// 3. Пошла реальная загрузка
	// 3.1. Сигнатуры "MM00x "
	char signature[6];
	fread(signature,6,1,fin);
	
	// Может читать файл конфигурации предыдущих версий
	//[23-AUG-2017]
	if(0==strncmp("MM004 ",signature,6)) version=4;
	else if(0==strncmp("MM003 ",signature,6)) version=3;
	else if(0==strncmp("MM002 ",signature,6)) version=2;
	else if(0!=strncmp("MM001 ",signature,6)) goto bad_file;

	int person,sound,modifier,num_mfcc,i;
	mfcc_t *new_mfcc=0;
	
	// Перебираем все 48 веток
	for(person=0; person<MM_Model_Num_Persons; person++)
		for(sound=0; sound<MM_Model_Num_Sounds; sound++)
			for(modifier=0; modifier<MM_Model_Num_Modifiers; modifier++)
			{
				if(1!=fread(&num_mfcc,sizeof(num_mfcc),1,fin)) goto bad_file; // Количество mfcc в веточке

				for(i=0;i<num_mfcc;i++)
				{
					new_mfcc=new(mfcc_t);
					snd_matrix[person][sound][modifier].Add(new_mfcc);
					if(1!=fread(new_mfcc,sizeof(mfcc_t),1,fin)) goto bad_file; // Именно так, сначала добавить в список, а потом - загрузить
				}
				
			}

	// Загрузка завершена, пересчёт границ делаем ДО Release()
	UpdateMinMax();

	// Теперь можете что хотите делать с моделью
	Release();

	fread((void *)&current_device_num, sizeof(current_device_num),1,fin); // какое устройство выбрать
	if(current_device_num>=iNumDevs) current_device_num=0;

	if(version>=2) // с [28-DEC] добавился флаг flag_kc_anytime
	{
		fread((void *)&KChFstate::flag_kc_anytime, sizeof(KChFstate::flag_kc_anytime),1,fin); 
	}

	if(version>=3) // с [23-AUG-2017] добавились клавиши для нажатия
	{
		fread((void *)&KChFstate::key_to_press, 6*sizeof(WORD),1,fin); 
	}

	if(version>=4) // с [25-AUG-2017] добавились повторы клавиш для нажатия
	{
		fread((void *)&KChFstate::repeat_key, 6*sizeof(char),1,fin); 
	}

	fclose(fin);
	return filetitle; 

bad_file: // Такого не должно случаться никогда...
	EmptyModel();
	Release();
	if(hdwnd) // Иногда нам не нужно видеть сообщения об ошибках
	{
		MessageBox(NULL,L"Неверный формат файла",filetitle,MB_ICONEXCLAMATION|MB_OK);
	}
	filetitle[0]=0;
	
	return NULL;
}

//====================================================================
// Чистим модель (пока требуется только перед загрузкой)
//====================================================================
void MModel::EmptyModel()
{
	int person,sound,modifier;

	for(person=0; person<MM_Model_Num_Persons; person++)
		for(sound=0; sound<MM_Model_Num_Sounds; sound++)
			for(modifier=0; modifier<MM_Model_Num_Modifiers; modifier++)
			{
				snd_matrix[person][sound][modifier].EmptyList();
			}
}


//==============================================================
// Дамп пределов в виде кода на C
//==============================================================
void MModel::DumpC()
{
	int s,x,sound_num=-1;

	FILE *fout=NULL;
	_wfopen_s(&fout,L"MMDUmp.C",L"w");
	
	fprintf(fout,"// Это загрузить из файла MMDUmp.C в LoadTestData()\n");

	//decision_matrix[MM_Model_Num_Sounds][13][2]

	float x_min, x_max;

	for(s=0; s<MM_Model_Num_Sounds; s++)
	{
		// Проверяем все 13 координат
		for(x=0;x<13;x++)
		{
			x_min=model.min_max_values[0][s][0][x][0];
			x_max=model.min_max_values[0][s][0][x][1];

			fprintf(fout,"decision_matrix[%d][%d][0]=%ff;\n",s,x,x_min);
			fprintf(fout,"decision_matrix[%d][%d][1]=%ff;\n",s,x,x_max);
		}
	}

	fclose(fout);
}




//================================================================================
// тестовые данные, пока не реализовал загрузку файла конфигурации 
//================================================================================
mfcc_t mfcc[7]=
{
	{13.248,  -0.472,  -0.603,   1.940,  -1.125,  -0.662,  -0.022,  -0.150,  -0.355,  -0.589, 0,0,0}, 
	{12.603,   0.456,  -0.757,   2.509,  -1.889,  -0.739,  -0.139,   0.076,  -0.463,  -0.919, 0,0,0}, 
	{14.504,  -0.050,  -0.651,   1.718,  -1.307,  -0.699,  -0.263,  -0.180,  -0.490,  -0.718, 0,0,0}, 
	{14.625,   0.087,  -0.753,   1.892,  -1.773,  -0.589,  -0.271,   0.027,  -0.564,  -0.839, 0,0,0},
	{15.036,   0.038,  -0.806,   1.695,  -1.583,  -0.496,  -0.309,  -0.092,  -0.581,  -0.787, 0,0,0},
	{14.870,   0.336,  -1.097,   1.959,  -1.857,  -0.322,  -0.344,   0.053,  -0.727,  -0.815, 0,0,0}, 
	{15.492,   0.038,  -0.827,   1.693,  -1.577,  -0.517,  -0.320,  -0.076,  -0.662,  -0.734, 0,0,0} 
};

// Для отладки
void MModel::LoadTestData()
{
/*	AddSound(0,0,0,&mfcc[0]);
	AddSound(0,0,0,&mfcc[1]);
	AddSound(0,0,0,&mfcc[2]);
	AddSound(0,0,0,&mfcc[3]);

	AddSound(0,0,1,&mfcc[4]);
	AddSound(0,0,1,&mfcc[5]);
	AddSound(0,0,1,&mfcc[6]); */

	// Это загрузить из файла MMDUmp.C в LoadTestData()
decision_matrix[0][0][0]=11.290342f;
decision_matrix[0][0][1]=15.407326f;
decision_matrix[0][1][0]=-0.480784f;
decision_matrix[0][1][1]=1.391899f;
decision_matrix[0][2][0]=-1.384647f;
decision_matrix[0][2][1]=-0.449189f;
decision_matrix[0][3][0]=1.306006f;
decision_matrix[0][3][1]=2.184643f;
decision_matrix[0][4][0]=-1.617535f;
decision_matrix[0][4][1]=-0.450790f;
decision_matrix[0][5][0]=-1.274131f;
decision_matrix[0][5][1]=-0.417742f;
decision_matrix[0][6][0]=-0.521067f;
decision_matrix[0][6][1]=0.325264f;
decision_matrix[0][7][0]=-0.528142f;
decision_matrix[0][7][1]=0.128402f;
decision_matrix[0][8][0]=-1.046493f;
decision_matrix[0][8][1]=-0.322476f;
decision_matrix[0][9][0]=-0.791868f;
decision_matrix[0][9][1]=-0.234684f;
decision_matrix[0][10][0]=-0.326605f;
decision_matrix[0][10][1]=0.183908f;
decision_matrix[0][11][0]=-0.245862f;
decision_matrix[0][11][1]=0.257555f;
decision_matrix[0][12][0]=-0.382097f;
decision_matrix[0][12][1]=0.225360f;
decision_matrix[1][0][0]=8.835925f;
decision_matrix[1][0][1]=11.919933f;
decision_matrix[1][1][0]=1.842904f;
decision_matrix[1][1][1]=2.849222f;
decision_matrix[1][2][0]=0.386688f;
decision_matrix[1][2][1]=1.212343f;
decision_matrix[1][3][0]=-0.139654f;
decision_matrix[1][3][1]=1.275747f;
decision_matrix[1][4][0]=-1.861509f;
decision_matrix[1][4][1]=-0.618009f;
decision_matrix[1][5][0]=-1.127878f;
decision_matrix[1][5][1]=-0.458907f;
decision_matrix[1][6][0]=-0.727651f;
decision_matrix[1][6][1]=0.056147f;
decision_matrix[1][7][0]=-0.610381f;
decision_matrix[1][7][1]=-0.146176f;
decision_matrix[1][8][0]=-0.281343f;
decision_matrix[1][8][1]=0.213004f;
decision_matrix[1][9][0]=-0.529918f;
decision_matrix[1][9][1]=0.008702f;
decision_matrix[1][10][0]=-0.495050f;
decision_matrix[1][10][1]=0.362584f;
decision_matrix[1][11][0]=-0.563247f;
decision_matrix[1][11][1]=0.017173f;
decision_matrix[1][12][0]=-0.520115f;
decision_matrix[1][12][1]=-0.030422f;
decision_matrix[2][0][0]=11.885610f;
decision_matrix[2][0][1]=15.306168f;
decision_matrix[2][1][0]=0.959324f;
decision_matrix[2][1][1]=2.529395f;
decision_matrix[2][2][0]=-1.007071f;
decision_matrix[2][2][1]=-0.224348f;
decision_matrix[2][3][0]=-0.591857f;
decision_matrix[2][3][1]=0.736985f;
decision_matrix[2][4][0]=-2.431574f;
decision_matrix[2][4][1]=-1.561412f;
decision_matrix[2][5][0]=-0.657238f;
decision_matrix[2][5][1]=-0.047033f;
decision_matrix[2][6][0]=-0.159609f;
decision_matrix[2][6][1]=0.349820f;
decision_matrix[2][7][0]=-0.321141f;
decision_matrix[2][7][1]=0.247155f;
decision_matrix[2][8][0]=-0.592129f;
decision_matrix[2][8][1]=-0.162861f;
decision_matrix[2][9][0]=-1.102238f;
decision_matrix[2][9][1]=-0.479948f;
decision_matrix[2][10][0]=-0.023816f;
decision_matrix[2][10][1]=0.641446f;
decision_matrix[2][11][0]=-0.309142f;
decision_matrix[2][11][1]=0.330921f;
decision_matrix[2][12][0]=-0.484294f;
decision_matrix[2][12][1]=0.098315f;
decision_matrix[3][0][0]=13.360507f;
decision_matrix[3][0][1]=16.705885f;
decision_matrix[3][1][0]=0.376933f;
decision_matrix[3][1][1]=1.282975f;
decision_matrix[3][2][0]=-2.182554f;
decision_matrix[3][2][1]=-1.432819f;
decision_matrix[3][3][0]=0.346564f;
decision_matrix[3][3][1]=1.163550f;
decision_matrix[3][4][0]=-1.776021f;
decision_matrix[3][4][1]=-1.171717f;
decision_matrix[3][5][0]=-0.622312f;
decision_matrix[3][5][1]=-0.090039f;
decision_matrix[3][6][0]=-0.631229f;
decision_matrix[3][6][1]=-0.214570f;
decision_matrix[3][7][0]=-0.471779f;
decision_matrix[3][7][1]=-0.110145f;
decision_matrix[3][8][0]=-0.109108f;
decision_matrix[3][8][1]=0.361068f;
decision_matrix[3][9][0]=-0.437079f;
decision_matrix[3][9][1]=-0.067324f;
decision_matrix[3][10][0]=-0.104139f;
decision_matrix[3][10][1]=0.374995f;
decision_matrix[3][11][0]=-0.402766f;
decision_matrix[3][11][1]=-0.027892f;
decision_matrix[3][12][0]=-0.401976f;
decision_matrix[3][12][1]=0.037395f;
decision_matrix[4][0][0]=8.959452f;
decision_matrix[4][0][1]=15.071016f;
decision_matrix[4][1][0]=-0.496415f;
decision_matrix[4][1][1]=1.393666f;
decision_matrix[4][2][0]=-2.368832f;
decision_matrix[4][2][1]=-1.017779f;
decision_matrix[4][3][0]=-0.521969f;
decision_matrix[4][3][1]=0.298861f;
decision_matrix[4][4][0]=-1.789410f;
decision_matrix[4][4][1]=-0.793839f;
decision_matrix[4][5][0]=0.025053f;
decision_matrix[4][5][1]=0.762696f;
decision_matrix[4][6][0]=-0.967906f;
decision_matrix[4][6][1]=-0.063930f;
decision_matrix[4][7][0]=-0.444551f;
decision_matrix[4][7][1]=0.325071f;
decision_matrix[4][8][0]=-0.587512f;
decision_matrix[4][8][1]=-0.057097f;
decision_matrix[4][9][0]=-0.189108f;
decision_matrix[4][9][1]=0.228794f;
decision_matrix[4][10][0]=-0.306082f;
decision_matrix[4][10][1]=0.485767f;
decision_matrix[4][11][0]=-0.340870f;
decision_matrix[4][11][1]=0.087196f;
decision_matrix[4][12][0]=-0.256571f;
decision_matrix[4][12][1]=0.136824f;
decision_matrix[5][0][0]=6.242992f;
decision_matrix[5][0][1]=13.953276f;
decision_matrix[5][1][0]=-3.022670f;
decision_matrix[5][1][1]=-0.812491f;
decision_matrix[5][2][0]=-1.026424f;
decision_matrix[5][2][1]=-0.204010f;
decision_matrix[5][3][0]=0.298632f;
decision_matrix[5][3][1]=1.965682f;
decision_matrix[5][4][0]=-1.336397f;
decision_matrix[5][4][1]=-0.531306f;
decision_matrix[5][5][0]=-0.368026f;
decision_matrix[5][5][1]=0.170181f;
decision_matrix[5][6][0]=-0.752007f;
decision_matrix[5][6][1]=0.211240f;
decision_matrix[5][7][0]=-0.721089f;
decision_matrix[5][7][1]=0.422815f;
decision_matrix[5][8][0]=-0.814417f;
decision_matrix[5][8][1]=-0.027154f;
decision_matrix[5][9][0]=-0.352851f;
decision_matrix[5][9][1]=0.102042f;
decision_matrix[5][10][0]=-0.498495f;
decision_matrix[5][10][1]=0.129654f;
decision_matrix[5][11][0]=-0.288284f;
decision_matrix[5][11][1]=0.256562f;
decision_matrix[5][12][0]=-0.398046f;
decision_matrix[5][12][1]=0.069959f;

}
