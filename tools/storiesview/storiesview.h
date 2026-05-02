#include <rw.h>
#include <skeleton.h>
#include <assert.h>

#include <rwgta.h>
#define PS2

using namespace std;
using rw::int8;
using rw::uint8;
using rw::int16;
using rw::uint16;
using rw::int32;
using rw::uint32;
using rw::float32;
using rw::bool32;

typedef uint16 float16;
#include <rsl.h>

#include "templates.h"

#include <relocchunk.h>

#include <leedsgta.h>
#include <streamworld.h>

//#define EDIT_LINKS
#define LODNAMES

//#define DUMPCOLLISION
//#define DUMPMODELS
//#define DUMPTEXTURES


const char *lookupHashKey(uint32 key);
uint32 GetKey(const char *str, int len);
uint32 GetUppercaseKey(const char *str, int len);
uint32 CalcHashKey(const char *str, int len);
uint32 CalcHashKey(const char *str);


#include "Pad.h"
#include "Camera.h"
#include "TexListStore.h"

void panic(const char *fmt, ...);
void debug(const char *fmt, ...);

void plCapturePad(int arg);
void plUpdatePad(CControllerState *state);

#define DEGTORAD(d) (d/180.0f*PI)
#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif

struct Zfile;
Zfile *zopen(const char *path, const char *mode);
void zclose(Zfile *zf);
int zread(Zfile *zf, void *dst, int n);
int ztell(Zfile *zf);

struct SceneGlobals {
	rw::World *world;
	rw::Camera *camera;
};
extern SceneGlobals Scene;

enum eLevel
{
#ifdef LCS
	LEVEL_INDUST = 1,
	LEVEL_COMMER = 2,
	LEVEL_SUBURB = 3,
	LEVEL_UNDERG = 4,
#else
	LEVEL_BEACH = 1,
	LEVEL_MAINLAND = 2,
	LEVEL_MALL = 3,
#endif
};

extern CCamera TheCamera;
//extern bool drawCubes;
extern bool drawWater;
extern bool drawCol;
extern bool drawBounds;
extern bool drawCurrentSector;
extern bool drawAllInteriors;
extern int drawFlagged;
extern bool drawLOD;
extern bool drawDummies;
extern bool drawWorld;
extern bool drawPathNodes;
extern bool drawUnnamed;
extern bool drawUnmatched;
extern bool drawOnlySwaps;
extern bool ignoreTime;
extern bool ignoreSwapState;
extern int frameCounter;
extern float timeStep;
extern float avgTimeStep;

extern int currentInterior;
extern bool passmask[NUMSECTORLISTS];
extern int swapstate[100];	// dunno how many we should have
extern int currentHour;
extern int currentMinute;
extern int currentWeather;

// helpers
extern rw::Geometry *cubeGeo;
extern rw::Material *cubeMat;
extern rw::Light *pAmbient;

bool GetIsTimeInRange(uint8 h1, uint8 h2);

rw::Raster *convertRasterPS2(RslRasterPS2 *ras);


void LoadCollisionFile(int id, uint8 *data);
int32 WriteCollisionFile(CColModel *colmodel, uint8 **bufp);
void RenderColModelWire(CColModel *col, rw::Matrix *xform, bool onlyBounds);
void RenderColBoxSolid(CColModel *col, rw::Matrix *xform, rw::RGBA c);
void RenderColMeshSolid(CColModel *col, rw::Matrix *xform, rw::RGBA c);

enum SectorType
{
	SECTOR_NA,
	SECTOR_WORLD,
	SECTOR_INTERIOR,
	SECTOR_TRIG,	// only resources
};

struct SectorExt;

// streamed world
struct BuildingExt
{
	int id;	// our own id
	sLevelSwap *swap;

	int32 iplId;
	int32 modelId;

	sGeomInstance *inst;	// a random instance
	SectorExt *sect;	// sector that instance is in

	bool isTransparent;
//	int interior;	// not useful
	bool selected;
	int highlight;
	rw::LLLink inSelection;

	struct Model {
		int lastFrame;
		int resId;
		Model *next;
	};
	Model *resources;
	rw::Matrix matrix;

	Model *GetResourceInfo(int id);

	static rw::LinkList selection;
	static BuildingExt *GetSelection(void);
	void Select(void);
	void Deselect(void);
	void SetEntity(int iplId);
};
struct SectorExt
{
	Sector *sect;
	rw::V3d origin;
	SectorType type;
	int secx, secy;	// world sector indices
	sBuildingSwapInfo *swap;

