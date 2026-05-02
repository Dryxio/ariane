#include "III.h"

CLinkList<CVisibilityPlugins::AlphaObjectInfo> CVisibilityPlugins::m_alphaList;
CLinkList<CVisibilityPlugins::AlphaObjectInfo> CVisibilityPlugins::m_alphaEntityList;

int32 CVisibilityPlugins::ms_atomicPluginOffset = -1;
int32 CVisibilityPlugins::ms_framePluginOffset = -1;
int32 CVisibilityPlugins::ms_clumpPluginOffset = -1;

void
CVisibilityPlugins::Initialise(void)
{
	m_alphaList.Init(20);
	m_alphaList.head.item.sort = 0.0f;
	m_alphaList.tail.item.sort = 100000000.0f;
	m_alphaEntityList.Init(350);	// TODO: set back to 150 when things are fixed
	m_alphaEntityList.head.item.sort = 0.0f;
	m_alphaEntityList.tail.item.sort = 100000000.0f;
}

void
CVisibilityPlugins::InitAlphaEntityList(void)
{
	m_alphaEntityList.Clear();
}

bool
CVisibilityPlugins::InsertEntityIntoSortedList(CEntity *e, float dist)
{
	AlphaObjectInfo item;
	item.entity = e;
	item.sort = dist;
	bool ret = !!m_alphaEntityList.InsertSorted(item);
	if(!ret)
		printf("list full %d\n", m_alphaEntityList.Count());
	return ret;
}

void
CVisibilityPlugins::InitAlphaAtomicList(void)
{
	m_alphaList.Clear();
}

void
CVisibilityPlugins::RenderFadingAtomic(rw::Atomic *atm, float camdist)
{
	rw::Atomic *distatm;
	float fadefactor;
	uchar alpha;
	CSimpleModelInfo *mi = GetAtomicModelInfo(atm);
	distatm = mi->GetAtomicFromDistance(camdist - FADE_DISTANCE);
	if(mi->m_additive){
		SetRenderState(rw::DESTBLEND, rw::BLENDONE);
		rw::Atomic::defaultRenderCB(atm);
		SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
	}else{
		fadefactor = (mi->GetLargestLodDistance() - (camdist - FADE_DISTANCE))/FADE_DISTANCE;
		if(fadefactor > 1.0f)
			fadefactor = 1.0f;
		alpha = mi->m_alpha * fadefactor;
		if(alpha == 255)
			rw::Atomic::defaultRenderCB(atm);
		else{
			rw::Geometry *g = distatm->geometry;
			uint32 oldflags = g->flags;
			g->flags |= rw::Geometry::MODULATE;
			for(int32 i = 0; i < g->matList.numMaterials; i++)
				g->matList.materials[i]->color.alpha = alpha;
			if(g != atm->geometry)
				atm->setGeometry(g, 0);
			rw::Atomic::defaultRenderCB(atm);
			for(int32 i = 0; i < g->matList.numMaterials; i++)
				g->matList.materials[i]->color.alpha = 255;
			g->flags = oldflags;
		}
	}
}

void
CVisibilityPlugins::RenderFadingEntities(void)
{
	CLink<AlphaObjectInfo> *node;
	CSimpleModelInfo *mi;
	for(node = m_alphaEntityList.tail.prev;
	    node != &m_alphaEntityList.head;
	    node = node->prev){
		CEntity *e = node->item.entity;
		if(e->m_rwObject == nil)
			continue;
		mi = (CSimpleModelInfo*)CModelInfo::GetModelInfo(e->m_modelIndex);
		if(mi->m_noZwrite){
			SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAALWAYS);
			SetRenderState(rw::ZWRITEENABLE, 0);
		}

		if(e->bDistanceFade){
			DeActivateDirectional();
			SetAmbientColours();
			e->bImBeingRendered = 1;
			RenderFadingAtomic((rw::Atomic*)e->m_rwObject, node->item.sort);
			e->bImBeingRendered = 0;
		}else
			CRenderer::RenderOneNonRoad(e);

		if(mi->m_noZwrite){
			SetRenderState(rw::ZWRITEENABLE, 1);
			SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAGREATEREQUAL);
		}
	}
}

// TODO

void
CVisibilityPlugins::RenderVehicleHiDetailCB(rw::Atomic *atomic)
{
	rw::Atomic::defaultRenderCB(atomic);
}

void
CVisibilityPlugins::RenderVehicleHiDetailAlphaCB(rw::Atomic *atomic)
{
	rw::Atomic::defaultRenderCB(atomic);
}

void
CVisibilityPlugins::RenderVehicleHiDetailCB_BigVehicle(rw::Atomic *atomic)
{
	rw::Atomic::defaultRenderCB(atomic);
}

void
CVisibilityPlugins::RenderVehicleHiDetailAlphaCB_BigVehicle(rw::Atomic *atomic)
{
	rw::Atomic::defaultRenderCB(atomic);
}

void
CVisibilityPlugins::RenderVehicleHiDetailCB_Boat(rw::Atomic *atomic)
{
	rw::Atomic::defaultRenderCB(atomic);
}

void
CVisibilityPlugins::RenderVehicleLowDetailCB_BigVehicle(rw::Atomic *atomic)
{
//	rw::Atomic::defaultRenderCB(atomic);
}

