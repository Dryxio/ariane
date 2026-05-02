#include "III.h"
#include "Camera.h"

// have to put these somewhere
int TempBufferIndicesStored;
int TempBufferVerticesStored;
rw::RWDEVICE::Im3DVertex TempVertexBuffer[TEMPBUFFERVERTSIZE];
uint16 TempIndexBuffer[TEMPBUFFERINDEXSIZE];



// Get rid of bullshit windows definitions, we're not running on an 8086
#ifdef far
#undef far
#undef near
#endif

int CRenderer::ms_nNoOfVisibleEntities;
int CRenderer::ms_nNoOfInVisibleEntities;
CEntity *CRenderer::ms_aVisibleEntityPtrs[2000];
CVector CRenderer::ms_vecCameraPosition;

enum Visbility
{
	VIS_INVISIBLE,
	VIS_VISIBLE,
	VIS_CULLED,
	VIS_STREAMME
};

void
CRenderer::ConstructRenderList(void)
{
	ms_nNoOfVisibleEntities = 0;
	ms_nNoOfInVisibleEntities = 0;
	ms_vecCameraPosition = TheCamera.GetPosition();
	ScanWorld();
}

void
CRenderer::ScanSectorPoly(rw::V2d *poly, int numVerts, void (*f)(CSector*))
{
	// TODO: actually scan the *polygon*
	// to simplify things we calculate the bounding rectangle
	// and just scan that instead
	int x, xmin = 100, xmax = 0;
	int y, ymin = 100, ymax = 0;
	int i;
	for(i = 0; i < numVerts; i++){
		if(poly[i].x < xmin) xmin = poly[i].x;
		if(poly[i].y < ymin) ymin = poly[i].y;
		if(poly[i].x > xmax) xmax = poly[i].x;
		if(poly[i].y > ymax) ymax = poly[i].y;
	}
	if(xmin < 0) xmin = 0;
	if(ymin < 0) ymin = 0;
	if(xmax >= NUMSECTORS_X) xmax = NUMSECTORS_X-1;
	if(ymax >= NUMSECTORS_Y) ymax = NUMSECTORS_Y-1;
	for(x = xmin; x <= xmax; x++)
		for(y = ymin; y <= ymax; y++)
			f(CWorld::GetSector(x, y));
}

enum Corners
{
	CORNER_CAM = 0,
	CORNER_FAR_TOPLEFT,
	CORNER_FAR_TOPRIGHT,
	CORNER_FAR_BOTRIGHT,
	CORNER_FAR_BOTLEFT,
	CORNER_LOD_LEFT,
	CORNER_LOD_RIGHT,
	CORNER_PRIO_LEFT,
	CORNER_PRIO_RIGHT,
};

