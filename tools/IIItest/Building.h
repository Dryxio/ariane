#ifndef _BUILDING_H_
#define _BUILDING_H_

class CBuilding : public CEntity
{
public:
	void *operator new(size_t);
	void operator delete(void*, size_t);
	CBuilding(void);

	// from CPlaceable
	~CBuilding(void);

	virtual bool GetIsATreadable(void) { return false; }
};

#endif
