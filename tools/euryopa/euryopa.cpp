#include "euryopa.h"
#include "modloader.h"
#include <limits.h>
#include <algorithm>
#include <stdlib.h>
#include <string.h>
#include <vector>

int gameversion;
int gameplatform;

Params params;
SaveDestination gSaveDestination = SAVE_DESTINATION_ORIGINAL_FILES;

int gGizmoMode = GIZMO_TRANSLATE;
bool gGizmoEnabled = true;
bool gGizmoHovered = false;
bool gGizmoUsing = false;
bool gPlaceSnapToObjects = true;
bool gPlaceSnapToGround = true;
bool gDragFollowGround = false;
bool gDragAlignToSurface = false;

// Brush tool
bool gBrushMode = false;
float gBrushZOffset = 0.0f;
bool gBrushAlignToSurface = false;
bool gBrushRandomYaw = false;
float gBrushSpacing = 2.0f;
bool gGizmoSnap = false;
float gGizmoSnapAngle = 15.0f;
float gGizmoSnapTranslate = 1.0f;

// Rect-select (marquee selection)
bool gRectSelectActive = false;
static bool sRectSelectDragging = false;
static float sRectStartX, sRectStartY;
static const float RECT_SELECT_THRESHOLD_SQ = 25.0f;	// 5px squared

int gameTxdSlot;

int currentHour = 12;
int currentMinute = 0;
int extraColours = -1;
int currentArea;

// Options

bool gRenderOnlyLod;
bool gRenderOnlyHD;
bool gRenderBackground = true;
bool gRenderWater = true;
bool gRenderPostFX = true;
bool gEnableFog = true;
bool gEnableTimecycleBoxes = true;
bool gUseBlurAmb = gRenderPostFX;
bool gOverrideBlurAmb = true;
bool gNoTimeCull;
bool gNoAreaCull;
bool gDoBackfaceCulling;	// init from params
bool gPlayAnimations = true;
bool gUseViewerCam;
bool gDrawTarget = true;

struct IplVisibilityEntry
{
	char key[256];
	char label[260];
	bool visible;
};
static std::vector<IplVisibilityEntry> gIplVisibilityEntries;
static bool gIplVisibilityEntriesDirty = true;

// non-rendering things
bool gRenderCollision;
bool gRenderZones;
bool gRenderMapZones;
bool gRenderNavigZones;
bool gRenderInfoZones;
bool gRenderCullZones;
bool gRenderAttribZones;
bool gRenderLegacyPedPaths;
bool gRenderLegacyCarPaths;
bool gRenderSaPedPaths;
bool gRenderSaPedPathWalkers;
bool gRenderSaCarPaths;
bool gRenderSaCarPathTraffic;
bool gRenderSaAreaGrid;
bool gRenderLightEffects = true;
bool gRenderEffects;
bool gRenderTimecycleBoxes;
int gSaPedPathWalkerCount = 12;
int gSaCarPathTrafficCount = 10;
float gSaCarPathTrafficSpeedScale = 1.0f;
bool gSaCarPathTrafficFreezeRoutes = false;
bool gRenderSaCarPathParkedCars = false;
int gSaCarPathParkedCarCount = 8;
uint32 gRequestedAASamples = 1;

// SA postfx
int  gColourFilter;
bool gRadiosity;

// SA building pipe
int gBuildingPipeSwitch = PLATFORM_PS2;
float gDayNightBalance;
float gWetRoadEffect;

// Neo stuff
float gNeoLightMapStrength = 0.5f;

static bool
buildIplVisibilityKey(const char *name, char *dst, size_t size)
{
	const char *base, *end, *dot;
	size_t len;

	if(dst == nil || size == 0 || name == nil || name[0] == '\0')
		return false;

	base = name;
	end = name + strlen(name);
	dot = end;
	for(const char *s = end; s > base; ){
		s--;
		if(*s == '.'){
			dot = s;
			break;
		}
	}
	len = dot - base;
	if(len == 0)
		len = end - base;
	if(len == 0)
		return false;

	if(len >= size)
		len = size - 1;
	memcpy(dst, base, len);
	dst[len] = '\0';
	return len > 0;
}

static bool
buildInstIplVisibilityKey(const ObjectInst *inst, char *dst, size_t size)
{
	if(inst == nil || dst == nil || size == 0)
		return false;
	if(inst->m_iplFilterKey[0] != '\0'){
		strncpy(dst, inst->m_iplFilterKey, size-1);
		dst[size-1] = '\0';
		return true;
	}
	if(inst->m_file == nil || inst->m_file->name == nil)
		return false;
	return buildIplVisibilityKey(inst->m_file->name, dst, size);
}

static int
findIplVisibilityEntryIndex(const char *key)
{
	int low = 0;
	int high = (int)gIplVisibilityEntries.size();
	while(low < high){
		int mid = low + (high - low)/2;
		int cmp = rw::strcmp_ci(gIplVisibilityEntries[mid].key, key);
		if(cmp < 0)
			low = mid + 1;
		else
			high = mid;
	}
	if(low < (int)gIplVisibilityEntries.size() &&
	   rw::strcmp_ci(gIplVisibilityEntries[low].key, key) == 0)
		return low;
	return -1;
}

void
RefreshIplVisibilityEntries(void)
{
	if(!gIplVisibilityEntriesDirty)
		return;

	std::vector<IplVisibilityEntry> entries;
	CPtrNode *p;

	for(p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		char key[256];

		if(!buildInstIplVisibilityKey(inst, key, sizeof(key)))
			continue;

		int low = 0;
		int high = (int)entries.size();
		while(low < high){
			int mid = low + (high - low)/2;
			int cmp = rw::strcmp_ci(entries[mid].key, key);
			if(cmp < 0)
				low = mid + 1;
			else
				high = mid;
		}
		if(low < (int)entries.size() &&
		   rw::strcmp_ci(entries[low].key, key) == 0)
			continue;

		IplVisibilityEntry entry;
		memset(&entry, 0, sizeof(entry));
		strncpy(entry.key, key, sizeof(entry.key)-1);
		snprintf(entry.label, sizeof(entry.label), "%s.ipl", key);

		int oldIndex = findIplVisibilityEntryIndex(key);
		entry.visible = oldIndex < 0 ? true : gIplVisibilityEntries[oldIndex].visible;
		entries.insert(entries.begin() + low, entry);
	}

	gIplVisibilityEntries.swap(entries);
	gIplVisibilityEntriesDirty = false;
}

int
GetIplVisibilityEntryCount(void)
{
	return (int)gIplVisibilityEntries.size();
}

const char*
GetIplVisibilityEntryName(int i)
{
	if(i < 0 || i >= (int)gIplVisibilityEntries.size())
		return "";
	return gIplVisibilityEntries[i].label;
}

bool
GetIplVisibilityEntryVisible(int i)
{
	if(i < 0 || i >= (int)gIplVisibilityEntries.size())
		return true;
	return gIplVisibilityEntries[i].visible;
}

void
SetIplVisibilityEntryVisible(int i, bool visible)
{
	if(i < 0 || i >= (int)gIplVisibilityEntries.size())
		return;
	gIplVisibilityEntries[i].visible = visible;
}

void
SetAllIplVisibilityEntries(bool visible)
{
	for(size_t i = 0; i < gIplVisibilityEntries.size(); i++)
		gIplVisibilityEntries[i].visible = visible;
}

void
ShowOnlyIplVisibilityEntry(int i)
{
	if(i < 0 || i >= (int)gIplVisibilityEntries.size())
		return;

	for(size_t j = 0; j < gIplVisibilityEntries.size(); j++)
		gIplVisibilityEntries[j].visible = (int)j == i;
}

bool
IsInstVisibleByIplFilter(const ObjectInst *inst)
{
	char key[256];
	int i;

	if(!buildInstIplVisibilityKey(inst, key, sizeof(key)))
		return true;

	i = findIplVisibilityEntryIndex(key);
	if(i < 0){
		RefreshIplVisibilityEntries();
		i = findIplVisibilityEntryIndex(key);
	}
	if(i < 0)
		return true;
	return gIplVisibilityEntries[i].visible;
}

void
SetInstIplFilterKey(ObjectInst *inst, const char *sceneName)
{
	char key[256];

	if(inst == nil)
		return;
	if(!buildIplVisibilityKey(sceneName, key, sizeof(key)))
		key[0] = '\0';
	if(strcmp(inst->m_iplFilterKey, key) == 0)
		return;
	strncpy(inst->m_iplFilterKey, key, sizeof(inst->m_iplFilterKey)-1);
	inst->m_iplFilterKey[sizeof(inst->m_iplFilterKey)-1] = '\0';
	gIplVisibilityEntriesDirty = true;
}

bool
IsHourInRange(int h1, int h2)
{
	if(h1 > h2)
		return currentHour >= h1 || currentHour < h2;
	else
		return currentHour >= h1 && currentHour < h2;
}

static WeatherInfo weathersIII[] = {
	{ "SUNNY", Weather::Sunny },
	{ "CLOUDY", 0 },
	{ "RAINY", 0 },
	{ "FOGGY", Weather::Foggy }
};
static WeatherInfo weathersVC[] = {
	{ "SUNNY", Weather::Sunny },
	{ "CLOUDY", 0 },
	{ "RAINY", 0 },
	{ "FOGGY", Weather::Foggy },
	{ "EXTRASUNNY", Weather::Sunny | Weather::Extrasunny },
	{ "HURRICANE", 0 },
	{ "EXTRACOLOURS", 0 }
};
static WeatherInfo weathersLCS[] = {
	{ "SUNNY", Weather::Sunny },
	{ "CLOUDY", 0 },
	{ "RAINY", 0 },
	{ "FOGGY", Weather::Foggy },
	{ "EXTRASUNNY", Weather::Sunny | Weather::Extrasunny },
	{ "HURRICANE", 0 },
	{ "EXTRACOLOURS", 0 },
	{ "SNOW", 0 }
};
static WeatherInfo weathersVCS[] = {
	{ "SUNNY", Weather::Sunny },
	{ "CLOUDY", 0 },
	{ "RAINY", 0 },
	{ "FOGGY", Weather::Foggy },
	{ "EXTRASUNNY", Weather::Sunny | Weather::Extrasunny },
	{ "HURRICANE", 0 },
	{ "EXTRACOLOURS", 0 },
	{ "ULTRASUNNY", Weather::Sunny | Weather::Extrasunny }
};
static WeatherInfo weathersSA[] = {
	{ "EXTRASUNNY LA", Weather::Sunny | Weather::Extrasunny },
	{ "SUNNY LA", Weather::Sunny },
	{ "EXTRASUNNY SMOG LA", Weather::Sunny | Weather::Extrasunny },
	{ "SUNNY SMOG LA", Weather::Sunny },
	{ "CLOUDY LA", 0 },
	{ "SUNNY SF", Weather::Sunny },
	{ "EXTRASUNNY SF", Weather::Sunny | Weather::Extrasunny },
	{ "CLOUDY SF", 0 },
	{ "RAINY SF", 0 },
	{ "FOGGY SF", Weather::Foggy },
	{ "SUNNY VEGAS", Weather::Sunny },
	{ "EXTRASUNNY VEGAS", Weather::Sunny | Weather::Extrasunny },
	{ "CLOUDY VEGAS", 0 },
	{ "EXTRASUNNY COUNTRYSIDE", Weather::Sunny | Weather::Extrasunny },
	{ "SUNNY COUNTRYSIDE", Weather::Sunny },
	{ "CLOUDY COUNTRYSIDE", 0 },
	{ "RAINY COUNTRYSIDE", 0 },
	{ "EXTRASUNNY DESERT", Weather::Sunny | Weather::Extrasunny },
	{ "SUNNY DESERT", Weather::Sunny },
	{ "SANDSTORM DESERT", Weather::Foggy },
	{ "UNDERWATER", 0 },
	{ "EXTRACOLOURS 1", 0 },
	{ "EXTRACOLOURS 2", 0 }
};

