enum ModelInfoType
{
	MODELINFO_SIMPLE       = 1,
	MODELINFO_MLO          = 2,	// unused
	MODELINFO_TIME         = 3,
	MODELINFO_WEAPON       = 4,
	MODELINFO_ELEMENTGROUP = 5,
	MODELINFO_VEHICLE      = 6,
	MODELINFO_PED          = 7,
	MODELINFO_XTRACOMP     = 8,	// unused
};

enum VehicleType
{
	VEHICLETYPE_CAR,
	VEHICLETYPE_BOAT,
#ifdef VCS
	VEHICLETYPE_JETSKI,
#endif
	VEHICLETYPE_TRAIN,
	VEHICLETYPE_HELI,
	VEHICLETYPE_PLANE,
	VEHICLETYPE_BIKE,
	VEHICLETYPE_FERRY,
};

struct CVector
{
	float x, y, z;
};

struct CRect
{
	float left;
	float top;
	float right;
	float bottom;
};

struct CVuVector
{
	float x, y, z, w;
};

struct CompressedVector
{
	int16 x, y, z;
	CVector Uncompress(void){
		CVector v = { x/128.0f, y/128.0f, z/128.0f };
		return v;
	}
};
#ifndef RW_PS2
static_assert(sizeof(CompressedVector) == 6, "CompressedVector: error");
#endif

struct CSphere
{
	CVector center;
	float radius;
};

struct CBox
{
	CVuVector min;
	CVuVector max;

	void FindMinMax(void){
		float tmp;
		if(max.x < min.x){
			tmp = min.x; min.x = max.x; max.x = tmp;
		}
		if(max.y < min.y){
			tmp = min.y; min.y = max.y; max.y = tmp;
		}
		if(max.z < min.z){
			tmp = min.z; min.z = max.z; max.z = tmp;
		}
	}
};

struct CColSphere
{
	CSphere sph;
	uint8 surface;
	uint8 piece;
	int32 pad[3];
};

struct CColBox
{
	CBox box;
	uint8 surface;
	uint8 piece;
	int32 pad[3];
};

struct CColTriangle
{
	int16 a, b, c;
	uint8 surface;
};
#ifndef RW_PS2
static_assert(sizeof(CColTriangle) == 8, "CColTriangle: error");
#endif

struct ColSection
{
	CBox box;
	int16 first;
	int16 last;
	int32 pad[3];
};

struct CColModel
{
	CSphere boundingSphere;
	CBox boundingBox;
#ifdef LCS
	int16 numSpheres;
	int16 numBoxes;
	int16 numTriangles;
	uint8 numLines;
	uint8 numTriSections;
	uint8 colStoreId;
	uint8 field_39;
	uint8 field_3A;
	uint8 field_3B;
#else
	int16 numBoxes;
	int16 numTriangles;
	int16 numSpheres;
	uint8 numTriSections;
	uint8 colStoreId;
#endif
	CColSphere *spheres;
	void *lines;
	CColBox *boxes;

	ColSection *triSections;
	CompressedVector *vertices;
	CColTriangle *triangles;
	int32 unk0;
	int32 pad0[2];
#ifdef VCS
	int32 pad1;
#endif
};
//static_assert(sizeof(CColModel) == 0x60, "CColModel: error");


struct TexListDef
{
	RslTexList *texlist;
	int32 refCount;
	char name[20];
};

struct PedStats
{
#ifdef LCS
	int32 index;
	char name[24];
	float fleeDistance;
	float headingChangeRate;
	uint8 fear;
	uint8 temper;
	uint8 lawfulness;
	uint8 sexiness;
	float attackStrength;
	float defendWeakness;
	uint16 flags;
#else
	int32 index;
	float fleeDistance;
	float headingChangeRate;
	float attackStrength;
	float defendWeakness;
	uint16 flags;
	uint8 fear;
	uint8 temper;
	uint8 lawfulness;
	uint8 sexiness;
	char name[24];
#endif
};

