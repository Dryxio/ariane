#ifndef _OBJECTDATA_H_
#define _OBJECTDATA_H_

struct ObjectInfo
{
	float mass;
	float turnMass;
	float airResistance;
	float elasticity;
	float submerged;
	float uprootLimit;
	float colDamageMult;
	uchar damageEffect;
	uchar colResponse;
	bool  camAvoid;
};

class CObjectData
{
	static ObjectInfo ms_aObjectInfo[NUMOBJECTINFO];
public:
	static void Initialise(const char *file);
};

#endif
