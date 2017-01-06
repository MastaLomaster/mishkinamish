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
protected:
	static volatile int state,minlevel,counter;
};

#endif