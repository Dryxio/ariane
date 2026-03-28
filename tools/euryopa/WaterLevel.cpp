#include "euryopa.h"
#include "modloader.h"
#include <limits.h>

namespace WaterLevel
{

#define WATERSTARTX params.waterStart.x
#define WATERENDX params.waterEnd.x
#define WATERSTARTY params.waterStart.y
#define WATERENDY params.waterEnd.y
#define WATERSMALLSECTORSZX ((WATERENDX - WATERSTARTX)/128)
#define WATERSMALLSECTORSZY ((WATERENDY - WATERSTARTY)/128)
#define WATERSECTORSZX ((WATERENDX - WATERSTARTX)/64)
#define WATERSECTORSZY ((WATERENDY - WATERSTARTY)/64)

static int ms_nNoOfWaterLevels;
static float ms_aWaterZs[48];
static CRect ms_aWaterRects[48];	// Seems to be unused
static uint8 aWaterBlockList[64][64];
static uint8 aWaterFineBlockList[128][128];

rw::Texture *gpWaterTex;
rw::Raster *gpWaterRaster;

// SA
WaterVertex waterVertices[NUMWATERVERTICES];
int numWaterVertices;
WaterQuad waterQuads[NUMWATERQUADS];
int numWaterQuads;
WaterTri waterTris[NUMWATERTRIS];
int numWaterTris;

// Editor state
bool gWaterEditMode;
int gWaterSubMode;	// 0=polygon, 1=vertex
bool gWaterDirty;
int gWaterCreateMode;	// 0=off, 1..N=placing corners
int gWaterCreateShape;	// 0=quad, 1=triangle
float gWaterCreateZ;
bool gWaterSnapEnabled = true;
float gWaterSnapSize = 4.0f;

// Accessor API
int GetNumQuads(void) { return numWaterQuads; }
int GetNumTris(void) { return numWaterTris; }
int GetNumVertices(void) { return numWaterVertices; }
WaterVertex *GetVertex(int i) { return &waterVertices[i]; }
WaterQuad *GetQuad(int i) { return &waterQuads[i]; }
WaterTri *GetTri(int i) { return &waterTris[i]; }

void
InitialiseWaterpro(void)
{
	FILE *file;

	ms_nNoOfWaterLevels = 0;
	if(file = fopen_ci("DATA\\waterpro.dat", "rb"), file == nil)
		return;
	fread(&ms_nNoOfWaterLevels, 1, sizeof(ms_nNoOfWaterLevels), file);
	fread(&ms_aWaterZs, 1, sizeof(ms_aWaterZs), file);
	fread(&ms_aWaterRects, 1, sizeof(ms_aWaterRects), file);
	fread(&aWaterBlockList, 1, sizeof(aWaterBlockList), file);
	fread(&aWaterFineBlockList, 1, sizeof(aWaterFineBlockList), file);
	fclose(file);
}

void
InitialiseWater(void)
{
	WaterVertex v[4];
	int flags;
	int nfields;

	FILE *file;
	char *line;
	if(file = fopen_ci("data/water.dat", "rb"), file == nil)
		return;
	int h = 0, w = 0;
	while(line = FileLoader::LoadLine(file)){
		if(line[0] == ';' || line[0] == '*' || line[0] == 'p')
			continue;
		flags = 1;
		nfields = sscanf(line,
			"%f %f %f %f %f %f %f "
			"%f %f %f %f %f %f %f "
			"%f %f %f %f %f %f %f "
			"%f %f %f %f %f %f %f "
			"%d",
			&v[0].pos.x, &v[0].pos.y, &v[0].pos.z, &v[0].speed.x, &v[0].speed.y, &v[0].waveunk, &v[0].waveheight,
			&v[1].pos.x, &v[1].pos.y, &v[1].pos.z, &v[1].speed.x, &v[1].speed.y, &v[1].waveunk, &v[1].waveheight,
			&v[2].pos.x, &v[2].pos.y, &v[2].pos.z, &v[2].speed.x, &v[2].speed.y, &v[2].waveunk, &v[2].waveheight,
			&v[3].pos.x, &v[3].pos.y, &v[3].pos.z, &v[3].speed.x, &v[3].speed.y, &v[3].waveunk, &v[3].waveheight,
			&flags);
		if(nfields == 28 || nfields == 29){
			// quad
			if(numWaterVertices+4 > NUMWATERVERTICES ||
			   numWaterQuads+1 > NUMWATERQUADS){
				log("warning: too much water (%d vertices, %d quads, %d tris)\n",
					numWaterVertices, numWaterQuads, numWaterTris);
				continue;
			}
			WaterQuad *q = &waterQuads[numWaterQuads++];
			q->indices[0] = numWaterVertices++;
			q->indices[1] = numWaterVertices++;
			q->indices[2] = numWaterVertices++;
			q->indices[3] = numWaterVertices++;
			waterVertices[q->indices[0]] = v[0];
			waterVertices[q->indices[1]] = v[1];
			waterVertices[q->indices[2]] = v[2];
			waterVertices[q->indices[3]] = v[3];
			q->flags = flags;
		}else{
			if(numWaterVertices+3 > NUMWATERVERTICES ||
			   numWaterTris+1 > NUMWATERTRIS){
				log("warning: too much water (%d vertices, %d quads, %d tris)\n",
					numWaterVertices, numWaterQuads, numWaterTris);
				continue;
			}
			// triangle
			WaterTri *t = &waterTris[numWaterTris++];
			t->indices[0] = numWaterVertices++;
			t->indices[1] = numWaterVertices++;
			t->indices[2] = numWaterVertices++;
			waterVertices[t->indices[0]] = v[0];
			waterVertices[t->indices[1]] = v[1];
			waterVertices[t->indices[2]] = v[2];
			t->flags = flags;
		}
	}
	fclose(file);
}

void
Initialise(void)
{
	if(params.water == GAME_SA)
		InitialiseWater();
	else
		InitialiseWaterpro();

	TxdPush();
	int ptxd = FindTxdSlot("particle");
	TxdMakeCurrent(ptxd);
	if(gpWaterTex == nil)
		gpWaterTex = rw::Texture::read(params.waterTex, nil);
	gpWaterRaster = gpWaterTex->raster;
	TxdPop();
}

#define TEMPBUFFERVERTSIZE 256
#define TEMPBUFFERINDEXSIZE 1024
static int TempBufferIndicesStored;
static int TempBufferVerticesStored;
static rw::RWDEVICE::Im3DVertex TempVertexBuffer[TEMPBUFFERVERTSIZE];
static uint16 TempIndexBuffer[TEMPBUFFERINDEXSIZE];

float TEXTURE_ADDU, TEXTURE_ADDV;

static void
RenderAndEmptyRenderBuffer(void)
{
	assert(TempBufferVerticesStored <= TEMPBUFFERVERTSIZE);
	assert(TempBufferIndicesStored <= TEMPBUFFERINDEXSIZE);
	if(TempBufferVerticesStored){
		rw::im3d::Transform(TempVertexBuffer, TempBufferVerticesStored, nil, rw::im3d::EVERYTHING);
		rw::im3d::RenderIndexedPrimitive(rw::PRIMTYPETRILIST, TempIndexBuffer, TempBufferIndicesStored);
		rw::im3d::End();
	}
	TempBufferVerticesStored = 0;
	TempBufferIndicesStored = 0;
}

void
RenderOneFlatSmallWaterPoly(float x, float y, float z, rw::RGBA const &color)
{
	if(TempBufferVerticesStored+4 >= TEMPBUFFERVERTSIZE ||
	   TempBufferIndicesStored+6 >= TEMPBUFFERINDEXSIZE)
		RenderAndEmptyRenderBuffer();

	int i = TempBufferVerticesStored;
	TempVertexBuffer[i + 0].setX(x);
	TempVertexBuffer[i + 0].setY(y);
	TempVertexBuffer[i + 0].setZ(z);
	TempVertexBuffer[i + 0].setU(TEXTURE_ADDU);
	TempVertexBuffer[i + 0].setV(TEXTURE_ADDV);
	TempVertexBuffer[i + 0].setColor(color.red, color.green, color.blue, color.alpha);

	TempVertexBuffer[i + 1].setX(x);
	TempVertexBuffer[i + 1].setY(y + WATERSMALLSECTORSZY);
	TempVertexBuffer[i + 1].setZ(z);
	TempVertexBuffer[i + 1].setU(TEXTURE_ADDU);
	TempVertexBuffer[i + 1].setV(TEXTURE_ADDV + 1.0f);
	TempVertexBuffer[i + 1].setColor(color.red, color.green, color.blue, color.alpha);

	TempVertexBuffer[i + 2].setX(x + WATERSMALLSECTORSZX);
	TempVertexBuffer[i + 2].setY(y + WATERSMALLSECTORSZY);
	TempVertexBuffer[i + 2].setZ(z);
	TempVertexBuffer[i + 2].setU(TEXTURE_ADDU + 1.0f);
	TempVertexBuffer[i + 2].setV(TEXTURE_ADDV + 1.0f);
	TempVertexBuffer[i + 2].setColor(color.red, color.green, color.blue, color.alpha);

	TempVertexBuffer[i + 3].setX(x + WATERSMALLSECTORSZX);
	TempVertexBuffer[i + 3].setY(y);
	TempVertexBuffer[i + 3].setZ(z);
	TempVertexBuffer[i + 3].setU(TEXTURE_ADDU + 1.0f);
	TempVertexBuffer[i + 3].setV(TEXTURE_ADDV);
	TempVertexBuffer[i + 3].setColor(color.red, color.green, color.blue, color.alpha);

	TempBufferVerticesStored += 4;
	TempIndexBuffer[TempBufferIndicesStored++] = i+0;
	TempIndexBuffer[TempBufferIndicesStored++] = i+1;
	TempIndexBuffer[TempBufferIndicesStored++] = i+2;
	TempIndexBuffer[TempBufferIndicesStored++] = i+0;
	TempIndexBuffer[TempBufferIndicesStored++] = i+2;
	TempIndexBuffer[TempBufferIndicesStored++] = i+3;
}

void
RenderOneFlatLargeWaterPoly(float x, float y, float z, rw::RGBA const &color)
{
	if(TempBufferVerticesStored+4 >= TEMPBUFFERVERTSIZE ||
	   TempBufferIndicesStored+6 >= TEMPBUFFERINDEXSIZE)
		RenderAndEmptyRenderBuffer();

	int i = TempBufferVerticesStored;
	TempVertexBuffer[i + 0].setX(x);
	TempVertexBuffer[i + 0].setY(y);
	TempVertexBuffer[i + 0].setZ(z);
	TempVertexBuffer[i + 0].setU(TEXTURE_ADDU);
	TempVertexBuffer[i + 0].setV(TEXTURE_ADDV);
	TempVertexBuffer[i + 0].setColor(color.red, color.green, color.blue, color.alpha);

	TempVertexBuffer[i + 1].setX(x);
	TempVertexBuffer[i + 1].setY(y + WATERSECTORSZY);
	TempVertexBuffer[i + 1].setZ(z);
	TempVertexBuffer[i + 1].setU(TEXTURE_ADDU);
	TempVertexBuffer[i + 1].setV(TEXTURE_ADDV + 2.0f);
	TempVertexBuffer[i + 1].setColor(color.red, color.green, color.blue, color.alpha);

	TempVertexBuffer[i + 2].setX(x + WATERSECTORSZX);
	TempVertexBuffer[i + 2].setY(y + WATERSECTORSZY);
	TempVertexBuffer[i + 2].setZ(z);
	TempVertexBuffer[i + 2].setU(TEXTURE_ADDU + 2.0f);
	TempVertexBuffer[i + 2].setV(TEXTURE_ADDV + 2.0f);
	TempVertexBuffer[i + 2].setColor(color.red, color.green, color.blue, color.alpha);

	TempVertexBuffer[i + 3].setX(x + WATERSECTORSZX);
	TempVertexBuffer[i + 3].setY(y);
	TempVertexBuffer[i + 3].setZ(z);
	TempVertexBuffer[i + 3].setU(TEXTURE_ADDU + 2.0f);
	TempVertexBuffer[i + 3].setV(TEXTURE_ADDV);
	TempVertexBuffer[i + 3].setColor(color.red, color.green, color.blue, color.alpha);

	TempBufferVerticesStored += 4;
	TempIndexBuffer[TempBufferIndicesStored++] = i+0;
	TempIndexBuffer[TempBufferIndicesStored++] = i+1;
	TempIndexBuffer[TempBufferIndicesStored++] = i+2;
	TempIndexBuffer[TempBufferIndicesStored++] = i+0;
	TempIndexBuffer[TempBufferIndicesStored++] = i+2;
	TempIndexBuffer[TempBufferIndicesStored++] = i+3;
}

void
RenderWaterpro(void)
{
	int i, j;

	rw::SetRenderStatePtr(rw::TEXTURERASTER, gpWaterTex->raster);
	rw::SetRenderState(rw::VERTEXALPHA, 1);
	rw::SetRenderState(rw::FOGENABLE, gEnableFog);

	rw::RGBA color = { 255, 255, 255, 255 };
	Timecycle::ColourSet *cs = &Timecycle::currentColours;
	if(params.water == GAME_III){
		color.red = clamp(cs->amb.red + cs->dir.red, 0.0f, 1.0f)*255.0f;
		color.green = clamp(cs->amb.green + cs->dir.green, 0.0f, 1.0f)*255.0f;
		color.blue = clamp(cs->amb.blue + cs->dir.blue, 0.0f, 1.0f)*255.0f;
	}else
		rw::convColor(&color, &cs->water);
	TEXTURE_ADDU = 0.0f;
	TEXTURE_ADDV = 0.0f;
	float x, y, z;

	rw::Sphere sph;
	// Small polys
	sph.radius = sqrt(WATERSMALLSECTORSZX*WATERSMALLSECTORSZX*2.0f)/2.0f;
	for(i = 0; i < 128; i++)
		for(j = 0; j < 128; j++){
			if(aWaterFineBlockList[i][j] & 0x80)
				continue;
			x = WATERSMALLSECTORSZX*i + WATERSTARTX;
			y = WATERSMALLSECTORSZY*j + WATERSTARTY;
			z = ms_aWaterZs[aWaterFineBlockList[i][j]];
			sph.center.x = x + WATERSMALLSECTORSZX/2;
			sph.center.y = y + WATERSMALLSECTORSZY/2;
			sph.center.z = z;
			if(TheCamera.m_rwcam->frustumTestSphere(&sph) != rw::Camera::SPHEREOUTSIDE)
				RenderOneFlatSmallWaterPoly(x, y, z, color);
		}

/*
	// Large polys
	sph.radius = sqrt(WATERSECTORSZX*WATERSECTORSZX*2.0f)/2.0f;
	for(i = 0; i < 64; i++)
		for(j = 0; j < 64; j++){
			if(aWaterBlockList[i][j] & 0x80)
				continue;
			x = WATERSECTORSZX*i + WATERSTARTX;
			y = WATERSECTORSZY*j + WATERSTARTY;
			z = ms_aWaterZs[aWaterBlockList[i][j]];
			sph.center.x = x + WATERSECTORSZX/2;
			sph.center.y = y + WATERSECTORSZY/2;
			sph.center.z = z;
			if(Scene.camera->frustumTestSphere(&sph) != rw::Camera::SPHEREOUTSIDE)
				RenderOneFlatLargeWaterPoly(x, y, z, color);
		}
*/
	RenderAndEmptyRenderBuffer();
}

static const float uvscale = 1/32.0f;

static void
RenderWaterQuad(WaterQuad *q)
{
	if(TempBufferVerticesStored+4 >= TEMPBUFFERVERTSIZE ||
	   TempBufferIndicesStored+6 >= TEMPBUFFERINDEXSIZE)
		RenderAndEmptyRenderBuffer();

	rw::RGBA color;

	rw::convColor(&color, &Timecycle::currentColours.water);

	WaterVertex *v;
	int i = TempBufferVerticesStored;
	int j;
	for(j = 0; j < 4; j++){
		v = &waterVertices[q->indices[j]];
		TempVertexBuffer[i + j].setX(v->pos.x);
		TempVertexBuffer[i + j].setY(v->pos.y);
		TempVertexBuffer[i + j].setZ(v->pos.z);
		TempVertexBuffer[i + j].setU(v->pos.x*uvscale);
		TempVertexBuffer[i + j].setV(v->pos.y*uvscale);
		TempVertexBuffer[i + j].setColor(color.red, color.green, color.blue, color.alpha);
	}

	TempBufferVerticesStored += 4;
	TempIndexBuffer[TempBufferIndicesStored++] = i+0;
	TempIndexBuffer[TempBufferIndicesStored++] = i+1;
	TempIndexBuffer[TempBufferIndicesStored++] = i+2;
	TempIndexBuffer[TempBufferIndicesStored++] = i+2;
	TempIndexBuffer[TempBufferIndicesStored++] = i+1;
	TempIndexBuffer[TempBufferIndicesStored++] = i+3;
}

static void
RenderWaterTri(WaterTri *t)
{
	if(TempBufferVerticesStored+3 >= TEMPBUFFERVERTSIZE ||
	   TempBufferIndicesStored+3 >= TEMPBUFFERINDEXSIZE)
		RenderAndEmptyRenderBuffer();

	rw::RGBA color;

	rw::convColor(&color, &Timecycle::currentColours.water);

	WaterVertex *v;
	int i = TempBufferVerticesStored;
	int j;
	for(j = 0; j < 3; j++){
		v = &waterVertices[t->indices[j]];
		TempVertexBuffer[i + j].setX(v->pos.x);
		TempVertexBuffer[i + j].setY(v->pos.y);
		TempVertexBuffer[i + j].setZ(v->pos.z);
		TempVertexBuffer[i + j].setU(v->pos.x*uvscale);
		TempVertexBuffer[i + j].setV(v->pos.y*uvscale);
		TempVertexBuffer[i + j].setColor(color.red, color.green, color.blue, color.alpha);
	}

	TempBufferVerticesStored += 3;
	TempIndexBuffer[TempBufferIndicesStored++] = i+0;
	TempIndexBuffer[TempBufferIndicesStored++] = i+1;
	TempIndexBuffer[TempBufferIndicesStored++] = i+2;
}

void
RenderWater(void)
{
	int i;

	rw::SetRenderStatePtr(rw::TEXTURERASTER, gpWaterTex->raster);
	rw::SetRenderState(rw::VERTEXALPHA, 1);
	rw::SetRenderState(rw::FOGENABLE, gEnableFog);

	for(i = 0; i < numWaterQuads; i++)
		RenderWaterQuad(&waterQuads[i]);
	for(i = 0; i < numWaterTris; i++)
		RenderWaterTri(&waterTris[i]);
	RenderAndEmptyRenderBuffer();
}

void
Render(void)
{
	SetRenderState(rw::CULLMODE, rw::CULLNONE);
	if(params.water == GAME_SA)
		RenderWater();
	else
		RenderWaterpro();
}

//
// ===== Water Editor =====
//

// Selection state
struct WaterPolyRef { int type; int index; };	// type: 0=quad, 1=tri
static WaterPolyRef waterPolySelection[NUMWATERQUADS + NUMWATERTRIS];
static int numWaterPolySelected;
static int waterVertSelection[NUMWATERVERTICES];
static int numWaterVertSelected;
static int hoveredWaterPoly = INT_MIN;	// encoded: >=0 quad, <0 -(tri+1), INT_MIN = none
static int hoveredWaterVert = -1;

// Weld state (built on drag start)
static int weldMoveList[NUMWATERVERTICES];
static int numWeldMoveList;
static bool weldBuilt;

static bool
IsWaterPolySelected(int type, int index)
{
	for(int i = 0; i < numWaterPolySelected; i++)
		if(waterPolySelection[i].type == type && waterPolySelection[i].index == index)
			return true;
	return false;
}

void
SelectWaterPoly(int type, int index)
{
	if(IsWaterPolySelected(type, index))
		return;
	if(numWaterPolySelected >= NUMWATERQUADS + NUMWATERTRIS)
		return;
	waterPolySelection[numWaterPolySelected].type = type;
	waterPolySelection[numWaterPolySelected].index = index;
	numWaterPolySelected++;
}

static void
DeselectWaterPoly(int type, int index)
{
	for(int i = 0; i < numWaterPolySelected; i++)
		if(waterPolySelection[i].type == type && waterPolySelection[i].index == index){
			waterPolySelection[i] = waterPolySelection[--numWaterPolySelected];
			return;
		}
}

static void
ToggleWaterPoly(int type, int index)
{
	if(IsWaterPolySelected(type, index))
		DeselectWaterPoly(type, index);
	else
		SelectWaterPoly(type, index);
}

void
ClearWaterPolySelection(void)
{
	numWaterPolySelected = 0;
}

static bool
IsWaterVertexSelected(int index)
{
	for(int i = 0; i < numWaterVertSelected; i++)
		if(waterVertSelection[i] == index)
			return true;
	return false;
}

static void
SelectWaterVertex(int index)
{
	if(IsWaterVertexSelected(index))
		return;
	if(numWaterVertSelected >= NUMWATERVERTICES)
		return;
	waterVertSelection[numWaterVertSelected++] = index;
}

static void
DeselectWaterVertex(int index)
{
	for(int i = 0; i < numWaterVertSelected; i++)
		if(waterVertSelection[i] == index){
			waterVertSelection[i] = waterVertSelection[--numWaterVertSelected];
			return;
		}
}

static void
ToggleWaterVertex(int index)
{
	if(IsWaterVertexSelected(index))
		DeselectWaterVertex(index);
	else
		SelectWaterVertex(index);
}

void
ClearWaterVertexSelection(void)
{
	numWaterVertSelected = 0;
}

void
ClearWaterSelection(void)
{
	ClearWaterPolySelection();
	ClearWaterVertexSelection();
	hoveredWaterPoly = INT_MIN;
	hoveredWaterVert = -1;
}

// Weld helper for property panel edits: after a vertex is moved via the panel,
// move all other vertices that were at the old position to match the new one.
void
WeldCoincidentVertices(int vertexIndex, rw::V3d oldPos)
{
	const float eps = 0.01f;
	rw::V3d newPos = waterVertices[vertexIndex].pos;
	for(int i = 0; i < numWaterVertices; i++){
		if(i == vertexIndex) continue;
		if(fabs(waterVertices[i].pos.x - oldPos.x) < eps &&
		   fabs(waterVertices[i].pos.y - oldPos.y) < eps &&
		   fabs(waterVertices[i].pos.z - oldPos.z) < eps){
			waterVertices[i].pos = newPos;
		}
	}
}

// Selection queries for gui
int GetNumSelectedPolys(void) { return numWaterPolySelected; }
int GetNumSelectedVertices(void) { return numWaterVertSelected; }
int GetSelectedPolyType(int sel) { return waterPolySelection[sel].type; }
int GetSelectedPolyIndex(int sel) { return waterPolySelection[sel].index; }
int GetSelectedVertexIndex(int sel) { return waterVertSelection[sel]; }

//
// Undo/Redo
//

#define WATER_MAX_UNDO 32

struct WaterSnapshot {
	WaterVertex *vertices;
	int numVertices;
	WaterQuad *quads;
	int numQuads;
	WaterTri *tris;
	int numTris;
};

static WaterSnapshot *undoStack[WATER_MAX_UNDO];
static int undoCount;
static WaterSnapshot *redoStack[WATER_MAX_UNDO];
static int redoCount;

static WaterSnapshot *
CaptureWaterState(void)
{
	WaterSnapshot *s = (WaterSnapshot *)malloc(sizeof(WaterSnapshot));
	s->numVertices = numWaterVertices;
	s->numQuads = numWaterQuads;
	s->numTris = numWaterTris;
	s->vertices = (WaterVertex *)malloc(numWaterVertices * sizeof(WaterVertex));
	memcpy(s->vertices, waterVertices, numWaterVertices * sizeof(WaterVertex));
	s->quads = (WaterQuad *)malloc(numWaterQuads * sizeof(WaterQuad));
	memcpy(s->quads, waterQuads, numWaterQuads * sizeof(WaterQuad));
	s->tris = (WaterTri *)malloc(numWaterTris * sizeof(WaterTri));
	memcpy(s->tris, waterTris, numWaterTris * sizeof(WaterTri));
	return s;
}

static void
FreeSnapshot(WaterSnapshot *s)
{
	if(s == nil) return;
	free(s->vertices);
	free(s->quads);
	free(s->tris);
	free(s);
}

static void
RestoreWaterState(WaterSnapshot *s)
{
	numWaterVertices = s->numVertices;
	numWaterQuads = s->numQuads;
	numWaterTris = s->numTris;
	memcpy(waterVertices, s->vertices, s->numVertices * sizeof(WaterVertex));
	memcpy(waterQuads, s->quads, s->numQuads * sizeof(WaterQuad));
	memcpy(waterTris, s->tris, s->numTris * sizeof(WaterTri));
}

void
WaterUndoPush(void)
{
	if(undoCount >= WATER_MAX_UNDO){
		FreeSnapshot(undoStack[0]);
		memmove(&undoStack[0], &undoStack[1], (WATER_MAX_UNDO - 1) * sizeof(WaterSnapshot *));
		undoCount--;
	}
	undoStack[undoCount++] = CaptureWaterState();
	for(int i = 0; i < redoCount; i++)
		FreeSnapshot(redoStack[i]);
	redoCount = 0;
}

void
WaterUndo(void)
{
	if(undoCount <= 0) return;
	CancelCreateMode();
	if(redoCount >= WATER_MAX_UNDO){
		FreeSnapshot(redoStack[0]);
		memmove(&redoStack[0], &redoStack[1], (WATER_MAX_UNDO - 1) * sizeof(WaterSnapshot *));
		redoCount--;
	}
	redoStack[redoCount++] = CaptureWaterState();
	WaterSnapshot *s = undoStack[--undoCount];
	RestoreWaterState(s);
	FreeSnapshot(s);
	ClearWaterSelection();
	gWaterDirty = true;
}

void
WaterRedo(void)
{
	if(redoCount <= 0) return;
	CancelCreateMode();
	if(undoCount >= WATER_MAX_UNDO){
		FreeSnapshot(undoStack[0]);
		memmove(&undoStack[0], &undoStack[1], (WATER_MAX_UNDO - 1) * sizeof(WaterSnapshot *));
		undoCount--;
	}
	undoStack[undoCount++] = CaptureWaterState();
	WaterSnapshot *s = redoStack[--redoCount];
	RestoreWaterState(s);
	FreeSnapshot(s);
	ClearWaterSelection();
	gWaterDirty = true;
}

bool WaterCanUndo(void) { return undoCount > 0; }
bool WaterCanRedo(void) { return redoCount > 0; }

//
// Delete / Duplicate
//

void
DeleteSelectedWaterPolys(void)
{
	if(numWaterPolySelected == 0) return;
	WaterUndoPush();

	static bool quadDel[NUMWATERQUADS];
	static bool triDel[NUMWATERTRIS];
	memset(quadDel, 0, sizeof(bool) * numWaterQuads);
	memset(triDel, 0, sizeof(bool) * numWaterTris);
	for(int s = 0; s < numWaterPolySelected; s++){
		if(waterPolySelection[s].type == 0)
			quadDel[waterPolySelection[s].index] = true;
		else
			triDel[waterPolySelection[s].index] = true;
	}

	static bool keepVert[NUMWATERVERTICES];
	memset(keepVert, 1, sizeof(bool) * numWaterVertices);
	for(int i = 0; i < numWaterQuads; i++)
		if(quadDel[i])
			for(int j = 0; j < 4; j++)
				keepVert[waterQuads[i].indices[j]] = false;
	for(int i = 0; i < numWaterTris; i++)
		if(triDel[i])
			for(int j = 0; j < 3; j++)
				keepVert[waterTris[i].indices[j]] = false;

	static int vertRemap[NUMWATERVERTICES];
	int newVertCount = 0;
	for(int i = 0; i < numWaterVertices; i++){
		if(keepVert[i]){
			vertRemap[i] = newVertCount;
			waterVertices[newVertCount] = waterVertices[i];
			newVertCount++;
		}else
			vertRemap[i] = -1;
	}
	numWaterVertices = newVertCount;

	int newQuadCount = 0;
	for(int i = 0; i < numWaterQuads; i++){
		if(!quadDel[i]){
			WaterQuad q = waterQuads[i];
			for(int j = 0; j < 4; j++)
				q.indices[j] = vertRemap[q.indices[j]];
			waterQuads[newQuadCount++] = q;
		}
	}
	numWaterQuads = newQuadCount;

	int newTriCount = 0;
	for(int i = 0; i < numWaterTris; i++){
		if(!triDel[i]){
			WaterTri t = waterTris[i];
			for(int j = 0; j < 3; j++)
				t.indices[j] = vertRemap[t.indices[j]];
			waterTris[newTriCount++] = t;
		}
	}
	numWaterTris = newTriCount;

	ClearWaterSelection();
	gWaterDirty = true;
}

void
DuplicateSelectedWaterPolys(void)
{
	if(numWaterPolySelected == 0) return;
	WaterUndoPush();

	rw::V3d offset = { 20.0f, 20.0f, 0.0f };
	int origCount = numWaterPolySelected;
	static WaterPolyRef toDup[NUMWATERQUADS + NUMWATERTRIS];
	memcpy(toDup, waterPolySelection, origCount * sizeof(WaterPolyRef));
	ClearWaterPolySelection();

	for(int s = 0; s < origCount; s++){
		int pt = toDup[s].type;
		int pi = toDup[s].index;
		if(pt == 0){
			if(numWaterVertices + 4 > NUMWATERVERTICES || numWaterQuads + 1 > NUMWATERQUADS)
				break;
			WaterQuad *src = &waterQuads[pi];
			WaterQuad *dst = &waterQuads[numWaterQuads];
			dst->flags = src->flags;
			for(int j = 0; j < 4; j++){
				int vi = numWaterVertices++;
				dst->indices[j] = vi;
				waterVertices[vi] = waterVertices[src->indices[j]];
				waterVertices[vi].pos = add(waterVertices[vi].pos, offset);
			}
			SelectWaterPoly(0, numWaterQuads++);
		}else{
			if(numWaterVertices + 3 > NUMWATERVERTICES || numWaterTris + 1 > NUMWATERTRIS)
				break;
			WaterTri *src = &waterTris[pi];
			WaterTri *dst = &waterTris[numWaterTris];
			dst->flags = src->flags;
			for(int j = 0; j < 3; j++){
				int vi = numWaterVertices++;
				dst->indices[j] = vi;
				waterVertices[vi] = waterVertices[src->indices[j]];
				waterVertices[vi].pos = add(waterVertices[vi].pos, offset);
			}
			SelectWaterPoly(1, numWaterTris++);
		}
	}
	gWaterDirty = true;
}

//
// Reload
//

void
ReloadWater(void)
{
	WaterUndoPush();
	// Refresh modloader so newly saved exports are visible
	ModloaderInit();
	numWaterVertices = 0;
	numWaterQuads = 0;
	numWaterTris = 0;
	InitialiseWater();
	ClearWaterSelection();
	CancelCreateMode();
	gWaterDirty = false;
	log("ReloadWater: reloaded %d quads, %d tris\n", numWaterQuads, numWaterTris);
}

//
// Snap helper
//

static rw::V3d
SnapToGrid(rw::V3d pos)
{
	if(!gWaterSnapEnabled) return pos;
	float g = gWaterSnapSize;
	pos.x = floorf(pos.x / g + 0.5f) * g;
	pos.y = floorf(pos.y / g + 0.5f) * g;
	return pos;
}

//
// Picking
//

int
PickWaterPoly(Ray ray)
{
	float bestT = 1.0e30f;
	int bestPoly = INT_MIN;

	// Test quads (2 tris each, matching render winding 0,1,2 and 2,1,3)
	for(int i = 0; i < numWaterQuads; i++){
		WaterQuad *q = &waterQuads[i];
		rw::V3d v0 = waterVertices[q->indices[0]].pos;
		rw::V3d v1 = waterVertices[q->indices[1]].pos;
		rw::V3d v2 = waterVertices[q->indices[2]].pos;
		rw::V3d v3 = waterVertices[q->indices[3]].pos;
		float t;
		if(IntersectRayTriangle(ray, v0, v1, v2, &t) && t > 0.0f && t < bestT){
			bestT = t;
			bestPoly = i;
		}
		if(IntersectRayTriangle(ray, v2, v1, v3, &t) && t > 0.0f && t < bestT){
			bestT = t;
			bestPoly = i;
		}
	}

	// Test triangles
	for(int i = 0; i < numWaterTris; i++){
		WaterTri *tr = &waterTris[i];
		rw::V3d v0 = waterVertices[tr->indices[0]].pos;
		rw::V3d v1 = waterVertices[tr->indices[1]].pos;
		rw::V3d v2 = waterVertices[tr->indices[2]].pos;
		float t;
		if(IntersectRayTriangle(ray, v0, v1, v2, &t) && t > 0.0f && t < bestT){
			bestT = t;
			bestPoly = -(i + 1);
		}
	}
	return bestPoly;
}

static int
PickWaterVertex(Ray ray)
{
	float bestT = 1.0e30f;
	int bestVert = -1;

	for(int i = 0; i < numWaterVertices; i++){
		rw::V3d vpos = waterVertices[i].pos;
		float dist = length(sub(vpos, ray.start));
		float radius = dist * 0.015f;
		if(radius < 2.0f) radius = 2.0f;

		CSphere sph;
		sph.center = vpos;
		sph.radius = radius;
		float t;
		if(IntersectRaySphere(ray, sph, &t) && t > 0.0f && t < bestT){
			bestT = t;
			bestVert = i;
		}
	}
	return bestVert;
}

static Ray
BuildMouseRay(void)
{
	Ray ray;
	ray.start = TheCamera.m_position;
	ray.dir = normalize(TheCamera.m_mouseDir);
	return ray;
}

//
// Creation
//

static rw::V3d createCorners[3];	// placed corners (up to 3 for triangle)
static int createCornersPlaced;		// how many corners placed so far
static rw::V3d createMouseWorld;	// current mouse world pos on Z plane
static bool createMouseValid;		// is mouse hitting the Z plane?

// Intersect ray with horizontal plane at z=h. Returns false if ray is parallel.
static bool
RayPlaneZ(Ray ray, float h, rw::V3d *hit)
{
	if(fabs(ray.dir.z) < 0.0001f)
		return false;
	float t = (h - ray.start.z) / ray.dir.z;
	if(t < 0.0f)
		return false;
	hit->x = ray.start.x + t * ray.dir.x;
	hit->y = ray.start.y + t * ray.dir.y;
	hit->z = h;
	return true;
}

// Snap a position to the nearest existing water vertex within radius.
// Returns the snapped position.
static rw::V3d
SnapToNearbyVertex(rw::V3d pos, float snapDist)
{
	float bestDist = snapDist;
	rw::V3d bestPos = pos;
	for(int i = 0; i < numWaterVertices; i++){
		rw::V3d d = sub(waterVertices[i].pos, pos);
		float dist = length(d);
		if(dist < bestDist){
			bestDist = dist;
			bestPos = waterVertices[i].pos;
		}
	}
	return bestPos;
}

// Create a new axis-aligned water quad from two opposite corners.
// Vertices are snapped to nearby existing vertices.
// Returns the quad index, or -1 on failure.
static int
CreateWaterQuad(rw::V3d a, rw::V3d b)
{
	if(numWaterVertices + 4 > NUMWATERVERTICES){
		log("CreateWaterQuad: vertex limit reached\n");
		return -1;
	}
	if(numWaterQuads + 1 > NUMWATERQUADS){
		log("CreateWaterQuad: quad limit reached\n");
		return -1;
	}

	// Build 4 corners (axis-aligned): SW, SE, NW, NE order
	// a = one corner, b = opposite corner
	float minX = a.x < b.x ? a.x : b.x;
	float maxX = a.x > b.x ? a.x : b.x;
	float minY = a.y < b.y ? a.y : b.y;
	float maxY = a.y > b.y ? a.y : b.y;
	float z = gWaterCreateZ;

	rw::V3d corners[4];
	corners[0] = SnapToGrid({ minX, minY, z });
	corners[1] = SnapToGrid({ maxX, minY, z });
	corners[2] = SnapToGrid({ minX, maxY, z });
	corners[3] = SnapToGrid({ maxX, maxY, z });

	const float snapDist = 2.0f;
	for(int i = 0; i < 4; i++)
		corners[i] = SnapToNearbyVertex(corners[i], snapDist);

	WaterQuad *q = &waterQuads[numWaterQuads];
	for(int i = 0; i < 4; i++){
		int vi = numWaterVertices++;
		q->indices[i] = vi;
		waterVertices[vi].pos = corners[i];
		waterVertices[vi].speed = { 0.0f, 0.0f };
		waterVertices[vi].waveunk = 0.0f;
		waterVertices[vi].waveheight = 0.0f;
	}
	q->flags = 1;
	return numWaterQuads++;
}

static int
CreateWaterTriangle(rw::V3d a, rw::V3d b, rw::V3d c)
{
	if(numWaterVertices + 3 > NUMWATERVERTICES){
		log("CreateWaterTriangle: vertex limit reached\n");
		return -1;
	}
	if(numWaterTris + 1 > NUMWATERTRIS){
		log("CreateWaterTriangle: tri limit reached\n");
		return -1;
	}

	rw::V3d corners[3] = { SnapToGrid(a), SnapToGrid(b), SnapToGrid(c) };
	const float snapDist = 2.0f;
	for(int i = 0; i < 3; i++)
		corners[i] = SnapToNearbyVertex(corners[i], snapDist);

	WaterTri *t = &waterTris[numWaterTris];
	for(int i = 0; i < 3; i++){
		int vi = numWaterVertices++;
		t->indices[i] = vi;
		waterVertices[vi].pos = corners[i];
		waterVertices[vi].speed = { 0.0f, 0.0f };
		waterVertices[vi].waveunk = 0.0f;
		waterVertices[vi].waveheight = 0.0f;
	}
	t->flags = 1;
	return numWaterTris++;
}

void
EnterCreateMode(void)
{
	// Default creation Z: use selected polygon's average Z, or hovered, or 0
	float z = 0.0f;
	if(numWaterPolySelected > 0){
		int pt = waterPolySelection[0].type;
		int pi = waterPolySelection[0].index;
		int n = pt == 0 ? 4 : 3;
		int *idx = pt == 0 ? waterQuads[pi].indices : waterTris[pi].indices;
		float sum = 0.0f;
		for(int j = 0; j < n; j++)
			sum += waterVertices[idx[j]].pos.z;
		z = sum / (float)n;
	}else if(hoveredWaterPoly != INT_MIN){
		int n; int *idx;
		if(hoveredWaterPoly >= 0){
			n = 4; idx = waterQuads[hoveredWaterPoly].indices;
		}else{
			int ti = -(hoveredWaterPoly + 1);
			n = 3; idx = waterTris[ti].indices;
		}
		float sum = 0.0f;
		for(int j = 0; j < n; j++)
			sum += waterVertices[idx[j]].pos.z;
		z = sum / (float)n;
	}
	gWaterCreateZ = z;
	gWaterCreateMode = 1;
	createCornersPlaced = 0;
	ClearWaterPolySelection();
	gWaterSubMode = 0;	// switch to polygon mode
}

void
CancelCreateMode(void)
{
	gWaterCreateMode = 0;
}

//
// HandleWaterTool
//

void
HandleWaterTool(void)
{
	ImGuiIO &io = ImGui::GetIO();
	if(io.WantCaptureMouse)
		return;

	Ray ray = BuildMouseRay();

	// Update mouse world position on creation Z plane (used for preview)
	createMouseValid = RayPlaneZ(ray, gWaterCreateZ, &createMouseWorld);

	// Apply grid snap to mouse position
	if(createMouseValid)
		createMouseWorld = SnapToGrid(createMouseWorld);

	// Creation mode
	if(gWaterCreateMode > 0){
		// Right click or ESC cancels (ESC handled in gui.cpp)
		if(CPad::IsMButtonClicked(2)){
			CancelCreateMode();
			return;
		}

		if(CPad::IsMButtonClicked(1) && createMouseValid){
			createCorners[createCornersPlaced++] = createMouseWorld;

			int neededCorners = gWaterCreateShape == 0 ? 2 : 3;
			if(createCornersPlaced >= neededCorners){
				// Commit the polygon
				WaterUndoPush();
				int newIdx = -1;
				if(gWaterCreateShape == 0){
					// Quad: require minimum size
					float dx = fabs(createCorners[1].x - createCorners[0].x);
					float dy = fabs(createCorners[1].y - createCorners[0].y);
					if(dx >= 4.0f && dy >= 4.0f)
						newIdx = CreateWaterQuad(createCorners[0], createCorners[1]);
				}else{
					// Triangle
					newIdx = CreateWaterTriangle(createCorners[0], createCorners[1], createCorners[2]);
				}

				if(newIdx >= 0){
					gWaterDirty = true;
					ClearWaterPolySelection();
					SelectWaterPoly(gWaterCreateShape == 0 ? 0 : 1, newIdx);
				}

				// Shift = keep creating, else exit
				if(CPad::IsShiftDown()){
					createCornersPlaced = 0;
				}else
					gWaterCreateMode = 0;
			}
		}
		return;
	}

	// Normal selection mode
	// Update hover state every frame
	if(gWaterSubMode == 0){
		hoveredWaterPoly = PickWaterPoly(ray);
		hoveredWaterVert = -1;
	}else{
		hoveredWaterVert = PickWaterVertex(ray);
		hoveredWaterPoly = INT_MIN;
	}

	// Left click: select
	if(CPad::IsMButtonClicked(1)){
		if(gWaterSubMode == 0){
			// Polygon mode
			int hit = PickWaterPoly(ray);
			int hitType, hitIndex;
			if(hit != INT_MIN){
				if(hit >= 0){ hitType = 0; hitIndex = hit; }
				else { hitType = 1; hitIndex = -(hit + 1); }

				if(CPad::IsShiftDown())
					SelectWaterPoly(hitType, hitIndex);
				else if(CPad::IsAltDown())
					DeselectWaterPoly(hitType, hitIndex);
				else if(CPad::IsCtrlDown())
					ToggleWaterPoly(hitType, hitIndex);
				else{
					ClearWaterPolySelection();
					SelectWaterPoly(hitType, hitIndex);
				}
			}else
				ClearWaterPolySelection();
		}else{
			// Vertex mode
			int hit = PickWaterVertex(ray);
			if(hit >= 0){
				if(CPad::IsShiftDown())
					SelectWaterVertex(hit);
				else if(CPad::IsAltDown())
					DeselectWaterVertex(hit);
				else if(CPad::IsCtrlDown())
					ToggleWaterVertex(hit);
				else{
					ClearWaterVertexSelection();
					SelectWaterVertex(hit);
				}
			}else
				ClearWaterVertexSelection();
		}
	}

	// Middle click: clear
	if(CPad::IsMButtonClicked(3)){
		ClearWaterPolySelection();
		ClearWaterVertexSelection();
	}
}

//
// Visual Overlay
//

static rw::RGBA waterWhite = { 255, 255, 255, 255 };
static rw::RGBA waterCyan = { 0, 200, 255, 255 };
static rw::RGBA waterDimRed = { 180, 60, 60, 180 };
static rw::RGBA waterYellow = { 255, 220, 0, 255 };
static rw::RGBA waterVertWhite = { 220, 220, 220, 255 };

static rw::RGBA
GetPolyWireColor(int flags)
{
	if(!(flags & 1))
		return waterDimRed;	// invisible
	if(flags & 2)
		return waterCyan;	// visible + limited depth
	return waterWhite;		// visible + normal
}

static void
RenderWaterPolyWireQuad(WaterQuad *q, rw::RGBA col)
{
	rw::V3d v0 = waterVertices[q->indices[0]].pos;
	rw::V3d v1 = waterVertices[q->indices[1]].pos;
	rw::V3d v2 = waterVertices[q->indices[2]].pos;
	rw::V3d v3 = waterVertices[q->indices[3]].pos;
	RenderLine(v0, v1, col, col);
	RenderLine(v1, v3, col, col);
	RenderLine(v3, v2, col, col);
	RenderLine(v2, v0, col, col);
}

static void
RenderWaterPolyWireTri(WaterTri *t, rw::RGBA col)
{
	rw::V3d v0 = waterVertices[t->indices[0]].pos;
	rw::V3d v1 = waterVertices[t->indices[1]].pos;
	rw::V3d v2 = waterVertices[t->indices[2]].pos;
	RenderWireTriangle(&v0, &v1, &v2, col, nil);
}

static void
RenderHighlightFillQuad(WaterQuad *q, rw::RGBA col)
{
	if(TempBufferVerticesStored+4 >= TEMPBUFFERVERTSIZE ||
	   TempBufferIndicesStored+6 >= TEMPBUFFERINDEXSIZE)
		RenderAndEmptyRenderBuffer();

	int i = TempBufferVerticesStored;
	for(int j = 0; j < 4; j++){
		rw::V3d p = waterVertices[q->indices[j]].pos;
		TempVertexBuffer[i + j].setX(p.x);
		TempVertexBuffer[i + j].setY(p.y);
		TempVertexBuffer[i + j].setZ(p.z);
		TempVertexBuffer[i + j].setU(0.0f);
		TempVertexBuffer[i + j].setV(0.0f);
		TempVertexBuffer[i + j].setColor(col.red, col.green, col.blue, col.alpha);
	}
	TempBufferVerticesStored += 4;
	TempIndexBuffer[TempBufferIndicesStored++] = i+0;
	TempIndexBuffer[TempBufferIndicesStored++] = i+1;
	TempIndexBuffer[TempBufferIndicesStored++] = i+2;
	TempIndexBuffer[TempBufferIndicesStored++] = i+2;
	TempIndexBuffer[TempBufferIndicesStored++] = i+1;
	TempIndexBuffer[TempBufferIndicesStored++] = i+3;
}

static void
RenderHighlightFillTri(WaterTri *t, rw::RGBA col)
{
	if(TempBufferVerticesStored+3 >= TEMPBUFFERVERTSIZE ||
	   TempBufferIndicesStored+3 >= TEMPBUFFERINDEXSIZE)
		RenderAndEmptyRenderBuffer();

	int i = TempBufferVerticesStored;
	for(int j = 0; j < 3; j++){
		rw::V3d p = waterVertices[t->indices[j]].pos;
		TempVertexBuffer[i + j].setX(p.x);
		TempVertexBuffer[i + j].setY(p.y);
		TempVertexBuffer[i + j].setZ(p.z);
		TempVertexBuffer[i + j].setU(0.0f);
		TempVertexBuffer[i + j].setV(0.0f);
		TempVertexBuffer[i + j].setColor(col.red, col.green, col.blue, col.alpha);
	}
	TempBufferVerticesStored += 3;
	TempIndexBuffer[TempBufferIndicesStored++] = i+0;
	TempIndexBuffer[TempBufferIndicesStored++] = i+1;
	TempIndexBuffer[TempBufferIndicesStored++] = i+2;
}

void
RenderEditOverlay(void)
{
	rw::SetRenderState(rw::CULLMODE, rw::CULLNONE);

	// All polygon wireframes
	for(int i = 0; i < numWaterQuads; i++){
		rw::RGBA col = GetPolyWireColor(waterQuads[i].flags);
		RenderWaterPolyWireQuad(&waterQuads[i], col);
	}
	for(int i = 0; i < numWaterTris; i++){
		rw::RGBA col = GetPolyWireColor(waterTris[i].flags);
		RenderWaterPolyWireTri(&waterTris[i], col);
	}

	// Hovered polygon
	if(hoveredWaterPoly != INT_MIN){
		if(hoveredWaterPoly >= 0)
			RenderWaterPolyWireQuad(&waterQuads[hoveredWaterPoly], waterCyan);
		else{
			int ti = -(hoveredWaterPoly + 1);
			RenderWaterPolyWireTri(&waterTris[ti], waterCyan);
		}
	}

	// Selected polygons: yellow wireframe + fill
	rw::RGBA fillYellow = { 255, 220, 0, 60 };
	rw::SetRenderStatePtr(rw::TEXTURERASTER, nil);
	rw::SetRenderState(rw::VERTEXALPHA, 1);
	rw::SetRenderState(rw::ZWRITEENABLE, 0);
	for(int i = 0; i < numWaterPolySelected; i++){
		if(waterPolySelection[i].type == 0){
			RenderWaterPolyWireQuad(&waterQuads[waterPolySelection[i].index], waterYellow);
			RenderHighlightFillQuad(&waterQuads[waterPolySelection[i].index], fillYellow);
		}else{
			RenderWaterPolyWireTri(&waterTris[waterPolySelection[i].index], waterYellow);
			RenderHighlightFillTri(&waterTris[waterPolySelection[i].index], fillYellow);
		}
	}
	RenderAndEmptyRenderBuffer();
	rw::SetRenderState(rw::ZWRITEENABLE, 1);

	// Vertex handles in vertex mode
	if(gWaterSubMode == 1){
		for(int i = 0; i < numWaterVertices; i++){
			CSphere sph;
			sph.center = waterVertices[i].pos;
			float dist = length(sub(sph.center, TheCamera.m_position));
			sph.radius = dist * 0.008f;
			if(sph.radius < 1.0f) sph.radius = 1.0f;

			rw::RGBA col = waterVertWhite;
			if(IsWaterVertexSelected(i))
				col = waterYellow;
			if(i == hoveredWaterVert)
				col = waterCyan;
			RenderSphereAsCross(&sph, col, nil);
		}
	}

	// Creation mode preview
	if(gWaterCreateMode > 0 && createMouseValid){
		rw::RGBA createGreen = { 0, 255, 100, 255 };
		rw::RGBA createFillGreen = { 0, 255, 100, 40 };
		rw::RGBA red = { 255, 60, 60, 255 };
		float crossSize = 5.0f;
		rw::V3d m = createMouseWorld;

		// Crosshair at mouse
		RenderLine({ m.x - crossSize, m.y, m.z }, { m.x + crossSize, m.y, m.z }, createGreen, createGreen);
		RenderLine({ m.x, m.y - crossSize, m.z }, { m.x, m.y + crossSize, m.z }, createGreen, createGreen);

		// Placed corners markers
		for(int i = 0; i < createCornersPlaced; i++){
			rw::V3d p = createCorners[i];
			RenderLine({ p.x - crossSize, p.y, p.z }, { p.x + crossSize, p.y, p.z }, waterYellow, waterYellow);
			RenderLine({ p.x, p.y - crossSize, p.z }, { p.x, p.y + crossSize, p.z }, waterYellow, waterYellow);
		}

		// Lines from placed corners to each other and to mouse
		for(int i = 0; i < createCornersPlaced; i++)
			RenderLine(createCorners[i], m, createGreen, createGreen);
		for(int i = 1; i < createCornersPlaced; i++)
			RenderLine(createCorners[i-1], createCorners[i], createGreen, createGreen);

		// Shape-specific preview
		if(gWaterCreateShape == 0 && createCornersPlaced == 1){
			// Quad preview rectangle
			rw::V3d a = createCorners[0];
			float z = gWaterCreateZ;
			rw::V3d c0 = { a.x, a.y, z };
			rw::V3d c1 = { m.x, a.y, z };
			rw::V3d c2 = { a.x, m.y, z };
			rw::V3d c3 = { m.x, m.y, z };
			RenderLine(c0, c1, createGreen, createGreen);
			RenderLine(c1, c3, createGreen, createGreen);
			RenderLine(c3, c2, createGreen, createGreen);
			RenderLine(c2, c0, createGreen, createGreen);

			// Red crosshair if too small
			float dx = fabs(m.x - a.x);
			float dy = fabs(m.y - a.y);
			if(dx < 4.0f || dy < 4.0f){
				RenderLine({ m.x - crossSize, m.y, m.z }, { m.x + crossSize, m.y, m.z }, red, red);
				RenderLine({ m.x, m.y - crossSize, m.z }, { m.x, m.y + crossSize, m.z }, red, red);
			}
		}else if(gWaterCreateShape == 1 && createCornersPlaced == 2){
			// Triangle preview: close the triangle
			RenderLine(createCorners[0], createCorners[1], createGreen, createGreen);
			RenderLine(createCorners[1], m, createGreen, createGreen);
			RenderLine(m, createCorners[0], createGreen, createGreen);
		}
	}
}

//
// Gizmo
//

static void
BuildWeldMoveList(void)
{
	// Collect all vertex indices to move
	static bool inSet[NUMWATERVERTICES];
	memset(inSet, 0, sizeof(bool) * numWaterVertices);
	numWeldMoveList = 0;

	// Step 1: Collect direct seed indices
	int seedCount = 0;
	static int seedIndices[NUMWATERVERTICES];
	if(gWaterSubMode == 0){
		// Polygon mode: all vertices of selected polygons
		for(int i = 0; i < numWaterPolySelected; i++){
			if(waterPolySelection[i].type == 0){
				WaterQuad *q = &waterQuads[waterPolySelection[i].index];
				for(int j = 0; j < 4; j++)
					if(!inSet[q->indices[j]]){
						inSet[q->indices[j]] = true;
						seedIndices[seedCount++] = q->indices[j];
					}
			}else{
				WaterTri *t = &waterTris[waterPolySelection[i].index];
				for(int j = 0; j < 3; j++)
					if(!inSet[t->indices[j]]){
						inSet[t->indices[j]] = true;
						seedIndices[seedCount++] = t->indices[j];
					}
			}
		}
	}else{
		// Vertex mode: selected vertices
		for(int i = 0; i < numWaterVertSelected; i++)
			if(!inSet[waterVertSelection[i]]){
				inSet[waterVertSelection[i]] = true;
				seedIndices[seedCount++] = waterVertSelection[i];
			}
	}

	// Step 2: Dedup seed positions, then expand to coincident vertices
	const float eps = 0.01f;
	for(int s = 0; s < seedCount; s++){
		rw::V3d spos = waterVertices[seedIndices[s]].pos;
		// Find all vertices at this position
		for(int v = 0; v < numWaterVertices; v++){
			if(inSet[v]) continue;
			rw::V3d vpos = waterVertices[v].pos;
			if(fabs(vpos.x - spos.x) < eps &&
			   fabs(vpos.y - spos.y) < eps &&
			   fabs(vpos.z - spos.z) < eps){
				inSet[v] = true;
			}
		}
	}

	// Step 3: Flatten into move list
	for(int i = 0; i < numWaterVertices; i++)
		if(inSet[i])
			weldMoveList[numWeldMoveList++] = i;

	weldBuilt = true;
}

void
DoWaterGizmo(void)
{
	gGizmoHovered = false;
	gGizmoUsing = false;

	bool hasSel = (gWaterSubMode == 0 && numWaterPolySelected > 0) ||
	              (gWaterSubMode == 1 && numWaterVertSelected > 0);
	if(!hasSel)
		return;

	// Compute centroid of affected vertices
	rw::V3d centroid = { 0.0f, 0.0f, 0.0f };
	int count = 0;
	if(gWaterSubMode == 0){
		for(int i = 0; i < numWaterPolySelected; i++){
			int n;
			int *idx;
			if(waterPolySelection[i].type == 0){
				n = 4;
				idx = waterQuads[waterPolySelection[i].index].indices;
			}else{
				n = 3;
				idx = waterTris[waterPolySelection[i].index].indices;
			}
			for(int j = 0; j < n; j++){
				centroid = add(centroid, waterVertices[idx[j]].pos);
				count++;
			}
		}
	}else{
		for(int i = 0; i < numWaterVertSelected; i++){
			centroid = add(centroid, waterVertices[waterVertSelection[i]].pos);
			count++;
		}
	}
	if(count == 0) return;
	centroid = scale(centroid, 1.0f / (float)count);

	// Build gizmo matrix
	float gizmat[16];
	memset(gizmat, 0, sizeof(gizmat));
	gizmat[0] = 1.0f;
	gizmat[5] = 1.0f;
	gizmat[10] = 1.0f;
	gizmat[15] = 1.0f;
	gizmat[12] = centroid.x;
	gizmat[13] = centroid.y;
	gizmat[14] = centroid.z;

	ImGuiIO &io = ImGui::GetIO();
	rw::Camera *cam = (rw::Camera*)rw::engine->currentCamera;
	float *fview = (float*)&cam->devView;
	float *fproj = (float*)&cam->devProj;

	ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
	ImGuizmo::Manipulate(fview, fproj, ImGuizmo::TRANSLATE, ImGuizmo::LOCAL, gizmat, nil, nil);

	gGizmoHovered = ImGuizmo::IsOver();
	bool isUsing = ImGuizmo::IsUsing();
	gGizmoUsing = isUsing;

	static bool wasDragging = false;
	static rw::V3d dragStartCentroid;

	if(isUsing && !wasDragging){
		// Drag just started
		WaterUndoPush();
		dragStartCentroid = centroid;
		BuildWeldMoveList();
		wasDragging = true;
	}

	if(isUsing && weldBuilt){
		// Apply delta
		rw::V3d newCentroid = { gizmat[12], gizmat[13], gizmat[14] };
		rw::V3d delta = sub(newCentroid, centroid);
		for(int i = 0; i < numWeldMoveList; i++)
			waterVertices[weldMoveList[i]].pos = add(waterVertices[weldMoveList[i]].pos, delta);
		gWaterDirty = true;
	}

	if(!isUsing && wasDragging){
		wasDragging = false;
		weldBuilt = false;
	}
}

//
// Save
//

static bool
ReplacePathWater(const char *src, const char *dst)
{
#ifdef _WIN32
	return MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH) != 0;
#else
	return rename(src, dst) == 0;
#endif
}

