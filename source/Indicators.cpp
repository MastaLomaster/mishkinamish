#include <Windows.h>
#include "Indicators.h"
#include "WorkerThread.h"
#include "MMGlobals.h"
#include "KChFstate.h"

#define num_steps 7

extern LONG volatile sounds_found[6];

extern volatile bool flag_training_mode;
extern volatile long training_mfcc_recorded; // 17-DEC
extern volatile long training_frame_counter;
extern volatile int training_sound;
extern volatile bool flag_move_mouse;

HWND Indicators::hdwnd=0;
HDC Indicators::memdc=0, Indicators::memdc2=0;
HBITMAP Indicators::hbm=0, Indicators::hbm2=0;
int Indicators::xpos, Indicators::ypos, Indicators::xsize, Indicators::ysize;
int Indicators::xpos6c[6], Indicators::ypos6c[6];

extern int volatile g_click_sound; //[27-DEC]

/* int Indicators::xpos_K, Indicators::ypos_K, Indicators::xpos_I, Indicators::ypos_I;
int Indicators::xpos_E, Indicators::ypos_E, Indicators::xpos_U, Indicators::ypos_U;
int Indicators::xpos_O, Indicators::ypos_O, Indicators::xpos_CH, Indicators::ypos_CH;
*/

int Indicators::hor_increase, Indicators::vert_increase;
int Indicators::KChWaits[2]={0};
volatile LONG Indicators::KChConfirmed=0L; // [18-dec]

HBRUSH Indicators::red_brush=0; 
HBRUSH Indicators::green_brush=0;
HPEN Indicators::red_pen=0;
//=============================================================================
// Созаём битмап и рисуем в него для последующих отрисовок индикаторов
//=============================================================================
 void Indicators::Init(HWND _hdwnd)
 {
	hdwnd=_hdwnd;
	//RECT rect={158,26,145,48}; // xpos,ypos,xsize,ysize, а никакой не rect. Нужно для DLU
	RECT rect={158,26,70,8*(num_steps+1)}; // xpos,ypos,xsize,ysize, а никакой не rect. Нужно для DLU
	// О, козлиная система Dialog Unit'ов!!!
	MapDialogRect(hdwnd,&rect);
	xpos=rect.left;
	ypos=rect.top;
	xsize=rect.right;
	ysize=rect.bottom;

	// Создадим супер-битмап
	HDC hdc=GetDC(_hdwnd);
	
	// Убираем DC
	if(memdc!=0) DeleteDC(memdc);
	if(memdc2!=0) DeleteDC(memdc2);
	// Убираем битмап
	if(hbm!=0) DeleteObject(hbm);
	if(hbm2!=0) DeleteObject(hbm2);
	
	
	// Cоздаём DC и битмап
	memdc=CreateCompatibleDC(hdc);
	memdc2=CreateCompatibleDC(hdc);
	hbm=CreateCompatibleBitmap(hdc,rect.right,rect.bottom); 
	hbm2=CreateCompatibleBitmap(hdc,rect.right,rect.bottom/(num_steps+1)); // Для полоски прогресса записи
	SelectObject(memdc,hbm);
	SelectObject(memdc2,hbm2);
	ReleaseDC(_hdwnd,hdc);

	// Рисуем на битмапе зелёных чело.. индикаторов на черном фоне + красная перегрузка
	red_brush=CreateSolidBrush( RGB(255,0,0));
	green_brush=CreateSolidBrush( RGB(0,255,0));
	red_pen=CreatePen(PS_SOLID,1,RGB(255,100,100));

	// Закрасим фон. Для этого вернём эксплуатируемым top и left нулевые значения.
	rect.top=0;rect.left=0;
	FillRect(memdc,&rect,(HBRUSH)GetStockObject(WHITE_BRUSH)); 

	// Растёт зелёная полоска
	hor_increase=xsize/num_steps;
	vert_increase=ysize/(num_steps+1);
	
	rect.right=0;
	for(int i=0;i<num_steps-1;i++)
	{
		rect.right+=hor_increase;
		rect.top+=vert_increase;rect.bottom+=vert_increase;
		FillRect(memdc,&rect,green_brush); 
	}
	// Конец-красный
	rect.left=rect.right;
	rect.right=xsize;
	rect.top+=vert_increase;rect.bottom+=vert_increase;
	FillRect(memdc,&rect,red_brush); 
	
	// Для красоты проведём вертикальные линии
	SelectObject(memdc,GetStockObject(WHITE_PEN));
	int i,j;
	//POINT p;
	for(i=1;i<num_steps;i++)
	{
		MoveToEx(memdc, i*hor_increase, 0, NULL);
		LineTo(memdc, i*hor_increase, ysize);
		for(j=0;j<i;j++)
		{
			// Рисуем смайлики
			Ellipse(memdc,j*hor_increase+2, i*vert_increase+2, (j+1)*hor_increase-2, (i+1)*vert_increase-2); 
			//Arc(memdc,j*hor_increase+2, i*vert_increase+2, (j+1)*hor_increase-2, (i+1)*vert_increase-2, 
			//	j*hor_increase+hor_increase/2, i*vert_increase+2, j*hor_increase+hor_increase/2, i*vert_increase+2); 

			// Красиво не получилось, оставим на потом
			/*
			SelectObject(memdc,GetStockObject(BLACK_PEN));
			// Глаз 1
			MoveToEx(memdc,j*hor_increase+hor_increase/3, i*vert_increase+vert_increase/3, NULL);
			LineTo(memdc,j*hor_increase+hor_increase/3+1,  i*vert_increase+vert_increase/3+1);
			// Глаз 2
			MoveToEx(memdc,(j+1)*hor_increase-hor_increase/3, i*vert_increase+vert_increase/3, NULL);
			LineTo(memdc,(j+1)*hor_increase-hor_increase/3+1,  i*vert_increase+vert_increase/3+1);

			Arc(memdc,j*hor_increase+5, i*vert_increase+5, (j+1)*hor_increase-5, (i+1)*vert_increase-5, 
				j*hor_increase, i*vert_increase+vert_increase*2/3, (j+1)*hor_increase, i*vert_increase+vert_increase*2/3); 

			// Последняя точка не закрашена - улыбка кривая
			GetCurrentPositionEx(memdc,&p);
			LineTo(memdc,p.x+1,p.y+1);

			SelectObject(memdc,GetStockObject(WHITE_PEN));
*/
		}
	}




	// Часть 2. Для вывода индикаторов звуков
	// 0..5 = И У O Э K Ч
	// И = 0
	rect.left=60;
	rect.top=78;
	MapDialogRect(hdwnd,&rect);
	xpos6c[0]=rect.left;
	ypos6c[0]=rect.top;

	// У=1
	rect.left=108;
	rect.top=103;
	MapDialogRect(hdwnd,&rect);
	xpos6c[1]=rect.left;
	ypos6c[1]=rect.top;

	// О=2
	rect.left=170;
	rect.top=78;
	MapDialogRect(hdwnd,&rect);
	xpos6c[2]=rect.left;
	ypos6c[2]=rect.top;

	// Э=3
	rect.left=108;
	rect.top=50;
	MapDialogRect(hdwnd,&rect);
	xpos6c[3]=rect.left;
	ypos6c[3]=rect.top;

	// К=4
	rect.left=60;
	rect.top=129;
	MapDialogRect(hdwnd,&rect);
	xpos6c[4]=rect.left;
	ypos6c[4]=rect.top;
	
	// Ч=5
	rect.left=170;
	rect.top=129;
	MapDialogRect(hdwnd,&rect);
	xpos6c[5]=rect.left;
	ypos6c[5]=rect.top;
 }

