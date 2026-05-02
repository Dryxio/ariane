#include "III.h"
#include "Camera.h"

//
// CBaseModelInfo
//

CBaseModelInfo::CBaseModelInfo(int type)
{
	m_name[0] = '\0';
	m_colModel = nil;
	m_twodEffects = nil;
	m_objectId = -1;
	m_refCount = 0;
	m_txdSlot = -1;
	m_type = type;
	m_num2dEffects = 0;
	m_freeCol = false;
}

void
CBaseModelInfo::AddRef(void)
{
	m_refCount++;
	AddTexDictionaryRef();
}

void
CBaseModelInfo::RemoveRef(void)
{
	m_refCount--;
	RemoveTexDictionaryRef();
}

void
CBaseModelInfo::SetTexDictionary(const char *name)
{
	int slot = CTxdStore::FindTxdSlot(name);
	if(slot < 0)
		slot = CTxdStore::AddTxdSlot(name);
	m_txdSlot = slot;
}

void
CBaseModelInfo::AddTexDictionaryRef(void)
{
	CTxdStore::AddRef(m_txdSlot);
}

void
CBaseModelInfo::RemoveTexDictionaryRef(void)
{
	CTxdStore::RemoveRef(m_txdSlot);
}

void
CBaseModelInfo::Add2dEffect(C2dEffect *fx)
{
	if(m_twodEffects)
		m_num2dEffects++;
	else{
		m_num2dEffects = 1;
		m_twodEffects = fx;
	}
}

//
// CSimpleModelInfo
//

void
CSimpleModelInfo::DeleteRwObject(void)
{
	int i;
	for(i = 0; i < m_numAtomics; i++)
		if(m_atomics[i]){
			rw::Frame *f = m_atomics[i]->getFrame();
			m_atomics[i]->destroy();
			f->destroy();
			m_atomics[i] = nil;
			RemoveTexDictionaryRef();
		}
}

rw::Object*
CSimpleModelInfo::CreateInstance(void)
{
	if(GetRwObject()){
		rw::Atomic *a = ((rw::Atomic*)GetRwObject())->clone();
		a->setFrame(rw::Frame::create());
		return (rw::Object*)a;
	}else
		return nil;
}

rw::Object*
CSimpleModelInfo::CreateInstance(rw::Matrix *mat)
{
	if(GetRwObject()){
		rw::Atomic *a = (rw::Atomic*)CreateInstance();
		a->getFrame()->matrix = *mat;
		return (rw::Object*)a;
	}else
		return nil;
}

void
CSimpleModelInfo::Init(void)
{
	m_atomics[0] = nil;
	m_atomics[1] = nil;
	m_atomics[2] = nil;
	m_numAtomics = 0;
	m_furthest      = 0;
	m_normalCull    = 0;
	m_isDamaged     = 0;
	m_isBigBuilding = 0;
	m_noFade        = 0;
	m_drawLast      = 0;
	m_additive      = 0;
	m_isSubway      = 0;
	m_ignoreLight   = 0;
	m_noZwrite      = 0;
}

void
CSimpleModelInfo::SetAtomic(int n, rw::Atomic *atomic)
{
	AddTexDictionaryRef();
	m_atomics[n] = atomic;
	if(m_ignoreLight)
		atomic->geometry->flags &= ~rw::Geometry::LIGHT;
}

void
CSimpleModelInfo::SetLodDistances(float *dist)
{
	m_lodDistances[0] = dist[0];
	m_lodDistances[1] = dist[1];
	m_lodDistances[2] = dist[2];
}

float
CSimpleModelInfo::GetLargestLodDistance(void)
{
	float d;
	// TODO: what exactly is going on here?
	if(m_furthest != 0 && !m_isDamaged)
		d = m_lodDistances[m_furthest-1];
	else
		d = m_lodDistances[m_numAtomics-1];
	return d * TheCamera.LODDistMultiplier;
}

rw::Atomic*
CSimpleModelInfo::GetAtomicFromDistance(float dist)
{
	int i;
	i = 0;
	// TODO: what exactly is going on here?
	if(m_isDamaged)
		i = m_furthest;
	for(; i < m_numAtomics; i++)
		if(dist < m_lodDistances[i]*TheCamera.LODDistMultiplier)
			return m_atomics[i];
	return nil;
}

void
CSimpleModelInfo::FindRelatedModel(void)
{
	int i;
	CBaseModelInfo *mi;
	for(i = 0; i < MODELINFOSIZE; i++){
		mi = CModelInfo::GetModelInfo(i);
		if(mi && mi != this &&
		   strcmp(GetName()+3, mi->GetName()+3) == 0){
			assert(mi->IsSimple());
			this->SetRelatedModel((CSimpleModelInfo*)mi);
			return;
		}
	}
}

