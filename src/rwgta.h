#ifndef _LIBRWGTA_RWGTA_H_
#define _LIBRWGTA_RWGTA_H_

namespace rw {
int32 findPlatform(Atomic *a);
int32 findPlatform(Clump *c);
void switchPipes(Atomic *a, int32 platform);
void switchPipes(Clump *c, int32 platform);
}

namespace gta {

enum
{
	VEND_ROCKSTAR     = 0x0253F2
};

enum
{
	ID_VISIBILITYATOMIC = MAKEPLUGINID(VEND_ROCKSTAR, 0x00),
	ID_VISIBILITYCLUMP  = MAKEPLUGINID(VEND_ROCKSTAR, 0x01),
	ID_VISIBILITYFRAME  = MAKEPLUGINID(VEND_ROCKSTAR, 0x02),
	ID_EXTRANORMALS     = MAKEPLUGINID(VEND_ROCKSTAR, 0xF2),
	ID_PIPELINE         = MAKEPLUGINID(VEND_ROCKSTAR, 0xF3),
	ID_ENVATM           = MAKEPLUGINID(VEND_ROCKSTAR, 0xF4),
	ID_TXDSTORE         = MAKEPLUGINID(VEND_ROCKSTAR, 0xF5),
	ID_SPECMAT          = MAKEPLUGINID(VEND_ROCKSTAR, 0xF6),
	ID_2DEFFECT         = MAKEPLUGINID(VEND_ROCKSTAR, 0xF8),
	ID_EXTRAVERTCOLORS  = MAKEPLUGINID(VEND_ROCKSTAR, 0xF9),
	ID_COLLISION        = MAKEPLUGINID(VEND_ROCKSTAR, 0xFA),
	ID_ANIMBLENDCLUMPSA = MAKEPLUGINID(VEND_ROCKSTAR, 0xFB),
	ID_ENVMAT           = MAKEPLUGINID(VEND_ROCKSTAR, 0xFC),
	ID_BREAKABLE        = MAKEPLUGINID(VEND_ROCKSTAR, 0xFD),
	ID_ANIMBLENDCLUMP   = MAKEPLUGINID(VEND_ROCKSTAR, 0xFD),
	ID_NODENAME         = MAKEPLUGINID(VEND_ROCKSTAR, 0xFE)
};

void attachPlugins(void);

// Node name

extern rw::int32 nodeNameOffset;
void registerNodeNamePlugin(void);
char *getNodeName(rw::Frame *f);

// Breakable model

struct Breakable
{
	rw::uint32 position;
	rw::uint32 numVertices;
	rw::uint32 numFaces;
	rw::uint32 numMaterials;

	rw::float32 *vertices;
	rw::float32 *texCoords;
	rw::uint8   *colors;
	rw::uint16  *faces;
	rw::uint16  *matIDs;
	char    (*texNames)[32];
	char    (*maskNames)[32];
	rw::float32 (*surfaceProps)[3];
};

extern rw::int32 breakableOffset;
void registerBreakableModelPlugin(void);

// Extra normals (only on Xbox)

extern rw::int32 extraNormalsOffset;
void registerExtraNormalsPlugin(void);
rw::V3d *allocateExtraNormals(rw::Geometry *g);
void freeExtraNormals(rw::Geometry *g);
rw::V3d *getExtraNormals(rw::Geometry *g);

// Extra vert colors (not on Xbox)

struct ExtraVertColors
{
	rw::RGBA *nightColors;
	rw::RGBA *dayColors;
	float balance;
};

extern rw::int32 extraVertColorOffset;
void allocateExtraVertColors(rw::Geometry *g);
void registerExtraVertColorPlugin(void);
rw::RGBA *getExtraVertColors(rw::Atomic *a);

// Environment mat

struct EnvMat
{
	rw::int8 scaleX, scaleY;
	rw::int8 transScaleX, transScaleY;
	rw::uint8 shininess;
	rw::Texture *texture;