void
InitParams(void)
{
	static const char *areasVC[] = {
		"Main Map", "Hotel", "Mansion", "Bank", "Mall", "Strip club",
		"Lawyer", "Coffee shop", "Concert hall", "Studio", "Rifle range",
		"Biker bar", "Police station", "Everywhere", "Dirt", "Blood", "Oval ring",
		"Malibu", "Print works"
	};

	params.initcampos.set(1356.0f, -1107.0f, 96.0f);
	params.initcamtarg.set(1276.0f, -984.0f, 68.0f);
	params.backfaceCull = true;
	params.alphaRefDefault = 2;
	params.alphaRef = 2;
	params.ps2AlphaTest = gameplatform == PLATFORM_PS2;
	params.map = gameversion;

	switch(gameversion){
	case GAME_III:
		params.initcampos.set(970.8f, -497.3f, 36.8f);
		params.initcamtarg.set(1092.5f, -417.3f, 3.8f);
		params.objFlagset = GAME_III;
		params.timecycle = GAME_III;
		params.numHours = 24;
		params.numWeathers = 4;
		params.weatherInfo = weathersIII;
		params.water = GAME_III;
		params.waterTex = "water_old";
		params.waterStart.set(-2048.0f, -2048.0f);
		params.waterEnd.set(2048.0f, 2048.0f);
		params.backfaceCull = false;
		params.checkColModels = true;
		params.maxNumColBoxes = 32;
		params.maxNumColSpheres = 128;
		params.maxNumColTriangles = 600;
		switch(gameplatform){
		case PLATFORM_PS2:
			break;
		case PLATFORM_PC:
			break;
		case PLATFORM_XBOX:
			// not so sure about the values
			// I think it's hardcoded by ID
			params.alphaRefDefault = 6;
			params.alphaRef = 128;
			params.txdFallbackGeneric = true;
			params.neoWorldPipe = GAME_III;
			break;
		}
		break;
	case GAME_VC:
		params.initcampos.set(131.5f, -1674.2f, 59.8f);
		params.initcamtarg.set(67.9f, -1542.0f, 26.3f);
		params.objFlagset = GAME_VC;
		params.numAreas = 19;
		params.areaNames = areasVC;
		params.timecycle = GAME_VC;
		params.numHours = 24;
		params.numWeathers = 7;
		params.extraColours = 6;
		params.numExtraColours = 1;
		params.weatherInfo = weathersVC;
		params.water = GAME_VC;
		params.waterTex = "waterclear256";
		params.waterStart.set(-2048.0f - 400.0f, -2048.0f);
		params.waterEnd.set(2048.0f - 400.0f, 2048.0f);
		switch(gameplatform){
		case PLATFORM_PS2:
			params.backfaceCull = false;
			break;
		case PLATFORM_PC:
			break;
		case PLATFORM_XBOX:
			// not so sure about the values
			// I think it's hardcoded by ID
			params.alphaRefDefault = 6;
			params.alphaRef = 128;
			params.neoWorldPipe = GAME_VC;
			params.backfaceCull = false;
			break;
		}
		break;
	case GAME_SA:
		params.initcampos.set(1789.0f, -1667.4f, 66.4f);
		params.initcamtarg.set(1679.1f, -1569.4f, 41.5f);
		params.objFlagset = GAME_SA;
		params.numAreas = 19;
		params.areaNames = areasVC;
		params.timecycle = GAME_SA;
		params.numHours = 8;
		params.numWeathers = 23;
		params.extraColours = 21;
		params.numExtraColours = 2;
		params.weatherInfo = weathersSA;
		params.background = GAME_SA;
		params.daynightPipe = true;
		params.water = GAME_SA;
		params.waterTex = "waterclear256";

		gBuildingPipeSwitch = gameplatform;
		gColourFilter = PLATFORM_PC;
		gRadiosity = gColourFilter == PLATFORM_PS2;
		if(gameplatform == PLATFORM_PS2){
			gColourFilter = PLATFORM_PS2;
		}else{
			params.alphaRefDefault = 2;
			params.alphaRef = 100;
		}
		break;
	case GAME_LCS:
		// TODO
		params.map = GAME_III;
		params.initcampos.set(970.8f, -497.3f, 36.8f);
		params.initcamtarg.set(1092.5f, -417.3f, 3.8f);
		params.objFlagset = GAME_VC;
		params.numAreas = 19;
		params.areaNames = areasVC;
		params.timecycle = GAME_LCS;
		params.numHours = 24;
		params.numWeathers = 8;
		params.extraColours = 6;
		params.numExtraColours = 1;
		params.weatherInfo = weathersLCS;
		params.water = GAME_III;
		params.waterTex = "waterclear256";
		params.waterStart.set(-2048.0f, -2048.0f);
		params.waterEnd.set(2048.0f, 2048.0f);
		params.backfaceCull = false;
		params.ps2AlphaTest = true;

		params.leedsPipe = 1;
		gBuildingPipeSwitch = PLATFORM_PS2;
		break;
	case GAME_VCS:
		// TODO
		params.map = GAME_VC;
		params.initcampos.set(131.5f, -1674.2f, 59.8f);
		params.initcamtarg.set(67.9f, -1542.0f, 26.3f);
		params.objFlagset = GAME_VC;
		params.numAreas = 19;
		params.areaNames = areasVC;
		params.timecycle = GAME_VCS;
		params.numHours = 24;
		params.numWeathers = 8;
		params.extraColours = 7;
		params.numExtraColours = 1;
		params.weatherInfo = weathersVCS;
		params.water = GAME_VC;
		params.waterTex = "waterclear256";
		params.waterStart.set(-2048.0f - 400.0f, -2048.0f);
		params.waterEnd.set(2048.0f - 400.0f, 2048.0f);
		params.backfaceCull = false;
		params.ps2AlphaTest = true;

		params.leedsPipe = 1;
		gBuildingPipeSwitch = PLATFORM_PS2;
		TheCamera.m_LODmult = 1.5f;
		break;
	// more configs in the future (LCSPC, VCSPC, UG, ...)
	}

	if(params.ps2AlphaTest){
		params.alphaRefDefault = 128;
		params.alphaRef = 128;
	}
}

void
FindVersion(void)
{
	FILE *f;

	if(f = fopen_ci("data/gta3.dat", "r"), f)
		gameversion = GAME_III;
	// This is wrong of course, but we'll use it as a hack
	else if(f = fopen_ci("data/gta_lcs.dat", "r"), f)
		gameversion = GAME_LCS;
	else if(f = fopen_ci("data/gta_vc.dat", "r"), f)
		gameversion = GAME_VC;
	else if(f = fopen_ci("data/gta_vcs.dat", "r"), f)
		gameversion = GAME_VCS;
	else if(f = fopen_ci("data/gta.dat", "r"), f)
		gameversion = GAME_SA;
	else{
		gameversion = GAME_NA;
		return;
	}
	if(doesFileExist("SYSTEM.CNF"))
		gameplatform = PLATFORM_PS2;
	else if(doesFileExist("default.xbe"))
		gameplatform = PLATFORM_XBOX;
	else
		gameplatform = PLATFORM_PC;
	fclose(f);
}

/*
void
test(void)
{
	CPtrNode *p;
	ObjectInst *inst, *inst2;
	int i;
	for(p = instances.first; p; p = p->next){
		inst = (ObjectInst*)p->item;

//		if(inst->m_numChildren > 1)
//			printf("%s has %d lod children\n", GetObjectDef(inst->m_objectId)->m_name, inst->m_numChildren);
		i = 0;
		for(inst2 = inst; inst2; inst2 = inst2->m_lod)
			i++;
		if(i > 2){
			printf("%s has %d lod levels\n", GetObjectDef(inst->m_objectId)->m_name, i);
			for(inst2 = inst; inst2; inst2 = inst2->m_lod)
				printf(" %s\n", GetObjectDef(inst2->m_objectId)->m_name);
		}
	}
	if(0){
		int i;
		CPtrNode *p;
		i = 0;
		for(p = instances.first; p; p = p->next)
			i++;
		log("%d instances\n", i);
	}
}*/

void
RenderEverythingColourCoded(void)
{
	rw::SetRenderState(rw::FOGENABLE, 0);
	SetRenderState(rw::ALPHATESTREF, 10);
	int aref = params.alphaRef;
	params.alphaRef = 10;
	gta::renderColourCoded = 1;
	RenderEverything();
	gta::renderColourCoded = 0;
	params.alphaRef = aref;
}

int32
pick(void)
{
	static rw::RGBA black = { 0, 0, 0, 0xFF };
	TheCamera.m_rwcam->clear(&black, rw::Camera::CLEARIMAGE|rw::Camera::CLEARZ);
	RenderEverythingColourCoded();
	return gta::GetColourCode(CPad::newMouseState.x, CPad::newMouseState.y);
}

static rw::V3d
GetColVertex(CColModel *col, int idx)
{
	if(col->flags & 0x80)
		return col->compVertices[idx].Uncompress();
	return col->vertices[idx];
}