struct CBaseModelInfo
{
	int32	field0;
	const char *name;	// we'll use field4 for the name for now
//	int32	field4;
	uint32	hashKey;
	void	*mdlFile;	// points to chunk header of mdl file at runtime
	uint8	type;
	int8	num2dFx;
	bool	ownsColModel;
	int8	field15;	// alpha?
	CColModel	*colModel;
	int16	_2dfxIndex;
	int16	objectIndex;
	int16	refCount;
	int16	txdSlot;
#ifdef VCS
	int16	unknownIndex;
#endif
	void	*vtable;
};
//#ifdef LCS
//static_assert(sizeof(CBaseModelInfo) == 0x24, "CBaseModelInfo: error");
//#else
//static_assert(sizeof(CBaseModelInfo) == 0x28, "CBaseModelInfo: error");
//#endif


struct CSimpleModelInfo : public CBaseModelInfo
{
	RslObject **objects;
	float	drawDistances[3];
	uint8	numObjects;
	uint16	flags;
	CSimpleModelInfo *relatedObject;
};

struct CTimeModelInfo : public CSimpleModelInfo
{
	int32	timeOn;
	int32	timeOff;
	int16	otherModel;
	// int16 pad
};

struct CWeaponModelInfo : public CSimpleModelInfo
{
	union {
		char *animFile;
		int32 animFileIndex;
	};

	int32 GetWeaponType(void) { return *(int32*)&this->relatedObject; }
	void SetWeaponType(int32 type) { *(int32*)&this->relatedObject = type; }
};

struct CElementGroupModelInfo : public CBaseModelInfo
{
	RslElementGroup *elementGroup;
	union {
		char *animFile;
		int32 animFileIndex;
	};
};

// hash: c8807962
struct CPedModelInfo : CElementGroupModelInfo
{
	int32 animGroup;
	int32 pedType;
	int32 pedStatType;
	uint32 carsDriveMask;
	void *hitColModel;
	int8 radio1, radio2;
#ifdef VCS
	// colour indices? maybe bytes?
	short unknown0[55];
	// editable materials it would seem, but how do they work?
	struct {
		RslMaterial *mat;	// filled at runtime
		int32 numX;
	} materialthing[6];
	char someName[16];	// 16 is just a wild guess, possibly wrong
	uint32 unknown1;	// always 0x7ffe9f0, even at runtime
	uint32 unknown2;	// awlays 0
#endif
};
//#ifdef LCS
//static_assert(sizeof(CPedModelInfo) == 0x44, "ResourceImage: error");
//#else
//static_assert(sizeof(CPedModelInfo) == 0xFC, "ResourceImage: error");
//#endif

struct CVehicleModelInfo__inst
{
	void *unused;	// probably ms_pEnvironmentMaps
	rw::RGBA m_vehicleColourTable[256];
	char m_compsUsed[2];
	char ms_compsToUse[2];
};

#ifdef LCS
#define NUMPRIM 25
#define NUMSEC 25
#else
#define NUMPRIM 30
#define NUMSEC 25
#endif


// hash: 51964ff
#ifdef LCS
#define NUM_VEHICLE_DUMMIES 5
#else
#define NUM_VEHICLE_DUMMIES 15
#endif
struct CVehicleModelInfo : CElementGroupModelInfo
{
#ifdef LCS
	uint8 m_lastColours[2];
	char m_gameName[10];
	uint32 m_vehicleType;
	float m_wheelScale;
	int16 m_wheelId;
	int16 m_handlingId;
	uint8 m_numDoors;
	int8 m_vehicleClass;
	uint8 m_level;
	uint8 m_numExtras;
	uint16 m_frequency;
	int32 unk0;	// probably pad for CVector4d
	CVuVector m_dummyPos[NUM_VEHICLE_DUMMIES];
	uint32 m_compRules;
	float m_bikeSteerAngle;
	RslMaterial *m_materialsPrimary[NUMPRIM];
	RslMaterial *m_materialsSecondary[NUMSEC];
	uint8 m_colours[2][8];	// from carcols.dat
	uint8 m_numColours;
	uint8 m_lastColour;
	int8 m_currentColours[2];
	float m_normalSplay;	// "amount the normals are splayed by to improve the environment mapping on PSP"
	RslElement **m_extras;
	int32 m_animFileIndex;
#else
/*
	still looking for:
	uint8 m_numExtras;
*/
	int unk0[2];
	void *m_handling;
	int unk1[5];
	float m_normalSplay;	// "amount the normals are splayed by to improve the environment mapping on PSP"
	uint32 m_vehicleType;
	float m_wheelScale;
	float m_wheelScaleRear;
	CVuVector m_dummyPos[NUM_VEHICLE_DUMMIES];
	uint32 m_compRules;
	float m_bikeSteerAngle;
	char m_gameName[8];

