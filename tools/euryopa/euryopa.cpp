#include "euryopa.h"
#include "modloader.h"

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
bool gGizmoSnap = false;
float gGizmoSnapAngle = 15.0f;
float gGizmoSnapTranslate = 1.0f;

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

// non-rendering things
bool gRenderCollision;
bool gRenderZones;
bool gRenderMapZones;
bool gRenderNavigZones;
bool gRenderInfoZones;
bool gRenderCullZones;
bool gRenderAttribZones;
bool gRenderPedPaths;
bool gRenderCarPaths;
bool gRenderEffects;
bool gRenderTimecycleBoxes;

// SA postfx
int  gColourFilter;
bool gRadiosity;

// SA building pipe
int gBuildingPipeSwitch = PLATFORM_PS2;
float gDayNightBalance;
float gWetRoadEffect;

// Neo stuff
float gNeoLightMapStrength = 0.5f;

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

static bool
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

static bool
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

static bool
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

static float
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
	return q;
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

static rw::Quat
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
	if(*numTransforms >= 64)
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
	ObjectInst *targets[64];
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
		if(numTargets < 64)
			targets[numTargets++] = inst;
	}

	if(numTargets == 0){
		if(skipped > 0)
			Toast(TOAST_SELECTION, "Ground snap skipped linked multi-selection");
		return 0;
	}

	UndoTransform transforms[64];
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

static rw::V3d
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

void
handleTool(void)
{
	// Don't process viewport clicks when ImGui wants the mouse
	ImGuiIO &io = ImGui::GetIO();
	if(io.WantCaptureMouse)
		return;

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

	// select
	if(CPad::IsMButtonClicked(1)){
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

		Path::selectedNode = Path::hoveredNode;
		Effects::selectedEffect = Effects::hoveredEffect;
	}else if(CPad::IsMButtonClicked(2)){
		if(CPad::IsCtrlDown()){
			Path::selectedNode = Path::hoveredNode;
			Effects::selectedEffect = Effects::hoveredEffect;
		}else{
			ClearSelection();
			ObjectInst *inst = GetInstanceByID(pick());
			if(inst && !inst->m_isDeleted)
				inst->Select();
		}
	}else if(CPad::IsMButtonClicked(3)){
		ClearSelection();
		Path::selectedNode = nil;
		Effects::selectedEffect = nil;
	}
}

rw::Texture *(*originalFindCB)(const char *name);
rw::TexDictionary *fallbackTxd;
static rw::Texture*
fallbackFindCB(const char *name)
{
	rw::Texture *t = originalFindCB(name);
	if(t) return t;
	return fallbackTxd->find(name);
}

void
LoadGame(void)
{
// for debugging...
//	SetCurrentDirectory("C:/Users/aap/games/gta3");
//	SetCurrentDirectory("C:/Users/aap/games/gtavc");
//	SetCurrentDirectory("C:/Users/aap/games/gtasa");

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
	if(!selection.first)
		return;

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
	static UndoTransform dragTransforms[64];
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
	if(gGizmoSnap){
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
		dragStartLeaderRot = inst->m_rotation;
		dragStartFollowGround = gGizmoMode == GIZMO_TRANSLATE && gDragFollowGround;
		dragStartAlignToSurface = dragStartFollowGround && gDragAlignToSurface;
		dragGroundBaseRot = inst->m_rotation;
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
			Toast(TOAST_SELECTION, "Selection too large: some objects won't move (max 64)");
	}
	// Record undo when drag ends
	if(!isUsing && wasDragging){
		UndoTransform finalTransforms[64];
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

			// Apply delta to all affected objects from snapshot
			for(int i = 0; i < dragNumTransforms; i++){
				ObjectInst *obj = dragTransforms[i].inst;
				obj->m_translation = add(dragTransforms[i].oldPos, totalDelta);
				obj->m_isDirty = true;
				obj->UpdateMatrix();
				updateRwFrame(obj);
			}

			// Apply leader-specific align-to-surface rotation
			if(dragStartAlignToSurface){
				inst->m_rotation = newLeaderRot;
				inst->UpdateMatrix();
				updateRwFrame(inst);
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

			// Compute rotation delta: the rotation that transforms startRot into newRot
			rw::Quat deltaQ = rw::mult(newLeaderRot, rw::conj(dragStartLeaderRot));

			// Apply to all affected objects: orbit positions around leader, compose rotations.
			// conj(deltaQ) converts from stored-quaternion space to world space;
			// right-multiplying oldRot by deltaQ applies the delta on the world side
			// of this codebase's conj(m_rotation) matrix convention.
			rw::Quat worldQ = rw::conj(deltaQ);
			for(int i = 0; i < dragNumTransforms; i++){
				ObjectInst *obj = dragTransforms[i].inst;
				rw::V3d offset = sub(dragTransforms[i].oldPos, dragStartLeaderPos);
				obj->m_translation = add(dragStartLeaderPos, rw::rotate(offset, worldQ));
				obj->m_rotation = rw::mult(dragTransforms[i].oldRot, deltaQ);
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
	Effects::hoveredEffect = nil;
	if(gRenderPedPaths)
		Path::RenderPedPaths();
	if(gRenderCarPaths)
		Path::RenderCarPaths();
	if(gRenderEffects)
		Effects::Render();

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