bool
IntersectRaySphere(const Ray &ray, const CSphere &sphere, float *t)
{
	rw::V3d diff = sub(ray.start, sphere.center);
	float a = dot(ray.dir, ray.dir);
	float b = 2.0f * dot(ray.dir, diff);
	float c = dot(diff, diff) - sq(sphere.radius);
	float discr = sq(b) - 4.0f*a*c;
	if(discr < 0.0f)
		return false;

	float root = sqrt(discr);
	float inv2a = 0.5f / a;
	float t0 = (-b - root) * inv2a;
	float t1 = (-b + root) * inv2a;
	float hit = t0 >= 0.0f ? t0 : t1;
	if(hit < 0.0f)
		return false;

	*t = hit;
	return true;
}

static bool
IntersectRayBox(const Ray &ray, const CBox &box, float *t)
{
	float tmin = 0.0f;
	float tmax = 1.0e30f;
	float rayStart[3] = { ray.start.x, ray.start.y, ray.start.z };
	float rayDir[3] = { ray.dir.x, ray.dir.y, ray.dir.z };
	float bmin[3] = { box.min.x, box.min.y, box.min.z };
	float bmax[3] = { box.max.x, box.max.y, box.max.z };

	for(int axis = 0; axis < 3; axis++){
		if(fabs(rayDir[axis]) < 0.0001f){
			if(rayStart[axis] < bmin[axis] || rayStart[axis] > bmax[axis])
				return false;
			continue;
		}

		float invDir = 1.0f / rayDir[axis];
		float t0 = (bmin[axis] - rayStart[axis]) * invDir;
		float t1 = (bmax[axis] - rayStart[axis]) * invDir;
		if(t0 > t1){
			float tmp = t0;
			t0 = t1;
			t1 = tmp;
		}

		tmin = max(tmin, t0);
		tmax = min(tmax, t1);
		if(tmin > tmax)
			return false;
	}

	*t = tmin;
	return true;
}

static rw::V3d
GetBoxHitNormal(const Ray &ray, const CBox &box, float t)
{
	const float eps = 0.01f;
	rw::V3d hit = add(ray.start, scale(ray.dir, t));
	rw::V3d normal = { 0.0f, 0.0f, 0.0f };

	if(fabs(hit.x - box.min.x) < eps) normal.x = -1.0f;
	else if(fabs(hit.x - box.max.x) < eps) normal.x = 1.0f;
	else if(fabs(hit.y - box.min.y) < eps) normal.y = -1.0f;
	else if(fabs(hit.y - box.max.y) < eps) normal.y = 1.0f;
	else if(fabs(hit.z - box.min.z) < eps) normal.z = -1.0f;
	else if(fabs(hit.z - box.max.z) < eps) normal.z = 1.0f;

	if(normal.x == 0.0f && normal.y == 0.0f && normal.z == 0.0f){
		float dxMin = fabs(hit.x - box.min.x);
		float dxMax = fabs(hit.x - box.max.x);
		float dyMin = fabs(hit.y - box.min.y);
		float dyMax = fabs(hit.y - box.max.y);
		float dzMin = fabs(hit.z - box.min.z);
		float dzMax = fabs(hit.z - box.max.z);
		float best = dxMin;
		normal.x = -1.0f;
		if(dxMax < best){ best = dxMax; normal.x = 1.0f; normal.y = 0.0f; normal.z = 0.0f; }
		if(dyMin < best){ best = dyMin; normal.x = 0.0f; normal.y = -1.0f; normal.z = 0.0f; }
		if(dyMax < best){ best = dyMax; normal.x = 0.0f; normal.y = 1.0f; normal.z = 0.0f; }
		if(dzMin < best){ best = dzMin; normal.x = 0.0f; normal.y = 0.0f; normal.z = -1.0f; }
		if(dzMax < best){ normal.x = 0.0f; normal.y = 0.0f; normal.z = 1.0f; }
	}

	return normal;
}

bool
IntersectRayTriangle(const Ray &ray, rw::V3d a, rw::V3d b, rw::V3d c, float *t)
{
	const float eps = 0.0001f;
	rw::V3d edge1 = sub(b, a);
	rw::V3d edge2 = sub(c, a);
	rw::V3d pvec = cross(ray.dir, edge2);
	float det = dot(edge1, pvec);
	if(fabs(det) < eps)
		return false;

	float invDet = 1.0f / det;
	rw::V3d tvec = sub(ray.start, a);
	float u = dot(tvec, pvec) * invDet;
	if(u < 0.0f || u > 1.0f)
		return false;

	rw::V3d qvec = cross(tvec, edge1);
	float v = dot(ray.dir, qvec) * invDet;
	if(v < 0.0f || u + v > 1.0f)
		return false;

	float hit = dot(edge2, qvec) * invDet;
	if(hit < 0.0f)
		return false;

	*t = hit;
	return true;
}

static bool
IntersectRayColModelDetailed(const Ray &worldRay, ObjectInst *inst, rw::V3d *hitPos, rw::V3d *hitNormal)
{
	ObjectDef *obj = GetObjectDef(inst->m_objectId);
	if(obj == nil || obj->m_colModel == nil)
		return false;

	CColModel *col = obj->m_colModel;
	rw::Matrix invMat;
	rw::Matrix::invert(&invMat, &inst->m_matrix);

	Ray ray;
	ray.start = worldRay.start;
	ray.dir = worldRay.dir;
	rw::V3d::transformPoints(&ray.start, &ray.start, 1, &invMat);
	rw::V3d::transformVectors(&ray.dir, &ray.dir, 1, &invMat);
	ray.dir = normalize(ray.dir);

	float broadT;
	if(!IntersectRayBox(ray, col->boundingBox, &broadT) &&
	   !IntersectRaySphere(ray, col->boundingSphere, &broadT))
		return false;

	float bestT = 1.0e30f;
	bool found = false;
	rw::V3d bestNormal = { 0.0f, 0.0f, 1.0f };

	for(int i = 0; i < col->numTriangles; i++){
		CColTriangle *tri = &col->triangles[i];
		rw::V3d a = GetColVertex(col, tri->a);
		rw::V3d b = GetColVertex(col, tri->b);
		rw::V3d c = GetColVertex(col, tri->c);
		float t;
		if(IntersectRayTriangle(ray, a, b, c, &t) && t < bestT){
			bestT = t;
			bestNormal = normalize(cross(sub(b, a), sub(c, a)));
			found = true;
		}
	}

	for(int i = 0; i < col->numBoxes; i++){
		float t;
		if(IntersectRayBox(ray, col->boxes[i].box, &t) && t < bestT){
			bestT = t;
			bestNormal = GetBoxHitNormal(ray, col->boxes[i].box, t);
			found = true;
		}
	}

	for(int i = 0; i < col->numSpheres; i++){
		float t;
		if(IntersectRaySphere(ray, col->spheres[i].sph, &t) && t < bestT){
			rw::V3d localHit = add(ray.start, scale(ray.dir, t));
			bestT = t;
			bestNormal = normalize(sub(localHit, col->spheres[i].sph.center));
			found = true;
		}
	}

	if(!found)
		return false;

	*hitPos = add(worldRay.start, scale(worldRay.dir, bestT));
	if(hitNormal){
		*hitNormal = bestNormal;
		rw::V3d::transformVectors(hitNormal, hitNormal, 1, &inst->m_matrix);
		*hitNormal = normalize(*hitNormal);
	}
	return true;
}

bool
IntersectRayColModel(const Ray &worldRay, ObjectInst *inst, rw::V3d *hitPos)
{
	return IntersectRayColModelDetailed(worldRay, inst, hitPos, nil);
}

static bool
CanSnapToInst(ObjectInst *inst)
{
	if(inst == nil || inst->m_isDeleted)
		return false;
	if(!gNoAreaCull && inst->m_area != currentArea && inst->m_area != 13)
		return false;
	ObjectDef *obj = GetObjectDef(inst->m_objectId);
	return obj && obj->m_colModel;
}

static bool
PointInsideInstBounds(ObjectInst *inst, float x, float y)
{
	CRect bounds = inst->GetBoundRect();
	return x >= bounds.left && x <= bounds.right &&
	       y >= bounds.bottom && y <= bounds.top;
}

static void
FindGroundHitInList(CPtrList *list, const Ray &ray, float x, float y, rw::V3d *bestHit, rw::V3d *bestNormal, float *bestT, bool ignoreSelection)
{
	for(CPtrNode *p = list->first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(ignoreSelection){
			if(inst->m_selected)
				continue;
			if(inst->m_lod && inst->m_lod->m_selected)
				continue;
			bool linkedToSelection = false;
			for(CPtrNode *sel = selection.first; sel; sel = sel->next){
				ObjectInst *selectedInst = (ObjectInst*)sel->item;
				if(selectedInst->m_lod == inst){
					linkedToSelection = true;
					break;
				}
			}
			if(linkedToSelection)
				continue;
		}
		if(!CanSnapToInst(inst))
			continue;
		if(!PointInsideInstBounds(inst, x, y))
			continue;

		rw::V3d hitPos;
		rw::V3d hitNormal;
		if(!IntersectRayColModelDetailed(ray, inst, &hitPos, &hitNormal))
			continue;

		float t = dot(sub(hitPos, ray.start), ray.dir);
		if(t >= 0.0f && t < *bestT){
			*bestT = t;
			*bestHit = hitPos;
			if(bestNormal)
				*bestNormal = hitNormal;
		}
	}
}

bool
GetGroundPlacementSurface(rw::V3d pos, rw::V3d *hitPos, rw::V3d *hitNormal, bool ignoreSelection)
{
	Ray ray;
	ray.start = pos;
	ray.start.z += 5000.0f;
	ray.dir = { 0.0f, 0.0f, -1.0f };

	float bestT = 1.0e30f;
	bool found = false;
	rw::V3d bestHit = pos;
	rw::V3d bestNormal = { 0.0f, 0.0f, 1.0f };

	if(pos.x >= worldBounds.left && pos.x < worldBounds.right &&
	   pos.y >= worldBounds.bottom && pos.y < worldBounds.top){
		Sector *s = GetSector(GetSectorIndexX(pos.x), GetSectorIndexY(pos.y));
		FindGroundHitInList(&s->buildings, ray, pos.x, pos.y, &bestHit, &bestNormal, &bestT, ignoreSelection);
		FindGroundHitInList(&s->buildings_overlap, ray, pos.x, pos.y, &bestHit, &bestNormal, &bestT, ignoreSelection);
		FindGroundHitInList(&s->bigbuildings, ray, pos.x, pos.y, &bestHit, &bestNormal, &bestT, ignoreSelection);
		FindGroundHitInList(&s->bigbuildings_overlap, ray, pos.x, pos.y, &bestHit, &bestNormal, &bestT, ignoreSelection);
	}

	FindGroundHitInList(&outOfBoundsSector.buildings, ray, pos.x, pos.y, &bestHit, &bestNormal, &bestT, ignoreSelection);
	FindGroundHitInList(&outOfBoundsSector.bigbuildings, ray, pos.x, pos.y, &bestHit, &bestNormal, &bestT, ignoreSelection);

	found = bestT < 1.0e30f;
	if(found){
		*hitPos = bestHit;
		if(hitNormal){
			if(bestNormal.z < 0.0f)
				bestNormal = scale(bestNormal, -1.0f);
			*hitNormal = bestNormal;
		}
	}
	return found;
}