void
CRenderer::ScanWorld(void)
{
	float far = TheCamera.m_pRwCamera->farPlane;
	rw::V2d vw = TheCamera.m_pRwCamera->viewWindow;
	rw::V3d vectors[9];
	rw::Matrix *cammatrix;
	rw::V2d poly[3];

	memset(vectors, 0, sizeof(vectors));
	vectors[CORNER_FAR_TOPLEFT].x = -vw.x * far;
	vectors[CORNER_FAR_TOPLEFT].y = vw.y * far;
	vectors[CORNER_FAR_TOPLEFT].z = far;
	vectors[CORNER_FAR_TOPRIGHT].x = vw.x * far;
	vectors[CORNER_FAR_TOPRIGHT].y = vw.y * far;
	vectors[CORNER_FAR_TOPRIGHT].z = far;
	vectors[CORNER_FAR_BOTRIGHT].x = vw.x * far;
	vectors[CORNER_FAR_BOTRIGHT].y = -vw.y * far;
	vectors[CORNER_FAR_BOTRIGHT].z = far;
	vectors[CORNER_FAR_BOTLEFT].x = -vw.x * far;
	vectors[CORNER_FAR_BOTLEFT].y = -vw.y * far;
	vectors[CORNER_FAR_BOTLEFT].z = far;

	cammatrix = &TheCamera.m_pRwCamera->getFrame()->matrix;

	CVisibilityPlugins::InitAlphaEntityList();

	CWorld::AdvanceCurrentScanCode();

	if(cammatrix->at.z > 0.0f){
		// looking up, bottom corners are further away
		vectors[CORNER_LOD_LEFT] = scale(vectors[CORNER_FAR_BOTLEFT], LOD_DISTANCE/far);
		vectors[CORNER_LOD_RIGHT] = scale(vectors[CORNER_FAR_BOTRIGHT], LOD_DISTANCE/far);
	}else{
		// looking down, top corners are further away
		vectors[CORNER_LOD_LEFT] = scale(vectors[CORNER_FAR_TOPLEFT], LOD_DISTANCE/far);
		vectors[CORNER_LOD_RIGHT] = scale(vectors[CORNER_FAR_TOPRIGHT], LOD_DISTANCE/far);
	}
	vectors[CORNER_PRIO_LEFT].x = vectors[CORNER_LOD_LEFT].x * 0.2f;
	vectors[CORNER_PRIO_LEFT].y = vectors[CORNER_LOD_LEFT].y * 0.2f;
	vectors[CORNER_PRIO_LEFT].z = vectors[CORNER_LOD_LEFT].z;
	vectors[CORNER_PRIO_RIGHT].x = vectors[CORNER_LOD_RIGHT].x * 0.2f;
	vectors[CORNER_PRIO_RIGHT].y = vectors[CORNER_LOD_RIGHT].y * 0.2f;
	vectors[CORNER_PRIO_RIGHT].z = vectors[CORNER_LOD_RIGHT].z;

	rw::V3d::transformPoints(vectors, vectors, 9, cammatrix);

	if(far <= LOD_DISTANCE){
		poly[0].x = CWorld::GetSectorIndexX(vectors[CORNER_CAM].x);
		poly[0].y = CWorld::GetSectorIndexY(vectors[CORNER_CAM].y);
		poly[1].x = CWorld::GetSectorIndexX(vectors[CORNER_FAR_TOPLEFT].x);
		poly[1].y = CWorld::GetSectorIndexY(vectors[CORNER_FAR_TOPLEFT].y);
		poly[2].x = CWorld::GetSectorIndexX(vectors[CORNER_FAR_TOPRIGHT].x);
		poly[2].y = CWorld::GetSectorIndexY(vectors[CORNER_FAR_TOPRIGHT].y);
	}else{
		// priority
		poly[0].x = CWorld::GetSectorIndexX(vectors[CORNER_CAM].x);
		poly[0].y = CWorld::GetSectorIndexY(vectors[CORNER_CAM].y);
		poly[1].x = CWorld::GetSectorIndexX(vectors[CORNER_PRIO_LEFT].x);
		poly[1].y = CWorld::GetSectorIndexY(vectors[CORNER_PRIO_LEFT].y);
		poly[2].x = CWorld::GetSectorIndexX(vectors[CORNER_PRIO_RIGHT].x);
		poly[2].y = CWorld::GetSectorIndexY(vectors[CORNER_PRIO_RIGHT].y);
		ScanSectorPoly(poly, 3, ScanSectorList);	// TODO: different function

		// below LOD
		poly[0].x = CWorld::GetSectorIndexX(vectors[CORNER_CAM].x);
		poly[0].y = CWorld::GetSectorIndexY(vectors[CORNER_CAM].y);
		poly[1].x = CWorld::GetSectorIndexX(vectors[CORNER_LOD_LEFT].x);
		poly[1].y = CWorld::GetSectorIndexY(vectors[CORNER_LOD_LEFT].y);
		poly[2].x = CWorld::GetSectorIndexX(vectors[CORNER_LOD_RIGHT].x);
		poly[2].y = CWorld::GetSectorIndexY(vectors[CORNER_LOD_RIGHT].y);
	}
	ScanSectorPoly(poly, 3, ScanSectorList);

	ScanBigBuildingList(&CWorld::GetBigBuildingList(CCollision::ms_collisionInMemory));
	ScanBigBuildingList(&CWorld::GetBigBuildingList(LEVEL_NONE));
}