	float getScaleX(void) { return scaleX/8.0f; }
	float getScaleY(void) { return scaleY/8.0f; }
	float getTransScaleX(void) { return transScaleX/8.0f; }
	float getTransScaleY(void) { return transScaleY/8.0f; }
	float getShininess(void) { return shininess/255.0f; }
};

extern rw::int32 envMatOffset;
EnvMat *getEnvMat(rw::Material *mat);

// Specular mat

struct SpecMat
{
	float specularity;
	rw::Texture *texture;
};

extern rw::int32 specMatOffset;
SpecMat *getSpecMat(rw::Material *mat);

void registerEnvSpecPlugin(void);

enum
{
	// PS2 pipes
	// building
	PDS_PS2_CustomBuilding_AtmPipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x80),
	PDS_PS2_CustomBuilding_MatPipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x81),
	PDS_PS2_CustomBuildingDN_AtmPipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x82),
	PDS_PS2_CustomBuildingDN_MatPipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x83),
	PDS_PS2_CustomBuildingEnvMap_AtmPipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x8C),
	PDS_PS2_CustomBuildingEnvMap_MatPipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x8D),
	PDS_PS2_CustomBuildingDNEnvMap_AtmPipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x8E),
	PDS_PS2_CustomBuildingDNEnvMap_MatPipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x8F),
	PDS_PS2_CustomBuildingUVA_AtmPipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x90),
	PDS_PS2_CustomBuildingUVA_MatPipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x91),
	PDS_PS2_CustomBuildingDNUVA_AtmPipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x92),
	PDS_PS2_CustomBuildingDNUVA_MatPipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x93),
	// car
	PDS_PS2_CustomCar_AtmPipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x84),
	PDS_PS2_CustomCar_MatPipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x85),
	PDS_PS2_CustomCarEnvMap_AtmPipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x86),
	PDS_PS2_CustomCarEnvMap_MatPipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x87),
//	PDS_PS2_CustomCarEnvMapUV2_AtmPipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x8A),	// this does not exist
	PDS_PS2_CustomCarEnvMapUV2_MatPipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x8B),
	// skin
	PDS_PS2_CustomSkinPed_AtmPipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x88),
	PDS_PS2_CustomSkinPed_MatPipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x89),

	// PC pipes
	RSPIPE_PC_CustomBuilding_PipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x9C),
	RSPIPE_PC_CustomBuildingDN_PipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x98),
	RSPIPE_PC_CustomCarEnvMap_PipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x9A),	// same as XBOX!

	// Xbox pipes
	RSPIPE_XBOX_CustomBuilding_PipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x9E),
	RSPIPE_XBOX_CustomBuildingDN_PipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x96),
	RSPIPE_XBOX_CustomBuildingEnvMap_PipeID = MAKEPIPEID(VEND_ROCKSTAR, 0xA0),
	RSPIPE_XBOX_CustomBuildingDNEnvMap_PipeID = MAKEPIPEID(VEND_ROCKSTAR, 0xA2),
	RSPIPE_XBOX_CustomCarEnvMap_PipeID = MAKEPIPEID(VEND_ROCKSTAR, 0x9A)	// same as PC!
};

// Pipeline

extern rw::int32 pipelineOffset;

void registerPipelinePlugin(void);
rw::uint32 getPipelineID(rw::Atomic *atomic);
void setPipelineID(rw::Atomic *atomic, rw::uint32 id);

extern rw::ObjPipeline *XboxCustomBuildingPipe;
extern rw::ObjPipeline *XboxCustomBuildingDNPipe;
extern rw::ObjPipeline *XboxCustomBuildingEnvMapPipe;
extern rw::ObjPipeline *XboxCustomBuildingDNEnvMapPipe;
void registerXboxPipes(void);

void attachCustomPipelines(rw::Atomic *atomic);

// 2dEffect

