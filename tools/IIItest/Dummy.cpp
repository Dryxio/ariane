#include "III.h"

CDummy::CDummy(void)
{
	m_type = ENTITY_TYPE_DUMMY;
	m_entryInfoList = nil;
}

CDummy::~CDummy(void)
{
	// TODO:
	//m_entryInfoList->Flush();
}
