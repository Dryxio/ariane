#ifdef _WIN32
#include <Windows.h>	// necessary for the moment
#endif

#include <rw.h>
#include <skeleton.h>
#include "imgui/ImGuizmo.h"
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include <rwgta.h>
#define PS2

//using namespace std;
using rw::int8;
using rw::uint8;
using rw::int16;
using rw::uint16;
using rw::int32;
using rw::uint32;
using rw::int64;
using rw::uint64;
using rw::float32;
using rw::bool32;
using rw::uintptr;
typedef unsigned int uint;

struct ObjectInst;

#include "sapaths.h"

#ifdef RWHALFPIXEL
#define HALFPX (0.5f)
#else
#define HALFPX (0.0f)
#endif

#include "Pad.h"
#include "camera.h"
#include "collision.h"

void panic(const char *fmt, ...);
void debug(const char *fmt, ...);
void log(const char *fmt, ...);
void SetupStyle(void);
void SetupFonts(void);
bool GetEditorRootDirectory(char *dir, size_t size);
bool GetGameRootDirectory(char *dir, size_t size);
bool BuildPath(char *dst, size_t size, const char *dir, const char *name);
bool EnsureParentDirectoriesForPath(const char *path);
bool GetArianeDataDirectory(char *dir, size_t size);
bool GetArianeDataPath(char *dst, size_t size, const char *name);
FILE *fopenArianeDataRead(const char *name, const char *legacyName = nil);
FILE *fopenArianeDataWrite(const char *name);
void setHotReloadTracePath(const char *path);
void hotReloadTrace(const char *fmt, ...);
void addToLogWindow(const char *fmt, va_list args);
void LoadInitialEditorWindowState(int *width, int *height);
void LoadInitialAntialiasingSettings(void);
void ApplyInitialEditorWindowState(void);
void UpdateEditorWindowState(void);
void OnEditorWindowResized(int width, int height);
void SaveEditorSettingsNow(void);

char *getPath(const char *path);
FILE *fopen_ci(const char *path, const char *mode);
bool doesFileExist(const char *path);
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
template <typename T> T min(T a, T b) { return a < b ? a : b; }
template <typename T> T max(T a, T b) { return a > b ? a : b; }
template <typename T> T clamp(T v, T min, T max) { return v<min ? min : v>max ? max : v; }
template <typename T> T sq(T a) { return a*a; }

void plCapturePad(int arg);
void plUpdatePad(CControllerState *state);

void ConvertTxd(rw::TexDictionary *txd);

extern float timeStep;
extern float avgTimeStep;

#define DEGTORAD(d) (d/180.0f*PI)

struct Ray {
	rw::V3d start;
	rw::V3d dir;
};
inline bool
SphereIntersect(const CSphere &sph, const Ray &ray)
{
	rw::V3d diff = sub(ray.start, sph.center);
	float a = dot(ray.dir,ray.dir);
	float b = 2*dot(ray.dir, diff);
	float c = dot(diff,diff) - sq(sph.radius);

	float discr = sq(b) - 4*a*c;
	return discr > 0.0f;
}
bool IntersectRayTriangle(const Ray &ray, rw::V3d a, rw::V3d b, rw::V3d c, float *t);
bool IntersectRaySphere(const Ray &ray, const CSphere &sphere, float *t);
bool IntersectRayColModel(const Ray &worldRay, ObjectInst *inst, rw::V3d *hitPos);

//
// Options
//

extern bool gRenderOnlyLod;
extern bool gRenderOnlyHD;
extern bool gRenderBackground;
extern bool gRenderWater;
extern bool gRenderPostFX;
extern bool gEnableFog;
extern bool gEnableTimecycleBoxes;
extern bool gUseBlurAmb;
extern bool gOverrideBlurAmb;
extern bool gNoTimeCull;
extern bool gNoAreaCull;
extern bool gDoBackfaceCulling;
extern bool gPlayAnimations;
extern bool gUseViewerCam;
extern bool gDrawTarget;
extern float gFlyFastMul;
extern float gFlySlowMul;
extern float gFovWheelStep;
void SetInstIplFilterKey(ObjectInst *inst, const char *sceneName);
bool IsInstVisibleByIplFilter(const ObjectInst *inst);
void RefreshIplVisibilityEntries(void);
int GetIplVisibilityEntryCount(void);
const char *GetIplVisibilityEntryName(int i);
bool GetIplVisibilityEntryVisible(int i);
void SetIplVisibilityEntryVisible(int i, bool visible);
void SetAllIplVisibilityEntries(bool visible);
void ShowOnlyIplVisibilityEntry(int i);

