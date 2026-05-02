#include "III.h"

eLevelName CTheZones::m_CurrLevel;
CZone     *CTheZones::m_pPlayersZone;

ushort    CTheZones::NumberOfAudioZones;
short     CTheZones::AudioZoneArray[NUMAUDIOZONES];
ushort    CTheZones::TotalNumberOfMapZones;
ushort    CTheZones::TotalNumberOfZones;
CZone     CTheZones::ZoneArray[NUMZONES];
CZone     CTheZones::MapZoneArray[NUMMAPZONES];
ushort    CTheZones::TotalNumberOfZoneInfos;
CZoneInfo CTheZones::ZoneInfoArray[2*NUMZONES];

#define SWAPF(a, b) { float t; t = a; a = b; b = t; }

void
CTheZones::Init(void)
{
	int i;
	for(i = 0; i < NUMAUDIOZONES; i++)
		AudioZoneArray[i] = -1;
	NumberOfAudioZones = 0;

	CZoneInfo *zonei;
	int x = 1000/6;
	for(i = 0; i < 2*NUMZONES; i++){
		zonei = &ZoneInfoArray[i];
		zonei->carDensity = 10;
		zonei->carThreshold[0] = x;
		zonei->carThreshold[1] = zonei->carThreshold[0] + x;
		zonei->carThreshold[2] = zonei->carThreshold[1] + x;
		zonei->carThreshold[3] = zonei->carThreshold[2] + x;
		zonei->carThreshold[4] = zonei->carThreshold[3];
		zonei->carThreshold[5] = zonei->carThreshold[4];
		zonei->copThreshold = zonei->carThreshold[5] + x;
		zonei->gangThreshold[0] = zonei->copThreshold;
		zonei->gangThreshold[1] = zonei->gangThreshold[0];
		zonei->gangThreshold[2] = zonei->gangThreshold[1];
		zonei->gangThreshold[3] = zonei->gangThreshold[2];
		zonei->gangThreshold[4] = zonei->gangThreshold[3];
		zonei->gangThreshold[5] = zonei->gangThreshold[4];
		zonei->gangThreshold[6] = zonei->gangThreshold[5];
		zonei->gangThreshold[7] = zonei->gangThreshold[6];
		zonei->car6Threshold = zonei->gangThreshold[7];
	}
	TotalNumberOfZoneInfos = 1;	// why 1?

	for(i = 0; i < NUMZONES; i++)
		memset(&ZoneArray[i], 0, sizeof(CZone));
	strcpy(ZoneArray[0].name, "CITYZON");
	ZoneArray[0].minx = -4000.0f;
	ZoneArray[0].miny = -4000.0f;
	ZoneArray[0].minz = -500.0f;
	ZoneArray[0].maxx =  4000.0f;
	ZoneArray[0].maxy =  4000.0f;
	ZoneArray[0].maxz =  500.0f;
	ZoneArray[0].level = LEVEL_NONE;
	TotalNumberOfZones = 1;

	m_CurrLevel = LEVEL_NONE;
	m_pPlayersZone = &ZoneArray[0];

	for(i = 0; i < NUMMAPZONES; i++){
		memset(&MapZoneArray[i], 0, sizeof(CZone));
		MapZoneArray[i].type = ZONE_MAPZONE;
	}
	strcpy(MapZoneArray[0].name, "THEMAP");
	MapZoneArray[0].minx = -4000.0f;
	MapZoneArray[0].miny = -4000.0f;
	MapZoneArray[0].minz = -500.0f;
	MapZoneArray[0].maxx =  4000.0f;
	MapZoneArray[0].maxy =  4000.0f;
	MapZoneArray[0].maxz =  500.0f;
	MapZoneArray[0].level = LEVEL_NONE;
	TotalNumberOfMapZones = 1;
}

void
CTheZones::Update(void)
{
	CVector pos = FindPlayerCoors();
	m_pPlayersZone = FindSmallestZonePosition(pos);
	m_CurrLevel = GetLevelFromPosition(pos);
}

void
CTheZones::CreateZone(char *name, eZoneType type,
	              float minx, float miny, float minz,
	              float maxx, float maxy, float maxz,
	              eLevelName level)
{
	CZone *zone;
	char *p;

	if(minx > maxx) SWAPF(minx, maxx);
	if(miny > maxy) SWAPF(miny, maxy);
	if(minz > maxz) SWAPF(minz, maxz);

	// make upper case
	for(p = name; *p; p++) if(islower(*p)) *p = toupper(*p);

	// add zone
	zone = &ZoneArray[TotalNumberOfZones++];
	strncpy(zone->name, name, 7);
	zone->name[7] = '\0';
	zone->type = type;
	zone->minx = minx;
	zone->miny = miny;
	zone->minz = minz;
	zone->maxx = maxx;
	zone->maxy = maxy;
	zone->maxz = maxz;
	zone->level = level;
	if(type == ZONE_AUDIO || type == ZONE_TYPE1 || type == ZONE_TYPE2){
		zone->zoneinfoDay = TotalNumberOfZoneInfos++;
		zone->zoneinfoNight = TotalNumberOfZoneInfos++;
	}
}

