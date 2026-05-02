enum {
	TEX_IDENT  = 0x00746578,
	MDL_IDENT  = 0x006D646C,
	WRLD_IDENT = 0x57524C44,
	GTAG_IDENT = 0x47544147,
	COL2_IDENT = 0x636F6C32,
};

typedef uint16 float16;

float halfFloatToFloat(float16 half);

struct RslObject;
struct RslObjectHasNode;
struct RslElementGroup;        // Clump
struct RslElement;             // Atomic
struct RslNode;                // Frame
struct RslNativeGeometry;      // unofficial name
struct RslNativeMesh;	       // unofficial name
struct RslGeometry;
struct RslSkin;
struct RslMaterial;
struct RslTAnimTree;           // HAnimHierarchy
struct RslTAnimNode;           // HAnimNode
struct RslPS2ResEntryHeader;   // unofficial name
struct RslPS2InstanceData;     // unofficial name
struct RslTexList;             // TexDictionary
struct RslTexture;

typedef RslNode *(*RslNodeCallBack)(RslNode *frame, void *data);
typedef RslElementGroup *(*RslElementGroupCallBack)(RslElementGroup *clump, void *data);
typedef RslElement *(*RslElementCallBack)(RslElement *atomic, void *data);
typedef RslMaterial *(*RslMaterialCallBack)(RslMaterial *material, void *data);
typedef RslTexture *(*RslTextureCallBack)(RslTexture *texture, void *pData);

struct RslV3
{
	float32 x, y, z;
};

struct RslMatrix
{
	RslV3 right;
	float rightw;
	RslV3 up;
	float upw;
	RslV3 at;
	float atw;
	RslV3 pos;
	float posw;
};

void RslMatrixSetIdentity(RslMatrix *matrix);

struct RslLLLink
{
	RslLLLink *next;
	RslLLLink *prev;
};

#define rslLLLinkGetData(linkvar,type,entry)                           \
    ((type*)(((uint8*)(linkvar))-offsetof(type,entry)))
#define rslLLLinkGetNext(linkvar)                                      \
    ((linkvar)->next)
#define rslLLLinkGetPrevious(linkvar)                                  \
    ((linkvar)->prev)
#define rslLLLinkInitialize(linkvar)                                   \
    ((linkvar)->prev = (RslLLLink*)NULL,                               \
     (linkvar)->next = (RslLLLink*)NULL)
#define rslLLLinkAttached(linkvar)                                     \
    ((linkvar)->next)

struct RslLinkList
{
	RslLLLink link;
};

#define rslLinkListInitialize(list)                                    \
    ((list)->link.next = ((RslLLLink*)(list)),                         \
     (list)->link.prev = ((RslLLLink*)(list)))
#define rslLinkListEmpty(list)                                         \
    (((list)->link.next) == (&(list)->link))
#define rslLinkListAddLLLink(list, linkvar)                            \
    ((linkvar)->next = (list)->link.next,                              \
     (linkvar)->prev = (&(list)->link),                                \
     ((list)->link.next)->prev = (linkvar),                            \
     (list)->link.next = (linkvar) )
#define rslLinkListRemoveLLLink(linkvar)                               \
    (((linkvar)->prev)->next = (linkvar)->next,                        \
     ((linkvar)->next)->prev = (linkvar)->prev)
#define rslLinkListGetFirstLLLink(list)                                \
    ((list)->link.next)
#define rslLinkListGetLastLLLink(list)                                 \
    ((list)->link.prev)
#define rslLinkListGetTerminator(list)                                 \
    (&((list)->link))

#define RSLFORLIST(_link, _list) \
	for(RslLLLink *_next = nil, *_link = rslLLLinkGetNext(&(_list).link); \
	    _next = rslLLLinkGetNext(_link), (_link) != rslLinkListGetTerminator(&_list); \
	    (_link) = _next)

struct RslObject {
	uint8  type;
	uint8  subType;
	uint8  flags;
	uint8  privateFlags;
	void  *parent;
};

#define rslObjectInitialize(o, t, s)                  \
{                                                     \
    ((RslObject*)(o))->type = (uint8)(t);             \
    ((RslObject*)(o))->subType = (uint8)(s);          \
    ((RslObject*)(o))->flags = 0;                     \
    ((RslObject*)(o))->privateFlags = 0;              \
    ((RslObject*)(o))->parent = NULL;                 \
}

#define rslObjectGetParent(object) (((RslObject*)(object))->parent)
#define rslObjectSetParent(c,p)    (((RslObject*)(c))->parent) = (void*)(p)