// non-rendering things
extern bool gRenderCollision;
extern bool gRenderZones;
extern bool gRenderMapZones;
extern bool gRenderNavigZones;
extern bool gRenderInfoZones;
extern bool gRenderCullZones;
extern bool gRenderAttribZones;
extern bool gRenderLegacyPedPaths;
extern bool gRenderLegacyCarPaths;
extern bool gRenderSaPedPaths;
extern bool gRenderSaPedPathWalkers;
extern bool gRenderSaCarPaths;
extern bool gRenderSaCarPathTraffic;
extern bool gRenderSaAreaGrid;
extern bool gRenderLightEffects;
extern bool gRenderEffects;
extern bool gRenderTimecycleBoxes;
extern int gSaPedPathWalkerCount;
extern int gSaCarPathTrafficCount;
extern float gSaCarPathTrafficSpeedScale;
extern bool gSaCarPathTrafficFreezeRoutes;
extern bool gRenderSaCarPathParkedCars;
extern int gSaCarPathParkedCarCount;
extern uint32 gRequestedAASamples;

// SA postfx
extern int  gColourFilter;
extern bool gRadiosity;

// SA building pipe
extern int gBuildingPipeSwitch;
extern float gDayNightBalance;
extern float gWetRoadEffect;

// Neo stuff
extern float gNeoLightMapStrength;


// These don't necessarily match the game's values, roughly double of SA PC
enum {
	MODELNAMELEN = 30,
	NUMOBJECTDEFS = 40000,
	NUMTEXDICTS = 10000,
	NUMCOLS = 510,
	NUMSCENES = 80,
	NUMIPLS = 512,
	NUMCDIMAGES = 100,
	NUMTCYCBOXES = 64,

	NUMWATERVERTICES = 4000,
	NUMWATERQUADS = 1000,
	NUMWATERTRIS = 1000,
	NUMZONES = 500,	// for each type
};

#define LODDISTANCE (300.0f)

#include "Rect.h"
#include "PtrNode.h"
#include "PtrList.h"

struct CRGBA
{
	uint8 r, g, b, a;
};

#include "timecycle.h"
#include "Sprite.h"

namespace Zones
{
void CreateZone(const char *name, int type, CBox box, int level, const char *text);
void Render(void);
void AddAttribZone(CBox box, int flags, int wantedLevelDrop);
void AddAttribZone(rw::V3d pos, float s1x, float s1y,
	float s2x, float s2y, float zmin, float zmax, int flags);
void AddMirrorAttribZone(rw::V3d pos, float s1x, float s1y,
	float s2x, float s2y, float zmin, float zmax,
	int flags, rw::Plane mirror);
void RenderAttribZones(void);
void RenderCullZones(void);
}

// Game

enum eGame
{
	GAME_NA,
	GAME_III,
	GAME_VC,
	GAME_SA,
	GAME_LCS,
	GAME_VCS
};
using rw::PLATFORM_NULL;
using rw::PLATFORM_PS2;
using rw::PLATFORM_XBOX;
enum {
	PLATFORM_PC = rw::PLATFORM_D3D8,
	PLATFORM_PSP = 10
};
extern int gameversion;
extern int gameplatform;
inline bool isIII(void) { return gameversion == GAME_III; }
inline bool isVC(void) { return gameversion == GAME_VC; }
inline bool isSA(void) { return gameversion == GAME_SA; }

struct WeatherInfo;

struct Params
{
	int map;
	rw::V3d initcampos;
	rw::V3d initcamtarg;
	int numAreas;
	const char **areaNames;

	int objFlagset;

	int timecycle;
	int numHours;
	int numWeathers;
	int extraColours;	// weather ID where extra colours start
	int numExtraColours;	// number of extra colour blocks
	WeatherInfo *weatherInfo;
	int background;
	int daynightPipe;

	int water;
	const char *waterTex;
	rw::V2d waterStart, waterEnd;	// waterpro

	int alphaRefDefault;	// preset value
	int alphaRef;		// the regular one we want to use for rendering
	bool ps2AlphaTest;	// emulate PS2 alpha test

	bool backfaceCull;
	bool txdFallbackGeneric;

	int neoWorldPipe;

	int leedsPipe;

	bool checkColModels;
	int maxNumColSpheres;
	int maxNumColBoxes;
	int maxNumColTriangles;
};
extern Params params;

extern int gameTxdSlot;