void
CTheZones::CreateMapZone(char *name, eZoneType type,
	                 float minx, float miny, float minz,
	                 float maxx, float maxy, float maxz,
	                 eLevelName level)
{
	CZone *zone;
	char *p;

	if(minx > maxx) SWAPF(minx, maxx);
	if(miny > maxy) SWAPF(miny, maxy);
	if(minz > maxz) SWAPF(minz, maxz);

	// make upper case
	for(p = name; *p; p++) if(islower(*p)) *p = toupper(*p);

	// add zone
	zone = &MapZoneArray[TotalNumberOfMapZones++];
	strncpy(zone->name, name, 7);
	zone->name[7] = '\0';
	zone->type = type;
	zone->minx = minx;
	zone->miny = miny;
	zone->minz = minz;
	zone->maxx = maxx;
	zone->maxy = maxy;
	zone->maxz = maxz;
	zone->level = level;
}

void
CTheZones::PostZoneCreation(void)
{
	int i;
	for(i = 1; i < TotalNumberOfZones; i++)
		InsertZoneIntoZoneHierarchy(&ZoneArray[i]);
	InitialiseAudioZoneArray();
}

void
CTheZones::InsertZoneIntoZoneHierarchy(CZone *zone)
{
	zone->child = nil;
	zone->parent = nil;
	zone->next = nil;
	InsertZoneIntoZoneHierRecursive(zone, &ZoneArray[0]);
}

bool
CTheZones::InsertZoneIntoZoneHierRecursive(CZone *inner, CZone *outer)
{
	int n;
	CZone *child, *next, *insert;

	// return false if inner was not inserted into outer
	if(outer == nil ||
	   !ZoneIsEntirelyContainedWithinOtherZone(inner, outer))
		return false;

	// try to insert inner into children of outer
	for(child = outer->child; child; child = child->next)
		if(InsertZoneIntoZoneHierRecursive(inner, child))
			return true;

	// insert inner as child of outer
	// count number of outer's children contained within inner
	n = 0;
	for(child = outer->child; child; child = child->next)
		if(ZoneIsEntirelyContainedWithinOtherZone(child, inner))
			n++;
	inner->next = outer->child;
	inner->parent = outer;
	outer->child = inner;
	// move children from outer to inner
	if(n){
		insert = inner;
		for(child = inner->next; child; child = next){
			next = child->next;
			if(ZoneIsEntirelyContainedWithinOtherZone(child,inner)){
				insert->next = child->next;
				child->parent = inner;
				child->next = inner->child;
				inner->child = child;
			}else
				insert = child;
		}
	}

	return true;
}

bool
CTheZones::ZoneIsEntirelyContainedWithinOtherZone(CZone *inner, CZone *outer)
{
	if(inner->minx < outer->minx ||
	   inner->maxx > outer->maxx ||
	   inner->miny < outer->miny ||
	   inner->maxy > outer->maxy ||
	   inner->minz < outer->minz ||
	   inner->maxz > outer->maxz){
		CVector min(inner->minx, inner->miny, inner->minz);
		if(PointLiesWithinZone(min, outer))
			debug("Overlapping zones %s and %s\n",
			      inner->name, outer->name);
		CVector max(inner->maxx, inner->maxy, inner->maxz);
		if(PointLiesWithinZone(max, outer))
			debug("Overlapping zones %s and %s\n",
			      inner->name, outer->name);
		return false;
	}
	return true;
}

bool
CTheZones::PointLiesWithinZone(const CVector &v, CZone *zone)
{
	return zone->minx <= v.x && v.x <= zone->maxx &&
	       zone->miny <= v.y && v.y <= zone->maxy &&
	       zone->minz <= v.z && v.z <= zone->maxz;
}

eLevelName
CTheZones::GetLevelFromPosition(CVector const &v)
{
	int i;
//	if(!PointLiesWithinZone(v, &MapZoneArray[0]))
//		debug("x = %.3f y= %.3f z = %.3f\n", v->x, v->y, v->z);
	for(i = 1; i < TotalNumberOfMapZones; i++)
		if(PointLiesWithinZone(v, &MapZoneArray[i]))
			return MapZoneArray[i].level;
	return MapZoneArray[0].level;
}