void
CRenderer::ScanBigBuildingList(CPtrList *list)
{
	CPtrNode *node;
	CEntity *e;
	for(node = list->first; node; node = node->next){
		e = (CEntity*)node->item;
		if(!e->m_flagC40 && SetupBigBuildingVisibility(e))
			ms_aVisibleEntityPtrs[ms_nNoOfVisibleEntities++] = e;
	}
}

void
CRenderer::ScanSectorList(CSector *sect)
{
	CPtrList *lists;
	CPtrNode *node;
	CEntity *e;
	int i;

	lists = sect->m_lists;
	for(i = 0; i < 10; i++)
		for(node = lists[i].first; node; node = node->next){
			e = (CEntity*)node->item;
			if(e->m_scanCode == CWorld::GetCurrentScanCode())
				continue;
			e->m_scanCode = CWorld::GetCurrentScanCode();
			switch(SetupEntityVisibility(e)){
			case VIS_INVISIBLE:
				break;
			case VIS_VISIBLE:
				ms_aVisibleEntityPtrs[ms_nNoOfVisibleEntities++] = e;
				break;
			case VIS_CULLED:
				break;
			case VIS_STREAMME:
				CStreaming::RequestModel(e->m_modelIndex, 0);
				break;
			}
		}
}

bool
CRenderer::SetupBigBuildingVisibility(CEntity *ent)
{
	CSimpleModelInfo *mi = (CSimpleModelInfo*)CModelInfo::GetModelInfo(ent->m_modelIndex);
	CTimeModelInfo *ti;

	// If there are two time objects, we want one of them visible at
	// all times. If there is only one, we have it easier.
	// So the following conditions hold:
	// If there's no related time object,
	//   this one is invisible if its time is out of range.
	// If there is a related time object,
	//   it must be loaded if this one is to be invisible.
	if(mi->m_type == TIMEMODELINFO){
 		ti = (CTimeModelInfo*)mi;
		if((ti->m_otherTimeModelID == -1 ||
		    ti->GetOtherModel()->GetRwObject()) &&
		   !CClock::GetIsTimeInRange(ti->m_timeOn, ti->m_timeOff))
			return false;
	}else if(mi->m_type == VEHICLEMODELINFO)
		return ent->IsVisible();

	float dist = (ms_vecCameraPosition-ent->GetPosition()).Magnitude();
	CSimpleModelInfo *nonLOD = mi->GetRelatedModel();

	// Find out whether to draw below near distance.
	// This is only the case if there is a non-LOD which is either not
	// loaded or not completely faded in yet.
	if(dist < mi->GetNearDistance() && dist < LOD_DISTANCE + STREAM_DISTANCE){
		// No non-LOD or non-LOD is completely visible.
		if(nonLOD == nil ||
		   nonLOD->GetRwObject() && nonLOD->m_alpha == 255)
			return false;

		// But if it is a time object, we'd rather draw the wrong
		// non-LOD than the right LOD.
		ti = (CTimeModelInfo*)nonLOD;
		if(nonLOD->m_type == TIMEMODELINFO &&
		   ti->m_otherTimeModelID != -1 &&
		   ti->GetOtherModel()->GetRwObject())
			return false;
	}

	rw::Atomic *a = mi->GetAtomicFromDistance(dist);
	if(a){
		if(ent->m_rwObject == nil)
			ent->CreateRwObject();
		assert(ent->m_rwObject);
		rw::Atomic *rwobj = (rw::Atomic*)ent->m_rwObject;

		// Make sure our atomic uses the right geometry and not
		// that of an atomic for another draw distance.
		if(a->geometry != rwobj->geometry)
			rwobj->setGeometry(a->geometry, 0);
		if(!ent->IsVisibleComplex())
			return false;
		if(mi->m_drawLast){
			CVisibilityPlugins::InsertEntityIntoSortedList(ent, dist);
			ent->bDistanceFade = 0;
			return false;
		}
		return true;
	}

	if(mi->m_noFade){
		ent->DeleteRwObject();
		return false;
	}


	// get faded atomic
	a = mi->GetAtomicFromDistance(dist - FADE_DISTANCE);
	if(a == nil){
		ent->DeleteRwObject();
		return false;
	}

	// Fade...
	if(ent->m_rwObject == nil)
		ent->CreateRwObject();
	assert(ent->m_rwObject);
	rw::Atomic *rwobj = (rw::Atomic*)ent->m_rwObject;
	if(a->geometry != rwobj->geometry)
		rwobj->setGeometry(a->geometry, 0);
	if(ent->IsVisibleComplex())
		CVisibilityPlugins::InsertEntityIntoSortedList(ent, dist);
	return false;
}

