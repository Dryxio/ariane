#include "storiesview.h"

rw::LinkList BuildingExt::selection;
rw::LinkList EntityExt::selection;

BuildingExt*
GetBuildingExt(int id)
{
	BuildingExt *be;
	if(gLevel->buildings[id])
		return gLevel->buildings[id];
	be = (BuildingExt*)malloc(sizeof(BuildingExt));
	memset(be, 0, sizeof(BuildingExt));
	be->iplId = -1;
	be->modelId = -1;
	be->id = id;
	gLevel->buildings[id] = be;
	return be;
}

BuildingExt*
FindBuildingExt(int id)
{
	if(id >= 0 && id < 0x8000 &&
	   gLevel->buildings[id])
		return gLevel->buildings[id];
	return nil;
}


BuildingExt::Model*
BuildingExt::GetResourceInfo(int id)
{
	Model *m;
	for(m = this->resources; m; m = m->next)
		if(m->resId == id)
			return m;
	m = (Model*)malloc(sizeof(Model));
	m->next = this->resources;
	this->resources = m;
	m->resId = id;
	m->lastFrame = 0;
	return m;
}

BuildingExt*
BuildingExt::GetSelection(void)
{
	if(selection.isEmpty())
		return nil;
	return LLLinkGetData(selection.link.next, BuildingExt, inSelection);
}

void
BuildingExt::Select(void)
{
	if(this->selected)
		return;
	this->selected = true;
	selection.add(&this->inSelection);
}

void
BuildingExt::Deselect(void)
{
	if(!this->selected)
		return;
	this->selected = false;
	this->inSelection.remove();
}

void
BuildingExt::SetEntity(int iplId)
{
	if(this->iplId >= 0)
		((EntityExt*)GetEntityById(this->iplId)->vtable)->RemoveBuilding(this);
	if(iplId >= 0)
		((EntityExt*)GetEntityById(iplId)->vtable)->AddBuilding(this);
}



CEntity*
EntityExt::GetSelection(void)
{
	if(selection.isEmpty())
		return nil;
	return LLLinkGetData(selection.link.next, EntityExt, inSelection)->entity;
}

void
EntityExt::Select(void)
{
	if(this->selected)
		return;
	this->selected = true;
	selection.add(&this->inSelection);
}

void
EntityExt::Deselect(void)
{
	if(!this->selected)
		return;
	this->selected = false;
	this->inSelection.remove();
}

void
EntityExt::AddBuilding(BuildingExt *b)
{
	this->n++;
	this->insts = rwResizeT(BuildingExt*, this->insts, this->n, 0);
	this->insts[this->n-1] = b;
	b->iplId = this->GetIplID();
	b->modelId = this->entity->modelIndex;
}

void
EntityExt::RemoveBuilding(BuildingExt *b)
{
	int i;
	b->iplId = -1;
	b->modelId = -1;
	for(i = 0; i < this->n; i++)
		if(this->insts[i] == b){
			memmove(&this->insts[i], &this->insts[i+1], sizeof(void*)*(this->n-(i+1)));
			this->n--;
			break;
		}
}

int
EntityExt::GetIplID(void)
{
	void *e = this->entity;
	CBuilding *buildings = pBuildingPool->m_entries;
	CTreadable *treadables = pTreadablePool->m_entries;

	if(buildings <= e && e < &buildings[pBuildingPool->m_size])
		return pBuildingPool->GetJustIndex((CBuilding*)this->entity);
	if(treadables <= e && e < &treadables[pTreadablePool->m_size])
		return pTreadablePool->GetJustIndex((CTreadable*)this->entity) | 0x10000;
	return -1;
}

void
EntityExt::JumpTo(void)
{
	rw::V3d center;
	CBaseModelInfo *obj = CModelInfo::Get(this->entity->modelIndex);
	CSphere *sph = &obj->colModel->boundingSphere;
	rw::Matrix mat = *(rw::Matrix*)&this->entity->placeable.matrix.matrix;
	mat.optimize();
	rw::V3d::transformPoints(&center, (rw::V3d*)&sph->center, 1, &mat);
	TheCamera.setTarget(center);
	TheCamera.setDistanceFromTarget(TheCamera.minDistToSphere(sph->radius));
}



ModelInfoExt *pModelInfoExts;

static void
linkEntity(CEntity *e)
{
	ModelInfoExt *mie = GetModelInfoExt(e->modelIndex);
	mie->inst = e;
}

void
InitModelInfoExt(void)
{
	pModelInfoExts = rwNewT(ModelInfoExt, CModelInfo::msNumModelInfos, 0);
	memset(pModelInfoExts, 0, sizeof(ModelInfoExt)*CModelInfo::msNumModelInfos);

	CEntity *e;
	int i, n;

	n = pBuildingPool->GetSize();
	for(i = 0; i < n; i++){
		e = pBuildingPool->GetSlot(i);
		if(e) linkEntity(e);
	}
	n = pTreadablePool->GetSize();
	for(i = 0; i < n; i++){
		e = pTreadablePool->GetSlot(i);
		if(e) linkEntity(e);
	}
	n = pDummyPool->GetSize();
	for(i = 0; i < n; i++){
		e = pDummyPool->GetSlot(i);
		if(e) linkEntity(e);
	}
}

ModelInfoExt*
GetModelInfoExt(int id)
{
	return &pModelInfoExts[id];
}
