#ifndef _DUMMY_H_
#define _DUMMY_H_

class CDummy : public CEntity
{
	// disable allocation
	static void *operator new(size_t) { assert(0); return nil; }
	static void operator delete(void*, size_t) { assert(0); }

protected:
	void *m_entryInfoList;

public:
	CDummy(void);

	// from CPlaceable
	~CDummy(void);
};

#endif