void
CSimpleModelInfo::SetupBigBuilding(void)
{
	CSimpleModelInfo *related;
	if(m_lodDistances[0] > LOD_DISTANCE && m_atomics[2] == nil){
		m_isBigBuilding = 1;
		FindRelatedModel();
		related = GetRelatedModel();
		if(related)
			m_lodDistances[2] = related->GetLargestLodDistance()/TheCamera.LODDistMultiplier;
		else
			m_lodDistances[2] = 100.0f;
	}
}

//
// CTimeModelInfo
//

CTimeModelInfo*
CTimeModelInfo::FindOtherTimeModel(void)
{
	char name[40];
	char *p;
	int i;

	strcpy(name, GetName());
	// change _nt to _dy
	if(p = strstr(name, "_nt"))
		strncpy(p, "_dy", 4);
	// change _dy to _nt
	else if(p = strstr(name, "_dy"))
		strncpy(p, "_nt", 4);
	else
		return nil;

	for(i = 0; i < MODELINFOSIZE; i++){
		CBaseModelInfo *mi = CModelInfo::GetModelInfo(i);
		if(mi && mi->m_type == TIMEMODELINFO &&
		   strncmp(name, mi->GetName(), 24) == 0){
			m_otherTimeModelID = i;
			return (CTimeModelInfo*)mi;
		}
	}
	return nil;
}

CTimeModelInfo*
CTimeModelInfo::GetOtherModel(void)
{
	return (CTimeModelInfo*)CModelInfo::GetModelInfo(m_otherTimeModelID);
}

//
// CClumpModelInfo
//

void
CClumpModelInfo::DeleteRwObject(void)
{
	if(m_clump){
		m_clump->destroy();
		m_clump = nil;
		RemoveTexDictionaryRef();
	}
}

rw::Object*
CClumpModelInfo::CreateInstance(void)
{
	return m_clump ? (rw::Object*)m_clump->clone() : nil;
}

rw::Object*
CClumpModelInfo::CreateInstance(rw::Matrix *mat)
{
	rw::Clump *clump;
	if(m_clump == nil)
		return nil;
	clump = (rw::Clump*)CreateInstance();
	clump->getFrame()->matrix = *mat;
	return (rw::Object*)clump;
}

void
CClumpModelInfo::SetClump(rw::Clump *clump)
{
	m_clump = clump;
	AddTexDictionaryRef();
	// TODO: more
}

//
// CVehicleModelInfo
//

void
CVehicleModelInfo::DeleteRwObject(void)
{
	// TODO: comps
	CClumpModelInfo::DeleteRwObject();
}

rw::Object*
CVehicleModelInfo::CreateInstance(void)
{
	rw::Clump *clump;
	clump = (rw::Clump*)CClumpModelInfo::CreateInstance();
	// TODO: comps
	return (rw::Object*)clump;
}

void
CVehicleModelInfo::SetClump(rw::Clump *clump)
{
	CClumpModelInfo::SetClump(clump);
	SetAtomicRenderCallbacks();
	// CClumpModelInfo::SetFrameIds
	// CVehicleModelInfo::PreprocessHierarchy
	// CVehicleModelInfo::FindEditableMaterialList
	m_envMap = nil;
	// CVehicleModelInfo::SetEnvironmentMap
}

static bool
hasAlpha(rw::Geometry *g)
{
	for(int32 i = 0; i < g->matList.numMaterials; i++)
		if(g->matList.materials[i]->color.alpha != 255)
			return true;
	return false;
}

void
CVehicleModelInfo::SetAtomicRenderer(rw::Atomic *atm, rw::Clump *clump)
{
	char *name = GetFrameNodeName(atm->getFrame());
	bool alpha = hasAlpha(atm->geometry);
	if(strstr(name, "_hi") || strncmp(name, "extra", 5) == 0){
		if(alpha || strncmp(name, "windscreen", 10) == 0)
			CVisibilityPlugins::SetAtomicRenderCallback(atm, CVisibilityPlugins::RenderVehicleHiDetailAlphaCB);
		else
			CVisibilityPlugins::SetAtomicRenderCallback(atm, CVisibilityPlugins::RenderVehicleHiDetailCB);
	}else if(strstr(name, "_lo")){
		atm->clump->removeAtomic(atm);
		atm->destroy();
		return;
	}else if(strstr(name, "_vlo"))
		CVisibilityPlugins::SetAtomicRenderCallback(atm, CVisibilityPlugins::RenderVehicleReallyLowDetailCB);
	else
		CVisibilityPlugins::SetAtomicRenderCallback(atm, nil);
	HideDamagedAtomic(atm);
}

