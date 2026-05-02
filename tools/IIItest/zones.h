#ifndef _ZONES_H_
#define _ZONES_H_

//
// Zones
//

enum eZoneType
{
	ZONE_AUDIO,
	ZONE_TYPE1,	// this should be NAVIG
	ZONE_TYPE2,	// this should be INFO...but all except MAPINFO get zoneinfo??
	ZONE_MAPZONE,
};

enum eZoneAttribs
{
	ATTRZONE_CAMCLOSEIN		= 1,
	ATTRZONE_STAIRS			= 2,
	ATTRZONE_1STPERSON		= 4,
	ATTRZONE_NORAIN			= 8,
	ATTRZONE_NOPOLICE		= 0x10,
	ATTRZONE_NOTCULLZONE		= 0x20,
	ATTRZONE_DOINEEDCOLLISION	= 0x40,
	ATTRZONE_SUBWAYVISIBLE		= 0x80,
};

enum eLevelName
{
	LEVEL_NONE = 0,
	LEVEL_INDUSTRIAL,
	LEVEL_COMMERCIAL,
	LEVEL_SUBURBAN
};

class CZone
{
public:
	char       name[8];
	float      minx;
	float      miny;
	float      minz;
	float      maxx;
	float      maxy;
	float      maxz;
	eZoneType  type;
	eLevelName level;
	short      zoneinfoNight;
	short      zoneinfoDay;
	CZone     *child;
	CZone     *parent;
	CZone     *next;
};

class CZoneInfo
{
public:
	ushort carDensity;

	ushort carThreshold[6];
	ushort copThreshold;
	ushort gangThreshold[8];
	ushort car6Threshold;

	ushort pedDensity;
	ushort copDensity;
	ushort gangDensity[9]; // really 9?
	ushort pedGroup;
};

class CTheZones
{
public:
	static eLevelName m_CurrLevel;
	static CZone *m_pPlayersZone;
	// FindIndex

	static ushort NumberOfAudioZones;
	static short AudioZoneArray[NUMAUDIOZONES];
	static ushort TotalNumberOfMapZones;
	static ushort TotalNumberOfZones;
	static CZone ZoneArray[NUMZONES];
	static CZone MapZoneArray[NUMMAPZONES];
	static ushort TotalNumberOfZoneInfos;
	static CZoneInfo ZoneInfoArray[2*NUMZONES];

	static void Init(void);
	static void Update(void);
	static void PostZoneCreation(void);
	static void CreateZone(char *name, eZoneType type,
	                       float minx, float miny, float minz,
	                       float maxx, float maxy, float maxz,
	                       eLevelName level);
	static void CreateMapZone(char *name, eZoneType type,
	                          float minx, float miny, float minz,
	                          float maxx, float maxy, float maxz,
	                          eLevelName level);
	static CZone *GetZone(ushort i) { return &ZoneArray[i]; }
	static void InsertZoneIntoZoneHierarchy(CZone *zone);
	static bool InsertZoneIntoZoneHierRecursive(CZone *z1, CZone *z2);
	static bool ZoneIsEntirelyContainedWithinOtherZone(CZone *z1, CZone *z2);
	static bool PointLiesWithinZone(const CVector &v, CZone *zone);
	static eLevelName GetLevelFromPosition(const CVector &v);
	static CZone *FindSmallestZonePosition(const CVector &v);
	static void AddZoneToAudioZoneArray(CZone *zone);
	static void InitialiseAudioZoneArray(void);
};

//
// Cull zones
//

class CCullZone
{
public:
	CVector position;
	float minx;
	float maxx;
	float miny;
	float maxy;
	float minz;
	float maxz;
	int32 m_indexStart;
	int16 unk2;
	int16 unk3;
	int16 unk4;
	int16 m_numBuildings;
	int16 m_numTreadablesPlus10m;
	int16 m_numTreadables;
};

struct CAttributeZone
{
	float minx;
	float maxx;
	float miny;
	float maxy;
	float minz;
	float maxz;
	short attributes;
	short wantedLevel;
};

class CCullZones
{
	static int  CurrentWantedLevelDrop_Player;
	static int  CurrentFlags_Camera;
	static int  OldCullZone;
	static int  EntityIndicesUsed;
	static int  CurrentFlags_Player;
	static bool bCurrentSubwayIsInvisible;

	static int       NumCullZones;
	static CCullZone aZones[NUMCULLZONES];
	static int       NumAttributeZones;
	static CAttributeZone aAttributeZones[NUMATTRIBZONES];
	static ushort    aIndices[NUMZONEINDICES];
	static short     aPointersToBigBuildingsForBuildings[NUMBUILDINGS];
	static short     aPointersToBigBuildingsForTreadables[NUMTREADABLES];
public:
	static void Init(void);
	static void AddCullZone(CVector const &position,
	                        float minx, float maxx,
	                        float miny, float maxy,
	                        float minz, float maxz,
	                        ushort flag, short wantedLevel);
};

#endif