extern int currentHour, currentMinute;
extern int extraColours;
extern int currentArea;

struct WeatherInfo
{
	const char *name;
	int flags;
};

namespace Weather
{
enum {
	Sunny = 0x01,
	Foggy = 0x02,
	Extrasunny = 0x04,
};

extern int oldWeather, newWeather;
extern float interpolation;
extern float cloudCoverage;
extern float foggyness;
extern float extraSunnyness;

void Update(void);
};

// TODO, this is a stub
struct GameFile
{
	char *name;
	char *sourcePath;  // physical disk path for I/O, or nil for base game files
	// types of files we want to have eventually:
	//	IDE Definition
	//	IPL Scene
	//	COL ?
	//	IMG ?
};
GameFile *NewGameFile(char *path);

enum SaveDestination
{
	SAVE_DESTINATION_ORIGINAL_FILES = 0,
	SAVE_DESTINATION_MODLOADER,
};
extern SaveDestination gSaveDestination;

bool IsHourInRange(int h1, int h2);
void FindVersion(void);
void LoadGame(void);
void Idle(void);
void DefinedState(void);

// Gizmo
enum GizmoMode {
	GIZMO_TRANSLATE,
	GIZMO_ROTATE
};
extern int gGizmoMode;
extern bool gGizmoEnabled;
extern bool gGizmoHovered;
extern bool gGizmoUsing;
extern bool gPlaceSnapToObjects;
extern bool gPlaceSnapToGround;
extern bool gDragFollowGround;
extern bool gDragAlignToSurface;

// Brush tool (paint objects onto surfaces)
extern bool gBrushMode;
extern float gBrushZOffset;         // signed; added to surface Z (negative sinks object into ground)
extern bool gBrushAlignToSurface;   // rotate placed objects to match surface normal
extern float gBrushYawMin;          // degrees; yaw applied is uniform random in [min,max]
extern float gBrushYawMax;          // degrees; min==max means no randomization (fixed angle)
extern float gBrushSpacing;         // min world-space distance between drag-painted placements (meters)
extern float gBrushRadius;          // brush disc radius (m); 0 = single placement at hit
extern int gBrushCount;             // number of objects per click (scatter count when radius > 0)
extern float gBrushDelayMs;         // min time between drag-paint bursts (ms); 0 = disabled
rw::Quat BuildGroundAlignedRotationFromRotation(const rw::Quat &sourceRotation, rw::V3d groundNormal);
bool GetBrushSurfaceHit(rw::V3d *hitPos, rw::V3d *hitNormal);
void EnterBrushMode(int objectId);
void ExitBrushMode(void);
extern bool gGizmoSnap;
extern float gGizmoSnapAngle;
extern float gGizmoSnapTranslate;

// Rect-select (marquee selection)
extern bool gRectSelectActive;	// true when LMB is consumed by rect-select (blocks camera)

// Batch operation limit (undo, delete, snap, gizmo drag, clipboard, etc.)
#define MAX_BATCH_OBJECTS 512

// Undo/Redo
enum UndoType {
	UNDO_MOVE,
	UNDO_ROTATE,
	UNDO_DELETE,
	UNDO_PASTE,
	UNDO_TRANSFORM_BATCH,
};

enum UndoTransformFlags {
	UNDO_TRANSFORM_POS = 1,
	UNDO_TRANSFORM_ROT = 2,
};

struct UndoTransform {
	ObjectInst *inst;
	rw::V3d oldPos;
	rw::V3d newPos;
	rw::Quat oldRot;
	rw::Quat newRot;
	uint8 flags;
};

struct UndoAction {
	int type;
	// For MOVE/ROTATE: single instance
	ObjectInst *inst;
	rw::V3d oldPos;
	rw::Quat oldRot;
	rw::V3d newPos;
	rw::Quat newRot;
	// For MOVE: LOD that was also moved
	ObjectInst *lodInst;
	rw::V3d lodOldPos;
	rw::V3d lodNewPos;
	// For DELETE: list of deleted instances (inst + LOD cascade)
	ObjectInst *deletedInsts[MAX_BATCH_OBJECTS];
	int numDeleted;
	// For PASTE: list of pasted instances (to delete on undo)
	ObjectInst *pastedInsts[MAX_BATCH_OBJECTS];
	int numPasted;
	// For batch transform actions (snap to ground, etc.)
	UndoTransform transforms[MAX_BATCH_OBJECTS];
	int numTransforms;
};

