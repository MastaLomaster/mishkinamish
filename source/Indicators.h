#ifndef __MM_INDICATORS
#define __MM_INDICATORS

class Indicators
{
public:
	static void Init(HWND _hdwnd);
	static void Halt(); // тут прибьём битмапы
	static void DrawLevel();
	static void Draw(HDC hdc);

	static volatile LONG KChConfirmed; // [18-dec]
protected:
	static HWND hdwnd;
	static HDC memdc,memdc2;
	static HBITMAP hbm, hbm2;
	static HBRUSH red_brush, green_brush;
	static HPEN red_pen;

	static int xpos,ypos,xsize,ysize;

	static int xpos6c[6], ypos6c[6]; // 0..5 = И У O Э K Ч

	static int KChWaits[2];

/*	static int xpos_K,ypos_K, xpos_I,ypos_I;
	static int xpos_E,ypos_E, xpos_U,ypos_U;
	static int xpos_O,ypos_O, xpos_CH,ypos_CH;
*/
	static int hor_increase, vert_increase;

};

#endif