#ifndef __MM_CLICKSOUND
#define __MM_CLICKSOUND

#include "MMGlobals.h"

#define MM_CLICK_SAMPLES (MM_SOUND_BUFFER_LEN/2)

class ClickSound
{
public:
	static void Init();
	static void AddSound(short *buf, int daite_dve); // Умещается в один буфер, поэтому делаем проще
protected:
	static bool initialized;
	static int remaining_samples;
	static short ClickSound::samples[2][MM_CLICK_SAMPLES];
};
#endif