void UndoRecordMove(ObjectInst *inst, rw::V3d oldPos, ObjectInst *lodInst, rw::V3d lodOldPos);
void UndoRecordRotate(ObjectInst *inst, rw::Quat oldRot);
void UndoRecordDelete(ObjectInst **insts, int num);
void UndoRecordPaste(ObjectInst **insts, int num);
void UndoRecordTransformBatch(UndoTransform *transforms, int num);
void Undo(void);
void Redo(void);

// Copy/Paste
void CopySelected(void);
void PasteClipboard(void);

// Prefabs
int ExportPrefab(const char *path);
int ImportPrefab(const char *path);
int ExportSelectedDffs(const char *dir, int *numFailed);
int ExportSelectedTxds(const char *dir, int *numFailed);

// Toast notifications
enum ToastCategory {
	TOAST_UNDO_REDO,
	TOAST_DELETE,
	TOAST_COPY_PASTE,
	TOAST_SAVE,
	TOAST_SELECTION,
	TOAST_SPAWN,
	TOAST_NUM_CATEGORIES
};
void Toast(ToastCategory cat, const char *fmt, ...);

// Diff viewer flags (bitmask — an instance can be both moved and rotated)
enum DiffFlags {
	DIFF_ADDED    = 1,
	DIFF_DELETED  = 2,
	DIFF_MOVED    = 4,
	DIFF_ROTATED  = 8,
	DIFF_RESTORED = 16,
};
int GetInstanceDiffFlags(ObjectInst *inst);
void StampChangeSeq(ObjectInst *inst);
uint32 BumpChangeSeq(void);
uint32 GetLatestChangeSeq(void);

// Object Spawner
extern bool gPlaceMode;
extern bool gSaveToOriginalIpl;  // NEW: Save new objects to their original IPL instead of custom.ipl
void InitLodLookup(void);
void SpawnPlaceObject(rw::V3d position, const rw::Quat *orientation = nil);
int SpawnPlaceObjectNoUndo(rw::V3d position, const rw::Quat *orientation,
	ObjectInst **outInsts, int outCapacity);
void SpawnExitPlaceMode(void);
int GetSpawnObjectId(void);
void SetSpawnObjectId(int id);
void SetCustomPlacementIpl(const char *logicalPath, const char *sourcePath, bool addToDat);
int GetLodForObject(int id);
int SnapSelectedToGround(bool alignRotation);
bool GetGroundPlacementSurface(rw::V3d pos, rw::V3d *hitPos, rw::V3d *hitNormal = nil, bool ignoreSelection = false);
rw::V3d GetPlacementPosition(void);
float GetPlacementBaseOffset(int objectId);

// Object Browser categories & favourites
void InitObjectCategories(void);
int GetObjectCategory(int modelId);
void LoadFavourites(void);
void SaveFavourites(void);
bool IsFavourite(int id);
void ToggleFavourite(int id);

// 3D Preview
void InitPreviewRenderer(void);
void ShutdownPreviewRenderer(void);
void RenderPreviewObject(int objectId);
extern rw::Texture *gPreviewTexture;
void HandleCustomImportDrop(const char *path);

// Game Data structures

void AddCdImage(const char *path);
void InitCdImages(void);
void RefreshCdImageMappings(void);
uint8 *ReadFileFromImage(int i, int *size);
GameFile *GetGameFileFromImage(int i);
const char *GetCdImageLogicalName(int i);
const char *GetCdImageSourcePath(int i);
bool GetCdImageEntrySourcePath(int i, char *outSourcePath, size_t outSourcePathSize);
bool GetPreviousCdImageEntryIndex(int i, int *outIndex);
bool WriteFileToImage(int i, uint8 *data, int size);
bool BuildModloaderImageEntryExportPath(int i, char *dst, size_t size);
void RequestObject(int id);
void LoadAllRequestedObjects(void);


struct TxdDef
{
	char name[MODELNAMELEN];
	rw::TexDictionary *txd;
	int parentId;
	int32 imageIndex;
};
extern rw::TexDictionary *defaultTxd;
void RegisterTexStorePlugin(void);
TxdDef *GetTxdDef(int i);
int FindTxdSlot(const char *name);
int AddTxdSlot(const char *name);
bool RemoveTxdSlot(int i);
void TxdPush(void);
void TxdPop(void);
bool IsTxdLoaded(int i);
void CreateTxd(int i);
void LoadTxd(int i);
void LoadTxd(int i, const char *path);
void TxdMakeCurrent(int i);
void TxdSetParent(const char *child, const char *parent);
void SetTxdLookupContext(const char *objectName, int txdSlot);


