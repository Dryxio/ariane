#include "storiesconv.h"

////////////
////////////
//////////// TODO: remove new
////////////
////////////

bool32 RslPSP;

float
halfFloatToFloat(uint16 half)
{
	uint32 sign = half & 0x8000;
	int32 exp = (half >> 10) & 0x1F;
	uint32 mant = half & 0x3FF;
	uint32 f = sign << 16 | (exp + 127 - 15) << 23 | mant << 13;
	return *(float*)&f;
}

#define MAKENAME(x) #x
#define mustFindChunk(s, type, length, version) do{if(!findChunk(s, type, length, version)) panic("couldn't find chunk %s", MAKENAME(type));}while(0)

static void matfxRead(Stream *stream, RslMaterial *mat);
static void hanimRead(Stream *stream, RslNode *f);

void
RslMatrixSetIdentity(RslMatrix *matrix)
{
	memset(matrix, 0, sizeof(RslMatrix));
	matrix->right.x = 1.0f;
	matrix->up.y = 1.0f;
	matrix->at.z = 1.0f;
}

void
rslObjectHasNodeSetNode(RslObjectHasNode *object, RslNode *f)
{
	if(object->object.parent)
		rslLinkListRemoveLLLink(&object->lNode);
	rslObjectSetParent(object, f);
	if(f){
		rslLinkListAddLLLink(&f->objectList, &object->lNode);
		f->root->object.privateFlags |= 1;
		f->object.privateFlags |= 2;
	}
}

RslNode*
RslNodeCreate(void)
{
	RslNode *f = new RslNode;
	rslObjectInitialize(&f->object, 0, 0);
	rslLinkListInitialize(&f->objectList);
	RslMatrixSetIdentity(&f->modelling);
	RslMatrixSetIdentity(&f->ltm);
	f->child = NULL;
	f->next = NULL;
	f->root = f;

	f->nodeId = -1;
	f->hier = NULL;

	f->name = NULL;

	f->hierId = 0;
	return f;
}

RslNode*
RslNodeAddChild(RslNode *parent, RslNode *child)
{
	RslNode *p = (RslNode*)child->object.parent;
	assert(p == NULL);
	child->next = parent->child;
	parent->child = child;
	child->object.parent = parent;
	child->root = parent->root;
	child->root->object.privateFlags |= 1;
	child->object.privateFlags |= 2;
	return parent;
}

int32
RslNodeCount(RslNode *f)
{
	int32 n = 1;
	for(RslNode *c = f->child; c; c = c->next)
		n += RslNodeCount(c);
	return n;
}

RslNode*
RslNodeForAllChildren(RslNode *frame, RslNodeCallBack callBack, void *data)
{
	for(RslNode *child = frame->child;
	    child;
	    child = child->next)
		if(callBack(child, data) == NULL)
			break;
	return frame;
}

struct StreamNode
{
	RslV3 right, up, at, pos;
	int32 parent;
	int32 flags;
};

void
rslNodeListStreamRead(Stream *stream, rslNodeList *framelist)
{
	uint32 length;
	StreamNode strfrm;
	RslNode *f;

	mustFindChunk(stream, ID_STRUCT, NULL, NULL);
	stream->read8(&framelist->numNodes, 4);
	framelist->frames = new RslNode*[framelist->numNodes];
	for(int32 i = 0; i < framelist->numNodes; i++){
		stream->read8(&strfrm, sizeof(strfrm));
		f = RslNodeCreate();
		f->modelling.right = strfrm.right;
		f->modelling.up = strfrm.up;
		f->modelling.at = strfrm.at;
		f->modelling.pos = strfrm.pos;
		framelist->frames[i] = f;
		if(strfrm.parent >= 0)
			RslNodeAddChild(framelist->frames[strfrm.parent], f);
	}
	ChunkHeaderInfo header;
	for(int32 i = 0; i < framelist->numNodes; i++){
		f = framelist->frames[i];
		mustFindChunk(stream, ID_EXTENSION, &length, NULL);
		while(length){
			readChunkHeaderInfo(stream, &header);
			if(header.type == ID_HANIM){
				hanimRead(stream, f);
			}else if(header.type == gta::ID_NODENAME){
				f->name = new char[header.length+1];
				memset(f->name, 0, header.length+1);
				stream->read8(f->name, header.length);
				f->name[header.length] = '\0';
			}else{
				stream->seek(header.length);
			}
			length -= 12 + header.length;
		}
	}
}