void
CVehicleModelInfo::SetAtomicRenderer_Boat(rw::Atomic *atm, rw::Clump *clump)
{
	char *name = GetFrameNodeName(atm->getFrame());
	if(strcmp(name, "boat_hi") == 0 || strncmp(name, "extra", 5) == 0)
		CVisibilityPlugins::SetAtomicRenderCallback(atm, CVisibilityPlugins::RenderVehicleHiDetailCB_Boat);
	else if(strstr(name, "_hi"))
		CVisibilityPlugins::SetAtomicRenderCallback(atm, CVisibilityPlugins::RenderVehicleHiDetailCB);
	else if(strstr(name, "_lo")){
		atm->clump->removeAtomic(atm);
		atm->destroy();
		return;
	}else if(strstr(name, "_vlo"))
		CVisibilityPlugins::SetAtomicRenderCallback(atm, CVisibilityPlugins::RenderVehicleReallyLowDetailCB_BigVehicle);
	else
		CVisibilityPlugins::SetAtomicRenderCallback(atm, nil);
	HideDamagedAtomic(atm);
}

void
CVehicleModelInfo::SetAtomicRenderer_Train(rw::Atomic *atm, rw::Clump *clump)
{
	char *name = GetFrameNodeName(atm->getFrame());
	bool alpha = hasAlpha(atm->geometry);
	if(strstr(name, "_hi")){
		if(alpha)
			CVisibilityPlugins::SetAtomicRenderCallback(atm, CVisibilityPlugins::RenderTrainHiDetailAlphaCB);
		else
			CVisibilityPlugins::SetAtomicRenderCallback(atm, CVisibilityPlugins::RenderTrainHiDetailCB);
	}else if(strstr(name, "_vlo"))
		CVisibilityPlugins::SetAtomicRenderCallback(atm, CVisibilityPlugins::RenderVehicleReallyLowDetailCB_BigVehicle);
	else
		CVisibilityPlugins::SetAtomicRenderCallback(atm, nil);
	HideDamagedAtomic(atm);
}

void
CVehicleModelInfo::SetAtomicRenderer_Heli(rw::Atomic *atm, rw::Clump *clump)
{
	CVisibilityPlugins::SetAtomicRenderCallback(atm, nil);
}

void
CVehicleModelInfo::SetAtomicRenderer_BigVehicle(rw::Atomic *atm, rw::Clump *clump)
{
	char *name = GetFrameNodeName(atm->getFrame());
	bool alpha = hasAlpha(atm->geometry);
	if(strstr(name, "_hi") || strncmp(name, "extra", 5) == 0){
		if(alpha)
			CVisibilityPlugins::SetAtomicRenderCallback(atm, CVisibilityPlugins::RenderVehicleHiDetailAlphaCB_BigVehicle);
		else
			CVisibilityPlugins::SetAtomicRenderCallback(atm, CVisibilityPlugins::RenderVehicleHiDetailCB_BigVehicle);
	}else if(strstr(name, "_lo")){
		if(alpha)
			CVisibilityPlugins::SetAtomicRenderCallback(atm, CVisibilityPlugins::RenderVehicleLowDetailAlphaCB_BigVehicle);
		else
			CVisibilityPlugins::SetAtomicRenderCallback(atm, CVisibilityPlugins::RenderVehicleLowDetailCB_BigVehicle);
	}else if(strstr(name, "_vlo"))
		CVisibilityPlugins::SetAtomicRenderCallback(atm, CVisibilityPlugins::RenderVehicleReallyLowDetailCB_BigVehicle);
	else
		CVisibilityPlugins::SetAtomicRenderCallback(atm, nil);
	HideDamagedAtomic(atm);
}

void
CVehicleModelInfo::HideDamagedAtomic(rw::Atomic *atm)
{
	char *name = GetFrameNodeName(atm->getFrame());
	if(strstr(name, "_dam")){
		atm->setFlags(0);
		CVisibilityPlugins::SetAtomicFlag(atm, 2);
	}else if(strstr(name, "_ok"))
		CVisibilityPlugins::SetAtomicFlag(atm, 2);
}

void
CVehicleModelInfo::SetAtomicRenderCallbacks(void)
{
	void (*f)(rw::Atomic*,rw::Clump*);
	switch(m_vehicleType){
	case Car:
	default: f = SetAtomicRenderer; break;
	case Boat: f = SetAtomicRenderer_Boat; break;
	case Train: f = SetAtomicRenderer_Train; break;
	case Heli: f = SetAtomicRenderer_Heli; break;
	case Plane: f = SetAtomicRenderer_BigVehicle; break;
	}
	FORLIST(lnk, m_clump->atomics)
		f(rw::Atomic::fromClump(lnk), m_clump);
}

