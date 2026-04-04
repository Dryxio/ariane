#include "euryopa.h"
#include "modloader.h"
#include "sapaths.h"
#include <cctype>
#include <cmath>
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

static bool gPreviewMetadataAttempted;
static bool gPreviewMetadataLoaded;
static char gPreviewMetadataFailureReason[128];
static std::vector<PreviewAnimGroupDef> gPreviewAnimGroups;
static std::vector<PreviewPedDef> gPreviewPedDefs;
static std::vector<PreviewWalkCycle> gPreviewWalkCycles;
static std::vector<PreviewPedAssets> gPreviewPedAssetCache;
static std::vector<PreviewWalker> gPreviewWalkers;

static bool loadLogicalPathData(const char *logicalPath, std::vector<uint8> &data);

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

static bool
buildAreaImgLogicalPath(int areaId, char *dst, size_t size)
{
	int written = snprintf(dst, size, "models/gta3.img/nodes%d.dat", areaId);
	return written >= 0 && (size_t)written < size;
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
	data.clear();
	if(outSourcePath && outSourcePathSize > 0)
		outSourcePath[0] = '\0';
	if(outLogicalPath && outLogicalPathSize > 0)
		outLogicalPath[0] = '\0';
	if(!buildAreaImgLogicalPath(areaId, logicalPath, sizeof(logicalPath)))
		return false;
	if(outLogicalPath && outLogicalPathSize > 0){
		strncpy(outLogicalPath, logicalPath, outLogicalPathSize-1);
		outLogicalPath[outLogicalPathSize-1] = '\0';
	}
	return ReadCdImageEntryByLogicalPath(logicalPath, data, outSourcePath, outSourcePathSize);
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
	if(opposite)
		return 0.5f - same/2.0f;
	if(same)
		return 0.5f - getNaviWidth(link)/5.4f/2.0f;
	return 0.5f - opposite/2.0f;
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
	case LOAD_SOURCE_IMG: return "IMG";
	case LOAD_SOURCE_LOOSE: return "data/Paths";
	case LOAD_SOURCE_AUTO:
	default: return "Auto";
	}
}

static const char*
activeLoadSourceLabel(ActiveLoadSource source)
{
	switch(source){
	case ACTIVE_LOAD_SOURCE_IMG: return "IMG";
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
	   !parseAreaData(areaId, fileData, sourcePath, &parsed, &requiredBytes, reason, sizeof(reason)) &&
	   isShortReadFailure(reason) &&
	   requiredBytes > fileData.size() &&
	   requiredBytes - fileData.size() <= MAX_LENIENT_IMG_OVERREAD_BYTES &&
	   logicalPath[0] != '\0' &&
	   ReadCdImageEntryByLogicalPathMinSize(logicalPath, requiredBytes, fileData, sourcePath, sizeof(sourcePath)) &&
	   parseAreaData(areaId, fileData, sourcePath, &parsed, &requiredBytes, reason, sizeof(reason))){
		int spillBytes = (int)(requiredBytes > originalSize ? requiredBytes - originalSize : 0);
		gUsedLenientImgRead = true;
		log("SAPaths: lenient IMG read recovered area %d from %s (needed %d extra bytes)\n",
		    areaId, sourcePath, spillBytes);
	}else if(!parseAreaData(areaId, fileData, sourcePath, &parsed, &requiredBytes, reason, sizeof(reason))){
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

void
Reset(void)
{
	clearAreas();
	clearPreviewWalkers();
	gLoadAttempted = false;
	gLoadSucceeded = false;
	gLoadFailed = false;
	gActiveLoadSource = ACTIVE_LOAD_SOURCE_NONE;
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
			break;
		}
	}

	gLoadFailed = !gLoadSucceeded;
	if(gLoadSucceeded){
		log("SAPaths: loaded %d streamed path areas from %s%s\n",
		    NUM_PATH_AREAS,
		    activeLoadSourceLabel(gActiveLoadSource),
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

bool
SetSelectedNodePosition(const rw::V3d &pos, bool notifyChange)
{
	EnsureLoaded();
	if(selectedNode == nil)
		return false;

	rw::V3d current = getNodePosition(*selectedNode);
	if(length(sub(current, pos)) < 0.0001f)
		return false;

	gBlockedCrossAreaNodeMove = false;
	gBlockedCrossAreaNaviMove = false;
	if(!positionBelongsToArea(selectedNode->ownerAreaId, pos.x, pos.y)){
		gBlockedCrossAreaNodeMove = true;
		return false;
	}

	rw::V3d delta = sub(pos, current);
	if(isVehicleNode(selectedNode) && !canMoveAttachedNavisWithNode(selectedNode->address(), delta)){
		gBlockedCrossAreaNaviMove = true;
		return false;
	}

	selectedNode->setPosition(pos.x, pos.y, pos.z);
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
			if(SphereIntersect(sphere, ray)){
				hoveredNode = &node;
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
	candidates.reserve(256);

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
		}
	}

	if(candidates.empty())
		return false;

	int index = (int)(previewSeedFloat01(seed) * candidates.size());
	index = clamp(index, 0, (int)candidates.size()-1);
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

	for(int attempts = 0; attempts < 16; attempts++){
		int pedIndex = (int)(previewSeedFloat01(walker.seed) * gPreviewPedDefs.size());
		pedIndex = clamp(pedIndex, 0, (int)gPreviewPedDefs.size()-1);
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

	if(!pickPreviewSpawnNode(walker.seed, &walker.current))
		return false;
	if(!chooseNextPreviewPedNode(walker.current, walker.previous, walker.direction, &walker.target, &walker.direction))
		return false;

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

void
RenderCarPaths(void)
{
	EnsureLoaded();
	if(!gLoadSucceeded)
		return;
	drawNodeSet(true);
	drawVehicleLanes();
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

				float naviPos[2] = { navi.x * NAVI_POS_SCALE, navi.y * NAVI_POS_SCALE };
				if(ImGui::DragFloat2("Navi Pos", naviPos, 0.125f)){
					navi.x = (int16)(naviPos[0] / NAVI_POS_SCALE);
					navi.y = (int16)(naviPos[1] / NAVI_POS_SCALE);
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
	default:
		return buildAreaImgLogicalPath(areaId, dst, size);
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
		"IMG (models/gta3.img)",
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
	ImGui::TextDisabled("Active: %s%s", activeLoadSourceLabel(gActiveLoadSource),
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
