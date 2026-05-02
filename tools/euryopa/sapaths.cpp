#include "euryopa.h"
#include "modloader.h"
#include "sapaths.h"
#include <cctype>
#include <cfloat>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <cstring>
#include <string>
#include <stdio.h>
#include <vector>

bool ReadCdImageEntryByLogicalPath(const char *logicalPath, std::vector<uint8> &data,
                                   char *outSourcePath, size_t outSourcePathSize);
bool ReadCdImageEntryByLogicalPathMinSize(const char *logicalPath, size_t minSize,
                                          std::vector<uint8> &data,
                                          char *outSourcePath, size_t outSourcePathSize);
bool GetCdImageEntryInfoByLogicalPath(const char *logicalPath, int *outEntryBytes,
                                      bool *outCompressed, bool *outLooseOverride,
                                      char *outSourcePath, size_t outSourcePathSize);
bool WriteCdImageEntryByLogicalPath(const char *logicalPath, uint8 *data, int size);

namespace SAPaths {

static const int NUM_PATH_AREAS = 64;
static const int NUM_DYNAMIC_LINK_SLOTS = 16*12;
static const float PATH_POS_SCALE = 1.0f/8.0f;
static const float PATH_WIDTH_SCALE = 1.0f/16.0f;
static const float NAVI_POS_SCALE = 1.0f/8.0f;
static const float NAVI_WIDTH_SCALE = 1.0f/16.0f;
static const float PATH_DRAW_DIST = 300.0f;
static const float NODE_DRAW_RADIUS = 0.9f;
static const float LANE_WIDTH = 5.0f;
static const float AREA_SIZE = 750.0f;
static const size_t MAX_LENIENT_IMG_OVERREAD_BYTES = 2048;
static const float PREVIEW_PI = 3.14159265358979323846f;
static const float PREVIEW_PED_Z_OFFSET = 1.08f;
static const float PREVIEW_CAR_Z_OFFSET = 0.50f;
static const int PREVIEW_POP_GROUP_COUNT = 18;
static const int PREVIEW_POP_ZONE_COUNT = 20;
static const int PREVIEW_POP_DAYSETS = 2;
static const int PREVIEW_POP_TIMESLOTS = 12;
static const int PREVIEW_CAR_GROUP_OTHER_COUNT = 18;
static const int PREVIEW_CAR_GROUP_GANG_BASE = 18;
static const int PREVIEW_CAR_GROUP_DEALERS = 28;
static const int PREVIEW_TRAFFIC_CURVE_SAMPLES = 24;
static const float PREVIEW_TRAFFIC_SPEED_MULTIPLIER = 3.0f;

struct NodeAddress {
	uint16 areaId;
	uint16 nodeId;

	bool isValid(void) const {
		return areaId != 0xFFFF && nodeId != 0xFFFF;
	}
};

struct CarPathLinkAddress {
	uint16 raw;

	bool isValid(void) const {
		return raw != 0xFFFF;
	}
	uint16 areaId(void) const { return raw >> 10; }
	uint16 nodeId(void) const { return raw & 0x03FF; }
};

struct DiskNode {
	uint32 next;
	uint32 prev;
	int16 x, y, z;
	int16 totalDistFromOrigin;
	int16 baseLinkId;
	uint16 areaId;
	uint16 nodeId;
	uint8 pathWidth;
	uint8 floodFill;
	uint8 flags0;
	uint8 flags1;
	uint8 flags2;
	uint8 flags3;
};

struct DiskCarPathLink {
	int16 x, y;
	uint16 attachedAreaId;
	uint16 attachedNodeId;
	int8 dirX, dirY;
	int8 pathWidth;
	uint8 laneFlags;
	uint16 flags;
};

static_assert(sizeof(NodeAddress) == 4, "NodeAddress size mismatch");
static_assert(sizeof(CarPathLinkAddress) == 2, "CarPathLinkAddress size mismatch");
static_assert(sizeof(DiskNode) == 28, "DiskNode size mismatch");
static_assert(sizeof(DiskCarPathLink) == 14, "DiskCarPathLink size mismatch");

static int getNaviOppositeLanes(const DiskCarPathLink &link);
static int getNaviSameLanes(const DiskCarPathLink &link);

struct Node {
	DiskNode raw;
	uint16 ownerAreaId;
	uint32 index;

	int numLinks(void) const { return raw.flags0 & 0x0F; }
	void setNumLinks(int n) { raw.flags0 = (raw.flags0 & 0xF0) | (n & 0x0F); }

	bool onDeadEnd(void) const { return (raw.flags0 & 0x10) != 0; }
	void setOnDeadEnd(bool b) { raw.flags0 = b ? raw.flags0 | 0x10 : raw.flags0 & ~0x10; }

	bool isSwitchedOff(void) const { return (raw.flags0 & 0x20) != 0; }
	void setSwitchedOff(bool b) { raw.flags0 = b ? raw.flags0 | 0x20 : raw.flags0 & ~0x20; }

	bool roadBlocks(void) const { return (raw.flags0 & 0x40) != 0; }
	void setRoadBlocks(bool b) { raw.flags0 = b ? raw.flags0 | 0x40 : raw.flags0 & ~0x40; }

	bool waterNode(void) const { return (raw.flags0 & 0x80) != 0; }
	void setWaterNode(bool b) { raw.flags0 = b ? raw.flags0 | 0x80 : raw.flags0 & ~0x80; }

	bool switchedOffOriginal(void) const { return (raw.flags1 & 0x01) != 0; }
	void setSwitchedOffOriginal(bool b) { raw.flags1 = b ? raw.flags1 | 0x01 : raw.flags1 & ~0x01; }

	bool dontWander(void) const { return (raw.flags1 & 0x04) != 0; }
	void setDontWander(bool b) { raw.flags1 = b ? raw.flags1 | 0x04 : raw.flags1 & ~0x04; }

	bool notHighway(void) const { return (raw.flags1 & 0x10) != 0; }
	void setNotHighway(bool b) { raw.flags1 = b ? raw.flags1 | 0x10 : raw.flags1 & ~0x10; }

	bool highway(void) const { return (raw.flags1 & 0x20) != 0; }
	void setHighway(bool b) { raw.flags1 = b ? raw.flags1 | 0x20 : raw.flags1 & ~0x20; }

	int spawnProbability(void) const { return raw.flags2 & 0x0F; }
	void setSpawnProbability(int n) { raw.flags2 = (raw.flags2 & 0xF0) | (n & 0x0F); }

	int behaviourType(void) const { return (raw.flags2 >> 4) & 0x0F; }
	void setBehaviourType(int n) { raw.flags2 = (raw.flags2 & 0x0F) | ((n & 0x0F) << 4); }

	float x(void) const { return raw.x * PATH_POS_SCALE; }
	float y(void) const { return raw.y * PATH_POS_SCALE; }
	float z(void) const { return raw.z * PATH_POS_SCALE; }
	void setPosition(float px, float py, float pz) {
		raw.x = (int16)(px / PATH_POS_SCALE);
		raw.y = (int16)(py / PATH_POS_SCALE);
		raw.z = (int16)(pz / PATH_POS_SCALE);
	}

	float width(void) const { return raw.pathWidth * PATH_WIDTH_SCALE; }
	void setWidth(float w) { raw.pathWidth = (uint8)clamp((int)(w / PATH_WIDTH_SCALE + 0.5f), 0, 255); }

