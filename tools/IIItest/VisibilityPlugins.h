#ifndef _VISIBILITYPLUGINS_H_
#define _VISIBILITYPLUGINS_H_

class CVisibilityPlugins
{
public:
	struct AlphaObjectInfo
	{
		union {
			CEntity *entity;
			rw::Atomic *atomic;
		};
		float sort;
	};

	static CLinkList<AlphaObjectInfo> m_alphaList;
	static CLinkList<AlphaObjectInfo> m_alphaEntityList;

	static void Initialise(void);
	static void InitAlphaEntityList(void);
	static bool InsertEntityIntoSortedList(CEntity *e, float dist);
	static void InitAlphaAtomicList(void);

	static void RenderFadingAtomic(rw::Atomic *atm, float dist);
	static void RenderFadingEntities(void);

	static void RenderVehicleHiDetailCB(rw::Atomic *atomic);
	static void RenderVehicleHiDetailAlphaCB(rw::Atomic *atomic);
	static void RenderVehicleHiDetailCB_BigVehicle(rw::Atomic *atomic);
	static void RenderVehicleHiDetailAlphaCB_BigVehicle(rw::Atomic *atomic);
	static void RenderVehicleHiDetailCB_Boat(rw::Atomic *atomic);
	static void RenderVehicleLowDetailCB_BigVehicle(rw::Atomic *atomic);
	static void RenderVehicleLowDetailAlphaCB_BigVehicle(rw::Atomic *atomic);
	static void RenderVehicleReallyLowDetailCB(rw::Atomic *atomic);
	static void RenderVehicleReallyLowDetailCB_BigVehicle(rw::Atomic *atomic);
	static void RenderTrainHiDetailCB(rw::Atomic *atomic);
	static void RenderTrainHiDetailAlphaCB(rw::Atomic *atomic);

	//
	// RW Plugins
	//

	union AtomicExt
	{
		CSimpleModelInfo *modelInfo;	// used by SimpleModelInfo
		int flags;			// used by ClumpModelInfo
	};
	static void SetAtomicModelInfo(rw::Atomic*, CSimpleModelInfo*);
	static CSimpleModelInfo *GetAtomicModelInfo(rw::Atomic *atomic);
	static void SetAtomicFlag(rw::Atomic*, int);
	static void ClearAtomicFlag(rw::Atomic*, int);
	static int GetAtomicId(rw::Atomic *atomic);
//	this is a useless wrapper
	static void SetAtomicRenderCallback(rw::Atomic*, rw::Atomic::RenderCB);

	static void *AtomicConstructor(void *object, int32 offset, int32 len);
	static void *AtomicDestructor(void *object, int32 offset, int32 len);
	static void *AtomicCopyConstructor(void *dst, void *src,
	                                   int32 offset, int32 len);
	static int32 ms_atomicPluginOffset;

	struct FrameExt
	{
		rw::Frame *frame;
	};
	static void *FrameConstructor(void *object, int32 offset, int32 len);
	static void *FrameDestructor(void *object, int32 offset, int32 len);
	static void *FrameCopyConstructor(void *dst, void *src,
	                                  int32 offset, int32 len);
	static int32 ms_framePluginOffset;

	struct ClumpExt
	{
		CClumpModelInfo *modelInfo;
		int alpha;
	};
	static void *ClumpConstructor(void *object, int32 offset, int32 len);
	static void *ClumpDestructor(void *object, int32 offset, int32 len);
	static void *ClumpCopyConstructor(void *dst, void *src,
	                                  int32 offset, int32 len);
	static int32 ms_clumpPluginOffset;

	static bool PluginAttach(void);
};

#endif
