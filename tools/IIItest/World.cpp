#include "III.h"

CPtrList CWorld::ms_bigBuildingsList[4];
CPtrList CWorld::ms_listMovingEntityPtrs;
CSector  CWorld::ms_aSectors[NUMSECTORS_Y][NUMSECTORS_X];
uint16 CWorld::ms_nCurrentScanCode;

bool CWorld::bNoMoreCollisionTorque;

void
CWorld::Initialise(void)
{
	bNoMoreCollisionTorque = false;
}

void
CWorld::Add(CEntity *ent)
{
//	if(ent->m_type == ENTITY_TYPE_VEHICLE ||
//	   ent->m_type == ENTITY_TYPE_PED)
//		; // do AUDIO stuff

	if(ent->bIsBIGBuilding)
		ms_bigBuildingsList[ent->m_level].InsertItem(ent);
	else
		ent->Add();

	if(ent->m_type != ENTITY_TYPE_BUILDING &&
	   ent->m_type != ENTITY_TYPE_DUMMY &&
	   !ent->bIsStatic)
		((CPhysical*)ent)->AddToMovingList();
}

void
CWorld::ClearScanCodes(void)
{
	CPtrNode *node;
	for(int i = 0; i < NUMSECTORS_Y; i++)
	for(int j = 0; j < NUMSECTORS_X; j++){
		CSector *s = &ms_aSectors[i][j];
		for(node = s->m_lists[ENTITYLIST_BUILDINGS].first; node; node = node->next)
			((CEntity*)node->item)->m_scanCode = 0;
		for(node = s->m_lists[ENTITYLIST_VEHICLES].first; node; node = node->next)
			((CEntity*)node->item)->m_scanCode = 0;
		for(node = s->m_lists[ENTITYLIST_PEDS].first; node; node = node->next)
			((CEntity*)node->item)->m_scanCode = 0;
		for(node = s->m_lists[ENTITYLIST_OBJECTS].first; node; node = node->next)
			((CEntity*)node->item)->m_scanCode = 0;
		for(node = s->m_lists[ENTITYLIST_DUMMIES].first; node; node = node->next)
			((CEntity*)node->item)->m_scanCode = 0;
	}
}

void
dumpEntity(CEntity *e)
{
	CBaseModelInfo *mi = CModelInfo::GetModelInfo(e->m_modelIndex);
	CVector pos = e->GetPosition();
//	debug("%d %s %f %f %f\n", e->m_modelIndex, mi->GetName(), pos.x, pos.y, pos.z);
}