struct ColFileHeader
{
	uint32 fourcc;
	uint32 modelsize;
	char name[24];
};

struct ColDef
{
	char name[MODELNAMELEN];
	int32 imageIndex;
};
ColDef *GetColDef(int i);
int AddColSlot(const char *name);
void LoadCol(int slot);
void LoadAllCollisions(void);

// One class for all map objects
struct ObjectDef
{
	enum Type {
		ATOMIC,
		CLUMP
	};

	int m_id;	// our own id
	char m_name[MODELNAMELEN];
	int m_txdSlot;
	int m_type;
	CColModel *m_colModel;
	bool m_gotChildCol;
	int m_pedPathIndex;
	int m_carPathIndex;
	int m_effectIndex;
	int m_numEffects;

	// flags
	bool m_normalCull;	// only III
	bool m_noFade;
	bool m_drawLast;
	bool m_additive;
	bool m_isSubway;	// only III?
	bool m_ignoreLight;
	bool m_noZwrite;
	// VC
	bool m_wetRoadReflection;
	bool m_noShadows;
	bool m_ignoreDrawDist;	// needs a better name perhaps
	bool m_isCodeGlass;
	bool m_isArtistGlass;
	// SA Base
	bool m_noBackfaceCulling;
	// SA Atomic
	bool m_dontCollideWithFlyer;
	bool m_isGarageDoor;
	bool m_isDamageable;
	bool m_isTree;
	bool m_isPalmTree;
	bool m_isTag;
	bool m_noCover;
	bool m_wetOnly;
	// SA Clump
	bool m_isDoor;

	// atomic info
	int m_numAtomics;
	float m_drawDist[3];
	rw::Atomic *m_atomics[3];
	// time objects
	bool m_isTimed;
	int m_timeOn, m_timeOff;

	// clump info
	rw::Clump *m_clump;
	char m_animname[MODELNAMELEN];
	void *m_animAsset;

	bool m_cantLoad;
	bool m_hasPreRendered;
	int32 m_imageIndex;
	float m_minDrawDist;
	bool m_isBigBuilding;
	bool m_isHidden;
	ObjectDef *m_relatedModel;
	ObjectDef *m_relatedTimeModel;

	GameFile *m_file;

	float GetLargestDrawDist(void);
	rw::Atomic *GetAtomicForDist(float dist);
	bool IsLoaded(void);
	void LoadAtomic(void);
	void LoadClump(void);
	void Load(void);
	void SetAtomic(int n, rw::Atomic *atomic);
	void SetClump(rw::Clump *clump);
	void CantLoad(void);
	void SetupBigBuilding(int first, int last);
	void SetFlags(int flags);
};
ObjectDef *AddObjectDef(int id);
void RemoveObjectDef(int id);
ObjectDef *GetObjectDef(int id);
ObjectDef *GetObjectDef(const char *name, int *id);


struct FileObjectInstance
{
	rw::V3d position;
	rw::Quat rotation;
	int objectId;
	int area;
	int lod;
};

struct ObjectInst
{
	rw::V3d m_translation;
	rw::Quat m_rotation;
	// cached form of the above
	rw::Matrix m_matrix;
	int m_objectId;
	int m_area;

	void *m_rwObject;
	bool m_isBigBuilding;
	uint16 m_scanCode;

	// SA only
	int m_lodId;
	int m_iplSlot;
	ObjectInst *m_lod;
	int m_numChildren;	// hq versions
	int m_numChildrenRendered;
	// SA flags
	bool m_isUnimportant;
	bool m_isUnderWater;
	bool m_isTunnel;
	bool m_isTunnelTransition;

	// additional stuff
	int32 m_id;	// to identify when picking
	int m_selected;
	int m_highlight;	// various ways to highlight this object
	bool m_isDeleted;	// marked for deletion (commented out in IPL)
	bool m_isAdded;		// newly created text IPL instance not yet saved to disk
	bool m_isDirty;		// position/rotation was modified since the last save
	bool m_gameEntityExists;	// hot reload believes this instance currently exists in the running game
	rw::V3d m_origTranslation;	// position the game currently has (for hot reload)
	rw::Quat m_origRotation;	// rotation the game currently has (for hot reload)
	// saved state (updated when IPL is written to disk)
	rw::V3d m_savedTranslation;
	rw::Quat m_savedRotation;
	bool m_savedStateValid;		// false for instances that have never been saved
	bool m_wasSavedDeleted;		// deletion state at last save (text IPL only)
	uint32 m_changeSeq;		// monotonic counter for chronological diff ordering
	int m_iplIndex;		// index of this instance within its IPL file (for save)
	int32 m_imageIndex;	// IMG directory index (for binary IPL save), -1 if text IPL
	int m_binInstIndex;	// index within binary IPL instance array
	char m_iplFilterKey[256];
	void *m_animState;
	float m_animTime;