int
CRenderer::SetupEntityVisibility(CEntity *ent)
{
	CSimpleModelInfo *mi = (CSimpleModelInfo*)CModelInfo::GetModelInfo(ent->m_modelIndex);
	CTimeModelInfo *ti;

	bool request = true;
	if(mi->m_type == TIMEMODELINFO){
 		ti = (CTimeModelInfo*)mi;
		if(CClock::GetIsTimeInRange(ti->m_timeOn, ti->m_timeOff)){
			// don't fade in, or between time objects
			if(ti->m_otherTimeModelID == -1 ||
			   ti->GetOtherModel()->GetRwObject())
				ti->m_alpha = 255;
		}else{
			// If we're out of range, this object is invisible
			// if there is no other object or
			//    the other object can be drawn
			if(ti->m_otherTimeModelID == -1 ||
			   ti->GetOtherModel()->GetRwObject())
				return VIS_INVISIBLE;
			// ...so we'll try to draw this one, but don't request
			// it since what we really want is the other one.
			request = false;
		}
	}else if(mi->m_type != SIMPLEMODELINFO){
		// TODO: vehicles and peds and shit
		return VIS_INVISIBLE;
	}else if(ent->m_type == ENTITY_TYPE_OBJECT){
		// TODO
		return VIS_INVISIBLE;
	}

	float dist = (ent->GetPosition() - ms_vecCameraPosition).Magnitude();

	// This can only happen with multi-atomic models (e.g. railtracks)
	// but why do we bump up the distance? can only be fading...
	if(LOD_DISTANCE + STREAM_DISTANCE < dist && dist < mi->GetLargestLodDistance())
		dist = mi->GetLargestLodDistance();

	if(ent->m_type == ENTITY_TYPE_OBJECT && ent->bRenderDamaged)
		mi->m_isDamaged = 1;

	rw::Atomic *a = mi->GetAtomicFromDistance(dist);
	if(a){
		mi->m_isDamaged = 0;
		if(ent->m_rwObject == nil)
			ent->CreateRwObject();
		assert(ent->m_rwObject);
		rw::Atomic *rwobj = (rw::Atomic*)ent->m_rwObject;
		// Make sure our atomic uses the right geometry and not
		// that of an atomic for another draw distance.
		if(a->geometry != rwobj->geometry)
			rwobj->setGeometry(a->geometry, 0);
		mi->IncreaseAlpha();
		if(ent->m_rwObject == nil || !ent->bIsVisible)
			return VIS_INVISIBLE;

		if(!ent->GetIsOnScreen()){
			mi->m_alpha = 255;
			return VIS_CULLED;
		}

		if(mi->m_alpha != 255){
			CVisibilityPlugins::InsertEntityIntoSortedList(ent, dist);
			ent->bDistanceFade = 1;
			return VIS_INVISIBLE;
		}

		if(mi->m_drawLast || ent->bDrawLast){
			CVisibilityPlugins::InsertEntityIntoSortedList(ent, dist);
			ent->bDistanceFade = 0;
			return VIS_INVISIBLE;
		}
		return VIS_VISIBLE;
	}

	// Object is not loaded, figure out what to do

	if(mi->m_noFade){
		mi->m_isDamaged = 0;
		// request model
		if(dist - STREAM_DISTANCE < mi->GetLargestLodDistance() && request)
			return VIS_STREAMME;
		return VIS_INVISIBLE;
	}

	// We might be fading

	a = mi->GetAtomicFromDistance(dist - FADE_DISTANCE);
	mi->m_isDamaged = 0;
	if(a == nil){
		// request model
		if(dist - FADE_DISTANCE - STREAM_DISTANCE < mi->GetLargestLodDistance() && request)
			return VIS_STREAMME;
		return VIS_INVISIBLE;
	}

	if(ent->m_rwObject == nil)
		ent->CreateRwObject();
	assert(ent->m_rwObject);
	rw::Atomic *rwobj = (rw::Atomic*)ent->m_rwObject;
	if(a->geometry != rwobj->geometry)
		rwobj->setGeometry(a->geometry, 0);
	mi->IncreaseAlpha();
	if(ent->m_rwObject == nil || !ent->bIsVisible)
		return VIS_INVISIBLE;

	if(!ent->GetIsOnScreen())
		mi->m_alpha = 255;
	else{
		CVisibilityPlugins::InsertEntityIntoSortedList(ent, dist);
		ent->bDistanceFade = 1;
	}
	return VIS_CULLED;
}