float
GetPlacementBaseOffset(int objectId)
{
	ObjectDef *obj = GetObjectDef(objectId);
	if(obj == nil || obj->m_colModel == nil)
		return 0.0f;
	return -obj->m_colModel->boundingBox.min.z;
}

static void updateRwFrame(ObjectInst *inst);

static rw::Quat
QuatFromMatrix(const rw::Matrix &matrix)
{
	rw::Quat q;
	float trace = matrix.right.x + matrix.up.y + matrix.at.z;
	if(trace > 0.0f){
		float s = sqrtf(trace + 1.0f) * 2.0f;
		q.w = 0.25f * s;
		q.x = (matrix.up.z - matrix.at.y) / s;
		q.y = (matrix.at.x - matrix.right.z) / s;
		q.z = (matrix.right.y - matrix.up.x) / s;
	}else if(matrix.right.x > matrix.up.y && matrix.right.x > matrix.at.z){
		float s = sqrtf(1.0f + matrix.right.x - matrix.up.y - matrix.at.z) * 2.0f;
		q.w = (matrix.up.z - matrix.at.y) / s;
		q.x = 0.25f * s;
		q.y = (matrix.up.x + matrix.right.y) / s;
		q.z = (matrix.at.x + matrix.right.z) / s;
	}else if(matrix.up.y > matrix.at.z){
		float s = sqrtf(1.0f + matrix.up.y - matrix.right.x - matrix.at.z) * 2.0f;
		q.w = (matrix.at.x - matrix.right.z) / s;
		q.x = (matrix.up.x + matrix.right.y) / s;
		q.y = 0.25f * s;
		q.z = (matrix.at.y + matrix.up.z) / s;
	}else{
		float s = sqrtf(1.0f + matrix.at.z - matrix.right.x - matrix.up.y) * 2.0f;
		q.w = (matrix.right.y - matrix.up.x) / s;
		q.x = (matrix.at.x + matrix.right.z) / s;
		q.y = (matrix.at.y + matrix.up.z) / s;
		q.z = 0.25f * s;
	}
	q.x = -q.x;
	q.y = -q.y;
	q.z = -q.z;
	float lenSq = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
	if(lenSq < 0.00000001f)
		return { 0.0f, 0.0f, 0.0f, 1.0f };
	float invLen = 1.0f / sqrtf(lenSq);
	q.x *= invLen;
	q.y *= invLen;
	q.z *= invLen;
	q.w *= invLen;
	return q;
}

static rw::Quat
NormalizeQuatOrIdentity(const rw::Quat &q)
{
	float lenSq = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
	if(lenSq < 0.00000001f)
		return { 0.0f, 0.0f, 0.0f, 1.0f };
	float invLen = 1.0f / sqrtf(lenSq);
	return { q.x * invLen, q.y * invLen, q.z * invLen, q.w * invLen };
}

static rw::V3d
NormalizeOr(const rw::V3d &v, const rw::V3d &fallback)
{
	float len = length(v);
	if(len < 0.0001f)
		return fallback;
	return scale(v, 1.0f / len);
}

static float
GetMinZOffsetForRotation(ObjectInst *inst, const rw::Quat &rotation)
{
	ObjectDef *obj = GetObjectDef(inst->m_objectId);
	if(obj == nil || obj->m_colModel == nil)
		return 0.0f;

	CBox box = obj->m_colModel->boundingBox;
	rw::Matrix rotMat;
	rotMat.rotate(conj(rotation), rw::COMBINEREPLACE);
	rotMat.pos.x = 0.0f;
	rotMat.pos.y = 0.0f;
	rotMat.pos.z = 0.0f;

	rw::V3d corners[8] = {
		{ box.min.x, box.min.y, box.min.z },
		{ box.min.x, box.min.y, box.max.z },
		{ box.min.x, box.max.y, box.min.z },
		{ box.min.x, box.max.y, box.max.z },
		{ box.max.x, box.min.y, box.min.z },
		{ box.max.x, box.min.y, box.max.z },
		{ box.max.x, box.max.y, box.min.z },
		{ box.max.x, box.max.y, box.max.z },
	};

	rw::V3d::transformPoints(corners, corners, 8, &rotMat);
	float minZ = corners[0].z;
	for(int i = 1; i < 8; i++)
		minZ = min(minZ, corners[i].z);
	return minZ;
}

rw::Quat
BuildGroundAlignedRotationFromRotation(const rw::Quat &sourceRotation, rw::V3d groundNormal)
{
	rw::V3d fallbackAt = { 0.0f, 0.0f, 1.0f };
	rw::V3d fallbackRight = { 1.0f, 0.0f, 0.0f };
	rw::V3d fallbackForward = { 0.0f, 1.0f, 0.0f };
	rw::Matrix sourceMatrix;
	sourceMatrix.rotate(conj(sourceRotation), rw::COMBINEREPLACE);

	groundNormal = NormalizeOr(groundNormal, fallbackAt);
	rw::V3d currentVertical = NormalizeOr(sourceMatrix.at, fallbackAt);
	float d = clamp(dot(currentVertical, groundNormal), -1.0f, 1.0f);
	const float pi = 3.14159265358979323846f;

	rw::Quat swing = { 0.0f, 0.0f, 0.0f, 1.0f };
	if(d < 0.9999f){
		rw::V3d axis;
		if(d > -0.9999f)
			axis = NormalizeOr(cross(currentVertical, groundNormal), fallbackRight);
		else{
			axis = sub(sourceMatrix.up, scale(currentVertical, dot(sourceMatrix.up, currentVertical)));
			if(length(axis) < 0.0001f)
				axis = sub(sourceMatrix.right, scale(currentVertical, dot(sourceMatrix.right, currentVertical)));
			axis = NormalizeOr(axis, fallbackRight);
		}
		float angle = d > -0.9999f ? acosf(d) : pi;
		swing = rw::Quat::rotation(angle, axis);
	}

	rw::V3d forward = rotate(sourceMatrix.up, swing);
	forward = sub(forward, scale(groundNormal, dot(forward, groundNormal)));
	if(length(forward) < 0.0001f)
		forward = rotate(sourceMatrix.right, swing);
	forward = sub(forward, scale(groundNormal, dot(forward, groundNormal)));
	forward = NormalizeOr(forward, fallbackForward);

	rw::V3d right = NormalizeOr(cross(forward, groundNormal), fallbackRight);
	forward = NormalizeOr(cross(groundNormal, right), forward);

	rw::Matrix matrix;
	matrix.right = right;
	matrix.up = forward;
	matrix.at = groundNormal;
	matrix.pos.x = 0.0f;
	matrix.pos.y = 0.0f;
	matrix.pos.z = 0.0f;
	return QuatFromMatrix(matrix);
}

static rw::Quat
BuildGroundAlignedRotation(ObjectInst *inst, rw::V3d groundNormal)
{
	return BuildGroundAlignedRotationFromRotation(inst->m_rotation, groundNormal);
}

static UndoTransform*
FindOrAddTransform(UndoTransform *transforms, int *numTransforms, ObjectInst *inst)
{
	for(int i = 0; i < *numTransforms; i++)
		if(transforms[i].inst == inst)
			return &transforms[i];
	if(*numTransforms >= MAX_BATCH_OBJECTS)
		return nil;
	UndoTransform *t = &transforms[(*numTransforms)++];
	memset(t, 0, sizeof(*t));
	t->inst = inst;
	t->oldPos = inst->m_translation;
	t->newPos = inst->m_translation;
	t->oldRot = inst->m_rotation;
	t->newRot = inst->m_rotation;
	return t;
}

static bool
HasSelectedHdChild(ObjectInst *lodInst)
{
	if(lodInst == nil)
		return false;
	for(CPtrNode *p = selection.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(!inst->m_isDeleted && inst->m_lod == lodInst)
			return true;
	}
	return false;
}

static int
CountSelectedHdChildren(ObjectInst *lodInst)
{
	int count = 0;
	if(lodInst == nil)
		return 0;
	for(CPtrNode *p = selection.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(!inst->m_isDeleted && inst->m_lod == lodInst)
			count++;
	}
	return count;
}

static void
ApplyTransform(UndoTransform &t)
{
	ObjectInst *inst = t.inst;
	bool refreshSectors = (t.flags & (UNDO_TRANSFORM_POS | UNDO_TRANSFORM_ROT)) != 0;
	if(refreshSectors)
		RemoveInstFromSectors(inst);
	if(t.flags & UNDO_TRANSFORM_POS)
		inst->m_translation = t.newPos;
	if(t.flags & UNDO_TRANSFORM_ROT)
		inst->m_rotation = t.newRot;
	inst->m_isDirty = true;
	StampChangeSeq(inst);
	inst->UpdateMatrix();
	updateRwFrame(inst);
	if(refreshSectors)
		InsertInstIntoSectors(inst);
}