	uint8 unk2;	// m_lastColour ?
	uint8 m_numColours;
	uint8 unk3;	// 0/1?
	uint8 m_colours[8][2];	// from carcols.dat
	char unk4[25];
	RslMaterial *m_materialsPrimary[NUMPRIM];
	RslMaterial *m_materialsSecondary[NUMSEC];

	RslElement **m_extras;
	int32 m_animFileIndex;
	int16 m_wheelId;
	uint16 m_frequency;
	uint8 m_numDoors;
	int8 m_vehicleClass;
	uint8 m_level;
	char unk5[0x29];
#endif
};
//#ifdef LCS
//static_assert(sizeof(CVehicleModelInfo) == 0x190, "CVehicleModelInfo: error");
//#else
//static_assert(offsetof(CVehicleModelInfo, m_dummyPos)-sizeof(CElementGroupModelInfo) == 0x30, "CVehicleModelInfo: error");
//static_assert(offsetof(CVehicleModelInfo, m_gameName)-sizeof(CElementGroupModelInfo) == 0x128, "CVehicleModelInfo: error");
//static_assert(sizeof(CVehicleModelInfo) == 0x2a0, "CVehicleModelInfo: error");
//#endif

// This is all very, very strange

struct CMatrix
{
	RslMatrix matrix;
	RslMatrix *pMatrix;
#ifdef VCS
	int32 firstRef;
#endif
	// this is weird....
	uint8 flagsA;
	uint8 flagsB;
	uint8 flagsC;
	uint8 flagsD;
	uint8 flagsE;
	uint8 flagsF;
	uint8 flagsG;
	uint8 flagsH;
};

struct CPlaceable
{
	CMatrix matrix;
};

struct CEntity
{
	CPlaceable placeable;
#ifdef LCS
	int32 firstRef; // pad in CMatrix?
	int32 rslObject;
	int16 scanCode;
	int16 random;
	int16 modelIndex;
#else
	// quite possibly completely wrong for VCS
	int32 rslObject;
	int16 scanCode;
	int16 modelIndex;
	int16 modelIndex2;	// seems to be the same
#endif
	uint8 level;
	uint8 area;	// seems uninitialized in VCS o_O
	void *vtable;
};
//static_assert(sizeof(CEntity) == 0x60, "CEntity: error");

struct CBuilding : public CEntity
{
};

struct CTreadable : public CBuilding
{
};

struct CDummy : public CEntity
{
};

// might be nicer to have this as proper templates
struct CPool_generic
{
	void *items;
	uint8 *flags;
	int32  size;
	int32  allocPtr;
	char name[16];
};

struct CPool_entity
{
	CEntity *items;
	uint8 *flags;
	int32  size;
	int32  allocPtr;
	char name[16];
};

struct CPool_txd
{
	TexListDef *items;
	uint8 *flags;
	int32  size;
	int32  allocPtr;
	char name[16];
};

#include "animation.h"

#define NUMWEATHERS

struct CTimeCycle
{
	CVuVector m_VectorToSun[16];
	float m_fShadowFrontX[16];
	float m_fShadowFrontY[16];
	float m_fShadowSideX[16];
	float m_fShadowSideY[16];
	float m_fShadowDisplacementX[16];
	float m_fShadowDisplacementY[16];

