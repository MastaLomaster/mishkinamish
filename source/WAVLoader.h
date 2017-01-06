#ifndef __MM_WAVLOADER
#define __MM_WAVLOADER

class MMWAVLoader
{
public:
	static void FillBuffer(short *input_buf);
	static TCHAR *LoadWavFile(TCHAR *_filename, HWND hdwnd);
protected:
	static unsigned long position, length;
	static void snd_left_right();
	//static bool f_samples_invalid;
};

#endif