	NodeAddress address(void) const { return { raw.areaId, raw.nodeId }; }
};

struct AreaData {
	bool loaded;
	bool dirty;
	char logicalPath[256];
	char sourcePath[1024];
	uint32 numNodes;
	uint32 numVehicleNodes;
	uint32 numPedNodes;
	uint32 numCarPathLinks;
	uint32 numAddresses;
	std::vector<Node> nodes;
	std::vector<DiskCarPathLink> naviNodes;
	std::vector<NodeAddress> nodeLinks;
	std::vector<CarPathLinkAddress> naviLinks;
	std::vector<uint8> linkLengths;
	std::vector<uint8> intersections;
};

enum LoadSourceMode
{
	LOAD_SOURCE_AUTO = 0,
	LOAD_SOURCE_IMG,
	LOAD_SOURCE_LOOSE,
};

enum ActiveLoadSource
{
	ACTIVE_LOAD_SOURCE_NONE = 0,
	ACTIVE_LOAD_SOURCE_IMG,
	ACTIVE_LOAD_SOURCE_LOOSE,
};

static AreaData gAreas[NUM_PATH_AREAS];
static bool gLoadAttempted;
static bool gLoadSucceeded;
static bool gLoadFailed;
static LoadSourceMode gLoadSourceMode = LOAD_SOURCE_AUTO;
static ActiveLoadSource gActiveLoadSource = ACTIVE_LOAD_SOURCE_NONE;
static char gActiveLoadSourceDetail[256];
static bool gUsedLenientImgRead;
static int gLastLoadFailureArea = -1;
static char gLastLoadFailurePath[1024];
static char gLastLoadFailureReason[128];
static int gSelectedLinkIndex = -1;

Node *hoveredNode;
Node *selectedNode;
static bool gBlockedCrossAreaNodeMove;
static bool gBlockedCrossAreaNaviMove;

static const rw::RGBA red = { 255, 0, 0, 255 };
static const rw::RGBA green = { 0, 255, 0, 255 };
static const rw::RGBA cyan = { 0, 255, 255, 255 };
static const rw::RGBA magenta = { 255, 0, 255, 255 };
static const rw::RGBA yellow = { 255, 255, 0, 255 };
static const rw::RGBA white = { 255, 255, 255, 255 };

enum PreviewZonePopulationType
{
	PREVIEW_ZONE_BUSINESS = 0,
	PREVIEW_ZONE_DESERT,
	PREVIEW_ZONE_ENTERTAINMENT,
	PREVIEW_ZONE_COUNTRYSIDE,
	PREVIEW_ZONE_RESIDENTIAL_RICH,
	PREVIEW_ZONE_RESIDENTIAL_AVERAGE,
	PREVIEW_ZONE_RESIDENTIAL_POOR,
	PREVIEW_ZONE_GANGLAND,
	PREVIEW_ZONE_BEACH,
	PREVIEW_ZONE_SHOPPING,
	PREVIEW_ZONE_PARK,
	PREVIEW_ZONE_INDUSTRY,
	PREVIEW_ZONE_ENTERTAINMENT_BUSY,
	PREVIEW_ZONE_SHOPPING_BUSY,
	PREVIEW_ZONE_SHOPPING_POSH,
	PREVIEW_ZONE_RESIDENTIAL_RICH_SECLUDED,
	PREVIEW_ZONE_AIRPORT,
	PREVIEW_ZONE_GOLF_CLUB,
	PREVIEW_ZONE_OUT_OF_TOWN_FACTORY,
	PREVIEW_ZONE_AIRPORT_RUNWAY,
};

enum PreviewPedRace
{
	PREVIEW_RACE_DEFAULT = 0,
	PREVIEW_RACE_BLACK = 1,
	PREVIEW_RACE_WHITE = 2,
	PREVIEW_RACE_ORIENTAL = 3,
	PREVIEW_RACE_HISPANIC = 4,
};

enum
{
	PREVIEW_HAS_ROT = 1,
	PREVIEW_HAS_TRANS = 2,
	PREVIEW_HAS_SCALE = 4
};

struct IfpChunkHeader {
	char ident[4];
	uint32 size;
};

struct PreviewAnimKeyFrame {
	rw::Quat rotation;
	float time;
	rw::V3d translation;
};

struct PreviewAnimNode {
	char name[24];
	int nodeId;
	int type;
	std::vector<PreviewAnimKeyFrame> keyFrames;
};

struct PreviewAnimation {
	float duration;
	std::vector<PreviewAnimNode> nodes;
};

struct PreviewAnimGroupDef {
	char groupName[32];
	char ifpFile[32];
	char walkAnim[32];
};

struct PreviewPedDef {
	char modelName[32];
	char txdName[32];
	char animGroup[32];
	uint8 race;
};

struct PreviewVehicleDef {
	char modelName[32];
	char txdName[32];
	char type[16];
	bool heavy;
	std::vector<uint32> colorPairs;
};

struct PreviewPopcycleSlot {
	uint8 maxPeds;
	uint8 maxCars;
	uint8 percDealers;
	uint8 percGang;
	uint8 percCops;
	uint8 percOther;
	uint8 groupPerc[PREVIEW_POP_GROUP_COUNT];
};

struct PreviewPedGroup {
	std::vector<std::string> modelNames;
};

struct PreviewCarGroup {
	std::vector<std::string> modelNames;
};

struct PreviewZoneBox {
	char label[16];
	float minx, miny, minz;
	float maxx, maxy, maxz;
	float area;
	int level;
};

struct PreviewZoneState {
	char label[16];
	int popType;
	uint8 raceMask;
	bool noCops;
	uint8 dealerStrength;
	uint8 gangStrength[10];
};

struct PreviewWalkCycle {
	bool attempted;
	bool loaded;
	char failureReason[128];
	char groupName[32];
	char ifpFile[32];
	char walkAnim[32];
	PreviewAnimation animation;
};

struct PreviewPedAssets {
	bool attempted;
	bool loaded;
	char failureReason[128];
	char modelName[32];
	char txdName[32];
	char animGroup[32];
	int walkCycleIndex;
	rw::TexDictionary *txd;
	rw::Clump *clump;
	std::vector<rw::Matrix> baseLocalMatrices;
	std::vector<PreviewAnimNode> tracksByNode;
	float animationDuration;
};

struct PreviewWalker {
	bool active;
	rw::Clump *clump;
	NodeAddress previous;
	NodeAddress current;
	NodeAddress target;
	uint8 direction;
	float segmentT;
	float speed;
	float animTime;
	uint32 seed;
	int assetIndex;
	rw::V3d fromPos;
	rw::V3d toPos;
};

struct PreviewVehicleAssets {
	bool attempted;
	bool loaded;
	char failureReason[128];
	char modelName[32];
	char txdName[32];
	bool heavy;
	rw::TexDictionary *txd;
	rw::Clump *clump;
	std::vector<uint8> materialColorRoles;
};

struct PreviewTrafficCar {
	bool active;
	bool heavy;
	rw::Clump *clump;
	uint8 colorIndices[4];
	std::vector<uint8> materialColorRoles;
	uint32 stableId;
	NodeAddress previous;
	NodeAddress current;
	NodeAddress target;
	CarPathLinkAddress currentLinkAddr;
	CarPathLinkAddress nextLinkAddr;
	bool currentSameDirection;
	bool nextSameDirection;
	int currentLaneIndex;
	int currentLaneCount;
	int nextLaneIndex;
	int nextLaneCount;
	float distanceOnCurve;
	float speed;
	float preferredSpawnDistance;
	uint32 seed;
	int assetIndex;
	rw::V3d curveP0;
	rw::V3d curveP1;
	rw::V3d curveP2;
	float curveLength;
	float curveSampleDistances[PREVIEW_TRAFFIC_CURVE_SAMPLES + 1];
	bool hasLastRenderedPos;
	rw::V3d lastRenderedPos;
	rw::V3d lastMotionDir;
	bool traverseReversed;
};

struct PreviewParkedCar {
	bool active;
	bool heavy;
	rw::Clump *clump;
	uint8 colorIndices[4];
	std::vector<uint8> materialColorRoles;
	uint32 stableId;
	uint32 seed;
	int assetIndex;
	NodeAddress current;
	NodeAddress target;
	CarPathLinkAddress naviAddr;
	bool sameDirection;
	float sideSign;
	float preferredSpawnDistance;
	rw::V3d pos;
	rw::V3d forward;
};

static bool gPreviewMetadataAttempted;
static bool gPreviewMetadataLoaded;
static char gPreviewMetadataFailureReason[128];
static std::vector<PreviewAnimGroupDef> gPreviewAnimGroups;
static std::vector<PreviewPedDef> gPreviewPedDefs;
static bool gPreviewVehicleMetadataAttempted;
static bool gPreviewVehicleMetadataLoaded;
static char gPreviewVehicleMetadataFailureReason[128];
static bool gPreviewVehicleColorsAttempted;
static bool gPreviewVehicleColorsLoaded;
static char gPreviewVehicleColorsFailureReason[128];
static CRGBA gPreviewVehicleColourTable[128];
static std::vector<PreviewVehicleDef> gPreviewVehicleDefs;
static std::vector<PreviewWalkCycle> gPreviewWalkCycles;
static std::vector<PreviewPedAssets> gPreviewPedAssetCache;
static std::vector<PreviewWalker> gPreviewWalkers;
static bool gPreviewPopulationAttempted;
static bool gPreviewPopulationLoaded;
static char gPreviewPopulationFailureReason[128];
static bool gPreviewVehiclePopulationAttempted;
static bool gPreviewVehiclePopulationLoaded;
static char gPreviewVehiclePopulationFailureReason[128];
static PreviewPopcycleSlot gPreviewPopcycle[PREVIEW_POP_ZONE_COUNT][PREVIEW_POP_DAYSETS][PREVIEW_POP_TIMESLOTS];
static std::vector<PreviewPedGroup> gPreviewPedGroups;
static std::vector<PreviewCarGroup> gPreviewCarGroups;
static std::vector<PreviewZoneBox> gPreviewInfoZones;
static std::vector<PreviewZoneBox> gPreviewMapZones;
static std::vector<PreviewZoneState> gPreviewZoneStates;
static std::vector<PreviewVehicleAssets> gPreviewVehicleAssetCache;
static std::vector<PreviewTrafficCar> gPreviewTrafficCars;
static std::vector<PreviewParkedCar> gPreviewParkedCars;
static uint32 gPreviewTrafficLastSeenChangeSeq;
static bool gPreviewTrafficLastFreezeRoutes;
static float gPreviewTrafficLightTimeMs;
static uint32 gPreviewParkedLastSeenChangeSeq;

#include "sa_zone_population_init.inc"

static bool loadLogicalPathData(const char *logicalPath, std::vector<uint8> &data);
static float previewSeedFloat01(uint32 &seed);
static void clearPreviewTrafficCars(void);
static void clearPreviewParkedCars(void);
static bool resetPreviewTrafficCar(PreviewTrafficCar &car, uint32 seed);
static bool getRoadCross(uint8 info);
static bool loadPreviewVehicleColorData(void);

static bool
readExact(const uint8 *data, size_t dataSize, size_t *offset, void *buf, size_t size)
{
	if(data == nil || offset == nil || *offset > dataSize || size > dataSize - *offset)
		return false;
	memcpy(buf, data + *offset, size);
	*offset += size;
	return true;
}

static bool
writeExact(FILE *f, const void *buf, size_t size)
{
	return fwrite(buf, 1, size, f) == size;
}

template <class T>
static void
appendBytes(std::vector<uint8> &out, const T &value)
{
	const uint8 *src = (const uint8*)&value;
	out.insert(out.end(), src, src + sizeof(T));
}

template <class T>
static void
appendBytes(std::vector<uint8> &out, const std::vector<T> &values)
{
	if(values.empty())
		return;
	const uint8 *src = (const uint8*)&values[0];
	out.insert(out.end(), src, src + sizeof(T)*values.size());
}

static void
copyPreviewString(char *dst, size_t size, const std::string &value)
{
	if(size == 0)
		return;
	strncpy(dst, value.c_str(), size - 1);
	dst[size - 1] = '\0';
}

static std::string
trimPreviewString(const std::string &value)
{
	size_t start = 0;
	size_t end = value.size();
	while(start < end && std::isspace((unsigned char)value[start]))
		start++;
	while(end > start && std::isspace((unsigned char)value[end - 1]))
		end--;
	return value.substr(start, end - start);
}

static std::string
lowerPreviewString(const std::string &value)
{
	std::string out = value;
	for(size_t i = 0; i < out.size(); i++)
		out[i] = (char)std::tolower((unsigned char)out[i]);
	return out;
}

static bool
previewStringEquals(const std::string &a, const char *b)
{
	return rw::strcmp_ci(a.c_str(), b) == 0;
}

static std::string
stripPreviewLineComment(const std::string &value)
{
	size_t slash = value.find("//");
	size_t hash = value.find('#');
	size_t cut = std::string::npos;
	if(slash != std::string::npos)
		cut = slash;
	if(hash != std::string::npos && (cut == std::string::npos || hash < cut))
		cut = hash;
	return cut == std::string::npos ? value : value.substr(0, cut);
}

static std::vector<std::string>
splitPreviewWhitespace(const std::string &line)
{
	std::vector<std::string> out;
	size_t i = 0;
	while(i < line.size()){
		while(i < line.size() && std::isspace((unsigned char)line[i]))
			i++;
		size_t start = i;
		while(i < line.size() && !std::isspace((unsigned char)line[i]))
			i++;
		if(i > start)
			out.push_back(line.substr(start, i - start));
	}
	return out;
}

static bool
parsePreviewInt(const std::string &value, int *out)
{
	char *end = nil;
	long parsed = std::strtol(value.c_str(), &end, 10);
	if(end == value.c_str() || *end != '\0')
		return false;
	*out = (int)parsed;
	return true;
}

static bool
parsePreviewFloat(const std::string &value, float *out)
{
	char *end = nil;
	float parsed = std::strtof(value.c_str(), &end);
	if(end == value.c_str() || *end != '\0')
		return false;
	*out = parsed;
	return true;
}

static uint8
previewRaceMaskFromRace(int race)
{
	return race >= PREVIEW_RACE_BLACK && race <= PREVIEW_RACE_HISPANIC ? (uint8)(1u << (race - 1)) : 0x0Fu;
}

static int
findPreviewRaceFromModelName(const char *modelName)
{
	for(int i = 0; i < 2 && modelName[i]; i++){
		switch(std::toupper((unsigned char)modelName[i])){
		case 'B': return PREVIEW_RACE_BLACK;
		case 'H': return PREVIEW_RACE_HISPANIC;
		case 'O':
		case 'I': return PREVIEW_RACE_ORIENTAL;
		case 'W': return PREVIEW_RACE_WHITE;
		}
	}
	return PREVIEW_RACE_DEFAULT;
}

static std::vector<std::string>
splitPreviewCsv(const std::string &line)
{
	std::vector<std::string> out;
	size_t start = 0;
	while(start <= line.size()){
		size_t comma = line.find(',', start);
		if(comma == std::string::npos)
			comma = line.size();
		out.push_back(trimPreviewString(line.substr(start, comma - start)));
		start = comma + 1;
		if(comma == line.size())
			break;
	}
	return out;
}

static std::vector<int>
extractPreviewInts(const std::string &line)
{
	std::vector<int> out;
	int current = 0;
	bool inNumber = false;
	for(size_t i = 0; i < line.size(); i++){
		unsigned char ch = (unsigned char)line[i];
		if(std::isdigit(ch)){
			current = current*10 + (ch - '0');
			inNumber = true;
		}else if(inNumber){
			out.push_back(current);
			current = 0;
			inNumber = false;
		}
	}
	if(inNumber)
		out.push_back(current);
	return out;
}

static int
parsePreviewZonePopulationTypeName(const std::string &value)
{
	std::string key = lowerPreviewString(value);
	if(key == "business") return PREVIEW_ZONE_BUSINESS;
	if(key == "desert") return PREVIEW_ZONE_DESERT;
	if(key == "entertainment") return PREVIEW_ZONE_ENTERTAINMENT;
	if(key == "countryside") return PREVIEW_ZONE_COUNTRYSIDE;
	if(key == "residential_rich") return PREVIEW_ZONE_RESIDENTIAL_RICH;
	if(key == "residential_average") return PREVIEW_ZONE_RESIDENTIAL_AVERAGE;
	if(key == "residential_poor") return PREVIEW_ZONE_RESIDENTIAL_POOR;
	if(key == "gangland") return PREVIEW_ZONE_GANGLAND;
	if(key == "beach") return PREVIEW_ZONE_BEACH;
	if(key == "shopping") return PREVIEW_ZONE_SHOPPING;
	if(key == "park") return PREVIEW_ZONE_PARK;
	if(key == "industrial" || key == "industry") return PREVIEW_ZONE_INDUSTRY;
	if(key == "entertainment_busy") return PREVIEW_ZONE_ENTERTAINMENT_BUSY;
	if(key == "shopping_busy") return PREVIEW_ZONE_SHOPPING_BUSY;
	if(key == "shopping_posh") return PREVIEW_ZONE_SHOPPING_POSH;
	if(key == "residential_rich_secluded") return PREVIEW_ZONE_RESIDENTIAL_RICH_SECLUDED;
	if(key == "airport") return PREVIEW_ZONE_AIRPORT;
	if(key == "golf_club") return PREVIEW_ZONE_GOLF_CLUB;
	if(key == "out_of_town_factory") return PREVIEW_ZONE_OUT_OF_TOWN_FACTORY;
	if(key == "airport_runway") return PREVIEW_ZONE_AIRPORT_RUNWAY;
	return -1;
}

static int
parsePreviewScmZonePopulationType(const std::string &value)
{
	std::string key = lowerPreviewString(value);
	if(key.find("popcycle_zone_") == 0)
		key.erase(0, strlen("popcycle_zone_"));
	return parsePreviewZonePopulationTypeName(key);
}

static uint8
parsePreviewScmRaceMask(const std::string &value)
{
	std::string key = lowerPreviewString(value);
	if(key.find("poprace_") == 0)
		key.erase(0, strlen("poprace_"));
	uint8 mask = 0;
	for(size_t i = 0; i < key.size(); i++){
		switch(std::toupper((unsigned char)key[i])){
		case 'B': mask |= 1u << (PREVIEW_RACE_BLACK - 1); break;
		case 'W': mask |= 1u << (PREVIEW_RACE_WHITE - 1); break;
		case 'O':
		case 'I': mask |= 1u << (PREVIEW_RACE_ORIENTAL - 1); break;
		case 'H': mask |= 1u << (PREVIEW_RACE_HISPANIC - 1); break;
		}
	}
	return mask ? mask : 0x0Fu;
}

static int
parsePreviewScmGangId(const std::string &value)
{
	std::string key = lowerPreviewString(value);
	if(key == "gang_flat") return 0;
	if(key == "gang_grove") return 1;
	if(key == "gang_nmex") return 2;
	if(key == "gang_sfmex") return 3;
	if(key == "gang_viet") return 4;
	if(key == "gang_mafia") return 5;
	if(key == "gang_triad") return 6;
	if(key == "gang_smex") return 7;
	return -1;
}

static void
appendPreviewFallbackData(void)
{
	bool haveManGroup = false;
	for(size_t i = 0; i < gPreviewAnimGroups.size(); i++)
		if(rw::strcmp_ci(gPreviewAnimGroups[i].groupName, "man") == 0)
			haveManGroup = true;
	if(!haveManGroup){
		PreviewAnimGroupDef group = {};
		copyPreviewString(group.groupName, sizeof(group.groupName), "man");
		copyPreviewString(group.ifpFile, sizeof(group.ifpFile), "ped");
		copyPreviewString(group.walkAnim, sizeof(group.walkAnim), "walk_civi");
		gPreviewAnimGroups.push_back(group);
	}

	bool haveMale01 = false;
	for(size_t i = 0; i < gPreviewPedDefs.size(); i++)
		if(rw::strcmp_ci(gPreviewPedDefs[i].modelName, "male01") == 0)
			haveMale01 = true;
	if(!haveMale01){
		PreviewPedDef ped = {};
		copyPreviewString(ped.modelName, sizeof(ped.modelName), "male01");
		copyPreviewString(ped.txdName, sizeof(ped.txdName), "male01");
		copyPreviewString(ped.animGroup, sizeof(ped.animGroup), "man");
		ped.race = PREVIEW_RACE_DEFAULT;
		gPreviewPedDefs.push_back(ped);
	}
}

static bool
loadPreviewMetadata(void)
{
	if(gPreviewMetadataAttempted)
		return gPreviewMetadataLoaded;

	gPreviewMetadataAttempted = true;
	gPreviewMetadataLoaded = false;
	gPreviewMetadataFailureReason[0] = '\0';
	gPreviewAnimGroups.clear();
	gPreviewPedDefs.clear();

	std::vector<uint8> animgrpData;
	if(!loadLogicalPathData("data/animgrp.dat", animgrpData)){
		snprintf(gPreviewMetadataFailureReason, sizeof(gPreviewMetadataFailureReason), "missing_animgrp_dat");
		appendPreviewFallbackData();
		return false;
	}

	std::string animgrpText((const char*)&animgrpData[0], animgrpData.size());
	bool readingGroup = false;
	bool keepGroup = false;
	PreviewAnimGroupDef currentGroup = {};
	for(size_t start = 0; start <= animgrpText.size(); ){
		size_t end = animgrpText.find('\n', start);
		if(end == std::string::npos)
			end = animgrpText.size();
		std::string line = animgrpText.substr(start, end - start);
		if(!line.empty() && line[line.size() - 1] == '\r')
			line.resize(line.size() - 1);
		size_t comment = line.find('#');
		if(comment != std::string::npos)
			line.erase(comment);
		line = trimPreviewString(line);
		start = end + 1;
		if(line.empty())
			continue;

		if(!readingGroup){
			std::vector<std::string> parts = splitPreviewCsv(line);
			if(parts.size() < 4)
				continue;
			readingGroup = true;
			keepGroup = rw::strcmp_ci(parts[2].c_str(), "walkcycle") == 0;
			currentGroup = {};
			if(keepGroup){
				copyPreviewString(currentGroup.groupName, sizeof(currentGroup.groupName), lowerPreviewString(parts[0]));
				copyPreviewString(currentGroup.ifpFile, sizeof(currentGroup.ifpFile), lowerPreviewString(parts[1]));
			}
			continue;
		}

		if(rw::strcmp_ci(line.c_str(), "end") == 0){
			if(keepGroup && currentGroup.groupName[0] && currentGroup.walkAnim[0])
				gPreviewAnimGroups.push_back(currentGroup);
			readingGroup = false;
			keepGroup = false;
			currentGroup = {};
			continue;
		}

		if(keepGroup && currentGroup.walkAnim[0] == '\0')
			copyPreviewString(currentGroup.walkAnim, sizeof(currentGroup.walkAnim), lowerPreviewString(line));
	}

	std::vector<uint8> pedsData;
	if(!loadLogicalPathData("data/peds.ide", pedsData)){
		snprintf(gPreviewMetadataFailureReason, sizeof(gPreviewMetadataFailureReason), "missing_peds_ide");
		appendPreviewFallbackData();
		return false;
	}

	std::string pedsText((const char*)&pedsData[0], pedsData.size());
	bool inPedsBlock = false;
	for(size_t start = 0; start <= pedsText.size(); ){
		size_t end = pedsText.find('\n', start);
		if(end == std::string::npos)
			end = pedsText.size();
		std::string line = pedsText.substr(start, end - start);
		if(!line.empty() && line[line.size() - 1] == '\r')
			line.resize(line.size() - 1);
		size_t comment = line.find('#');
		if(comment != std::string::npos)
			line.erase(comment);
		line = trimPreviewString(line);
		start = end + 1;
		if(line.empty())
			continue;

		if(!inPedsBlock){
			if(rw::strcmp_ci(line.c_str(), "peds") == 0)
				inPedsBlock = true;
			continue;
		}
		if(rw::strcmp_ci(line.c_str(), "end") == 0)
			break;

		std::vector<std::string> parts = splitPreviewCsv(line);
		if(parts.size() < 6)
			continue;

		std::string modelName = lowerPreviewString(parts[1]);
		std::string txdName = lowerPreviewString(parts[2]);
		std::string animGroup = lowerPreviewString(parts[5]);
		if(modelName.empty() || txdName.empty() || animGroup.empty())
			continue;
		if(modelName == "null" || txdName == "null")
			continue;

		bool duplicate = false;
		for(size_t i = 0; i < gPreviewPedDefs.size(); i++)
			if(rw::strcmp_ci(gPreviewPedDefs[i].modelName, modelName.c_str()) == 0 &&
			   rw::strcmp_ci(gPreviewPedDefs[i].animGroup, animGroup.c_str()) == 0)
				duplicate = true;
		if(duplicate)
			continue;

		PreviewPedDef ped = {};
		copyPreviewString(ped.modelName, sizeof(ped.modelName), modelName);
		copyPreviewString(ped.txdName, sizeof(ped.txdName), txdName);
		copyPreviewString(ped.animGroup, sizeof(ped.animGroup), animGroup);
		ped.race = (uint8)findPreviewRaceFromModelName(modelName.c_str());
		gPreviewPedDefs.push_back(ped);
	}

	appendPreviewFallbackData();
	gPreviewMetadataLoaded = !gPreviewAnimGroups.empty() && !gPreviewPedDefs.empty();
	if(!gPreviewMetadataLoaded)
		snprintf(gPreviewMetadataFailureReason, sizeof(gPreviewMetadataFailureReason), "empty_preview_metadata");
	log("SAPaths: loaded preview metadata (%d ped defs, %d anim groups)\n",
		(int)gPreviewPedDefs.size(), (int)gPreviewAnimGroups.size());
	return gPreviewMetadataLoaded;
}

static bool
isPreviewSupportedVehicleType(const std::string &type)
{
	return type == "car" || type == "mtruck";
}

static int
findPreviewVehicleDefIndexByModelName(const char *modelName)
{
	for(size_t i = 0; i < gPreviewVehicleDefs.size(); i++)
		if(rw::strcmp_ci(gPreviewVehicleDefs[i].modelName, modelName) == 0)
			return (int)i;
	return -1;
}

static bool
loadPreviewVehicleMetadata(void)
{
	if(gPreviewVehicleMetadataAttempted)
		return gPreviewVehicleMetadataLoaded;

	gPreviewVehicleMetadataAttempted = true;
	gPreviewVehicleMetadataLoaded = false;
	gPreviewVehicleMetadataFailureReason[0] = '\0';
	gPreviewVehicleDefs.clear();

	std::vector<uint8> vehiclesData;
	if(!loadLogicalPathData("data/vehicles.ide", vehiclesData)){
		snprintf(gPreviewVehicleMetadataFailureReason, sizeof(gPreviewVehicleMetadataFailureReason), "missing_vehicles_ide");
		return false;
	}

	std::string vehiclesText((const char*)&vehiclesData[0], vehiclesData.size());
	bool inCarsBlock = false;
	for(size_t start = 0; start <= vehiclesText.size(); ){
		size_t end = vehiclesText.find('\n', start);
		if(end == std::string::npos)
			end = vehiclesText.size();
		std::string line = vehiclesText.substr(start, end - start);
		if(!line.empty() && line[line.size() - 1] == '\r')
			line.resize(line.size() - 1);
		size_t comment = line.find('#');
		if(comment != std::string::npos)
			line.erase(comment);
		line = trimPreviewString(line);
		start = end + 1;
		if(line.empty())
			continue;

		if(!inCarsBlock){
			if(rw::strcmp_ci(line.c_str(), "cars") == 0)
				inCarsBlock = true;
			continue;
		}
		if(rw::strcmp_ci(line.c_str(), "end") == 0)
			break;

		std::vector<std::string> parts = splitPreviewCsv(line);
		if(parts.size() < 4)
			continue;

		std::string modelName = lowerPreviewString(parts[1]);
		std::string txdName = lowerPreviewString(parts[2]);
		std::string type = lowerPreviewString(parts[3]);
		if(modelName.empty() || txdName.empty() || !isPreviewSupportedVehicleType(type))
			continue;
		if(findPreviewVehicleDefIndexByModelName(modelName.c_str()) >= 0)
			continue;

		PreviewVehicleDef vehicle = {};
		copyPreviewString(vehicle.modelName, sizeof(vehicle.modelName), modelName);
		copyPreviewString(vehicle.txdName, sizeof(vehicle.txdName), txdName);
		copyPreviewString(vehicle.type, sizeof(vehicle.type), type);
		vehicle.heavy = type == "mtruck";
		gPreviewVehicleDefs.push_back(vehicle);
	}

	gPreviewVehicleMetadataLoaded = !gPreviewVehicleDefs.empty();
	if(!gPreviewVehicleMetadataLoaded)
		snprintf(gPreviewVehicleMetadataFailureReason, sizeof(gPreviewVehicleMetadataFailureReason), "empty_vehicle_preview_metadata");
	else
		log("SAPaths: loaded preview vehicle metadata (%d supported vehicle defs)\n",
			(int)gPreviewVehicleDefs.size());
	return gPreviewVehicleMetadataLoaded;
}

static bool
loadPreviewVehicleColorData(void)
{
	if(gPreviewVehicleColorsAttempted)
		return gPreviewVehicleColorsLoaded;

	gPreviewVehicleColorsAttempted = true;
	gPreviewVehicleColorsLoaded = false;
	gPreviewVehicleColorsFailureReason[0] = '\0';
	for(int i = 0; i < 128; i++)
		gPreviewVehicleColourTable[i] = { 245, 245, 245, 255 };
	if(!loadPreviewVehicleMetadata() || gPreviewVehicleDefs.empty()){
		snprintf(gPreviewVehicleColorsFailureReason, sizeof(gPreviewVehicleColorsFailureReason), "missing_vehicle_metadata");
		return false;
	}
	for(size_t i = 0; i < gPreviewVehicleDefs.size(); i++)
		gPreviewVehicleDefs[i].colorPairs.clear();

	std::vector<uint8> carcolsData;
	if(!loadLogicalPathData("data/carcols.dat", carcolsData)){
		snprintf(gPreviewVehicleColorsFailureReason, sizeof(gPreviewVehicleColorsFailureReason), "missing_carcols_dat");
		return false;
	}

	enum {
		PREVIEW_CARCOLS_NONE,
		PREVIEW_CARCOLS_COL,
		PREVIEW_CARCOLS_CAR
	} mode = PREVIEW_CARCOLS_NONE;
	std::string carcolsText((const char*)&carcolsData[0], carcolsData.size());
	int paletteIndex = 0;
	int vehiclesWithColors = 0;
	for(size_t start = 0; start <= carcolsText.size(); ){
		size_t end = carcolsText.find('\n', start);
		if(end == std::string::npos)
			end = carcolsText.size();
		std::string line = carcolsText.substr(start, end - start);
		if(!line.empty() && line[line.size() - 1] == '\r')
			line.resize(line.size() - 1);
		line = trimPreviewString(stripPreviewLineComment(line));
		start = end + 1;
		if(line.empty())
			continue;

		if(rw::strcmp_ci(line.c_str(), "col") == 0){
			mode = PREVIEW_CARCOLS_COL;
			continue;
		}
		if(rw::strcmp_ci(line.c_str(), "car") == 0){
			mode = PREVIEW_CARCOLS_CAR;
			continue;
		}
		if(rw::strcmp_ci(line.c_str(), "end") == 0){
			mode = PREVIEW_CARCOLS_NONE;
			continue;
		}

		if(mode == PREVIEW_CARCOLS_COL){
			std::vector<int> numbers = extractPreviewInts(line);
			if(numbers.size() < 3 || paletteIndex >= 128)
				continue;
			gPreviewVehicleColourTable[paletteIndex++] = {
				(uint8)clamp(numbers[0], 0, 255),
				(uint8)clamp(numbers[1], 0, 255),
				(uint8)clamp(numbers[2], 0, 255),
				255
			};
			continue;
		}

		if(mode == PREVIEW_CARCOLS_CAR){
			size_t comma = line.find(',');
			if(comma == std::string::npos)
				continue;
			std::string modelName = lowerPreviewString(trimPreviewString(line.substr(0, comma)));
			if(modelName.empty())
				continue;
			int vehicleDefIndex = findPreviewVehicleDefIndexByModelName(modelName.c_str());
			if(vehicleDefIndex < 0)
				continue;
			std::vector<int> numbers = extractPreviewInts(line.substr(comma + 1));
			PreviewVehicleDef &vehicleDef = gPreviewVehicleDefs[vehicleDefIndex];
			size_t previousCount = vehicleDef.colorPairs.size();
			for(size_t i = 0; i + 1 < numbers.size(); i += 2){
				uint8 primary = (uint8)clamp(numbers[i], 0, 127);
				uint8 secondary = (uint8)clamp(numbers[i + 1], 0, 127);
				uint32 packed = (uint32)primary |
					((uint32)secondary << 8) |
					((uint32)primary << 16) |
					((uint32)secondary << 24);
				vehicleDef.colorPairs.push_back(packed);
			}
			if(previousCount == 0 && !vehicleDef.colorPairs.empty())
				vehiclesWithColors++;
		}
	}

	gPreviewVehicleColorsLoaded = paletteIndex > 0;
	if(!gPreviewVehicleColorsLoaded)
		snprintf(gPreviewVehicleColorsFailureReason, sizeof(gPreviewVehicleColorsFailureReason), "empty_vehicle_color_data");
	else
		log("SAPaths: loaded preview vehicle colors (%d palette entries, %d vehicle models)\n",
			paletteIndex, vehiclesWithColors);
	return gPreviewVehicleColorsLoaded;
}

static PreviewZoneState*
findPreviewZoneState(const char *label)
{
	for(size_t i = 0; i < gPreviewZoneStates.size(); i++)
		if(rw::strcmp_ci(gPreviewZoneStates[i].label, label) == 0)
			return &gPreviewZoneStates[i];
	return nil;
}

static int
findPreviewPedDefIndexByModelName(const char *modelName)
{
	for(size_t i = 0; i < gPreviewPedDefs.size(); i++)
		if(rw::strcmp_ci(gPreviewPedDefs[i].modelName, modelName) == 0)
			return (int)i;
	return -1;
}

static bool
loadPreviewPopulationData(void)
{
	if(gPreviewPopulationAttempted)
		return gPreviewPopulationLoaded;

	gPreviewPopulationAttempted = true;
	gPreviewPopulationLoaded = false;
	gPreviewPopulationFailureReason[0] = '\0';
	memset(gPreviewPopcycle, 0, sizeof(gPreviewPopcycle));
	gPreviewPedGroups.clear();
	gPreviewInfoZones.clear();
	gPreviewMapZones.clear();
	gPreviewZoneStates.clear();

	std::vector<uint8> popcycleData;
	if(!loadLogicalPathData("data/popcycle.dat", popcycleData)){
		snprintf(gPreviewPopulationFailureReason, sizeof(gPreviewPopulationFailureReason), "missing_popcycle_dat");
		return false;
	}

	std::string popcycleText((const char*)&popcycleData[0], popcycleData.size());
	int currentZoneType = -1;
	int currentDayset = -1;
	int currentSlot = 0;
	for(size_t start = 0; start <= popcycleText.size(); ){
		size_t end = popcycleText.find('\n', start);
		if(end == std::string::npos)
			end = popcycleText.size();
		std::string rawLine = popcycleText.substr(start, end - start);
		if(!rawLine.empty() && rawLine[rawLine.size() - 1] == '\r')
			rawLine.resize(rawLine.size() - 1);
		std::string trimmed = trimPreviewString(rawLine);
		start = end + 1;
		if(trimmed.empty())
			continue;

		if(trimmed.find("//") == 0){
			std::string comment = trimPreviewString(trimmed.substr(2));
			if(comment == "Weekday"){
				currentDayset = 0;
				currentSlot = 0;
				continue;
			}
			if(comment == "Weekend"){
				currentDayset = 1;
				currentSlot = 0;
				continue;
			}
			if(!comment.empty() && std::isupper((unsigned char)comment[0])){
				size_t dash = comment.find('-');
				if(dash != std::string::npos)
					comment = trimPreviewString(comment.substr(0, dash));
				currentZoneType = parsePreviewZonePopulationTypeName(lowerPreviewString(comment));
			}
			continue;
		}

		std::string line = trimPreviewString(stripPreviewLineComment(rawLine));
		if(line.empty() || currentZoneType < 0 || currentDayset < 0 || currentSlot >= PREVIEW_POP_TIMESLOTS)
			continue;

		std::vector<std::string> parts = splitPreviewWhitespace(line);
		if(parts.size() < 6 + PREVIEW_POP_GROUP_COUNT)
			continue;

		PreviewPopcycleSlot &slot = gPreviewPopcycle[currentZoneType][currentDayset][currentSlot++];
		slot.maxPeds = (uint8)clamp(std::atoi(parts[0].c_str()), 0, 255);
		slot.maxCars = (uint8)clamp(std::atoi(parts[1].c_str()), 0, 255);
		slot.percDealers = (uint8)clamp(std::atoi(parts[2].c_str()), 0, 255);
		slot.percGang = (uint8)clamp(std::atoi(parts[3].c_str()), 0, 255);
		slot.percCops = (uint8)clamp(std::atoi(parts[4].c_str()), 0, 255);
		slot.percOther = (uint8)clamp(std::atoi(parts[5].c_str()), 0, 255);
		for(int i = 0; i < PREVIEW_POP_GROUP_COUNT; i++)
			slot.groupPerc[i] = (uint8)clamp(std::atoi(parts[6 + i].c_str()), 0, 255);
	}

	std::vector<uint8> pedgrpData;
	if(!loadLogicalPathData("data/pedgrp.dat", pedgrpData)){
		snprintf(gPreviewPopulationFailureReason, sizeof(gPreviewPopulationFailureReason), "missing_pedgrp_dat");
		return false;
	}

	std::string pedgrpText((const char*)&pedgrpData[0], pedgrpData.size());
	for(size_t start = 0; start <= pedgrpText.size(); ){
		size_t end = pedgrpText.find('\n', start);
		if(end == std::string::npos)
			end = pedgrpText.size();
		std::string rawLine = pedgrpText.substr(start, end - start);
		if(!rawLine.empty() && rawLine[rawLine.size() - 1] == '\r')
			rawLine.resize(rawLine.size() - 1);
		start = end + 1;

		std::string line = trimPreviewString(stripPreviewLineComment(rawLine));
		if(line.empty())
			continue;

		PreviewPedGroup group;
		std::vector<std::string> parts = splitPreviewCsv(line);
		for(size_t i = 0; i < parts.size(); i++){
			std::string model = lowerPreviewString(parts[i]);
			if(model.empty())
				continue;
			group.modelNames.push_back(model);
		}
		if(!group.modelNames.empty())
			gPreviewPedGroups.push_back(group);
	}

	std::vector<uint8> infoZonData;
	if(!loadLogicalPathData("data/info.zon", infoZonData)){
		snprintf(gPreviewPopulationFailureReason, sizeof(gPreviewPopulationFailureReason), "missing_info_zon");
		return false;
	}

	std::string infoZonText((const char*)&infoZonData[0], infoZonData.size());
	for(size_t start = 0; start <= infoZonText.size(); ){
		size_t end = infoZonText.find('\n', start);
		if(end == std::string::npos)
			end = infoZonText.size();
		std::string rawLine = infoZonText.substr(start, end - start);
		if(!rawLine.empty() && rawLine[rawLine.size() - 1] == '\r')
			rawLine.resize(rawLine.size() - 1);
		start = end + 1;

		std::string line = trimPreviewString(stripPreviewLineComment(rawLine));
		if(line.empty() || rw::strcmp_ci(line.c_str(), "zone") == 0 || rw::strcmp_ci(line.c_str(), "end") == 0)
			continue;

		std::vector<std::string> parts = splitPreviewCsv(line);
		if(parts.size() < 10)
			continue;

		PreviewZoneBox zone = {};
		copyPreviewString(zone.label, sizeof(zone.label), parts[0]);
		zone.minx = std::strtof(parts[2].c_str(), nil);
		zone.miny = std::strtof(parts[3].c_str(), nil);
		zone.minz = std::strtof(parts[4].c_str(), nil);
		zone.maxx = std::strtof(parts[5].c_str(), nil);
		zone.maxy = std::strtof(parts[6].c_str(), nil);
		zone.maxz = std::strtof(parts[7].c_str(), nil);
		zone.area = (zone.maxx - zone.minx) * (zone.maxy - zone.miny) * max(1.0f, zone.maxz - zone.minz);
		zone.level = std::atoi(parts[8].c_str());
		gPreviewInfoZones.push_back(zone);

		PreviewZoneState state = {};
		copyPreviewString(state.label, sizeof(state.label), parts[0]);
		state.popType = PREVIEW_ZONE_RESIDENTIAL_AVERAGE;
		state.raceMask = 0x0Fu;
		state.noCops = false;
		state.dealerStrength = 0;
		memset(state.gangStrength, 0, sizeof(state.gangStrength));
		gPreviewZoneStates.push_back(state);
	}

	std::vector<uint8> mapZonData;
	if(loadLogicalPathData("data/map.zon", mapZonData)){
		std::string mapZonText((const char*)&mapZonData[0], mapZonData.size());
		for(size_t start = 0; start <= mapZonText.size(); ){
			size_t end = mapZonText.find('\n', start);
			if(end == std::string::npos)
				end = mapZonText.size();
			std::string rawLine = mapZonText.substr(start, end - start);
			if(!rawLine.empty() && rawLine[rawLine.size() - 1] == '\r')
				rawLine.resize(rawLine.size() - 1);
			start = end + 1;

			std::string line = trimPreviewString(stripPreviewLineComment(rawLine));
			if(line.empty() || rw::strcmp_ci(line.c_str(), "zone") == 0 || rw::strcmp_ci(line.c_str(), "end") == 0)
				continue;

			std::vector<std::string> parts = splitPreviewCsv(line);
			if(parts.size() < 10)
				continue;

			PreviewZoneBox zone = {};
			copyPreviewString(zone.label, sizeof(zone.label), parts[0]);
			zone.minx = std::strtof(parts[2].c_str(), nil);
			zone.miny = std::strtof(parts[3].c_str(), nil);
			zone.minz = std::strtof(parts[4].c_str(), nil);
			zone.maxx = std::strtof(parts[5].c_str(), nil);
			zone.maxy = std::strtof(parts[6].c_str(), nil);
			zone.maxz = std::strtof(parts[7].c_str(), nil);
			zone.area = (zone.maxx - zone.minx) * (zone.maxy - zone.miny) * max(1.0f, zone.maxz - zone.minz);
			zone.level = std::atoi(parts[8].c_str());
			gPreviewMapZones.push_back(zone);
		}
	}

	{
		std::string script(gPreviewZonePopulationInitScript ? gPreviewZonePopulationInitScript : "");
		for(size_t start = 0; start <= script.size(); ){
			size_t end = script.find('\n', start);
			if(end == std::string::npos)
				end = script.size();
			std::string rawLine = script.substr(start, end - start);
			start = end + 1;

			std::string line = trimPreviewString(stripPreviewLineComment(rawLine));
			if(line.empty())
				continue;
			std::vector<std::string> parts = splitPreviewWhitespace(line);
			if(parts.size() < 3)
				continue;

			PreviewZoneState *state = findPreviewZoneState(parts[1].c_str());
			if(state == nil)
				continue;

			if(parts[0] == "SET_ZONE_POPULATION_TYPE"){
				int popType = parsePreviewScmZonePopulationType(parts[2]);
				if(popType >= 0)
					state->popType = popType;
			}else if(parts[0] == "SET_ZONE_POPULATION_RACE"){
				state->raceMask = parsePreviewScmRaceMask(parts[2]);
			}else if(parts[0] == "SET_ZONE_GANG_STRENGTH" && parts.size() >= 4){
				int gang = parsePreviewScmGangId(parts[2]);
				if(gang >= 0 && gang < 10)
					state->gangStrength[gang] = (uint8)clamp(std::atoi(parts[3].c_str()), 0, 255);
			}else if(parts[0] == "SET_ZONE_DEALER_STRENGTH" && parts.size() >= 3){
				state->dealerStrength = (uint8)clamp(std::atoi(parts[2].c_str()), 0, 255);
			}else if(parts[0] == "SET_ZONE_NO_COPS" && parts.size() >= 3){
				state->noCops = rw::strcmp_ci(parts[2].c_str(), "TRUE") == 0;
			}
		}
	}

	gPreviewPopulationLoaded =
		!gPreviewPedGroups.empty() &&
		!gPreviewInfoZones.empty() &&
		gPreviewPedGroups.size() >= 57;
	if(!gPreviewPopulationLoaded)
		snprintf(gPreviewPopulationFailureReason, sizeof(gPreviewPopulationFailureReason), "incomplete_population_data");
	else
		log("SAPaths: loaded preview population data (%d ped groups, %d info zones)\n",
			(int)gPreviewPedGroups.size(), (int)gPreviewInfoZones.size());
	return gPreviewPopulationLoaded;
}

static bool
loadPreviewVehiclePopulationData(void)
{
	if(gPreviewVehiclePopulationAttempted)
		return gPreviewVehiclePopulationLoaded;

	gPreviewVehiclePopulationAttempted = true;
	gPreviewVehiclePopulationLoaded = false;
	gPreviewVehiclePopulationFailureReason[0] = '\0';
	gPreviewCarGroups.clear();

	if(!loadPreviewPopulationData()){
		snprintf(gPreviewVehiclePopulationFailureReason, sizeof(gPreviewVehiclePopulationFailureReason), "missing_base_population_data");
		return false;
	}

	std::vector<uint8> cargrpData;
	if(!loadLogicalPathData("data/cargrp.dat", cargrpData)){
		snprintf(gPreviewVehiclePopulationFailureReason, sizeof(gPreviewVehiclePopulationFailureReason), "missing_cargrp_dat");
		return false;
	}

	std::string cargrpText((const char*)&cargrpData[0], cargrpData.size());
	for(size_t start = 0; start <= cargrpText.size(); ){
		size_t end = cargrpText.find('\n', start);
		if(end == std::string::npos)
			end = cargrpText.size();
		std::string rawLine = cargrpText.substr(start, end - start);
		if(!rawLine.empty() && rawLine[rawLine.size() - 1] == '\r')
			rawLine.resize(rawLine.size() - 1);
		start = end + 1;

		std::string line = trimPreviewString(stripPreviewLineComment(rawLine));
		if(line.empty())
			continue;

		PreviewCarGroup group;
		std::vector<std::string> parts = splitPreviewCsv(line);
		for(size_t i = 0; i < parts.size(); i++){
			std::string model = lowerPreviewString(parts[i]);
			if(model.empty())
				continue;
			group.modelNames.push_back(model);
		}
		if(!group.modelNames.empty())
			gPreviewCarGroups.push_back(group);
	}

	gPreviewVehiclePopulationLoaded = gPreviewCarGroups.size() >= 29;
	if(!gPreviewVehiclePopulationLoaded)
		snprintf(gPreviewVehiclePopulationFailureReason, sizeof(gPreviewVehiclePopulationFailureReason), "incomplete_cargrp_data");
	else
		log("SAPaths: loaded preview vehicle population data (%d car groups)\n",
			(int)gPreviewCarGroups.size());
	return gPreviewVehiclePopulationLoaded;
}

static int
findPreviewAnimGroupIndex(const char *groupName)
{
	for(size_t i = 0; i < gPreviewAnimGroups.size(); i++)
		if(rw::strcmp_ci(gPreviewAnimGroups[i].groupName, groupName) == 0)
			return (int)i;
	return -1;
}

static int
findPreviewWalkCycleIndex(const char *groupName)
{
	for(size_t i = 0; i < gPreviewWalkCycles.size(); i++)
		if(rw::strcmp_ci(gPreviewWalkCycles[i].groupName, groupName) == 0)
			return (int)i;
	return -1;
}

static int
findPreviewPedAssetIndex(const char *modelName, const char *txdName, const char *animGroup)
{
	for(size_t i = 0; i < gPreviewPedAssetCache.size(); i++)
		if(rw::strcmp_ci(gPreviewPedAssetCache[i].modelName, modelName) == 0 &&
		   rw::strcmp_ci(gPreviewPedAssetCache[i].txdName, txdName) == 0 &&
		   rw::strcmp_ci(gPreviewPedAssetCache[i].animGroup, animGroup) == 0)
			return (int)i;
	return -1;
}

static int
findPreviewVehicleAssetIndex(const char *modelName, const char *txdName)
{
	for(size_t i = 0; i < gPreviewVehicleAssetCache.size(); i++)
		if(rw::strcmp_ci(gPreviewVehicleAssetCache[i].modelName, modelName) == 0 &&
		   rw::strcmp_ci(gPreviewVehicleAssetCache[i].txdName, txdName) == 0)
			return (int)i;
	return -1;
}

static rw::TexDictionary*
loadPreviewTextureDictionary(const char *logicalPath)
{
	std::vector<uint8> txdData;
	if(!loadLogicalPathData(logicalPath, txdData))
		return nil;

	rw::StreamMemory txdStream;
	txdStream.open(&txdData[0], txdData.size());
	rw::TexDictionary *txd = nil;
	if(findChunk(&txdStream, rw::ID_TEXDICTIONARY, nil, nil)){
		txd = rw::TexDictionary::streamRead(&txdStream);
		if(txd)
			ConvertTxd(txd);
	}
	txdStream.close();
	return txd;
}

static void
mergePreviewTextureDictionary(rw::TexDictionary *dst, rw::TexDictionary *src)
{
	if(dst == nil || src == nil)
		return;
	FORLIST(lnk, src->textures)
		dst->addFront(rw::Texture::fromDict(lnk));
}

static rw::TexDictionary*
loadPreviewVehicleTextureDictionary(const char *txdName)
{
	char txdPath[128];
	if(snprintf(txdPath, sizeof(txdPath), "models/gta3.img/%s.txd", txdName) >= (int)sizeof(txdPath))
		return nil;

	rw::TexDictionary *sharedTxd = loadPreviewTextureDictionary("models/generic/vehicle.txd");
	rw::TexDictionary *modelTxd = loadPreviewTextureDictionary(txdPath);
	if(sharedTxd == nil && modelTxd == nil)
		return nil;

	rw::TexDictionary *combined = rw::TexDictionary::create();
	if(sharedTxd){
		mergePreviewTextureDictionary(combined, sharedTxd);
		sharedTxd->destroy();
	}
	if(modelTxd){
		mergePreviewTextureDictionary(combined, modelTxd);
		modelTxd->destroy();
	}
	return combined;
}

static const PreviewZoneBox*
findPreviewSmallestZone(const std::vector<PreviewZoneBox> &zones, const rw::V3d &pos)
{
	const PreviewZoneBox *best = nil;
	float bestArea = 0.0f;
	for(size_t i = 0; i < zones.size(); i++){
		const PreviewZoneBox &zone = zones[i];
		if(pos.x < zone.minx || pos.x > zone.maxx ||
		   pos.y < zone.miny || pos.y > zone.maxy ||
		   pos.z < zone.minz || pos.z > zone.maxz)
			continue;
		if(best == nil || zone.area < bestArea){
			best = &zone;
			bestArea = zone.area;
		}
	}
	return best;
}

static int
getPreviewWorldZoneIndexForPosition(const rw::V3d &pos)
{
	const PreviewZoneBox *zone = findPreviewSmallestZone(gPreviewMapZones, pos);
	if(zone == nil)
		return 0;
	switch(zone->level){
	case 2: return 1;
	case 3: return 2;
	default: return 0;
	}
}

static const PreviewZoneState*
getPreviewZoneStateForPosition(const rw::V3d &pos)
{
	const PreviewZoneBox *zone = findPreviewSmallestZone(gPreviewInfoZones, pos);
	return zone ? findPreviewZoneState(zone->label) : nil;
}

static int
getPreviewCurrentWeekendIndex(void)
{
	time_t now = time(nil);
	struct tm localTm;
#if defined(_WIN32)
	localtime_s(&localTm, &now);
#else
	localtime_r(&now, &localTm);
#endif
	switch(localTm.tm_wday){
	case 0: return 1;
	case 1: return currentHour >= 20 ? 1 : 0;
	case 6: return currentHour >= 20 ? 1 : 0;
	default: return 0;
	}
}

static const PreviewPopcycleSlot*
getPreviewPopcycleSlotForPosition(const rw::V3d &pos, const PreviewZoneState **outState)
{
	if(outState)
		*outState = nil;
	if(!loadPreviewPopulationData())
		return nil;

	const PreviewZoneState *state = getPreviewZoneStateForPosition(pos);
	if(state == nil)
		return nil;
	if(outState)
		*outState = state;
	if(state->popType < 0 || state->popType >= PREVIEW_POP_ZONE_COUNT)
		return nil;
	return &gPreviewPopcycle[state->popType][getPreviewCurrentWeekendIndex()][clamp(currentHour / 2, 0, PREVIEW_POP_TIMESLOTS - 1)];
}

static int
getPreviewPedGroupIndex(int popGroup, int worldZone)
{
	switch(popGroup){
	case 0: return worldZone;
	case 1: return 3 + worldZone;
	case 2: return 6 + worldZone;
	case 3: return 9;
	case 4: return 10;
	case 5: return 11 + worldZone;
	case 6: return 14 + worldZone;
	case 7: return 17 + worldZone;
	case 8: return 20 + worldZone;
	case 9: return 23 + worldZone;
	case 10: return 26 + worldZone;
	case 11: return 29;
	case 12: return 30 + worldZone;
	case 13: return 33 + worldZone;
	case 14: return 36 + worldZone;
	case 15: return 39;
	case 16: return 40;
	case 17: return 41;
	default: return -1;
	}
}

static int
choosePreviewPedDefFromGroup(int pedGroupIndex, uint8 raceMask, uint32 &seed)
{
	if(pedGroupIndex < 0 || pedGroupIndex >= (int)gPreviewPedGroups.size())
		return -1;
	const PreviewPedGroup &group = gPreviewPedGroups[pedGroupIndex];
	if(group.modelNames.empty())
		return -1;

	for(int tries = 0; tries < (int)group.modelNames.size(); tries++){
		int modelIndex = (int)(previewSeedFloat01(seed) * group.modelNames.size());
		modelIndex = clamp(modelIndex, 0, (int)group.modelNames.size() - 1);
		int pedDefIndex = findPreviewPedDefIndexByModelName(group.modelNames[modelIndex].c_str());
		if(pedDefIndex < 0)
			continue;
		const PreviewPedDef &pedDef = gPreviewPedDefs[pedDefIndex];
		if(pedDef.race != PREVIEW_RACE_DEFAULT && (raceMask & previewRaceMaskFromRace(pedDef.race)) == 0)
			continue;
		return pedDefIndex;
	}
	return -1;
}

static int
choosePreviewCopPedDef(int popType, int worldZone)
{
	const char *modelName = "lapd1";
	if(popType == PREVIEW_ZONE_DESERT)
		modelName = "dsher";
	else if(popType == PREVIEW_ZONE_COUNTRYSIDE || popType == PREVIEW_ZONE_OUT_OF_TOWN_FACTORY)
		modelName = "csher";
	else if(worldZone == 1)
		modelName = "sfpd1";
	else if(worldZone == 2)
		modelName = "lvpd1";
	return findPreviewPedDefIndexByModelName(modelName);
}

static int
choosePreviewPedDefForPosition(const rw::V3d &pos, uint32 &seed)
{
	const PreviewZoneState *state = nil;
	const PreviewPopcycleSlot *slot = getPreviewPopcycleSlotForPosition(pos, &state);
	if(slot == nil || state == nil)
		return -1;

	const int worldZone = getPreviewWorldZoneIndexForPosition(pos);
	const float gangDensity = (float)(
		state->gangStrength[0] + state->gangStrength[1] + state->gangStrength[2] + state->gangStrength[3] +
		state->gangStrength[4] + state->gangStrength[5] + state->gangStrength[6] + state->gangStrength[7] +
		state->gangStrength[8] + state->gangStrength[9]
	);
	float dealerFactor = max(0.1f, state->dealerStrength / 100.0f);
	float gangFactor = min(0.5f, gangDensity / 100.0f);
	float copsFactor = state->noCops ? 0.0f :
		(gangFactor >= 0.15f ? max(0.03f, 0.3f - gangFactor) : max(0.02f, gangFactor));
	float otherFactor = max(0.0f, 1.0f - (dealerFactor + gangFactor + copsFactor));

	float dealerWeight = slot->percDealers * dealerFactor;
	float gangWeight = slot->percGang * gangFactor;
	float copsWeight = slot->percCops * copsFactor;
	float otherWeight = slot->percOther * otherFactor;
	float totalWeight = dealerWeight + gangWeight + copsWeight + otherWeight;
	if(totalWeight <= 0.001f)
		otherWeight = totalWeight = 1.0f;

	float roll = previewSeedFloat01(seed) * totalWeight;
	if(roll < otherWeight){
		int totalGroupWeight = 0;
		for(int i = 0; i < PREVIEW_POP_GROUP_COUNT; i++)
			totalGroupWeight += slot->groupPerc[i];
		if(totalGroupWeight <= 0)
			totalGroupWeight = PREVIEW_POP_GROUP_COUNT;
		int choice = (int)(previewSeedFloat01(seed) * totalGroupWeight);
		for(int group = 0; group < PREVIEW_POP_GROUP_COUNT; group++){
			int weight = slot->groupPerc[group];
			if(totalGroupWeight == PREVIEW_POP_GROUP_COUNT)
				weight = 1;
			if(choice < weight)
				return choosePreviewPedDefFromGroup(getPreviewPedGroupIndex(group, worldZone), state->raceMask, seed);
			choice -= weight;
		}
	}else{
		roll -= otherWeight;
		if(roll < copsWeight){
			int pedDef = choosePreviewCopPedDef(state->popType, worldZone);
			if(pedDef >= 0)
				return pedDef;
		}else{
			roll -= copsWeight;
			if(roll < dealerWeight){
				int pedDef = choosePreviewPedDefFromGroup(52, state->raceMask, seed);
				if(pedDef >= 0)
					return pedDef;
			}else{
				int sumGang = 0;
				for(int i = 0; i < 10; i++)
					sumGang += state->gangStrength[i];
				if(sumGang > 0){
					int choice = (int)(previewSeedFloat01(seed) * sumGang);
					for(int i = 0; i < 10; i++){
						if(choice < state->gangStrength[i])
							return choosePreviewPedDefFromGroup(42 + i, state->raceMask, seed);
						choice -= state->gangStrength[i];
					}
				}
			}
		}
	}

	return -1;
}

static int
choosePreviewVehicleDefFromGroup(int carGroupIndex, uint32 &seed)
{
	if(carGroupIndex < 0 || carGroupIndex >= (int)gPreviewCarGroups.size())
		return -1;
	const PreviewCarGroup &group = gPreviewCarGroups[carGroupIndex];
	if(group.modelNames.empty())
		return -1;

	for(int tries = 0; tries < (int)group.modelNames.size(); tries++){
		int modelIndex = (int)(previewSeedFloat01(seed) * group.modelNames.size());
		modelIndex = clamp(modelIndex, 0, (int)group.modelNames.size() - 1);
		int vehicleDefIndex = findPreviewVehicleDefIndexByModelName(group.modelNames[modelIndex].c_str());
		if(vehicleDefIndex >= 0)
			return vehicleDefIndex;
	}
	return -1;
}

static int
choosePreviewPoliceVehicleDef(int popType, int worldZone)
{
	const char *modelName = "copcarla";
	if(popType == PREVIEW_ZONE_DESERT || popType == PREVIEW_ZONE_COUNTRYSIDE || popType == PREVIEW_ZONE_OUT_OF_TOWN_FACTORY)
		modelName = "copcarru";
	else if(worldZone == 1)
		modelName = "copcarsf";
	else if(worldZone == 2)
		modelName = "copcarvg";
	return findPreviewVehicleDefIndexByModelName(modelName);
}

static int
choosePreviewVehicleDefForPosition(const rw::V3d &pos, uint32 &seed)
{
	if(!loadPreviewVehicleMetadata() || !loadPreviewVehiclePopulationData())
		return -1;

	const PreviewZoneState *state = nil;
	const PreviewPopcycleSlot *slot = getPreviewPopcycleSlotForPosition(pos, &state);
	if(slot == nil || state == nil)
		return -1;

	const int worldZone = getPreviewWorldZoneIndexForPosition(pos);
	const float gangDensity = (float)(
		state->gangStrength[0] + state->gangStrength[1] + state->gangStrength[2] + state->gangStrength[3] +
		state->gangStrength[4] + state->gangStrength[5] + state->gangStrength[6] + state->gangStrength[7] +
		state->gangStrength[8] + state->gangStrength[9]
	);
	float dealerFactor = max(0.1f, state->dealerStrength / 100.0f);
	float gangFactor = min(0.5f, gangDensity / 100.0f);
	float copsFactor = state->noCops ? 0.0f :
		(gangFactor >= 0.15f ? max(0.03f, 0.3f - gangFactor) : max(0.02f, gangFactor));
	float otherFactor = max(0.0f, 1.0f - (dealerFactor + gangFactor + copsFactor));

	float dealerWeight = slot->percDealers * dealerFactor;
	float gangWeight = slot->percGang * gangFactor;
	float copsWeight = slot->percCops * copsFactor;
	float otherWeight = slot->percOther * otherFactor;
	float totalWeight = dealerWeight + gangWeight + copsWeight + otherWeight;
	if(totalWeight <= 0.001f)
		otherWeight = totalWeight = 1.0f;

	float roll = previewSeedFloat01(seed) * totalWeight;
	if(roll < otherWeight){
		int totalGroupWeight = 0;
		for(int i = 0; i < PREVIEW_CAR_GROUP_OTHER_COUNT; i++)
			totalGroupWeight += slot->groupPerc[i];
		if(totalGroupWeight <= 0)
			totalGroupWeight = PREVIEW_CAR_GROUP_OTHER_COUNT;
		int choice = (int)(previewSeedFloat01(seed) * totalGroupWeight);
		for(int group = 0; group < PREVIEW_CAR_GROUP_OTHER_COUNT; group++){
			int weight = slot->groupPerc[group];
			if(totalGroupWeight == PREVIEW_CAR_GROUP_OTHER_COUNT)
				weight = 1;
			if(choice < weight)
				return choosePreviewVehicleDefFromGroup(group, seed);
			choice -= weight;
		}
	}else{
		roll -= otherWeight;
		if(roll < copsWeight){
			int vehicleDef = choosePreviewPoliceVehicleDef(state->popType, worldZone);
			if(vehicleDef >= 0)
				return vehicleDef;
		}else{
			roll -= copsWeight;
			if(roll < dealerWeight){
				int vehicleDef = choosePreviewVehicleDefFromGroup(PREVIEW_CAR_GROUP_DEALERS, seed);
				if(vehicleDef >= 0)
					return vehicleDef;
			}else{
				int sumGang = 0;
				for(int i = 0; i < 10; i++)
					sumGang += state->gangStrength[i];
				if(sumGang > 0){
					int choice = (int)(previewSeedFloat01(seed) * sumGang);
					for(int i = 0; i < 10; i++){
						if(choice < state->gangStrength[i])
							return choosePreviewVehicleDefFromGroup(PREVIEW_CAR_GROUP_GANG_BASE + i, seed);
						choice -= state->gangStrength[i];
					}
				}
			}
		}
	}

	return -1;
}

static float
getPreviewTrafficVehicleFitWeight(const PreviewVehicleDef &vehicleDef, const Node *node, const DiskCarPathLink *navi)
{
	if(node == nil)
		return 1.0f;

	float roadWidth = node->width();
	int totalLanes = 0;
	if(navi)
		totalLanes = getNaviSameLanes(*navi) + getNaviOppositeLanes(*navi);

	bool narrowRoad = roadWidth <= 7.5f || totalLanes <= 2;
	bool wideRoad = roadWidth >= 12.0f || totalLanes >= 4;
	bool industrialRoad = node->highway();
	bool localRoad = node->notHighway();

	float weight = 1.0f;
	if(vehicleDef.heavy){
		if(narrowRoad)
			weight *= 0.12f;
		else if(localRoad)
			weight *= 0.35f;
		else if(wideRoad)
			weight *= 1.15f;
		if(industrialRoad)
			weight *= 1.25f;
	}else{
		if(narrowRoad)
			weight *= 1.15f;
		if(localRoad)
			weight *= 1.10f;
		if(wideRoad)
			weight *= 0.9f;
	}

	return weight;
}

static int
choosePreviewVehicleDefForTrafficContext(const rw::V3d &pos, const Node *node, const DiskCarPathLink *navi, uint32 &seed)
{
	int fallback = -1;
	for(int attempts = 0; attempts < 12; attempts++){
		int vehicleDefIndex = choosePreviewVehicleDefForPosition(pos, seed);
		if(vehicleDefIndex < 0)
			break;
		if(fallback < 0)
			fallback = vehicleDefIndex;
		const PreviewVehicleDef &vehicleDef = gPreviewVehicleDefs[vehicleDefIndex];
		float fitWeight = getPreviewTrafficVehicleFitWeight(vehicleDef, node, navi);
		if(fitWeight >= 0.999f || previewSeedFloat01(seed) < fitWeight)
			return vehicleDefIndex;
	}

	float totalWeight = 0.0f;
	for(size_t i = 0; i < gPreviewVehicleDefs.size(); i++)
		totalWeight += getPreviewTrafficVehicleFitWeight(gPreviewVehicleDefs[i], node, navi);
	if(totalWeight > 0.001f){
		float roll = previewSeedFloat01(seed) * totalWeight;
		for(size_t i = 0; i < gPreviewVehicleDefs.size(); i++){
			float weight = getPreviewTrafficVehicleFitWeight(gPreviewVehicleDefs[i], node, navi);
			if(roll < weight)
				return (int)i;
			roll -= weight;
		}
	}

	return fallback;
}

static uint8
choosePreviewFallbackVehicleColorIndex(uint32 &seed)
{
	int colorIndex = 1 + (int)(previewSeedFloat01(seed) * 125.0f);
	colorIndex = clamp(colorIndex, 1, 125);
	if(colorIndex == 126)
		colorIndex = 1;
	return (uint8)colorIndex;
}

static void
choosePreviewVehicleColorIndices(const PreviewVehicleDef &vehicleDef, uint32 &seed, uint8 outColors[4])
{
	if(outColors == nil)
		return;
	if(loadPreviewVehicleColorData() && !vehicleDef.colorPairs.empty()){
		int pairIndex = clamp((int)(previewSeedFloat01(seed) * vehicleDef.colorPairs.size()), 0, (int)vehicleDef.colorPairs.size() - 1);
		uint32 packed = vehicleDef.colorPairs[pairIndex];
		outColors[0] = (uint8)(packed & 0xFF);
		outColors[1] = (uint8)((packed >> 8) & 0xFF);
		outColors[2] = (uint8)((packed >> 16) & 0xFF);
		outColors[3] = (uint8)((packed >> 24) & 0xFF);
		return;
	}

	outColors[0] = choosePreviewFallbackVehicleColorIndex(seed);
	outColors[1] = choosePreviewFallbackVehicleColorIndex(seed);
	outColors[2] = outColors[0];
	outColors[3] = outColors[1];
}

static uint32
advancePreviewSeed(uint32 &seed)
{
	seed = seed*1664525u + 1013904223u;
	return seed;
}

static float
previewSeedFloat01(uint32 &seed)
{
	return (advancePreviewSeed(seed) & 0x00FFFFFF) / 16777216.0f;
}

static rw::V3d
lerpV3d(const rw::V3d &a, const rw::V3d &b, float t)
{
	return add(scale(a, 1.0f - t), scale(b, t));
}

static rw::Quat
normalizePreviewQuat(const rw::Quat &q)
{
	float len = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
	if(len < 0.0001f)
		return rw::makeQuat(1.0f, 0.0f, 0.0f, 0.0f);
	return scale(q, 1.0f/len);
}

static bool
readIfpChunkHeader(const std::vector<uint8> &data, size_t *offset, IfpChunkHeader *header)
{
	const uint8 *bytes = data.empty() ? nil : &data[0];
	return readExact(bytes, data.size(), offset, header, sizeof(*header));
}

static bool
readRoundedChunkData(const std::vector<uint8> &data, size_t *offset, uint32 size, std::vector<uint8> &out)
{
	size_t rounded = (size + 3) & ~3;
	out.resize(rounded);
	if(rounded == 0)
		return true;
	const uint8 *bytes = data.empty() ? nil : &data[0];
	return readExact(bytes, data.size(), offset, &out[0], rounded);
}

static rw::Clump*
loadRwClumpFromMemory(rw::Stream *stream)
{
	using namespace rw;

	rw::Clump *clump = nil;
	ChunkHeaderInfo header;
	readChunkHeaderInfo(stream, &header);
	UVAnimDictionary *prev = currentUVAnimDictionary;
	if(header.type == ID_UVANIMDICT){
		UVAnimDictionary *dict = UVAnimDictionary::streamRead(stream);
		currentUVAnimDictionary = dict;
		readChunkHeaderInfo(stream, &header);
	}
	if(header.type == ID_CLUMP)
		clump = Clump::streamRead(stream);
	currentUVAnimDictionary = prev;
	return clump;
}

static void
setupPreviewAtomic(rw::Atomic *atm)
{
	gta::attachCustomPipelines(atm);
	int32 driver = rw::platform;
	int32 platform = rw::findPlatform(atm);
	if(platform){
		rw::platform = platform;
		rw::switchPipes(atm, rw::platform);
	}
	if(atm->geometry->flags & rw::Geometry::NATIVE)
		atm->uninstance();
	rw::ps2::unconvertADC(atm->geometry);
	rw::platform = driver;

	if(rw::Skin::get(atm->geometry)){
		rw::Skin::setPipeline(atm, 1);
		atm->setRenderCB(myRenderCB);
		return;
	}

	if(params.neoWorldPipe)
		atm->pipeline = neoWorldPipe;
	else if(params.leedsPipe)
		atm->pipeline = gta::leedsPipe;
	else if(params.daynightPipe && IsBuildingPipeAttached(atm))
		SetupBuildingPipe(atm);
	else if(params.daynightPipe)
		SetupBuildingPipe(atm);
	else
		atm->pipeline = nil;
	atm->setRenderCB(myRenderCB);
}

static rw::Frame*
findHAnimHierarchyCB(rw::Frame *frame, void *data)
{
	rw::HAnimData *hd = rw::HAnimData::get(frame);
	if(hd->hierarchy){
		*(rw::HAnimHierarchy**)data = hd->hierarchy;
		return nil;
	}
	frame->forAllChildren(findHAnimHierarchyCB, data);
	return frame;
}

static rw::HAnimHierarchy*
getHAnimHierarchyFromClump(rw::Clump *clump)
{
	rw::HAnimHierarchy *hier = nil;
	findHAnimHierarchyCB(clump->getFrame(), &hier);
	return hier;
}

static void
initHierarchyFromFrames(rw::HAnimHierarchy *hier)
{
	for(int i = 0; i < hier->numNodes; i++)
		if(hier->nodeInfo[i].frame)
			hier->matrices[hier->nodeInfo[i].index] = *hier->nodeInfo[i].frame->getLTM();
}

static void
setupPreviewClump(rw::Clump *clump)
{
	rw::HAnimHierarchy *hier = getHAnimHierarchyFromClump(clump);
	if(hier){
		hier->attach();
		initHierarchyFromFrames(hier);
	}

	FORLIST(lnk, clump->atomics){
		rw::Atomic *atomic = rw::Atomic::fromClump(lnk);
		setupPreviewAtomic(atomic);
		if(hier)
			rw::Skin::setHierarchy(atomic, hier);
	}
}

struct PreviewFrameFindData {
	const char *name;
	rw::Frame *found;
};

static rw::Frame*
findPreviewFrameByNameCB(rw::Frame *frame, void *data)
{
	PreviewFrameFindData *find = (PreviewFrameFindData*)data;
	if(find == nil || find->found)
		return nil;

	const char *nodeName = gta::getNodeName(frame);
	if(nodeName && rw::strcmp_ci(nodeName, find->name) == 0){
		find->found = frame;
		return nil;
	}
	frame->forAllChildren(findPreviewFrameByNameCB, data);
	return frame;
}

static rw::Frame*
findPreviewFrameByName(rw::Clump *clump, const char *name)
{
	if(clump == nil || name == nil)
		return nil;
	PreviewFrameFindData find = { name, nil };
	findPreviewFrameByNameCB(clump->getFrame(), &find);
	return find.found;
}

static rw::Atomic*
findPreviewVehicleWheelSourceAtomic(rw::Clump *clump)
{
	if(clump == nil)
		return nil;

	rw::Atomic *fallback = nil;
	FORLIST(lnk, clump->atomics){
		rw::Atomic *atomic = rw::Atomic::fromClump(lnk);
		if(atomic == nil)
			continue;
		rw::Frame *frame = atomic->getFrame();
		const char *nodeName = frame ? gta::getNodeName(frame) : nil;
		if(nodeName == nil)
			continue;
		if(rw::strcmp_ci(nodeName, "wheel") == 0)
			return atomic;
		if(strncmp(nodeName, "wheel_rf", 8) == 0 && strstr(nodeName, "dummy") == nil)
			return atomic;
		if(fallback == nil && rw::strcmp_ci(nodeName, "wheel_rf_dummy") == 0)
			fallback = atomic;
	}
	return fallback;
}

static bool
previewFrameHasAtomic(rw::Clump *clump, rw::Frame *frame)
{
	if(clump == nil || frame == nil)
		return false;
	FORLIST(lnk, clump->atomics){
		rw::Atomic *atomic = rw::Atomic::fromClump(lnk);
		if(atomic && atomic->getFrame() == frame)
			return true;
	}
	return false;
}

static void
ensurePreviewVehicleWheelAtomics(rw::Clump *clump)
{
	static const char *wheelDummyNames[] = {
		"wheel_lf_dummy",
		"wheel_rf_dummy",
		"wheel_lb_dummy",
		"wheel_rb_dummy"
	};

	if(clump == nil)
		return;
	rw::Atomic *sourceAtomic = findPreviewVehicleWheelSourceAtomic(clump);
	if(sourceAtomic == nil)
		return;

	rw::HAnimHierarchy *hier = getHAnimHierarchyFromClump(clump);
	for(size_t i = 0; i < sizeof(wheelDummyNames)/sizeof(wheelDummyNames[0]); i++){
		rw::Frame *dummyFrame = findPreviewFrameByName(clump, wheelDummyNames[i]);
		if(dummyFrame == nil || previewFrameHasAtomic(clump, dummyFrame))
			continue;

		rw::Atomic *clone = sourceAtomic->clone();
		if(clone == nil)
			continue;
		clone->setFrame(dummyFrame);
		setupPreviewAtomic(clone);
		if(hier)
			rw::Skin::setHierarchy(clone, hier);
		clump->addAtomic(clone);
	}
}

static uint8
getPreviewVehicleMaterialColorRole(const rw::RGBA &color)
{
	if(color.red == 60 && color.green == 255 && color.blue == 0)
		return 1;
	if(color.red == 255 && color.green == 0 && color.blue == 175)
		return 2;
	if(color.red == 0 && color.green == 255 && color.blue == 255)
		return 3;
	if(color.red == 255 && color.green == 0 && color.blue == 255)
		return 4;
	return 0;
}

static bool
isPreviewVehicleLightMarkerColor(const rw::RGBA &color)
{
	return (color.red == 255 && color.green == 175 && color.blue == 0) ||
	       (color.red == 0 && color.green == 255 && color.blue == 200) ||
	       (color.red == 185 && color.green == 255 && color.blue == 0) ||
	       (color.red == 255 && color.green == 60 && color.blue == 0);
}

static void
collectPreviewVehicleMaterials(rw::Clump *clump, std::vector<rw::Material*> &materials)
{
	materials.clear();
	if(clump == nil)
		return;

	FORLIST(lnk, clump->atomics){
		rw::Atomic *atomic = rw::Atomic::fromClump(lnk);
		if(atomic == nil || atomic->geometry == nil)
			continue;
		for(int i = 0; i < atomic->geometry->matList.numMaterials; i++){
			rw::Material *mat = atomic->geometry->matList.materials[i];
			if(mat)
				materials.push_back(mat);
		}
	}
}

static void
buildPreviewVehicleMaterialColorRoles(rw::Clump *clump, std::vector<uint8> &roles)
{
	std::vector<rw::Material*> materials;
	collectPreviewVehicleMaterials(clump, materials);
	roles.resize(materials.size());
	for(size_t i = 0; i < materials.size(); i++)
		roles[i] = getPreviewVehicleMaterialColorRole(materials[i]->color);
}

static void
normalizePreviewVehicleMaterialColors(rw::Clump *clump)
{
	if(clump == nil)
		return;

	FORLIST(lnk, clump->atomics){
		rw::Atomic *atomic = rw::Atomic::fromClump(lnk);
		if(atomic == nil || atomic->geometry == nil)
			continue;
		for(int i = 0; i < atomic->geometry->matList.numMaterials; i++){
			rw::Material *mat = atomic->geometry->matList.materials[i];
			if(mat && mat->texture &&
			   getPreviewVehicleMaterialColorRole(mat->color) == 0 &&
			   !isPreviewVehicleLightMarkerColor(mat->color)){
				mat->color.red = 255;
				mat->color.green = 255;
				mat->color.blue = 255;
			}
		}
	}
}

static void
applyPreviewVehicleColorIndices(rw::Clump *clump, const std::vector<uint8> &roles, const uint8 colorIndices[4])
{
	if(clump == nil || colorIndices == nil)
		return;

	std::vector<rw::Material*> materials;
	collectPreviewVehicleMaterials(clump, materials);
	size_t count = min(materials.size(), roles.size());
	for(size_t i = 0; i < count; i++){
		int colorSlot = (int)roles[i] - 1;
		if(colorSlot < 0 || colorSlot >= 4)
			continue;
		int paletteIndex = colorIndices[colorSlot];
		if(paletteIndex < 0 || paletteIndex >= 128)
			continue;
		const CRGBA &color = gPreviewVehicleColourTable[paletteIndex];
		materials[i]->color.red = color.r;
		materials[i]->color.green = color.g;
		materials[i]->color.blue = color.b;
	}
}

static void
stripPreviewVehicleDamageAtomics(rw::Clump *clump)
{
	if(clump == nil)
		return;

	FORLIST(lnk, clump->atomics){
		rw::Atomic *atomic = rw::Atomic::fromClump(lnk);
		if(atomic == nil)
			continue;
		rw::Frame *frame = atomic->getFrame();
		char *nodeName = frame ? gta::getNodeName(frame) : nil;
		if(nodeName && strstr(nodeName, "_dam"))
			atomic->setFlags(0);
	}
}

static void
stripPreviewVehicleLowDetailAtomics(rw::Clump *clump)
{
	if(clump == nil)
		return;

	FORLIST(lnk, clump->atomics){
		rw::Atomic *atomic = rw::Atomic::fromClump(lnk);
		if(atomic == nil)
			continue;
		rw::Frame *frame = atomic->getFrame();
		char *nodeName = frame ? gta::getNodeName(frame) : nil;
		if(nodeName && (strstr(nodeName, "_vlo") || strstr(nodeName, "_lo_")))
			atomic->setFlags(0);
	}
}

static void
destroyPreviewWalker(PreviewWalker &walker)
{
	if(walker.clump){
		walker.clump->destroy();
		walker.clump = nil;
	}
	walker.assetIndex = -1;
	walker.active = false;
}

static void
clearPreviewWalkers(void)
{
	for(size_t i = 0; i < gPreviewWalkers.size(); i++)
		destroyPreviewWalker(gPreviewWalkers[i]);
	gPreviewWalkers.clear();
}

static void
destroyPreviewTrafficCar(PreviewTrafficCar &car)
{
	if(car.clump){
		car.clump->destroy();
		car.clump = nil;
	}
	car.assetIndex = -1;
	car.active = false;
	car.materialColorRoles.clear();
}

static void
destroyPreviewParkedCar(PreviewParkedCar &car)
{
	if(car.clump){
		car.clump->destroy();
		car.clump = nil;
	}
	car.assetIndex = -1;
	car.active = false;
	car.materialColorRoles.clear();
}

static void
clearPreviewTrafficCars(void)
{
	for(size_t i = 0; i < gPreviewTrafficCars.size(); i++)
		destroyPreviewTrafficCar(gPreviewTrafficCars[i]);
	gPreviewTrafficCars.clear();
	gPreviewTrafficLastSeenChangeSeq = GetLatestChangeSeq();
}

static void
clearPreviewParkedCars(void)
{
	for(size_t i = 0; i < gPreviewParkedCars.size(); i++)
		destroyPreviewParkedCar(gPreviewParkedCars[i]);
	gPreviewParkedCars.clear();
	gPreviewParkedLastSeenChangeSeq = GetLatestChangeSeq();
}

static bool
buildAreaImgLogicalPathForArchive(int areaId, const char *archiveLogicalPath, char *dst, size_t size)
{
	int written = snprintf(dst, size, "%s/nodes%d.dat", archiveLogicalPath, areaId);
	return written >= 0 && (size_t)written < size;
}

static bool
buildAreaPathsImgLogicalPath(int areaId, char *dst, size_t size)
{
	return buildAreaImgLogicalPathForArchive(areaId, "models/paths.img", dst, size);
}

static bool
buildAreaGta3ImgLogicalPath(int areaId, char *dst, size_t size)
{
	return buildAreaImgLogicalPathForArchive(areaId, "models/gta3.img", dst, size);
}

static bool
extractArchiveLogicalPath(const char *logicalPath, char *dst, size_t size)
{
	if(dst == nil || size == 0)
		return false;
	dst[0] = '\0';
	if(logicalPath == nil || logicalPath[0] == '\0')
		return false;

	const char *slash = strrchr(logicalPath, '/');
	if(slash == nil)
		return false;

	size_t len = slash - logicalPath;
	if(len >= size)
		return false;
	memcpy(dst, logicalPath, len);
	dst[len] = '\0';
	return true;
}

static uint32
getPreviewTrafficBaseSeed(const PreviewTrafficCar &car)
{
	return 0x2468ACE1u + car.stableId*0x01020305u;
}

static uint32
getPreviewTrafficResetSeed(const PreviewTrafficCar &car, uint32 salt)
{
	if(gSaCarPathTrafficFreezeRoutes)
		return getPreviewTrafficBaseSeed(car);
	return car.seed + salt;
}

static bool
buildAreaLegacyLogicalPath(int areaId, char *dst, size_t size)
{
	int written = snprintf(dst, size, "data/paths/nodes%d.dat", areaId);
	return written >= 0 && (size_t)written < size;
}

static bool
loadAreaImgData(int areaId, std::vector<uint8> &data, char *outSourcePath, size_t outSourcePathSize,
                char *outLogicalPath, size_t outLogicalPathSize)
{
	char logicalPath[64];
	char fallbackLogicalPath[64];
	data.clear();
	if(outSourcePath && outSourcePathSize > 0)
		outSourcePath[0] = '\0';
	if(outLogicalPath && outLogicalPathSize > 0)
		outLogicalPath[0] = '\0';
	if(!buildAreaPathsImgLogicalPath(areaId, logicalPath, sizeof(logicalPath)) ||
	   !buildAreaGta3ImgLogicalPath(areaId, fallbackLogicalPath, sizeof(fallbackLogicalPath)))
		return false;

	if(ReadCdImageEntryByLogicalPath(logicalPath, data, outSourcePath, outSourcePathSize)){
		if(outLogicalPath && outLogicalPathSize > 0){
			strncpy(outLogicalPath, logicalPath, outLogicalPathSize-1);
			outLogicalPath[outLogicalPathSize-1] = '\0';
		}
		return true;
	}
	if(ReadCdImageEntryByLogicalPath(fallbackLogicalPath, data, outSourcePath, outSourcePathSize)){
		if(outLogicalPath && outLogicalPathSize > 0){
			strncpy(outLogicalPath, fallbackLogicalPath, outLogicalPathSize-1);
			outLogicalPath[outLogicalPathSize-1] = '\0';
		}
		return true;
	}
	if(outLogicalPath && outLogicalPathSize > 0){
		strncpy(outLogicalPath, logicalPath, outLogicalPathSize-1);
		outLogicalPath[outLogicalPathSize-1] = '\0';
	}
	return false;
}

static bool
loadAreaLooseData(int areaId, std::vector<uint8> &data, char *outSourcePath, size_t outSourcePathSize,
                  char *outLogicalPath, size_t outLogicalPathSize)
{
	char legacyLogicalPath[64];
	char stockPath[64];
	int size = 0;

	data.clear();
	if(outSourcePath && outSourcePathSize > 0)
		outSourcePath[0] = '\0';
	if(outLogicalPath && outLogicalPathSize > 0)
		outLogicalPath[0] = '\0';
	if(!buildAreaLegacyLogicalPath(areaId, legacyLogicalPath, sizeof(legacyLogicalPath)))
		return false;
	if(outLogicalPath && outLogicalPathSize > 0){
		strncpy(outLogicalPath, legacyLogicalPath, outLogicalPathSize-1);
		outLogicalPath[outLogicalPathSize-1] = '\0';
	}
	const char *redirect = ModloaderGetSourcePath(legacyLogicalPath);
	if(redirect){
		uint8 *buf = ReadLooseFile(redirect, &size);
		if(buf){
			data.assign(buf, buf + size);
			free(buf);
			if(outSourcePath && outSourcePathSize > 0){
				strncpy(outSourcePath, redirect, outSourcePathSize-1);
				outSourcePath[outSourcePathSize-1] = '\0';
			}
			return true;
		}
	}

	snprintf(stockPath, sizeof(stockPath), "data/Paths/NODES%d.DAT", areaId);
	uint8 *buf = ReadLooseFile(stockPath, &size);
	if(buf){
		data.assign(buf, buf + size);
		free(buf);
		if(outSourcePath && outSourcePathSize > 0){
			strncpy(outSourcePath, stockPath, outSourcePathSize-1);
			outSourcePath[outSourcePathSize-1] = '\0';
		}
		return true;
	}

	snprintf(stockPath, sizeof(stockPath), "data/paths/nodes%d.dat", areaId);
	buf = ReadLooseFile(stockPath, &size);
	if(buf){
		data.assign(buf, buf + size);
		free(buf);
		if(outSourcePath && outSourcePathSize > 0){
			strncpy(outSourcePath, stockPath, outSourcePathSize-1);
			outSourcePath[outSourcePathSize-1] = '\0';
		}
		return true;
	}

	return false;
}

static AreaData*
getArea(uint16 areaId)
{
	if(areaId >= NUM_PATH_AREAS)
		return nil;
	return &gAreas[areaId];
}

static Node*
getNode(NodeAddress addr)
{
	AreaData *area = getArea(addr.areaId);
	if(area == nil || !area->loaded || addr.nodeId >= area->nodes.size())
		return nil;
	return &area->nodes[addr.nodeId];
}

static uint8
computeLinkLength(const Node &a, const Node &b)
{
	float dx = b.x() - a.x();
	float dy = b.y() - a.y();
	int len = (int)std::floor(std::sqrt(dx*dx + dy*dy) + 0.5f);
	return (uint8)clamp(len, 0, 255);
}

static void
alignNaviDirectionToAttachedNode(DiskCarPathLink &navi, const Node &attached)
{
	float dx = attached.x() - navi.x * NAVI_POS_SCALE;
	float dy = attached.y() - navi.y * NAVI_POS_SCALE;
	float len = std::sqrt(dx*dx + dy*dy);
	if(len < 0.0001f){
		navi.dirX = 0;
		navi.dirY = 0;
	}else{
		navi.dirX = (int8)clamp((int)std::floor(dx/len*100.0f + (dx >= 0.0f ? 0.5f : -0.5f)), -128, 127);
		navi.dirY = (int8)clamp((int)std::floor(dy/len*100.0f + (dy >= 0.0f ? 0.5f : -0.5f)), -128, 127);
	}
}

static void refreshMetadataForNode(const NodeAddress &changedAddr);
static bool findReciprocalLinkSlot(const NodeAddress &from, const NodeAddress &to, uint16 *outAreaId, int *outSlot);

static void
touchChangeSeq(void)
{
	BumpChangeSeq();
}

static void
setAreaDirty(uint16 areaId, bool notifyChange)
{
	AreaData *area = getArea(areaId);
	if(area == nil)
		return;
	area->dirty = true;
	if(notifyChange)
		touchChangeSeq();
}

static bool
isVehicleNode(const Node *node)
{
	if(node == nil)
		return false;
	AreaData *area = getArea(node->ownerAreaId);
	return area && node->index < area->numVehicleNodes;
}

static int
getAreaGridX(int areaId)
{
	return areaId % 8;
}

static int
getAreaGridY(int areaId)
{
	return areaId / 8;
}

static float
getAreaMinX(int areaId)
{
	return -3000.0f + getAreaGridX(areaId) * AREA_SIZE;
}

static float
getAreaMinY(int areaId)
{
	return -3000.0f + getAreaGridY(areaId) * AREA_SIZE;
}

static float
getAreaMaxX(int areaId)
{
	return getAreaMinX(areaId) + AREA_SIZE;
}

static float
getAreaMaxY(int areaId)
{
	return getAreaMinY(areaId) + AREA_SIZE;
}

static int
findAreaIdForPosition(float x, float y)
{
	int gridX = clamp((int)((x + 3000.0f) / AREA_SIZE), 0, 7);
	int gridY = clamp((int)((y + 3000.0f) / AREA_SIZE), 0, 7);
	return gridY * 8 + gridX;
}

static bool
positionBelongsToArea(int areaId, float x, float y)
{
	return findAreaIdForPosition(x, y) == areaId;
}

static bool
areaInDrawRange(int areaId)
{
	rw::V3d cam = TheCamera.m_position;
	float minX = getAreaMinX(areaId) - PATH_DRAW_DIST;
	float maxX = getAreaMaxX(areaId) + PATH_DRAW_DIST;
	float minY = getAreaMinY(areaId) - PATH_DRAW_DIST;
	float maxY = getAreaMaxY(areaId) + PATH_DRAW_DIST;
	return cam.x >= minX && cam.x <= maxX && cam.y >= minY && cam.y <= maxY;
}

static rw::V3d
getNodePosition(const Node &node)
{
	rw::V3d pos = { node.x(), node.y(), node.z() };
	return pos;
}

static rw::V3d
getPedWanderOffset(const Node &node, uint32 seed)
{
	int xoff = (int)(seed & 0xF) - 7;
	int yoff = (int)((seed >> 4) & 0xF) - 7;
	float scale = node.raw.pathWidth * 0.00775f;
	rw::V3d offset = { xoff * scale, yoff * scale, 0.0f };
	return offset;
}

static bool
loadPreviewAnimation(const std::vector<uint8> &data, const char *wantedName, PreviewAnimation &outAnim)
{
	size_t offset = 0;
	IfpChunkHeader root;
	std::vector<uint8> chunkData;

	outAnim.duration = 0.0f;
	outAnim.nodes.clear();

	if(!readIfpChunkHeader(data, &offset, &root))
		return false;

	if(strncmp(root.ident, "ANP3", 4) == 0){
		char packageName[24];
		uint32 numAnimations = 0;
		if(!readExact(&data[0], data.size(), &offset, packageName, sizeof(packageName)) ||
		   !readExact(&data[0], data.size(), &offset, &numAnimations, sizeof(numAnimations)))
			return false;

		for(uint32 animIndex = 0; animIndex < numAnimations; animIndex++){
			char animName[24];
			uint32 numNodes = 0;
			uint32 frameDataSize = 0;
			uint32 flags = 0;
			if(!readExact(&data[0], data.size(), &offset, animName, sizeof(animName)) ||
			   !readExact(&data[0], data.size(), &offset, &numNodes, sizeof(numNodes)) ||
			   !readExact(&data[0], data.size(), &offset, &frameDataSize, sizeof(frameDataSize)) ||
			   !readExact(&data[0], data.size(), &offset, &flags, sizeof(flags)))
				return false;

			bool wanted = rw::strcmp_ci(animName, wantedName) == 0;
			PreviewAnimation anim;
			anim.duration = 0.0f;
			if(wanted)
				anim.nodes.reserve(numNodes);

			for(uint32 nodeIndex = 0; nodeIndex < numNodes; nodeIndex++){
				char nodeName[24];
				uint32 frameType = 0;
				uint32 numKeyFrames = 0;
				uint32 boneId = 0;
				if(!readExact(&data[0], data.size(), &offset, nodeName, sizeof(nodeName)) ||
				   !readExact(&data[0], data.size(), &offset, &frameType, sizeof(frameType)) ||
				   !readExact(&data[0], data.size(), &offset, &numKeyFrames, sizeof(numKeyFrames)) ||
				   !readExact(&data[0], data.size(), &offset, &boneId, sizeof(boneId)))
					return false;

				bool hasTranslation = frameType == 2 || frameType == 4;
				bool compressed = frameType == 3 || frameType == 4;
				if(!compressed)
					return false;

				PreviewAnimNode node = {};
				if(wanted){
					strncpy(node.name, nodeName, sizeof(node.name));
					node.name[sizeof(node.name)-1] = '\0';
					node.nodeId = boneId;
					node.type = PREVIEW_HAS_ROT | (hasTranslation ? PREVIEW_HAS_TRANS : 0);
					node.keyFrames.reserve(numKeyFrames);
				}

				for(uint32 keyIndex = 0; keyIndex < numKeyFrames; keyIndex++){
					int16 qx, qy, qz, qw, dt;
					PreviewAnimKeyFrame keyFrame = {};
					if(!readExact(&data[0], data.size(), &offset, &qx, sizeof(qx)) ||
					   !readExact(&data[0], data.size(), &offset, &qy, sizeof(qy)) ||
					   !readExact(&data[0], data.size(), &offset, &qz, sizeof(qz)) ||
					   !readExact(&data[0], data.size(), &offset, &qw, sizeof(qw)) ||
					   !readExact(&data[0], data.size(), &offset, &dt, sizeof(dt)))
						return false;

					keyFrame.rotation = normalizePreviewQuat(rw::makeQuat(
						qw / 4096.0f,
						qx / 4096.0f,
						qy / 4096.0f,
						qz / 4096.0f
					));
					keyFrame.time = dt / 60.0f;

					if(hasTranslation){
						int16 tx, ty, tz;
						if(!readExact(&data[0], data.size(), &offset, &tx, sizeof(tx)) ||
						   !readExact(&data[0], data.size(), &offset, &ty, sizeof(ty)) ||
						   !readExact(&data[0], data.size(), &offset, &tz, sizeof(tz)))
							return false;
						keyFrame.translation = { tx / 1024.0f, ty / 1024.0f, tz / 1024.0f };
					}

					if(wanted)
						node.keyFrames.push_back(keyFrame);
					anim.duration = max(anim.duration, keyFrame.time);
				}

				if(wanted)
					anim.nodes.push_back(node);
			}

			if(wanted){
				outAnim = anim;
				return true;
			}
		}
		return false;
	}

	int numPackages = 0;
	if(strncmp(root.ident, "ANLF", 4) == 0){
		if(!readRoundedChunkData(data, &offset, root.size, chunkData) || chunkData.size() < sizeof(int32))
			return false;
		numPackages = *(int32*)&chunkData[0];
	}else if(strncmp(root.ident, "ANPK", 4) == 0){
		offset = 0;
		numPackages = 1;
	}else
		return false;

	for(int pkg = 0; pkg < numPackages; pkg++){
		IfpChunkHeader anpk, info;
		if(!readIfpChunkHeader(data, &offset, &anpk) ||
		   !readIfpChunkHeader(data, &offset, &info) ||
		   !readRoundedChunkData(data, &offset, info.size, chunkData) ||
		   chunkData.size() < sizeof(int32))
			return false;

		int numAnimations = *(int32*)&chunkData[0];
		for(int animIndex = 0; animIndex < numAnimations; animIndex++){
			IfpChunkHeader nameChunk, dgan, nodeInfoChunk;
			std::vector<uint8> nameData;
			if(!readIfpChunkHeader(data, &offset, &nameChunk) ||
			   !readRoundedChunkData(data, &offset, nameChunk.size, nameData) ||
			   !readIfpChunkHeader(data, &offset, &dgan) ||
			   !readIfpChunkHeader(data, &offset, &nodeInfoChunk) ||
			   !readRoundedChunkData(data, &offset, nodeInfoChunk.size, chunkData) ||
			   chunkData.size() < 8)
				return false;

			bool wanted = rw::strcmp_ci((const char*)&nameData[0], wantedName) == 0;
			int numNodes = *(int32*)&chunkData[0];
			PreviewAnimation anim;
			anim.duration = 0.0f;
			if(wanted)
				anim.nodes.reserve(numNodes);

			for(int nodeIndex = 0; nodeIndex < numNodes; nodeIndex++){
				IfpChunkHeader cpan, animChunk;
				if(!readIfpChunkHeader(data, &offset, &cpan) ||
				   !readIfpChunkHeader(data, &offset, &animChunk) ||
				   !readRoundedChunkData(data, &offset, animChunk.size, chunkData) ||
				   chunkData.size() < 44)
					return false;

				int numKeyFrames = *(int32*)&chunkData[28];
				int nodeId = *(int32*)&chunkData[40];
				int type = 0;
				PreviewAnimNode node = {};
				if(wanted){
					strncpy(node.name, (const char*)&chunkData[0], sizeof(node.name));
					node.name[sizeof(node.name)-1] = '\0';
					node.nodeId = nodeId;
				}

				if(numKeyFrames <= 0)
					continue;

				IfpChunkHeader keyChunk;
				if(!readIfpChunkHeader(data, &offset, &keyChunk))
					return false;

				if(strncmp(keyChunk.ident, "KR00", 4) == 0)
					type = PREVIEW_HAS_ROT;
				else if(strncmp(keyChunk.ident, "KRT0", 4) == 0)
					type = PREVIEW_HAS_ROT | PREVIEW_HAS_TRANS;
				else if(strncmp(keyChunk.ident, "KRTS", 4) == 0)
					type = PREVIEW_HAS_ROT | PREVIEW_HAS_TRANS | PREVIEW_HAS_SCALE;
				else
					return false;

				if(wanted)
					node.type = type;

				for(int keyIndex = 0; keyIndex < numKeyFrames; keyIndex++){
					PreviewAnimKeyFrame keyFrame = {};
					if(type & PREVIEW_HAS_ROT){
						if(!readExact(&data[0], data.size(), &offset, &keyFrame.rotation, sizeof(keyFrame.rotation)))
							return false;
						keyFrame.rotation.x = -keyFrame.rotation.x;
						keyFrame.rotation.y = -keyFrame.rotation.y;
						keyFrame.rotation.z = -keyFrame.rotation.z;
						keyFrame.rotation = normalizePreviewQuat(keyFrame.rotation);
					}
					if(type & PREVIEW_HAS_TRANS){
						if(!readExact(&data[0], data.size(), &offset, &keyFrame.translation, sizeof(keyFrame.translation)))
							return false;
					}
					if(type & PREVIEW_HAS_SCALE){
						rw::V3d unusedScale;
						if(!readExact(&data[0], data.size(), &offset, &unusedScale, sizeof(unusedScale)))
							return false;
					}
					if(!readExact(&data[0], data.size(), &offset, &keyFrame.time, sizeof(keyFrame.time)))
						return false;
					if(wanted)
						node.keyFrames.push_back(keyFrame);
					anim.duration = max(anim.duration, keyFrame.time);
				}

				if(wanted)
					anim.nodes.push_back(node);
			}

			if(wanted){
				outAnim = anim;
				return true;
			}
		}
	}

	return false;
}

static bool
samplePreviewTrack(const PreviewAnimNode &track, float time, rw::Quat *rotation, rw::V3d *translation)
{
	if(track.keyFrames.empty())
		return false;
	if(track.keyFrames.size() == 1){
		if(rotation) *rotation = track.keyFrames[0].rotation;
		if(translation) *translation = track.keyFrames[0].translation;
		return true;
	}

	const PreviewAnimKeyFrame *prev = &track.keyFrames[0];
	const PreviewAnimKeyFrame *next = &track.keyFrames[track.keyFrames.size()-1];
	for(size_t i = 1; i < track.keyFrames.size(); i++){
		if(time <= track.keyFrames[i].time){
			next = &track.keyFrames[i];
			prev = &track.keyFrames[i-1];
			break;
		}
	}

	float span = next->time - prev->time;
	float t = span > 0.0001f ? (time - prev->time) / span : 0.0f;
	t = clamp(t, 0.0f, 1.0f);
	if(rotation) *rotation = slerp(prev->rotation, next->rotation, t);
	if(translation) *translation = lerpV3d(prev->translation, next->translation, t);
	return true;
}

static bool
loadLogicalPathData(const char *logicalPath, std::vector<uint8> &data)
{
	char sourcePath[1024];
	sourcePath[0] = '\0';
	if(ReadCdImageEntryByLogicalPath(logicalPath, data, sourcePath, sizeof(sourcePath)))
		return true;

	const char *redirect = ModloaderGetSourcePath(logicalPath);
	int size = 0;
	uint8 *buf = nil;
	if(redirect)
		buf = ReadLooseFile(redirect, &size);
	else
		buf = ReadLooseFile(logicalPath, &size);
	if(buf && size > 0){
		data.assign(buf, buf + size);
		free(buf);
		return true;
	}
	if(buf)
		free(buf);

	char gameRoot[1024];
	char absPath[1024];
	if(GetGameRootDirectory(gameRoot, sizeof(gameRoot)) &&
	   BuildPath(absPath, sizeof(absPath), gameRoot, logicalPath)){
		buf = ReadLooseFile(absPath, &size);
		if(buf && size > 0){
			data.assign(buf, buf + size);
			free(buf);
			return true;
		}
		if(buf)
			free(buf);
	}
	return false;
}

static int
ensurePreviewWalkCycleLoaded(const char *groupName)
{
	int existing = findPreviewWalkCycleIndex(groupName);
	if(existing >= 0){
		PreviewWalkCycle &cycle = gPreviewWalkCycles[existing];
		return cycle.loaded ? existing : -1;
	}

	if(!loadPreviewMetadata() && gPreviewAnimGroups.empty())
		return -1;

	int groupIndex = findPreviewAnimGroupIndex(groupName);
	if(groupIndex < 0)
		groupIndex = findPreviewAnimGroupIndex("man");
	if(groupIndex < 0)
		return -1;

	PreviewWalkCycle cycle = {};
	copyPreviewString(cycle.groupName, sizeof(cycle.groupName), gPreviewAnimGroups[groupIndex].groupName);
	copyPreviewString(cycle.ifpFile, sizeof(cycle.ifpFile), gPreviewAnimGroups[groupIndex].ifpFile);
	copyPreviewString(cycle.walkAnim, sizeof(cycle.walkAnim), gPreviewAnimGroups[groupIndex].walkAnim);
	cycle.attempted = true;
	cycle.loaded = false;
	cycle.failureReason[0] = '\0';
	gPreviewWalkCycles.push_back(cycle);

	PreviewWalkCycle &loadedCycle = gPreviewWalkCycles.back();
	char ifpPath[128];
	if(snprintf(ifpPath, sizeof(ifpPath), "anim/%s.ifp", loadedCycle.ifpFile) >= (int)sizeof(ifpPath)){
		snprintf(loadedCycle.failureReason, sizeof(loadedCycle.failureReason), "ifp_path_too_long");
		return -1;
	}

	std::vector<uint8> ifpData;
	if(!loadLogicalPathData(ifpPath, ifpData) ||
	   !loadPreviewAnimation(ifpData, loadedCycle.walkAnim, loadedCycle.animation)){
		snprintf(loadedCycle.failureReason, sizeof(loadedCycle.failureReason), "missing_walk_animation");
		log("SAPaths: failed loading %s/%s for preview walkers\n",
			loadedCycle.ifpFile, loadedCycle.walkAnim);
		return -1;
	}

	loadedCycle.loaded = true;
	return (int)gPreviewWalkCycles.size() - 1;
}

static int
loadPreviewPedAssets(int pedDefIndex)
{
	if(pedDefIndex < 0 || pedDefIndex >= (int)gPreviewPedDefs.size())
		return -1;

	PreviewPedDef &pedDef = gPreviewPedDefs[pedDefIndex];
	int existing = findPreviewPedAssetIndex(pedDef.modelName, pedDef.txdName, pedDef.animGroup);
	if(existing >= 0)
		return gPreviewPedAssetCache[existing].loaded ? existing : -1;

	PreviewPedAssets assets = {};
	copyPreviewString(assets.modelName, sizeof(assets.modelName), pedDef.modelName);
	copyPreviewString(assets.txdName, sizeof(assets.txdName), pedDef.txdName);
	copyPreviewString(assets.animGroup, sizeof(assets.animGroup), pedDef.animGroup);
	assets.walkCycleIndex = ensurePreviewWalkCycleLoaded(assets.animGroup);
	assets.attempted = true;
	assets.loaded = false;
	assets.failureReason[0] = '\0';
	assets.animationDuration = 0.0f;
	gPreviewPedAssetCache.push_back(assets);

	PreviewPedAssets &entry = gPreviewPedAssetCache.back();
	if(entry.walkCycleIndex < 0){
		snprintf(entry.failureReason, sizeof(entry.failureReason), "missing_walk_cycle");
		return -1;
	}

	char txdPath[128];
	char dffPath[128];
	if(snprintf(txdPath, sizeof(txdPath), "models/gta3.img/%s.txd", entry.txdName) >= (int)sizeof(txdPath) ||
	   snprintf(dffPath, sizeof(dffPath), "models/gta3.img/%s.dff", entry.modelName) >= (int)sizeof(dffPath)){
		snprintf(entry.failureReason, sizeof(entry.failureReason), "model_path_too_long");
		return -1;
	}

	std::vector<uint8> txdData;
	if(!loadLogicalPathData(txdPath, txdData)){
		snprintf(entry.failureReason, sizeof(entry.failureReason), "missing_txd");
		log("SAPaths: preview walker TXD not found: %s\n", txdPath);
		return -1;
	}

	rw::StreamMemory txdStream;
	txdStream.open(&txdData[0], txdData.size());
	if(findChunk(&txdStream, rw::ID_TEXDICTIONARY, nil, nil)){
		entry.txd = rw::TexDictionary::streamRead(&txdStream);
		if(entry.txd)
			ConvertTxd(entry.txd);
	}
	txdStream.close();
	if(entry.txd == nil){
		snprintf(entry.failureReason, sizeof(entry.failureReason), "invalid_txd");
		return -1;
	}

	std::vector<uint8> dffData;
	if(!loadLogicalPathData(dffPath, dffData)){
		snprintf(entry.failureReason, sizeof(entry.failureReason), "missing_dff");
		log("SAPaths: preview walker DFF not found: %s\n", dffPath);
		return -1;
	}

	rw::TexDictionary *prevTxd = rw::TexDictionary::getCurrent();
	rw::TexDictionary::setCurrent(entry.txd);
	rw::StreamMemory dffStream;
	dffStream.open(&dffData[0], dffData.size());
	entry.clump = loadRwClumpFromMemory(&dffStream);
	dffStream.close();
	rw::TexDictionary::setCurrent(prevTxd);
	if(entry.clump == nil){
		snprintf(entry.failureReason, sizeof(entry.failureReason), "invalid_dff");
		return -1;
	}

	setupPreviewClump(entry.clump);
	rw::HAnimHierarchy *hier = getHAnimHierarchyFromClump(entry.clump);
	if(hier == nil){
		snprintf(entry.failureReason, sizeof(entry.failureReason), "missing_hanim");
		return -1;
	}

	entry.baseLocalMatrices.resize(hier->numNodes);
	entry.tracksByNode.clear();
	entry.tracksByNode.resize(hier->numNodes);
	for(int i = 0; i < hier->numNodes; i++){
		if(hier->nodeInfo[i].frame)
			entry.baseLocalMatrices[i] = hier->nodeInfo[i].frame->matrix;
		else
			entry.baseLocalMatrices[i].setIdentity();
	}

	PreviewWalkCycle &cycle = gPreviewWalkCycles[entry.walkCycleIndex];
	entry.animationDuration = max(cycle.animation.duration, 0.001f);
	for(size_t i = 0; i < cycle.animation.nodes.size(); i++){
		PreviewAnimNode &track = cycle.animation.nodes[i];
		int index = hier->getIndex(track.nodeId);
		if(index < 0){
			for(int boneIndex = 0; boneIndex < hier->numNodes; boneIndex++){
				rw::Frame *frame = hier->nodeInfo[boneIndex].frame;
				if(frame == nil)
					continue;
				const char *boneName = gta::getNodeName(frame);
				if(boneName && rw::strcmp_ci(boneName, track.name) == 0){
					index = boneIndex;
					break;
				}
			}
		}
		if(index >= 0)
			entry.tracksByNode[index] = track;
	}

	entry.loaded = true;
	log("SAPaths: loaded preview walker assets (%s + %s)\n",
		entry.modelName, cycle.walkAnim);
	return (int)gPreviewPedAssetCache.size() - 1;
}

static int
loadPreviewVehicleAssets(int vehicleDefIndex)
{
	if(vehicleDefIndex < 0 || vehicleDefIndex >= (int)gPreviewVehicleDefs.size())
		return -1;

	PreviewVehicleDef &vehicleDef = gPreviewVehicleDefs[vehicleDefIndex];
	int existing = findPreviewVehicleAssetIndex(vehicleDef.modelName, vehicleDef.txdName);
	if(existing >= 0)
		return gPreviewVehicleAssetCache[existing].loaded ? existing : -1;

	PreviewVehicleAssets assets = {};
	copyPreviewString(assets.modelName, sizeof(assets.modelName), vehicleDef.modelName);
	copyPreviewString(assets.txdName, sizeof(assets.txdName), vehicleDef.txdName);
	assets.heavy = vehicleDef.heavy;
	assets.attempted = true;
	assets.loaded = false;
	assets.failureReason[0] = '\0';
	gPreviewVehicleAssetCache.push_back(assets);

	PreviewVehicleAssets &entry = gPreviewVehicleAssetCache.back();
	char dffPath[128];
	if(snprintf(dffPath, sizeof(dffPath), "models/gta3.img/%s.dff", entry.modelName) >= (int)sizeof(dffPath)){
		snprintf(entry.failureReason, sizeof(entry.failureReason), "model_path_too_long");
		return -1;
	}

	entry.txd = loadPreviewVehicleTextureDictionary(entry.txdName);
	if(entry.txd == nil){
		snprintf(entry.failureReason, sizeof(entry.failureReason), "missing_txd");
		log("SAPaths: preview vehicle TXD not found: %s\n", entry.txdName);
		return -1;
	}

	std::vector<uint8> dffData;
	if(!loadLogicalPathData(dffPath, dffData)){
		snprintf(entry.failureReason, sizeof(entry.failureReason), "missing_dff");
		log("SAPaths: preview vehicle DFF not found: %s\n", dffPath);
		return -1;
	}

	rw::TexDictionary *prevTxd = rw::TexDictionary::getCurrent();
	rw::TexDictionary::setCurrent(entry.txd);
	rw::StreamMemory dffStream;
	dffStream.open(&dffData[0], dffData.size());
	entry.clump = loadRwClumpFromMemory(&dffStream);
	dffStream.close();
	rw::TexDictionary::setCurrent(prevTxd);
	if(entry.clump == nil){
		snprintf(entry.failureReason, sizeof(entry.failureReason), "invalid_dff");
		return -1;
	}

	setupPreviewClump(entry.clump);
	stripPreviewVehicleDamageAtomics(entry.clump);
	stripPreviewVehicleLowDetailAtomics(entry.clump);
	ensurePreviewVehicleWheelAtomics(entry.clump);
	normalizePreviewVehicleMaterialColors(entry.clump);
	buildPreviewVehicleMaterialColorRoles(entry.clump, entry.materialColorRoles);
	entry.loaded = true;
	log("SAPaths: loaded preview vehicle assets (%s)\n", entry.modelName);
	return (int)gPreviewVehicleAssetCache.size() - 1;
}

static rw::V2d
getNaviPosition(const DiskCarPathLink &navi)
{
	rw::V2d pos = { navi.x * NAVI_POS_SCALE, navi.y * NAVI_POS_SCALE };
	return pos;
}

static float
getNaviWidth(const DiskCarPathLink &link)
{
	return link.pathWidth * NAVI_WIDTH_SCALE;
}

static int
getNaviOppositeLanes(const DiskCarPathLink &link)
{
	return link.laneFlags & 0x07;
}

static int
getNaviSameLanes(const DiskCarPathLink &link)
{
	return (link.laneFlags >> 3) & 0x07;
}

static void
setNaviOppositeLanes(DiskCarPathLink &link, int n)
{
	link.laneFlags = (link.laneFlags & ~0x07) | (n & 0x07);
}

static void
setNaviSameLanes(DiskCarPathLink &link, int n)
{
	link.laneFlags = (link.laneFlags & ~0x38) | ((n & 0x07) << 3);
}

static bool
getNaviTrafficLightDir(const DiskCarPathLink &link)
{
	return (link.laneFlags & 0x40) != 0;
}

static void
setNaviTrafficLightDir(DiskCarPathLink &link, bool on)
{
	link.laneFlags = on ? link.laneFlags | 0x40 : link.laneFlags & ~0x40;
}

static int
getNaviTrafficLightState(const DiskCarPathLink &link)
{
	return link.flags & 0x03;
}

static void
setNaviTrafficLightState(DiskCarPathLink &link, int state)
{
	link.flags = (link.flags & ~0x03) | (state & 0x03);
}

static bool
getNaviBridgeLights(const DiskCarPathLink &link)
{
	return (link.flags & 0x04) != 0;
}

static void
setNaviBridgeLights(DiskCarPathLink &link, bool on)
{
	link.flags = on ? link.flags | 0x04 : link.flags & ~0x04;
}

static float
getLaneOffset(const DiskCarPathLink &link)
{
	int opposite = getNaviOppositeLanes(link);
	int same = getNaviSameLanes(link);
	if(opposite == 0)
		return 0.5f - same*0.5f;
	if(same == 0)
		return 0.5f - opposite*0.5f;
	return getNaviWidth(link)/5.4f + 0.5f;
}

static rw::V3d
quadraticBezierV3d(const rw::V3d &a, const rw::V3d &b, const rw::V3d &c, float t)
{
	float u = 1.0f - t;
	return add(add(scale(a, u*u), scale(b, 2.0f*u*t)), scale(c, t*t));
}

static rw::V3d
quadraticBezierTangent(const rw::V3d &a, const rw::V3d &b, const rw::V3d &c, float t)
{
	return add(scale(sub(b, a), 2.0f*(1.0f - t)), scale(sub(c, b), 2.0f*t));
}

static void
buildPreviewTrafficCurveSamples(PreviewTrafficCar &car)
{
	car.curveSampleDistances[0] = 0.0f;
	rw::V3d prev = car.curveP0;
	float cumulative = 0.0f;
	for(int i = 1; i <= PREVIEW_TRAFFIC_CURVE_SAMPLES; i++){
		float t = (float)i / (float)PREVIEW_TRAFFIC_CURVE_SAMPLES;
		rw::V3d p = quadraticBezierV3d(car.curveP0, car.curveP1, car.curveP2, t);
		cumulative += length(sub(p, prev));
		car.curveSampleDistances[i] = cumulative;
		prev = p;
	}
	car.curveLength = max(0.01f, cumulative);
}

static float
getPreviewTrafficCurveTForDistance(const PreviewTrafficCar &car, float distance)
{
	float d = clamp(distance, 0.0f, car.curveLength);
	for(int i = 1; i <= PREVIEW_TRAFFIC_CURVE_SAMPLES; i++){
		float nextDistance = car.curveSampleDistances[i];
		if(d <= nextDistance){
			float prevDistance = car.curveSampleDistances[i - 1];
			float segLen = nextDistance - prevDistance;
			float segT = segLen > 0.0001f ? (d - prevDistance) / segLen : 0.0f;
			return ((float)(i - 1) + segT) / (float)PREVIEW_TRAFFIC_CURVE_SAMPLES;
		}
	}
	return 1.0f;
}

static rw::V3d
getPreviewTrafficTravelDirection(const DiskCarPathLink &navi, bool sameDirection)
{
	rw::V3d dir = { (float)navi.dirX, (float)navi.dirY, 0.0f };
	dir.z = 0.0f;
	float len = length(dir);
	if(len < 0.001f)
		dir = { 0.0f, 1.0f, 0.0f };
	else
		dir = scale(dir, 1.0f/len);
	if(!sameDirection)
		dir = scale(dir, -1.0f);
	return dir;
}

static int
getPreviewTrafficDirectionSign(const NodeAddress &from, const NodeAddress &to)
{
	if(to.areaId < from.areaId)
		return -1;
	if(to.areaId > from.areaId)
		return 1;
	return to.nodeId < from.nodeId ? -1 : 1;
}

static rw::V3d
getPreviewTrafficTravelDirection(const NodeAddress &from, const NodeAddress &to,
                                 const DiskCarPathLink &navi)
{
	rw::V3d dir = { (float)navi.dirX, (float)navi.dirY, 0.0f };
	dir.z = 0.0f;
	float len = length(dir);
	if(len < 0.001f)
		dir = { 0.0f, 1.0f, 0.0f };
	else
		dir = scale(dir, 1.0f/len);
	return scale(dir, (float)getPreviewTrafficDirectionSign(from, to));
}

static bool
isPreviewTrafficSegmentTravelReversed(const NodeAddress &from, const NodeAddress &to,
                                      const DiskCarPathLink &navi)
{
	Node *fromNode = getNode(from);
	Node *toNode = getNode(to);
	if(fromNode == nil || toNode == nil)
		return false;

	rw::V3d pathDir = sub(getNodePosition(*toNode), getNodePosition(*fromNode));
	pathDir.z = 0.0f;
	float pathLen = length(pathDir);
	if(pathLen < 0.001f)
		return false;
	pathDir = scale(pathDir, 1.0f/pathLen);

	rw::V3d travelDir = getPreviewTrafficTravelDirection(from, to, navi);
	return dot(pathDir, travelDir) < 0.0f;
}

static rw::V3d
getPreviewTrafficLaneBasisDirection(const DiskCarPathLink &navi)
{
	rw::V3d dir = { (float)navi.dirX, (float)navi.dirY, 0.0f };
	dir.z = 0.0f;
	float len = length(dir);
	if(len < 0.001f)
		return { 0.0f, 1.0f, 0.0f };
	return scale(dir, 1.0f/len);
}

static rw::V3d
getPreviewTrafficLaneBasisDirection(const NodeAddress &from, const NodeAddress &to,
                                    const DiskCarPathLink &navi)
{
	Node *fromNode = getNode(from);
	Node *toNode = getNode(to);
	if(fromNode && toNode){
		rw::V3d dir = sub(getNodePosition(*toNode), getNodePosition(*fromNode));
		dir.z = 0.0f;
		float len = length(dir);
		if(len >= 0.001f)
			return scale(dir, 1.0f/len);
	}
	return getPreviewTrafficLaneBasisDirection(navi);
}

static float
getPreviewTrafficLaneOffsetUnits(const DiskCarPathLink &navi, bool sameDirection, int laneIndex)
{
	int laneCount = sameDirection ? getNaviSameLanes(navi) : getNaviOppositeLanes(navi);
	if(laneCount <= 0)
		return 0.0f;
	laneIndex = clamp(laneIndex, 0, laneCount - 1);
	return getLaneOffset(navi) + laneIndex;
}

static rw::V3d
getPreviewTrafficCurveControlPoint(const rw::V3d &startPos, const rw::V3d &startDir,
                                   const rw::V3d &endPos, const rw::V3d &endDir, float z)
{
	rw::V3d chord = sub(endPos, startPos);
	chord.z = 0.0f;
	float chordLen = length(chord);
	rw::V3d mid = lerpV3d(startPos, endPos, 0.5f);
	mid.z = z;

	if(chordLen < 0.001f)
		return mid;

	float dirDot = dot(startDir, endDir);
	if(dirDot > 0.985f)
		return mid;

	float denom = startDir.x * endDir.y - startDir.y * endDir.x;
	if(std::fabs(denom) < 0.001f)
		return mid;

	float dx = endPos.x - startPos.x;
	float dy = endPos.y - startPos.y;
	float s = (dx * endDir.y - dy * endDir.x) / denom;
	float u = (dx * startDir.y - dy * startDir.x) / denom;
	if(s < 0.0f || u < 0.0f)
		return mid;

	rw::V3d control = add(startPos, scale(startDir, s));
	control.z = z;
	if(length(sub(control, mid)) > chordLen * 1.5f)
		return mid;
	return control;
}

static bool
getPreviewDirectedVehicleLink(const NodeAddress &from, const NodeAddress &to,
                              CarPathLinkAddress *outNaviAddr, DiskCarPathLink **outNavi,
                              bool *outSameDirection, int *outLaneCount)
{
	if(outNaviAddr)
		*outNaviAddr = { 0xFFFF };
	if(outNavi)
		*outNavi = nil;
	if(outSameDirection)
		*outSameDirection = false;
	if(outLaneCount)
		*outLaneCount = 0;

	Node *node = getNode(from);
	AreaData *area = getArea(from.areaId);
	if(node == nil || area == nil || !area->loaded)
		return false;

	for(int li = 0; li < node->numLinks(); li++){
		int slot = node->raw.baseLinkId + li;
		if(slot < 0 || slot >= (int)area->nodeLinks.size() || slot >= (int)area->naviLinks.size())
			continue;
		if(!(area->nodeLinks[slot].areaId == to.areaId && area->nodeLinks[slot].nodeId == to.nodeId))
			continue;

		CarPathLinkAddress naviAddr = area->naviLinks[slot];
		if(!naviAddr.isValid())
			return false;

		AreaData *naviArea = getArea(naviAddr.areaId());
		if(naviArea == nil || !naviArea->loaded || naviAddr.nodeId() >= naviArea->naviNodes.size())
			return false;

		DiskCarPathLink *navi = &naviArea->naviNodes[naviAddr.nodeId()];
		bool sameDirection =
			navi->attachedAreaId == to.areaId &&
			navi->attachedNodeId == to.nodeId;

		int laneCount = sameDirection ? getNaviSameLanes(*navi) : getNaviOppositeLanes(*navi);
		if(outNaviAddr)
			*outNaviAddr = naviAddr;
		if(outNavi)
			*outNavi = navi;
		if(outSameDirection)
			*outSameDirection = sameDirection;
		if(outLaneCount)
			*outLaneCount = laneCount;
		return laneCount > 0;
	}

	return false;
}

static DiskCarPathLink*
getPreviewTrafficNavi(CarPathLinkAddress addr)
{
	if(!addr.isValid())
		return nil;
	AreaData *naviArea = getArea(addr.areaId());
	if(naviArea == nil || !naviArea->loaded || addr.nodeId() >= naviArea->naviNodes.size())
		return nil;
	return &naviArea->naviNodes[addr.nodeId()];
}

static bool
hasPreviewTrafficLink(const NodeAddress &addr)
{
	Node *node = getNode(addr);
	AreaData *area = getArea(addr.areaId);
	if(node == nil || area == nil || !area->loaded)
		return false;

	for(int li = 0; li < node->numLinks(); li++){
		int slot = node->raw.baseLinkId + li;
		if(slot < 0 || slot >= (int)area->nodeLinks.size())
			continue;
		NodeAddress target = area->nodeLinks[slot];
		int laneCount = 0;
		if(getPreviewDirectedVehicleLink(addr, target, nil, nil, nil, &laneCount) && laneCount > 0)
			return true;
	}
	return false;
}

static rw::V3d
getPreviewTrafficLanePointAtNavi(const NodeAddress &from, const NodeAddress &to,
                                 const DiskCarPathLink &navi, bool sameDirection, int laneIndex, float z)
{
	rw::V3d dir = getPreviewTrafficTravelDirection(from, to, navi);
	float laneOffsetUnits = getPreviewTrafficLaneOffsetUnits(navi, sameDirection, laneIndex);
	float laneOffset = laneOffsetUnits * LANE_WIDTH;
	rw::V2d pos = getNaviPosition(navi);
	return {
		pos.x + laneOffset * dir.y,
		pos.y - laneOffset * dir.x,
		z
	};
}

static void dumpPreviewTrafficCarRouteDebug(const PreviewTrafficCar &car);
static int mapPreviewTrafficLaneIndex(const DiskCarPathLink &fromNavi, bool fromSameDirection, int fromLaneIndex,
                                      const DiskCarPathLink &toNavi, bool toSameDirection);

static void
dumpPreviewTrafficLinkDebug(const NodeAddress &source, const NodeAddress &target,
                            int slot, CarPathLinkAddress naviAddr)
{
	AreaData *area = getArea(source.areaId);
	Node *sourceNode = getNode(source);
	Node *targetNode = getNode(target);
	DiskCarPathLink *navi = getPreviewTrafficNavi(naviAddr);
	if(area == nil || sourceNode == nil || targetNode == nil || navi == nil){
		log("SAPaths debug: can't dump link %d:%d -> %d:%d (missing area/node/navi)\n",
		    source.areaId, source.nodeId, target.areaId, target.nodeId);
		return;
	}

	auto logDir = [](const char *label, const rw::V3d &dir) {
		log("  %s=(%.3f, %.3f, %.3f)\n", label, dir.x, dir.y, dir.z);
	};

	rw::V3d pathDir = sub(getNodePosition(*targetNode), getNodePosition(*sourceNode));
	pathDir.z = 0.0f;
	float pathLen = length(pathDir);
	if(pathLen > 0.001f)
		pathDir = scale(pathDir, 1.0f/pathLen);
	else
		pathDir = { 0.0f, 1.0f, 0.0f };

	bool forwardSame = false;
	int forwardLaneCount = 0;
	getPreviewDirectedVehicleLink(source, target, nil, nil, &forwardSame, &forwardLaneCount);
	bool reverseSame = false;
	int reverseLaneCount = 0;
	getPreviewDirectedVehicleLink(target, source, nil, nil, &reverseSame, &reverseLaneCount);

	rw::V3d currentTravelDir = getPreviewTrafficTravelDirection(source, target, *navi);
	rw::V3d vanillaTravelDir = getPreviewTrafficTravelDirection(*navi, forwardSame);
	rw::V2d naviPos = getNaviPosition(*navi);
	float z = (sourceNode->z() + targetNode->z()) * 0.5f;

	log("SAPaths debug link %d:%d -> %d:%d slot=%d navi=%d:%d\n",
	    source.areaId, source.nodeId, target.areaId, target.nodeId,
	    slot, naviAddr.areaId(), naviAddr.nodeId());
	log("  attachedTo=%d:%d dir=(%d,%d) width=%.3f lanes same=%d opp=%d laneOffsetBase=%.3f\n",
	    navi->attachedAreaId, navi->attachedNodeId, navi->dirX, navi->dirY,
	    getNaviWidth(*navi), getNaviSameLanes(*navi), getNaviOppositeLanes(*navi), getLaneOffset(*navi));
	log("  forwardSame=%d forwardLaneCount=%d reverseSame=%d reverseLaneCount=%d\n",
	    forwardSame ? 1 : 0, forwardLaneCount, reverseSame ? 1 : 0, reverseLaneCount);
	log("  naviPos=(%.3f, %.3f) sourcePos=(%.3f, %.3f) targetPos=(%.3f, %.3f)\n",
	    naviPos.x, naviPos.y, sourceNode->x(), sourceNode->y(), targetNode->x(), targetNode->y());
	logDir("pathDir", pathDir);
	logDir("travelDir.current", currentTravelDir);
	logDir("travelDir.sameFlag", vanillaTravelDir);

	for(int group = 0; group < 2; group++){
		bool sameDirection = group == 0;
		int laneCount = sameDirection ? getNaviSameLanes(*navi) : getNaviOppositeLanes(*navi);
		if(laneCount <= 0)
			continue;
		log("  group=%s laneCount=%d\n", sameDirection ? "same" : "opposite", laneCount);
		for(int lane = 0; lane < laneCount; lane++){
			rw::V3d point = getPreviewTrafficLanePointAtNavi(source, target, *navi, sameDirection, lane, z);
			float offsetUnits = getPreviewTrafficLaneOffsetUnits(*navi, sameDirection, lane);
			log("    lane=%d offsetUnits=%.3f point=(%.3f, %.3f, %.3f)\n",
			    lane, offsetUnits, point.x, point.y, point.z);
		}
	}

	bool foundTouchingCar = false;
	float bestNearestDistance = FLT_MAX;
	size_t bestNearestCarIndex = SIZE_MAX;
	rw::V3d bestNearestPos = { 0.0f, 0.0f, 0.0f };
	rw::V3d bestNearestTangent = { 0.0f, 1.0f, 0.0f };
	for(size_t i = 0; i < gPreviewTrafficCars.size(); i++){
		const PreviewTrafficCar &car = gPreviewTrafficCars[i];
		if(!car.active)
			continue;

		float t = getPreviewTrafficCurveTForDistance(car, car.distanceOnCurve);
		rw::V3d pos = quadraticBezierV3d(car.curveP0, car.curveP1, car.curveP2, t);
		rw::V3d tangent = quadraticBezierTangent(car.curveP0, car.curveP1, car.curveP2, t);
		tangent.z = 0.0f;
		float tangentLen = length(tangent);
		if(tangentLen > 0.001f)
			tangent = scale(tangent, 1.0f/tangentLen);
		else
			tangent = { 0.0f, 1.0f, 0.0f };

		float dx = pos.x - naviPos.x;
		float dy = pos.y - naviPos.y;
		float distanceToNavi = std::sqrt(dx*dx + dy*dy);
		if(distanceToNavi < bestNearestDistance){
			bestNearestDistance = distanceToNavi;
			bestNearestCarIndex = i;
			bestNearestPos = pos;
			bestNearestTangent = tangent;
		}

		bool touches = car.currentLinkAddr.raw == naviAddr.raw || car.nextLinkAddr.raw == naviAddr.raw;
		if(!touches)
			continue;
		foundTouchingCar = true;
		log("  previewCar[%u] prev=%d:%d cur=%d:%d tgt=%d:%d curLink=%d nextLink=%d curSame=%d nextSame=%d curLane=%d/%d nextLane=%d/%d dist=%.3f/%.3f pos=(%.3f, %.3f, %.3f)\n",
		    car.stableId,
		    car.previous.areaId, car.previous.nodeId,
		    car.current.areaId, car.current.nodeId,
		    car.target.areaId, car.target.nodeId,
		    car.currentLinkAddr.raw, car.nextLinkAddr.raw,
		    car.currentSameDirection ? 1 : 0, car.nextSameDirection ? 1 : 0,
		    car.currentLaneIndex, car.currentLaneCount,
		    car.nextLaneIndex, car.nextLaneCount,
		    car.distanceOnCurve, car.curveLength,
		    pos.x, pos.y, pos.z);
		logDir("    tangent", tangent);
		dumpPreviewTrafficCarRouteDebug(car);
	}

	if(bestNearestCarIndex != SIZE_MAX && !foundTouchingCar){
		const PreviewTrafficCar &car = gPreviewTrafficCars[bestNearestCarIndex];
		log("  nearestPreviewCar[%u] distanceToNavi=%.3f prev=%d:%d cur=%d:%d tgt=%d:%d curLink=%d nextLink=%d curSame=%d nextSame=%d curLane=%d/%d nextLane=%d/%d pos=(%.3f, %.3f, %.3f)\n",
		    car.stableId, bestNearestDistance,
		    car.previous.areaId, car.previous.nodeId,
		    car.current.areaId, car.current.nodeId,
		    car.target.areaId, car.target.nodeId,
		    car.currentLinkAddr.raw, car.nextLinkAddr.raw,
		    car.currentSameDirection ? 1 : 0, car.nextSameDirection ? 1 : 0,
		    car.currentLaneIndex, car.currentLaneCount,
		    car.nextLaneIndex, car.nextLaneCount,
		    bestNearestPos.x, bestNearestPos.y, bestNearestPos.z);
		logDir("    tangent", bestNearestTangent);
		dumpPreviewTrafficCarRouteDebug(car);
	}
}

static void
dumpPreviewTrafficCarRouteDebug(const PreviewTrafficCar &car)
{
	if(!car.active){
		log("SAPaths debug previewCar[%u] inactive\n", car.stableId);
		return;
	}

	auto dumpRouteSegment = [&](const char *label,
	                            const NodeAddress &from, const NodeAddress &to,
	                            CarPathLinkAddress naviAddr, bool sameDirection,
	                            int laneIndex, int laneCount) {
		DiskCarPathLink *navi = getPreviewTrafficNavi(naviAddr);
		Node *fromNode = getNode(from);
		Node *toNode = getNode(to);
		if(navi == nil || fromNode == nil || toNode == nil){
			log("  %s missing route data\n", label);
			return;
		}

		rw::V3d travelDirCurrent = getPreviewTrafficTravelDirection(from, to, *navi);
		rw::V3d travelDirSame = getPreviewTrafficTravelDirection(*navi, sameDirection);
		float z = (fromNode->z() + toNode->z()) * 0.5f;
		rw::V3d lanePoint = getPreviewTrafficLanePointAtNavi(from, to, *navi, sameDirection, laneIndex, z);

		log("  %s %d:%d -> %d:%d navi=%d:%d same=%d lane=%d/%d attachedTo=%d:%d dir=(%d,%d) lanes same=%d opp=%d\n",
		    label,
		    from.areaId, from.nodeId, to.areaId, to.nodeId,
		    naviAddr.areaId(), naviAddr.nodeId(),
		    sameDirection ? 1 : 0, laneIndex, laneCount,
		    navi->attachedAreaId, navi->attachedNodeId,
		    navi->dirX, navi->dirY,
		    getNaviSameLanes(*navi), getNaviOppositeLanes(*navi));
		log("    %s travelDir.current=(%.3f, %.3f, %.3f) travelDir.sameFlag=(%.3f, %.3f, %.3f) lanePoint=(%.3f, %.3f, %.3f)\n",
		    label,
		    travelDirCurrent.x, travelDirCurrent.y, travelDirCurrent.z,
		    travelDirSame.x, travelDirSame.y, travelDirSame.z,
		    lanePoint.x, lanePoint.y, lanePoint.z);
	};

	auto dumpOutgoingChoices = [&](void) {
		DiskCarPathLink *currentNavi = getPreviewTrafficNavi(car.currentLinkAddr);
		Node *current = getNode(car.current);
		AreaData *area = getArea(car.current.areaId);
		if(currentNavi == nil || current == nil || area == nil || !area->loaded)
			return;

		rw::V3d wantedDir = getPreviewTrafficTravelDirection(car.previous, car.current, *currentNavi);
		log("  outgoing candidates from %d:%d (wantedDir=(%.3f, %.3f, %.3f))\n",
		    car.current.areaId, car.current.nodeId, wantedDir.x, wantedDir.y, wantedDir.z);
		for(int li = 0; li < current->numLinks(); li++){
			int slot = current->raw.baseLinkId + li;
			if(slot < 0 || slot >= (int)area->nodeLinks.size())
				continue;
			NodeAddress targetAddr = area->nodeLinks[slot];
			Node *target = getNode(targetAddr);
			if(target == nil || target->waterNode())
				continue;

			CarPathLinkAddress naviAddr = { 0xFFFF };
			DiskCarPathLink *navi = nil;
			bool sameDirection = false;
			int laneCount = 0;
			if(!getPreviewDirectedVehicleLink(car.current, targetAddr, &naviAddr, &navi, &sameDirection, &laneCount) || laneCount <= 0)
				continue;

			rw::V3d candidateDir = getPreviewTrafficTravelDirection(car.current, targetAddr, *navi);
			float score = dot(candidateDir, wantedDir);
			float nodeBias = 0.0f;
			if(target){
				rw::V3d nodeDir = sub(getNodePosition(*target), getNodePosition(*current));
				nodeDir.z = 0.0f;
				float nodeLen = length(nodeDir);
				if(nodeLen > 0.001f){
					nodeDir = scale(nodeDir, 1.0f/nodeLen);
					nodeBias = dot(nodeDir, wantedDir) * 0.2f;
					score += nodeBias;
				}
			}
			if(targetAddr.isValid() &&
			   targetAddr.areaId == car.previous.areaId &&
			   targetAddr.nodeId == car.previous.nodeId)
				score -= 1.0f;

			int mappedLane = mapPreviewTrafficLaneIndex(*currentNavi, car.currentSameDirection,
				car.currentLaneIndex, *navi, sameDirection);
			log("    -> %d:%d navi=%d:%d same=%d lanes=%d mappedLane=%d score=%.3f nodeBias=%.3f chosen=%d attachedTo=%d:%d\n",
			    targetAddr.areaId, targetAddr.nodeId,
			    naviAddr.areaId(), naviAddr.nodeId(),
			    sameDirection ? 1 : 0, laneCount, mappedLane,
			    score, nodeBias,
			    (targetAddr.areaId == car.target.areaId && targetAddr.nodeId == car.target.nodeId) ? 1 : 0,
			    navi->attachedAreaId, navi->attachedNodeId);
		}
	};

	auto dumpIncomingChoices = [&](void) {
		Node *current = getNode(car.current);
		AreaData *area = getArea(car.current.areaId);
		Node *outgoing = getNode(car.target);
		if(current == nil || area == nil || !area->loaded)
			return;

		rw::V3d outgoingDir = { 0.0f, 1.0f, 0.0f };
		if(outgoing){
			outgoingDir = sub(getNodePosition(*outgoing), getNodePosition(*current));
			outgoingDir.z = 0.0f;
			float len = length(outgoingDir);
			if(len > 0.001f)
				outgoingDir = scale(outgoingDir, 1.0f/len);
		}

		log("  incoming candidates into %d:%d (outgoingDir=(%.3f, %.3f, %.3f))\n",
		    car.current.areaId, car.current.nodeId, outgoingDir.x, outgoingDir.y, outgoingDir.z);
		for(int li = 0; li < current->numLinks(); li++){
			int slot = current->raw.baseLinkId + li;
			if(slot < 0 || slot >= (int)area->nodeLinks.size())
				continue;
			NodeAddress prevAddr = area->nodeLinks[slot];
			if(prevAddr.areaId == car.target.areaId && prevAddr.nodeId == car.target.nodeId)
				continue;
			Node *previous = getNode(prevAddr);
			if(previous == nil || previous->waterNode())
				continue;

			CarPathLinkAddress naviAddr = { 0xFFFF };
			DiskCarPathLink *navi = nil;
			bool sameDirection = false;
			int laneCount = 0;
			if(!getPreviewDirectedVehicleLink(prevAddr, car.current, &naviAddr, &navi, &sameDirection, &laneCount) || laneCount <= 0)
				continue;

			rw::V3d incomingDir = sub(getNodePosition(*current), getNodePosition(*previous));
			incomingDir.z = 0.0f;
			float len = length(incomingDir);
			if(len < 0.001f)
				continue;
			incomingDir = scale(incomingDir, 1.0f/len);

			float score = dot(incomingDir, outgoingDir);
			int mappedLane = mapPreviewTrafficLaneIndex(*getPreviewTrafficNavi(car.nextLinkAddr), car.nextSameDirection,
				car.nextLaneIndex, *navi, sameDirection);
			log("    <- %d:%d navi=%d:%d same=%d lanes=%d mappedLane=%d score=%.3f chosen=%d attachedTo=%d:%d\n",
			    prevAddr.areaId, prevAddr.nodeId,
			    naviAddr.areaId(), naviAddr.nodeId(),
			    sameDirection ? 1 : 0, laneCount, mappedLane,
			    score,
			    (prevAddr.areaId == car.previous.areaId && prevAddr.nodeId == car.previous.nodeId) ? 1 : 0,
			    navi->attachedAreaId, navi->attachedNodeId);
		}
	};

	log("SAPaths debug previewCar[%u] route prev=%d:%d cur=%d:%d tgt=%d:%d curLink=%d nextLink=%d curSame=%d nextSame=%d curLane=%d/%d nextLane=%d/%d reverse=%d curveLen=%.3f dist=%.3f\n",
	    car.stableId,
	    car.previous.areaId, car.previous.nodeId,
	    car.current.areaId, car.current.nodeId,
	    car.target.areaId, car.target.nodeId,
	    car.currentLinkAddr.raw, car.nextLinkAddr.raw,
	    car.currentSameDirection ? 1 : 0, car.nextSameDirection ? 1 : 0,
	    car.currentLaneIndex, car.currentLaneCount,
	    car.nextLaneIndex, car.nextLaneCount,
	    car.traverseReversed ? 1 : 0,
	    car.curveLength, car.distanceOnCurve);
	dumpRouteSegment("current", car.previous, car.current, car.currentLinkAddr,
	                 car.currentSameDirection, car.currentLaneIndex, car.currentLaneCount);
	dumpRouteSegment("next", car.current, car.target, car.nextLinkAddr,
	                 car.nextSameDirection, car.nextLaneIndex, car.nextLaneCount);
	log("  curveP0=(%.3f, %.3f, %.3f) curveP1=(%.3f, %.3f, %.3f) curveP2=(%.3f, %.3f, %.3f)\n",
	    car.curveP0.x, car.curveP0.y, car.curveP0.z,
	    car.curveP1.x, car.curveP1.y, car.curveP1.z,
	    car.curveP2.x, car.curveP2.y, car.curveP2.z);
	if(car.hasLastRenderedPos)
		log("  lastMotionDir=(%.3f, %.3f, %.3f)\n",
		    car.lastMotionDir.x, car.lastMotionDir.y, car.lastMotionDir.z);
	dumpOutgoingChoices();
	dumpIncomingChoices();
}

static int
mapPreviewTrafficLaneIndex(const DiskCarPathLink &fromNavi, bool fromSameDirection, int fromLaneIndex,
                           const DiskCarPathLink &toNavi, bool toSameDirection)
{
	(void)fromNavi;
	(void)fromSameDirection;

	int toLaneCount = toSameDirection ? getNaviSameLanes(toNavi) : getNaviOppositeLanes(toNavi);
	if(toLaneCount <= 0)
		return 0;
	return clamp(fromLaneIndex, 0, toLaneCount - 1);
}

static bool
getPreviewTrafficLaneEndpoint(const NodeAddress &from, const NodeAddress &to,
                              int laneIndex, int laneCountHint, bool atTarget,
                              rw::V3d *outPoint)
{
	Node *fromNode = getNode(from);
	Node *toNode = getNode(to);
	if(fromNode == nil || toNode == nil || outPoint == nil)
		return false;

	CarPathLinkAddress naviAddr = { 0xFFFF };
	DiskCarPathLink *navi = nil;
	bool sameDirection = false;
	int laneCount = 0;
	float z = atTarget ? toNode->z() : fromNode->z();

	if(!atTarget){
		if(!getPreviewDirectedVehicleLink(from, to, &naviAddr, &navi, &sameDirection, &laneCount) || navi == nil || laneCount <= 0)
			return false;
		laneIndex = clamp(laneIndex, 0, laneCount - 1);
	}else{
		bool reverseSameDirection = false;
		if(!getPreviewDirectedVehicleLink(to, from, &naviAddr, &navi, &reverseSameDirection, nil) || navi == nil)
			return false;
		sameDirection = !reverseSameDirection;
		laneCount = sameDirection ? getNaviSameLanes(*navi) : getNaviOppositeLanes(*navi);
		if(laneCount <= 0)
			return false;
		if(laneCountHint > 0){
			CarPathLinkAddress fromNaviAddr = { 0xFFFF };
			DiskCarPathLink *fromNavi = nil;
			bool fromSameDirection = false;
			int fromLaneCount = 0;
			if(getPreviewDirectedVehicleLink(from, to, &fromNaviAddr, &fromNavi, &fromSameDirection, &fromLaneCount) &&
			   fromNavi && fromLaneCount > 0)
				laneIndex = mapPreviewTrafficLaneIndex(*fromNavi, fromSameDirection, laneIndex, *navi, sameDirection);
			else
				laneIndex = clamp((int)(((laneIndex + 0.5f) / laneCountHint) * laneCount), 0, laneCount - 1);
		}else
			laneIndex = clamp(laneIndex, 0, laneCount - 1);
	}

	*outPoint = getPreviewTrafficLanePointAtNavi(from, to, *navi, sameDirection, laneIndex, z);
	return true;
}

static bool
getPreviewTrafficCarApproxPosition(const PreviewTrafficCar &car, rw::V3d *outPos)
{
	if(outPos == nil || !car.active)
		return false;
	float t = getPreviewTrafficCurveTForDistance(car, car.distanceOnCurve);
	*outPos = quadraticBezierV3d(car.curveP0, car.curveP1, car.curveP2, t);
	return true;
}

static bool
getPreviewParkedCarApproxPosition(const PreviewParkedCar &car, rw::V3d *outPos)
{
	if(outPos == nil || !car.active)
		return false;
	*outPos = car.pos;
	return true;
}

static bool
isPreviewVehiclePositionOccupied(const rw::V3d &pos, float minSeparation)
{
	for(size_t i = 0; i < gPreviewTrafficCars.size(); i++){
		rw::V3d carPos;
		if(!getPreviewTrafficCarApproxPosition(gPreviewTrafficCars[i], &carPos))
			continue;
		if(length(sub(carPos, pos)) < minSeparation)
			return true;
	}
	for(size_t i = 0; i < gPreviewParkedCars.size(); i++){
		rw::V3d carPos;
		if(!getPreviewParkedCarApproxPosition(gPreviewParkedCars[i], &carPos))
			continue;
		if(length(sub(carPos, pos)) < minSeparation)
			return true;
	}
	return false;
}

enum {
	PREVIEW_LIGHT_GREEN = 0,
	PREVIEW_LIGHT_YELLOW = 1,
	PREVIEW_LIGHT_RED = 2,
};

static int
getPreviewTrafficLightCycleState(int trafficLightDir)
{
	int cycleTime = ((int)gPreviewTrafficLightTimeMs / 2) & 16383;
	if(trafficLightDir == 1){
		if(cycleTime < 5000)
			return PREVIEW_LIGHT_GREEN;
		if(cycleTime < 6000)
			return PREVIEW_LIGHT_YELLOW;
		return PREVIEW_LIGHT_RED;
	}
	if(trafficLightDir == 2){
		if(cycleTime < 6000)
			return PREVIEW_LIGHT_RED;
		if(cycleTime < 11000)
			return PREVIEW_LIGHT_GREEN;
		if(cycleTime < 12000)
			return PREVIEW_LIGHT_YELLOW;
		return PREVIEW_LIGHT_RED;
	}
	return PREVIEW_LIGHT_GREEN;
}

static bool
shouldPreviewTrafficCarStopForLight(const PreviewTrafficCar &car)
{
	DiskCarPathLink *nextNavi = getPreviewTrafficNavi(car.nextLinkAddr);
	if(nextNavi == nil)
		return false;

	int trafficLightState = getNaviTrafficLightState(*nextNavi);
	if(trafficLightState == 0)
		return false;

	bool attachedToCurrent =
		nextNavi->attachedAreaId == car.current.areaId &&
		nextNavi->attachedNodeId == car.current.nodeId;
	if(attachedToCurrent != getNaviTrafficLightDir(*nextNavi))
		return false;

	return getPreviewTrafficLightCycleState(trafficLightState) != PREVIEW_LIGHT_GREEN;
}

static float
getPreviewTrafficNodeCrossDistance(const PreviewTrafficCar &car)
{
	Node *current = getNode(car.current);
	if(current == nil)
		return car.curveLength;

	rw::V3d nodePos = getNodePosition(*current);
	float bestDistance = car.curveLength;
	float bestNodeDist = 999999.0f;
	for(int i = 0; i <= PREVIEW_TRAFFIC_CURVE_SAMPLES; i++){
		float t = (float)i / (float)PREVIEW_TRAFFIC_CURVE_SAMPLES;
		rw::V3d sample = quadraticBezierV3d(car.curveP0, car.curveP1, car.curveP2, t);
		float nodeDist = length(sub(sample, nodePos));
		if(nodeDist < bestNodeDist){
			bestNodeDist = nodeDist;
			bestDistance = car.curveSampleDistances[i];
		}
	}

	return bestDistance;
}

static float
getPreviewTrafficLightStopDistance(const PreviewTrafficCar &car)
{
	float stopBackoff = max(2.5f, car.heavy ? 4.5f : 3.5f);
	return clamp(getPreviewTrafficNodeCrossDistance(car) - stopBackoff, 0.0f, car.curveLength);
}

static bool
isPreviewTrafficCarDirectlyAheadOnRoute(const PreviewTrafficCar &car, const PreviewTrafficCar &other, float *outDistanceAhead)
{
	if(outDistanceAhead)
		*outDistanceAhead = 0.0f;
	if(!car.active || !other.active)
		return false;
	if(&car == &other)
		return false;

	float distanceAhead = 0.0f;
	if(other.current.areaId == car.current.areaId &&
	   other.current.nodeId == car.current.nodeId &&
	   other.target.areaId == car.target.areaId &&
	   other.target.nodeId == car.target.nodeId &&
	   other.nextLinkAddr.raw == car.nextLinkAddr.raw &&
	   other.nextLaneIndex == car.nextLaneIndex){
		distanceAhead = other.distanceOnCurve - car.distanceOnCurve;
	}else if(other.previous.areaId == car.current.areaId &&
	         other.previous.nodeId == car.current.nodeId &&
	         other.current.areaId == car.target.areaId &&
	         other.current.nodeId == car.target.nodeId &&
	         other.currentLinkAddr.raw == car.nextLinkAddr.raw &&
	         other.currentLaneIndex == car.nextLaneIndex){
		distanceAhead = (car.curveLength - car.distanceOnCurve) + other.distanceOnCurve;
	}else{
		return false;
	}

	if(distanceAhead <= 0.001f)
		return false;
	if(outDistanceAhead)
		*outDistanceAhead = distanceAhead;
	return true;
}

static float
getPreviewTrafficSpacingStopDistance(const PreviewTrafficCar &car)
{
	float wantedGap = car.heavy ? 9.0f : 7.0f;
	float bestDistanceAhead = 999999.0f;
	bool foundAhead = false;
	for(size_t i = 0; i < gPreviewTrafficCars.size(); i++){
		float distanceAhead = 0.0f;
		if(!isPreviewTrafficCarDirectlyAheadOnRoute(car, gPreviewTrafficCars[i], &distanceAhead))
			continue;
		if(distanceAhead < bestDistanceAhead){
			bestDistanceAhead = distanceAhead;
			foundAhead = true;
		}
	}

	if(!foundAhead)
		return car.curveLength;
	return clamp(car.distanceOnCurve + max(0.0f, bestDistanceAhead - wantedGap), 0.0f, car.curveLength);
}

static bool
arePreviewTrafficCarsInConflictingJunctionState(const PreviewTrafficCar &car, const PreviewTrafficCar &other)
{
	if(!car.active || !other.active || &car == &other)
		return false;
	if(car.current.areaId != other.current.areaId || car.current.nodeId != other.current.nodeId)
		return false;
	if(car.nextLinkAddr.raw == other.nextLinkAddr.raw && car.nextLaneIndex == other.nextLaneIndex)
		return false;
	if(car.previous.areaId == other.previous.areaId &&
	   car.previous.nodeId == other.previous.nodeId &&
	   car.target.areaId == other.target.areaId &&
	   car.target.nodeId == other.target.nodeId)
		return false;
	return true;
}

static bool
shouldPreviewTrafficCarYieldAtJunction(const PreviewTrafficCar &car)
{
	DiskCarPathLink *nextNavi = getPreviewTrafficNavi(car.nextLinkAddr);
	if(nextNavi && getNaviTrafficLightState(*nextNavi) != 0)
		return false;

	float ownCrossDistance = getPreviewTrafficNodeCrossDistance(car);
	float ownDistanceToNode = ownCrossDistance - car.distanceOnCurve;
	if(ownDistanceToNode < -3.0f || ownDistanceToNode > 12.0f)
		return false;

	for(size_t i = 0; i < gPreviewTrafficCars.size(); i++){
		const PreviewTrafficCar &other = gPreviewTrafficCars[i];
		if(!arePreviewTrafficCarsInConflictingJunctionState(car, other))
			continue;

		float otherCrossDistance = getPreviewTrafficNodeCrossDistance(other);
		float otherDistanceToNode = otherCrossDistance - other.distanceOnCurve;
		if(otherDistanceToNode < -3.0f || otherDistanceToNode > 12.0f)
			continue;

		if(otherDistanceToNode + 0.75f < ownDistanceToNode)
			return true;
		if(std::fabs(otherDistanceToNode - ownDistanceToNode) <= 0.75f &&
		   other.stableId < car.stableId)
			return true;
	}

	return false;
}

static float
getPreviewTrafficYieldStopDistance(const PreviewTrafficCar &car)
{
	float stopBackoff = max(2.0f, car.heavy ? 4.0f : 3.0f);
	return clamp(getPreviewTrafficNodeCrossDistance(car) - stopBackoff, 0.0f, car.curveLength);
}

static bool
pickPreviewTrafficSpawnNode(uint32 &seed, NodeAddress *outAddr, float preferredDistance, float minSeparation)
{
	std::vector<NodeAddress> candidates;
	std::vector<float> weights;
	candidates.reserve(256);
	weights.reserve(256);

	for(int areaId = 0; areaId < NUM_PATH_AREAS; areaId++){
		AreaData &area = gAreas[areaId];
		if(!area.loaded || !areaInDrawRange(areaId))
			continue;
		for(uint32 i = 0; i < area.numVehicleNodes; i++){
			Node &node = area.nodes[i];
			if(node.numLinks() <= 0 || node.waterNode() || node.isSwitchedOff())
				continue;
			if(!hasPreviewTrafficLink(node.address()))
				continue;
			rw::V3d pos = getNodePosition(node);
			float cameraDistance = TheCamera.distanceTo(pos);
			if(cameraDistance > PATH_DRAW_DIST*0.85f)
				continue;

			if(isPreviewVehiclePositionOccupied(pos, minSeparation))
				continue;

			candidates.push_back(node.address());
			const PreviewPopcycleSlot *slot = getPreviewPopcycleSlotForPosition(pos, nil);
			float weight = (float)max(1, node.spawnProbability() + 1);
			if(slot)
				weight *= max(1.0f, (float)slot->maxCars);
			float distanceBias = 1.0f;
			if(preferredDistance > 0.0f){
				float distanceDelta = std::fabs(cameraDistance - preferredDistance);
				float normalizedDelta = distanceDelta / max(40.0f, PATH_DRAW_DIST*0.35f);
				distanceBias = 1.25f / (1.0f + normalizedDelta*normalizedDelta*4.0f);
			}
			weight *= distanceBias;
			weights.push_back(weight);
		}
	}

	if(candidates.empty())
		return false;

	float totalWeight = 0.0f;
	for(size_t i = 0; i < weights.size(); i++)
		totalWeight += weights[i];

	float roll = previewSeedFloat01(seed) * totalWeight;
	for(size_t i = 0; i < candidates.size(); i++){
		if(roll < weights[i]){
			*outAddr = candidates[i];
			return true;
		}
		roll -= weights[i];
	}

	*outAddr = candidates.back();
	return true;
}

struct PreviewParkedSpawnState {
	NodeAddress current;
	NodeAddress target;
	CarPathLinkAddress naviAddr;
	bool sameDirection;
	float sideSign;
	rw::V3d pos;
	rw::V3d forward;
};

static bool
pickPreviewParkedSpawnState(uint32 &seed, PreviewParkedSpawnState *outState, float preferredDistance, float minSeparation)
{
	if(outState == nil)
		return false;

	std::vector<PreviewParkedSpawnState> candidates;
	std::vector<float> weights;
	for(int areaId = 0; areaId < NUM_PATH_AREAS; areaId++){
		AreaData *area = getArea(areaId);
		if(area == nil || !area->loaded || !areaInDrawRange(areaId))
			continue;

		for(uint32 ni = 0; ni < area->numVehicleNodes && ni < area->nodes.size(); ni++){
			Node &node = area->nodes[ni];
			if(node.numLinks() <= 0 || node.waterNode() || node.width() < 5.0f)
				continue;

			for(int li = 0; li < node.numLinks(); li++){
				int slot = node.raw.baseLinkId + li;
				if(slot < 0 || slot >= (int)area->nodeLinks.size())
					continue;
				NodeAddress targetAddr = area->nodeLinks[slot];
				Node *target = getNode(targetAddr);
				if(target == nil || target->waterNode())
					continue;

				CarPathLinkAddress naviAddr = { 0xFFFF };
				DiskCarPathLink *navi = nil;
				bool sameDirection = false;
				int laneCount = 0;
				if(!getPreviewDirectedVehicleLink(node.address(), targetAddr, &naviAddr, &navi, &sameDirection, &laneCount) ||
				   navi == nil || laneCount <= 0)
					continue;
				bool roadCross = slot < (int)area->intersections.size() &&
					getRoadCross(area->intersections[slot]);

				rw::V3d baseDir = getPreviewTrafficTravelDirection(node.address(), targetAddr, *navi);
				rw::V3d right = cross(baseDir, { 0.0f, 0.0f, 1.0f });
				float rightLen = length(right);
				if(rightLen < 0.001f)
					continue;
				right = scale(right, 1.0f/rightLen);

				rw::V3d startPos = getNodePosition(node);
				rw::V3d endPos = getNodePosition(*target);
				rw::V3d segment = sub(endPos, startPos);
				segment.z = 0.0f;
				float segmentLength = length(segment);
				if(segmentLength < 6.0f)
					continue;
				float nodeWidth = node.width();
				float targetWidth = target->width();
				float avgWidth = max(5.0f, (nodeWidth + targetWidth)*0.5f);
				int sameLanes = getNaviSameLanes(*navi);
				int oppositeLanes = getNaviOppositeLanes(*navi);
				int totalLanes = max(1, sameLanes + oppositeLanes);
				float carriageHalfWidth = max(avgWidth*0.45f, totalLanes * (LANE_WIDTH*0.5f));
				float curbClearance = node.notHighway() ? 1.2f : 2.0f;
				float shoulderOffset = carriageHalfWidth + curbClearance;
				float minProgress = 0.20f;
				float maxProgress = 0.80f;
				for(int sideIdx = 0; sideIdx < 2; sideIdx++){
					float sideSign = sideIdx == 0 ? 1.0f : -1.0f;
					float progress = minProgress + previewSeedFloat01(seed) * (maxProgress - minProgress);
					rw::V3d center = lerpV3d(startPos, endPos, progress);
					rw::V3d forward = baseDir;
					if(sideSign < 0.0f && oppositeLanes > 0)
						forward = scale(baseDir, -1.0f);
					rw::V3d pos = {
						center.x + right.x * shoulderOffset * sideSign,
						center.y + right.y * shoulderOffset * sideSign,
						center.z
					};
					float cameraDistance = TheCamera.distanceTo(pos);
					if(cameraDistance > PATH_DRAW_DIST*0.90f)
						continue;
					if(isPreviewVehiclePositionOccupied(pos, minSeparation))
						continue;

					PreviewParkedSpawnState state = {};
					state.current = node.address();
					state.target = targetAddr;
					state.naviAddr = naviAddr;
					state.sameDirection = sameDirection;
					state.sideSign = sideSign;
					state.pos = pos;
					state.forward = forward;
					candidates.push_back(state);

					float weight = (float)max(1, node.spawnProbability() + 1);
					if(node.notHighway())
						weight *= 1.9f;
					if(node.highway())
						weight *= 0.20f;
					if(roadCross)
						weight *= 0.18f;
					if(node.numLinks() > 2 || target->numLinks() > 2)
						weight *= 0.30f;
					if(node.width() < 7.0f)
						weight *= 1.25f;
					if(totalLanes <= 2)
						weight *= 1.20f;
					else if(totalLanes >= 4)
						weight *= 0.70f;
					if(segmentLength > 40.0f)
						weight *= 1.10f;
					if(preferredDistance > 0.0f){
						float distanceDelta = std::fabs(cameraDistance - preferredDistance);
						float normalizedDelta = distanceDelta / max(35.0f, PATH_DRAW_DIST*0.30f);
						weight *= 1.25f / (1.0f + normalizedDelta*normalizedDelta*4.0f);
					}
					weights.push_back(weight);
				}
			}
		}
	}

	if(candidates.empty())
		return false;

	float totalWeight = 0.0f;
	for(size_t i = 0; i < weights.size(); i++)
		totalWeight += weights[i];
	if(totalWeight <= 0.001f){
		int index = clamp((int)(previewSeedFloat01(seed) * candidates.size()), 0, (int)candidates.size() - 1);
		*outState = candidates[index];
		return true;
	}

	float roll = previewSeedFloat01(seed) * totalWeight;
	for(size_t i = 0; i < candidates.size(); i++){
		if(roll < weights[i]){
			*outState = candidates[i];
			return true;
		}
		roll -= weights[i];
	}

	*outState = candidates.back();
	return true;
}

static bool
chooseNextPreviewTrafficLink(const NodeAddress &currentAddr, const NodeAddress &previousAddr, uint32 &seed,
                             NodeAddress *outTarget, CarPathLinkAddress *outNaviAddr, int *outLaneIndex,
                             int *outLaneCount = nil, bool *outSameDirection = nil,
                             int previousLaneIndex = -1, int previousLaneCount = 0)
{
	Node *current = getNode(currentAddr);
	AreaData *area = getArea(currentAddr.areaId);
	if(current == nil || area == nil || !area->loaded || current->numLinks() <= 0)
		return false;

	rw::V3d wantedDir = { 0.0f, 1.0f, 0.0f };
	if(previousAddr.isValid()){
		Node *previous = getNode(previousAddr);
		if(previous){
			wantedDir = sub(getNodePosition(*current), getNodePosition(*previous));
			wantedDir.z = 0.0f;
			float wantedLen = length(wantedDir);
			if(wantedLen > 0.001f)
				wantedDir = scale(wantedDir, 1.0f/wantedLen);
		}
	}else{
		float angle = previewSeedFloat01(seed) * (PREVIEW_PI*2.0f);
		wantedDir = { std::sin(angle), std::cos(angle), 0.0f };
	}

	float bestScore = -999999.0f;
	bool found = false;
	for(int li = 0; li < current->numLinks(); li++){
		int slot = current->raw.baseLinkId + li;
		if(slot < 0 || slot >= (int)area->nodeLinks.size())
			continue;
		NodeAddress targetAddr = area->nodeLinks[slot];
		Node *target = getNode(targetAddr);
		if(target == nil || target->waterNode())
			continue;

		CarPathLinkAddress naviAddr = { 0xFFFF };
		DiskCarPathLink *navi = nil;
		bool sameDirection = false;
		int laneCount = 0;
		if(!getPreviewDirectedVehicleLink(currentAddr, targetAddr, &naviAddr, &navi, &sameDirection, &laneCount) || laneCount <= 0)
			continue;

		rw::V3d dir = sub(getNodePosition(*target), getNodePosition(*current));
		dir.z = 0.0f;
		float len = length(dir);
		if(len < 0.001f)
			continue;
		dir = scale(dir, 1.0f/len);

		float score = dot(dir, wantedDir);
		if(targetAddr.isValid() &&
		   targetAddr.areaId == previousAddr.areaId &&
		   targetAddr.nodeId == previousAddr.nodeId)
			score -= 0.75f;
		score += previewSeedFloat01(seed) * 0.2f;
		score += laneCount * 0.05f;

		if(!found || score > bestScore){
			bestScore = score;
			*outTarget = targetAddr;
			*outNaviAddr = naviAddr;
			if(outLaneCount)
				*outLaneCount = laneCount;
			if(outSameDirection)
				*outSameDirection = sameDirection;
			if(previousLaneIndex >= 0 && previousLaneCount > 0){
				CarPathLinkAddress previousNaviAddr = { 0xFFFF };
				DiskCarPathLink *previousNavi = nil;
				bool previousSameDirection = false;
				int resolvedPreviousLaneCount = 0;
				if(getPreviewDirectedVehicleLink(previousAddr, currentAddr, &previousNaviAddr, &previousNavi,
					&previousSameDirection, &resolvedPreviousLaneCount) &&
				   previousNavi && resolvedPreviousLaneCount > 0)
					*outLaneIndex = mapPreviewTrafficLaneIndex(*previousNavi, previousSameDirection, previousLaneIndex, *navi, sameDirection);
				else
					*outLaneIndex = clamp((int)(((previousLaneIndex + 0.5f) / previousLaneCount) * laneCount), 0, laneCount - 1);
			}else
				*outLaneIndex = clamp((int)(previewSeedFloat01(seed) * laneCount), 0, laneCount - 1);
			found = true;
		}
	}

	return found;
}

static bool
chooseNextPreviewTrafficLinkFromCurrentLink(const NodeAddress &currentAddr, const NodeAddress &previousAddr,
                                            CarPathLinkAddress currentLinkAddr, bool currentSameDirection,
                                            uint32 &seed, NodeAddress *outTarget, CarPathLinkAddress *outNaviAddr,
                                            int *outLaneIndex, int *outLaneCount = nil, bool *outSameDirection = nil,
                                            int previousLaneIndex = -1, int previousLaneCount = 0)
{
	DiskCarPathLink *currentNavi = getPreviewTrafficNavi(currentLinkAddr);
	if(currentNavi == nil)
		return chooseNextPreviewTrafficLink(currentAddr, previousAddr, seed, outTarget, outNaviAddr,
			outLaneIndex, outLaneCount, outSameDirection, previousLaneIndex, previousLaneCount);

	Node *current = getNode(currentAddr);
	AreaData *area = getArea(currentAddr.areaId);
	if(current == nil || area == nil || !area->loaded || current->numLinks() <= 0)
		return false;

	rw::V3d wantedDir = getPreviewTrafficTravelDirection(previousAddr, currentAddr, *currentNavi);
	float bestScore = -999999.0f;
	bool found = false;
	for(int li = 0; li < current->numLinks(); li++){
		int slot = current->raw.baseLinkId + li;
		if(slot < 0 || slot >= (int)area->nodeLinks.size())
			continue;
		NodeAddress targetAddr = area->nodeLinks[slot];
		Node *target = getNode(targetAddr);
		if(target == nil || target->waterNode())
			continue;

		CarPathLinkAddress naviAddr = { 0xFFFF };
		DiskCarPathLink *navi = nil;
		bool sameDirection = false;
		int laneCount = 0;
		if(!getPreviewDirectedVehicleLink(currentAddr, targetAddr, &naviAddr, &navi, &sameDirection, &laneCount) || laneCount <= 0)
			continue;

		rw::V3d candidateDir = getPreviewTrafficTravelDirection(currentAddr, targetAddr, *navi);
		float score = dot(candidateDir, wantedDir);

		if(targetAddr.isValid() &&
		   targetAddr.areaId == previousAddr.areaId &&
		   targetAddr.nodeId == previousAddr.nodeId)
			score -= 1.0f;

		Node *previous = getNode(previousAddr);
		if(previous && target){
			rw::V3d nodeDir = sub(getNodePosition(*target), getNodePosition(*current));
			nodeDir.z = 0.0f;
			float nodeLen = length(nodeDir);
			if(nodeLen > 0.001f){
				nodeDir = scale(nodeDir, 1.0f/nodeLen);
				score += dot(nodeDir, wantedDir) * 0.2f;
			}
		}

		score += previewSeedFloat01(seed) * 0.12f;
		score += laneCount * 0.05f;

		if(!found || score > bestScore){
			bestScore = score;
			*outTarget = targetAddr;
			*outNaviAddr = naviAddr;
			if(outLaneCount)
				*outLaneCount = laneCount;
			if(outSameDirection)
				*outSameDirection = sameDirection;
			if(previousLaneIndex >= 0 && previousLaneCount > 0)
				*outLaneIndex = mapPreviewTrafficLaneIndex(*currentNavi, currentSameDirection, previousLaneIndex, *navi, sameDirection);
			else
				*outLaneIndex = clamp((int)(previewSeedFloat01(seed) * laneCount), 0, laneCount - 1);
			found = true;
		}
	}

	return found;
}

static bool
choosePreviewIncomingTrafficLink(const NodeAddress &currentAddr, const NodeAddress &outgoingTargetAddr,
                                 uint32 &seed, NodeAddress *outPrevious,
                                 CarPathLinkAddress *outNaviAddr, int *outLaneIndex,
                                 int desiredLaneIndex = -1, int desiredLaneCount = 0,
                                 int *outLaneCount = nil, bool *outSameDirection = nil)
{
	Node *current = getNode(currentAddr);
	AreaData *area = getArea(currentAddr.areaId);
	Node *outgoing = getNode(outgoingTargetAddr);
	if(current == nil || area == nil || !area->loaded || current->numLinks() <= 0)
		return false;

	rw::V3d outgoingDir = { 0.0f, 1.0f, 0.0f };
	if(outgoing){
		outgoingDir = sub(getNodePosition(*outgoing), getNodePosition(*current));
		outgoingDir.z = 0.0f;
		float len = length(outgoingDir);
		if(len > 0.001f)
			outgoingDir = scale(outgoingDir, 1.0f/len);
	}

	float bestScore = -999999.0f;
	bool found = false;
	for(int li = 0; li < current->numLinks(); li++){
		int slot = current->raw.baseLinkId + li;
		if(slot < 0 || slot >= (int)area->nodeLinks.size())
			continue;

		NodeAddress prevAddr = area->nodeLinks[slot];
		if(prevAddr.areaId == outgoingTargetAddr.areaId && prevAddr.nodeId == outgoingTargetAddr.nodeId)
			continue;

		Node *previous = getNode(prevAddr);
		if(previous == nil || previous->waterNode())
			continue;

		CarPathLinkAddress naviAddr = { 0xFFFF };
		DiskCarPathLink *navi = nil;
		bool sameDirection = false;
		int laneCount = 0;
		if(!getPreviewDirectedVehicleLink(prevAddr, currentAddr, &naviAddr, &navi, &sameDirection, &laneCount) || laneCount <= 0)
			continue;

		rw::V3d incomingDir = sub(getNodePosition(*current), getNodePosition(*previous));
		incomingDir.z = 0.0f;
		float len = length(incomingDir);
		if(len < 0.001f)
			continue;
		incomingDir = scale(incomingDir, 1.0f/len);

		float score = dot(incomingDir, outgoingDir);
		score += previewSeedFloat01(seed) * 0.15f;
		score += laneCount * 0.05f;

		if(!found || score > bestScore){
			bestScore = score;
			*outPrevious = prevAddr;
			*outNaviAddr = naviAddr;
			if(outLaneCount)
				*outLaneCount = laneCount;
			if(outSameDirection)
				*outSameDirection = sameDirection;
			if(desiredLaneIndex >= 0 && desiredLaneCount > 0){
				CarPathLinkAddress desiredNaviAddr = { 0xFFFF };
				DiskCarPathLink *desiredNavi = nil;
				bool desiredSameDirection = false;
				int resolvedDesiredLaneCount = 0;
				if(getPreviewDirectedVehicleLink(currentAddr, outgoingTargetAddr, &desiredNaviAddr, &desiredNavi,
					&desiredSameDirection, &resolvedDesiredLaneCount) &&
				   desiredNavi && resolvedDesiredLaneCount > 0)
					*outLaneIndex = mapPreviewTrafficLaneIndex(*desiredNavi, desiredSameDirection, desiredLaneIndex, *navi, sameDirection);
				else
					*outLaneIndex = clamp((int)(((desiredLaneIndex + 0.5f) / desiredLaneCount) * laneCount), 0, laneCount - 1);
			}else
				*outLaneIndex = clamp((int)(previewSeedFloat01(seed) * laneCount), 0, laneCount - 1);
			found = true;
		}
	}

	return found;
}

static bool
getRoadCross(uint8 info)
{
	return (info & 0x01) != 0;
}

static void
setRoadCross(uint8 &info, bool on)
{
	info = on ? info | 0x01 : info & ~0x01;
}

static bool
getPedTrafficLight(uint8 info)
{
	return (info & 0x02) != 0;
}

static void
setPedTrafficLight(uint8 &info, bool on)
{
	info = on ? info | 0x02 : info & ~0x02;
}

static bool
isShortReadFailure(const char *reason)
{
	return strcmp(reason, "short_header") == 0 ||
	       strcmp(reason, "short_nodes") == 0 ||
	       strcmp(reason, "short_navis") == 0 ||
	       strcmp(reason, "short_links") == 0;
}

static const char*
loadSourceModeLabel(LoadSourceMode mode)
{
	switch(mode){
	case LOAD_SOURCE_IMG: return "IMG archive";
	case LOAD_SOURCE_LOOSE: return "data/Paths";
	case LOAD_SOURCE_AUTO:
	default: return "Auto";
	}
}

static const char*
activeLoadSourceLabel(ActiveLoadSource source)
{
	switch(source){
	case ACTIVE_LOAD_SOURCE_IMG: return "IMG archive";
	case ACTIVE_LOAD_SOURCE_LOOSE: return "data/Paths";
	default: return "none";
	}
}

static void
clearAreas(void)
{
	for(int i = 0; i < NUM_PATH_AREAS; i++)
		gAreas[i] = AreaData();
	hoveredNode = nil;
	selectedNode = nil;
	gSelectedLinkIndex = -1;
}

static bool
parseAreaData(int areaId, const std::vector<uint8> &fileData, const char *sourcePath,
              const char *logicalPath,
              AreaData *outArea, size_t *outRequiredBytes, char *outReason, size_t outReasonSize)
{
	size_t offset = 0;
	if(outRequiredBytes)
		*outRequiredBytes = 0;
	if(outReason && outReasonSize > 0)
		outReason[0] = '\0';
	if(fileData.empty()){
		if(outReason && outReasonSize > 0)
			snprintf(outReason, outReasonSize, "empty_data");
		return false;
	}
	const uint8 *fileBytes = &fileData[0];

	AreaData area = {};
	if(logicalPath){
		strncpy(area.logicalPath, logicalPath, sizeof(area.logicalPath)-1);
		area.logicalPath[sizeof(area.logicalPath)-1] = '\0';
	}
	strncpy(area.sourcePath, sourcePath, sizeof(area.sourcePath)-1);
	area.sourcePath[sizeof(area.sourcePath)-1] = '\0';

	if(!readExact(fileBytes, fileData.size(), &offset, &area.numNodes, sizeof(area.numNodes)) ||
	   !readExact(fileBytes, fileData.size(), &offset, &area.numVehicleNodes, sizeof(area.numVehicleNodes)) ||
	   !readExact(fileBytes, fileData.size(), &offset, &area.numPedNodes, sizeof(area.numPedNodes)) ||
	   !readExact(fileBytes, fileData.size(), &offset, &area.numCarPathLinks, sizeof(area.numCarPathLinks)) ||
	   !readExact(fileBytes, fileData.size(), &offset, &area.numAddresses, sizeof(area.numAddresses))){
		if(outRequiredBytes)
			*outRequiredBytes = 5*sizeof(uint32);
		if(outReason && outReasonSize > 0)
			snprintf(outReason, outReasonSize, "short_header");
		return false;
	}

	size_t numLinkSlots = area.numAddresses > 0 ? area.numAddresses + NUM_DYNAMIC_LINK_SLOTS : 0;
	size_t requiredBytes = 5*sizeof(uint32) +
		sizeof(DiskNode)*area.numNodes +
		sizeof(DiskCarPathLink)*area.numCarPathLinks;
	if(area.numAddresses > 0){
		requiredBytes += sizeof(NodeAddress)*numLinkSlots;
		requiredBytes += sizeof(CarPathLinkAddress)*area.numAddresses;
		requiredBytes += sizeof(uint8)*numLinkSlots;
		requiredBytes += sizeof(uint8)*numLinkSlots;
	}
	if(outRequiredBytes)
		*outRequiredBytes = requiredBytes;

	if(area.numNodes != area.numVehicleNodes + area.numPedNodes){
		if(outReason && outReasonSize > 0)
			snprintf(outReason, outReasonSize, "invalid_counts");
		return false;
	}

	area.nodes.resize(area.numNodes);
	for(uint32 i = 0; i < area.numNodes; i++){
		if(!readExact(fileBytes, fileData.size(), &offset, &area.nodes[i].raw, sizeof(DiskNode))){
			if(outReason && outReasonSize > 0)
				snprintf(outReason, outReasonSize, "short_nodes");
			return false;
		}
		area.nodes[i].ownerAreaId = areaId;
		area.nodes[i].index = i;
	}

	area.naviNodes.resize(area.numCarPathLinks);
	if(!area.naviNodes.empty() &&
	   !readExact(fileBytes, fileData.size(), &offset,
	              &area.naviNodes[0], sizeof(DiskCarPathLink)*area.naviNodes.size())){
		if(outReason && outReasonSize > 0)
			snprintf(outReason, outReasonSize, "short_navis");
		return false;
	}

	if(area.numAddresses > 0){
		area.nodeLinks.resize(numLinkSlots);
		area.linkLengths.resize(numLinkSlots);
		area.intersections.resize(numLinkSlots);
		area.naviLinks.resize(area.numAddresses);
		if(!readExact(fileBytes, fileData.size(), &offset,
		              &area.nodeLinks[0], sizeof(NodeAddress)*numLinkSlots) ||
		   !readExact(fileBytes, fileData.size(), &offset,
		              &area.naviLinks[0], sizeof(CarPathLinkAddress)*area.naviLinks.size()) ||
		   !readExact(fileBytes, fileData.size(), &offset,
		              &area.linkLengths[0], sizeof(uint8)*numLinkSlots) ||
		   !readExact(fileBytes, fileData.size(), &offset,
		              &area.intersections[0], sizeof(uint8)*numLinkSlots)){
			if(outReason && outReasonSize > 0)
				snprintf(outReason, outReasonSize, "short_links");
			return false;
		}
	}

	area.loaded = true;
	if(outArea)
		*outArea = std::move(area);
	return true;
}

static bool
loadAreaFromSource(int areaId, ActiveLoadSource source)
{
	std::vector<uint8> fileData;
	size_t requiredBytes = 0;
	char sourcePath[1024];
	char logicalPath[64];
	char reason[64];
	sourcePath[0] = '\0';
	logicalPath[0] = '\0';
	reason[0] = '\0';

	bool gotData = source == ACTIVE_LOAD_SOURCE_IMG ?
		loadAreaImgData(areaId, fileData, sourcePath, sizeof(sourcePath), logicalPath, sizeof(logicalPath)) :
		loadAreaLooseData(areaId, fileData, sourcePath, sizeof(sourcePath), logicalPath, sizeof(logicalPath));
	if(!gotData){
		gLastLoadFailureArea = areaId;
		snprintf(gLastLoadFailurePath, sizeof(gLastLoadFailurePath), "%s", logicalPath[0] ? logicalPath : "(unresolved)");
		snprintf(gLastLoadFailureReason, sizeof(gLastLoadFailureReason), "couldn't_open");
		log("SAPaths: couldn't open nodes area %d from %s\n", areaId, logicalPath[0] ? logicalPath : activeLoadSourceLabel(source));
		return false;
	}
	if(fileData.empty()){
		gLastLoadFailureArea = areaId;
		snprintf(gLastLoadFailurePath, sizeof(gLastLoadFailurePath), "%s", sourcePath);
		snprintf(gLastLoadFailureReason, sizeof(gLastLoadFailureReason), "empty_data");
		log("SAPaths: empty nodes data in %s\n", sourcePath);
		return false;
	}

	AreaData parsed;
	size_t originalSize = fileData.size();
	if(source == ACTIVE_LOAD_SOURCE_IMG &&
	   !parseAreaData(areaId, fileData, sourcePath, logicalPath, &parsed, &requiredBytes, reason, sizeof(reason)) &&
	   isShortReadFailure(reason) &&
	   requiredBytes > fileData.size() &&
	   requiredBytes - fileData.size() <= MAX_LENIENT_IMG_OVERREAD_BYTES &&
	   logicalPath[0] != '\0' &&
	   ReadCdImageEntryByLogicalPathMinSize(logicalPath, requiredBytes, fileData, sourcePath, sizeof(sourcePath)) &&
	   parseAreaData(areaId, fileData, sourcePath, logicalPath, &parsed, &requiredBytes, reason, sizeof(reason))){
		int spillBytes = (int)(requiredBytes > originalSize ? requiredBytes - originalSize : 0);
		gUsedLenientImgRead = true;
		log("SAPaths: lenient IMG read recovered area %d from %s (needed %d extra bytes)\n",
		    areaId, sourcePath, spillBytes);
	}else if(!parseAreaData(areaId, fileData, sourcePath, logicalPath, &parsed, &requiredBytes, reason, sizeof(reason))){
		gLastLoadFailureArea = areaId;
		snprintf(gLastLoadFailurePath, sizeof(gLastLoadFailurePath), "%s", sourcePath[0] ? sourcePath : logicalPath);
		snprintf(gLastLoadFailureReason, sizeof(gLastLoadFailureReason), "%s", reason[0] ? reason : "parse_failed");
		if(strcmp(reason, "short_header") == 0)
			log("SAPaths: short header in %s\n", sourcePath);
		else if(strcmp(reason, "invalid_counts") == 0)
			log("SAPaths: invalid node counts in %s\n", sourcePath);
		else if(strcmp(reason, "short_nodes") == 0)
			log("SAPaths: short node table in %s\n", sourcePath);
		else if(strcmp(reason, "short_navis") == 0)
			log("SAPaths: short navi table in %s\n", sourcePath);
		else if(strcmp(reason, "short_links") == 0)
			log("SAPaths: short link table in %s\n", sourcePath);
		else
			log("SAPaths: failed parsing %s (%s)\n", sourcePath, reason[0] ? reason : "parse_failed");
		return false;
	}

	gAreas[areaId] = std::move(parsed);
	return true;
}

static bool
loadAllAreasFromSource(ActiveLoadSource source)
{
	for(int i = 0; i < NUM_PATH_AREAS; i++)
		if(!loadAreaFromSource(i, source))
			return false;
	return true;
}

static void
updateActiveLoadSourceDetail(void)
{
	gActiveLoadSourceDetail[0] = '\0';

	if(gActiveLoadSource == ACTIVE_LOAD_SOURCE_LOOSE){
		strncpy(gActiveLoadSourceDetail, "data/Paths", sizeof(gActiveLoadSourceDetail)-1);
		gActiveLoadSourceDetail[sizeof(gActiveLoadSourceDetail)-1] = '\0';
		return;
	}
	if(gActiveLoadSource != ACTIVE_LOAD_SOURCE_IMG)
		return;

	char firstArchive[256];
	firstArchive[0] = '\0';
	bool mixedArchives = false;
	for(int areaId = 0; areaId < NUM_PATH_AREAS; areaId++){
		const AreaData &area = gAreas[areaId];
		if(!area.loaded || area.logicalPath[0] == '\0')
			continue;

		char archiveLogicalPath[256];
		if(!extractArchiveLogicalPath(area.logicalPath, archiveLogicalPath, sizeof(archiveLogicalPath)))
			continue;

		if(firstArchive[0] == '\0'){
			strncpy(firstArchive, archiveLogicalPath, sizeof(firstArchive)-1);
			firstArchive[sizeof(firstArchive)-1] = '\0';
		}else if(strcmp(firstArchive, archiveLogicalPath) != 0){
			mixedArchives = true;
			break;
		}
	}

	if(mixedArchives){
		strncpy(gActiveLoadSourceDetail, "mixed IMG archives", sizeof(gActiveLoadSourceDetail)-1);
		gActiveLoadSourceDetail[sizeof(gActiveLoadSourceDetail)-1] = '\0';
	}else if(firstArchive[0] != '\0'){
		strncpy(gActiveLoadSourceDetail, firstArchive, sizeof(gActiveLoadSourceDetail)-1);
		gActiveLoadSourceDetail[sizeof(gActiveLoadSourceDetail)-1] = '\0';
	}
}

void
Reset(void)
{
	clearAreas();
	clearPreviewWalkers();
	clearPreviewTrafficCars();
	clearPreviewParkedCars();
	gLoadAttempted = false;
	gLoadSucceeded = false;
	gLoadFailed = false;
	gActiveLoadSource = ACTIVE_LOAD_SOURCE_NONE;
	gActiveLoadSourceDetail[0] = '\0';
	gUsedLenientImgRead = false;
	gLastLoadFailureArea = -1;
	gLastLoadFailurePath[0] = '\0';
	gLastLoadFailureReason[0] = '\0';
	gBlockedCrossAreaNodeMove = false;
	gBlockedCrossAreaNaviMove = false;
}

void
EnsureLoaded(void)
{
	if(!isSA() || gLoadAttempted)
		return;

	gLoadAttempted = true;
	gLoadSucceeded = false;
	gLoadFailed = false;
	gUsedLenientImgRead = false;
	gActiveLoadSourceDetail[0] = '\0';
	gLastLoadFailureArea = -1;
	gLastLoadFailurePath[0] = '\0';
	gLastLoadFailureReason[0] = '\0';
	gActiveLoadSource = ACTIVE_LOAD_SOURCE_NONE;

	ActiveLoadSource attempts[2];
	int numAttempts = 0;
	switch(gLoadSourceMode){
	case LOAD_SOURCE_IMG:
		attempts[numAttempts++] = ACTIVE_LOAD_SOURCE_IMG;
		break;
	case LOAD_SOURCE_LOOSE:
		attempts[numAttempts++] = ACTIVE_LOAD_SOURCE_LOOSE;
		break;
	case LOAD_SOURCE_AUTO:
	default:
		attempts[numAttempts++] = ACTIVE_LOAD_SOURCE_IMG;
		attempts[numAttempts++] = ACTIVE_LOAD_SOURCE_LOOSE;
		break;
	}

	for(int i = 0; i < numAttempts; i++){
		clearAreas();
		if(loadAllAreasFromSource(attempts[i])){
			gLoadSucceeded = true;
			gActiveLoadSource = attempts[i];
			updateActiveLoadSourceDetail();
			break;
		}
	}

	gLoadFailed = !gLoadSucceeded;
	if(gLoadSucceeded){
		log("SAPaths: loaded %d streamed path areas from %s%s\n",
		    NUM_PATH_AREAS,
		    gActiveLoadSourceDetail[0] ? gActiveLoadSourceDetail : activeLoadSourceLabel(gActiveLoadSource),
		    gUsedLenientImgRead ? " (lenient IMG read)" : "");
	}else{
		clearAreas();
		log("SAPaths: failed to load streamed path areas using %s\n",
		    loadSourceModeLabel(gLoadSourceMode));
	}
}

static void
markAreaDirty(uint16 areaId)
{
	setAreaDirty(areaId, true);
}

static bool
findReciprocalLinkSlot(const NodeAddress &from, const NodeAddress &to, uint16 *outAreaId, int *outSlot)
{
	Node *target = getNode(to);
	if(target == nil)
		return false;

	AreaData *area = getArea(target->ownerAreaId);
	if(area == nil || !area->loaded)
		return false;

	for(int li = 0; li < target->numLinks(); li++){
		int slot = target->raw.baseLinkId + li;
		if(slot < 0 || slot >= (int)area->nodeLinks.size())
			continue;
		NodeAddress linked = area->nodeLinks[slot];
		if(linked.areaId != from.areaId || linked.nodeId != from.nodeId)
			continue;
		if(outAreaId)
			*outAreaId = target->ownerAreaId;
		if(outSlot)
			*outSlot = slot;
		return true;
	}
	return false;
}

static bool
canMoveAttachedNavisWithNode(const NodeAddress &changedAddr, const rw::V3d &delta)
{
	if(std::abs(delta.x) < 0.0001f && std::abs(delta.y) < 0.0001f)
		return true;

	for(int areaId = 0; areaId < NUM_PATH_AREAS; areaId++){
		AreaData &area = gAreas[areaId];
		if(!area.loaded)
			continue;
		for(uint32 i = 0; i < area.naviNodes.size(); i++){
			DiskCarPathLink &navi = area.naviNodes[i];
			if(navi.attachedAreaId != changedAddr.areaId || navi.attachedNodeId != changedAddr.nodeId)
				continue;
			rw::V2d pos = getNaviPosition(navi);
			if(!positionBelongsToArea(areaId, pos.x + delta.x, pos.y + delta.y))
				return false;
		}
	}
	return true;
}

static void
moveAttachedNavisWithNode(const NodeAddress &changedAddr, const rw::V3d &delta)
{
	if(std::abs(delta.x) < 0.0001f && std::abs(delta.y) < 0.0001f)
		return;

	for(int areaId = 0; areaId < NUM_PATH_AREAS; areaId++){
		AreaData &area = gAreas[areaId];
		if(!area.loaded)
			continue;

		bool changed = false;
		for(uint32 i = 0; i < area.naviNodes.size(); i++){
			DiskCarPathLink &navi = area.naviNodes[i];
			if(navi.attachedAreaId != changedAddr.areaId || navi.attachedNodeId != changedAddr.nodeId)
				continue;
			navi.x = (int16)clamp((int)std::floor((navi.x * NAVI_POS_SCALE + delta.x) / NAVI_POS_SCALE + (delta.x >= 0.0f ? 0.5f : -0.5f)), -32768, 32767);
			navi.y = (int16)clamp((int)std::floor((navi.y * NAVI_POS_SCALE + delta.y) / NAVI_POS_SCALE + (delta.y >= 0.0f ? 0.5f : -0.5f)), -32768, 32767);
			changed = true;
		}
		if(changed)
			area.dirty = true;
	}
}

bool
HasSelectedNode(void)
{
	EnsureLoaded();
	return selectedNode != nil;
}

bool
GetSelectedNodePosition(rw::V3d *pos)
{
	EnsureLoaded();
	if(selectedNode == nil || pos == nil)
		return false;
	*pos = getNodePosition(*selectedNode);
	return true;
}

static rw::V3d
quantizeNodePosition(const rw::V3d &pos)
{
	int16 qx = (int16)(pos.x / PATH_POS_SCALE);
	int16 qy = (int16)(pos.y / PATH_POS_SCALE);
	int16 qz = (int16)(pos.z / PATH_POS_SCALE);
	return {
		qx * PATH_POS_SCALE,
		qy * PATH_POS_SCALE,
		qz * PATH_POS_SCALE
	};
}

bool
SetSelectedNodePosition(const rw::V3d &pos, bool notifyChange)
{
	EnsureLoaded();
	if(selectedNode == nil)
		return false;

	rw::V3d current = getNodePosition(*selectedNode);
	rw::V3d quantizedPos = quantizeNodePosition(pos);
	rw::V3d delta = sub(quantizedPos, current);
	if(length(delta) < 0.0001f)
		return false;

	gBlockedCrossAreaNodeMove = false;
	gBlockedCrossAreaNaviMove = false;
	if(!positionBelongsToArea(selectedNode->ownerAreaId, quantizedPos.x, quantizedPos.y)){
		gBlockedCrossAreaNodeMove = true;
		return false;
	}

	if(isVehicleNode(selectedNode) && !canMoveAttachedNavisWithNode(selectedNode->address(), delta)){
		gBlockedCrossAreaNaviMove = true;
		return false;
	}

	selectedNode->setPosition(quantizedPos.x, quantizedPos.y, quantizedPos.z);
	setAreaDirty(selectedNode->ownerAreaId, notifyChange);
	if(isVehicleNode(selectedNode))
		moveAttachedNavisWithNode(selectedNode->address(), delta);
	refreshMetadataForNode(selectedNode->address());
	return true;
}

void
CommitSelectedNodeEdit(void)
{
	EnsureLoaded();
	if(selectedNode == nil)
		return;
	touchChangeSeq();
}

static void
refreshMetadataForNode(const NodeAddress &changedAddr)
{
	Node *changed = getNode(changedAddr);
	if(changed == nil)
		return;

	for(int areaId = 0; areaId < NUM_PATH_AREAS; areaId++){
		AreaData &area = gAreas[areaId];
		if(!area.loaded)
			continue;

		bool areaChanged = false;
		for(uint32 i = 0; i < area.nodes.size(); i++){
			Node &node = area.nodes[i];
			NodeAddress nodeAddr = node.address();
			for(int li = 0; li < node.numLinks(); li++){
				int slot = node.raw.baseLinkId + li;
				if(slot < 0 || slot >= (int)area.numAddresses || slot >= (int)area.nodeLinks.size())
					continue;
				NodeAddress linkedAddr = area.nodeLinks[slot];
				if(!linkedAddr.isValid())
					continue;
				if(!(nodeAddr.areaId == changedAddr.areaId && nodeAddr.nodeId == changedAddr.nodeId) &&
				   !(linkedAddr.areaId == changedAddr.areaId && linkedAddr.nodeId == changedAddr.nodeId))
					continue;
				Node *linked = getNode(linkedAddr);
				if(linked == nil)
					continue;
				area.linkLengths[slot] = computeLinkLength(node, *linked);
				areaChanged = true;
			}
		}
		if(areaChanged)
			area.dirty = true;
	}
}

static void
buildAreaFileData(const AreaData &area, std::vector<uint8> &out)
{
	out.clear();
	appendBytes(out, area.numNodes);
	appendBytes(out, area.numVehicleNodes);
	appendBytes(out, area.numPedNodes);
	appendBytes(out, area.numCarPathLinks);
	appendBytes(out, area.numAddresses);
	for(uint32 i = 0; i < area.nodes.size(); i++)
		appendBytes(out, area.nodes[i].raw);
	appendBytes(out, area.naviNodes);
	appendBytes(out, area.nodeLinks);
	appendBytes(out, area.naviLinks);
	appendBytes(out, area.linkLengths);
	appendBytes(out, area.intersections);
}

static void
drawNodeSphere(const rw::V3d &pos, const rw::RGBA &col, bool vehicle)
{
	CSphere sphere;
	sphere.center = pos;
	sphere.radius = NODE_DRAW_RADIUS;
	if(vehicle)
		RenderSphereAsWireBox(&sphere, col, nil);
	else
		RenderWireSphere(&sphere, col, nil);
}

static void
drawNodeSet(bool vehicles)
{
	Ray ray;
	ray.start = TheCamera.m_position;
	ray.dir = normalize(TheCamera.m_mouseDir);
	static float hoveredNodeHitT = FLT_MAX;
	if(hoveredNode == nil)
		hoveredNodeHitT = FLT_MAX;

	for(int areaId = 0; areaId < NUM_PATH_AREAS; areaId++){
		AreaData &area = gAreas[areaId];
		if(!area.loaded || !areaInDrawRange(areaId))
			continue;
		uint32 start = vehicles ? 0 : area.numVehicleNodes;
		uint32 end = vehicles ? area.numVehicleNodes : area.numNodes;
		rw::RGBA baseCol = vehicles ? red : magenta;
		for(uint32 i = start; i < end; i++){
			Node &node = area.nodes[i];
			rw::V3d p = getNodePosition(node);
			if(TheCamera.distanceTo(p) > PATH_DRAW_DIST)
				continue;

			rw::RGBA c = &node == selectedNode ? white : baseCol;
			CSphere sphere;
			sphere.center = p;
			sphere.radius = NODE_DRAW_RADIUS;
			float hitT = 0.0f;
			if(IntersectRaySphere(ray, sphere, &hitT) && hitT >= 0.0f && hitT < hoveredNodeHitT){
				hoveredNode = &node;
				hoveredNodeHitT = hitT;
				c = cyan;
			}
			drawNodeSphere(p, c, vehicles);

			for(int li = 0; li < node.numLinks(); li++){
				int slot = node.raw.baseLinkId + li;
				if(slot < 0 || slot >= (int)area.nodeLinks.size())
					continue;
				Node *other = getNode(area.nodeLinks[slot]);
				if(other == nil)
					continue;
				rw::V3d q = getNodePosition(*other);
				if(TheCamera.distanceTo(q) > PATH_DRAW_DIST && TheCamera.distanceTo(p) > PATH_DRAW_DIST)
					continue;
				bool selected = &node == selectedNode || other == selectedNode;
				rw::RGBA lc = &node == hoveredNode ? cyan :
					selected ? white :
					getRoadCross(area.intersections[slot]) ? yellow : baseCol;
				RenderLine(p, q, lc, lc);
			}
		}
	}
}

static void
drawVehicleLanes(void)
{
	for(int areaId = 0; areaId < NUM_PATH_AREAS; areaId++){
		AreaData &area = gAreas[areaId];
		if(!area.loaded || !areaInDrawRange(areaId))
			continue;
		for(uint32 i = 0; i < area.numVehicleNodes; i++){
			Node &node = area.nodes[i];
			rw::V3d p = getNodePosition(node);
			if(TheCamera.distanceTo(p) > PATH_DRAW_DIST)
				continue;
			for(int li = 0; li < node.numLinks(); li++){
				int slot = node.raw.baseLinkId + li;
				if(slot < 0 || slot >= (int)area.numAddresses || slot >= (int)area.naviLinks.size())
					continue;
				Node *other = getNode(area.nodeLinks[slot]);
				if(other == nil)
					continue;
				CarPathLinkAddress naviAddr = area.naviLinks[slot];
				if(!naviAddr.isValid())
					continue;
				AreaData *naviArea = getArea(naviAddr.areaId());
				if(naviArea == nil || !naviArea->loaded || naviAddr.nodeId() >= naviArea->naviNodes.size())
					continue;
				DiskCarPathLink &navi = naviArea->naviNodes[naviAddr.nodeId()];
				rw::V3d q = getNodePosition(*other);
				rw::V3d dir = sub(q, p);
				dir.z = 0.0f;
				if(length(dir) < 0.001f)
					continue;
				rw::V3d up = { 0.0f, 0.0f, 1.0f };
				rw::V3d right = normalize(cross(up, dir));
				float laneOff = getLaneOffset(navi);
				rw::V3d r1 = add(p, scale(right, laneOff*LANE_WIDTH));
				rw::V3d r2 = add(q, scale(right, laneOff*LANE_WIDTH));
				rw::V3d l1 = sub(p, scale(right, laneOff*LANE_WIDTH));
				rw::V3d l2 = sub(q, scale(right, laneOff*LANE_WIDTH));
				for(int l = 0; l < getNaviSameLanes(navi); l++)
					RenderLine(sub(l1, scale(right, LANE_WIDTH*l)), sub(l2, scale(right, LANE_WIDTH*l)), green, green);
				for(int l = 0; l < getNaviOppositeLanes(navi); l++)
					RenderLine(add(r1, scale(right, LANE_WIDTH*l)), add(r2, scale(right, LANE_WIDTH*l)), green, green);
			}
		}
	}
}

static void
drawArrow(const rw::V3d &start, const rw::V3d &dir, float lengthUnits, rw::RGBA col)
{
	rw::V3d n = dir;
	n.z = 0.0f;
	float len = length(n);
	if(len < 0.001f)
		return;
	n = scale(n, 1.0f/len);

	rw::V3d end = add(start, scale(n, lengthUnits));
	RenderLine(start, end, col, col);

	rw::V3d right = { n.y, -n.x, 0.0f };
	rw::V3d headBase = add(start, scale(n, lengthUnits*0.72f));
	float headSize = min(lengthUnits*0.28f, 3.0f);
	RenderLine(end, add(headBase, scale(right, headSize*0.5f)), col, col);
	RenderLine(end, sub(headBase, scale(right, headSize*0.5f)), col, col);
}

static void
drawPreviewTrafficLinkOverlayOneDirection(const NodeAddress &source, const NodeAddress &target,
                                          CarPathLinkAddress naviAddr, rw::RGBA laneCol, rw::RGBA arrowCol)
{
	DiskCarPathLink *navi = getPreviewTrafficNavi(naviAddr);
	Node *sourceNode = getNode(source);
	Node *targetNode = getNode(target);
	if(navi == nil || sourceNode == nil || targetNode == nil)
		return;

	bool sameDirection = false;
	int laneCount = 0;
	if(!getPreviewDirectedVehicleLink(source, target, nil, nil, &sameDirection, &laneCount) || laneCount <= 0)
		return;

	float z = (sourceNode->z() + targetNode->z()) * 0.5f + 0.35f;

	rw::V3d dir = getPreviewTrafficTravelDirection(source, target, *navi);
	for(int lane = 0; lane < laneCount; lane++){
		rw::V3d point = getPreviewTrafficLanePointAtNavi(source, target, *navi, sameDirection, lane, z);
		CSphere sphere;
		sphere.center = point;
		sphere.radius = 0.75f;
		RenderSphereAsCross(&sphere, laneCol, nil);
		drawArrow(point, dir, 7.0f, arrowCol);
	}
}

static float
distancePointToSegment2D(const rw::V3d &p, const rw::V3d &a, const rw::V3d &b)
{
	rw::V3d ab = sub(b, a);
	ab.z = 0.0f;
	float abLenSq = dot(ab, ab);
	if(abLenSq < 0.0001f){
		rw::V3d delta = sub(p, a);
		delta.z = 0.0f;
		return length(delta);
	}

	rw::V3d ap = sub(p, a);
	ap.z = 0.0f;
	float t = clamp(dot(ap, ab) / abLenSq, 0.0f, 1.0f);
	rw::V3d closest = add(a, scale(ab, t));
	rw::V3d delta = sub(p, closest);
	delta.z = 0.0f;
	return length(delta);
}

static void
drawPreviewTrafficCarDebugOverlay(const PreviewTrafficCar &car, rw::RGBA tangentCol, rw::RGBA frameCol)
{
	if(!car.active || car.clump == nil)
		return;

	float t = getPreviewTrafficCurveTForDistance(car, car.distanceOnCurve);
	rw::V3d pos = quadraticBezierV3d(car.curveP0, car.curveP1, car.curveP2, t);
	rw::V3d tangent = quadraticBezierTangent(car.curveP0, car.curveP1, car.curveP2, t);
	tangent.z = 0.0f;
	if(length(tangent) >= 0.001f)
		tangent = normalize(tangent);
	else
		tangent = { 0.0f, 1.0f, 0.0f };

	rw::V3d frameForward = car.clump->getFrame()->matrix.up;
	frameForward.z = 0.0f;
	if(length(frameForward) >= 0.001f)
		frameForward = normalize(frameForward);
	else
		frameForward = tangent;

	CSphere sphere;
	sphere.center = pos;
	sphere.radius = 0.9f;
	RenderSphereAsCross(&sphere, tangentCol, nil);
	drawArrow(pos, tangent, 9.0f, tangentCol);
	drawArrow(pos, frameForward, 5.5f, frameCol);
	if(car.hasLastRenderedPos && length(car.lastMotionDir) >= 0.001f)
		drawArrow(pos, car.lastMotionDir, 7.0f, { 255, 160, 0, 255 });
}

static void
drawSelectedPreviewTrafficCarsDebugOverlay(const NodeAddress &source, const NodeAddress &target,
                                           CarPathLinkAddress naviAddr)
{
	DiskCarPathLink *navi = getPreviewTrafficNavi(naviAddr);
	Node *sourceNode = getNode(source);
	Node *targetNode = getNode(target);
	if(navi == nil || sourceNode == nil || targetNode == nil)
		return;

	float z = (sourceNode->z() + targetNode->z()) * 0.5f;
	rw::V3d laneA = getPreviewTrafficLanePointAtNavi(source, target, *navi, false, 0, z);
	rw::V3d laneB = laneA;
	bool hasLaneSegment = false;
	bool sameDirection = false;
	int laneCount = 0;
	if(getPreviewDirectedVehicleLink(source, target, nil, nil, &sameDirection, &laneCount) && laneCount > 0){
		laneA = getPreviewTrafficLanePointAtNavi(source, target, *navi, sameDirection, 0, z);
		laneB = getPreviewTrafficLanePointAtNavi(source, target, *navi, sameDirection, laneCount - 1, z);
		hasLaneSegment = true;
	}

	bool foundTouching = false;
	float bestDistance = 999999.0f;
	const PreviewTrafficCar *nearest = nil;
	for(size_t i = 0; i < gPreviewTrafficCars.size(); i++){
		const PreviewTrafficCar &car = gPreviewTrafficCars[i];
		if(!car.active || car.clump == nil)
			continue;
		if(car.currentLinkAddr.raw == naviAddr.raw || car.nextLinkAddr.raw == naviAddr.raw){
			drawPreviewTrafficCarDebugOverlay(car, red, green);
			foundTouching = true;
			continue;
		}
		if(hasLaneSegment){
			float t = getPreviewTrafficCurveTForDistance(car, car.distanceOnCurve);
			rw::V3d pos = quadraticBezierV3d(car.curveP0, car.curveP1, car.curveP2, t);
			float distance = distancePointToSegment2D(pos, laneA, laneB);
			if(distance < bestDistance){
				bestDistance = distance;
				nearest = &car;
			}
		}
	}

	if(!foundTouching && nearest != nil)
		drawPreviewTrafficCarDebugOverlay(*nearest, red, green);
}

static void
drawSelectedPreviewTrafficDebugOverlay(void)
{
	if(selectedNode == nil || !isVehicleNode(selectedNode))
		return;

	AreaData *area = getArea(selectedNode->ownerAreaId);
	if(area == nil || gSelectedLinkIndex < 0 || gSelectedLinkIndex >= selectedNode->numLinks())
		return;

	int slot = selectedNode->raw.baseLinkId + gSelectedLinkIndex;
	if(slot < 0 || slot >= (int)area->nodeLinks.size() || slot >= (int)area->naviLinks.size())
		return;

	CarPathLinkAddress naviAddr = area->naviLinks[slot];
	if(!naviAddr.isValid())
		return;

	NodeAddress source = selectedNode->address();
	NodeAddress target = area->nodeLinks[slot];
	drawPreviewTrafficLinkOverlayOneDirection(source, target, naviAddr, cyan, white);
	drawSelectedPreviewTrafficCarsDebugOverlay(source, target, naviAddr);

	uint16 reciprocalAreaId = 0xFFFF;
	int reciprocalSlot = -1;
	if(findReciprocalLinkSlot(source, target, &reciprocalAreaId, &reciprocalSlot)){
		AreaData *recArea = getArea(reciprocalAreaId);
		if(recArea && reciprocalSlot >= 0 && reciprocalSlot < (int)recArea->naviLinks.size()){
			CarPathLinkAddress recNaviAddr = recArea->naviLinks[reciprocalSlot];
			if(recNaviAddr.isValid())
				drawPreviewTrafficLinkOverlayOneDirection(target, source, recNaviAddr, yellow, magenta);
		}
	}
}

static uint8
directionToOctant(const rw::V3d &dir)
{
	float angle = std::atan2(dir.x, dir.y);
	int octant = (int)std::floor((angle + PREVIEW_PI/8.0f) / (PREVIEW_PI/4.0f));
	octant %= 8;
	if(octant < 0)
		octant += 8;
	return (uint8)octant;
}

static bool
chooseNextPreviewPedNode(const NodeAddress &currentAddr, const NodeAddress &previousAddr, uint8 direction,
                        NodeAddress *outTarget, uint8 *outDirection)
{
	Node *current = getNode(currentAddr);
	if(current == nil || current->numLinks() <= 0)
		return false;
	*outTarget = { 0xFFFF, 0xFFFF };

	AreaData *area = getArea(current->ownerAreaId);
	if(area == nil)
		return false;

	float angle = direction * (PREVIEW_PI/4.0f);
	rw::V3d wantedDir = { std::sin(angle), std::cos(angle), 0.0f };
	float bestScore = -999999.0f;
	NodeAddress fallback = { 0xFFFF, 0xFFFF };
	rw::V3d fallbackDir = { 0.0f, 1.0f, 0.0f };

	for(int li = 0; li < current->numLinks(); li++){
		int slot = current->raw.baseLinkId + li;
		if(slot < 0 || slot >= (int)area->nodeLinks.size())
			continue;
		NodeAddress linkedAddr = area->nodeLinks[slot];
		Node *linked = getNode(linkedAddr);
		if(linked == nil)
			continue;
		if(!current->isSwitchedOff() && linked->isSwitchedOff())
			continue;
		if(!current->dontWander() && linked->dontWander())
			continue;

		rw::V3d delta = sub(getNodePosition(*linked), getNodePosition(*current));
		delta.z = 0.0f;
		float len = length(delta);
		if(len < 0.001f)
			continue;
		rw::V3d normalized = scale(delta, 1.0f/len);
		if(!fallback.isValid()){
			fallback = linkedAddr;
			fallbackDir = normalized;
		}
		float score = dot(normalized, wantedDir);
		if(score >= bestScore){
			bestScore = score;
			*outTarget = linkedAddr;
			if(outDirection)
				*outDirection = directionToOctant(normalized);
		}
	}

	if(!outTarget->isValid() && fallback.isValid()){
		*outTarget = fallback;
		if(outDirection)
			*outDirection = directionToOctant(fallbackDir);
	}

	if(outTarget->isValid() &&
	   outTarget->areaId == previousAddr.areaId &&
	   outTarget->nodeId == previousAddr.nodeId &&
	   fallback.isValid()){
		*outTarget = fallback;
		if(outDirection)
			*outDirection = directionToOctant(fallbackDir);
	}

	return outTarget->isValid();
}

static bool
pickPreviewSpawnNode(uint32 &seed, NodeAddress *outAddr)
{
	std::vector<NodeAddress> candidates;
	std::vector<float> weights;
	candidates.reserve(256);
	weights.reserve(256);

	for(int areaId = 0; areaId < NUM_PATH_AREAS; areaId++){
		AreaData &area = gAreas[areaId];
		if(!area.loaded || !areaInDrawRange(areaId))
			continue;
		for(uint32 i = area.numVehicleNodes; i < area.numNodes; i++){
			Node &node = area.nodes[i];
			if(node.numLinks() <= 0 || node.dontWander() || node.waterNode())
				continue;
			rw::V3d pos = getNodePosition(node);
			if(TheCamera.distanceTo(pos) > PATH_DRAW_DIST*0.75f)
				continue;
			candidates.push_back(node.address());
			float weight = 1.0f;
			const PreviewZoneState *state = nil;
			const PreviewPopcycleSlot *slot = getPreviewPopcycleSlotForPosition(pos, &state);
			if(slot)
				weight = max(1.0f, (float)slot->maxPeds);
			weights.push_back(weight);
		}
	}

	if(candidates.empty())
		return false;

	float totalWeight = 0.0f;
	for(size_t i = 0; i < weights.size(); i++)
		totalWeight += weights[i];
	float roll = previewSeedFloat01(seed) * totalWeight;
	int index = 0;
	for(size_t i = 0; i < candidates.size(); i++){
		if(roll < weights[i]){
			index = (int)i;
			break;
		}
		roll -= weights[i];
		index = (int)i;
	}
	*outAddr = candidates[index];
	return true;
}

static void
updatePreviewWalkerEndpoints(PreviewWalker &walker)
{
	Node *current = getNode(walker.current);
	Node *target = getNode(walker.target);
	if(current == nil || target == nil){
		walker.active = false;
		return;
	}

	uint32 fromSeed = walker.seed ^ (walker.current.areaId << 16) ^ walker.current.nodeId;
	uint32 toSeed = walker.seed ^ (walker.target.areaId << 16) ^ walker.target.nodeId;
	walker.fromPos = add(getNodePosition(*current), getPedWanderOffset(*current, fromSeed));
	walker.toPos = add(getNodePosition(*target), getPedWanderOffset(*target, toSeed));
}

static void
applyPreviewWalkerAnimation(PreviewWalker &walker)
{
	if(walker.clump == nil || walker.assetIndex < 0 || walker.assetIndex >= (int)gPreviewPedAssetCache.size())
		return;
	PreviewPedAssets &assets = gPreviewPedAssetCache[walker.assetIndex];
	if(!assets.loaded)
		return;

	rw::HAnimHierarchy *hier = getHAnimHierarchyFromClump(walker.clump);
	if(hier == nil || hier->numNodes <= 0)
		return;

	float animTime = walker.animTime;
	if(assets.animationDuration > 0.001f)
		animTime = std::fmod(animTime, assets.animationDuration);

	rw::Matrix rootMat;
	rw::Frame *hierRoot = hier->parentFrame;
	rw::Frame *rootParent = hierRoot ? hierRoot->getParent() : nil;
	if(hierRoot && rootParent && !(hier->flags & rw::HAnimHierarchy::LOCALSPACEMATRICES))
		rootMat = *rootParent->getLTM();
	else
		rootMat.setIdentity();

	rw::Matrix *parentMat = &rootMat;
	rw::Matrix *stack[64];
	rw::Matrix **sp = stack;
	*sp++ = parentMat;

	for(int i = 0; i < hier->numNodes; i++){
		rw::Matrix local = assets.baseLocalMatrices[i];
		const PreviewAnimNode &track = assets.tracksByNode[i];
		if(!track.keyFrames.empty()){
			rw::Quat rotation;
			rw::V3d translation;
			if(samplePreviewTrack(track, animTime, &rotation, &translation)){
				rw::V3d localPos = local.pos;
				local.rotate(rotation, rw::COMBINEREPLACE);
				local.pos = localPos;
				if(track.type & PREVIEW_HAS_TRANS){
					if(i == 0){
						local.pos.x = 0.0f;
						local.pos.y = 0.0f;
						local.pos.z = translation.z;
					}else
						local.pos = translation;
				}
			}
		}

		rw::Matrix::mult(&hier->matrices[i], &local, parentMat);
		if(hier->nodeInfo[i].flags & rw::HAnimHierarchy::PUSH){
			if(sp < stack + 64)
				*sp++ = parentMat;
		}
		parentMat = &hier->matrices[i];
		if(hier->nodeInfo[i].flags & rw::HAnimHierarchy::POP){
			if(sp > stack)
				parentMat = *--sp;
			else
				parentMat = &rootMat;
		}
	}
}

static bool
resetPreviewWalker(PreviewWalker &walker, uint32 seed)
{
	destroyPreviewWalker(walker);
	if(!loadPreviewMetadata() && gPreviewPedDefs.empty())
		return false;

	walker.seed = seed;
	walker.active = true;
	walker.previous = { 0xFFFF, 0xFFFF };
	walker.current = { 0xFFFF, 0xFFFF };
	walker.target = { 0xFFFF, 0xFFFF };
	walker.assetIndex = -1;
	walker.direction = (uint8)(advancePreviewSeed(walker.seed) & 7);
	walker.segmentT = previewSeedFloat01(walker.seed);
	walker.speed = 1.0f + previewSeedFloat01(walker.seed)*0.45f;

	if(!pickPreviewSpawnNode(walker.seed, &walker.current))
		return false;
	if(!chooseNextPreviewPedNode(walker.current, walker.previous, walker.direction, &walker.target, &walker.direction))
		return false;

	rw::V3d spawnPos = getNodePosition(*getNode(walker.current));
	for(int attempts = 0; attempts < 20; attempts++){
		int pedIndex = choosePreviewPedDefForPosition(spawnPos, walker.seed);
		if(pedIndex < 0){
			pedIndex = (int)(previewSeedFloat01(walker.seed) * gPreviewPedDefs.size());
			pedIndex = clamp(pedIndex, 0, (int)gPreviewPedDefs.size()-1);
		}
		walker.assetIndex = loadPreviewPedAssets(pedIndex);
		if(walker.assetIndex >= 0)
			break;
	}
	if(walker.assetIndex < 0)
		return false;

	PreviewPedAssets &assets = gPreviewPedAssetCache[walker.assetIndex];
	walker.clump = assets.clump->clone();
	if(walker.clump == nil)
		return false;
	setupPreviewClump(walker.clump);
	walker.animTime = previewSeedFloat01(walker.seed) * assets.animationDuration;

	updatePreviewWalkerEndpoints(walker);
	return true;
}

static void
advancePreviewWalkerSegment(PreviewWalker &walker)
{
	walker.previous = walker.current;
	walker.current = walker.target;
	walker.target = { 0xFFFF, 0xFFFF };
	walker.segmentT = 0.0f;
	if(!chooseNextPreviewPedNode(walker.current, walker.previous, walker.direction, &walker.target, &walker.direction))
		walker.active = false;
	else
		updatePreviewWalkerEndpoints(walker);
}

static void
updatePreviewWalker(PreviewWalker &walker)
{
	if(!walker.active){
		resetPreviewWalker(walker, walker.seed + 1);
		return;
	}

	rw::V3d approxPos = lerpV3d(walker.fromPos, walker.toPos, clamp(walker.segmentT, 0.0f, 1.0f));
	if(TheCamera.distanceTo(approxPos) > PATH_DRAW_DIST*1.5f){
		walker.active = false;
		resetPreviewWalker(walker, walker.seed + 31);
		return;
	}

	if(gPlayAnimations)
		walker.animTime += timeStep;

	for(int steps = 0; steps < 4; steps++){
		rw::V3d delta = sub(walker.toPos, walker.fromPos);
		float segmentLength = length(delta);
		if(segmentLength < 0.01f){
			advancePreviewWalkerSegment(walker);
			if(!walker.active){
				resetPreviewWalker(walker, walker.seed + 17);
				return;
			}
			continue;
		}

		walker.segmentT += (walker.speed * timeStep) / segmentLength;
		if(walker.segmentT < 1.0f)
			break;
		walker.segmentT -= 1.0f;
		advancePreviewWalkerSegment(walker);
		if(!walker.active){
			resetPreviewWalker(walker, walker.seed + 17);
			return;
		}
	}

	rw::V3d pos = lerpV3d(walker.fromPos, walker.toPos, clamp(walker.segmentT, 0.0f, 1.0f));
	pos.z += PREVIEW_PED_Z_OFFSET;
	rw::V3d dir = sub(walker.toPos, walker.fromPos);
	dir.z = 0.0f;
	float dirLen = length(dir);
	if(dirLen < 0.001f)
		dir = { 0.0f, 1.0f, 0.0f };
	else
		dir = scale(dir, 1.0f/dirLen);

	rw::Matrix world;
	world.setIdentity();
	world.lookAt({ 0.0f, 0.0f, 1.0f }, dir);
	world.pos = pos;
	walker.clump->getFrame()->matrix = world;
	walker.clump->getFrame()->updateObjects();
	applyPreviewWalkerAnimation(walker);
}

static void
renderPreviewWalkers(void)
{
	if(!gRenderSaPedPathWalkers || gSaPedPathWalkerCount <= 0)
		return;
	if(!loadPreviewMetadata() && gPreviewPedDefs.empty())
		return;

	int wantedCount = clamp(gSaPedPathWalkerCount, 0, 32);
	while((int)gPreviewWalkers.size() > wantedCount){
		destroyPreviewWalker(gPreviewWalkers.back());
		gPreviewWalkers.pop_back();
	}
	while((int)gPreviewWalkers.size() < wantedCount){
		PreviewWalker walker = {};
		walker.seed = 0x12345678u + (uint32)gPreviewWalkers.size()*0x1020304u;
		resetPreviewWalker(walker, walker.seed);
		gPreviewWalkers.push_back(walker);
	}

	rw::SetRenderState(rw::ZTESTENABLE, 1);
	rw::SetRenderState(rw::ZWRITEENABLE, 1);
	rw::SetRenderState(rw::FOGENABLE, 0);
	rw::SetRenderState(rw::CULLMODE, rw::CULLBACK);
	gPreviewTrafficLightTimeMs += timeStep * (1000.0f/60.0f);
	rw::RGBAf savedAmbient = pAmbient->color;
	rw::RGBAf savedDirect = pDirect->color;
	pAmbient->setColor(0.75f, 0.75f, 0.75f);
	pDirect->setColor(0.55f, 0.55f, 0.55f);

	for(size_t i = 0; i < gPreviewWalkers.size(); i++){
		updatePreviewWalker(gPreviewWalkers[i]);
		if(gPreviewWalkers[i].active && gPreviewWalkers[i].clump)
			gPreviewWalkers[i].clump->render();
	}

	pAmbient->setColor(savedAmbient.red, savedAmbient.green, savedAmbient.blue);
	pDirect->setColor(savedDirect.red, savedDirect.green, savedDirect.blue);
	rw::SetRenderState(rw::CULLMODE, rw::CULLNONE);
}

static bool
updatePreviewTrafficCarCurve(PreviewTrafficCar &car)
{
	Node *previous = getNode(car.previous);
	Node *current = getNode(car.current);
	Node *target = getNode(car.target);
	if(previous == nil || current == nil || target == nil){
		car.active = false;
		return false;
	}

	DiskCarPathLink *currentNavi = getPreviewTrafficNavi(car.currentLinkAddr);
	DiskCarPathLink *nextNavi = getPreviewTrafficNavi(car.nextLinkAddr);
	if(currentNavi == nil || nextNavi == nil){
		car.active = false;
		return false;
	}

	car.currentLaneCount = car.currentSameDirection ? getNaviSameLanes(*currentNavi) : getNaviOppositeLanes(*currentNavi);
	car.nextLaneCount = car.nextSameDirection ? getNaviSameLanes(*nextNavi) : getNaviOppositeLanes(*nextNavi);
	if(car.currentLaneCount <= 0 || car.nextLaneCount <= 0){
		car.active = false;
		return false;
	}
	car.currentLaneIndex = clamp(car.currentLaneIndex, 0, car.currentLaneCount - 1);
	car.nextLaneIndex = clamp(car.nextLaneIndex, 0, car.nextLaneCount - 1);

	rw::V3d currentDir = getPreviewTrafficTravelDirection(car.previous, car.current, *currentNavi);
	rw::V3d nextDir = getPreviewTrafficTravelDirection(car.current, car.target, *nextNavi);
	float startZ, endZ;
	rw::V3d startDir, endDir;
	if(!car.traverseReversed){
		startZ = (previous->z() + current->z()) * 0.5f;
		endZ = (current->z() + target->z()) * 0.5f;
		car.curveP0 = getPreviewTrafficLanePointAtNavi(car.previous, car.current, *currentNavi,
			car.currentSameDirection, car.currentLaneIndex, startZ);
		car.curveP2 = getPreviewTrafficLanePointAtNavi(car.current, car.target, *nextNavi,
			car.nextSameDirection, car.nextLaneIndex, endZ);
		startDir = currentDir;
		endDir = nextDir;
	}else{
		startZ = (current->z() + target->z()) * 0.5f;
		endZ = (previous->z() + current->z()) * 0.5f;
		car.curveP0 = getPreviewTrafficLanePointAtNavi(car.current, car.target, *nextNavi,
			car.nextSameDirection, car.nextLaneIndex, startZ);
		car.curveP2 = getPreviewTrafficLanePointAtNavi(car.previous, car.current, *currentNavi,
			car.currentSameDirection, car.currentLaneIndex, endZ);
		startDir = nextDir;
		endDir = currentDir;
	}
	float controlZ = (startZ + endZ) * 0.5f;
	car.curveP1 = getPreviewTrafficCurveControlPoint(car.curveP0, startDir, car.curveP2, endDir, controlZ);
	buildPreviewTrafficCurveSamples(car);
	return true;
}

static bool
previewTrafficCarTouchesDirtyArea(const PreviewTrafficCar &car)
{
	auto areaIsDirty = [](uint16 areaId) {
		AreaData *area = getArea(areaId);
		return area && area->loaded && area->dirty;
	};

	return (car.previous.isValid() && areaIsDirty(car.previous.areaId)) ||
	       (car.current.isValid() && areaIsDirty(car.current.areaId)) ||
	       (car.target.isValid() && areaIsDirty(car.target.areaId)) ||
	       (car.currentLinkAddr.isValid() && areaIsDirty(car.currentLinkAddr.areaId())) ||
	       (car.nextLinkAddr.isValid() && areaIsDirty(car.nextLinkAddr.areaId()));
}

static bool
isPreviewTrafficCarPathStateValid(const PreviewTrafficCar &car)
{
	if(!car.previous.isValid() || !car.current.isValid() || !car.target.isValid())
		return false;

	CarPathLinkAddress resolvedCurrentLinkAddr = { 0xFFFF };
	bool resolvedCurrentSameDirection = false;
	int resolvedCurrentLaneCount = 0;
	if(!getPreviewDirectedVehicleLink(car.previous, car.current, &resolvedCurrentLinkAddr, nil,
		&resolvedCurrentSameDirection, &resolvedCurrentLaneCount))
		return false;
	if(resolvedCurrentLinkAddr.raw != car.currentLinkAddr.raw ||
	   resolvedCurrentSameDirection != car.currentSameDirection ||
	   resolvedCurrentLaneCount <= 0)
		return false;

	CarPathLinkAddress resolvedNextLinkAddr = { 0xFFFF };
	bool resolvedNextSameDirection = false;
	int resolvedNextLaneCount = 0;
	if(!getPreviewDirectedVehicleLink(car.current, car.target, &resolvedNextLinkAddr, nil,
		&resolvedNextSameDirection, &resolvedNextLaneCount))
		return false;
	if(resolvedNextLinkAddr.raw != car.nextLinkAddr.raw ||
	   resolvedNextSameDirection != car.nextSameDirection ||
	   resolvedNextLaneCount <= 0)
		return false;

	return true;
}

static void
refreshPreviewTrafficCarForLiveEdit(PreviewTrafficCar &car)
{
	if(!car.active || !previewTrafficCarTouchesDirtyArea(car))
		return;

	if(!isPreviewTrafficCarPathStateValid(car)){
		resetPreviewTrafficCar(car, getPreviewTrafficResetSeed(car, 53));
		return;
	}

	float distanceRatio = car.curveLength > 0.001f ? car.distanceOnCurve / car.curveLength : 0.0f;
	if(!updatePreviewTrafficCarCurve(car)){
		resetPreviewTrafficCar(car, getPreviewTrafficResetSeed(car, 53));
		return;
	}
	car.distanceOnCurve = clamp(distanceRatio * car.curveLength, 0.0f, car.curveLength);
}

static void
refreshPreviewTrafficCarsForLiveEdit(void)
{
	for(size_t i = 0; i < gPreviewTrafficCars.size(); i++)
		refreshPreviewTrafficCarForLiveEdit(gPreviewTrafficCars[i]);
}

static float
choosePreviewTrafficSpeed(const PreviewVehicleAssets &assets, const Node *current, uint32 &seed)
{
	float minSpeed = assets.heavy ? 2.4f : 3.2f;
	float maxSpeed = assets.heavy ? 4.2f : 5.6f;
	if(current){
		if(current->highway()){
			minSpeed += 1.1f;
			maxSpeed += 1.6f;
		}else if(current->notHighway()){
			minSpeed -= 0.4f;
			maxSpeed -= 0.6f;
		}
	}
	if(maxSpeed < minSpeed)
		maxSpeed = minSpeed;
	return (minSpeed + previewSeedFloat01(seed) * (maxSpeed - minSpeed)) *
		PREVIEW_TRAFFIC_SPEED_MULTIPLIER;
}

static float
getPreviewTrafficSegmentSpeedScale(const PreviewTrafficCar &car)
{
	Node *current = getNode(car.current);
	DiskCarPathLink *currentNavi = getPreviewTrafficNavi(car.currentLinkAddr);
	DiskCarPathLink *nextNavi = getPreviewTrafficNavi(car.nextLinkAddr);
	if(current == nil || currentNavi == nil || nextNavi == nil)
		return 1.0f;

	float scale = 1.0f;

	int currentTotalLanes = getNaviSameLanes(*currentNavi) + getNaviOppositeLanes(*currentNavi);
	int nextTotalLanes = getNaviSameLanes(*nextNavi) + getNaviOppositeLanes(*nextNavi);
	int laneCount = max(currentTotalLanes, nextTotalLanes);
	float roadWidth = current->width();
	if(current->highway())
		scale *= 1.12f;
	else if(current->notHighway())
		scale *= 0.92f;
	if(laneCount >= 4 || roadWidth >= 12.0f)
		scale *= 1.08f;
	else if(laneCount <= 2 || roadWidth <= 7.5f)
		scale *= 0.90f;

	rw::V3d currentDir = getPreviewTrafficTravelDirection(car.previous, car.current, *currentNavi);
	rw::V3d nextDir = getPreviewTrafficTravelDirection(car.current, car.target, *nextNavi);
	if(car.traverseReversed){
		rw::V3d tmp = currentDir;
		currentDir = nextDir;
		nextDir = tmp;
	}
	float turnDot = clamp(dot(currentDir, nextDir), -1.0f, 1.0f);
	if(turnDot < 0.35f)
		scale *= 0.58f;
	else if(turnDot < 0.60f)
		scale *= 0.72f;
	else if(turnDot < 0.82f)
		scale *= 0.84f;
	else if(turnDot > 0.97f)
		scale *= 1.04f;

	float curveLengthFactor = car.curveLength > 24.0f ? 1.04f : car.curveLength < 12.0f ? 0.92f : 1.0f;
	scale *= curveLengthFactor;

	if(car.heavy)
		scale *= turnDot < 0.82f ? 0.88f : 0.94f;

	return clamp(scale, 0.45f, 1.35f);
}

static bool
resetPreviewTrafficCar(PreviewTrafficCar &car, uint32 seed)
{
	destroyPreviewTrafficCar(car);
	if(!loadPreviewVehicleMetadata() || gPreviewVehicleDefs.empty())
		return false;

	car.seed = seed;
	car.active = true;
	car.previous = { 0xFFFF, 0xFFFF };
	car.current = { 0xFFFF, 0xFFFF };
	car.target = { 0xFFFF, 0xFFFF };
	car.currentLinkAddr = { 0xFFFF };
	car.nextLinkAddr = { 0xFFFF };
	car.currentSameDirection = false;
	car.nextSameDirection = false;
	car.currentLaneIndex = 0;
	car.currentLaneCount = 0;
	car.nextLaneIndex = 0;
	car.nextLaneCount = 0;
	car.traverseReversed = false;
	car.hasLastRenderedPos = false;
	car.lastRenderedPos = { 0.0f, 0.0f, 0.0f };
	car.lastMotionDir = { 0.0f, 0.0f, 0.0f };
	car.colorIndices[0] = 1;
	car.colorIndices[1] = 1;
	car.colorIndices[2] = 1;
	car.colorIndices[3] = 1;
	car.materialColorRoles.clear();
	car.assetIndex = -1;
	car.distanceOnCurve = 0.0f;
	car.curveLength = 0.01f;
	if(car.preferredSpawnDistance <= 0.0f){
		float preferredMin = PATH_DRAW_DIST*0.28f;
		float preferredMax = PATH_DRAW_DIST*0.72f;
		car.preferredSpawnDistance = preferredMin + previewSeedFloat01(car.seed) * (preferredMax - preferredMin);
	}

	bool foundPathState = false;
	for(int pathAttempts = 0; pathAttempts < 24; pathAttempts++){
		NodeAddress spawnNode = { 0xFFFF, 0xFFFF };
		NodeAddress outgoingTarget = { 0xFFFF, 0xFFFF };
		NodeAddress incomingSource = { 0xFFFF, 0xFFFF };
		CarPathLinkAddress currentLinkAddr = { 0xFFFF };
		CarPathLinkAddress nextLinkAddr = { 0xFFFF };
		int currentLaneIndex = 0;
		int currentLaneCount = 0;
		int nextLaneIndex = 0;
		int nextLaneCount = 0;
		bool currentSameDirection = false;
		bool nextSameDirection = false;

		if(!pickPreviewTrafficSpawnNode(car.seed, &spawnNode, car.preferredSpawnDistance, 25.0f))
			continue;
		if(!chooseNextPreviewTrafficLink(spawnNode, { 0xFFFF, 0xFFFF }, car.seed,
			&outgoingTarget, &nextLinkAddr, &nextLaneIndex, &nextLaneCount, &nextSameDirection))
			continue;
		if(!choosePreviewIncomingTrafficLink(spawnNode, outgoingTarget, car.seed,
			&incomingSource, &currentLinkAddr, &currentLaneIndex,
			nextLaneIndex, nextLaneCount, &currentLaneCount, &currentSameDirection))
			continue;

		car.previous = incomingSource;
		car.current = spawnNode;
		car.target = outgoingTarget;
		car.currentLinkAddr = currentLinkAddr;
		car.nextLinkAddr = nextLinkAddr;
		car.currentSameDirection = currentSameDirection;
		car.nextSameDirection = nextSameDirection;
		car.currentLaneIndex = currentLaneIndex;
		car.currentLaneCount = currentLaneCount;
		car.nextLaneIndex = nextLaneIndex;
		car.nextLaneCount = nextLaneCount;
		DiskCarPathLink *resolvedCurrentNavi = getPreviewTrafficNavi(car.currentLinkAddr);
		DiskCarPathLink *resolvedNextNavi = getPreviewTrafficNavi(car.nextLinkAddr);
		car.traverseReversed =
			resolvedCurrentNavi && resolvedNextNavi &&
			isPreviewTrafficSegmentTravelReversed(car.previous, car.current, *resolvedCurrentNavi) &&
			isPreviewTrafficSegmentTravelReversed(car.current, car.target, *resolvedNextNavi);
		foundPathState = true;
		break;
	}
	if(!foundPathState)
		return false;

	rw::V3d spawnPos = getNodePosition(*getNode(car.current));
	Node *spawnNode = getNode(car.current);
	DiskCarPathLink *spawnNavi = getPreviewTrafficNavi(car.nextLinkAddr);
	int chosenVehicleIndex = -1;
	for(int attempts = 0; attempts < 24; attempts++){
		int vehicleIndex = choosePreviewVehicleDefForTrafficContext(spawnPos, spawnNode, spawnNavi, car.seed);
		if(vehicleIndex < 0){
			vehicleIndex = (int)(previewSeedFloat01(car.seed) * gPreviewVehicleDefs.size());
			vehicleIndex = clamp(vehicleIndex, 0, (int)gPreviewVehicleDefs.size() - 1);
		}
		car.assetIndex = loadPreviewVehicleAssets(vehicleIndex);
		if(car.assetIndex >= 0){
			chosenVehicleIndex = vehicleIndex;
			break;
		}
	}
	if(car.assetIndex < 0 || chosenVehicleIndex < 0)
		return false;

	PreviewVehicleAssets &assets = gPreviewVehicleAssetCache[car.assetIndex];
	choosePreviewVehicleColorIndices(gPreviewVehicleDefs[chosenVehicleIndex], car.seed, car.colorIndices);
	car.heavy = assets.heavy;
	car.speed = choosePreviewTrafficSpeed(assets, getNode(car.current), car.seed);
	car.clump = assets.clump->clone();
	if(car.clump == nil)
		return false;
	car.materialColorRoles = assets.materialColorRoles;
	setupPreviewClump(car.clump);
	if(!updatePreviewTrafficCarCurve(car))
		return false;
	car.distanceOnCurve = previewSeedFloat01(car.seed) * car.curveLength;
	return true;
}

static bool
resetPreviewParkedCar(PreviewParkedCar &car, uint32 seed)
{
	destroyPreviewParkedCar(car);
	if(!loadPreviewVehicleMetadata() || gPreviewVehicleDefs.empty())
		return false;

	car.seed = seed;
	car.active = true;
	car.current = { 0xFFFF, 0xFFFF };
	car.target = { 0xFFFF, 0xFFFF };
	car.naviAddr = { 0xFFFF };
	car.sameDirection = false;
	car.sideSign = 1.0f;
	car.colorIndices[0] = 1;
	car.colorIndices[1] = 1;
	car.colorIndices[2] = 1;
	car.colorIndices[3] = 1;
	car.materialColorRoles.clear();
	car.assetIndex = -1;
	if(car.preferredSpawnDistance <= 0.0f){
		float preferredMin = PATH_DRAW_DIST*0.30f;
		float preferredMax = PATH_DRAW_DIST*0.75f;
		car.preferredSpawnDistance = preferredMin + previewSeedFloat01(car.seed) * (preferredMax - preferredMin);
	}

	PreviewParkedSpawnState spawn = {};
	if(!pickPreviewParkedSpawnState(car.seed, &spawn, car.preferredSpawnDistance, 5.0f))
		return false;

	car.current = spawn.current;
	car.target = spawn.target;
	car.naviAddr = spawn.naviAddr;
	car.sameDirection = spawn.sameDirection;
	car.sideSign = spawn.sideSign;
	car.pos = spawn.pos;
	car.forward = spawn.forward;

	Node *spawnNode = getNode(car.current);
	DiskCarPathLink *spawnNavi = getPreviewTrafficNavi(car.naviAddr);
	int chosenVehicleIndex = -1;
	for(int attempts = 0; attempts < 24; attempts++){
		int vehicleIndex = choosePreviewVehicleDefForTrafficContext(car.pos, spawnNode, spawnNavi, car.seed);
		if(vehicleIndex < 0){
			vehicleIndex = (int)(previewSeedFloat01(car.seed) * gPreviewVehicleDefs.size());
			vehicleIndex = clamp(vehicleIndex, 0, (int)gPreviewVehicleDefs.size() - 1);
		}
		car.assetIndex = loadPreviewVehicleAssets(vehicleIndex);
		if(car.assetIndex >= 0){
			chosenVehicleIndex = vehicleIndex;
			break;
		}
	}
	if(car.assetIndex < 0 || chosenVehicleIndex < 0)
		return false;

	PreviewVehicleAssets &assets = gPreviewVehicleAssetCache[car.assetIndex];
	choosePreviewVehicleColorIndices(gPreviewVehicleDefs[chosenVehicleIndex], car.seed, car.colorIndices);
	car.heavy = assets.heavy;
	car.clump = assets.clump->clone();
	if(car.clump == nil)
		return false;
	car.materialColorRoles = assets.materialColorRoles;
	setupPreviewClump(car.clump);
	return true;
}

static void
advancePreviewTrafficCarSegment(PreviewTrafficCar &car)
{
	if(car.traverseReversed){
		NodeAddress oldPrevious = car.previous;
		NodeAddress oldCurrent = car.current;
		CarPathLinkAddress oldCurrentLinkAddr = car.currentLinkAddr;
		bool oldCurrentSameDirection = car.currentSameDirection;
		int oldCurrentLaneIndex = car.currentLaneIndex;
		int oldCurrentLaneCount = car.currentLaneCount;

		car.target = oldCurrent;
		car.current = oldPrevious;
		car.nextLinkAddr = oldCurrentLinkAddr;
		car.nextSameDirection = oldCurrentSameDirection;
		car.nextLaneIndex = oldCurrentLaneIndex;
		car.nextLaneCount = oldCurrentLaneCount;
		car.previous = { 0xFFFF, 0xFFFF };
		car.currentLinkAddr = { 0xFFFF };
		car.currentSameDirection = false;
		car.currentLaneIndex = 0;
		car.currentLaneCount = 0;
		car.distanceOnCurve = 0.0f;

		if(!choosePreviewIncomingTrafficLink(car.current, car.target, car.seed,
			&car.previous, &car.currentLinkAddr, &car.currentLaneIndex,
			car.nextLaneIndex, car.nextLaneCount, &car.currentLaneCount, &car.currentSameDirection)){
			car.active = false;
			return;
		}

		DiskCarPathLink *resolvedCurrentNavi = getPreviewTrafficNavi(car.currentLinkAddr);
		DiskCarPathLink *resolvedNextNavi = getPreviewTrafficNavi(car.nextLinkAddr);
		car.traverseReversed =
			resolvedCurrentNavi && resolvedNextNavi &&
			isPreviewTrafficSegmentTravelReversed(car.previous, car.current, *resolvedCurrentNavi) &&
			isPreviewTrafficSegmentTravelReversed(car.current, car.target, *resolvedNextNavi);
		updatePreviewTrafficCarCurve(car);
		return;
	}

	car.previous = car.current;
	car.current = car.target;
	car.currentLinkAddr = car.nextLinkAddr;
	car.currentSameDirection = car.nextSameDirection;
	car.currentLaneIndex = car.nextLaneIndex;
	car.currentLaneCount = car.nextLaneCount;
	car.target = { 0xFFFF, 0xFFFF };
	car.distanceOnCurve = 0.0f;
	if(!chooseNextPreviewTrafficLinkFromCurrentLink(car.current, car.previous,
		car.currentLinkAddr, car.currentSameDirection, car.seed,
		&car.target, &car.nextLinkAddr, &car.nextLaneIndex, &car.nextLaneCount, &car.nextSameDirection,
		car.currentLaneIndex, car.currentLaneCount))
		car.active = false;
	else{
		DiskCarPathLink *resolvedCurrentNavi = getPreviewTrafficNavi(car.currentLinkAddr);
		DiskCarPathLink *resolvedNextNavi = getPreviewTrafficNavi(car.nextLinkAddr);
		car.traverseReversed =
			resolvedCurrentNavi && resolvedNextNavi &&
			isPreviewTrafficSegmentTravelReversed(car.previous, car.current, *resolvedCurrentNavi) &&
			isPreviewTrafficSegmentTravelReversed(car.current, car.target, *resolvedNextNavi);
		updatePreviewTrafficCarCurve(car);
	}
}

static void
updatePreviewParkedCar(PreviewParkedCar &car)
{
	if(!car.active){
		resetPreviewParkedCar(car, car.seed + 1);
		return;
	}
	if(TheCamera.distanceTo(car.pos) > PATH_DRAW_DIST*1.55f){
		resetPreviewParkedCar(car, car.seed + 17);
		return;
	}

	rw::V3d dir = car.forward;
	dir.z = 0.0f;
	float dirLen = length(dir);
	if(dirLen < 0.001f)
		dir = { 0.0f, 1.0f, 0.0f };
	else
		dir = scale(dir, 1.0f/dirLen);

	rw::V3d groundNormal = { 0.0f, 0.0f, 1.0f };
	rw::V3d right = cross(dir, groundNormal);
	float rightLen = length(right);
	if(rightLen < 0.001f)
		right = { 1.0f, 0.0f, 0.0f };
	else
		right = scale(right, 1.0f/rightLen);
	rw::V3d forward = cross(groundNormal, right);
	float forwardLen = length(forward);
	if(forwardLen < 0.001f)
		forward = dir;
	else
		forward = scale(forward, 1.0f/forwardLen);

	rw::Matrix world;
	world.right = right;
	world.up = forward;
	world.at = groundNormal;
	world.pos = car.pos;
	world.pos.z += PREVIEW_CAR_Z_OFFSET;
	car.clump->getFrame()->matrix = world;
	car.clump->getFrame()->updateObjects();
}

static void
updatePreviewTrafficCar(PreviewTrafficCar &car)
{
	if(!car.active){
		resetPreviewTrafficCar(car, getPreviewTrafficResetSeed(car, 1));
		return;
	}

	float approxT = getPreviewTrafficCurveTForDistance(car, car.distanceOnCurve);
	rw::V3d approxPos = quadraticBezierV3d(car.curveP0, car.curveP1, car.curveP2, approxT);
	if(TheCamera.distanceTo(approxPos) > PATH_DRAW_DIST*1.6f){
		car.active = false;
		resetPreviewTrafficCar(car, getPreviewTrafficResetSeed(car, 31));
		return;
	}

	float segmentSpeedScale = getPreviewTrafficSegmentSpeedScale(car);
	float remainingDistance = car.speed * segmentSpeedScale * gSaCarPathTrafficSpeedScale * timeStep;
	for(int steps = 0; steps < 8 && remainingDistance > 0.0f; steps++){
		float lightStopDistance = shouldPreviewTrafficCarStopForLight(car) ? getPreviewTrafficLightStopDistance(car) : car.curveLength;
		float spacingStopDistance = getPreviewTrafficSpacingStopDistance(car);
		float yieldStopDistance = shouldPreviewTrafficCarYieldAtJunction(car) ? getPreviewTrafficYieldStopDistance(car) : car.curveLength;
		float distanceLimit = min(car.curveLength, min(lightStopDistance, min(spacingStopDistance, yieldStopDistance)));
		float distanceLeftOnCurve = max(0.0f, distanceLimit - car.distanceOnCurve);
		if(distanceLeftOnCurve <= 0.001f){
			remainingDistance = 0.0f;
			break;
		}
		if(remainingDistance < distanceLeftOnCurve){
			car.distanceOnCurve += remainingDistance;
			remainingDistance = 0.0f;
			break;
		}
		remainingDistance -= distanceLeftOnCurve;
		advancePreviewTrafficCarSegment(car);
		if(!car.active){
			resetPreviewTrafficCar(car, getPreviewTrafficResetSeed(car, 17));
			return;
		}
	}

	float t = getPreviewTrafficCurveTForDistance(car, car.distanceOnCurve);
	rw::V3d pos = quadraticBezierV3d(car.curveP0, car.curveP1, car.curveP2, t);
	rw::V3d dir = quadraticBezierTangent(car.curveP0, car.curveP1, car.curveP2, t);
	dir.z = 0.0f;
	float dirLen = length(dir);
	if(dirLen < 0.001f)
		dir = { 0.0f, 1.0f, 0.0f };
	else
		dir = scale(dir, 1.0f/dirLen);

	rw::V3d groundNormal = { 0.0f, 0.0f, 1.0f };
	rw::V3d right = cross(dir, groundNormal);
	float rightLen = length(right);
	if(rightLen < 0.001f)
		right = { 1.0f, 0.0f, 0.0f };
	else
		right = scale(right, 1.0f/rightLen);
	rw::V3d forward = cross(groundNormal, right);
	float forwardLen = length(forward);
	if(forwardLen < 0.001f)
		forward = dir;
	else
		forward = scale(forward, 1.0f/forwardLen);

	rw::Matrix world;
	world.right = right;
	world.up = forward;
	world.at = groundNormal;
	world.pos = pos;
	world.pos.z += PREVIEW_CAR_Z_OFFSET;
	rw::V3d planarPos = world.pos;
	planarPos.z = 0.0f;
	if(car.hasLastRenderedPos){
		rw::V3d motion = sub(planarPos, car.lastRenderedPos);
		motion.z = 0.0f;
		float motionLen = length(motion);
		if(motionLen >= 0.001f)
			car.lastMotionDir = scale(motion, 1.0f/motionLen);
	}
	car.lastRenderedPos = planarPos;
	car.hasLastRenderedPos = true;
	car.clump->getFrame()->matrix = world;
	car.clump->getFrame()->updateObjects();
}

static void
renderPreviewTrafficCars(void)
{
	if(!gRenderSaCarPathTraffic || gSaCarPathTrafficCount <= 0){
		clearPreviewTrafficCars();
		return;
	}
	if(!loadPreviewVehicleMetadata() || gPreviewVehicleDefs.empty())
		return;

	if(gPreviewTrafficLastFreezeRoutes != gSaCarPathTrafficFreezeRoutes){
		clearPreviewTrafficCars();
		gPreviewTrafficLastFreezeRoutes = gSaCarPathTrafficFreezeRoutes;
	}

	int wantedCount = clamp(gSaCarPathTrafficCount, 0, 32);
	while((int)gPreviewTrafficCars.size() > wantedCount){
		destroyPreviewTrafficCar(gPreviewTrafficCars.back());
		gPreviewTrafficCars.pop_back();
	}
	while((int)gPreviewTrafficCars.size() < wantedCount){
		PreviewTrafficCar car = {};
		car.stableId = (uint32)gPreviewTrafficCars.size();
		car.seed = 0x2468ACE1u + (uint32)gPreviewTrafficCars.size()*0x01020305u;
		float preferredMin = PATH_DRAW_DIST*0.28f;
		float preferredMax = PATH_DRAW_DIST*0.72f;
		float slotT = wantedCount > 1 ? (float)gPreviewTrafficCars.size() / (float)(wantedCount - 1) : 0.5f;
		car.preferredSpawnDistance = preferredMin + slotT * (preferredMax - preferredMin);
		resetPreviewTrafficCar(car, car.seed);
		gPreviewTrafficCars.push_back(car);
	}

	uint32 latestChangeSeq = GetLatestChangeSeq();
	if(gPreviewTrafficLastSeenChangeSeq != latestChangeSeq){
		refreshPreviewTrafficCarsForLiveEdit();
		gPreviewTrafficLastSeenChangeSeq = latestChangeSeq;
	}

	rw::SetRenderState(rw::ZTESTENABLE, 1);
	rw::SetRenderState(rw::ZWRITEENABLE, 1);
	rw::SetRenderState(rw::FOGENABLE, 0);
	rw::SetRenderState(rw::CULLMODE, rw::CULLBACK);
	rw::RGBAf savedAmbient = pAmbient->color;
	rw::RGBAf savedDirect = pDirect->color;
	pAmbient->setColor(0.80f, 0.80f, 0.80f);
	pDirect->setColor(0.65f, 0.65f, 0.65f);

	for(size_t i = 0; i < gPreviewTrafficCars.size(); i++){
		updatePreviewTrafficCar(gPreviewTrafficCars[i]);
		if(gPreviewTrafficCars[i].active && gPreviewTrafficCars[i].clump){
			applyPreviewVehicleColorIndices(gPreviewTrafficCars[i].clump,
				gPreviewTrafficCars[i].materialColorRoles,
				gPreviewTrafficCars[i].colorIndices);
			gPreviewTrafficCars[i].clump->render();
		}
	}

	pAmbient->setColor(savedAmbient.red, savedAmbient.green, savedAmbient.blue);
	pDirect->setColor(savedDirect.red, savedDirect.green, savedDirect.blue);
	rw::SetRenderState(rw::CULLMODE, rw::CULLNONE);
}

static void
renderPreviewParkedCars(void)
{
	if(!gRenderSaCarPathParkedCars || gSaCarPathParkedCarCount <= 0){
		clearPreviewParkedCars();
		return;
	}
	if(!loadPreviewVehicleMetadata() || gPreviewVehicleDefs.empty())
		return;

	uint32 latestChangeSeq = GetLatestChangeSeq();
	if(gPreviewParkedLastSeenChangeSeq != latestChangeSeq){
		clearPreviewParkedCars();
		gPreviewParkedLastSeenChangeSeq = latestChangeSeq;
	}

	int wantedCount = clamp(gSaCarPathParkedCarCount, 0, 24);
	while((int)gPreviewParkedCars.size() > wantedCount){
		destroyPreviewParkedCar(gPreviewParkedCars.back());
		gPreviewParkedCars.pop_back();
	}
	while((int)gPreviewParkedCars.size() < wantedCount){
		PreviewParkedCar car = {};
		car.stableId = (uint32)gPreviewParkedCars.size();
		car.seed = 0x3579BDF1u + (uint32)gPreviewParkedCars.size()*0x01040811u;
		float preferredMin = PATH_DRAW_DIST*0.30f;
		float preferredMax = PATH_DRAW_DIST*0.75f;
		float slotT = wantedCount > 1 ? (float)gPreviewParkedCars.size() / (float)(wantedCount - 1) : 0.5f;
		car.preferredSpawnDistance = preferredMin + slotT * (preferredMax - preferredMin);
		resetPreviewParkedCar(car, car.seed);
		gPreviewParkedCars.push_back(car);
	}

	rw::SetRenderState(rw::ZTESTENABLE, 1);
	rw::SetRenderState(rw::ZWRITEENABLE, 1);
	rw::SetRenderState(rw::FOGENABLE, 0);
	rw::SetRenderState(rw::CULLMODE, rw::CULLBACK);
	rw::RGBAf savedAmbient = pAmbient->color;
	rw::RGBAf savedDirect = pDirect->color;
	pAmbient->setColor(0.80f, 0.80f, 0.80f);
	pDirect->setColor(0.65f, 0.65f, 0.65f);

	for(size_t i = 0; i < gPreviewParkedCars.size(); i++){
		updatePreviewParkedCar(gPreviewParkedCars[i]);
		if(gPreviewParkedCars[i].active && gPreviewParkedCars[i].clump){
			applyPreviewVehicleColorIndices(gPreviewParkedCars[i].clump,
				gPreviewParkedCars[i].materialColorRoles,
				gPreviewParkedCars[i].colorIndices);
			gPreviewParkedCars[i].clump->render();
		}
	}

	pAmbient->setColor(savedAmbient.red, savedAmbient.green, savedAmbient.blue);
	pDirect->setColor(savedDirect.red, savedDirect.green, savedDirect.blue);
	rw::SetRenderState(rw::CULLMODE, rw::CULLNONE);
}

void
RenderCarPaths(void)
{
	EnsureLoaded();
	if(!gLoadSucceeded)
		return;
	drawNodeSet(true);
	drawVehicleLanes();
	drawSelectedPreviewTrafficDebugOverlay();
	renderPreviewTrafficCars();
	renderPreviewParkedCars();
}

void
RenderPedPaths(void)
{
	EnsureLoaded();
	if(!gLoadSucceeded)
		return;
	drawNodeSet(false);
	renderPreviewWalkers();
}

void
RenderAreaGrid(void)
{
	EnsureLoaded();
	if(!gLoadSucceeded)
		return;

	static const rw::RGBA gridCol = { 80, 180, 255, 255 };
	static const rw::RGBA selectedAreaCol = { 255, 200, 50, 255 };
	static const float GRID_Z = 5.0f;

	int selectedAreaId = selectedNode ? selectedNode->ownerAreaId : -1;

	// draw vertical lines (constant X, varying Y)
	for(int gx = 0; gx <= 8; gx++){
		float x = -3000.0f + gx * AREA_SIZE;
		// only draw segments near the camera
		for(int gy = 0; gy < 8; gy++){
			float y0 = -3000.0f + gy * AREA_SIZE;
			float y1 = y0 + AREA_SIZE;
			rw::V3d mid = { x, (y0 + y1) * 0.5f, GRID_Z };
			if(TheCamera.distanceTo(mid) > PATH_DRAW_DIST * 1.5f)
				continue;
			// highlight edges of selected area
			bool highlight = false;
			if(selectedAreaId >= 0){
				int selGx = getAreaGridX(selectedAreaId);
				int selGy = getAreaGridY(selectedAreaId);
				if((gx == selGx || gx == selGx + 1) && gy == selGy)
					highlight = true;
			}
			rw::RGBA c = highlight ? selectedAreaCol : gridCol;
			rw::V3d a = { x, y0, GRID_Z };
			rw::V3d b = { x, y1, GRID_Z };
			RenderLine(a, b, c, c);
		}
	}
	// draw horizontal lines (constant Y, varying X)
	for(int gy = 0; gy <= 8; gy++){
		float y = -3000.0f + gy * AREA_SIZE;
		for(int gx = 0; gx < 8; gx++){
			float x0 = -3000.0f + gx * AREA_SIZE;
			float x1 = x0 + AREA_SIZE;
			rw::V3d mid = { (x0 + x1) * 0.5f, y, GRID_Z };
			if(TheCamera.distanceTo(mid) > PATH_DRAW_DIST * 1.5f)
				continue;
			bool highlight = false;
			if(selectedAreaId >= 0){
				int selGx = getAreaGridX(selectedAreaId);
				int selGy = getAreaGridY(selectedAreaId);
				if((gy == selGy || gy == selGy + 1) && gx == selGx)
					highlight = true;
			}
			rw::RGBA c = highlight ? selectedAreaCol : gridCol;
			rw::V3d a = { x0, y, GRID_Z };
			rw::V3d b = { x1, y, GRID_Z };
			RenderLine(a, b, c, c);
		}
	}
}

static void
ensureSelectedLinkIndex(void)
{
	if(selectedNode == nil){
		gSelectedLinkIndex = -1;
		return;
	}
	if(selectedNode->numLinks() <= 0){
		gSelectedLinkIndex = -1;
		return;
	}
	if(gSelectedLinkIndex < 0 || gSelectedLinkIndex >= selectedNode->numLinks())
		gSelectedLinkIndex = 0;
}

static void
selectLinkedNode(int slot)
{
	if(selectedNode == nil)
		return;
	AreaData *area = getArea(selectedNode->ownerAreaId);
	if(area == nil || slot < 0 || slot >= (int)area->nodeLinks.size())
		return;
	Node *other = getNode(area->nodeLinks[slot]);
	if(other){
		selectedNode = other;
		gSelectedLinkIndex = 0;
	}
}

static const char *behaviourTypeLabels[] = {
	"0 - Default",
	"1 - Roadblock",
	"2 - Parking",
	"3 - Unknown",
	"4 - Unknown",
	"5 - Unknown",
	"6 - Unknown",
	"7 - Unknown",
	"8 - Unknown",
	"9 - Unknown",
	"10 - Unknown",
	"11 - Unknown",
	"12 - Unknown",
	"13 - Unknown",
	"14 - Unknown",
	"15 - Unknown",
};

static const char *trafficLightStateLabels[] = {
	"0 - Disabled (no traffic light)",
	"1 - North-South phase",
	"2 - West-East phase",
	"3 - Unused (undefined in vanilla)",
};

static void
drawNodeEditor(void)
{
	ensureSelectedLinkIndex();
	if(selectedNode == nil)
		return;

	AreaData *area = getArea(selectedNode->ownerAreaId);
	if(area == nil)
		return;

	bool isVehicle = isVehicleNode(selectedNode);
	ImVec4 nodeTypeColor = isVehicle ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(1.0f, 0.3f, 1.0f, 1.0f);
	ImGui::TextColored(nodeTypeColor, "%s  |  Area %d  Node %d",
		isVehicle ? "Vehicle Node" : "Ped Node",
		selectedNode->raw.areaId, selectedNode->raw.nodeId);
	ImGui::TextDisabled("Source: %s", area->sourcePath[0] ? area->sourcePath : "(unknown)");
	ImGui::TextDisabled("Stream area: %d  (grid %d, %d)  |  bounds X [%.0f, %.0f]  Y [%.0f, %.0f]",
		selectedNode->ownerAreaId,
		getAreaGridX(selectedNode->ownerAreaId), getAreaGridY(selectedNode->ownerAreaId),
		getAreaMinX(selectedNode->ownerAreaId), getAreaMaxX(selectedNode->ownerAreaId),
		getAreaMinY(selectedNode->ownerAreaId), getAreaMaxY(selectedNode->ownerAreaId));
	if(area->dirty)
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Area has unsaved edits");
	if(gBlockedCrossAreaNodeMove)
		ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
			"Blocked: nodes cannot cross SA streamed area boundaries yet.");
	if(gBlockedCrossAreaNaviMove)
		ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
			"Blocked: moving this car node would push attached navis into another streamed area.");

	ImGui::Separator();

	float pos[3] = { selectedNode->x(), selectedNode->y(), selectedNode->z() };
	if(ImGui::DragFloat3("Position", pos, 0.125f)){
		SetSelectedNodePosition({ pos[0], pos[1], pos[2] }, true);
	}
	ImGui::SetItemTooltip("World position of this node.\nDrag to move, or use the translate gizmo (W key) in the viewport.\nQuantized to 0.125 units (1/8). Nodes cannot cross area boundaries.");

	float width = selectedNode->width();
	if(ImGui::DragFloat("Path Width", &width, 0.0625f, 0.0f, 16.0f)){
		selectedNode->setWidth(width);
		markAreaDirty(selectedNode->ownerAreaId);
	}
	ImGui::SetItemTooltip("Width of the path at this node, in game units.\nAffects how far from the center vehicles/peds can deviate.\nQuantized to 0.0625 units (1/16).");

	ImGui::Separator();

	int spawn = selectedNode->spawnProbability();
	if(ImGui::SliderInt("Spawn Probability", &spawn, 0, 15)){
		selectedNode->setSpawnProbability(spawn);
		markAreaDirty(selectedNode->ownerAreaId);
	}
	ImGui::SetItemTooltip("Traffic/ped spawn weight at this node (0-15).\n"
		"SA uses: (rand & 0xF) > min(nodeA, nodeB) to skip spawning.\n"
		"0 = very rare (~6%% chance), 15 = guaranteed spawn.\n"
		"The minimum of both connected nodes is used.");

	int behaviour = selectedNode->behaviourType();
	if(ImGui::Combo("Behaviour Type", &behaviour, behaviourTypeLabels, IM_ARRAYSIZE(behaviourTypeLabels))){
		selectedNode->setBehaviourType(behaviour);
		markAreaDirty(selectedNode->ownerAreaId);
	}
	ImGui::SetItemTooltip("Special behaviour assigned to this node.\n"
		"0 = Default (normal path node)\n"
		"1 = Roadblock (police can set up roadblocks here)\n"
		"2 = Parking (vehicles may park at this node)");

	ImGui::Separator();
	ImGui::TextDisabled("Flags");

	bool onDeadEnd = selectedNode->onDeadEnd();
	if(ImGui::Checkbox("Dead End", &onDeadEnd)){
		selectedNode->setOnDeadEnd(onDeadEnd);
		markAreaDirty(selectedNode->ownerAreaId);
	}
	ImGui::SetItemTooltip("This node is at a dead end.\nVehicles will turn around here rather than continuing.");
	ImGui::SameLine();
	bool switchedOff = selectedNode->isSwitchedOff();
	if(ImGui::Checkbox("Switched Off", &switchedOff)){
		selectedNode->setSwitchedOff(switchedOff);
		markAreaDirty(selectedNode->ownerAreaId);
	}
	ImGui::SetItemTooltip("Soft-disables this node. Not a hard block:\n"
		"- Excluded from low-traffic node selection\n"
		"- AI cannot enter from a non-switched node (directional barrier)\n"
		"- Skipped as vehicle spawn point in some contexts\n"
		"Used by missions/scripts to close roads via SwitchRoadsOffInArea.");

	bool roadBlocks = selectedNode->roadBlocks();
	if(ImGui::Checkbox("Road Block", &roadBlocks)){
		selectedNode->setRoadBlocks(roadBlocks);
		markAreaDirty(selectedNode->ownerAreaId);
	}
	ImGui::SetItemTooltip("Road block flag on this node.\nNote: actual roadblock placement in vanilla SA is driven by\ndata/paths/roadblox.dat, not this flag alone.\nExact vanilla effect of this bit is not fully confirmed.");
	ImGui::SameLine();
	bool water = selectedNode->waterNode();
	if(ImGui::Checkbox("Water Node", &water)){
		selectedNode->setWaterNode(water);
		markAreaDirty(selectedNode->ownerAreaId);
	}
	ImGui::SetItemTooltip("This is a boat/water path node.\nOnly boats will use this node for navigation.");

	bool dontWander = selectedNode->dontWander();
	if(ImGui::Checkbox("Dont Wander", &dontWander)){
		selectedNode->setDontWander(dontWander);
		markAreaDirty(selectedNode->ownerAreaId);
	}
	ImGui::SetItemTooltip("Peds will not wander to this node.\nPrevents idle pedestrians from walking here.");
	ImGui::SameLine();
	bool highway = selectedNode->highway();
	if(ImGui::Checkbox("Highway", &highway)){
		selectedNode->setHighway(highway);
		markAreaDirty(selectedNode->ownerAreaId);
	}
	ImGui::SetItemTooltip("Marks this node as a highway node.\nPresent in vanilla path data but no confirmed gameplay\neffect found in the reversed SA source.");

	bool notHighway = selectedNode->notHighway();
	if(ImGui::Checkbox("Not Highway", &notHighway)){
		selectedNode->setNotHighway(notHighway);
		markAreaDirty(selectedNode->ownerAreaId);
	}
	ImGui::SetItemTooltip("Marks this node as explicitly not a highway.\nPresent in vanilla path data but no confirmed gameplay\neffect found in the reversed SA source.");
	ImGui::SameLine();
	bool switchedOffOriginal = selectedNode->switchedOffOriginal();
	ImGui::BeginDisabled();
	ImGui::Checkbox("Orig Switched Off", &switchedOffOriginal);
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Read-only. The original value of 'Switched Off' when the game loaded this area.\nSA copies this from the Switched Off flag at load time.");
}