enum e2dEffectType {
	ET_LIGHT = 0,
	ET_PARTICLE,
	ET_PEDQUEUE = 3,
	ET_SUNGLARE,
	ET_INTERIOR,
	ET_ENTRYEXIT,
	ET_ROADSIGN,
	ET_TRIGGERPOINT,
	ET_COVERPOINT,
	ET_ESCALATOR
};

enum eFlashiness {
	FL_CONSTANT = 0,
	FL_RANDOM,
	FL_RANDOM_OVERRIDE_IF_WET,
	FL_ONCE_SECOND,
	FL_TWICE_SECOND,
	FL_FIVE_SECOND,
	FL_RANDOM_FLASHINESS,
	FL_OFF,
	FL_RED_LIGHTS_FOR_BRIDGE,
	FL_ALARM,
	FL_ON_WHEN_RAINING,
	FL_CYCLE_1,
	FL_CYCLE_2,
	FL_CYCLE_3
};

enum ePedQueueType {
	QT_ATM = 0,
	QT_SEAT,
	QT_BUS_STOP,
	QT_PIZZA,
	QT_SHELTER,
	QT_TRIGGER_SCRIPT,
	QT_LOOK_AT,
	QT_SCRIPTED,
	QT_PARK,
	QT_STEP,
	MAX_NUM_QUEUE_TYPES
};

enum eInteriorType {
	IT_SHOP = 0,
	IT_OFFICE,
	IT_LOUNGE,
	IT_BEDROOM,
	IT_KITCHEN,
	IT_BATHROOM,
	IT_OFFLICENSE,
	IT_HOTELROOM,
	IT_MISC,
	MAX_NUM_INTERIOR_TYPES,
	IT_TESTROOM = 99
};

struct Effect2dHeader {
	rw::V3d pos;
	rw::int32 type;
	rw::int32 size;
};

struct LightAttr {
	rw::RGBA col;
	float lodDist;
	float size; // pointlight range
	float coronaSize;
	float shadowSize;
	rw::uint16 flags;
	rw::uint8 flashiness;
	rw::uint8 reflectionType;
	rw::uint8 lensFlareType;
	rw::uint8 shadowAlpha;
	rw::uint8 shadowDepth;
	rw::int8 lightDirX;
	rw::int8 lightDirY;
	rw::int8 lightDirZ;
	char coronaTex[24];
	char shadowTex[24];
};

// 0x78 bytes; this is an earlier version according to the DWARF headers
// Both are read by the game and fully work
struct LightAttrFileStream_1 {
	rw::RGBA col;
	float lodDist;
	float size;
	float coronaSize;
	float shadowSize;
	rw::uint8 flashiness;
	rw::uint8 reflectionType;
	rw::uint8 lensFlareType;
	rw::uint8 shadowAlpha;
	rw::uint8 flags;
	char coronaTex[24];
	char shadowTex[24];
	rw::uint8 shadowDepth;
	rw::uint8 extraFlags;
};

// 0x80 bytes; final ver
struct LightAttrFileStream_2 {
	rw::RGBA col;
	float lodDist;
	float size;
	float coronaSize;
	float shadowSize;
	rw::uint8 flashiness;
	rw::uint8 reflectionType;
	rw::uint8 lensFlareType;
	rw::uint8 shadowAlpha;
	rw::uint8 flags;
	char coronaTex[24];
	char shadowTex[24];
	rw::uint8 shadowDepth;
	rw::uint8 extraFlags;
	rw::int8 lightDirX;
	rw::int8 lightDirY;
	rw::int8 lightDirZ;
};

struct ParticleAttr {
	char name[24];
};

struct PedQueueAttr {
	rw::V3d queueDir;
	rw::V3d useDir;
	rw::V3d forwardDir;

	rw::uint8 type;
	rw::uint8 interest;
	rw::uint8 lookAt;
	rw::uint8 flags;

	char scriptName[8];
};

struct PedQueueAttrStream {
	rw::int32 type;

