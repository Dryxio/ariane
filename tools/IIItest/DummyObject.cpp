#include "III.h"

void*
CDummyObject::operator new(size_t)
{
	CDummyObject *dummy = (CDummyObject*)CPools::GetDummyPool()->New();
	assert(dummy);
	return dummy;
}

void
CDummyObject::operator delete(void *p, size_t)
{
	CPools::GetDummyPool()->Delete((CDummy*)p);
}


CDummyObject::CDummyObject(void)
{
}

CDummyObject::~CDummyObject(void)
{
}