	uint8 m_nAmbientRed[24][8];
	uint8 m_nAmbientGreen[24][8];
	uint8 m_nAmbientBlue[24][8];
	uint8 m_nAmbientRed_Obj[24][8];
	uint8 m_nAmbientGreen_Obj[24][8];
	uint8 m_nAmbientBlue_Obj[24][8];
	uint8 m_nAmbientRed_Bl[24][8];
	uint8 m_nAmbientGreen_Bl[24][8];
	uint8 m_nAmbientBlue_Bl[24][8];
	uint8 m_nAmbientRed_Obj_Bl[24][8];
	uint8 m_nAmbientGreen_Obj_Bl[24][8];
	uint8 m_nAmbientBlue_Obj_Bl[24][8];
	uint8 m_nDirectionalRed[24][8];
	uint8 m_nDirectionalGreen[24][8];
	uint8 m_nDirectionalBlue[24][8];
	uint8 m_nSkyTopRed[24][8];
	uint8 m_nSkyTopGreen[24][8];
	uint8 m_nSkyTopBlue[24][8];
	uint8 m_nSkyBottomRed[24][8];
	uint8 m_nSkyBottomGreen[24][8];
	uint8 m_nSkyBottomBlue[24][8];
	uint8 m_nSunCoreRed[24][8];
	uint8 m_nSunCoreGreen[24][8];
	uint8 m_nSunCoreBlue[24][8];
	uint8 m_nSunCoronaRed[24][8];
	uint8 m_nSunCoronaGreen[24][8];
	uint8 m_nSunCoronaBlue[24][8];
	uint8 m_fSunSize[24][8];
	uint8 m_fSpriteSize[24][8];
	uint8 m_fSpriteBrightness[24][8];
	uint8 m_nShadowStrength[24][8];
	uint8 m_nLightShadowStrength[24][8];
	uint8 m_nPoleShadowStrength[24][8];
	int16 m_fFarClip[24][8];
	int16 m_fFogStart[24][8];
#ifdef VCS
	uint8 m_nRadiosityIntensity[24][8];
	uint8 m_nRadiosityLimit[24][8];
#endif
	uint8 m_fLightsOnGroundBrightness[24][8];
	uint8 m_nLowCloudsRed[24][8];
	uint8 m_nLowCloudsGreen[24][8];
	uint8 m_nLowCloudsBlue[24][8];
	uint8 m_nFluffyCloudsTopRed[24][8];
	uint8 m_nFluffyCloudsTopGreen[24][8];
	uint8 m_nFluffyCloudsTopBlue[24][8];
	uint8 m_nFluffyCloudsBottomRed[24][8];
	uint8 m_nFluffyCloudsBottomGreen[24][8];
	uint8 m_nFluffyCloudsBottomBlue[24][8];
	uint8 m_fBlurRed[24][8];
	uint8 m_fBlurGreen[24][8];
	uint8 m_fBlurBlue[24][8];
	uint8 m_fWaterRed[24][8];
	uint8 m_fWaterGreen[24][8];
	uint8 m_fWaterBlue[24][8];
	uint8 m_fWaterAlpha[24][8];
#ifdef VCS
	float m_fBlurAlpha[24][8];
	float m_fBlurOffset[24][8];
#endif

