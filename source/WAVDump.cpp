#include <Windows.h>
#include <stdio.h>
#include "resource.h"
#include "WAVDump.h"

static char dump_filename[1024],dump_filename_rename[1024];
static FILE *fout=0; // не сделал членом класса, чтобы хедер на зависел от <stdio.h>

int MMWAVDump::counter=0;
HWND MMWAVDump::hdwnd=0;

// референс - WAVLoader
// Вспомогательный класс заголовка WAV-файла
class GZ_WAVHeader2{
public:
  char	RIFF[4];
  long	RIFFsize;
  char	WAVEfmt[8]; // Содержит строку "WAVEfmt "
  long  WAVEsize; // Для PCM здесь 16
  short wFormatTag; // 1 для PCM, то есть без компрессии
  unsigned short wChannels;
  unsigned long dwSamplesPerSec;
  unsigned long dwAvgBytesPerSec;
  unsigned short wBlockAlign;
  unsigned short wBitsPerSample;
// Замечание: здесь могут быть дополнительные поля, в зависимости от wFormatTag. 
  char	DATA[4];
  unsigned long	DATAsize;
};

static GZ_WAVHeader2 wh=
{
	{'R','I','F','F'}, 0, {'W','A','V','E','f','m','t',' '}, 16,1,1,16000,32000,2,16,{'d','a','t','a'},0
};

//============================================================
// Вызывается кнопкой
//============================================================
void  MMWAVDump::Start(HWND _hdwnd)
{
	SYSTEMTIME st;

	hdwnd=_hdwnd;

	if(0!=fout) return;

	// 1. Соорудим имя файла
	GetLocalTime(&st);
	sprintf_s(dump_filename,sizeof(dump_filename),"MMDump-%d-%d-%d=%d-%02d-%02d.WA_",st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);
	sprintf_s(dump_filename_rename,sizeof(dump_filename_rename),"MMDump-%d-%d-%d=%d-%02d-%02d.WAV",st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);

	// 2. Откроем же его
	fopen_s(&fout,dump_filename,"wb");

	if(0==fout) return;

	// 3. Запишем заголовок
	fwrite(&wh,sizeof(wh),1,fout);

	// 4. Меняем надпись на кнопке диалога
	SetDlgItemText(hdwnd, IDC_BUTTON_WAV_DUMP, L"Остановить запись");
}


bool MMWAVDump::DumpBuffer(short *buf, int length)
{
	if(!fout) return false;

	// пишем
	fwrite(buf,length,1,fout);

	// Увеличиваем счётчик
	counter+=length;

	// максимум у нас будет ~3 Мегабайта
	if(counter>3000000)
	{
		Stop();
		return false;
	}

	return true;
}

void MMWAVDump::Stop()
{
	// 1. Прописать нормальный header
	wh.DATAsize=counter;
	wh.RIFFsize=counter+36;
	fseek(fout,0,SEEK_SET); // проматываем на начало
	fwrite(&wh,sizeof(wh),1,fout); // перезаписываем заголовок

	// 2. Закрытие файла
	fclose(fout);
	fout=0;

	// 3. Переименовываем файл
	rename(dump_filename, dump_filename_rename);

	// 4. Меняем надпись на кнопке диалога
	SetDlgItemText(hdwnd, IDC_BUTTON_WAV_DUMP, L"Сохранять в WAV");
}