#include "III.h"

void*
CTreadable::operator new(size_t)
{
	CTreadable *treadable = CPools::GetTreadablePool()->New();
	assert(treadable);
	return treadable;
}

void
CTreadable::operator delete(void *p, size_t)
{
	CPools::GetTreadablePool()->Delete((CTreadable*)p);
}