struct RslObjectHasNode {
	RslObject   object;
	RslLLLink   lNode;
	void      (*sync)();
};

void rslObjectHasNodeSetNode(RslObjectHasNode *object, RslNode *f);

struct RslRasterPS2 {
#ifdef VCS
	uint32 unk1;
	uint32 unk2;
#endif
	uint8 *data;
	uint32 flags;
};

struct RslRasterPSP {
	uint32 unk1;
	uint8 *data;
	int16  minWidth;
	uint8  logWidth;
	uint8  logHeight;
	uint8  depth;
	uint8  mipmaps;
	uint16 unk2;	// like RW raster format?
};

struct RslPs2StreamRaster {
	uint32 width;
	uint32 height;
	uint32 depth;
	uint32 mipmaps;
	uint32 unused;
};

union RslRaster {
	RslRasterPS2 ps2;
	RslRasterPSP psp;
};

RslRaster *RslCreateRasterPS2(uint32 w, uint32 h, uint32 d, uint32 mipmaps);

struct RslTexList {
	RslObject   object;
	RslLinkList texturesInDict;
	RslLLLink   lInInstance;
};

RslTexList *RslTexListStreamRead(rw::Stream *stream);
RslTexList *RslTexListCreate(void);
RslTexture *RslTexListAddTexture(RslTexList *dict, RslTexture *tex);
RslTexList *RslTexListForAllTextures(RslTexList *dict, RslTextureCallBack fpCallBack, void *pData);

struct RslTexture {
	RslRaster        *raster;
	RslTexList       *dict;
	RslLLLink         lInDictionary;
	char              name[32];
	char              mask[32];
};

RslTexture *RslTextureCreate(RslRaster *raster);
void RslTextureDestroy(RslTexture *texture);
RslTexture *RslTextureStreamRead(rw::Stream *stream);
RslTexture *RslReadNativeTexture(rw::Stream *stream);

struct RslNode {
	RslObject         object;
	RslLinkList       objectList;
		         
	RslMatrix         modelling;
	RslMatrix         ltm;
	RslNode          *child;
	RslNode          *next;
	RslNode          *root;

	// RwHAnimNodeExtension
#ifdef LCS
	int32              nodeId;
#else
	int32		nodeId;	// always 0?
	int32		unk;	// 0 or uninitialized?
#endif
	RslTAnimTree      *hier;
	// R* Node name
	char              *name;
	// R* Visibility
	int32              hierId;
// VCS
};

RslNode *RslNodeCreate(void);
RslNode *RslNodeAddChild(RslNode *parent, RslNode *child);
int32 RslNodeCount(RslNode *f);
RslNode *RslNodeForAllChildren(RslNode *frame, RslNodeCallBack callBack, void *data);

struct rslNodeList
{
	RslNode **frames;
	int32 numNodes;
};

void rslNodeListStreamRead(rw::Stream *stream, rslNodeList *framelist);
void rslNodeListInitialize(rslNodeList *frameList, RslNode *root);

struct RslElementGroup {
	RslObject   object;
	RslLinkList atomicList;
};

#define RslElementGroupGetNode(_clump)                                    \
    ((RslNode*)rslObjectGetParent(_clump))

#define RslElementGroupSetNode(_clump, _frame)                            \
    (rslObjectSetParent(_clump, _frame),                            \
     (_clump))

RslElementGroup *RslElementGroupCreate(void);
RslElementGroup *RslElementGroupStreamRead(rw::Stream *stream);
RslElementGroup *RslElementGroupAddElement(RslElementGroup *clump, RslElement *a);
int32 RslElementGroupGetNumElements(RslElementGroup *clump);
RslElementGroup *RslElementGroupForAllElements(RslElementGroup *clump, RslElementCallBack callback, void *pData);

struct RslElement {
	RslObjectHasNode   object;
	RslGeometry       *geometry;
	RslElementGroup   *clump;
	RslLLLink          inElementGroupLink;

	uint32             renderCallBack;
	// CVisibilityComponents
	int16              modelInfoId;
	uint16             visIdFlag;
	// RpSkin
	RslTAnimTree      *hier;
	// what about visibility? matfx?
	// int32              pad;	// 0xAAAAAAAA
};

#define RslElementGetNode(_atomic)                                  \
    ((RslNode*)rslObjectGetParent(_atomic))

RslElement *RslElementCreate(void);
RslElement *RslElementSetNode(RslElement *atomic, RslNode *frame);
RslElement *RslElementStreamRead(rw::Stream *stream, rslNodeList *framelist);

struct RslMaterialList {
	RslMaterial **materials;
	int32         numMaterials;
	int32         space;
};