int
SnapSelectedToGround(bool alignRotation)
{
	ObjectInst *targets[MAX_BATCH_OBJECTS];
	int numTargets = 0;
	int skipped = 0;

	for(CPtrNode *p = selection.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_isDeleted)
			continue;
		if(inst->m_lod && CountSelectedHdChildren(inst->m_lod) > 1){
			skipped++;
			continue;
		}
		if(HasSelectedHdChild(inst)){
			skipped++;
			continue;
		}
		if(numTargets < MAX_BATCH_OBJECTS)
			targets[numTargets++] = inst;
	}

	if(numTargets == 0){
		if(skipped > 0)
			Toast(TOAST_SELECTION, "Ground snap skipped linked multi-selection");
		return 0;
	}

	UndoTransform transforms[MAX_BATCH_OBJECTS];
	int numTransforms = 0;
	int snapped = 0;

	for(int i = 0; i < numTargets; i++){
		ObjectInst *inst = targets[i];
		rw::V3d hitPos, hitNormal;
		if(!GetGroundPlacementSurface(inst->m_translation, &hitPos, &hitNormal, true)){
			skipped++;
			continue;
		}

		rw::Quat newRot = inst->m_rotation;
		if(alignRotation)
			newRot = BuildGroundAlignedRotation(inst, hitNormal);

		rw::V3d newPos = inst->m_translation;
		newPos.z = hitPos.z - GetMinZOffsetForRotation(inst, newRot);
		rw::V3d delta = sub(newPos, inst->m_translation);

		if(length(delta) < 0.0001f && (!alignRotation || memcmp(&newRot, &inst->m_rotation, sizeof(newRot)) == 0))
			continue;

		UndoTransform *self = FindOrAddTransform(transforms, &numTransforms, inst);
		if(self == nil)
			break;
		self->flags |= UNDO_TRANSFORM_POS;
		self->newPos = newPos;
		if(alignRotation){
			self->flags |= UNDO_TRANSFORM_ROT;
			self->newRot = newRot;
		}

		if(inst->m_lod && !inst->m_lod->m_isDeleted){
			UndoTransform *lod = FindOrAddTransform(transforms, &numTransforms, inst->m_lod);
			if(lod){
				lod->flags |= UNDO_TRANSFORM_POS;
				lod->newPos = add(inst->m_lod->m_translation, delta);
			}
		}else{
			for(CPtrNode *p = instances.first; p; p = p->next){
				ObjectInst *child = (ObjectInst*)p->item;
				if(child != inst && child->m_lod == inst && !child->m_isDeleted){
					UndoTransform *childTransform = FindOrAddTransform(transforms, &numTransforms, child);
					if(childTransform){
						childTransform->flags |= UNDO_TRANSFORM_POS;
						childTransform->newPos = add(child->m_translation, delta);
					}
				}
			}
		}
		snapped++;
	}

	if(numTransforms == 0){
		if(skipped > 0)
			Toast(TOAST_SELECTION, "Ground snap skipped %d instance(s)", skipped);
		return 0;
	}

	for(int i = 0; i < numTransforms; i++)
		ApplyTransform(transforms[i]);
	UndoRecordTransformBatch(transforms, numTransforms);

	if(snapped > 0){
		if(alignRotation)
			Toast(TOAST_SELECTION, "Aligned %d instance(s) to ground", snapped);
		else
			Toast(TOAST_SELECTION, "Snapped %d instance(s) to ground", snapped);
	}
	if(skipped > 0)
		Toast(TOAST_SELECTION, "Skipped %d linked/conflicting instance(s)", skipped);
	return snapped;
}

rw::V3d
GetPlacementPosition(void)
{
	rw::V3d origin = TheCamera.m_position;
	rw::V3d dir = normalize(TheCamera.m_mouseDir);
	Ray ray;
	ray.start = origin;
	ray.dir = dir;
	float baseOffset = GetPlacementBaseOffset(GetSpawnObjectId());

	if(gPlaceSnapToObjects){
		ObjectInst *targetInst = GetInstanceByID(pick());
		rw::V3d hitPos;
		if(CanSnapToInst(targetInst) && IntersectRayColModel(ray, targetInst, &hitPos)){
			hitPos.z += baseOffset;
			return hitPos;
		}
	}

	// Intersect ray with horizontal plane at camera target height
	float planeZ = TheCamera.m_target.z;
	rw::V3d surfacePos;
	if(fabs(dir.z) < 0.001f)
		surfacePos = add(origin, scale(dir, 50.0f));
	else{
		float t = (planeZ - origin.z) / dir.z;
		if(t < 1.0f) t = 50.0f;
		if(t > 5000.0f) t = 5000.0f;

		surfacePos.x = origin.x + dir.x * t;
		surfacePos.y = origin.y + dir.y * t;
		surfacePos.z = planeZ;
	}

	if(gPlaceSnapToGround){
		rw::V3d groundHit;
		if(GetGroundPlacementSurface(surfacePos, &groundHit))
			surfacePos = groundHit;
	}

	surfacePos.z += baseOffset;
	return surfacePos;
}

// --- Brush tool ---

// Find the surface point under the mouse cursor and its normal.
// Tries a direct ray-cast against visible instances first (so the brush paints
// on whatever is under the cursor — terrain, buildings, rocks); falls back to
// intersecting the mouse ray with a horizontal plane at the camera target
// height and then snapping to ground.
// Selected instances are always skipped: after a brush placement the new
// instance is selected, so this prevents drag-paint from stacking up on its
// own just-painted objects.
bool
GetBrushSurfaceHit(rw::V3d *hitPos, rw::V3d *hitNormal)
{
	Ray ray;
	ray.start = TheCamera.m_position;
	ray.dir = normalize(TheCamera.m_mouseDir);

	// Direct hit against any visible object, ignoring the current selection.
	float bestT = 1.0e30f;
	ObjectInst *hitInst = GetVisibleInstUnderRay(ray, nil, &bestT);
	if(hitInst && !hitInst->m_selected){
		rw::V3d p, n;
		if(IntersectRayColModelDetailed(ray, hitInst, &p, &n)){
			if(n.z < 0.0f) n = scale(n, -1.0f);
			if(hitPos) *hitPos = p;
			if(hitNormal) *hitNormal = n;
			return true;
		}
	}

	// Fallback: project ray to camera-target plane, then snap down to ground.
	float planeZ = TheCamera.m_target.z;
	rw::V3d dir = ray.dir;
	rw::V3d origin = ray.start;
	rw::V3d surfacePos;
	if(fabs(dir.z) < 0.001f)
		surfacePos = add(origin, scale(dir, 50.0f));
	else{
		float t = (planeZ - origin.z) / dir.z;
		if(t < 1.0f) t = 50.0f;
		if(t > 5000.0f) t = 5000.0f;
		surfacePos.x = origin.x + dir.x * t;
		surfacePos.y = origin.y + dir.y * t;
		surfacePos.z = planeZ;
	}

	rw::V3d groundHit, groundNormal = { 0.0f, 0.0f, 1.0f };
	if(GetGroundPlacementSurface(surfacePos, &groundHit, &groundNormal, /*ignoreSelection=*/true)){
		if(hitPos) *hitPos = groundHit;
		if(hitNormal) *hitNormal = groundNormal;
		return true;
	}
	return false;
}

void
EnterBrushMode(int objectId)
{
	if(objectId < 0 || GetObjectDef(objectId) == nil)
		return;
	// Mutually exclusive with place mode
	if(gPlaceMode)
		SpawnExitPlaceMode();
	SetSpawnObjectId(objectId);
	gBrushMode = true;
}

void
ExitBrushMode(void)
{
	gBrushMode = false;
}

// Last drag-painted position; used to enforce gBrushSpacing while LMB is held.
static rw::V3d sBrushLastPaintPos = { 0.0f, 0.0f, 0.0f };
static bool sBrushHasLastPaint = false;

static void
brushPlaceAt(rw::V3d hitPos, rw::V3d hitNormal)
{
	int objId = GetSpawnObjectId();
	if(objId < 0) return;

	// Lift by the object's base-of-bounding-box offset so the model sits ON
	// the surface (matches the normal placement path in GetPlacementPosition).
	// Then apply the user-authored Z offset on top (negative to sink into slopes).
	rw::V3d pos = hitPos;
	pos.z += GetPlacementBaseOffset(objId);
	pos.z += gBrushZOffset;

	rw::Quat rot = { 0.0f, 0.0f, 0.0f, 1.0f };
	if(gBrushRandomYaw){
		float yaw = ((float)rand() / (float)RAND_MAX) * 6.28318530718f;
		rot = rw::Quat::rotation(yaw, { 0.0f, 0.0f, 1.0f });
	}
	if(gBrushAlignToSurface){
		rot = BuildGroundAlignedRotationFromRotation(rot, hitNormal);
		SpawnPlaceObject(pos, &rot);
	}else if(gBrushRandomYaw){
		SpawnPlaceObject(pos, &rot);
	}else{
		SpawnPlaceObject(pos);
	}

	sBrushLastPaintPos = hitPos;
	sBrushHasLastPaint = true;
}

static void
handleBrushTool(void)
{
	ImGuiIO &io = ImGui::GetIO();
	if(io.WantCaptureMouse || gGizmoHovered || gGizmoUsing || ImGuizmo::IsOver())
		return;

	// Cancel
	if(CPad::IsMButtonClicked(2) || CPad::IsKeyJustDown(KEY_ESC)){
		ExitBrushMode();
		return;
	}

	rw::V3d hitPos, hitNormal;
	bool haveHit = GetBrushSurfaceHit(&hitPos, &hitNormal);

	if(!haveHit){
		sBrushHasLastPaint = false;
		return;
	}

	// LMB just pressed — place one, start a paint stroke
	if(CPad::IsMButtonJustDown(1)){
		brushPlaceAt(hitPos, hitNormal);
		return;
	}

	// LMB held and moved — continuous paint, gated by gBrushSpacing
	if(CPad::IsMButtonDown(1) && sBrushHasLastPaint && gBrushSpacing > 0.01f){
		rw::V3d d = sub(hitPos, sBrushLastPaintPos);
		float d2 = d.x*d.x + d.y*d.y + d.z*d.z;
		float s2 = gBrushSpacing * gBrushSpacing;
		if(d2 >= s2)
			brushPlaceAt(hitPos, hitNormal);
	}

	// LMB released — end stroke
	if(!CPad::IsMButtonDown(1))
		sBrushHasLastPaint = false;
}

// --- Rect-select (marquee selection) ---

// Called early in Draw(), before Camera.Process, to detect Shift+LMB start
// and set gRectSelectActive so the camera knows to skip LMB look.
static void
updateRectSelectEarly(void)
{
	ImGuiIO &io = ImGui::GetIO();
	bool blockInput = io.WantCaptureMouse || gGizmoHovered || gGizmoUsing;

	// Start: Shift+LMB just pressed, not blocked
	if(!gRectSelectActive && !blockInput &&
	   CPad::IsShiftDown() && CPad::IsMButtonJustDown(1) &&
	   !gPlaceMode && !gBrushMode && !WaterLevel::gWaterEditMode){
		gRectSelectActive = true;
		sRectSelectDragging = false;
		sRectStartX = (float)CPad::newMouseState.x;
		sRectStartY = (float)CPad::newMouseState.y;
	}

	// Threshold: promote pending to dragging
	if(gRectSelectActive && !sRectSelectDragging && CPad::IsMButtonDown(1)){
		float dx = (float)CPad::newMouseState.x - sRectStartX;
		float dy = (float)CPad::newMouseState.y - sRectStartY;
		if(dx*dx + dy*dy > RECT_SELECT_THRESHOLD_SQ)
			sRectSelectDragging = true;
	}

	// Cancel if LMB released before threshold (was a Shift+click, not a drag)
	if(gRectSelectActive && !sRectSelectDragging && !CPad::IsMButtonDown(1))
		gRectSelectActive = false;
}

struct RectSelectCtx {
	float x1, y1, x2, y2;
	bool removeMode;
	int count;
};

