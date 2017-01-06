#ifndef __MM_COPY_SHMOPY
#define __MM_COPY_SHMOPY

class CopyShmopy
{
public:
	static void Process(short *dest, short *src);
	static void Init();
	static void Halt();
	static void CS_mel_cep(float *mfspec, float *mfcep);

protected:
	static float *DoMagic(float *input_data, bool remember_master_mfcc=false);
	static void IntegrateMagic160(float *magic_data, short *dest);

	static bool initialized;
};

#endif