	GameFile *m_file;

	void UpdateMatrix(void);
	void *CreateRwObject(void);
	void Init(FileObjectInstance *fi);
	void SetupBigBuilding(void);
	CRect GetBoundRect(void);
	bool IsOnScreen(void);
	void PreRender(void);

	void JumpTo(void);
	void Select(void);
	void Deselect(void);
	void Delete(void);
	void Undelete(void);
};
extern CPtrList instances;
extern CPtrList selection;
ObjectInst *GetInstanceByID(int32 id);
int32 pick(void);
ObjectInst *AddInstance(void);
void ClearSelection(void);
void DeleteSelected(void);
int DeleteAllInstances(void);
void RemoveInstFromSectors(ObjectInst *inst);



enum EffectType {
	FX_LIGHT,
	FX_PARTICLE,
	FX_LOOKATPOINT,
	FX_PEDQUEUE,
	FX_SUNGLARE,
	FX_INTERIOR,
	FX_ENTRYEXIT,
	FX_ROADSIGN,
	FX_TRIGGERPOINT,
	FX_COVERPOINT,
	FX_ESCALATOR
};

enum FlareType {
        FLARE_NONE,
        FLARE_SUN,
        FLARE_HEADLIGHTS
};

// III and VC for now
struct Effect {
	int id;
	rw::V3d pos;
	rw::RGBA col;
	int type;
	struct Light {
		float lodDist;
		float size;
		float coronaSize;
		float shadowSize;
		int flashiness;
		int reflection;
		int lensFlareType;
		int shadowAlpha;
		int flags;
		char coronaTex[32];
		char shadowTex[32];
	};
	struct Particle {
		int particleType;
		char name[24];
		rw::V3d dir;
		float size;
	};
	struct LookAtPoint {
		rw::V3d dir;
		int type;
		int probability;
	};
	struct PedQueue {
		rw::V3d queueDir;
		rw::V3d useDir;
		rw::V3d forwardDir;
		int type;
		int interest;
		int lookAt;
		int flags;
		char scriptName[8];
	};
	struct Interior {
		int type;
		int group;
		float width;
		float depth;
		float height;
		float rot;
	};
	struct EntryExit {
		float enterAngle;
		float radiusX;
		float radiusY;
		rw::V3d exitPos;
		float exitAngle;
		int areaCode;
		int flags;
		int extraColor;
		int openTime;
		int shutTime;
		int extraFlags;
		char title[8];
	};
	struct Roadsign {
		float width;
		float height;
		float rotX;
		float rotY;
		float rotZ;
		int flags;
		char text[4][16];
	};
	struct TriggerPoint {
		int index;
	};
	struct CoverPoint {
		float dirX;
		float dirY;
		int usage;
	};
	struct Escalator {
		rw::V3d bottom;
		rw::V3d top;
		rw::V3d end;
		bool goingUp;
	};
	union {
		Light light;
		Particle prtcl;
		LookAtPoint look;
		PedQueue queue;
		Interior interior;
		EntryExit entryExit;
		Roadsign roadsign;
		TriggerPoint triggerPoint;
		CoverPoint coverPoint;
		Escalator escalator;
		// glare has no extra data
	};

	void JumpTo(ObjectInst *inst);
};

namespace Effects {
extern Effect *hoveredEffect, *guiHoveredEffect;
extern Effect *selectedEffect;
void AddEffect(Effect e);
Effect *GetEffect(int idx);
void RenderLights(void);
void Render(void);
}


enum PathType {
	PedPath,
	CarPath,
	WaterPath
};

#define LaneWidth 5.0f
struct PathNode {
	int type;
	int link;
	int linkType;
	float x, y, z;
	float width;
	int lanesIn, lanesOut;
	// VC
	int speed;
	int flags;
	float density;
	// SA
	int special;

	enum Type {
		NodeNone = 0,
		NodeExternal,
		NodeInternal
	};
	enum Flags {
		NodeDisabled = 1,
		NodeRoadBlock = 2,
		NodeBetweenLevels = 4,
		NodeUnderBridge = 8
	};