//
// CModelInfo
//

CBaseModelInfo *CModelInfo::ms_modelInfoPtrs[MODELINFOSIZE];
CStore<CSimpleModelInfo, SIMPLEMODELSIZE> CModelInfo::ms_simpleModelStore;
CStore<CTimeModelInfo, TIMEMODELSIZE> CModelInfo::ms_timeModelStore;
CStore<CClumpModelInfo, CLUMPMODELSIZE> CModelInfo::ms_clumpModelStore;
CStore<CPedModelInfo, PEDMODELSIZE> CModelInfo::ms_pedModelStore;
CStore<CVehicleModelInfo, VEHICLEMODELSIZE> CModelInfo::ms_vehicleModelStore;
CStore<C2dEffect, TWODFXSIZE> CModelInfo::ms_2dEffectStore;

void
CModelInfo::Initialise(void)
{
	CSimpleModelInfo *mi;

	for(int i = 0; i < MODELINFOSIZE; i++)
		ms_modelInfoPtrs[i] = nil;
	ms_simpleModelStore.clear();
	ms_timeModelStore.clear();
	ms_clumpModelStore.clear();
	ms_pedModelStore.clear();
	ms_vehicleModelStore.clear();
	ms_2dEffectStore.clear();

#define NEWMODEL(id, col) \
	mi = CModelInfo::AddSimpleModel(id);\
	mi->SetColModel(&col, false);\
	mi->SetTexDictionary("generic");\
	mi->m_numAtomics = 1;\
	mi->m_lodDistances[0] = 80.0f;

	// car components
	NEWMODEL(190, CTempColModels::ms_colModelDoor1);
	NEWMODEL(191, CTempColModels::ms_colModelBumper1);
	NEWMODEL(192, CTempColModels::ms_colModelPanel1);
	NEWMODEL(193, CTempColModels::ms_colModelBonnet1);
	NEWMODEL(194, CTempColModels::ms_colModelBoot1);
	NEWMODEL(195, CTempColModels::ms_colModelWheel1);

	// ped components
	NEWMODEL(196, CTempColModels::ms_colModelBodyPart1);
	NEWMODEL(197, CTempColModels::ms_colModelBodyPart2);

#undef NEWMODEL
}

CSimpleModelInfo*
CModelInfo::AddSimpleModel(int id)
{
	CSimpleModelInfo *modelinfo;
	modelinfo = CModelInfo::ms_simpleModelStore.alloc();
	CModelInfo::ms_modelInfoPtrs[id] = modelinfo;
	modelinfo->Init();
	return modelinfo;
}

CTimeModelInfo*
CModelInfo::AddTimeModel(int id)
{
	CTimeModelInfo *modelinfo;
	modelinfo = CModelInfo::ms_timeModelStore.alloc();
	CModelInfo::ms_modelInfoPtrs[id] = modelinfo;
	modelinfo->Init();
	return modelinfo;
}

CClumpModelInfo*
CModelInfo::AddClumpModel(int id)
{
	CClumpModelInfo *modelinfo;
	modelinfo = CModelInfo::ms_clumpModelStore.alloc();
	CModelInfo::ms_modelInfoPtrs[id] = modelinfo;
	modelinfo->m_clump = nil;
	return modelinfo;
}

CPedModelInfo*
CModelInfo::AddPedModel(int id)
{
	CPedModelInfo *modelinfo;
	modelinfo = CModelInfo::ms_pedModelStore.alloc();
	CModelInfo::ms_modelInfoPtrs[id] = modelinfo;
	modelinfo->m_clump = nil;
	return modelinfo;
}

CVehicleModelInfo*
CModelInfo::AddVehicleModel(int id)
{
	CVehicleModelInfo *modelinfo;
	modelinfo = CModelInfo::ms_vehicleModelStore.alloc();
	CModelInfo::ms_modelInfoPtrs[id] = modelinfo;
	modelinfo->m_clump = nil;
	modelinfo->m_vehicleType = -1;
	modelinfo->m_extraModelIndex = -1;
	modelinfo->m_primaryMaterials[0] = nil;
	modelinfo->m_secondaryMaterials[0] = nil;
	return modelinfo;
}

CBaseModelInfo*
CModelInfo::GetModelInfo(const char *name, int *id)
{
	CBaseModelInfo *modelinfo;
	for(int i = 0; i < MODELINFOSIZE; i++){
		modelinfo = CModelInfo::ms_modelInfoPtrs[i];
	 	if(modelinfo && rw::strncmp_ci(modelinfo->GetName(), name, 24) == 0){
			if(id)
				*id = i;
			return modelinfo;
		}
	}
	return nil;
}
