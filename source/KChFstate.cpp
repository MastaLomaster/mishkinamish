#include <Windows.h>
#include "KChFstate.h"
#include "Indicators.h"

volatile int KChFstate::state(0), KChFstate::minlevel(0), KChFstate::counter(0);
volatile bool KChFstate::flag_kc_anytime=false;
volatile int KChFstate::next_kc_counter=0; // Через сколько фреймов можно нажать  К/Ч (в упрощенном режиме)

volatile WORD KChFstate::key_to_press[6]={0xffff,0xffff,0xffff,0xffff,0xffff,0xffff};
volatile char KChFstate::repeat_key[6]={0,0,0,0,0,0}; 
//volatile char KChFstate::toggle_key[6]={0,0,0,0,0,0}; // является ли переключателем 
volatile char KChFstate::toggle_key[6]={0,0,0,0,0,0}; // является ли переключателем 


int KChFstate::key_state[6]={0,0,0,0,0,0}; // нажата-отжата
int KChFstate::key_cycle_counter[6]={0,0,0,0,0,0}; // Сколько ещё циклов нельзя менять состояние клавиши

#ifdef _DEBUG
void KCOnTick(int state);
#else
static void KCOnTick(int state){}; // Если не отладка, то функция пуста
#endif



//==================================================================================================
// // может ли к или Ч быть рассмотрена кандидатом на нажатие (в нужное ли время ?) mod. [28-DEC]
//==================================================================================================
bool KChFstate::IsKCValid()
{
	if(flag_kc_anytime) // упрощённый режим
	{
		if(0==next_kc_counter) // можно нажимать прямо в следующем фрейме
		{
			Indicators::KChConfirmed=1;
			next_kc_counter=20;
		}

		// всегда в упрощенном режиме
		return true;
	}
	else
	{
		//06.04.2019
		// Чтобы могло сработать тихе произношение буквы К
		if(1==state)
		{
			Indicators::KChConfirmed=1;
			return true;
		}

		// Есть мысль, что и в состоянии 3 всегда valid. Лишь бы уровень сигнала был подходящим. Меняем:
		// return ((2==state)||((3==state)&&(counter<=3));
		return ((2==state)||(3==state));
	}
}


//============================================================
// Пришёл новый фрейм и его уровень энергии подсчитан [18-dec]
//============================================================
void KChFstate::NewFrame(int energy_level)
{
	// [28-DEC] упрощённый режим
	if(flag_kc_anytime)
	{
		state=0;
		counter=0;

		// Декремент вызывается из потока WorkingThread, а проверка на 0 и выставление в 20 - в основном потоке 
		// Но атомарности ни там ни здесь не нужно, так как:
		// пропуск одного декремента не критичен, запоздание на один фрейм при нажатии некритично.
		if(next_kc_counter>0) next_kc_counter--;

		return; // чуть не забыл!
	}

	switch(state)
	{
	case 0:
		if(energy_level<minlevel) // не тот уровень мы считали нижним!
		{
			minlevel=energy_level;
			counter=0;
			KCOnTick(state); // отладка
			return;
		}
		if(energy_level>minlevel+1) // снова не тот уровень мы считали базовым для спокойствия! но уже в сторону повышения.
		{
			minlevel=energy_level-1;
			counter=0;
			KCOnTick(state); // отладка
			return;
		}
		// Остаёмся в нужных пределах
		counter++;
		if(counter>20)
		{
			counter=0;
			state=1;
		}
		KCOnTick(state); // отладка
		return;

	case 1: // Здесь нам позволено получить всплеск энергии, но не позволено получить понижение энергии
		if(energy_level<minlevel) // не тот уровень мы считали нижним!
		{
			state=0;
			KCOnTick(state); // отладка
			minlevel=energy_level;
			counter=0;
			return;
		}
		if(energy_level>minlevel+1) // мы выждали паузу и можем перейти в состояние 2 при всплеске
		{
#ifdef _DEBUG
			OutputDebugString(L"\r\n***NACHALO!***\r\n");
#endif
			state=2;
			KCOnTick(state); // отладка
			counter=0;
			return;
		}
		// Если же ничего из вышеперечисленного не происходит, можем оставаться в этом состоянии хоть вечность. И счетчик нам не нужен.
		KCOnTick(state); // отладка 
		return;

	case 2:
		if(energy_level<=minlevel+1) // Это то, чего я ждал! Короткий звук завершился вовремя! Теперь ждём, что после него тоже будет пауза
		{
			state=3;
			KCOnTick(state); // отладка
			counter=0;
			return;
		}
		counter++;
		// А вот находиться сдесь более 9 фреймов нельзя (восьмой уже пришёл ещё в состоянии 2)... 
		// Звук то короткий. Кто долго здесь находится, тот отсюда слетает...
		if(counter>8)
		{
			// Король на час не усидел больше положенного...
// !!! Здесь сбросить ожидающих "к" и "ч"
			Indicators::KChConfirmed=2;
#ifdef _DEBUG
			OutputDebugString(L"***dolgo :-( !***\r\n");
#endif
			state=0;
			KCOnTick(state); // отладка
			counter=0;
			return;
		}

		KCOnTick(state); // отладка 
		return; // Единыжды это может быть выполнено. Второй раз - уже перебор фреймов короткого звука.

	case 3:
		if((energy_level>minlevel+1)&&counter>3) // Не выдержал паузы! Вслед за коротким звуком вплотную последовал другой. Валим отсюда (3 первых фрейма прощается).
		{
// !!! Здесь сбросить ожидающих "к" и "ч"
			Indicators::KChConfirmed=2;
			state=0;
			KCOnTick(state); // отладка
			counter=0;
#ifdef _DEBUG
			OutputDebugString(L"\r\nLISHNY VSPLESK!\r\n");
#endif
			return;

		}
		counter++;
		if(counter>12)
		{
			// О, сладкий момент! Короткий звук пойман за хвост! Отсюда с почётом уходим в состояние 0
// !!! Здесь подтвердить ожидающих "к" и "ч"
			Indicators::KChConfirmed=1;
#ifdef _DEBUG
			OutputDebugString(L"\r\nSHORT SOUND!\r\n");
#endif
			state=1;
			KCOnTick(state); // отладка
			counter=0;
			return;
		}
		KCOnTick(state); // отладка (тут красное прёт)
		return;
	}
	
}


