#ifndef _TREADABLE_H_
#define _TREADABLE_H_

class CTreadable : public CBuilding
{
public:
	void *operator new(size_t);
	void operator delete(void*, size_t);
	CTreadable(void) {}

	// from CPlaceable
	~CTreadable(void) {}

	// from CBuilding
	bool GetIsATreadable(void) { return true; }
};

#endif