//=============================================================================
// Чистим за собой
//=============================================================================
 void Indicators::Halt()
 {
	// Убираем DC
	if(memdc!=0) DeleteDC(memdc);
	if(memdc2!=0) DeleteDC(memdc2);
	// Убираем битмап
	if(hbm!=0) DeleteObject(hbm);
	if(hbm2!=0) DeleteObject(hbm2);

	// Кисти более не нужны
	DeleteObject(red_brush);
	DeleteObject(green_brush);
	DeleteObject(red_pen);
 }

 //========================================================================
 // Двигает мышь (содрано из bkb/Fixation.cpp)
 //========================================================================
 static void MoveMouse(long hor, long vert)
 {
	 // Содрано из интернета
	double XSCALEFACTOR = 65535.0 / (GetSystemMetrics(SM_CXSCREEN) - 1);
    double YSCALEFACTOR = 65535.0 / (GetSystemMetrics(SM_CYSCREEN) - 1);

	INPUT input[1]={0};

	// 1. Сначала подвинем курсор
	input[0].type=INPUT_MOUSE;
	//input[0].mi.dx=(LONG)(hor*XSCALEFACTOR);
	//input[0].mi.dy=(LONG)(vert*YSCALEFACTOR);
	input[0].mi.dx=hor;
	input[0].mi.dy=vert;
	input[0].mi.mouseData=0; // Нужно для всяких колёс прокрутки 
	//input[0].mi.dwFlags=MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_MOVE;
	input[0].mi.dwFlags=MOUSEEVENTF_MOVE;
	input[0].mi.time=0;

	input[0].mi.dwExtraInfo=0;
			
	// Имитирует нажатие движение мыши
	SendInput(1,input,sizeof(INPUT));
 }


 //================================================================================================================
 // Щелчок мышью: 0 - К (нажатие с отпусканием), 1 - Ч (только нажатие или отпускание) (содрано из bkb/Fixation.cpp)
 //=================================================================================================================
 static void Click(int KCh)
 {
	 g_click_sound=KCh+1; // звук допускает значения 1 или 2

	 static int pressed=0;

	 INPUT input[2]={0,0};

	// 2. нажатие левой кнопки
	input[0].type=INPUT_MOUSE;
	input[0].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;

	// 3. отпускание левой кнопки
	input[1].type=INPUT_MOUSE;
	input[1].mi.dwFlags=MOUSEEVENTF_LEFTUP;

	if(0==KCh)
	{
		// Имитирует нажатие и отпускание левой кнопки мыши
		SendInput(2,input,sizeof(INPUT));
		pressed=0;
	}
	else
	{
		// Только нажаатие или только отпускание
		if(0==pressed)
		{
			pressed=1;
			SendInput(1,&input[0],sizeof(INPUT));
		}
		else
		{
			//pressed=0;
			//SendInput(1,&input[1],sizeof(INPUT));
		}
	}
 }

 //=============================================================================