	bool water;
	int lanesInX, lanesOutX;

	int idx;
	int tabId;
	int objId;
	// for internal nodes
	int numLinks;
	int links[12];

	float laneOffset(void) {
		if(lanesInX == 0)
			return 0.5f - 0.5f*lanesOutX;
		if(lanesOutX == 0)
			return 0.5f - 0.5f*lanesInX;
		return 0.5f + width/(2.0f*LaneWidth);
	}
	float laneOffsetIII(void) {
		if(lanesInX == 0)
			return 0.5f - 0.5f*lanesOutX;
		if(lanesOutX == 0)
			return 0.5f - 0.5f*lanesInX;
		return 0.5f;
	}
	void JumpTo(ObjectInst *inst);
	bool isDetached(void);
};

namespace Path {
extern PathNode *hoveredNode, *guiHoveredNode;
extern PathNode *selectedNode;
void AddNode(PathType type, int id, PathNode node);
PathNode *GetPedNode(int base, int i);
PathNode *GetCarNode(int base, int i);
PathNode *GetDetachedPedNode(int base, int i);
PathNode *GetDetachedCarNode(int base, int i);
void RenderPedPaths(void);
void RenderCarPaths(void);
}


// World/sectors

struct Sector
{
	CPtrList buildings;
	CPtrList buildings_overlap;
	CPtrList bigbuildings;
	CPtrList bigbuildings_overlap;
};
extern int numSectorsX, numSectorsY;
extern CRect worldBounds;
extern Sector *sectors;
extern Sector outOfBoundsSector;
void InitSectors(void);
Sector *GetSector(int ix, int iy);
//Sector *GetSector(float x, float y);
int GetSectorIndexX(float x);
int GetSectorIndexY(float x);
bool IsInstInBounds(ObjectInst *inst);
void InsertInstIntoSectors(ObjectInst *inst);


struct IplDef
{
	char name[MODELNAMELEN];
	int instArraySlot;

	int32 imageIndex;
};
int AddInstArraySlot(int n);
ObjectInst **GetInstArray(int i);
int GetInstArraySize(int i);
IplDef *GetIplDef(int i);
int AddIplSlot(const char *name);
void LoadIpl(int i, const char *sceneName = nil);

// File Loader

namespace FileLoader
{

struct BinaryIplSaveResult
{
	int numSavedImages;
	int32 savedImages[256];
	int numFailedImages;
	int32 failedImages[256];
	int numBlockedEmptyDeletes;
	int numFailedFiles;
};

struct AutomaticBackupResult
{
	bool createdSnapshot;
	bool hadWarnings;
	int numTextFiles;
	int numBinaryFiles;
	int numErrors;
	char snapshotPath[1024];
};

extern GameFile *currentFile;

struct DatDesc
{
	char name[5];
	void (*handler)(char *line);

	static void *get(DatDesc *desc, const char *name);
};

char *LoadLine(FILE *f);
void LoadLevel(const char *filename);
void LoadObjectTypes(const char *filename);
void LoadScene(const char *filename);
void LoadCollisionFile(const char *path);
rw::TexDictionary *LoadTexDictionary(const char *path);
BinaryIplSaveResult SaveScene(const char *filename);
BinaryIplSaveResult SaveBinaryIpls(const int32 *skipImages = nil, int numSkipImages = 0);
AutomaticBackupResult CreateAutomaticBackup(const char *rootDir, int keepCount);
}

// Rendering

enum HighlightStyle
{
	HIGHLIGHT_NONE,
	HIGHLIGHT_FILTER,
	HIGHLIGHT_SELECTION,
	HIGHLIGHT_HOVER,
};

struct SceneGlobals {
	rw::World *world;
	rw::Camera *camera;
};
extern rw::Light *pAmbient, *pDirect;
extern rw::Texture *whiteTex;
extern SceneGlobals Scene;
extern CCamera TheCamera;

bool32 instWhite(int type, uint8 *dst, uint32 numVertices, uint32 stride);

void myRenderCB(rw::Atomic *atomic);

// Neo World pipeline
extern rw::ObjPipeline *neoWorldPipe;
void MakeNeoWorldPipe(void);

// SA DN building pipeline
bool IsBuildingPipeAttached(rw::Atomic *atm);
void SetupBuildingPipe(rw::Atomic *atm);
void UpdateDayNightBalance(void);
// this should perhaps not be public
void GetBuildingEnvMatrix(rw::Atomic *atomic, rw::Frame *envframe, rw::RawMatrix *envmat);
extern rw::ObjPipeline *buildingPipe;
extern rw::ObjPipeline *buildingDNPipe;
void MakeCustomBuildingPipelines(void);

