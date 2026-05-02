#include "III.h"

void*
CBuilding::operator new(size_t)
{
	CBuilding *building = CPools::GetBuildingPool()->New();
	assert(building);
	return building;
}

void
CBuilding::operator delete(void *p, size_t)
{
	CPools::GetBuildingPool()->Delete((CBuilding*)p);
}


CBuilding::CBuilding(void)
{
//	printf("CBuilding::CBuilding()\n");

	m_type = ENTITY_TYPE_BUILDING;
	bUsesCollision = true;
}

CBuilding::~CBuilding(void)
{
//	printf("CBuilding::~CBuilding()\n");
}
