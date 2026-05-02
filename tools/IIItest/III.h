#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cassert>

#include <rw.h>
enum
{
	VEND_ROCKSTAR     = 0x0253F2,
};


using rw::uint8;
using rw::int8;
using rw::uint16;
using rw::int16;
using rw::uint32;
using rw::int32;
using rw::bool32;
typedef rw::uintptr uintptr;
typedef unsigned int uint;
typedef unsigned char uchar;
typedef unsigned short ushort;

#define ALIGNPTR(p) (void*)((((uintptr)(void*)p) + sizeof(void*)-1) & ~(sizeof(void*)-1))

// taken from skeleton
struct Globals
{
	const char *windowtitle;
	int32 width;
	int32 height;
	bool32 quit;
};
extern Globals globals;

extern  uchar work_buff[55000];
void debug(const char *fmt, ...);

void ps2_srand(uint s);
int ps2_rand(void);

// node name
void NodeNamePluginAttach(void);
char *GetFrameNodeName(rw::Frame *f);


#include "Pad.h"
#include "templates.h"
#include "config.h"
#include "BasicTypes.h"


class C2dEffect
{
public:
	struct Light {
		float dist;
		float outerRange;
		float size;
		float innerRange;
		uchar flash;
		uchar wet;
		uchar flare;
		uchar shadowIntens;
		uchar flag;
		rw::Texture *corona;
		rw::Texture *shadow;
	};
	struct Particle {
		int particleType;
		float dir[3];
		float scale;
	};
	struct Attractor {
		float dir[3];
		uchar flag;
		uchar probability;
	};

	rw::V3d pos;
	rw::RGBA col;
	uchar type;
	union {
		Light light;
		Particle particle;
		Attractor attractor;
	};
};

#define LOD_DISTANCE 300.0f
#define FADE_DISTANCE 20.0f
#define STREAM_DISTANCE 30.0f

#include "Timer.h"
#include "Clock.h"
#include "Weather.h"
#include "Rect.h"
#include "zones.h"
#include "Animation.h"
#include "Collision.h"
#include "ModelInfo.h"
#include "ModelIndices.h"
#include "Lists.h"
#include "Placeable.h"
#include "Entity.h"
#include "Building.h"
#include "Treadable.h"
#include "Dummy.h"
#include "DummyObject.h"
#include "DummyPed.h"
#include "Physical.h"
#include "World.h"
#include "Pools.h"
#include "PathFind.h"
#include "ObjectData.h"

#include "WaterLevel.h"
#include "Sprite.h"
#include "Clouds.h"
#include "TxdStore.h"
#include "TimeCycle.h"
#include "VisibilityPlugins.h"
#include "TempColModels.h"
#include "Game.h"
#include "Renderer.h"
#include "streaming.h"

inline float
clamp(float v, float min, float max){ return v<min ? min : v>max ? max : v; }
inline float
sq(float x) { return x*x; }
#define PI M_PI
#define DEGTORAD(d) (d/180.0f*PI)


char *getPath(const char *path);
char *skipWhite(char *s);
void convertTxd(rw::TexDictionary *txd);

extern bool isRunning;
CVector FindPlayerCoors(void);

// RW stuff
struct GlobalScene
{
	rw::Camera *camera;
	rw::World  *world;
};
extern GlobalScene Scene;
extern rw::Light *pAmbient;
extern rw::Light *pDirect;
extern rw::Light *pExtraDirectionals[4];

void SetLightsWithTimeOfDayColour(rw::World*);
void LightsCreate(rw::World*);
void SetAmbientAndDirectionalColours(float mult);
void ReSetAmbientAndDirectionalColours(void);
void DeActivateDirectional(void);
void ActivateDirectional(void);
void SetAmbientColours(void);
void SetAmbientColoursForPedsCarsAndObjects(void);


rw::Camera *CameraCreate(int width, int height, int z);
void WindowResize(rw::Rect *r);
void DefinedState(void);

extern CEntity *debugent;

void SystemInit(void);
void GameInit(void);
void TheGame(void);

