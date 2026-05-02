#ifndef _PED_H_
#define _PED_H_

class CPed : public CPhysical
{
public:
	CEntity *m_pContactEntity;

	bool IsPlayer(void) { return false; }
};

#endif