// Project an instance's bounding box to a screen-space AABB and test overlap
// with the selection rectangle.  Returns true if the projected bounds overlap.
// Falls back to bounding-sphere center when no collision model is available.
static bool
instOverlapsScreenRect(ObjectInst *inst, float rx1, float ry1, float rx2, float ry2)
{
	ObjectDef *obj = GetObjectDef(inst->m_objectId);

	if(obj && obj->m_colModel){
		CColModel *col = obj->m_colModel;
		float sxMin = 1e30f, syMin = 1e30f;
		float sxMax = -1e30f, syMax = -1e30f;
		int projected = 0;

		for(int ix = 0; ix < 2; ix++)
		for(int iy = 0; iy < 2; iy++)
		for(int iz = 0; iz < 2; iz++){
			rw::V3d v;
			v.x = ix ? col->boundingBox.max.x : col->boundingBox.min.x;
			v.y = iy ? col->boundingBox.max.y : col->boundingBox.min.y;
			v.z = iz ? col->boundingBox.max.z : col->boundingBox.min.z;
			rw::V3d::transformPoints(&v, &v, 1, &inst->m_matrix);

			rw::V3d sp;
			float sw, sh;
			if(!Sprite::CalcScreenCoors(v, &sp, &sw, &sh, false))
				continue;
			if(sp.x < sxMin) sxMin = sp.x;
			if(sp.y < syMin) syMin = sp.y;
			if(sp.x > sxMax) sxMax = sp.x;
			if(sp.y > syMax) syMax = sp.y;
			projected++;
		}

		if(projected == 0)
			return false;

		// AABB overlap test
		return sxMin <= rx2 && sxMax >= rx1 && syMin <= ry2 && syMax >= ry1;
	}

	// No collision model — fall back to object position
	rw::V3d sp;
	float sw, sh;
	if(!Sprite::CalcScreenCoors(inst->m_translation, &sp, &sw, &sh, false))
		return false;
	return sp.x >= rx1 && sp.x <= rx2 && sp.y >= ry1 && sp.y <= ry2;
}

static void
rectSelectHighlightInst(ObjectInst *inst, void *data)
{
	if(inst->m_isDeleted) return;
	RectSelectCtx *ctx = (RectSelectCtx*)data;

	if(instOverlapsScreenRect(inst, ctx->x1, ctx->y1, ctx->x2, ctx->y2)){
		if(inst->m_highlight < HIGHLIGHT_HOVER)
			inst->m_highlight = HIGHLIGHT_HOVER;
	}
}

// Called late in Draw(), after handleTool: draws overlay, previews, commits on release
static void
handleRectSelect(void)
{
	if(!sRectSelectDragging) return;

	float curX = (float)CPad::newMouseState.x;
	float curY = (float)CPad::newMouseState.y;

	// Normalise rect
	RectSelectCtx ctx;
	ctx.x1 = sRectStartX < curX ? sRectStartX : curX;
	ctx.y1 = sRectStartY < curY ? sRectStartY : curY;
	ctx.x2 = sRectStartX > curX ? sRectStartX : curX;
	ctx.y2 = sRectStartY > curY ? sRectStartY : curY;
	ctx.removeMode = CPad::IsAltDown();
	ctx.count = 0;

	// Draw selection rectangle overlay
	ImDrawList *dl = ImGui::GetForegroundDrawList();
	ImVec2 p0(ctx.x1, ctx.y1);
	ImVec2 p1(ctx.x2, ctx.y2);
	dl->AddRectFilled(p0, p1, IM_COL32(100, 150, 255, 40));
	dl->AddRect(p0, p1, IM_COL32(100, 150, 255, 200), 0.0f, 0, 1.5f);

	if(CPad::IsMButtonDown(1)){
		// Still dragging — preview highlights
		ForEachVisibleInst(rectSelectHighlightInst, &ctx);
	}else{
		// LMB released — commit selection via colour-coded picking pass
		bool addMode = CPad::IsCtrlDown();
		if(!addMode && !ctx.removeMode)
			ClearSelection();

		// Render scene with colour codes and read the selection rectangle
		static rw::RGBA black = { 0, 0, 0, 0xFF };
		TheCamera.m_rwcam->clear(&black, rw::Camera::CLEARIMAGE|rw::Camera::CLEARZ);
		RenderEverythingColourCoded();

		int rx = (int)ctx.x1;
		int ry = (int)ctx.y1;
		int rw = (int)(ctx.x2 - ctx.x1 + 0.5f);
		int rh = (int)(ctx.y2 - ctx.y1 + 0.5f);
		int32 codes[MAX_BATCH_OBJECTS];
		int numCodes = gta::GetColourCodesInRect(rx, ry, rw, rh, codes, MAX_BATCH_OBJECTS);

		int count = 0;
		for(int i = 0; i < numCodes; i++){
			ObjectInst *inst = GetInstanceByID(codes[i]);
			if(inst && !inst->m_isDeleted){
				if(ctx.removeMode)
					inst->Deselect();
				else
					inst->Select();
				count++;
			}
		}

		if(count > 0)
			Toast(TOAST_SELECTION, "Selected %d object(s)", count);

		sRectSelectDragging = false;
		gRectSelectActive = false;
	}
}

void
handleTool(void)
{
	// Don't process viewport clicks when ImGui wants the mouse
	ImGuiIO &io = ImGui::GetIO();
	if(io.WantCaptureMouse || gGizmoHovered || gGizmoUsing || ImGuizmo::IsOver() || gRectSelectActive)
		return;

	// Water edit mode intercepts all clicks
	if(WaterLevel::gWaterEditMode){
		WaterLevel::HandleWaterTool();
		return;
	}

	// Place mode intercepts all clicks
	if(gPlaceMode){
		if(CPad::IsMButtonClicked(1)){
			rw::V3d pos = GetPlacementPosition();
			SpawnPlaceObject(pos);
			// Shift+click = keep placing, plain click = single place
			if(!CPad::IsKeyDown(KEY_LSHIFT) && !CPad::IsKeyDown(KEY_RSHIFT))
				gPlaceMode = false;
			return;
		}
		if(CPad::IsMButtonClicked(2) || CPad::IsKeyJustDown(KEY_ESC)){
			SpawnExitPlaceMode();
			return;
		}
		return;  // Absorb clicks while in place mode
	}

	// Brush mode intercepts all clicks (click-to-place + drag-to-paint)
	if(gBrushMode){
		handleBrushTool();
		return;
	}

	// select
	if(CPad::IsMButtonClicked(1)){
		if(Path::hoveredNode || SAPaths::hoveredNode || Effects::hoveredEffect){
			ClearSelection();
			Path::selectedNode = Path::hoveredNode;
			SAPaths::selectedNode = SAPaths::hoveredNode;
			Effects::selectedEffect = Effects::hoveredEffect;
		}else{
			ObjectInst *inst = GetInstanceByID(pick());
			if(inst && !inst->m_isDeleted){
				if(CPad::IsShiftDown())
					inst->Select();
				else if(CPad::IsAltDown())
					inst->Deselect();
				else if(CPad::IsCtrlDown()){
					if(inst->m_selected) inst->Deselect();
					else inst->Select();
				}else{
					ClearSelection();
					inst->Select();
				}
			}else
				ClearSelection();

			Path::selectedNode = nil;
			SAPaths::selectedNode = nil;
			Effects::selectedEffect = nil;
		}
	}else if(CPad::IsMButtonClicked(2)){
		if(CPad::IsCtrlDown()){
			Path::selectedNode = Path::hoveredNode;
			SAPaths::selectedNode = SAPaths::hoveredNode;
			Effects::selectedEffect = Effects::hoveredEffect;
		}else{
			if(Path::hoveredNode || SAPaths::hoveredNode || Effects::hoveredEffect){
				ClearSelection();
				Path::selectedNode = Path::hoveredNode;
				SAPaths::selectedNode = SAPaths::hoveredNode;
				Effects::selectedEffect = Effects::hoveredEffect;
			}else{
				ClearSelection();
				ObjectInst *inst = GetInstanceByID(pick());
				if(inst && !inst->m_isDeleted)
					inst->Select();
			}
		}
	}else if(CPad::IsMButtonClicked(3)){
		ClearSelection();
		Path::selectedNode = nil;
		SAPaths::selectedNode = nil;
		Effects::selectedEffect = nil;
	}
}

rw::Texture *(*originalFindCB)(const char *name);
rw::TexDictionary *fallbackTxd;
static rw::Texture*
findTextureCaseInsensitive(rw::TexDictionary *txd, const char *name)
{
	if(txd == nil || name == nil)
		return nil;
	FORLIST(lnk, txd->textures){
		rw::Texture *tex = rw::Texture::fromDict(lnk);
		if(tex && rw::strcmp_ci(tex->name, name) == 0)
			return tex;
	}
	return nil;
}
static rw::Texture*
fallbackFindCB(const char *name)
{
	rw::Texture *t = originalFindCB(name);
	if(t) return t;
	t = fallbackTxd->find(name);
	if(t) return t;
	return findTextureCaseInsensitive(fallbackTxd, name);
}

