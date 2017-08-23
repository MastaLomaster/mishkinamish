#ifndef __MM_KCFSTATE
#define __MM_KCFSTATE

// Конечный автомат для ловли коротких звуков (типа "к" и "ч") [18-DEC]

class KChFstate
{
public:
	static void NewFrame(int energy_level);
	static bool IsKCValid(); // может ли к или Ч быть рассмотрена кандидатом на нажатие (в нужное ли время ?)
	static volatile bool flag_kc_anytime; // флаг деградирует конечный автомат для обработки K/Ch в любое время
	static volatile int next_kc_counter; // Через сколько фреймов можно нажать  К/Ч (в упрощенном режиме)

	// [23-AUG-2017] Лучшего места для хранения нажимаемых клавиш не нашлось...
	static volatile WORD key_to_press[6];
	static void SetKeyToPress(int i, WORD key){if((i>=0)&&(i<6)) key_to_press[i]=key;}
	static LONG TryToPress(int i, LONG move);
protected:
	static volatile int state,minlevel,counter;
	static int key_state[6]; // нажата-отжата
};

#endif