CZone*
CTheZones::FindSmallestZonePosition(const CVector &v)
{
	CZone *best = &ZoneArray[0];
	// zone to test next
	CZone *zone = ZoneArray[0].child;
	while(zone)
		// if in zone, descent into children
		if(PointLiesWithinZone(v, zone)){
			best = zone;
			zone = zone->child;
		// otherwise try next zone
		}else
			zone = zone->next;
	return best;
}

void
CTheZones::AddZoneToAudioZoneArray(CZone *zone)
{
	if(zone->type == ZONE_AUDIO)
		AudioZoneArray[NumberOfAudioZones++] = zone - ZoneArray;
}

void
CTheZones::InitialiseAudioZoneArray(void)
{
	bool gonext;
	CZone *zone;

	gonext = false;
	zone = &ZoneArray[0];
	// Go deep first,
	// set gonext when backing up a level to visit the next child
	while(zone)
		if(gonext){
			AddZoneToAudioZoneArray(zone);
			if(zone->next){
				gonext = false;
				zone = zone->next;
			}else
				zone = zone->parent;
		}else if(zone->child)
			zone = zone->child;
		else{
			AddZoneToAudioZoneArray(zone);
			if(zone->next)
				zone = zone->next;
			else{
				gonext = true;
				zone = zone->parent;
			}
		}
}

//
// CCullZones
//

int  CCullZones::CurrentWantedLevelDrop_Player;
int  CCullZones::CurrentFlags_Camera;
int  CCullZones::OldCullZone;
int  CCullZones::EntityIndicesUsed;
int  CCullZones::CurrentFlags_Player;
bool CCullZones::bCurrentSubwayIsInvisible;

int       CCullZones::NumCullZones;
CCullZone CCullZones::aZones[NUMCULLZONES];
int       CCullZones::NumAttributeZones;
CAttributeZone CCullZones::aAttributeZones[NUMATTRIBZONES];
ushort    CCullZones::aIndices[NUMZONEINDICES];
short     CCullZones::aPointersToBigBuildingsForBuildings[NUMBUILDINGS];
short     CCullZones::aPointersToBigBuildingsForTreadables[NUMTREADABLES];

void
CCullZones::Init(void)
{
	int i;

	CurrentWantedLevelDrop_Player = 0;
	NumAttributeZones = 0;
	NumCullZones = 0;
	CurrentFlags_Camera = 0;
	OldCullZone = -1;
	EntityIndicesUsed = 0;
	CurrentFlags_Player = 0;
	bCurrentSubwayIsInvisible = 0;
	for(i = 0; i < NUMBUILDINGS; i++)
		aPointersToBigBuildingsForBuildings[i] = -1;
	for(i = 0; i < NUMTREADABLES; i++)
		aPointersToBigBuildingsForTreadables[i] = -1;
}

void
CCullZones::AddCullZone(CVector const &position,
                        float minx, float maxx,
                        float miny, float maxy,
                        float minz, float maxz,
                        ushort flag, short wantedLevel)
{
	CCullZone *cull;
	CAttributeZone *attrib;

	CVector v;
	if((flag & 0x20) == 0){
		cull = &aZones[NumCullZones++];
		v = position;
		// WTF is this?
		if((v-CVector(1032.14f, -624.255f, 24.93f)).Magnitude() < 1.0f)
			v = CVector(1061.7f, -613.0f, 19.0f);
		if((v-CVector(1029.48f, -495.757f, 21.98f)).Magnitude() < 1.0f)
			v = CVector(1061.4f, -506.0f, 18.5f);
		cull->position.x = clamp(v.x, minx, maxx);
		cull->position.y = clamp(v.y, miny, maxy);
		cull->position.z = clamp(v.z, minz, maxz);
		cull->minx = minx;
		cull->maxx = maxx;
		cull->miny = miny;
		cull->maxy = maxy;
		cull->minz = minz;
		cull->maxz = maxz;
		cull->unk2 = 0;
		cull->unk3 = 0;
		cull->unk4 = 0;
		cull->m_indexStart = 0;
	}
	if(flag & ~0x20){
		attrib = &aAttributeZones[NumAttributeZones++];
		attrib->minx = minx;
		attrib->maxx = maxx;
		attrib->miny = miny;
		attrib->maxy = maxy;
		attrib->minz = minz;
		attrib->maxz = maxz;
		attrib->attributes = flag;
		attrib->wantedLevel = wantedLevel;
	}
}