	int numInstances;
	rw::Atomic **instances;
	rw::Atomic **dummies;
};
struct ResourceExt
{
	SectorExt *sector;
};
struct LevelExt
{
	sLevelChunk *chunk;
	int levelid;
	FILE *imgfile;
	int numWorldSectors;
	int numSectors;
	SectorExt *sectors;
#ifdef VCS
	Area **areas;
#endif
	BuildingExt *buildings[0x8000];
	ResourceExt *res;
};
extern LevelExt *gLevel;
extern SectorExt *worldSectors[NUMSECTORSX][NUMSECTORSY];
void GetSectorForPosition(float x, float y, int *ix, int *iy);
Resource *GetResource(int id);
void LoadLevel(eLevel lev);
void LoadSector(int n, int interior);
void LoadArea(int n);
void LoadSectorInsts(SectorExt *se);
void RenderSector(SectorExt*);
void RenderCubesSector(SectorExt*);
BuildingExt *GetBuildingExt(int id);
BuildingExt *FindBuildingExt(int id);

struct EntityExt
{
	CEntity *entity;

	bool selected;
	int highlight;
	rw::LLLink inSelection;

	// To link world buildings into IPL entities
	int n;
	BuildingExt **insts;

	static rw::LinkList selection;
	static CEntity *GetSelection(void);
	void Select(void);
	void Deselect(void);
	void AddBuilding(BuildingExt *b);
	void RemoveBuilding(BuildingExt *b);
	int GetIplID(void);
	void JumpTo(void);
};

struct ModelInfoExt
{
	// do we even need this anymore?
	CEntity *inst;	// a random instance

	int buildingId;	// a random one, not necessary in this level!
	bool isSwap;	// will be swapped in for something else
};
extern ModelInfoExt *pModelInfoExts;
void InitModelInfoExt(void);
ModelInfoExt *GetModelInfoExt(int id);

typedef CPool<CBuilding, CBuilding> BuildingPool;
typedef CPool<CTreadable, CTreadable> TreadablePool;
typedef CPool<CDummy, CDummy> DummyPool;
extern BuildingPool *pBuildingPool;
extern TreadablePool *pTreadablePool;
extern DummyPool *pDummyPool;
extern CTimeCycle *pTimecycle;
extern rw::RGBA currentAmbient;
extern rw::RGBA currentEmissive;
extern CPathFind *gpThePaths;
extern CPool_col *pColPool;
extern CStreaming *pStreaming;

struct CModelInfo
{
	static int msNumModelInfos;
	static CBaseModelInfo **ms_modelInfoPtrs;
	static void Load(int n, CBaseModelInfo **mi);
	static CBaseModelInfo *Get(int n);
};

struct CWaterLevel_ : CWaterLevel
{
	static CWaterLevel_ *mspInst;
	static void Initialize(CWaterLevel *wl);
	static void RenderAndEmptyRenderBuffer(void);
	void RenderWater(void);
	static void RenderOneFlatSmallWaterPoly(float x, float y, float z, rw::RGBA const &color);
	static void RenderOneFlatLargeWaterPoly(float x, float y, float z, rw::RGBA const &color);
};





enum HighlightStyle
{
	HIGHLIGHT_NONE,
	HIGHLIGHT_FILTER,
	HIGHLIGHT_SELECTION,
	HIGHLIGHT_HOVER,
};

namespace Renderer
{
extern rw::ObjPipeline *buildingPipe;
void renderColModels(void);
void renderDebugIPL(void);
void renderPathNodes(void);
void reset(void);
void addToOpaqueRenderList(sGeomInstance *inst, rw::Atomic *a);
void addToTransparentRenderList(sGeomInstance*inst, rw::Atomic *a);
void renderOpaque(void);
void renderTransparent(void);
void renderEverythingColourCoded(void);

void myRenderCB(rw::Atomic *atomic);

extern rw::ObjPipeline *colourCodePipe;
};

void updatePassMask(void);
void gui(void);

void RenderDebugLines(void);
void RenderDebugTris(void);
void RenderLine(rw::V3d v1, rw::V3d v2, rw::RGBA c1, rw::RGBA c2);
void RenderWireBoxVerts(rw::V3d *verts, rw::RGBA col);
void RenderWireBox(CBox *box, rw::RGBA col, rw::Matrix *xform);
void RenderSolidBox(CBox *box, rw::RGBA col, rw::Matrix *xform);
void RenderWireSphere(CSphere *sphere, rw::RGBA col, rw::Matrix *xform);
void RenderSolidSphere(CSphere *sphere, rw::RGBA col, rw::Matrix *xform);
void RenderWireTriangle(rw::V3d *v1, rw::V3d *v2, rw::V3d *v3, rw::RGBA col, rw::Matrix *xform);
void RenderSolidTriangle(rw::V3d *v1, rw::V3d *v2, rw::V3d *v3, rw::RGBA col, rw::Matrix *xform);
void RenderAxesWidget(rw::V3d pos, rw::V3d x, rw::V3d y, rw::V3d z);

// misc

extern FILE *logfile;
void openLogFile(char *path);
void closeLogFile(void);
void dumpIPLBoundingSpheres(void);
void dumpInstBS(int level, sGeomInstance *inst);

CEntity *GetEntityById(int id);
void LinkInstances(void);
void WriteLinks(void);

void DumpCollisions(void);
void DumpModels(void);

void dumpModelInfoLinks(void);

enum {
	TexName = 1,
};
void ReadDefFile(char *filename);
char *FindNameDef(int type, char *name);