	int32 m_fCurrentAmbientRed;
	int32 m_fCurrentAmbientGreen;
	int32 m_fCurrentAmbientBlue;
	int32 m_fCurrentAmbientRed_Obj;
	int32 m_fCurrentAmbientGreen_Obj;
	int32 m_fCurrentAmbientBlue_Obj;
	int32 m_fCurrentAmbientRed_Bl;
	int32 m_fCurrentAmbientGreen_Bl;
	int32 m_fCurrentAmbientBlue_Bl;
	int32 m_fCurrentAmbientRed_Obj_Bl;
	int32 m_fCurrentAmbientGreen_Obj_Bl;
	int32 m_fCurrentAmbientBlue_Obj_Bl;
	int32 m_fCurrentDirectionalRed;
	int32 m_fCurrentDirectionalGreen;
	int32 m_fCurrentDirectionalBlue;
	int32 m_nCurrentSkyTopRed;
	int32 m_nCurrentSkyTopGreen;
	int32 m_nCurrentSkyTopBlue;
	int32 m_nCurrentSkyBottomRed;
	int32 m_nCurrentSkyBottomGreen;
	int32 m_nCurrentSkyBottomBlue;
	int32 m_nCurrentFogColourRed;
	int32 m_nCurrentFogColourGreen;
	int32 m_nCurrentFogColourBlue;
	int32 m_nCurrentSunCoreRed;
	int32 m_nCurrentSunCoreGreen;
	int32 m_nCurrentSunCoreBlue;
	int32 m_nCurrentSunCoronaRed;
	int32 m_nCurrentSunCoronaGreen;
	int32 m_nCurrentSunCoronaBlue;
	int32 m_fCurrentSunSize;
	int32 m_fCurrentSpriteSize;
	int32 m_fCurrentSpriteBrightness;
	int16 m_nCurrentShadowStrength;
	int16 m_nCurrentLightShadowStrength;
	int16 m_nCurrentPoleShadowStrength;
	int32 m_fCurrentFarClip;
	int32 m_fCurrentFogStart;
#ifdef VCS
	int32 m_nCurrentRadiosityIntensity;
	int32 m_nCurrentRadiosityLimit;
#endif
	int32 m_fCurrentLightsOnGroundBrightness;
	int32 m_nCurrentLowCloudsRed;
	int32 m_nCurrentLowCloudsGreen;
	int32 m_nCurrentLowCloudsBlue;
	int32 m_nCurrentFluffyCloudsTopRed;
	int32 m_nCurrentFluffyCloudsTopGreen;
	int32 m_nCurrentFluffyCloudsTopBlue;
	int32 m_nCurrentFluffyCloudsBottomRed;
	int32 m_nCurrentFluffyCloudsBottomGreen;
	int32 m_nCurrentFluffyCloudsBottomBlue;
	int32 m_fCurrentBlurRed;
	int32 m_fCurrentBlurGreen;
	int32 m_fCurrentBlurBlue;
	int32 m_fCurrentWaterRed;
	int32 m_fCurrentWaterGreen;
	int32 m_fCurrentWaterBlue;
	int32 m_fCurrentWaterAlpha;
#ifdef VCS
	int32 m_fCurrentBlurAlpha;
	int32 m_fCurrentBlurOffset;
#endif
	int32 m_CurrentStoredValue;
	int32 m_FogReduction;
	int32 m_ExtraColour;
	int32 m_bExtraColourOn;
	int32 m_ExtraColourInter;
	int32 field_337C;	// just alignment?
};

struct CWaterLevel
{
	int ms_nNoOfWaterLevels;
	float *ms_aWaterZs;
	CRect *ms_aWaterRects;
	uint8 aWaterBlockList[64][64];
	uint8 aWaterFineBlockList[128][128];
	RslElement *ms_pWavyAtomic;
	RslElement *ms_pMaskAtomic;
};

struct CZone
{
	char name[8];
	CVector min, max;
	int32 type;
	int32 level;
	int16 zoneinfoDay;
	int16 zoneinfoNight;
	CZone *child;
	CZone *parent;
	CZone *next;
};

struct CTheZones
{
	int32 m_CurrLevel;
#ifdef LCS
	int32 FindIndex;
#else
	int32 unk[2];	// first two bytes looks like TotalNumberOfInfoZones
#endif
	CZone *NavigationZoneArray;
	CZone *InfoZoneArray;
	void *ZoneInfoArray;	// size 0x44
	int16 TotalNumberOfNavigationZones;
	int16 TotalNumberOfInfoZones;
	int16 TotalNumberOfZoneInfos;
	CZone *MapZoneArray;
	int16 AudioZoneArray[36];
	int16 TotalNumberOfMapZones;
	int16 NumberOfAudioZones;
};

struct CAttributeZone
{
	int16 x1;
	int16 x2;
	int16 y1;
	int16 y2;
	int16 z1;
	int16 z2;
	int16 attribute;
	int16 wantedLevelDrop;
};

