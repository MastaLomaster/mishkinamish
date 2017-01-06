//================================================================
// Кастрированный сфинкс - чиста для подсчёта MFCC
//================================================================
#ifndef __CAS_SPHINX
#define __CAS_SPHINX

#define M_PI 3.14159265358979323846

#define MEL_NUM_FILTERS 40
#define MEL_NUM_CEPSTRA 13

void cas_mel_spec(float *mfspec, float *spec);

#endif