//=========================================================================================
// Вспомогательная функция для нажатия на клавишу
// SendInput содран из Mhook
//=========================================================================================
void KChFstate::MM_KeyDown(int i)
{
	INPUT input={0};

	if(key_to_press[i]>=0xFF00) // Спец.случай. нажатие на кнопки мыши
	{
		input.type=INPUT_MOUSE;
		
		if(0xFF00==key_to_press[i]) // левая
			input.mi.dwFlags=MOUSEEVENTF_LEFTDOWN;
		else
			input.mi.dwFlags=MOUSEEVENTF_RIGHTDOWN;
	}
	else // клавиатура
	{
		input.type=INPUT_KEYBOARD;
		input.ki.dwFlags = KEYEVENTF_SCANCODE;

		if(key_to_press[i]>0xFF) // Этот скан-код из двух байтов, где первый - E0
		{
			input.ki.dwFlags|=KEYEVENTF_EXTENDEDKEY;
		}

		input.ki.wScan=key_to_press[i];
	}
			
			
	SendInput(1,&input,sizeof(INPUT));

}

//=========================================================================================
// Вспомогательная функция для отпускания клавиши
// SendInput содран из Mhook
//=========================================================================================
void KChFstate::MM_KeyUp(int i)
{
	INPUT input={0};
	if(key_to_press[i]>=0xFF00) // Спец.случай. нажатие на кнопки мыши
	{
		input.type=INPUT_MOUSE;
				
		if(0xFF00==key_to_press[i]) // левая
			input.mi.dwFlags=MOUSEEVENTF_LEFTUP;
		else
			input.mi.dwFlags=MOUSEEVENTF_RIGHTUP;
	}
	else // клавиатура
	{
		input.type=INPUT_KEYBOARD;
		input.ki.dwFlags = KEYEVENTF_SCANCODE|KEYEVENTF_KEYUP;

		if(key_to_press[i]>0xFF) // Этот скан-код из двух байтов, где первый - E0
		{
			input.ki.dwFlags|=KEYEVENTF_EXTENDEDKEY;
		}

		input.ki.wScan=key_to_press[i];
	}

	SendInput(1,&input,sizeof(INPUT));	
}

//=========================================================================================
// Если вместо мыши нажимаем клавиши, то move обнуляется
// Диаграмма состояний http://localhost/mmish/kchfstate/trytopress.html
//=========================================================================================
LONG KChFstate::TryToPress(int i, LONG move)
{
	// Защита от дурака...
	if(i<0||i>5) return move;

	// Возможно, клавишу с номером i мы вообще не нажимаем...
	if(key_to_press[i]==0xffff) return move;

	// счётчик. один тик=1/10 секунды. переключатель можно отменить через 2 тика (1/5 секунды)
	if(key_cycle_counter[i]>0) key_cycle_counter[i]--; else key_cycle_counter[i]=0; // Защита от случайного ухода в минус.

	// 0 - клавиша не нажата
	if(0==key_state[i]) //Нажимаем, если move больше граничного значения. Пусть это будет 3
	{
		if(move>3)
		{
			if(toggle_key[i]&&(key_cycle_counter[i]>0)) // не отстоялся, чтобы переключиться. Взбаламутили снова
			{
				key_cycle_counter[i]=2;
			}
			else
			{
				MM_KeyDown(i);
				key_state[i]=1; // Клавиша нажата
				key_cycle_counter[i]=2;
			}
		}
	}
	else if(1==key_state[i]) // 1 - Клавиша нажата
	{
		if((move>0)&&(toggle_key[i])) // переключатель пытаются подтолкнуть
		{
			if(key_cycle_counter[i]>0) // не отстоялся, чтобы выключиться. Взбаламутили снова
			{
				key_cycle_counter[i]=2;
				if(repeat_key[i]) // но автоповтор работает
				{
					MM_KeyUp(i);
					key_state[i]=2;
				}
			}
			else // по крайней мере два такта его не трогали, можно отжимать
			{
				MM_KeyUp(i);
				key_state[i]=0;
				key_cycle_counter[i]=2;
			}
		}
		else if(((0==move)||(repeat_key[i]))&&(!toggle_key[i])) // Отжимаем её, только если вообще не было признаков этого звука (move==0) или [25-AUG-2017] пришло время отжатия для повтора
		{
			MM_KeyUp(i);
			key_state[i]=0; // Клавиша отжата
		}
		else if((repeat_key[i])&&(toggle_key[i])) // временно отжимаем
		{
			MM_KeyUp(i);
			key_state[i]=2;
		}
	}
	else // Осталось только состояние 2 - временно отжатая клавиша в режиме REPEAT + TOGGLE 
	{
		// Всегда нажимаем такую клавишу и переходим в состояние 1
		MM_KeyDown(i);
		key_state[i]=1; // Клавиша нажата
		if((move>0)&&(key_cycle_counter[i]>0)) key_cycle_counter[i]=2;
	}
	return 0;
}