// Рисуем индикаторы
//=============================================================================
 static bool flag_tishina_v_studii=false;
 void Indicators::Draw(HDC hdc)
 {

	 // 1. Индикатор уровня рисуется вот в таком прямоугольнике DLU
	 //RECT rect={162+7, 26+7, 310+7, 34+7};
	 // Берём себе локальную копию
	 int indicator_value=WorkerThread::indicator_value;

	 if(indicator_value<0) indicator_value=0;
	 if(indicator_value>num_steps) indicator_value=num_steps; // страховка

	 // [25-DEC] Тишина в студии!
	 if(flag_training_mode&&(training_frame_counter<=40))
	 {
		 RECT temp_rect={xpos,ypos,xpos+xsize,ypos+ysize/(num_steps+1)};
		 FillRect(hdc,&temp_rect,red_brush);
		 SetTextColor(hdc,RGB(255,255,255));
		 SetBkColor(hdc,RGB(255,0,0));
		 TextOut(hdc,xpos,ypos,L"Тишина в студии!",17); // при переводе здесь быть осторожным, длина строки изменится
		 flag_tishina_v_studii=true;
	 }
	 else
	 {
		 BitBlt(hdc,xpos,ypos,xsize,ysize/(num_steps+1),memdc,0,WorkerThread::indicator_value*vert_increase,SRCCOPY);
		//  BitBlt(hdc,xpos,ypos,xsize,ysize/6,memdc,0,3*(ysize/6),SRCCOPY);
	 }

	// Индикаторы кнопок (проверка)
	if(!flag_training_mode)
	{
		int i,confirmed;
		LONG l,move[4];
		// Для первых 4 звуков, отвечающих за движение
		for(i=0; i<4; i++) 
		{
			l=InterlockedExchange(&sounds_found[i], 0);
			if(l>0)
				BitBlt(hdc,xpos6c[i],ypos6c[i],hor_increase,vert_increase,memdc,0,1*vert_increase,SRCCOPY);
			else
				BitBlt(hdc,xpos6c[i],ypos6c[i],hor_increase,vert_increase,memdc,0,0,SRCCOPY);
			if(i<4) move[i]=l; // Движение влево, вниз, вправо, вверх
			// [23-AUG-2017] Если вместо мыши нажимаем клавиши, то move[i] обнуляется
			if(flag_move_mouse)( move[i]=KChFstate::TryToPress(i,move[i]) );
		}

		
		// Если есть подтверждение или сброс - риcовать спец. прямоугольник
		// Здесь была ошибка, это делалось два раза в цикле ниже
		confirmed=InterlockedExchange(&KChConfirmed, 0);

		// Для последних 2 звуков, отвечающих за нажатие (К и Ч) [18-DEC]
		for(i=4; i<6; i++) 
		{
			// Был ли звук К или Ч?
			l=InterlockedExchange(&sounds_found[i], 0);

			switch(confirmed)
			{
			case 0: // Обычная отрисовка, ничего не происходит !!! Здесь лучше красный квадратик, как несостоявшееся нажатие
				if(l>0) // Найден К или Ч
				{
					// Но в то ли время он появился?
					if(KChFstate::IsKCValid())
					{
						KChWaits[i-4]=1; // Ждём подтверждения, что это короткий звук и рисуем обычный зелёный
						BitBlt(hdc,xpos6c[i],ypos6c[i],hor_increase,vert_increase,memdc,0,1*vert_increase,SRCCOPY);
					}
					else // Ты пришёл не вовремя!
					{
						KChWaits[i-4]=0;
						
#ifdef _DEBUG
			OutputDebugString(L"NEVOVREMYA :-( !***\r\n");
#endif
						// Отрисовка красным полноцветным
						BitBlt(hdc,xpos6c[i],ypos6c[i],hor_increase,vert_increase,memdc,(num_steps-1)*hor_increase,num_steps*vert_increase,SRCCOPY);
					}
				}
				else // Белый квадратик
					BitBlt(hdc,xpos6c[i],ypos6c[i],hor_increase,vert_increase,memdc,0,0,SRCCOPY);
				break;

			case 1: // Дождались, можно нажимать. Но только, если нужная буква была ранее произнесена
				//[28-DEC] если конфликт К и Ч, Ч побеждает
				if((1==KChWaits[0])&&(1==KChWaits[1])) 
					KChWaits[0]=0;

				if(1==KChWaits[i-4])
				{
					// Важно сбросить признак того, что нас ждали
					KChWaits[i-4]=0;

					// !!! Здесь будет нажатие или чё там
					if(flag_move_mouse)
					{
						// А не нажимаем ли мы клавишу клавиатуры вместо клика?
						if(0==KChFstate::TryToPress(i, 10))
						{
							//Тут же отжимаем клавишу назад
							KChFstate::TryToPress(i, 0);
						}
						else // Неееб кликаем по старинке.
						{
							Click(i-4);
						}
					}

					// Отрисовка зелёным полноцветным
					BitBlt(hdc,xpos6c[i],ypos6c[i],hor_increase,vert_increase,memdc,0,num_steps*vert_increase,SRCCOPY);
				}
				else // Белый квадратик
					BitBlt(hdc,xpos6c[i],ypos6c[i],hor_increase,vert_increase,memdc,0,0,SRCCOPY);
				break;

			case 2: // Облом. Рисуем красный прямоугольник. Но только, если нужная буква была ранее произнесена
				if(1==KChWaits[i-4])
				{
					// Важно сбросить признак того, что нас ждали
					KChWaits[i-4]=0;

					// Отрисовка красным полноцветным
					BitBlt(hdc,xpos6c[i],ypos6c[i],hor_increase,vert_increase,memdc,(num_steps-1)*hor_increase,num_steps*vert_increase,SRCCOPY);
				}
				else // Белый квадратик
					BitBlt(hdc,xpos6c[i],ypos6c[i],hor_increase,vert_increase,memdc,0,0,SRCCOPY);
				break;

			} // switch
		} // for i

		// Не удивляйтесь, но именно здесь происходит управление мышью...
		if((move[0]!=0)||(move[1]!=0)||(move[2]!=0)||(move[3]!=0))
		{
			if(flag_move_mouse) MoveMouse(move[2]-move[0],move[1]-move[3]); // Порядок кнопок - против часовой стрелки, начиная с "лево"
		}
	}
	else // При включении training_mode надо сделать InvalidateRect диалогу, иначе останутся рудименты
	{
		if(training_frame_counter>40) // Выждали паузу, теперь можно записывать
		{
			RECT r2={0,0,xsize,vert_increase};

			// Замалевать остатки "тишины в студии"
			if(flag_tishina_v_studii)
			{
				flag_tishina_v_studii=false;
				InvalidateRect(hdwnd,NULL,TRUE);
			}

			// Для звуков "к" и "Ч" тренировка короче (сейчас одинакова для обоих, но может быть разная)
			long limit=MM_NUM_MFCC_IN_TRAINING;
			if(training_sound==4) limit=100;
			if(training_sound==5) limit=100;

			// Рисуем типа ProgressBar трейнинга
			//vert_increase*(7l*training_mfcc_recorded/limit)
			FillRect(memdc2,&r2,(HBRUSH)GetStockObject(WHITE_BRUSH));
			r2.right=training_mfcc_recorded*r2.right/limit;
			FillRect(memdc2,&r2,green_brush);

			BitBlt(hdc,xpos6c[training_sound],ypos6c[training_sound],xsize,vert_increase, memdc2, 0, 0,SRCCOPY);
		
			// Рисуем красную полоску, громче которой надо говорить
			SelectObject(hdc, red_pen);
			MoveToEx(hdc,xpos+hor_increase*(WorkerThread::training_silence_indicator+1), ypos,NULL);
			LineTo(hdc,xpos+hor_increase*(WorkerThread::training_silence_indicator+1), ypos+vert_increase);
		}

	}

 }