void
LoadGame(void)
{
// for debugging...
//	SetCurrentDirectory("C:/Users/aap/games/gta3");
//	SetCurrentDirectory("C:/Users/aap/games/gtavc");
//	SetCurrentDirectory("C:/Users/aap/games/gtasa");

	SAPaths::Reset();
	FindVersion();
	ModloaderInit();
	switch(gameversion){
	case GAME_III: debug("found III!\n"); break;
	case GAME_VC: debug("found VC!\n"); break;
	case GAME_SA: debug("found SA!\n"); break;
	case GAME_LCS: debug("found LCS!\n"); break;
	case GAME_VCS: debug("found VCS!\n"); break;
	default: panic("unknown game");
	}
	switch(gameplatform){
	case PLATFORM_PS2: debug("assuming PS2\n"); break;
	case PLATFORM_XBOX: debug("assuming Xbox\n"); break;
	default: debug("assuming PC\n"); break;
	}
	InitParams();

	TheCamera.m_position = params.initcampos;
	TheCamera.m_target = params.initcamtarg;
	TheCamera.setDistanceToTarget(50.0f);
	gDoBackfaceCulling = params.backfaceCull;

	defaultTxd = rw::TexDictionary::getCurrent();

	int particleTxdSlot = AddTxdSlot("particle");
	LoadTxd(particleTxdSlot, "MODELS/PARTICLE.TXD");

	gameTxdSlot = AddTxdSlot("generic");
	CreateTxd(gameTxdSlot);
	TxdMakeCurrent(gameTxdSlot);
	if(params.txdFallbackGeneric){
		fallbackTxd = rw::TexDictionary::getCurrent();
		originalFindCB = rw::Texture::findCB;
		rw::Texture::findCB = fallbackFindCB;
	}

	Timecycle::Initialize();
	if(params.neoWorldPipe)
		Timecycle::InitNeoWorldTweak();
	WaterLevel::Initialise();
	Clouds::Init();

	AddColSlot("generic");
	AddIplSlot("generic");

	AddCdImage("MODELS\\GTA3.IMG");
	if(isSA())
		AddCdImage("MODELS\\GTA_INT.IMG");

	if(ModloaderIsActive()){
		ModloaderDatEntry imgEntries[64];
		int nImg = ModloaderGetAdditions(imgEntries, 64);
		for(int i = 0; i < nImg; i++)
			if(strcmp(imgEntries[i].type, "IMG") == 0 || strcmp(imgEntries[i].type, "CDIMAGE") == 0)
				AddCdImage(imgEntries[i].logicalPath);
	}

	FileLoader::LoadLevel("data/default.dat");
	switch(gameversion){
	case GAME_III: FileLoader::LoadLevel("data/gta3.dat"); break;
	case GAME_VC: FileLoader::LoadLevel("data/gta_vc.dat"); break;
	case GAME_SA: FileLoader::LoadLevel("data/gta.dat"); break;
	case GAME_LCS: FileLoader::LoadLevel("data/gta_lcs.dat"); break;
	case GAME_VCS: FileLoader::LoadLevel("data/gta_vcs.dat"); break;
	}

	if(ModloaderIsActive()){
		ModloaderDatEntry entries[256];
		int n = ModloaderGetAdditions(entries, 256);
		RefreshCdImageMappings();
		for(int i = 0; i < n; i++){
			if(strcmp(entries[i].type, "IPL") == 0){
				FileLoader::currentFile = NewGameFile((char*)entries[i].logicalPath);
				FileLoader::LoadScene(entries[i].logicalPath);
			}
		}
	}

	InitLodLookup();
	InitObjectCategories();
	LoadFavourites();
	// InitPreviewRenderer called lazily on first use
	InitSectors();

	CPtrNode *p;
	ObjectInst *inst;
	int instCount = 0;
	for(p = instances.first; p; p = p->next){
		inst = (ObjectInst*)p->item;
		InsertInstIntoSectors(inst);
		instCount++;
	}

	// hide the islands
	ObjectDef *obj;
	if(params.map == GAME_III){
		obj = GetObjectDef("IslandLODInd", nil);
		if(obj) obj->m_isHidden = true;
		obj = GetObjectDef("IslandLODcomIND", nil);
		if(obj) obj->m_isHidden = true;
		obj = GetObjectDef("IslandLODcomSUB", nil);
		if(obj) obj->m_isHidden = true;
		obj = GetObjectDef("IslandLODsubIND", nil);
		if(obj) obj->m_isHidden = true;
		obj = GetObjectDef("IslandLODsubCOM", nil);
		if(obj) obj->m_isHidden = true;
	}else if(params.map == GAME_VC){
		obj = GetObjectDef("IslandLODmainland", nil);
		if(obj) obj->m_isHidden = true;
		obj = GetObjectDef("IslandLODbeach", nil);
		if(obj) obj->m_isHidden = true;
	}
}

static void
updateRwFrame(ObjectInst *inst)
{
	if(inst->m_rwObject == nil) return;
	ObjectDef *obj = GetObjectDef(inst->m_objectId);
	if(obj == nil) return;
	rw::Frame *f;
	if(obj->m_type == ObjectDef::ATOMIC)
		f = ((rw::Atomic*)inst->m_rwObject)->getFrame();
	else
		f = ((rw::Clump*)inst->m_rwObject)->getFrame();
	f->transform(&inst->m_matrix, rw::COMBINEREPLACE);
}

void
dogizmo(void)
{
	gGizmoHovered = false;
	gGizmoUsing = false;

	if(!gGizmoEnabled)
		return;

	if(WaterLevel::gWaterEditMode){
		WaterLevel::DoWaterGizmo();
		return;
	}

	if(!selection.first){
		static bool wasDraggingSaNode = false;
		static rw::V3d dragStartSaNodePos;

		if(!SAPaths::HasSelectedNode()){
			wasDraggingSaNode = false;
			return;
		}

		rw::V3d nodePos;
		if(!SAPaths::GetSelectedNodePosition(&nodePos))
			return;

		rw::Camera *cam = (rw::Camera*)rw::engine->currentCamera;
		float *fview = (float*)&cam->devView;
		float *fproj = (float*)&cam->devProj;
		rw::RawMatrix gizobj;
		rw::RawMatrix::setIdentity(&gizobj);
		gizobj.pos.x = nodePos.x;
		gizobj.pos.y = nodePos.y;
		gizobj.pos.z = nodePos.z;
		float *fobj = (float*)&gizobj;

		ImGuiIO &io = ImGui::GetIO();
		ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

		float snapValues[3];
		float *snapPtr = nil;
		bool useSnap = gGizmoSnap && CPad::IsShiftDown();
		if(useSnap){
			snapValues[0] = gGizmoSnapTranslate;
			snapValues[1] = gGizmoSnapTranslate;
			snapValues[2] = gGizmoSnapTranslate;
			snapPtr = snapValues;
		}

		ImGuizmo::Manipulate(fview, fproj, ImGuizmo::TRANSLATE, ImGuizmo::WORLD, fobj, nil, snapPtr);

		gGizmoHovered = ImGuizmo::IsOver();
		bool isUsing = ImGuizmo::IsUsing();
		gGizmoUsing = isUsing;

		if(isUsing && !wasDraggingSaNode)
			dragStartSaNodePos = nodePos;

		if(isUsing){
			rw::V3d newPos = { gizobj.pos.x, gizobj.pos.y, gizobj.pos.z };
			SAPaths::SetSelectedNodePosition(newPos, false);
		}else if(wasDraggingSaNode){
			rw::V3d finalPos;
			if(SAPaths::GetSelectedNodePosition(&finalPos) &&
			   length(sub(finalPos, dragStartSaNodePos)) >= 0.0001f)
				SAPaths::CommitSelectedNodeEdit();
		}

		wasDraggingSaNode = isUsing;
		return;
	}

	ObjectInst *inst = (ObjectInst*)selection.first->item;
	if(inst->m_isDeleted)
		return;

	static bool wasDragging = false;
	static rw::V3d dragStartLeaderPos;
	static rw::Quat dragStartLeaderRot;
	static bool dragStartFollowGround;
	static bool dragStartAlignToSurface;
	static float dragGroundOffset;
	static rw::Quat dragGroundBaseRot;
	// Snapshot of all affected objects for multi-select translate
	static UndoTransform dragTransforms[MAX_BATCH_OBJECTS];
	static int dragNumTransforms;

	rw::Camera *cam;
	rw::RawMatrix gizobj;
	float *fview, *fproj, *fobj;

	// Build object matrix from instance
	rw::convMatrix(&gizobj, &inst->m_matrix);

	cam = (rw::Camera*)rw::engine->currentCamera;
	fview = (float*)&cam->devView;
	fproj = (float*)&cam->devProj;
	fobj = (float*)&gizobj;

	ImGuiIO &io = ImGui::GetIO();
	ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

	ImGuizmo::OPERATION op = gGizmoMode == GIZMO_ROTATE ? ImGuizmo::ROTATE : ImGuizmo::TRANSLATE;
	float snapValues[3];
	float *snapPtr = nil;
	bool useSnap = gGizmoSnap && CPad::IsShiftDown();
	if(useSnap){
		if(gGizmoMode == GIZMO_ROTATE){
			snapValues[0] = gGizmoSnapAngle;
			snapValues[1] = gGizmoSnapAngle;
			snapValues[2] = gGizmoSnapAngle;
		}else{
			snapValues[0] = gGizmoSnapTranslate;
			snapValues[1] = gGizmoSnapTranslate;
			snapValues[2] = gGizmoSnapTranslate;
		}
		snapPtr = snapValues;
	}
	ImGuizmo::Manipulate(fview, fproj, op, ImGuizmo::LOCAL, fobj, nil, snapPtr);

	gGizmoHovered = ImGuizmo::IsOver();
	bool isUsing = ImGuizmo::IsUsing();
	gGizmoUsing = isUsing;

	// Capture start state when drag begins
	if(isUsing && !wasDragging){
		dragStartLeaderPos = inst->m_translation;
		dragStartLeaderRot = NormalizeQuatOrIdentity(inst->m_rotation);
		dragStartFollowGround = gGizmoMode == GIZMO_TRANSLATE && gDragFollowGround;
		dragStartAlignToSurface = dragStartFollowGround && gDragAlignToSurface;
		dragGroundBaseRot = dragStartLeaderRot;
		dragGroundOffset = 0.0f;
		if(dragStartFollowGround){
			rw::V3d hitPos, hitNormal;
			if(GetGroundPlacementSurface(inst->m_translation, &hitPos, &hitNormal, true))
				dragGroundOffset = inst->m_translation.z - (hitPos.z - GetMinZOffsetForRotation(inst, inst->m_rotation));
		}

		// Build deduplicated snapshot of all affected objects
		dragNumTransforms = 0;
		bool dragOverflow = false;
		for(CPtrNode *p = selection.first; p; p = p->next){
			ObjectInst *sel = (ObjectInst*)p->item;
			if(sel->m_isDeleted)
				continue;
			if(FindOrAddTransform(dragTransforms, &dragNumTransforms, sel) == nil)
				dragOverflow = true;
			// Include LOD if this object has one
			if(sel->m_lod && !sel->m_lod->m_isDeleted)
				if(FindOrAddTransform(dragTransforms, &dragNumTransforms, sel->m_lod) == nil)
					dragOverflow = true;
			// If this IS a LOD, include its HD children
			for(CPtrNode *q = instances.first; q; q = q->next){
				ObjectInst *child = (ObjectInst*)q->item;
				if(child != sel && child->m_lod == sel && !child->m_isDeleted)
					if(FindOrAddTransform(dragTransforms, &dragNumTransforms, child) == nil)
						dragOverflow = true;
			}
		}
		if(dragOverflow)
			Toast(TOAST_SELECTION, "Selection too large: some objects won't move (max %d)", MAX_BATCH_OBJECTS);
	}
	// Record undo when drag ends
	if(!isUsing && wasDragging){
		UndoTransform finalTransforms[MAX_BATCH_OBJECTS];
		int numFinal = 0;
		for(int i = 0; i < dragNumTransforms; i++){
			ObjectInst *obj = dragTransforms[i].inst;
			uint8 flags = 0;
			if(length(sub(obj->m_translation, dragTransforms[i].oldPos)) >= 0.0001f)
				flags |= UNDO_TRANSFORM_POS;
			if(memcmp(&obj->m_rotation, &dragTransforms[i].oldRot, sizeof(rw::Quat)) != 0)
				flags |= UNDO_TRANSFORM_ROT;
			if(flags != 0){
				if(flags & UNDO_TRANSFORM_POS){
					RemoveInstFromSectors(obj);
					InsertInstIntoSectors(obj);
				}
				StampChangeSeq(obj);
				UndoTransform &t = finalTransforms[numFinal++];
				t.inst = obj;
				t.oldPos = dragTransforms[i].oldPos;
				t.newPos = obj->m_translation;
				t.oldRot = dragTransforms[i].oldRot;
				t.newRot = obj->m_rotation;
				t.flags = flags;
			}
		}
		if(numFinal > 0)
			UndoRecordTransformBatch(finalTransforms, numFinal);
	}
	wasDragging = isUsing;

	if(isUsing){
		// Extract position from the gizmo result
		rw::V3d newLeaderPos;
		newLeaderPos.x = gizobj.pos.x;
		newLeaderPos.y = gizobj.pos.y;
		newLeaderPos.z = gizobj.pos.z;

		if(gGizmoMode == GIZMO_TRANSLATE){
			rw::Quat newLeaderRot = inst->m_rotation;
			if(dragStartFollowGround){
				rw::V3d groundHit, groundNormal;
				if(GetGroundPlacementSurface(newLeaderPos, &groundHit, &groundNormal, true)){
					if(dragStartAlignToSurface)
						newLeaderRot = BuildGroundAlignedRotationFromRotation(dragGroundBaseRot, groundNormal);
					newLeaderPos.z = groundHit.z - GetMinZOffsetForRotation(inst, newLeaderRot) + dragGroundOffset;
				}
			}

			// Compute total delta from leader's start position (avoids frame-by-frame drift)
			rw::V3d totalDelta = sub(newLeaderPos, dragStartLeaderPos);

			// Pass 1: compute new positions and rotations from snapshot
			// (don't update matrices yet, so raycasts see old collision state)
			for(int i = 0; i < dragNumTransforms; i++){
				ObjectInst *obj = dragTransforms[i].inst;
				rw::V3d newPos = add(dragTransforms[i].oldPos, totalDelta);
				obj->m_rotation = dragTransforms[i].oldRot;

				if(dragStartFollowGround && obj != inst){
					// Per-object ground follow: each object individually snaps to terrain
					rw::V3d groundHit, groundNormal;
					if(GetGroundPlacementSurface(newPos, &groundHit, &groundNormal, true)){
						rw::Quat rot = dragTransforms[i].oldRot;
						if(dragStartAlignToSurface)
							rot = BuildGroundAlignedRotationFromRotation(dragTransforms[i].oldRot, groundNormal);
						newPos.z = groundHit.z - GetMinZOffsetForRotation(obj, rot);
						obj->m_rotation = rot;
					}
				}

				obj->m_translation = newPos;
				obj->m_isDirty = true;
			}

			// Leader align-to-surface (uses its own ground-followed rotation)
			if(dragStartAlignToSurface)
				inst->m_rotation = newLeaderRot;

			// Pass 2: update all matrices and frames at once
			for(int i = 0; i < dragNumTransforms; i++){
				dragTransforms[i].inst->UpdateMatrix();
				updateRwFrame(dragTransforms[i].inst);
			}
		}else if(gGizmoMode == GIZMO_ROTATE){
			// Extract leader's new rotation from gizmo result
			inst->m_matrix.right.x = gizobj.right.x;
			inst->m_matrix.right.y = gizobj.right.y;
			inst->m_matrix.right.z = gizobj.right.z;
			inst->m_matrix.up.x = gizobj.up.x;
			inst->m_matrix.up.y = gizobj.up.y;
			inst->m_matrix.up.z = gizobj.up.z;
			inst->m_matrix.at.x = gizobj.at.x;
			inst->m_matrix.at.y = gizobj.at.y;
			inst->m_matrix.at.z = gizobj.at.z;
			rw::Quat newLeaderRot = QuatFromMatrix(inst->m_matrix);

			// Stored instance quaternions are conjugated relative to world-space rotation.
			// To apply the leader's world-space delta to the rest of the selection, the
			// stored-space delta must be built as conj(start) * new, not new * conj(start).
			rw::Quat deltaQ = NormalizeQuatOrIdentity(rw::mult(rw::conj(dragStartLeaderRot), newLeaderRot));

			// Apply to all affected objects: orbit positions around leader, compose rotations.
			// conj(deltaQ) is the world-space delta; right-multiplying oldRot by deltaQ
			// applies that world delta under this codebase's conj(m_rotation) convention.
			rw::Quat worldQ = rw::conj(deltaQ);
			for(int i = 0; i < dragNumTransforms; i++){
				ObjectInst *obj = dragTransforms[i].inst;
				rw::V3d offset = sub(dragTransforms[i].oldPos, dragStartLeaderPos);
				obj->m_translation = add(dragStartLeaderPos, rw::rotate(offset, worldQ));
				obj->m_rotation = NormalizeQuatOrIdentity(rw::mult(dragTransforms[i].oldRot, deltaQ));
				obj->m_isDirty = true;
				obj->UpdateMatrix();
				updateRwFrame(obj);
			}
		}
	}
}