static void
drawLinksEditor(void)
{
	if(selectedNode == nil)
		return;
	AreaData *area = getArea(selectedNode->ownerAreaId);
	if(area == nil || selectedNode->numLinks() <= 0)
		return;

	ensureSelectedLinkIndex();
	ImGui::Separator();
	ImGui::Text("Links (%d)", selectedNode->numLinks());
	ImGui::SetItemTooltip("Outgoing connections from this node to other path nodes.\nEach link can have a navi node (lane waypoint) between the two endpoints.\nClick a row to inspect/edit that link's details below.");
	if(ImGui::BeginTable("SAPathLinks", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)){
		ImGui::TableSetupColumn("#");
		ImGui::TableSetupColumn("Target Node");
		ImGui::TableSetupColumn("Length");
		ImGui::TableSetupColumn("Road Cross");
		ImGui::TableSetupColumn("Ped Light");
		ImGui::TableSetupColumn("Navi Node");
		ImGui::TableSetupColumn("Same Lanes");
		ImGui::TableSetupColumn("Opp Lanes");
		ImGui::TableHeadersRow();
		for(int li = 0; li < selectedNode->numLinks(); li++){
			int slot = selectedNode->raw.baseLinkId + li;
			if(slot < 0 || slot >= (int)area->nodeLinks.size())
				continue;
			NodeAddress target = area->nodeLinks[slot];
			CarPathLinkAddress naviAddr = slot < (int)area->naviLinks.size() ? area->naviLinks[slot] : CarPathLinkAddress{ 0xFFFF };
			DiskCarPathLink *navi = nil;
			AreaData *naviArea = nil;
			if(naviAddr.isValid()){
				naviArea = getArea(naviAddr.areaId());
				if(naviArea && naviAddr.nodeId() < naviArea->naviNodes.size())
					navi = &naviArea->naviNodes[naviAddr.nodeId()];
			}

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			char label[32];
			snprintf(label, sizeof(label), "%d", li);
			if(ImGui::Selectable(label, li == gSelectedLinkIndex, ImGuiSelectableFlags_SpanAllColumns))
				gSelectedLinkIndex = li;

			ImGui::TableSetColumnIndex(1);
			if(target.isValid()){
				char targetLabel[32];
				snprintf(targetLabel, sizeof(targetLabel), "%d:%d##sa_link_%d", target.areaId, target.nodeId, li);
				if(ImGui::SmallButton(targetLabel))
					selectLinkedNode(slot);
			}else{
				ImGui::TextDisabled("none");
			}
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%u", area->linkLengths[slot]);
			ImGui::TableSetColumnIndex(3);
			ImGui::TextUnformatted(getRoadCross(area->intersections[slot]) ? "Y" : "-");
			ImGui::TableSetColumnIndex(4);
			ImGui::TextUnformatted(getPedTrafficLight(area->intersections[slot]) ? "Y" : "-");
			ImGui::TableSetColumnIndex(5);
			if(navi)
				ImGui::Text("%d:%d", naviAddr.areaId(), naviAddr.nodeId());
			else
				ImGui::TextDisabled("-");
			ImGui::TableSetColumnIndex(6);
			if(navi) ImGui::Text("%d", getNaviSameLanes(*navi)); else ImGui::TextDisabled("-");
			ImGui::TableSetColumnIndex(7);
			if(navi) ImGui::Text("%d", getNaviOppositeLanes(*navi)); else ImGui::TextDisabled("-");
		}
		ImGui::EndTable();
	}

	if(gSelectedLinkIndex < 0 || gSelectedLinkIndex >= selectedNode->numLinks())
		return;

	int slot = selectedNode->raw.baseLinkId + gSelectedLinkIndex;
	if(slot < 0 || slot >= (int)area->nodeLinks.size())
		return;
	NodeAddress sourceAddr = selectedNode->address();
	NodeAddress targetAddr = area->nodeLinks[slot];
	uint16 reciprocalAreaId = 0xFFFF;
	int reciprocalSlot = -1;
	bool hasReciprocal = targetAddr.isValid() &&
		findReciprocalLinkSlot(sourceAddr, targetAddr, &reciprocalAreaId, &reciprocalSlot);

	ImGui::Separator();
	ImGui::Text("Selected Link %d", gSelectedLinkIndex);
	if(hasReciprocal)
		ImGui::TextDisabled("Reciprocal link found. Length and intersection flags are mirrored;\nnavi edits below apply only to this direction.");
	else if(targetAddr.isValid())
		ImGui::TextDisabled("No reciprocal link found (one-way connection).");

	int length = area->linkLengths[slot];
	if(ImGui::SliderInt("Link Length", &length, 0, 255)){
		area->linkLengths[slot] = (uint8)length;
		markAreaDirty(selectedNode->ownerAreaId);
		if(hasReciprocal){
			AreaData *recArea = getArea(reciprocalAreaId);
			if(recArea && reciprocalSlot >= 0 && reciprocalSlot < (int)recArea->linkLengths.size()){
				recArea->linkLengths[reciprocalSlot] = (uint8)length;
				setAreaDirty(reciprocalAreaId, false);
			}
		}
	}
	ImGui::SetItemTooltip("Precomputed distance between the two nodes (0-255).\nUsed by the AI pathfinder to pick shortest routes.\nAutomatically recomputed when you move nodes with the gizmo.");

	bool roadCross = getRoadCross(area->intersections[slot]);
	if(ImGui::Checkbox("Crosses Road", &roadCross)){
		setRoadCross(area->intersections[slot], roadCross);
		markAreaDirty(selectedNode->ownerAreaId);
		if(hasReciprocal){
			AreaData *recArea = getArea(reciprocalAreaId);
			if(recArea && reciprocalSlot >= 0 && reciprocalSlot < (int)recArea->intersections.size()){
				setRoadCross(recArea->intersections[reciprocalSlot], roadCross);
				setAreaDirty(reciprocalAreaId, false);
			}
		}
	}
	ImGui::SetItemTooltip("This link crosses a road.\nPeds will look both ways before crossing here.");
	ImGui::SameLine();
	bool pedLight = getPedTrafficLight(area->intersections[slot]);
	if(ImGui::Checkbox("Ped Traffic Light", &pedLight)){
		setPedTrafficLight(area->intersections[slot], pedLight);
		markAreaDirty(selectedNode->ownerAreaId);
		if(hasReciprocal){
			AreaData *recArea = getArea(reciprocalAreaId);
			if(recArea && reciprocalSlot >= 0 && reciprocalSlot < (int)recArea->intersections.size()){
				setPedTrafficLight(recArea->intersections[reciprocalSlot], pedLight);
				setAreaDirty(reciprocalAreaId, false);
			}
		}
	}
	ImGui::SetItemTooltip("Pedestrian traffic light present at this crossing.\nPeds will wait for the light before crossing.");

	if(isVehicleNode(selectedNode) && slot < (int)area->naviLinks.size()){
		CarPathLinkAddress naviAddr = area->naviLinks[slot];
		if(naviAddr.isValid()){
			AreaData *naviArea = getArea(naviAddr.areaId());
			if(naviArea && naviAddr.nodeId() < naviArea->naviNodes.size()){
				DiskCarPathLink &navi = naviArea->naviNodes[naviAddr.nodeId()];

				ImGui::Separator();
				ImGui::Text("Navi Node  (%d:%d)", naviAddr.areaId(), naviAddr.nodeId());
				ImGui::SetItemTooltip("Navi nodes are lane waypoints placed between two path nodes.\n"
					"They define road width, lane count, and traffic direction\n"
					"for the road segment between this node and the target.");
				if(ImGui::Button("Dump Preview Traffic Debug")){
					dumpPreviewTrafficLinkDebug(sourceAddr, targetAddr, slot, naviAddr);
					if(hasReciprocal){
						AreaData *recArea = getArea(reciprocalAreaId);
						if(recArea && reciprocalSlot >= 0 &&
						   reciprocalSlot < (int)recArea->naviLinks.size()){
							CarPathLinkAddress recNaviAddr = recArea->naviLinks[reciprocalSlot];
							NodeAddress reciprocalSource = targetAddr;
							NodeAddress reciprocalTarget = sourceAddr;
							if(recNaviAddr.isValid())
								dumpPreviewTrafficLinkDebug(reciprocalSource, reciprocalTarget, reciprocalSlot, recNaviAddr);
						}
					}
				}
				ImGui::SetItemTooltip("Logs the selected link, its reciprocal direction when present, and any active or nearest preview cars.\nUse this on a problematic highway segment, then inspect the app log/terminal output.");

				float naviPos[2] = { navi.x * NAVI_POS_SCALE, navi.y * NAVI_POS_SCALE };
				if(ImGui::DragFloat2("Navi Pos", naviPos, 0.125f)){
					navi.x = (int16)(naviPos[0] / NAVI_POS_SCALE);
					navi.y = (int16)(naviPos[1] / NAVI_POS_SCALE);
					Node *attached = getNode({ navi.attachedAreaId, navi.attachedNodeId });
					if(attached)
						alignNaviDirectionToAttachedNode(navi, *attached);
					markAreaDirty(naviAddr.areaId());
				}
				ImGui::SetItemTooltip("2D position (X, Y) of this navi waypoint.\nUsually placed at the midpoint between two path nodes.\nQuantized to 0.125 units.");

				int naviDir[2] = { navi.dirX, navi.dirY };
				if(ImGui::SliderInt2("Navi Direction", naviDir, -100, 100)){
					navi.dirX = (int8)clamp(naviDir[0], -128, 127);
					navi.dirY = (int8)clamp(naviDir[1], -128, 127);
					markAreaDirty(naviAddr.areaId());
				}
				ImGui::SetItemTooltip("Direction vector of traffic flow (X, Y).\nScaled to -100..100. Determines which way vehicles drive.\nUse 'Point Dir To Node' to auto-calculate from geometry.");
				ImGui::SameLine();
				if(ImGui::Button("Point Dir To Node")){
					Node *attached = getNode({ navi.attachedAreaId, navi.attachedNodeId });
					if(attached){
						alignNaviDirectionToAttachedNode(navi, *attached);
						markAreaDirty(naviAddr.areaId());
					}
				}
				ImGui::SetItemTooltip("Auto-compute direction vector pointing from this navi toward its attached node.");

				float naviWidth = getNaviWidth(navi);
				if(ImGui::DragFloat("Navi Width", &naviWidth, 0.0625f, 0.0f, 16.0f)){
					navi.pathWidth = (int8)clamp((int)(naviWidth / NAVI_WIDTH_SCALE + 0.5f), -128, 127);
					markAreaDirty(naviAddr.areaId());
				}
				ImGui::SetItemTooltip("Total road width at this navi waypoint.\nAffects lane positioning and vehicle lateral offset.");

				int same = getNaviSameLanes(navi);
				if(ImGui::SliderInt("Same Dir Lanes", &same, 0, 7)){
					setNaviSameLanes(navi, same);
					markAreaDirty(naviAddr.areaId());
				}
				ImGui::SetItemTooltip("Number of lanes going in the same direction as the navi direction.\nThese are the 'right side' lanes (following traffic flow).");

				int opposite = getNaviOppositeLanes(navi);
				if(ImGui::SliderInt("Opposite Dir Lanes", &opposite, 0, 7)){
					setNaviOppositeLanes(navi, opposite);
					markAreaDirty(naviAddr.areaId());
				}
				ImGui::SetItemTooltip("Number of lanes going opposite to the navi direction.\nThese are the 'left side' / oncoming traffic lanes.\nSet to 0 for one-way roads.");

				bool tlDir = getNaviTrafficLightDir(navi);
				if(ImGui::Checkbox("Traffic Light Direction", &tlDir)){
					setNaviTrafficLightDir(navi, tlDir);
					markAreaDirty(naviAddr.areaId());
				}
				ImGui::SetItemTooltip("Whether this navi's traffic flow matches the traffic light cycle.\nUsed to sync vehicle stopping with light phases.");
				ImGui::SameLine();
				bool bridge = getNaviBridgeLights(navi);
				if(ImGui::Checkbox("Bridge Lights", &bridge)){
					setNaviBridgeLights(navi, bridge);
					markAreaDirty(naviAddr.areaId());
				}
				ImGui::SetItemTooltip("Bridge/train crossing signal flag.\nIn vanilla SA, train crossing objects set this flag via\nSetLinksBridgeLights when a train approaches.\nVehicles read it (ShouldCarStopForBridge) and stop\nif the flag transitions from unset to set on their next navi.");

				int tlState = getNaviTrafficLightState(navi);
				if(ImGui::Combo("Traffic Light State", &tlState, trafficLightStateLabels, IM_ARRAYSIZE(trafficLightStateLabels))){
					setNaviTrafficLightState(navi, tlState);
					markAreaDirty(naviAddr.areaId());
				}
				ImGui::SetItemTooltip("Which traffic light phase controls this road segment.\n"
					"0 = Disabled (cars pass freely, no light)\n"
					"1 = North-South phase (uses LightForCars1)\n"
					"2 = West-East phase (uses LightForCars2)\n"
					"3 = Unused in vanilla SA (undefined behavior)");
			}
		}
	}
}

