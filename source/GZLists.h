#ifndef __GZ_LISTS
#define __GZ_LISTS

//***************************************************************************************
// Два списка: простой и с сортировкой по микросекундам (GZSList,GZMList)
// Итераторы изведены, как ухудшающие читабельность
//***************************************************************************************
// Часть 1. Простой список
//***************************************************************************************

//============================
// Элемент простого списка
//============================
template <class Mydata>
class GZSListable
{
template <class Mydata> friend class GZSList;
public:
	Mydata *element;
	GZSListable(Mydata *_element):element(_element),next(NULL),prev(NULL){};
	GZSListable *next,*prev;
};

//============================
// Простой список
//============================
template <class Mydata>
class GZSList
{
public:
	GZSListable<Mydata> *first,*last;
	long num_elements;

	GZSList():num_elements(0),first(NULL),last(NULL){};
	~GZSList(){EmptyList();}
	
	GZSListable<Mydata> *Add(Mydata *_element);
	void Remove(Mydata *_element);
	void EmptyList(); // опустошить список
};

//===========================================================
// Добавляем ( для простого списка порядок не важен)
//===========================================================
template <class Mydata>
GZSListable<Mydata> *GZSList<Mydata>::Add(Mydata *_element)
{
	// С этого начинаем, чтобы потом не забыть
	num_elements++;

	// Сразу создаём элемент для вставки;
	GZSListable<Mydata> *p_new= new GZSListable<Mydata>(_element);

	// добавляем в конец
	if(NULL!=last) 	last->next=p_new;
	else first=p_new;

	p_new->prev=last;
	last=p_new;

	return p_new;
};

//======================================================================
// Удаляет элемент из простого списка
//======================================================================
template <class Mydata>
void GZSList<Mydata>::Remove(Mydata *_element) 
{
	// Перебор всех элементов списка
	GZSListable<Mydata> *p=first;
	
	// Просматриваем весь список и находим элемент
	while(NULL!=p)
	{
		if(p->element==_element) // Нашли
		{
			num_elements--;

			if(p->prev==NULL) first=p->next;
			else p->prev->next=p->next;

			if(p->next==NULL) last=p->prev;
			else p->next->prev=p->prev;

			delete p; // Всегда ли нужно?
			return;
		}
		p=p->next;
	}
}

//======================================================================
// опустошить список
//======================================================================
template <class Mydata>
void GZSList<Mydata>::EmptyList() 
{
	// Перебор всех элементов списка
	GZSListable<Mydata> *p=first,*p_next;
	
	// Уничтожаем элементы один за другим
	while(NULL!=p)
	{
		p_next=p->next;
		delete p;
		p=p_next;
	}

	first=NULL; last=NULL; num_elements=0;

}

//***************************************************************************************
// Часть 2. Список с сортировкой по микросекундам (содрано из Listable в GZ64a)
//***************************************************************************************

//============================
// Элемент списка c timestamp
//============================
template <class Mydata>
class GZTListable
{
template <class Mydata> friend class GZTList;
public:
	Mydata *element;
	__int64 timestamp;
	GZTListable(Mydata *_element, __int64 _timestamp):element(_element),timestamp(_timestamp),next(NULL),prev(NULL){};
	~GZTListable(){	delete element;} // в отличие от простого списка - элемент удаляется
	GZTListable *next,*prev;
};

//===================================
// Cписок c сортировкой по timestamp
//===================================
template <class Mydata>
class GZTList
{
public:
	GZTListable<Mydata> *first,*last;
	long num_elements;

	GZTList():num_elements(0),first(NULL),last(NULL){};
	~GZTList(){EmptyList();}
	
	GZTListable<Mydata> *Add(Mydata *_element, __int64 _timestamp);
	
	void Remove(Mydata *_element);
	void EmptyList(); // опустошить список
		
	//GZTListable<Mydata> *operator [] (int num);
	//GZTListable<Mydata> *Insert(Mydata *_element, __int64 _timestamp, int index); // Вставить по конкретному номеру
	//GZTListable<Mydata> *Closest(__int64 _timestamp, __int64 range=10000); // поиск в диапазоне 1/100 секунды
};

//===========================================================
// Вставляем, руководствуясь timestamp см. день 27
//===========================================================
template <class Mydata>
GZTListable<Mydata> *GZTList<Mydata>::Add(Mydata *_element, __int64 _timestamp)
{
	// С этого начинаем, чтобы потом не забыть
	num_elements++;

	// Перебираем все отсчеты, пока не найдём с бОльшим timestamp или конец
	// Сразу создаём элемент для вставки;
	GZTListable<Mydata> *p=first, *p_new= new GZTListable<Mydata>(_element, _timestamp);

	while(NULL!=p) // p - валидный элемент списка, кандидат на следующий по возрастанию
	{
		if(p->timestamp>_timestamp) // наткнулись на элемент, чей timestamp больше нашего
		{
			// Корректируем связки новенького и того, кто указывал на старенького
			// 1. новенький
			p_new->next=p; 
			p_new->prev=p->prev;

			// 2. ссылка в предыдущем элементе (или first)
			if(NULL==p->prev) first=p_new;
			else p->prev->next=p_new;

			// 3. ссылка в последующем элементе
			p->prev=p_new;
			return p_new;
		}
		p=p->next; // берём либо переменную first в самом списке, либо переменную next в элементе списка
	}

	// не нашли кандидата с бОльшим timestamp, добавляем в конец списка
	if(NULL!=last) 	last->next=p_new;
	else first=p_new;

	p_new->prev=last;
	last=p_new;

	return p_new;
};

//======================================================================
// Удаляет элемент из списка
//======================================================================
template <class Mydata>
void GZTList<Mydata>::Remove(Mydata *_element) 
{
	// Перебор всех элементов списка
	GZTListable<Mydata> *p=first;
	
	// Просматриваем весь список и находим ту, что поближе (неоптимизировано)
	while(NULL!=p)
	{
		if(p->element==_element) // Нашли
		{
			num_elements--;

			if(p->prev==NULL) first=p->next;
			else p->prev->next=p->next;

			if(p->next==NULL) last=p->prev;
			else p->next->prev=p->prev;

			delete p; // Всегда ли нужно?
			return;
		}
		p=p->next;
	}
}

//======================================================================
// опустошить список
//======================================================================
template <class Mydata>
void GZTList<Mydata>::EmptyList() 
{
	// Перебор всех элементов списка
	GZTListable<Mydata> *p=first,*p_next;
	
	// Уничтожаем элементы один за другим
	while(NULL!=p)
	{
		p_next=p->next;
		delete p;
		p=p_next;
	}

	first=NULL; last=NULL; num_elements=0;

}

#endif