static RslNode**
rslNodeListFill(RslNode *f, RslNode **flist)
{
	*flist++ = f;
	if(f->next)
		flist = rslNodeListFill(f->next, flist);
	if(f->child)
		flist = rslNodeListFill(f->child, flist);
	return flist;
}

void
rslNodeListInitialize(rslNodeList *frameList, RslNode *root)
{
	frameList->numNodes = RslNodeCount(root);
	frameList->frames = new RslNode*[frameList->numNodes];
	rslNodeListFill(root, frameList->frames);
}

RslTAnimTree*
RslTAnimTreeCreate(int32 numNodes, uint32 *nodeFlags, int32 *nodeIDs, int32 flags, int32 maxKeySize)
{
	RslTAnimTree *hier = new RslTAnimTree;
	memset(hier, 0, sizeof(RslTAnimTree));
	if(maxKeySize < 0x24)
		maxKeySize = 0x24;
	hier->flags = flags;
	hier->numNodes = numNodes;
#ifdef LCS
	hier->parentNode = 0;
	hier->maxKeyFrameSize = maxKeySize;
	hier->currentKeyFrameSize = 0x24;
#endif

	int32 msz = numNodes*0x40 + 0x3f;
	uint8 *p = new uint8[msz];
	memset(p, 0, msz);
	hier->pMatrixArrayUnaligned = p;
	uintptr ip = (uintptr)p;
	ip &= ~0x3f;
	hier->pMatrixArray = (float32*)ip;

	hier->pNodeInfo = new RslTAnimNodeInfo[numNodes];
	for(int32 i = 0; i < numNodes; i++){
		hier->pNodeInfo[i].id = nodeIDs[i];
		hier->pNodeInfo[i].index = i;
		hier->pNodeInfo[i].flags = nodeFlags[i];
		hier->pNodeInfo[i].frame = NULL;
	}
#ifdef LCS
	hier->parentTree = hier;
#endif
	return hier;
}

static void
hanimRead(Stream *stream, RslNode *f)
{
	int32 version;
	int32 id;
	int32 numNodes;
	int32 flags;
	int32 maxKeySize;
	uint32 *nodeFlags;
	int32 *nodeIDs;

	version = stream->readI32();
	assert(version == 0x100);
	id = stream->readI32();
	f->nodeId = id;
	numNodes = stream->readI32();
	if(numNodes == 0)
		return;

	flags = stream->readI32();
	maxKeySize = stream->readI32();
	nodeFlags = new uint32[numNodes];
	nodeIDs = new int32[numNodes];
	memset(nodeFlags, 0, numNodes*4);
	memset(nodeIDs, 0, numNodes*4);
	for(int32 i = 0; i < numNodes; i++){
		nodeIDs[i] = stream->readI32();
		stream->readI32();
		nodeFlags[i] = stream->readI32();
	}
	f->hier = RslTAnimTreeCreate(numNodes, nodeFlags,
	                             nodeIDs, flags, maxKeySize);
	delete[] nodeFlags;
	delete[] nodeIDs;
}


RslElement*
RslElementCreate(void)
{
	RslElement *a = new RslElement;
	memset(a, 0, sizeof(RslElement));
	rslObjectInitialize(&a->object, 1, 0);
	a->object.object.flags = 5;
	a->object.object.privateFlags |= 1;
	rslObjectHasNodeSetNode(&a->object, NULL);
	return a;
}

RslElement*
RslElementSetNode(RslElement *atomic, RslNode *frame)
{
	rslObjectHasNodeSetNode(&atomic->object, frame);
	return atomic;
}

