#include "III.h"

void*
CDummyPed::operator new(size_t)
{
	CDummyPed *dummy = (CDummyPed*)CPools::GetDummyPool()->New();
	assert(dummy);
	return dummy;
}

void
CDummyPed::operator delete(void *p, size_t)
{
	CPools::GetDummyPool()->Delete((CDummy*)p);
}


CDummyPed::CDummyPed(void)
{
}

CDummyPed::~CDummyPed(void)
{
}