void rslMaterialListStreamRead(rw::Stream *stream, RslMaterialList *matlist);

struct RslGeometry {
	RslObject       object;
	int16           refCount;
	int16           pad1;

	RslMaterialList matList;

	RslSkin        *skin;
	uint32          pad2;            // 0xAAAAAAAA
};

RslGeometry *RslGeometryCreate(uint32 sz);
RslGeometry *RslGeometryForAllMaterials(RslGeometry *geometry, RslMaterialCallBack fpCallBack, void *pData);

struct RslMatFXEnv {
	RslNode *frame;
	union {
		char       *texname;
		RslTexture *texture;
	};
	float32   intensity;
};

struct RslMatFX {
	union {
		RslMatFXEnv env;
	};
	int32     effectType;
};

struct RslMaterial {
	union {
		char       *texname;
		RslTexture *texture;
	};
	rw::RGBA    color;
	uint32      refCount;
	RslMatFX   *matfx;
};

RslMaterial *RslMaterialCreate(void);
RslMaterial *RslMaterialStreamRead(rw::Stream *stream);

struct RslTAnimNodeInfo {
	int8      id;
	int8      index;
	int8      flags;
	RslNode  *frame;
};

struct RslTAnimTree {
	int32              flags;
	int32              numNodes;
	void              *pCurrentAnim;
	float32            currentTime;
	void              *pNextFrame;
	void             (*pAnimCallBack)();
	void              *pAnimCallBackData;
	float32            animCallBackTime;
	void             (*pAnimLoopCallBack)();
	void              *pAnimLoopCallBackData;
	float32           *pMatrixArray;
	void              *pMatrixArrayUnaligned;
	RslTAnimNodeInfo  *pNodeInfo;
#ifdef LCS
	RslNode           *parentNode;
	int32              maxKeyFrameSize;
	int32              currentKeyFrameSize;
	void             (*keyFrameToMatrixCB)();
	void             (*keyFrameBlendCB)();
	void             (*keyFrameInterpolateCB)();
	void             (*keyFrameAddCB)();
	RslTAnimTree      *parentTree;
	int32              offsetInParent;
	int32              rootParentOffset;
#endif
};

struct RslSkin {
	uint32   numBones;
	uint32   numUsedBones;	// == numBones
	uint8   *usedBones;	// NULL
	float32 *invMatrices;
	int32    numWeights;	// 0
	uint8   *indices;	// NULL
	float32 *weights;	// NULL
	uint32   unk1;          // 0
	uint32   unk2;          // 0
	uint32   unk3;          // 0
	uint32   unk4;          // 0
	uint32   unk5;          // 0
	void    *data;          // NULL
};

RslSkin *RslSkinStreamRead(rw::Stream *stream, RslGeometry *g);

struct sPspGeometry {
	uint32  size;		// of header + data
	uint32  flags;		// PSP VTYPE
	uint32  numStrips;
	uint32  unk1;		// 0?
	float32 bound[4];	// bound AABB for PSP/Mobile
	float32 scale[3];
	int32   numVerts;
	float32 pos[3];
	int32   unk2;
	uint32  offset;		// from beginning of geometry struct to vertices
	float32 unk3;
};
static_assert(sizeof(sPspGeometry) == 0x48, "sPspGeometry: error");

struct sPspGeometryMesh {
	uint32  offset;		// into vertex data
	uint16  numTriangles;
	uint16  matID;
	// This is less certain
	float32 unk1;		// ??
	float32 uvScale[2];	// scale for u and v
	float32 uvClip[4];	// UV clip - U_min, V_min, U_max, V_max coordinates (0.0 to 1.0)
	float32 unk3;		// ??

	uint8 bonemap[8];
};
static_assert(sizeof(sPspGeometryMesh) == 0x30, "sPspGeometryMesh: error");

struct sPs2Geometry {
	float32 bound[4];
	uint32  size;		// and numMeshes
	int32   flags;
	uint16  numVerts;	// how many verts total
	uint16  dmaOffset;	// offset from beginning of struct to DMA data
	uint16  boundBox[6];	// bounding box minimum/maximum
	float32 scale[3];		// mesh/part scale
	float32 pos[3];			// mesh/part XYZ
};

struct sPs2GeometryMesh {
	float32  bound[4];
	float32  uvScale[2];
	int32    unknown;	// has to do with the raster, see 0x39FA8C in VCS SLES file
	uint32   dmaPacket;
	uint16   numTriangles;    // number of triangles this mesh/part has
	int16    matID;			  // material ID's for mesh/part
	float16  min[3];          // bounding box minimum/maximum
	float16  max[3];
};