RslElement*
RslElementStreamRead(Stream *stream, rslNodeList *framelist)
{
	uint32 length;
	int32 buf[4];
	RslElement *a;
	RslGeometry *g;

	mustFindChunk(stream, ID_STRUCT, NULL, NULL);
	stream->read8(buf, 16);
	a = RslElementCreate();
	a->object.object.flags = buf[2];
	mustFindChunk(stream, ID_GEOMETRY, NULL, NULL);

	if(RslPSP){
		sPspGeometry res, *rp;
		stream->read8(&res, sizeof(sPspGeometry));
		g = RslGeometryCreate(res.size);
		rp = (sPspGeometry*)(g+1);
		*rp++ = res;
		stream->read8(rp, res.size-sizeof(sPspGeometry));
	}else{
		sPs2Geometry res, *rp;
		stream->read8(&res, sizeof(sPs2Geometry));
		g = RslGeometryCreate(res.size & 0xFFFFF);
		rp = (sPs2Geometry*)(g+1);
		*rp++ = res;
		stream->read8(rp, (res.size&0xFFFFF)-sizeof(sPs2Geometry));
	}

	rslMaterialListStreamRead(stream, &g->matList);
	a->geometry = g;
	RslElementSetNode(a, framelist->frames[buf[0]]);

	// This is not how it's done in LCS, they got extensions wrong :(
	// Geometry
	ChunkHeaderInfo header;
	mustFindChunk(stream, ID_EXTENSION, &length, NULL);
	while(length){
		readChunkHeaderInfo(stream, &header);
		if(header.type == ID_SKIN){
			g->skin = RslSkinStreamRead(stream, g);
		}else
			stream->seek(header.length);
		length -= 12 + header.length;
	}

	// Element
	mustFindChunk(stream, ID_EXTENSION, &length, NULL);
	while(length){
		readChunkHeaderInfo(stream, &header);
		stream->seek(header.length);
		length -= 12 + header.length;
	}
	return a;
}

RslSkin*
RslSkinStreamRead(Stream *stream, RslGeometry *g)
{
	uint32 info;
	RslSkin *skin = new RslSkin;
	memset(skin, 0, sizeof(RslSkin));
	info = stream->readU32();
	// LCS is different here, doesn't matter
	info &= 0xFF;
	skin->numBones = info;
	skin->numUsedBones = info;
	skin->invMatrices = new float[skin->numBones*16];
	stream->read8(skin->invMatrices, skin->numBones*16*4);
	// TODO: allocate...if we'd really care
	(void)g;
	return skin;
}



RslElementGroup*
RslElementGroupCreate(void)
{
	RslElementGroup *clump = new RslElementGroup;
	rslObjectInitialize(&clump->object, 2, 0);
	rslLinkListInitialize(&clump->atomicList);
	return clump;
}

RslElementGroup*
RslElementGroupStreamRead(Stream *stream)
{
	uint32 version, length;
	int32 buf[3];
	int32 numElements;
	rslNodeList framelist;
	RslElementGroup *clump;

	mustFindChunk(stream, ID_STRUCT, NULL, &version);
	if(version > 0x33000){
		stream->read8(buf, 12);
		numElements = buf[0];
	}else
		stream->read8(&numElements, 4);

	clump = RslElementGroupCreate();
	mustFindChunk(stream, ID_FRAMELIST, NULL, NULL);
	rslNodeListStreamRead(stream, &framelist);
	clump->object.parent = framelist.frames[0];

	for(int32 i = 0; i < numElements; i++){
		mustFindChunk(stream, ID_ATOMIC, NULL, &version);
		RslElement *a = RslElementStreamRead(stream, &framelist);
		RslElementGroupAddElement(clump, a);
	}

	ChunkHeaderInfo header;
	mustFindChunk(stream, ID_EXTENSION, &length, NULL);
	while(length){
		readChunkHeaderInfo(stream, &header);
		stream->seek(header.length);
		length -= 12 + header.length;
	}
	delete[] framelist.frames;
	return clump;
}

RslElementGroup*
RslElementGroupAddElement(RslElementGroup *clump, RslElement *a)
{
	rslLinkListAddLLLink(&clump->atomicList, &a->inElementGroupLink);
	a->clump = clump;
	return clump;
}

