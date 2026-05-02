#ifndef _POOLS_H_
#define _POOLS_H_

typedef CPool<CBuilding, CBuilding> CBuildingPool;
typedef CPool<CTreadable, CTreadable> CTreadablePool;
typedef CPool<CDummy, CDummyPed> CDummyPool;
typedef CPool<CPtrNode, CPtrNode> CPtrNodePool;
typedef CPool<CEntryInfoNode, CEntryInfoNode> CEntryInfoNodePool;

class CPools
{
	static CBuildingPool *ms_pBuildingPool;
	static CTreadablePool *ms_pTreadablePool;
	static CDummyPool *ms_pDummyPool;
	static CPtrNodePool *ms_pPtrNodePool;
	static CEntryInfoNodePool *ms_pEntryInfoNodePool;
public:
	// static CPool<cAudioScriptObject, cAudioScriptObject> *ms_pAudioScriptObjectPool;
	// static CPool<CObject, CCutsceneHead> *ms_pObjectPool;
	// static CPool<CVehicle, CVehicle> *ms_pVehiclePool;
	// static CPool<CPed, CPlayerPed> *ms_pPedPool;

	static void Initialise(void);
	static void Shutdown(void);
	// CheckPoolsEmpty(void);

	static CBuildingPool *GetBuildingPool(void) { return ms_pBuildingPool; }
	static CTreadablePool *GetTreadablePool(void) { return ms_pTreadablePool; }
	static CDummyPool *GetDummyPool(void) { return ms_pDummyPool; }
	static CPtrNodePool *GetPtrNodePool(void) { return ms_pPtrNodePool; }
	static CEntryInfoNodePool *GetEntryInfoNodePool(void) { return ms_pEntryInfoNodePool; }
};

#endif