struct C2dEffect
{
	// type 0
	struct Light {
		float dist;
		float outerRange;
		float size;
		float innerRange;
		uint8 flash;
		uint8 wet;
		uint8 flare;
		uint8 shadowIntens;
		uint8 flag;
		RslTexture *corona;
		RslTexture *shadow;
	};
	// type 1
	struct Particle {
		int32 subtype;	// particle type
		CVector direction;
		float scale;
	};
	// type 2
	struct Attractor {
		CVector direction;
		uint8 subtype;	// unknown
		uint8 probability;
	};
	// type 3
	struct PedBehaviour {
		CVector direction;
		CVector rotation;
		uint8 subtype;	// behaviour
	};

	CVuVector pos;
	rw::RGBA colour;
	uint8 type;
	union {
		Light light;
		Particle particle;
		Attractor attractor;
		PedBehaviour pedbehaviour;
	};

	int pad1, pad2;	// align the CVuVector
};
//static_assert(sizeof(C2dEffect) == 0x40, "C2dEffect: error");

struct CPathNode
{
#ifdef LCS
	// from VC, but seems to work for LCS too
	int16 prevIndex;
	int16 nextIndex;
	int16 x;
	int16 y;
	int16 z;
	int16 unknown;
	int16 firstLink;
	int8 width;
	int8 group;
	int8 numLinks : 4;
	int8 bDeadEnd : 1;
	int8 bTurnedOff : 1;	// flag 8 in node info
	int8 flagA40 : 1;	// flag 20 in node info
	int8 flagA80 : 1;	// flag 4 in node info
	int8 flagB1 : 1;	// flag 10 in node info
	int8 flagB2 : 1;	// flag 2 in node info
	int8 flagB4 : 1;
	int8 speedLimit : 2;
	int8 flagB20 : 1;
	int8 flagB40 : 1;
	int8 flagB80 : 1;
	int8 spawnRate : 4;
	int8 flagsC : 4;
#else
	int16 x;
	int16 y;
	int8 z;
	uint8 unk1;	// flood fill group?
	int16 firstLink;
	uint16 numLinks : 4;
	uint16 flags : 12;
#endif
};

struct CCarPathLink
{
#ifdef LCS
	char data[12];
#else
	char data[4];
#endif
};

struct CPathConnection	// not original name
{
	uint16 idx : 14;
	uint16 bTrafficLight : 1;
	uint16 bCrossesRoad : 1;
};

struct CPathFind
{
#ifdef LCS
	CPathNode *m_pathNodes;	// m_numPathNodes
	CCarPathLink *m_carPathLinks;	// m_numCarPathLinks
	CPathConnection *m_connections;	// m_numConnections
	uint8 *m_distances;		// m_numConnections
	uint16 *m_carPathConnections;	// m_numCarPathConnections

	int32 m_numPathNodes;		// 0x158F
	int32 m_numCarPathNodes;	// 0x894
	int32 m_numPedPathNodes;	// 0xCFB
	int16 m_numMapObjects;		// 0x4C3
	int16 m_numConnections;		// 0x2CF6
	int16 m_numCarPathConnections;	// 0x11FC
	int16 pad1;	// AAAA
	int32 m_numCarPathLinks;	// 0x8FE
	int32 unk1;
	uint8 m_numGroups[2];
	CPathNode m_extraNodes[512];	// ?
	int16 pad2;
	uint8 unk2[0x64c];	// mostly AA. not on mobile it seems?
	uint8 unk3[120000];	// grid or something? no idea...
#else
	CPathNode *m_pathNodes;	// m_numPathNodes
	CCarPathLink *m_carPathLinks;	// m_numCarPathLinks
	uint16 *m_carPathConnections;	// m_numCarPathConnections

	int32 m_numPathNodes;		// 0x20C3
	int32 m_numCarPathNodes;	// 0xC07
	int32 m_numPedPathNodes;	// 0x14BC
	int16 m_numMapObjects;		// 0
	int16 m_numConnections;		// 0x44F4
	int16 m_numCarPathConnections;	// 0x18D2
	int16 pad1;	// AAAA
	int32 m_numCarPathLinks;	// 0xC69
	int16 pad2;	// AAAA
	int16 unk1;	// 0
	uint8 unk2[0x648];	// mostly AA
	uint8 unk3[30000];	// grid or something? no idea...