int
GetDirtyAreaCount(void)
{
	int count = 0;
	for(int i = 0; i < NUM_PATH_AREAS; i++)
		if(gAreas[i].dirty)
			count++;
	return count;
}

bool
HasDirtyAreas(void)
{
	return GetDirtyAreaCount() > 0;
}

static bool
buildAreaLogicalPathForActiveSource(int areaId, char *dst, size_t size)
{
	switch(gActiveLoadSource){
	case ACTIVE_LOAD_SOURCE_LOOSE:
		return buildAreaLegacyLogicalPath(areaId, dst, size);
	case ACTIVE_LOAD_SOURCE_IMG:
		if(areaId >= 0 && areaId < NUM_PATH_AREAS && gAreas[areaId].logicalPath[0] != '\0'){
			int written = snprintf(dst, size, "%s", gAreas[areaId].logicalPath);
			return written >= 0 && (size_t)written < size;
		}
		return buildAreaPathsImgLogicalPath(areaId, dst, size);
	default:
		return buildAreaGta3ImgLogicalPath(areaId, dst, size);
	}
}

static bool
writeBufferToPath(const char *path, const std::vector<uint8> &data)
{
	if(path == nil || path[0] == '\0' || !EnsureParentDirectoriesForPath(path))
		return false;
	FILE *f = fopen(path, "wb");
	if(f == nil)
		return false;
	bool ok = data.empty() ? true : writeExact(f, &data[0], data.size());
	fclose(f);
	return ok;
}