RslElementGroup*
RslElementGroupForAllElements(RslElementGroup *clump, RslElementCallBack callback, void *pData)
{
	RslElement *a;
	RSLFORLIST(link, clump->atomicList){
		a = rslLLLinkGetData(link, RslElement, inElementGroupLink);
		if(callback(a, pData) == NULL)
			break;
	}
	return clump;
}

int32
RslElementGroupGetNumElements(RslElementGroup *clump)
{
	int32 n = 0;
	RSLFORLIST(link, clump->atomicList)
		n++;
	return n;
}

RslGeometry*
RslGeometryCreate(uint32 sz)
{
	sz += sizeof(RslGeometry);
	RslGeometry *g = (RslGeometry*)new uint8[sz];
	memset(g, 0, sz);
	rslObjectInitialize(&g->object, 8, 0);
	g->refCount = 1;
	return g;
}

RslGeometry*
RslGeometryForAllMaterials(RslGeometry *geometry, RslMaterialCallBack fpCallBack, void *pData)
{
	for(int32 i = 0; i < geometry->matList.numMaterials; i++)
		if(fpCallBack(geometry->matList.materials[i], pData) == NULL)
			break;
	return geometry;
}

struct RslMaterialChunkInfo
{
	int32             flags;
	RGBA              color;    // used
	int32             unused;
	bool32            textured; // used
	SurfaceProperties surfaceProps;
};

RslMaterial*
RslMaterialCreate(void)
{
	RslMaterial *mat = new RslMaterial;
	mat->texture = NULL;
	mat->color.red = 255;
	mat->color.green = 255;
	mat->color.blue = 255;
	mat->color.alpha = 255;
	mat->refCount = 1;
	mat->matfx = NULL;
	return mat;
}

RslMaterial*
RslMaterialStreamRead(Stream *stream)
{
	uint32 length;
	RslMaterialChunkInfo chunk;
	mustFindChunk(stream, ID_STRUCT, NULL, NULL);
	stream->read8(&chunk, sizeof(chunk));
	RslMaterial *mat = RslMaterialCreate();
	mat->color = chunk.color;
	if(chunk.textured)
		mat->texture = RslTextureStreamRead(stream);

	ChunkHeaderInfo header;
	mustFindChunk(stream, ID_EXTENSION, &length, NULL);
	while(length){
		readChunkHeaderInfo(stream, &header);
		if(header.type == ID_MATFX)
			matfxRead(stream, mat);
		else
			stream->seek(header.length);
		length -= 12 + header.length;
	}
	return mat;
}

void
RslMatFXMaterialSetEffects(RslMaterial *mat, int32 effect)
{
	if(effect != MatFX::NOTHING){
		if(mat->matfx == NULL){
			mat->matfx = new RslMatFX;
			memset(mat->matfx, 0, sizeof(RslMatFX));
		}
		mat->matfx->effectType = effect;
	}else{
		RslMatFX *matfx = mat->matfx;
		if(matfx->env.texture)
			RslTextureDestroy(matfx->env.texture);
		delete matfx;
		mat->matfx = NULL;
	}
}

static void
matfxRead(Stream *stream, RslMaterial *mat)
{
	int32 effect = stream->readI32();
	RslMatFXMaterialSetEffects(mat, effect);
	if(effect == MatFX::BUMPMAP){
		stream->seek(12);
		return;
	}
	RslMatFX *mfx = mat->matfx;
	int32 type = stream->readI32();
	float32 coef;
	int32 fbalpha;
	switch(type){
	case MatFX::ENVMAP:
		coef = stream->readF32();
		fbalpha = stream->readI32();
		(void)fbalpha;
		mfx->env.frame = NULL;
		mfx->env.texture = RslTextureStreamRead(stream);
		mfx->env.intensity = coef;
		break;
	case MatFX::BUMPMAP:
		coef = stream->readF32();
		break;
	}
}

