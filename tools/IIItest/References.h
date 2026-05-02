#ifndef _REFERENCES_H_
#define _REFERENCES_H_

class CEntity;

struct CReference
{
	CReference *next;
	CEntity **pentity;
};

class CReferences
{
	static CReference aRefs[NUMREFERENCES];
public:
	static CReference *pEmptyList;
	static void Init(void);
};

#endif