void
CVisibilityPlugins::RenderVehicleLowDetailAlphaCB_BigVehicle(rw::Atomic *atomic)
{
//	rw::Atomic::defaultRenderCB(atomic);
}

void
CVisibilityPlugins::RenderVehicleReallyLowDetailCB(rw::Atomic *atomic)
{
//	rw::Atomic::defaultRenderCB(atomic);
}

void
CVisibilityPlugins::RenderVehicleReallyLowDetailCB_BigVehicle(rw::Atomic *atomic)
{
//	rw::Atomic::defaultRenderCB(atomic);
}

void
CVisibilityPlugins::RenderTrainHiDetailCB(rw::Atomic *atomic)
{
	rw::Atomic::defaultRenderCB(atomic);
}

void
CVisibilityPlugins::RenderTrainHiDetailAlphaCB(rw::Atomic *atomic)
{
	rw::Atomic::defaultRenderCB(atomic);
}


//
// RW Plugins
//

enum
{
	ID_VISIBILITYATOMIC = MAKEPLUGINID(VEND_ROCKSTAR, 0x00),
	ID_VISIBILITYCLUMP  = MAKEPLUGINID(VEND_ROCKSTAR, 0x01),
	ID_VISIBILITYFRAME  = MAKEPLUGINID(VEND_ROCKSTAR, 0x02),
};

bool
CVisibilityPlugins::PluginAttach(void)
{
	using namespace rw;

	ms_atomicPluginOffset = Atomic::registerPlugin(sizeof(AtomicExt),
		ID_VISIBILITYATOMIC,
		AtomicConstructor, AtomicDestructor, AtomicCopyConstructor);

	ms_framePluginOffset = Frame::registerPlugin(sizeof(FrameExt),
		ID_VISIBILITYFRAME,
		FrameConstructor, FrameDestructor, FrameCopyConstructor);

	ms_clumpPluginOffset = Clump::registerPlugin(sizeof(ClumpExt),
		ID_VISIBILITYCLUMP,
		ClumpConstructor, ClumpDestructor, ClumpCopyConstructor);
	return true;
}

#define ATOMICEXT(o) ((AtomicExt*)PLUGINOFFSET(AtomicExt, o, ms_atomicPluginOffset))
#define FRAMEEXT(o) ((FrameExt*)PLUGINOFFSET(FrameExt, o, ms_framePluginOffset))
#define CLUMPEXT(o) ((ClumpExt*)PLUGINOFFSET(ClumpExt, o, ms_clumpPluginOffset))

//
// Atomic
//

void*
CVisibilityPlugins::AtomicConstructor(void *object, int32, int32)
{
	ATOMICEXT(object)->modelInfo = nil;
	return object;
}

void*
CVisibilityPlugins::AtomicDestructor(void *object, int32, int32)
{
	return object;
}

void*
CVisibilityPlugins::AtomicCopyConstructor(void *dst, void *src, int32, int32)
{
	*ATOMICEXT(dst) = *ATOMICEXT(src);
	return dst;
}

void
CVisibilityPlugins::SetAtomicModelInfo(rw::Atomic *atomic,
                                       CSimpleModelInfo *modelInfo)
{
	AtomicExt *ext = ATOMICEXT(atomic);
	ext->modelInfo = modelInfo;
	// TODO: set renderCB from modelInfo->type
}

CSimpleModelInfo*
CVisibilityPlugins::GetAtomicModelInfo(rw::Atomic *atomic)
{
	return ATOMICEXT(atomic)->modelInfo;
}

void
CVisibilityPlugins::SetAtomicFlag(rw::Atomic *atomic, int f)
{
	ATOMICEXT(atomic)->flags |= f;
}

void
CVisibilityPlugins::ClearAtomicFlag(rw::Atomic *atomic, int f)
{
	ATOMICEXT(atomic)->flags &= ~f;
}

int
CVisibilityPlugins::GetAtomicId(rw::Atomic *atomic)
{
	return ATOMICEXT(atomic)->flags;
}

// This is rather useless, but whatever
void
CVisibilityPlugins::SetAtomicRenderCallback(rw::Atomic *atomic, rw::Atomic::RenderCB cb)
{
	if(cb == nil)
		cb = rw::Atomic::defaultRenderCB;
	atomic->setRenderCB(cb);
}

//
// Frame
//

void*
CVisibilityPlugins::FrameConstructor(void *object, int32, int32)
{
	FRAMEEXT(object)->frame = nil;
	return object;
}

void*
CVisibilityPlugins::FrameDestructor(void *object, int32, int32)
{
	return object;
}

void*
CVisibilityPlugins::FrameCopyConstructor(void *dst, void *src, int32, int32)
{
	*FRAMEEXT(dst) = *FRAMEEXT(src);
	return dst;
}

//
// Clump
//

void*
CVisibilityPlugins::ClumpConstructor(void *object, int32, int32)
{
	ClumpExt *ext = CLUMPEXT(object);
	ext->modelInfo = nil;
	ext->alpha = 0xFF;
	return object;
}

void*
CVisibilityPlugins::ClumpDestructor(void *object, int32, int32)
{
	return object;
}

void*
CVisibilityPlugins::ClumpCopyConstructor(void *dst, void *src, int32, int32)
{
	CLUMPEXT(dst)->modelInfo = CLUMPEXT(src)->modelInfo;
	return dst;
}