// input events
void KeyDown(int k);
void KeyUp(int k);
void CharInput(int c);
void MouseMove(int x, int y);
void MouseButton(int buttons);

// platform implementations
extern rw::EngineOpenParams engineOpenParams;
int plGetTimeInMS(void);
bool plWindowclosed(void);
void plHandleEvents(void);
void plCapturePad(int n);
void plUpdatePad(CControllerState *state);
void plAttachInput(void);

#define TEMPBUFFERVERTSIZE 256
#define TEMPBUFFERINDEXSIZE 1024
extern int TempBufferIndicesStored;
extern int TempBufferVerticesStored;
extern rw::RWDEVICE::Im3DVertex TempVertexBuffer[TEMPBUFFERVERTSIZE];
extern uint16 TempIndexBuffer[TEMPBUFFERINDEXSIZE];

struct StrAssoc
{
	const char *key;
	int val;

	static int get(StrAssoc *desc, const char *key);
};

struct DatDesc
{
	char name[5];
	void (*handler)(char *line);

	static void *get(DatDesc *desc, const char *name);
};

class CFileLoader
{
	static DatDesc ideDesc[];
	static DatDesc iplDesc[];
	static DatDesc zoneDesc[];
public:

	static char *LoadLine(FileHandle f);
	static char *NextLine(char *line);
	static char *NextDelim(char *line);
	static char *NextDelimNL(char *line);
	static char *SkipDelim(char *line);

	static void LoadLevel(const char *filename);
	static void LoadObjectTypes(const char *filename);
	static void LoadScene(const char *filename);
	static void LoadMapZones(const char *filename);
	static void LoadDataFile(const char *filename, DatDesc *desc);
	static void LoadCollisionModel(CColModel *colmodel, rw::uint8 *buf);
	static void LoadCollisionFile(const char *filename);

	static void LoadNothing(char *line) {}

	// IDE
	static void LoadObject(char *line);
	static void LoadTimeObject(char *line);
	static void LoadClumpObject(char *line);
	static void LoadVehicleObject(char *line);
	static void LoadPedObject(char *line);
	static void LoadPathLine(char *line);
	 static int  LoadPathHeader(char *line);
	 static void LoadPedPathNode(char *line, int id, int i);
	 static void LoadCarPathNode(char *line, int id, int i);
	static void Load2dEffect(char *line);

	// IPL
	static void LoadObjectInstance(char *line);
	static void LoadZone(char *line);
	static void LoadCullZone(char *line);
	static void LoadPickup(char *line);

	// ZONE
	static void LoadMapZone(char *line);

	// models
	static bool LoadAtomicFile(rw::Stream *stream, int id);
	static bool LoadClumpFile(rw::Stream *stream, int id);
	static void LoadClumpFile(const char *filename);
	static void LoadModelFile(const char *filename);

	// textures
	static rw::TexDictionary *LoadTexDictionary(const char *filename);
	static void AddTexDictionaries(rw::TexDictionary *dst,
	                               rw::TexDictionary *src);
};

class CPedType
{
public:
	enum {
		Player1, Player2, Player3, Player4,
		CivMale, CivFemale,
		Cop,
		Gang1, Gang2, Gang3, Gang4, Gang5,
		Gang6, Gang7, Gang8, Gang9,
		Emergency,
		Fireman,
		Criminal,
		Unused1, Unused2,
		Special,
		Prostitute,
		Invalid
	};
	static int FindPedType(const char *name);
};

class CPedStats
{
	struct Stats {
		int id;
		char name[24];
		float fleeDistance;
		float headingChangeRate;
		uchar fear;
		uchar temper;
		uchar lawfullness;
		uchar sexiness;
		float attackStrength;
		float defendWeakness;
		uint16 flags;

		Stats(void);
	};
	static Stats *ms_apPedStats[NUMPEDSTATS];
public:
	static void Initialise(void);
	static void LoadPedStats(void);
	static int  GetPedStatType(const char *name);
};

#include "HandlingDataMgr.h"

// Debug things
extern rw::Camera debugCamState;	// copy of the camera state at some point in time
void DrawDebugFrustum(void);