bool
SaveDirtyAreas(int *savedAreas)
{
	EnsureLoaded();
	if(!gLoadSucceeded)
		return false;

	int count = 0;
	for(int areaId = 0; areaId < NUM_PATH_AREAS; areaId++){
		AreaData &area = gAreas[areaId];
		if(!area.loaded || !area.dirty)
			continue;

		std::vector<uint8> data;
		buildAreaFileData(area, data);

		char logicalPath[64];
		if(!buildAreaLogicalPathForActiveSource(areaId, logicalPath, sizeof(logicalPath))){
			log("SAPaths: failed to build logical path for area %d\n", areaId);
			return false;
		}

		bool ok = false;
		if(gSaveDestination == SAVE_DESTINATION_MODLOADER){
			char exportPath[1024];
			if(!BuildModloaderLogicalExportPath(logicalPath, exportPath, sizeof(exportPath))){
				log("SAPaths: failed to resolve export path for area %d\n", areaId);
				return false;
			}
			ok = writeBufferToPath(exportPath, data);
			if(!ok){
				log("SAPaths: couldn't write %s\n", exportPath);
				return false;
			}
		}else if(gActiveLoadSource == ACTIVE_LOAD_SOURCE_LOOSE){
			ok = writeBufferToPath(area.sourcePath, data);
			if(!ok){
				log("SAPaths: couldn't write %s\n", area.sourcePath);
				return false;
			}
		}else{
			int entryBytes = 0;
			bool compressed = false;
			bool looseOverride = false;
			char resolvedPath[1024];
			if(!GetCdImageEntryInfoByLogicalPath(logicalPath, &entryBytes, &compressed, &looseOverride,
			                                     resolvedPath, sizeof(resolvedPath))){
				log("SAPaths: failed to resolve IMG entry metadata for %s\n", logicalPath);
				return false;
			}
			if(compressed){
				log("SAPaths: refusing direct save to compressed IMG entry %s\n", logicalPath);
				return false;
			}
			if(looseOverride){
				ok = WriteCdImageEntryByLogicalPath(logicalPath, data.empty() ? nil : &data[0], (int)data.size());
			}else{
				if((int)data.size() > entryBytes){
					log("SAPaths: rebuilt area %d (%d bytes) exceeds IMG span %d in %s\n",
					    areaId, (int)data.size(), entryBytes, resolvedPath);
					return false;
				}
				std::vector<uint8> padded(entryBytes, 0);
				if(!data.empty())
					memcpy(&padded[0], &data[0], data.size());
				ok = WriteCdImageEntryByLogicalPath(logicalPath, padded.empty() ? nil : &padded[0], (int)padded.size());
			}
			if(!ok){
				log("SAPaths: couldn't write %s\n", resolvedPath);
				return false;
			}
		}

		area.dirty = false;
		count++;
	}

	if(savedAreas)
		*savedAreas = count;
	return true;
}