void
rpMaterialListAppendMaterial(RslMaterialList *matlist, RslMaterial *mat)
{
	if(matlist->space <= matlist->numMaterials){
		RslMaterial **mats = matlist->materials;
		matlist->materials = new RslMaterial*[matlist->space+16];
		if(matlist->space)
			memcpy(matlist->materials, mats, matlist->space*sizeof(RslMaterial*));
		matlist->space += 16;
		delete[] mats;
	}
	matlist->materials[matlist->numMaterials++] = mat;
}

void
rslMaterialListStreamRead(Stream *stream, RslMaterialList *matlist)
{
	int32 numMaterials;
	RslMaterial *mat;
	mustFindChunk(stream, ID_MATLIST, NULL, NULL);
	mustFindChunk(stream, ID_STRUCT, NULL, NULL);
	numMaterials = stream->readI32();
	int32 *refs = new int32[numMaterials];
	stream->read8(refs, 4*numMaterials);
	for(int32 i = 0; i < numMaterials; i++){
		assert(refs[i] < 0);
		mustFindChunk(stream, ID_MATERIAL, NULL, NULL);
		mat = RslMaterialStreamRead(stream);
		rpMaterialListAppendMaterial(matlist, mat);
	}
	delete[] refs;
}

RslTexList*
RslTexListCreate(void)
{
	RslTexList *dict = new RslTexList;
	memset(dict, 0, sizeof(RslTexList));
	rslObjectInitialize(&dict->object, 6, 0);
	rslLinkListInitialize(&dict->texturesInDict);
	return dict;
}

RslTexture*
RslTexListAddTexture(RslTexList *dict, RslTexture *tex)
{
	if(tex->dict)
		rslLinkListRemoveLLLink(&tex->lInDictionary);
	tex->dict = dict;
	rslLinkListAddLLLink(&dict->texturesInDict, &tex->lInDictionary);
	return tex;
}

RslTexList*
RslTexListForAllTextures(RslTexList *dict, RslTextureCallBack fpCallBack, void *pData)
{
	RslTexture *t;
	RSLFORLIST(link, dict->texturesInDict){
		t = rslLLLinkGetData(link, RslTexture, lInDictionary);
		if(fpCallBack(t, pData) == NULL)
			break;
	}
	return dict;
}

// TODO!
void
RslTextureDestroy(RslTexture *texture)
{
	delete texture;
}

RslTexture*
RslTextureCreate(RslRaster *raster)
{
	RslTexture *tex = new RslTexture;
	memset(tex, 0, sizeof(RslTexture));
	tex->raster = raster;
	return tex;
}

RslTexture*
RslTextureStreamRead(Stream *stream)
{
	uint32 length;
	RslTexture *tex = RslTextureCreate(NULL);
	mustFindChunk(stream, ID_TEXTURE, NULL, NULL);
	mustFindChunk(stream, ID_STRUCT, NULL, NULL);
	stream->readI32();	// filter addressing
	mustFindChunk(stream, ID_STRING, &length, NULL);
	stream->read8(tex->name, length);
	mustFindChunk(stream, ID_STRING, &length, NULL);
	stream->read8(tex->mask, length);

	ChunkHeaderInfo header;
	mustFindChunk(stream, ID_EXTENSION, &length, NULL);
	while(length){
		readChunkHeaderInfo(stream, &header);
		stream->seek(header.length);
		length -= 12 + header.length;
	}
	return tex;
}

static bool
guessSwizzlingLevel(uint32 w, uint32 h, uint32 d)
{
#ifdef VCS
	return false;
#endif
	// unsure, used on loadscreens
	if(w > 256 || h > 256)
		return false;

	switch(d){
	case 4:
		if(w < 32 || h < 16)
			return false;
		if(w > h*2)	// to catch 64x16
			return false;
		return true;
	case 8:
		if(w < 16 || h < 16)
			return false;
		if(w < 128 && h > w)
			return false;
		return true;
	case 16:
		// not encountered
		return false;
	case 32:
		// very unsure. We only have square textures
		// and how does the swizzling even work for 32 bit?
		if(w < 32)
			return false;
		return true;
	default:
		return false;
	}
}