	rw::V3d queueDir;
	rw::V3d useDir;
	rw::V3d forwardDir;

	char scriptName[8];

	rw::int32 interest;
	rw::int16 lookAt;
	rw::int16 flags;
};

struct InteriorAttr {
	rw::uint8 type;
	rw::int8 group;

	rw::uint8 width, depth, height;
	rw::int8 door;

	rw::int8 lDoorStart, lDoorEnd;
	rw::int8 rDoorStart, rDoorEnd;
	rw::int8 tDoorStart, tDoorEnd;

	rw::int8 lWindowStart, lWindowEnd;
	rw::int8 rWindowStart, rWindowEnd;
	rw::int8 tWindowStart, tWindowEnd;

	rw::int8 noGoLeft[3];
	rw::int8 noGoBottom[3];
	rw::int8 noGoWidth[3];
	rw::int8 noGoDepth[3];

	rw::uint8 seed;
	rw::uint8 status;

	float rot;
};

struct EntryExitAttr {
	float prot;
	float wx, wy;

	rw::V3d spawn;
	float spawnrot;
	rw::int16 areacode;
	rw::uint8 flags;
	rw::uint8 extracol;

	char title[8];

	rw::uint8 openTime;
	rw::uint8 shutTime;
	rw::uint8 extraFlags;
};

struct RoadsignAttr {
	float width, height;
	float rotX, rotY, rotZ;

	union {
		struct {
			rw::uint16 numLines : 2;
			rw::uint16 numLetters : 2;
			rw::uint16 paletteID : 2;
		};

		rw::uint16 flags;
	};

	char text[4][16];
};

struct TriggerPointAttr {
	rw::int32 index;
};

struct CoverPointAttr {
	float dirOfCoverX, dirOfCoverY;
	rw::int8 usage;
};

struct EscalatorAttr {
	rw::V3d coords[3];
	bool goingUp;
};

struct Effect2d {
	rw::V3d posn;
	rw::uint8 type;

	union {
		LightAttr l;
		ParticleAttr p;
		PedQueueAttr q;
		InteriorAttr i;
		EntryExitAttr e;
		RoadsignAttr rs;
		TriggerPointAttr t;
		CoverPointAttr c;
		EscalatorAttr es;
	} attr;
};

extern rw::int32 twodEffectOffset;
rw::int32 getNum2dEffects(rw::Geometry* geom);
Effect2d* get2dEffects(rw::Geometry* geom);

void register2dEffectPlugin(void);

// Collision

extern rw::int32 collisionOffset;

void registerCollisionPlugin(void);

// PDS pipes

struct SaVert : rw::ps2::Vertex {
	rw::RGBA   c1;
};
void insertSAVertex(rw::Geometry *geo, rw::int32 i, rw::uint32 mask, SaVert *v);
rw::int32 findSAVertex(rw::Geometry *g, rw::uint32 flags[], rw::uint32 mask, SaVert *v);

void registerPDSPipes(void);


//
// Colour code pipe for picking and highlighting
//

extern bool renderColourCoded;
extern rw::RGBA colourCode;
rw::ObjPipeline *makeColourCodePipeline(void);
rw::int32 GetColourCode(int x, int y);
int GetColourCodesInRect(int x, int y, int w, int h, rw::int32 *out, int maxOut);

//
// Leeds building pipe
//

extern rw::RGBAf leedsPipe_amb;
extern rw::RGBAf leedsPipe_emiss;
extern rw::int32 leedsPipe_platformSwitch;	// 0 - psp, 1 - ps2, 2 - mobile
extern rw::ObjPipeline *leedsPipe;

void MakeLeedsPipe(void);
#ifdef RW_D3D9
void leedsRenderCB(rw::Atomic *atomic, rw::d3d9::InstanceDataHeader *header);
#else
#ifdef RWDEVICE
void leedsRenderCB(rw::Atomic *atomic, rw::RWDEVICE::InstanceDataHeader *header);
#endif
#endif

}

#endif
