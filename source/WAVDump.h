#ifndef __MM_WAVDUMP
#define __MM_WAVDUMP

class MMWAVDump
{
public:
	static bool DumpBuffer(short *buf, int length);
	static void Stop();
	static void Start(HWND _hdwnd); 
protected:
	static int counter;
	static HWND hdwnd;
};

#endif