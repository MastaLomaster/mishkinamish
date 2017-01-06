#ifndef __MM_MODEL
#define __MM_MODEL

#include "GZLists.h"

#define MM_Model_Num_Persons 2
#define MM_Model_Num_Sounds 6
#define MM_Model_Num_Modifiers 4

typedef struct
{
	float coeff[13];
} mfcc_t;

// Так задают гиперплоскость [22-DEC]
typedef struct
{
	mfcc_t base_point;
	mfcc_t direction;
	bool defined;
	float sigma_kvadrat;
} hyperplane_t;

class MModel
{
public:
	MModel();
	~MModel();

	int WhichSound(mfcc_t *test, bool first_found=false); // По умолчанию ищет конфликты, если явно указать - вернёт первый попавшийся
	void Capture(){EnterCriticalSection(&belongs_to_me);}
	void Release(){LeaveCriticalSection(&belongs_to_me);}
	
	void OGLDraw(int person, int sound, int modifier, int mx, int my, int mz);
	void AddSound(int person, int sound, int modifier, mfcc_t *_mfcc);

	wchar_t *Save(bool saveas, HWND hdwnd, wchar_t *_filename=NULL);
	wchar_t *Load(HWND hdwnd, wchar_t *_filename=NULL);
	void EmptyModel();
	void EmptySound(int _sound) {snd_matrix[0][_sound][0].EmptyList();UpdateMinMax();} // Для удобства StartTrainingMode()
	int IsSoundFilled(int _sound){return snd_matrix[0][_sound][0].num_elements;} // Для индикации восклицательных знаков
	void DumpC(); // Временно

	void LoadTestData(); // Для отладки
	float global_min_max_values[13][2]; // min и max агрегированные для всех звуков
	float min_max_values[MM_Model_Num_Persons][MM_Model_Num_Sounds][MM_Model_Num_Modifiers][13][2]; // min и max агрегированные для звука конкретного человека и конкретной модификации

	// Для отладки вынес в public
	hyperplane_t hplane[MM_Model_Num_Sounds+1][MM_Model_Num_Sounds+1];
protected:
	float ClaculateBasePoint(int from, int to); // см. [24-DEC]
	GZSList<mfcc_t> snd_matrix[MM_Model_Num_Persons][MM_Model_Num_Sounds][MM_Model_Num_Modifiers];
	float decision_matrix[MM_Model_Num_Sounds][13][2]; // min и max агрегированные для звука  (по всем людям и модификациям?)
	CRITICAL_SECTION belongs_to_me; // Для монопольного использования модели (изменение и отрисовка не могут происходить одновременно)
	void UpdateMinMax();
};

#endif