	CPathConnection *m_connections;	// m_numConnections
	uint8 *m_distances;		// m_numConnections

	int32 unk4;	// 0
	int32 unk5;	// 0
	uint8 unk6[6000];	// mostly AA
	// 5 more ints?
#endif
};

struct ColEntry
{
	int32 unk;
	uint8 loaded;
	CRect rect;
#ifdef VCS
	CRect rect2;	// what is this?
#endif
	char name[20];
#ifdef LCS
	int16 firstIndex;
	int16 lastIndex;
#else
	int32 firstIndex;
	int32 lastIndex;
#endif
	uint8 *chunkData;	// pointer to relocatable chunk, includes header
};

struct CPool_col
{
	ColEntry *items;
	uint8 *flags;
	int32  size;
	int32  allocPtr;
	char name[16];
};

struct CStreamingInfo
{
#ifdef LCS
	CStreamingInfo *pNextRequest;
	CStreamingInfo *pPrevRequest;
	uint8 uLoadStatus;
	uint8 uLoadFlags;
	int16 nModelIndex;
	int32 cdPosn;
	int32 cdSize;
#else
	int32 field_0;	// uninitialized
	int32 field_4;	// 0
	int32 field_8;	// 0
	int16 field_C;	// 0
	int16 field_E;	// uninitialized
	int32 cdPosn;
	int32 cdSize;
#endif
};

struct CStreaming
{
#ifdef LCS
	// MOBILE! the beginning is the same on PS2 but count on it
	int32 field_0;
	CStreamingInfo ms_aInfoForModel[6175];
	int32 ms_imageOffsets;
	int32 field_1E274;
	int32 ms_imageSize;
	int32 field_1E27C;
	uint8 array_1E280[50];
	int32 field_1E2B4;
	int32 field_1E2B8;
	int32 field_1E2BC;
	int32 field_1E2C0;
	int32 field_1E2C4;
	int32 field_1E2C8;
	int32 field_1E2CC;
	uint8 array_1E2D0[16];
	int32 field_1E2E0;
	int32 field_1E2E4;
	int32 ms_currentPedGrp;
	int16 ms_loadedGangs;
	int16 field_1E2EE;
	int32 field_1E2F0;
	int32 ms_streamingBufferSize;
	void *ms_pExtraObjectsDir;
	CStreamingInfo ms_startLoadedList;
	CStreamingInfo ms_endLoadedList;
	CStreamingInfo ms_startRequestedList;
	CStreamingInfo ms_endRequestedList;
	int32 IslandLODIndID;
	int32 IslandLODcomINDId;
	int32 IslandLODcomSUBID;
	int32 IslandLODsubINDID;
	int32 IslandLODsubCOMID;
	int32 field_1E360;
	int32 field_1E364;
	int32 field_1E368;
	int32 field_1E36C;
	int32 field_1E370;

	// streaming ranges
	// 4900 models	(dff, mdl)
	// 1200 txds	(txd, tex)
	// 15 cols	(col, col2)
	// 60 ifps	(ifp, anim)
	int32 GetTxdOffset(void) { return 4900; }
	int32 GetColOffset(void) { return 4900 + 1200; }
	int32 GetIfpOffset(void) { return 4900 + 1200 + 15; }
	int32 GetNumStreamInfo(void) { return 4900 + 1200 + 15 + 60; }
#else
	// this thing is way different again, thanks R*
	int32 field_0;	// 0
	int32 field_4;	// 74000
	int32 txdIdOffset;
	int32 colIdOffset;
	int32 ifpIdOffset;
	int32 numStreamInfos;
	int32 field_18;	// 0
	int32 field_1C;	// C
	uint8 array_20[0x50];	// probably same as array_1E280 above
	uint8 array_70[0x58];
	CStreamingInfo *ms_aInfoForModel;
	uint32 fields_CC[6];	// probably two linked lists
	void *ms_pExtraObjectsDir;
	// ...
	int32 GetTxdOffset(void) { return txdIdOffset; }
	int32 GetColOffset(void) { return colIdOffset; }
	int32 GetIfpOffset(void) { return ifpIdOffset; }
	int32 GetNumStreamInfo(void) { return numStreamInfos; }
#endif
};