bool
SaveWater(void)
{
	char finalPath[1024];
	const char *logicalPath = "data/water.dat";

	if(gSaveDestination == SAVE_DESTINATION_MODLOADER){
		if(!BuildModloaderLogicalExportPath(logicalPath, finalPath, sizeof(finalPath))){
			log("SaveWater: failed to build modloader export path\n");
			return false;
		}
	}else{
		// Original files: write to the base game file
		strncpy(finalPath, logicalPath, sizeof(finalPath));
		finalPath[sizeof(finalPath)-1] = '\0';
	}

	if(!EnsureParentDirectoriesForPath(finalPath)){
		log("SaveWater: failed to create directories for %s\n", finalPath);
		return false;
	}

	// Write to temp file
	char tempPath[1024];
	char backupPath[1024];
	snprintf(tempPath, sizeof(tempPath), "%s.ariane.tmp", finalPath);
	snprintf(backupPath, sizeof(backupPath), "%s.ariane.bak", finalPath);

	FILE *f = fopen(tempPath, "w");
	if(f == nil){
		log("SaveWater: can't open %s for writing\n", tempPath);
		return false;
	}

	fprintf(f, "processed\n");

	for(int i = 0; i < numWaterQuads; i++){
		WaterQuad *q = &waterQuads[i];
		for(int j = 0; j < 4; j++){
			WaterVertex *v = &waterVertices[q->indices[j]];
			fprintf(f, "%.5f %.5f %.5f %.5f %.5f %.5f %.5f ",
				v->pos.x, v->pos.y, v->pos.z,
				v->speed.x, v->speed.y,
				v->waveunk, v->waveheight);
		}
		fprintf(f, "%d\n", q->flags);
	}

	for(int i = 0; i < numWaterTris; i++){
		WaterTri *t = &waterTris[i];
		for(int j = 0; j < 3; j++){
			WaterVertex *v = &waterVertices[t->indices[j]];
			fprintf(f, "%.5f %.5f %.5f %.5f %.5f %.5f %.5f ",
				v->pos.x, v->pos.y, v->pos.z,
				v->speed.x, v->speed.y,
				v->waveunk, v->waveheight);
		}
		fprintf(f, "%d\n", t->flags);
	}

	fclose(f);

	// Promote: backup existing, then rename temp to final
	FILE *existing = fopen(finalPath, "rb");
	if(existing){
		fclose(existing);
		if(!ReplacePathWater(finalPath, backupPath)){
			log("SaveWater: can't backup %s to %s\n", finalPath, backupPath);
			remove(tempPath);
			return false;
		}
	}

	if(!ReplacePathWater(tempPath, finalPath)){
		log("SaveWater: can't promote %s to %s\n", tempPath, finalPath);
		// Rollback
		if(existing)
			ReplacePathWater(backupPath, finalPath);
		remove(tempPath);
		return false;
	}

	// Clean up backup
	if(existing)
		remove(backupPath);

	gWaterDirty = false;
	log("SaveWater: saved %d quads, %d tris to %s\n", numWaterQuads, numWaterTris, finalPath);
	return true;
}

}