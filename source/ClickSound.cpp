#include <Windows.h>
#include <math.h>
#include "ClickSound.h"

bool ClickSound::initialized=false;
int ClickSound::remaining_samples=0;
short ClickSound::samples[2][MM_CLICK_SAMPLES];

// до до
static float freq[2]={523.25f,1046.50f};
#define PI 3.14159

//===================================================
// Заполняем звуком буфер
//===================================================
void ClickSound::Init()
{
	// Код содран из bkb:Click
	int i,j;
	
	// Огибающая - берём косинус + 1 в интервале от -PI до PI
	// Сам сигнал: sin(2*PI*freq[i]) (Учитывая, что сигнал занимает 205/16000=0.013 секунды)
	//samples[i][j]=10000*(1+cos(-PI+2*PI/BKB_CLICK_SAMPLES*j))*sin(freq[i]*0.015*(2.0*PI/BKB_CLICK_SAMPLES*j));
	// Попробуем чуток уводить частоту вверх
	for(i=0;i<2;i++)
	{
		for(j=0;j<MM_CLICK_SAMPLES;j++)
		{
			samples[i][j]=10000*(1+cos(-PI+2*PI/MM_CLICK_SAMPLES*j))*sin((freq[i]+freq[i]*0.5*j/MM_CLICK_SAMPLES)*205.0/16000.0*(2.0*PI/MM_CLICK_SAMPLES*j));
		}
	}

	initialized=true;
}

//=============================================================
// Переписываем буфер куда просят
// Умещается в один буфер, поэтому делаем проще, чем задумали
//=============================================================
void ClickSound::AddSound(short *buf, int daite_dve)
{
	if(daite_dve<1||daite_dve>2) return;
	if(!initialized) Init();

	int size=MM_SOUND_BUFFER_LEN/2*daite_dve;
	
	CopyMemory(buf,samples,size*sizeof(short));
}