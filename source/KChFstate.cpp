#include <Windows.h>
#include "KChFstate.h"
#include "Indicators.h"

volatile int KChFstate::state(0), KChFstate::minlevel(0), KChFstate::counter(0);
volatile bool KChFstate::flag_kc_anytime=false;
volatile int KChFstate::next_kc_counter=0; // Через сколько фреймов можно нажать  К/Ч (в упрощенном режиме)

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
			return;
		}
		if(energy_level>minlevel+1) // снова не тот уровень мы считали базовым для спокойствия! но уже в сторону повышения.
		{
			minlevel=energy_level-1;
			counter=0;
			return;
		}
		// Остаёмся в нужных пределах
		counter++;
		if(counter>20)
		{
			counter=0;
			state=1;
		}
		return;

	case 1: // Здесь нам позволено получить всплеск энергии, но не позволено получить понижение энергии
		if(energy_level<minlevel) // не тот уровень мы считали нижним!
		{
			state=0;
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
			counter=0;
			return;
		}
		// Если же ничего из вышеперечисленного не происходит, можем оставаться в этом состоянии хоть вечность. И счетчик нам не нужен.
		return;

	case 2:
		if(energy_level<=minlevel+1) // Это то, чего я ждал! Короткий звук завершился вовремя! Теперь ждём, что после него тоже будет пауза
		{
			state=3;
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
			counter=0;
			return;
		}
		return; // Единыжды это может быть выполнено. Второй раз - уже перебор фреймов короткого звука.

	case 3:
		if((energy_level>minlevel+1)&&counter>3) // Не выдержал паузы! Вслед за коротким звуком вплотную последовал другой. Валим отсюда (3 первых фрейма прощается).
		{
// !!! Здесь сбросить ожидающих "к" и "ч"
			Indicators::KChConfirmed=2;
			state=0;
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
			state=0;
			counter=0;
			return;
		}
		return;
	}
	
}