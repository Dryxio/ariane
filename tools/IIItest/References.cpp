#include "III.h"
#include "References.h"

CReference CReferences::aRefs[NUMREFERENCES];
CReference *CReferences::pEmptyList;

void
CReferences::Init(void)
{
	int i;
	pEmptyList = &aRefs[0];
	for(i = 0; i < NUMREFERENCES; i++){
		aRefs[i].pentity = nil;
		aRefs[i].next = &aRefs[i+1];
	}
	aRefs[NUMREFERENCES-1].next = nil;
}