// Leeds building pipeline
extern rw::ObjPipeline *leedsPipe;
void MakeLeedsPipe(void);

void RegisterPipes(void);
void RenderInit(void);
void BuildRenderList(void);
void RenderOpaque(void);
void RenderTransparent(void);
void RenderEverything(void);
ObjectInst *GetVisibleInstUnderRay(const Ray &ray, rw::V3d *hitPos = nil, float *hitT = nil);
void ForEachVisibleInst(void (*fn)(ObjectInst *inst, void *data), void *data);

// Debug Render
void RenderLine(rw::V3d v1, rw::V3d v2, rw::RGBA c1, rw::RGBA c2);
void RenderWireBoxVerts(rw::V3d *verts, rw::RGBA col);
void RenderWireBox(CBox *box, rw::RGBA col, rw::Matrix *xform);
void RenderSphereAsWireBox(CSphere *sphere, rw::RGBA col, rw::Matrix *xform);
void RenderSphereAsCross(CSphere *sphere, rw::RGBA col, rw::Matrix *xform);
void RenderWireSphere(CSphere *sphere, rw::RGBA col, rw::Matrix *xform);
void RenderWireTriangle(rw::V3d *v1, rw::V3d *v2, rw::V3d *v3, rw::RGBA col, rw::Matrix *xform);
void RenderAxesWidget(rw::V3d pos, rw::V3d x, rw::V3d y, rw::V3d z);

void RenderEverythingCollisions(void);
void RenderDebugLines(void);


void RenderPostFX(void);


namespace WaterLevel
{
	struct WaterVertex {
		rw::V3d pos;
		rw::V2d speed;
		float waveunk, waveheight;
	};
	struct WaterQuad {
		int indices[4];
		int flags;	// bit 0: visible, bit 1: limited depth
	};
	struct WaterTri {
		int indices[3];
		int flags;
	};

	void Initialise(void);
	void Render(void);

	// Editor state
	extern bool gWaterEditMode;
	extern int gWaterSubMode;	// 0=polygon, 1=vertex
	extern bool gWaterDirty;
	extern int gWaterCreateMode;	// 0=off, 1..N=placing corners
	extern int gWaterCreateShape;	// 0=quad, 1=triangle
	extern float gWaterCreateZ;
	extern bool gWaterSnapEnabled;
	extern float gWaterSnapSize;

	// Accessor API
	int GetNumQuads(void);
	int GetNumTris(void);
	int GetNumVertices(void);
	WaterVertex *GetVertex(int i);
	WaterQuad *GetQuad(int i);
	WaterTri *GetTri(int i);

	// Editor functions
	void HandleWaterTool(void);
	void DoWaterGizmo(void);
	void RenderEditOverlay(void);
	bool SaveWater(void);
	void ClearWaterPolySelection(void);
	void ClearWaterVertexSelection(void);
	void ClearWaterSelection(void);
	void WeldCoincidentVertices(int vertexIndex, rw::V3d oldPos);
	void EnterCreateMode(void);
	void CancelCreateMode(void);
	int PickWaterPoly(Ray ray, float *hitT = nil);
	void SelectWaterPoly(int type, int index);
	void DeleteSelectedWaterPolys(void);
	void DuplicateSelectedWaterPolys(void);
	void ClearAllWater(void);
	void ReloadWater(void);

	// Undo/Redo
	void WaterUndoPush(void);
	void WaterUndo(void);
	void WaterRedo(void);
	bool WaterCanUndo(void);
	bool WaterCanRedo(void);

	// Selection queries for gui
	int GetNumSelectedPolys(void);
	int GetNumSelectedVertices(void);
	int GetSelectedPolyType(int sel);	// 0=quad, 1=tri
	int GetSelectedPolyIndex(int sel);
	int GetSelectedVertexIndex(int sel);
};

namespace Clouds
{
	extern float CloudRotation;

	void Init(void);
	// III and VC
	void RenderBackground(int16 topred, int16 topgreen, int16 topblue,
		int16 botred, int16 botgreen, int16 botblue, int16 alpha);
	void RenderHorizon(void);

	void RenderLowClouds(void);
	void RenderFluffyClouds(void);
	// SA
	void RenderSkyPolys(void);
}


//
// GUI
//

void gui(void);
void uiShowCdImages(void);
