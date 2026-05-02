#ifndef _ENTITY_H_
#define _ENTITY_H_

enum eEntityType
{
	ENTITY_TYPE_NOTHING = 0,
	ENTITY_TYPE_BUILDING,
	ENTITY_TYPE_VEHICLE,
	ENTITY_TYPE_PED,
	ENTITY_TYPE_OBJECT,
	ENTITY_TYPE_DUMMY,
	ENTITY_TYPE_6,
	ENTITY_TYPE_7,
};

enum eEntityStatus
{
	// from SA MTA! let's hope they didn't change from III
	STATUS_PLAYER = 0,
	STATUS_PLAYER_PLAYBACKFROMBUFFER,
	STATUS_SIMPLE,
	STATUS_PHYSICS,
	STATUS_ABANDONED,
	STATUS_WRECKED,
	STATUS_TRAIN_MOVING,
	STATUS_TRAIN_NOT_MOVING,
	STATUS_HELI,
	STATUS_PLANE,
	STATUS_PLAYER_REMOTE,
	STATUS_PLAYER_DISABLED,
	//STATUS_TRAILER,
	//STATUS_SIMPLE_TRAILER
};

class CPed;
class CVehicle;
struct CReference;

// NB: does not match actual game structure exactly
class CEntity : public CPlaceable
{
	// disable allocation
	static void *operator new(size_t) { assert(0); return nil; }
	static void operator delete(void*, size_t) { assert(0); }

public:
	rw::Object *m_rwObject;
	uint32 m_type : 3;
	uint32 m_status : 5;

	// flagsA
	uint32 bUsesCollision : 1;
	uint32 bCollisionProcessed : 1;
	uint32 bIsStatic : 1;
	uint32 bHasContacted : 1;
	uint32 bPedPhysics : 1;
	uint32 bIsStuck : 1;
	uint32 bIsInSafePosition : 1;
	uint32 bUseCollisionRecords : 1;

	// flagsB
	uint32 bWasPostponed : 1;
	uint32 m_flagB2 : 1;	// explosion proof?
	uint32 bIsVisible : 1;
	uint32 bHasCollided : 1;	//
	uint32 bRenderScorched : 1;
	uint32 m_flagB20 : 1;	// bFlashing?
	uint32 bIsBIGBuilding : 1;
	// VC inserts one more flag here: if drawdist <= 2000
	uint32 bRenderDamaged : 1;

	// flagsC
	uint32 m_flagC1 : 1;	// bullet proof?
	uint32 m_flagC2 : 1;	// fire proof?
	uint32 m_flagC4 : 1;	// collision proof?
	uint32 m_flagC8 : 1;	// melee proof?
	uint32 m_flagC10 : 1;	// bOnlyDamagedByPlayer?
	uint32 m_flagC20 : 1;
	uint32 m_flagC40 : 1;
	uint32 m_flagC80 : 1;

	// flagsD
	uint32 bRemoveFromWorld : 1;
	uint32 bHasHitWall : 1;
	uint32 bImBeingRendered : 1;
	uint32 m_flagD8 : 1;
	uint32 m_flagD10 : 1;
	uint32 bDrawLast : 1;
	uint32 m_flagD40 : 1;
	uint32 m_flagD80 : 1;

	// flagsE
	uint32 bDistanceFade : 1;
	uint32 m_flagE2 : 1;

	uint16 m_scanCode;
	int16 m_modelIndex;
	eLevelName m_level;	// int16
	CReference *m_pFirstReference;

	CEntity(void);
	void SetupBigBuilding(void);
	bool IsVisible(void);
	bool IsVisibleComplex(void);
	bool GetIsOnScreen(void);
	bool GetIsOnScreenComplex(void);
	bool GetIsTouching(const CVector center, float radius);
	void GetBoundCenter(CVector &out);
	float GetBoundRadius(void) { return CModelInfo::GetModelInfo(m_modelIndex)->GetColModel()->boundingSphere.radius; }
	int GetModelIndex(void) { return m_modelIndex; }
	void UpdateRwFrame(void);
	void RegisterReference(CEntity **pent);
	void ResolveReferences(void);
	void PruneReferences(void);

	bool IsBuilding(void) { return m_type == ENTITY_TYPE_BUILDING; }
	bool IsVehicle(void) { return m_type == ENTITY_TYPE_VEHICLE; }
	bool IsPed(void) { return m_type == ENTITY_TYPE_PED; }
	bool IsObject(void) { return m_type == ENTITY_TYPE_OBJECT; }
	bool IsDummy(void) { return m_type == ENTITY_TYPE_DUMMY; }
	CPed *Ped(void) { return (CPed*)this; }
	CVehicle *Vehicle(void) { return (CVehicle*)this; }

	// from CPlaceable
	~CEntity(void);

	virtual void Add(void);
	virtual void Remove(void);
	virtual void SetModelIndexNoCreate(uint i) { m_modelIndex = i; }
	virtual void SetModelIndex(uint i) { m_modelIndex = i; CreateRwObject(); }
	virtual void CreateRwObject(void);
	virtual void DeleteRwObject(void);
	virtual CRect GetBoundRect(void);
	virtual void ProcessControl(void) {}
	virtual void ProcessCollision(void) {}
	virtual void ProcessShift(void) {}
	virtual void Teleport(CVector v) {}
	// PreRender
	virtual void Render(void);
	virtual void SetupLighting(void);
	virtual void RemoveLighting(void) {}
	virtual void FlagToDestroyWhenNextProcessed(void) {}
};

#endif