uint32
guessSwizzling(uint32 w, uint32 h, uint32 d, uint32 mipmaps)
{
	uint32 swiz = 0;
	for(uint32 i = 0; i < mipmaps; i++){
		if(guessSwizzlingLevel(w, h, d))
			swiz |= 1<<i;
		w /= 2;
		h /= 2;
	}
	return swiz;
}

RslRaster*
RslCreateRasterPSP(uint32 w, uint32 h, uint32 d, uint32 mipmaps, uint32 unknown)
{
	RslRasterPSP *r;
	r = new RslRasterPSP;
	uint32 tmp, logw = 0, logh = 0;
	for(tmp = 1; tmp < w; tmp <<= 1)
		logw++;
	for(tmp = 1; tmp < h; tmp <<= 1)
		logh++;
	r->minWidth = 128/d;	// min 16 bytes
	r->logWidth = logw;
	r->logHeight = logh;
	r->depth = d;
	r->mipmaps = mipmaps;
	r->unk2 = unknown;
//	printf("%3d %3d %2d %d %4x\n", w, h, d, mipmaps, unknown);
	return (RslRaster*)r;
}

RslRaster*
RslCreateRasterPS2(uint32 w, uint32 h, uint32 d, uint32 mipmaps)
{
	RslRasterPS2 *r;
	r = new RslRasterPS2;
	uint32 tmp, logw = 0, logh = 0;
	for(tmp = 1; tmp < w; tmp <<= 1)
		logw++;
	for(tmp = 1; tmp < h; tmp <<= 1)
		logh++;
	r->flags = 0;
	r->flags |= logw&0x3F;
	r->flags |= (logh&0x3F)<<6;
	r->flags |= d << 12;
	r->flags |= mipmaps << 20;
	uint32 swiz = guessSwizzling(w, h, d, mipmaps);
	r->flags |= swiz << 24;
	return (RslRaster*)r;
}

RslTexture*
RslReadNativeTexture(Stream *stream)
{
	RslPs2StreamRaster rasterInfo;
	uint32 len;
	uint32 buf[2];
	RslTexture *tex = RslTextureCreate(NULL);
	mustFindChunk(stream, ID_STRUCT, NULL, NULL);
	stream->read8(buf, sizeof(buf));
	assert(buf[0] == 0x00505350); /* "PSP\0" */
	mustFindChunk(stream, ID_STRING, &len, NULL);
	stream->read8(tex->name, len);
	mustFindChunk(stream, ID_STRING, &len, NULL);
	stream->read8(tex->mask, len);
	mustFindChunk(stream, ID_STRUCT, NULL, NULL);
	mustFindChunk(stream, ID_STRUCT, &len, NULL);
	stream->read8(&rasterInfo, sizeof(rasterInfo));
	mustFindChunk(stream, ID_STRUCT, &len, NULL);
	if(RslPSP){
		tex->raster = RslCreateRasterPSP(rasterInfo.width,
			rasterInfo.height, rasterInfo.depth, rasterInfo.mipmaps,
			rasterInfo.unused);
		tex->raster->psp.data = new uint8[len];
		stream->read8(tex->raster->psp.data, len);
	}else{
		tex->raster = RslCreateRasterPS2(rasterInfo.width,
			rasterInfo.height, rasterInfo.depth, rasterInfo.mipmaps);
		tex->raster->ps2.data = new uint8[len];
		stream->read8(tex->raster->ps2.data, len);
	}
	mustFindChunk(stream, ID_EXTENSION, &len, NULL);
	stream->seek(len);
	return tex;
}

RslTexList*
RslTexListStreamRead(Stream *stream)
{
	mustFindChunk(stream, ID_STRUCT, NULL, NULL);
	int32 numTex = stream->readI32();
	RslTexList *txd = RslTexListCreate();
	for(int32 i = 0; i < numTex; i++){
		mustFindChunk(stream, ID_TEXTURENATIVE, NULL, NULL);
		RslTexture *tex = RslReadNativeTexture(stream);
		RslTexListAddTexture(txd, tex);
	}
	return txd;
}
