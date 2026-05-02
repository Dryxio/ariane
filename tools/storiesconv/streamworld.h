


#ifdef VCS
#define NUMSECTORSX 32
#define NUMSECTORSY 36
#endif

#ifdef LCS
#define NUMSECTORSX 40
#define NUMSECTORSY 46
#endif

enum {
	// pass 0
	SECLIST_SUPERLOD,
#ifdef VCS
	SECLIST_UNDERWATER,
#endif
	SECLIST_LOD,
	SECLIST_ROADS,
	// pass 1
	SECLIST_NORMAL,
	SECLIST_NOZWRITE,
	// pass 2
	SECLIST_LIGHTS,
	SECLIST_TRANSPARENT,
	SECLIST_END,
	NUMSECTORLISTS
};

// some type names from LCS:
//	sDMAModel	  // lucid
//	sDMAMaterial	  // lucid
//	sDMAMesh	  // lucid
//	cWorldGeom
//	cWorldGeom::sModel
//	cWorldGeom::sModelInstance
//	cWorldGeom::cSector
//	sLevelSectorDirectory
//	sStripData
//	sDynamic
//	sLevelSwap
//	sInteriorSwap
//	sBuildingSwapInfo

struct sBuildingGeometry	// leeds name
{
	uint16 numMeshes;
	uint16 size;
	// array of numMeshes sClippableBuildingMeshes
	// dma data
};

struct sClippableBuildingMesh	// leeds name
{
#ifdef LCS
	uint16  texID;		// index into resource table
	uint16  packetSize;	// upper bit unknown
#else
	int32   packetSize;
	uint16  texID;		// index into resource table
#endif
	float16 uvScale[2];	// half precision float
	uint16  unk1;
	float16 min[3];		// bounding box
	float16 max[3];

	int GetPacketSize(void){
#ifdef LCS
		return packetSize&0x7FFF;
#else
		return packetSize>>1;
#endif
	}
};


struct Resource
{
	union {
		RslRaster         *raster;
		sBuildingGeometry *geometry;
		uint8             *raw;
	};
	void *dmaChain;	// at runtime
#ifdef VCS
	int id;
#endif
};

// One row of streamable sectors
struct sLevelSectorDirectory	// not completely sure about the name
{
	sChunkHeader *header;	// an array
	int32 startOff;	// starting offset of this row, (actually 16 bit i think)
			// (startOff sectors in the world will be considered empty)
};

struct sLevelSwap
{
	// There are three cases:
	// timeOff == 0xFF -> unconditional swap
	// timeOff &  0x80 -> timed object with timeOff&0x7F
	// otherwise -> timeOff building swap slot, timeOn swap state
	uint8 timeOff;
	uint8 timeOn;
	int16 id;	// instance ID

	bool IsUnconditional(void) { return timeOff == 0xFF; }
	bool IsTimed(void) { return !!(timeOff&0x80); }
	int GetTimeOff_Slot(void) { return timeOff&0x7F; }
	int GetTimeOn_State(void) { return timeOn; }
	bool IsVisible(void);
};

typedef sLevelSwap sBuildingSwapInfo;
/*
// rather identical to the above
struct sBuildingSwapInfo
{
	uint8 timeOff;
	uint8 timeOn;
	int16 id;	// sector ID

	bool IsUnconditional(void) { return timeOff == 0xFF; }
	bool IsTimed(void) { return !!(timeOff&0x80); }
	int GetTimeOff_Slot(void) { return timeOff&0x7F; }
	int GetTimeOn_State(void) { return timeOn; }
};
*/

struct AreaResource
{
	int16 id1;
	int16 id2;
	void *data;
};

struct Area
{
	int32 numResources;
	AreaResource *resources;
};

struct AreaInfo
{
	int16 a, b;	// sector coords or something?
	uint32 fileOffset;	// into image, start of sChunkHeader
	uint32 fileSize;
	int32 numResources;
};

struct SwapInfo
{
	union {
		int32 buildingId;	// in file
		CEntity *building;	// after load
	};
	uint32 hash;		// this is the interface to the script
	int32 swapState;	// 0 not included
	int32 modelA;		// model ID for 0 state
	int32 modelB;		// model ID for swapped state
	int32 swapSlot;
	int32 field_18;
};

struct sInteriorSwap
{
	uint8 secx, secy;	// position of this sector
	uint8 swapSlot;		// index into the building swap array
	uint8 swapState;	// normally 0 or 1, but can go higher
	int16 sectorId;		// sector index
};

struct sDynamic
{
	// from majestic. looks right
	RslV3 scale;
	int16 modelId;
	int16 resId;
	RslV3 pos;
	uint16 flags;
	int16 resId2;	// transparent
	float bounds[4];
};

struct OverlayResource
{
	int32 id;
	union {
		RslRaster         *raster;
		sBuildingGeometry *geometry;
		uint8             *raw;
	};
};

struct sGeomInstance
{
	uint16  id;
	uint16  resId;
	float16 bound[4];
	int32   pad;
	RslMatrix matrix;

	int16 GetId(void) { return id & 0x7FFF; }
	bool IsFlagged(void) {
		return !!(id & 0x8000);
/*
#ifdef LCS
		return !(id & 0x8000);
#else
		return !!(id & 0x8000);
#endif
*/
	}
};
#ifndef RW_PS2
static_assert(sizeof(sGeomInstance) == 0x50, "sGeomInstance: error");
#endif

struct Sector
{
	OverlayResource *resources;
	uint16           numResources;
	uint16           unk1;
	sGeomInstance	*passes[NUMSECTORLISTS];
	int16            numSwaps;
	uint16           unk3;
	// cf cWorldStream::QueueBuildingSwaps
	sBuildingSwapInfo *swaps;
};

struct sLevelChunk	// leeds name
{
	Resource *resourceTable;
	sLevelSectorDirectory sectorRows[NUMSECTORSY];
	sLevelSectorDirectory sectorEnd;
	int32 numResources;
	// Positions for the first 32 building instances...what's so special about them?
	CVector positions[32];

#ifdef VCS
	int32 numSwapInfos;
	SwapInfo *swapInfos;	// 28 bytes
#endif
	int32 numLevelSwaps;
	sLevelSwap *levelSwaps;
#ifdef LCS
	int32 numDynamics;
	sDynamic *dynamics;
#endif
	int32 numInteriors;
	sInteriorSwap *interiors;

#ifdef LCS
	int32 numRadarSections;
	sChunkHeader *radarSections;
#endif

#ifdef VCS
	// dynamics again?
	int32 numDynamics;
	sDynamic *dynamics;

	int32 numAreas;
	AreaInfo *areas;
#endif
};
//#ifdef VCS
//static_assert(offsetof(sLevelChunk, numResources)+0x20 == 0x14c, "sLevelChunk: error");
////static_assert(offsetof(sLevelChunk, numTimeObjects)+0x20 == 0x2d0, "sLevelChunk: error");
//#endif
//
//#ifdef LCS
//static_assert(offsetof(sLevelChunk, numResources)+0x20 == 0x19c, "sLevelChunk: error");
////static_assert(offsetof(sLevelChunk, numTimeObjects)+0x20 == 0x320, "sLevelChunk: error");
//#endif