bool
CollectDirtyAreaBackupFiles(std::vector<BackupFile> &files)
{
	EnsureLoaded();
	if(!gLoadSucceeded)
		return false;

	files.clear();
	for(int areaId = 0; areaId < NUM_PATH_AREAS; areaId++){
		AreaData &area = gAreas[areaId];
		if(!area.loaded || !area.dirty)
			continue;

		BackupFile file;
		if(!buildAreaLogicalPathForActiveSource(areaId, file.logicalPath, sizeof(file.logicalPath)))
			return false;
		strncpy(file.sourcePath, area.sourcePath, sizeof(file.sourcePath)-1);
		file.sourcePath[sizeof(file.sourcePath)-1] = '\0';
		buildAreaFileData(area, file.data);
		files.push_back(file);
	}
	return true;
}

bool
HasInfoToShow(void)
{
	EnsureLoaded();
	return isSA();
}

void
DrawInfoPanel(void)
{
	EnsureLoaded();

	int dirtyAreas = GetDirtyAreaCount();
	const char *modeLabels[] = {
		"Auto",
		"IMG (models/paths.img or models/gta3.img)",
		"Loose (data/Paths)"
	};
	int mode = (int)gLoadSourceMode;
	bool canChangeSource = dirtyAreas == 0;
	if(!canChangeSource)
		ImGui::BeginDisabled();
	if(ImGui::Combo("SA Path Source", &mode, modeLabels, IM_ARRAYSIZE(modeLabels))){
		gLoadSourceMode = (LoadSourceMode)mode;
		Reset();
		EnsureLoaded();
	}
	if(!canChangeSource)
		ImGui::EndDisabled();
	if(!canChangeSource)
		ImGui::TextDisabled("Save or discard SA path edits before switching source.");
	ImGui::SameLine();
	if(!canChangeSource)
		ImGui::BeginDisabled();
	if(ImGui::Button("Reload SA Paths")){
		Reset();
		EnsureLoaded();
	}
	if(!canChangeSource)
		ImGui::EndDisabled();

	ImGui::TextDisabled("Requested: %s", loadSourceModeLabel(gLoadSourceMode));
	ImGui::TextDisabled("Active: %s%s",
	                    gActiveLoadSourceDetail[0] ? gActiveLoadSourceDetail : activeLoadSourceLabel(gActiveLoadSource),
	                    gUsedLenientImgRead ? " (lenient IMG read)" : "");

	if(gLoadFailed){
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Failed to load streamed SA path areas");
		if(gLastLoadFailureArea >= 0)
			ImGui::TextDisabled("First failure: area %d (%s)", gLastLoadFailureArea,
			                    gLastLoadFailureReason[0] ? gLastLoadFailureReason : "unknown");
		if(gLastLoadFailurePath[0] != '\0')
			ImGui::TextDisabled("Path: %s", gLastLoadFailurePath);
		return;
	}

	if(ImGui::Button("Save SA Path Areas")){
		int saved = 0;
		if(SaveDirtyAreas(&saved))
			Toast(TOAST_SAVE, "Saved %d SA path area(s)", saved);
		else
			Toast(TOAST_SAVE, "Failed to save SA path areas");
	}
	ImGui::SameLine();
	ImGui::Text("Dirty areas: %d", dirtyAreas);

	if(ImGui::TreeNode("How to Use")){
		ImGui::TextWrapped(
			"SA uses a streamed path network split into 64 areas (8x8 grid, 750 units each). "
			"The path system has three layers:");
		ImGui::BulletText("Nodes: intersections and waypoints on the road/sidewalk network");
		ImGui::BulletText("Links: connections between nodes (each node can have up to 15)");
		ImGui::BulletText("Navi nodes: lane waypoints on vehicle links that define road width,\n"
			"lane count, and traffic direction between two nodes");
		ImGui::Spacing();
		ImGui::TextWrapped("Controls:");
		ImGui::BulletText("Left-click a node in the viewport to select it");
		ImGui::BulletText("Press W to activate the translate gizmo, then drag to move");
		ImGui::BulletText("Vehicle nodes are shown as red boxes, ped nodes as magenta spheres");
		ImGui::BulletText("Link lines match node color (red/magenta), white when selected,\nyellow = road crossings, green = vehicle lanes");
		ImGui::BulletText("Hover over any field below for a description of what it does in-game");
		ImGui::Spacing();
		ImGui::TextWrapped(
			"Nodes cannot be moved across area boundaries. "
			"Edits are held in memory until you click Save.");
		ImGui::TreePop();
	}

	ImGui::Separator();

	if(selectedNode == nil){
		ImGui::TextDisabled("Select a streamed SA path node in the viewport to begin editing.");
		return;
	}

	drawNodeEditor();
	drawLinksEditor();
}

}