void
CRenderer::RenderOneNonRoad(CEntity *ent)
{
	// TODO: vehicle and ped things
	ent->SetupLighting();
	ent->Render();
	ent->RemoveLighting();
}

void
CRenderer::RenderRoads(void)
{
	int i;
	CEntity *e;
	SetRenderState(rw::FOGENABLE, 1);
	for(i = 0; i < ms_nNoOfVisibleEntities; i++){
		e = ms_aVisibleEntityPtrs[i];
		if(e->m_type == ENTITY_TYPE_BUILDING &&
		   ((CBuilding*)e)->GetIsATreadable())
			e->Render();
	}
}

void
CRenderer::RenderEverythingBarRoads(void)
{
	// TODO: gSortedVehiclesAndPeds, transparency
	int i;
	CEntity *e;
	for(i = 0; i < ms_nNoOfVisibleEntities; i++){
		ms_aVisibleEntityPtrs[i]->Render();
		e = ms_aVisibleEntityPtrs[i];
		if(e->m_type == ENTITY_TYPE_BUILDING &&
		   ((CBuilding*)e)->GetIsATreadable())
			continue;
		CRenderer::RenderOneNonRoad(e);
	}
}

void
CRenderer::RenderEverything(void)
{
	int i;
	for(i = 0; i < ms_nNoOfVisibleEntities; i++)
		ms_aVisibleEntityPtrs[i]->Render();
}

void
CRenderer::RenderFadingInEntities(void)
{
	SetRenderState(rw::FOGENABLE, 1);
	DeActivateDirectional();
	SetAmbientColours();
	CVisibilityPlugins::RenderFadingEntities();
}

void
CRenderer::RenderCollisionLines(void)
{
	int i;
	CVector dist;
	CEntity *e;
	CLink<CVisibilityPlugins::AlphaObjectInfo> *node;

	for(i = 0; i < ms_nNoOfVisibleEntities; i++){
		e = ms_aVisibleEntityPtrs[i];
		dist = e->GetPosition() - ms_vecCameraPosition;
		if(dist.x > -100.0f && dist.x < 100.0f &&
		   dist.y > -100.0f && dist.y < 100.0f)
			CCollision::DrawColModel(e->GetMatrix(), *CModelInfo::GetModelInfo(e->m_modelIndex)->GetColModel());
	}

	for(node = CVisibilityPlugins::m_alphaEntityList.tail.prev;
	    node != &CVisibilityPlugins::m_alphaEntityList.head;
	    node = node->prev){
		CEntity *e = node->item.entity;
		dist = e->GetPosition() - ms_vecCameraPosition;
		if(dist.x > -100.0f && dist.x < 100.0f &&
		   dist.y > -100.0f && dist.y < 100.0f)
			CCollision::DrawColModel(e->GetMatrix(), *CModelInfo::GetModelInfo(e->m_modelIndex)->GetColModel());
	}
}