static uint64 frameCounter;

void
updateFPS(void)
{
	static float history[100];
	static float total;
	static int n;
	static int i;

	total += timeStep - history[i];
	history[i] = timeStep;
	i = (i+1) % 100;
	n = i > n ? i : n;
	avgTimeStep = total / n;
}

void
Draw(void)
{
	static rw::RGBA clearcol = { 0x80, 0x80, 0x80, 0xFF };

	CPad *pad = CPad::GetPad(0);
	if(pad->NewState.start && pad->NewState.select){
		sk::globals.quit = 1;
		return;
	}

	// HACK: we load a lot in the first frame
	// which messes up the average
	if(frameCounter == 0)
		timeStep = 1/30.0f;

	if(!gOverrideBlurAmb)
		gUseBlurAmb = gRenderPostFX;

	updateFPS();

	Weather::Update();
	Timecycle::Update();
	Timecycle::SetLights();

	UpdateDayNightBalance();

	TheCamera.m_rwcam->setFarPlane(max(Timecycle::currentColours.farClp, 500.0f));
	TheCamera.m_rwcam->fogPlane = Timecycle::currentColours.fogSt;
	TheCamera.m_rwcam_viewer->setFarPlane(5000.0f);
	TheCamera.m_rwcam_viewer->fogPlane = Timecycle::currentColours.fogSt;

	CPad::UpdatePads();
	updateRectSelectEarly();
	TheCamera.Process();
	TheCamera.update();
	if(gUseViewerCam)
		Scene.camera = TheCamera.m_rwcam_viewer;
	else
		Scene.camera = TheCamera.m_rwcam;
	// Render 3D preview to texture (before main camera update)
	if(GetSpawnObjectId() >= 0)
		RenderPreviewObject(GetSpawnObjectId());

	Scene.camera->beginUpdate();

	DefinedState();

	ImGui_ImplRW_NewFrame(timeStep);
	ImGuizmo::BeginFrame();

	LoadAllRequestedObjects();
	BuildRenderList();

	// Has to be called for highlighting some objects
	// but also can mess with timecycle mid frame :/
	gui();

	dogizmo();

	handleTool();

	handleRectSelect();

	DefinedState();
	Scene.camera->clear(&clearcol, rw::Camera::CLEARIMAGE|rw::Camera::CLEARZ);
	if(gRenderBackground){
		SetRenderState(rw::ALPHATESTREF, 0);
		SetRenderState(rw::CULLMODE, rw::CULLNONE);
		rw::RGBA skytop, skybot;
		rw::convColor(&skytop, &Timecycle::currentColours.skyTop);
		rw::convColor(&skybot, &Timecycle::currentColours.skyBottom);
		if(params.background == GAME_SA){
			Clouds::RenderSkyPolys();
			Clouds::RenderLowClouds();
		}else{
			Clouds::RenderBackground(skytop.red, skytop.green, skytop.blue,
				skybot.red, skybot.green, skybot.blue, 255);
			Clouds::RenderLowClouds();
			if(params.timecycle != GAME_VCS)
				Clouds::RenderFluffyClouds();
			Clouds::RenderHorizon();
		}
		SetRenderState(rw::ALPHATESTREF, params.alphaRef);
	}

	rw::SetRenderState(rw::FOGENABLE, gEnableFog);
	RenderOpaque();
	if(gRenderWater)
		WaterLevel::Render();
	RenderTransparent();
	if(gRenderLightEffects)
		Effects::RenderLights();
	// DEBUG render object picking
	//RenderEverythingColourCoded();


	if(gRenderPostFX)
		RenderPostFX();

	DefinedState();
	rw::SetRenderState(rw::FOGENABLE, 0);

	SetRenderState(rw::CULLMODE, rw::CULLNONE);

	if(gDrawTarget)
		TheCamera.DrawTarget();
	if(gRenderCollision)
		RenderEverythingCollisions();
	if(gRenderTimecycleBoxes)
		Timecycle::RenderBoxes();
	if(gRenderZones)
		Zones::Render();
	if(gRenderCullZones)
		Zones::RenderCullZones();
	if(gRenderAttribZones)
		Zones::RenderAttribZones();
	Path::hoveredNode = nil;
	SAPaths::hoveredNode = nil;
	Effects::hoveredEffect = nil;
	if(gRenderLegacyPedPaths)
		Path::RenderPedPaths();
	if(gRenderLegacyCarPaths)
		Path::RenderCarPaths();
	if(gRenderSaPedPaths)
		SAPaths::RenderPedPaths();
	if(gRenderSaCarPaths)
		SAPaths::RenderCarPaths();
	if(gRenderSaAreaGrid)
		SAPaths::RenderAreaGrid();
	if(gRenderEffects)
		Effects::Render();
	if(WaterLevel::gWaterEditMode)
		WaterLevel::RenderEditOverlay();

	rw::SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAALWAYS);	// don't mess up GUI
	// This fucks up the z buffer, but what else can we do?
	RenderDebugLines();
	ImGui::EndFrame();
	ImGui::Render();

	ImGui_ImplRW_RenderDrawLists(ImGui::GetDrawData());

	Scene.camera->endUpdate();
	Scene.camera->showRaster(rw::Raster::FLIPWAITVSYNCH);
	frameCounter++;
}

void
Idle(void)
{
	static int state = 0;
	switch(state){
	case 0:
		LoadGame();
		state = 1;
		break;
	case 1:
		Draw();
		break;
	}
}