struct ResourceImage {
	CPathFind *paths;
	CPool_entity *buildingPool;
	CPool_entity *treadablePool;
	CPool_entity *dummyPool;
	CPool_generic *entryInfoNodePool;
	CPool_generic *ptrNodePool;
	int32 numModelInfos;
	CBaseModelInfo **modelInfoPtrs;
	void *carArrays;
	void *totalNumOfCarsOfRating;
	CTheZones *theZones;	// gta3.zon
	void *sectors;
	void *bigBuildingList;
	int32 num2deffects;
	C2dEffect *_2deffects;
	int16 *modelIndices;
	CPool_txd *texlistPool;
	RslTexList *storedTexList;
	CPool_col *colPool;
	int32 colOnlyBB;
	void *tempColModels;
	void *objectInfo;	// object.dat
#ifdef LCS
	CVehicleModelInfo__inst *vehicleModelInfo_Info;	// carcols.dat
#else
	void *unknown0;
#endif
	CStreaming *streaming_Inst;	// gta3.dir

	CAnimManager *animManagerInst;
	void *fightMoves;		// fistfite.dat

#ifdef LCS
	void *pedAnimInfo;
#endif
	void *pedType;		// ped.dat
	PedStats **pedStats;		// pedstats.dat [42]

#ifdef VCS
	uint8 *colourTable;	// pedcols.dat + carcols.dat
	void *unknown1;
#endif

	int32 numAttributeZones;	// cull.ipl
	CAttributeZone *attributeZones;		// cull.ipl
	int32 numOccludersOnMap;	// empty
	void *occluders;
	CWaterLevel *waterLevelInst;		// waterpro.dat
#ifdef LCS
	void *handlingManager;		// handling.dat
#else
	void *weatherTypeList;
#endif
	void *adhesiveLimitTable;		// surface.dat
	CTimeCycle *timecycle;			// timecyc.dat
	void *pedGroups;			// pedgrp.dat
	void *particleSystemManager;	// particle.dat
	void *weaponTables;		// weapon.dat
#ifdef VCS
	int unknown2;
#endif
	RslElementGroup **markers;	// [32] in LCS
	int cutsceneDir;

#ifdef LCS
	void *ferryInst;	// ferry.dat
	void *trainInst;	// tracks(2).dat
	void *planeInst;	// flight.dat
//	int UNUSED;	// only on mobile, which doesn't write a game.dtz/dat to begin with
#else
	// one probably flight.dat
	void *unknown3;
	void *unknown4;	// 0
	void *unknown5;
	void *unknown6;	// 0
#endif
	void *menuCompressedTextures;
#ifdef VCS
	void *unknown7;
	void *unknown8;
	void *unknown9;
	int32 unknown10;
	void *fontdef;
#endif
	int fontTexListSize;
	void *fontCompressedTexList;
#ifdef VCS
	void *radarTextures;
	// some more
#endif
};
//#ifdef VCS
//static_assert(offsetof(ResourceImage, animManagerInst)+0x20 == 0x80, "ResourceImage: error");
//static_assert(offsetof(ResourceImage, weatherTypeList) == 0x8C, "ResourceImage: error");
//static_assert(offsetof(ResourceImage, timecycle) == 0x94, "ResourceImage: error");
//static_assert(offsetof(ResourceImage, markers)+0x20 == 0xC8, "ResourceImage: error");
//static_assert(offsetof(ResourceImage, menuCompressedTextures)+0x20 == 0xE0, "ResourceImage: error");
//static_assert(offsetof(ResourceImage, fontTexListSize)+0x20 == 0xF8, "ResourceImage: error");
//#endif
//#ifdef LCS
//static_assert(offsetof(ResourceImage, animManagerInst)+0x20 == 0x80, "ResourceImage: error");
//static_assert(offsetof(ResourceImage, markers)+0x20 == 0xC0, "ResourceImage: error");
//static_assert(offsetof(ResourceImage, ferryInst)+0x20 == 0xC8, "ResourceImage: error");
//#endif
