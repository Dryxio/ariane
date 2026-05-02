#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

#include <rw.h>

#include "rwgta.h"

using namespace std;

namespace rw {

int32
findPlatform(Atomic *a)
{
	Geometry *g = a->geometry;
	if(g->instData)
		return g->instData->platform;
	return 0;
}

int32
findPlatform(Clump *c)
{
	FORLIST(lnk, c->atomics){
		Geometry *g = Atomic::fromClump(lnk)->geometry;
		if(g->instData)
			return g->instData->platform;
	}
	return 0;
}

void
switchPipes(Atomic *a, int32 platform)
{
	if(a->pipeline && a->pipeline->platform != platform){
		uint32 plgid = a->pipeline->pluginID;
		switch(plgid){
		// assume default pipe won't be attached explicitly
		case ID_SKIN:
			a->pipeline = skinGlobals.pipelines[platform];
			break;
		case ID_MATFX:
			a->pipeline = matFXGlobals.pipelines[platform];
			break;
		}
	}
}

void
switchPipes(Clump *c, int32 platform)
{
	FORLIST(lnk, c->atomics){
		Atomic *a = Atomic::fromClump(lnk);
		switchPipes(a, platform);
	}
}

}

typedef rw::int32 int32;
typedef rw::int16 int16;
typedef rw::int8 int8;
typedef rw::uint32 uint32;
typedef rw::uint16 uint16;
typedef rw::uint8 uint8;
typedef rw::float32 float32;

/* broken NTL dragonball online plugin - we have no better place to do this than here */
static rw::Stream *ntlread(rw::Stream *stream, int32, void*, int32, int32) { return stream; }
static rw::Stream *ntlwrite(rw::Stream *stream, int32, void*, int32, int32) { return stream; }
static int32 ntlsize(void*, int32, int32) { return 0; }
static void registerNTLmaterialPlugin(void)
{
	rw::Material::registerPlugin(0x0, 0x177, nil, nil, nil);
	rw::Material::registerPluginStream(0x177, ntlread, ntlwrite, ntlsize);
}

namespace gta {

void
attachPlugins(void)
{
	rw::ps2::registerPDSPlugin(40);
	rw::ps2::registerPluginPDSPipes();
	gta::registerPDSPipes();

	gta::registerXboxPipes();

	rw::registerMeshPlugin();
	rw::registerNativeDataPlugin();
	rw::registerAtomicRightsPlugin();
	rw::registerMaterialRightsPlugin();
	rw::xbox::registerVertexFormatPlugin();
	rw::registerSkinPlugin();
	rw::registerUserDataPlugin();	// not GTA
	rw::registerHAnimPlugin();
	gta::registerNodeNamePlugin();
	rw::registerMatFXPlugin();
	rw::registerUVAnimPlugin();
	rw::ps2::registerADCPlugin();

	gta::registerExtraNormalsPlugin();
	gta::registerExtraVertColorPlugin();
	gta::registerEnvSpecPlugin();
	gta::registerBreakableModelPlugin();
	gta::registerCollisionPlugin();
	gta::register2dEffectPlugin();
	gta::registerPipelinePlugin();

	registerNTLmaterialPlugin();
}

//
// Frame
//

// Node Name

int32 nodeNameOffset;

static void*
createNodeName(void *object, int32 offset, int32)
{
	char *name = PLUGINOFFSET(char, object, offset);
	name[0] = '\0';
	return object;
}

static void*
copyNodeName(void *dst, void *src, int32 offset, int32)
{
	char *dstname = PLUGINOFFSET(char, dst, offset);
	char *srcname = PLUGINOFFSET(char, src, offset);
	strncpy(dstname, srcname, 23);
	return dst;
}

static void*
destroyNodeName(void *object, int32, int32)
{
	return object;
}

static rw::Stream*
readNodeName(rw::Stream *stream, int32 len, void *object, int32 offset, int32)
{
	char *name = PLUGINOFFSET(char, object, offset);
	stream->read8(name, len);
	name[len] = '\0';
	//printf("%s\n", name);
	return stream;
}

static rw::Stream*
writeNodeName(rw::Stream *stream, int32 len, void *object, int32 offset, int32)
{
	char *name = PLUGINOFFSET(char, object, offset);
	stream->write8(name, len);
	return stream;
}

static int32
getSizeNodeName(void *object, int32 offset, int32)
{
	char *name = PLUGINOFFSET(char, object, offset);
	int32 len = strlen(name);
	return len > 0 ? len : 0;
}


void
registerNodeNamePlugin(void)
{
	nodeNameOffset = rw::Frame::registerPlugin(24, ID_NODENAME,
	                                       createNodeName,
	                                       destroyNodeName,
	                                       copyNodeName);
	rw::Frame::registerPluginStream(ID_NODENAME,
	                            readNodeName,
	                            writeNodeName,
	                            getSizeNodeName);
}

char*
getNodeName(rw::Frame *f)
{
	return PLUGINOFFSET(char, f, nodeNameOffset);
}

//
// Geometry
//

// Breakable Model

int32 breakableOffset;

static void*
createBreakableModel(void *object, int32 offset, int32)
{
	*PLUGINOFFSET(uint8*, object, offset) = 0;
	return object;
}

static void*
destroyBreakableModel(void *object, int32 offset, int32)
{
	uint8 *p = *PLUGINOFFSET(uint8*, object, offset);
	delete[] p;
	return object;
}

static rw::Stream*
readBreakableModel(rw::Stream *stream, int32, void *object, int32 o, int32)
{
	uint32 header[13];
	uint32 hasBreakable = stream->readU32();
	if(hasBreakable == 0)
		return stream;
	stream->read8(header, 13*4);
	uint32 size = header[1]*(12+8+4) + header[5]*(6+2) +
	              header[8]*(32+32+12);
	uint8 *p = new uint8[sizeof(Breakable)+size];
	Breakable *breakable = (Breakable*)p;
	*PLUGINOFFSET(Breakable*, object, o) = breakable;
	breakable->position     = header[0];
	breakable->numVertices  = header[1];
	breakable->numFaces     = header[5];
	breakable->numMaterials = header[8];
	p += sizeof(Breakable);
	stream->read8(p, size);
	breakable->vertices = (float*)p;
	p += breakable->numVertices*12;
	breakable->texCoords = (float*)p;
	p += breakable->numVertices*8;
	breakable->colors = (uint8*)p;
	p += breakable->numVertices*4;
	breakable->faces = (uint16*)p;
	p += breakable->numFaces*6;
	breakable->matIDs = (uint16*)p;
	p += breakable->numFaces*2;
	breakable->texNames = (char(*)[32])p;
	p += breakable->numMaterials*32;
	breakable->maskNames = (char(*)[32])p;
	p += breakable->numMaterials*32;
	breakable->surfaceProps = (float32(*)[3])p;
	return stream;
}

static rw::Stream*
writeBreakableModel(rw::Stream *stream, int32, void *object, int32 o, int32)
{
	uint32 header[13];
	Breakable *breakable = *PLUGINOFFSET(Breakable*, object, o);
	uint8 *p = (uint8*)breakable;
	if(breakable == nil){
		stream->writeU32(0);
		return stream;
	}
	stream->writeU32(1);
	memset((char*)header, 0, 13*4);
	header[0] = breakable->position;
	header[1] = breakable->numVertices;
	header[5] = breakable->numFaces;
	header[8] = breakable->numMaterials;
	stream->write8(header, 13*4);
	p += sizeof(Breakable);
	stream->write8(p, breakable->numVertices*(12+8+4) +
	                       breakable->numFaces*(6+2) +
	                       breakable->numMaterials*(32+32+12));
	return stream;
}

static int32
getSizeBreakableModel(void *object, int32 offset, int32)
{
	Breakable *breakable = *PLUGINOFFSET(Breakable*, object, offset);
	if(breakable == nil)
		return 0; //4;
	return 56 + breakable->numVertices*(12+8+4) +
	            breakable->numFaces*(6+2) +
	            breakable->numMaterials*(32+32+12);
}

void
registerBreakableModelPlugin(void)
{
	breakableOffset = rw::Geometry::registerPlugin(sizeof(Breakable*),
	                                           ID_BREAKABLE,
	                                           createBreakableModel,
	                                           destroyBreakableModel, nil);
	rw::Geometry::registerPluginStream(ID_BREAKABLE,
	                               readBreakableModel,
	                               writeBreakableModel,
	                               getSizeBreakableModel);
}

// Extra normals

int32 extraNormalsOffset;

rw::V3d*
allocateExtraNormals(rw::Geometry *g)
{
	rw::V3d **extraNormals = PLUGINOFFSET(rw::V3d*, g, extraNormalsOffset);
	rwFree(*extraNormals);
	*extraNormals = rwNewT(rw::V3d, g->numVertices, 0);
	return *extraNormals;
}

void
freeExtraNormals(rw::Geometry *g)
{
	rw::V3d **extranormals = PLUGINOFFSET(rw::V3d*, g, extraNormalsOffset);
	rwFree(*extranormals);
	*extranormals = nil;
}

rw::V3d*
getExtraNormals(rw::Geometry *g)
{
	return *PLUGINOFFSET(rw::V3d*, g, extraNormalsOffset);
}

static void*
createExtraNormals(void *object, int32 offset, int32)
{
	*PLUGINOFFSET(rw::V3d*, object, offset) = nil;
	return object;
}

static void*
destroyExtraNormals(void *object, int32 offset, int32)
{
	freeExtraNormals((rw::Geometry*)object);
	return object;
}

static rw::Stream*
readExtraNormals(rw::Stream *stream, int32, void *object, int32 offset, int32)
{
	rw::Geometry *geo = (rw::Geometry*)object;
	rw::V3d *extranormals = allocateExtraNormals(geo);
	stream->read8(extranormals, geo->numVertices*3*4);
	return stream;
}

static rw::Stream*
writeExtraNormals(rw::Stream *stream, int32, void *object, int32 offset, int32)
{
	rw::Geometry *geo = (rw::Geometry*)object;
	rw::V3d *extranormals = *PLUGINOFFSET(rw::V3d*, object, offset);
	assert(extranormals != nil);
	stream->write8(extranormals, geo->numVertices*3*4);
	return stream;
}

static int32
getSizeExtraNormals(void *object, int32 offset, int32)
{
	rw::Geometry *geo = (rw::Geometry*)object;
	if(*PLUGINOFFSET(rw::V3d*, object, offset))
		return geo->numVertices*3*4;
	return 0;
}

void
registerExtraNormalsPlugin(void)
{
	extraNormalsOffset = rw::Geometry::registerPlugin(sizeof(void*),
	                                              ID_EXTRANORMALS,
	                                              createExtraNormals,
	                                              destroyExtraNormals,
	                                              nil);
	rw::Geometry::registerPluginStream(ID_EXTRANORMALS,
	                               readExtraNormals,
	                               writeExtraNormals,
	                               getSizeExtraNormals);
}


// Extra colors

int32 extraVertColorOffset;

void
allocateExtraVertColors(rw::Geometry *g)
{
	ExtraVertColors *colordata =
		PLUGINOFFSET(ExtraVertColors, g, extraVertColorOffset);
	colordata->nightColors = new rw::RGBA[g->numVertices];
	colordata->dayColors = new rw::RGBA[g->numVertices];
	colordata->balance = 1.0f;
}

static void*
createExtraVertColors(void *object, int32 offset, int32)
{
	ExtraVertColors *colordata =
		PLUGINOFFSET(ExtraVertColors, object, offset);
	colordata->nightColors = nil;
	colordata->dayColors = nil;
	colordata->balance = 0.0f;
	return object;
}

static void*
destroyExtraVertColors(void *object, int32 offset, int32)
{
	ExtraVertColors *colordata =
		PLUGINOFFSET(ExtraVertColors, object, offset);
	delete[] colordata->nightColors;
	delete[] colordata->dayColors;
	return object;
}

static rw::Stream*
readExtraVertColors(rw::Stream *stream, int32, void *object, int32 offset, int32)
{
	uint32 hasData;
	ExtraVertColors *colordata =
		PLUGINOFFSET(ExtraVertColors, object, offset);
	hasData = stream->readU32();
	if(!hasData)
		return stream;
	rw::Geometry *geometry = (rw::Geometry*)object;
	colordata->nightColors = new rw::RGBA[geometry->numVertices];
	colordata->dayColors = new rw::RGBA[geometry->numVertices];
	colordata->balance = 1.0f;
	stream->read8(colordata->nightColors, geometry->numVertices*4);
	if(geometry->colors)
		memcpy(colordata->dayColors, geometry->colors,
		       geometry->numVertices*4);
	return stream;
}

static rw::Stream*
writeExtraVertColors(rw::Stream *stream, int32, void *object, int32 offset, int32)
{
	ExtraVertColors *colordata =
		PLUGINOFFSET(ExtraVertColors, object, offset);
	stream->writeU32(colordata->nightColors != nil);
	if(colordata->nightColors){
		rw::Geometry *geometry = (rw::Geometry*)object;
		stream->write8(colordata->nightColors, geometry->numVertices*4);
	}
	return stream;
}

static int32
getSizeExtraVertColors(void *object, int32 offset, int32)
{
	ExtraVertColors *colordata =
		PLUGINOFFSET(ExtraVertColors, object, offset);
	rw::Geometry *geometry = (rw::Geometry*)object;
	if(colordata->nightColors)
		return 4 + geometry->numVertices*4;
	return 0;
}

void
registerExtraVertColorPlugin(void)
{
	extraVertColorOffset = rw::Geometry::registerPlugin(sizeof(ExtraVertColors),
	                                                ID_EXTRAVERTCOLORS,
	                                                createExtraVertColors,
	                                                destroyExtraVertColors,
	                                                nil);
	rw::Geometry::registerPluginStream(ID_EXTRAVERTCOLORS,
	                               readExtraVertColors,
	                               writeExtraVertColors,
	                               getSizeExtraVertColors);
}

rw::RGBA*
getExtraVertColors(rw::Atomic *a)
{
	return PLUGINOFFSET(gta::ExtraVertColors, a->geometry, gta::extraVertColorOffset)->nightColors;
}

// Environment mat

int32 envMatOffset;

static void*
createEnvMat(void *object, int32 offset, int32)
{
	*PLUGINOFFSET(EnvMat*, object, offset) = nil;
	return object;
}

static void*
destroyEnvMat(void *object, int32 offset, int32)
{
	EnvMat **envmat = PLUGINOFFSET(EnvMat*, object, offset);
	delete *envmat;
	*envmat = nil;
	return object;
}

static void*
copyEnvMat(void *dst, void *src, int32 offset, int32)
{
	EnvMat *srcenv = *PLUGINOFFSET(EnvMat*, src, offset);
	if(srcenv == nil)
		return dst;
	EnvMat *dstenv = new EnvMat;
	dstenv->scaleX = srcenv->scaleX;
	dstenv->scaleY = srcenv->scaleY;
	dstenv->transScaleX = srcenv->transScaleX;
	dstenv->transScaleY = srcenv->transScaleY;
	dstenv->shininess = srcenv->shininess;
	dstenv->texture = nil;
	*PLUGINOFFSET(EnvMat*, dst, offset) = dstenv;
	return dst;
}

struct EnvStream {
	float scaleX, scaleY;
	float transScaleX, transScaleY;
	float shininess;
	int32 zero;
};

static rw::Stream*
readEnvMat(rw::Stream *stream, int32, void *object, int32 offset, int32)
{
	EnvStream buf;
	EnvMat *env = new EnvMat;
	*PLUGINOFFSET(EnvMat*, object, offset) = env;
	stream->read8(&buf, sizeof(buf));
	env->scaleX = (int8)(buf.scaleX*8.0f);
	env->scaleY = (int8)(buf.scaleY*8.0f);
	env->transScaleX = (int8)(buf.transScaleX*8.0f);
	env->transScaleY = (int8)(buf.transScaleY*8.0f);
	env->shininess = (uint8)(buf.shininess*255.0f);
	env->texture = nil;
	return stream;
}

static rw::Stream*
writeEnvMat(rw::Stream *stream, int32, void *object, int32 offset, int32)
{
	EnvStream buf;
	EnvMat *env = *PLUGINOFFSET(EnvMat*, object, offset);
	buf.scaleX = env->scaleX/8.0f;
	buf.scaleY = env->scaleY/8.0f;
	buf.transScaleX = env->transScaleX/8.0f;
	buf.transScaleY = env->transScaleY/8.0f;
	buf.shininess = env->shininess/255.0f;
	buf.zero = 0;
	stream->write8(&buf, sizeof(buf));
	return stream;
}

static int32
getSizeEnvMat(void *object, int32 offset, int32)
{
	EnvMat *env = *PLUGINOFFSET(EnvMat*, object, offset);
	return env ? (int)sizeof(EnvStream) : 0;
}

EnvMat*
getEnvMat(rw::Material *mat)
{
	return *PLUGINOFFSET(EnvMat*, mat, envMatOffset);
}

// Specular mat

int32 specMatOffset;

static void*
createSpecMat(void *object, int32 offset, int32)
{
	*PLUGINOFFSET(SpecMat*, object, offset) = nil;
	return object;
}

static void*
destroySpecMat(void *object, int32 offset, int32)
{
	SpecMat **specmat = PLUGINOFFSET(SpecMat*, object, offset);
	if(*specmat == nil)
		return object;
	if((*specmat)->texture)
		(*specmat)->texture->destroy();
	delete *specmat;
	*specmat = nil;
	return object;
}

static void*
copySpecMat(void *dst, void *src, int32 offset, int32)
{
	SpecMat *srcspec = *PLUGINOFFSET(SpecMat*, src, offset);
	if(srcspec == nil)
		return dst;
	SpecMat *dstspec = new SpecMat;
	*PLUGINOFFSET(SpecMat*, dst, offset) = dstspec;
	dstspec->specularity = srcspec->specularity;
	dstspec->texture = srcspec->texture;
	dstspec->texture->refCount++;
	return dst;
}

struct SpecStream {
	float specularity;
	char texname[24];
};

static rw::Stream*
readSpecMat(rw::Stream *stream, int32, void *object, int32 offset, int32)
{
	SpecStream buf;
	SpecMat *spec = new SpecMat;
	*PLUGINOFFSET(SpecMat*, object, offset) = spec;
	stream->read8(&buf, sizeof(buf));
	spec->specularity = buf.specularity;
	spec->texture = rw::Texture::create(nil);
	strncpy(spec->texture->name, buf.texname, 24);
	return stream;
}

static rw::Stream*
writeSpecMat(rw::Stream *stream, int32, void *object, int32 offset, int32)
{
	SpecStream buf;
	SpecMat *spec = *PLUGINOFFSET(SpecMat*, object, offset);
	buf.specularity = spec->specularity;
	strncpy(buf.texname, spec->texture->name, 24);
	stream->write8(&buf, sizeof(buf));
	return stream;
}

static int32
getSizeSpecMat(void *object, int32 offset, int32)
{
	SpecMat *spec = *PLUGINOFFSET(SpecMat*, object, offset);
	return spec ? (int)sizeof(SpecStream) : 0;
}

SpecMat*
getSpecMat(rw::Material *mat)
{
	return *PLUGINOFFSET(SpecMat*, mat, specMatOffset);
}

void
registerEnvSpecPlugin(void)
{
	envMatOffset = rw::Material::registerPlugin(sizeof(EnvMat*), ID_ENVMAT,
	                                        createEnvMat,
                                                destroyEnvMat,
                                                copyEnvMat);
	rw::Material::registerPluginStream(ID_ENVMAT, readEnvMat,
                                                  writeEnvMat,
                                                  getSizeEnvMat);
	specMatOffset = rw::Material::registerPlugin(sizeof(SpecMat*), ID_SPECMAT,
	                                         createSpecMat,
                                                 destroySpecMat,
                                                 copySpecMat);
	rw::Material::registerPluginStream(ID_SPECMAT, readSpecMat,
                                                   writeSpecMat,
                                                   getSizeSpecMat);
}

// Pipeline

int32 pipelineOffset;

static void*
createPipeline(void *object, int32 offset, int32)
{
	*PLUGINOFFSET(uint32, object, offset) = 0;
	return object;
}

static void*
copyPipeline(void *dst, void *src, int32 offset, int32)
{
	*PLUGINOFFSET(uint32, dst, offset) = *PLUGINOFFSET(uint32, src, offset);
	return dst;
}

static rw::Stream*
readPipeline(rw::Stream *stream, int32, void *object, int32 offset, int32)
{
	*PLUGINOFFSET(uint32, object, offset) = stream->readU32();
//	printf("%x\n", *PLUGINOFFSET(uint32, object, offset));
	return stream;
}

static rw::Stream*
writePipeline(rw::Stream *stream, int32, void *object, int32 offset, int32)
{
	stream->writeU32(*PLUGINOFFSET(uint32, object, offset));
	return stream;
}

static int32
getSizePipeline(void *object, int32 offset, int32)
{
	if(*PLUGINOFFSET(uint32, object, offset))
		return 4;
	return 0;
}

void
registerPipelinePlugin(void)
{
	pipelineOffset = rw::Atomic::registerPlugin(sizeof(uint32), ID_PIPELINE,
	                                        createPipeline,
                                                nil,
                                                copyPipeline);
	rw::Atomic::registerPluginStream(ID_PIPELINE, readPipeline,
	                             writePipeline, getSizePipeline);
}

uint32
getPipelineID(rw::Atomic *atomic)
{
	return *PLUGINOFFSET(uint32, atomic, pipelineOffset);
}

void
setPipelineID(rw::Atomic *atomic, uint32 id)
{
	*PLUGINOFFSET(uint32, atomic, pipelineOffset) = id;
}

void
attachCustomPipelines(rw::Atomic *atomic)
{
	int32 id = getPipelineID(atomic);
	uint32 hasNormals = atomic->geometry->flags & rw::Geometry::NORMALS;

	// Xbox pipes
	switch(id){
	case RSPIPE_XBOX_CustomBuildingDN_PipeID:
		if(hasNormals){
			atomic->pipeline = XboxCustomBuildingDNPipe;
			setPipelineID(atomic, RSPIPE_XBOX_CustomBuildingDN_PipeID);
			break;
		}
		// fallthrough
	case RSPIPE_XBOX_CustomBuilding_PipeID:
		atomic->pipeline = XboxCustomBuildingPipe;
		setPipelineID(atomic, RSPIPE_XBOX_CustomBuilding_PipeID);
		break;

	case RSPIPE_XBOX_CustomBuildingDNEnvMap_PipeID:
		if(hasNormals){
			atomic->pipeline = XboxCustomBuildingDNEnvMapPipe;
			setPipelineID(atomic, RSPIPE_XBOX_CustomBuildingDNEnvMap_PipeID);
			break;
		}
		// fallthrough
	case RSPIPE_XBOX_CustomBuildingEnvMap_PipeID:
		atomic->pipeline = XboxCustomBuildingEnvMapPipe;
		setPipelineID(atomic, RSPIPE_XBOX_CustomBuildingEnvMap_PipeID);
		break;
	}

	// PS2 pipes are already attached by PDS
	// PC pipes don't exist here because we have nothing to uninstance
}

// 2dEffect

int32 twodEffectOffset;

static void*
create2dEffect(void *object, int32 offset, int32)
{
	*PLUGINOFFSET(SpecMat*, object, offset) = nil;
	return object;
}

static void*
destroy2dEffect(void *object, int32 offset, int32)
{
	uint8* buffer = *PLUGINOFFSET(uint8*, object, offset);

	if (buffer != nil) {
		delete[] buffer;
		*PLUGINOFFSET(uint8*, object, offset) = nil;
	}

	return object;
}

static void*
copy2dEffect(void *dst, void *src, int32 offset, int32)
{
	uint8* srcBuffer;
	uint8* destBuffer;
	int32 numEffects;

	srcBuffer = *PLUGINOFFSET(uint8*, src, offset);
	if (srcBuffer == nil)
		return dst;

	numEffects = *(int32*)srcBuffer;

	destBuffer = new uint8[numEffects * sizeof(Effect2d) + 4];
	*PLUGINOFFSET(uint8*, dst, offset) = destBuffer;

	memcpy(destBuffer, srcBuffer, numEffects * sizeof(Effect2d) + 4);
	return dst;
}

static rw::Stream*
read2dEffect(rw::Stream *stream, int32 size, void *object, int32 offset, int32)
{
	Effect2d* effects;
	uint8* buffer;
	int32 numEffects;

	numEffects = stream->readI32();

	if (numEffects == 0) {
		*PLUGINOFFSET(uint8*, object, offset) = nil;
		return stream;
	}

	buffer = new uint8[numEffects * sizeof(Effect2d) + 4];
	effects = (Effect2d*)(buffer + 4);
	*(int32*)buffer = numEffects;

	*PLUGINOFFSET(uint8*, object, offset) = buffer;

	for (int32 i = 0; i < numEffects; i++) {
		Effect2dHeader header;
		PedQueueAttrStream pedqueue;
		Effect2d* effect;

		effect = &effects[i];
		stream->read8(&header, sizeof(header));

		effect->posn = header.pos;
		effect->type = header.type;
		switch (header.type) {
		case ET_LIGHT:
			if (header.size == sizeof(LightAttrFileStream_1)) {
				LightAttrFileStream_1 lightv1;
				stream->read8(&lightv1, sizeof(lightv1));

				effect->attr.l.col = lightv1.col;
				effect->attr.l.lodDist = lightv1.lodDist;
				effect->attr.l.size = lightv1.size;
				effect->attr.l.coronaSize = lightv1.coronaSize;
				effect->attr.l.shadowSize = lightv1.shadowSize;
				effect->attr.l.flags = lightv1.flags + (lightv1.extraFlags << 8);
				effect->attr.l.flashiness = lightv1.flashiness;
				effect->attr.l.reflectionType = lightv1.reflectionType;
				effect->attr.l.lensFlareType = lightv1.lensFlareType;
				effect->attr.l.shadowAlpha = lightv1.shadowAlpha;

				strcpy(effect->attr.l.coronaTex, lightv1.coronaTex);
				effect->attr.l.coronaTex[23] = '\0';

				strcpy(effect->attr.l.shadowTex, lightv1.shadowTex);
				effect->attr.l.shadowTex[23] = '\0';

				effect->attr.l.shadowDepth = lightv1.shadowDepth;
				effect->attr.l.lightDirX = 0;
				effect->attr.l.lightDirY = 0;
				effect->attr.l.lightDirZ = 100;
			}
			else if (header.size == sizeof(LightAttrFileStream_2)) {
				LightAttrFileStream_2 lightv2;
				stream->read8(&lightv2, sizeof(lightv2));

				effect->attr.l.col = lightv2.col;
				effect->attr.l.lodDist = lightv2.lodDist;
				effect->attr.l.size = lightv2.size;
				effect->attr.l.coronaSize = lightv2.coronaSize;
				effect->attr.l.shadowSize = lightv2.shadowSize;
				effect->attr.l.flags = lightv2.flags + (lightv2.extraFlags << 8);
				effect->attr.l.flashiness = lightv2.flashiness;
				effect->attr.l.reflectionType = lightv2.reflectionType;
				effect->attr.l.lensFlareType = lightv2.lensFlareType;
				effect->attr.l.shadowAlpha = lightv2.shadowAlpha;

				strcpy(effect->attr.l.coronaTex, lightv2.coronaTex);
				effect->attr.l.coronaTex[23] = '\0';

				strcpy(effect->attr.l.shadowTex, lightv2.shadowTex);
				effect->attr.l.shadowTex[23] = '\0';

				effect->attr.l.shadowDepth = lightv2.shadowDepth;
				effect->attr.l.lightDirX = lightv2.lightDirX;
				effect->attr.l.lightDirY = lightv2.lightDirY;
				effect->attr.l.lightDirZ = lightv2.lightDirZ;
			}
			break;

		case ET_PARTICLE:
			stream->read8(&effect->attr.p, sizeof(effect->attr.p));
			break;

		case ET_PEDQUEUE:
			stream->read8(&pedqueue, sizeof(pedqueue));

			effect->attr.q.type = pedqueue.type;
			effect->attr.q.queueDir = pedqueue.queueDir;
			effect->attr.q.useDir = pedqueue.useDir;
			effect->attr.q.forwardDir = pedqueue.forwardDir;

			strcpy(effect->attr.q.scriptName, pedqueue.scriptName);
			effect->attr.q.scriptName[7] = '\0';

			effect->attr.q.interest = pedqueue.interest;
			effect->attr.q.lookAt = pedqueue.lookAt;
			effect->attr.q.flags = pedqueue.flags;
			break;

		case ET_INTERIOR:
			stream->read8(&effect->attr.i, sizeof(effect->attr.i));
			break;

		case ET_ENTRYEXIT:
			stream->read8(&effect->attr.e, sizeof(effect->attr.e));
			break;

		case ET_ROADSIGN:
			stream->read8(&effect->attr.rs, sizeof(effect->attr.rs));
			break;

		case ET_TRIGGERPOINT:
			stream->read8(&effect->attr.t, sizeof(effect->attr.t));
			break;

		case ET_COVERPOINT:
			stream->read8(&effect->attr.c, sizeof(effect->attr.c));
			break;

		case ET_ESCALATOR:
			stream->read8(&effect->attr.es, sizeof(effect->attr.es));
			break;
		}
	}

	return stream;
}

static rw::Stream*
write2dEffect(rw::Stream *stream, int32, void *object, int32 offset, int32)
{
	int32 numEffects;
	Effect2d* effects;
	uint8* buffer;

	buffer = *PLUGINOFFSET(uint8*, object, offset);
	numEffects = *(int32*)(buffer);
	effects = (Effect2d*)(buffer + 4);

	stream->write8(&numEffects, sizeof(numEffects));
	for (int32 i = 0; i < numEffects; i++) {
		Effect2d* effect;
		Effect2dHeader header;
		PedQueueAttrStream pedqueue;
		LightAttrFileStream_2 lightattr;

		effect = &effects[i];

		header.pos = effect->posn;
		header.type = effect->type;
		switch (effect->type) {
		case ET_LIGHT: header.size = sizeof(lightattr); break;
		case ET_PARTICLE: header.size = sizeof(effect->attr.p); break;
		case ET_PEDQUEUE: header.size = sizeof(pedqueue); break;
		case ET_SUNGLARE: header.size = 0; break;
		case ET_INTERIOR: header.size = sizeof(effect->attr.i); break;
		case ET_ENTRYEXIT: header.size = sizeof(effect->attr.e); break;
		case ET_ROADSIGN: header.size = sizeof(effect->attr.rs); break;
		case ET_TRIGGERPOINT: header.size = sizeof(effect->attr.t); break;
		case ET_COVERPOINT: header.size = sizeof(effect->attr.c); break;
		case ET_ESCALATOR: header.size = sizeof(effect->attr.es); break;
		}

		stream->write8(&header, sizeof(header));

		switch (effect->type) {
		case ET_LIGHT:
			lightattr.col = effect->attr.l.col;
			lightattr.lodDist = effect->attr.l.lodDist;
			lightattr.size = effect->attr.l.size;
			lightattr.coronaSize = effect->attr.l.coronaSize;
			lightattr.shadowSize = effect->attr.l.shadowSize;
			lightattr.flags = effect->attr.l.flags & 255;
			lightattr.extraFlags = (effect->attr.l.flags >> 8) & 255;
			lightattr.flashiness = effect->attr.l.flashiness;
			lightattr.reflectionType = effect->attr.l.reflectionType;
			lightattr.lensFlareType = effect->attr.l.lensFlareType;
			lightattr.shadowAlpha = effect->attr.l.shadowAlpha;

			strcpy(lightattr.coronaTex, effect->attr.l.coronaTex);
			lightattr.coronaTex[23] = '\0';

			strcpy(lightattr.shadowTex, effect->attr.l.shadowTex);
			lightattr.shadowTex[23] = '\0';

			lightattr.shadowDepth = effect->attr.l.shadowDepth;
			lightattr.lightDirX = effect->attr.l.lightDirX;
			lightattr.lightDirY = effect->attr.l.lightDirY;
			lightattr.lightDirZ = effect->attr.l.lightDirZ;

			stream->write8(&lightattr, sizeof(lightattr));
			break;

		case ET_PARTICLE:
			stream->write8(&effect->attr.p, sizeof(effect->attr.p));
			break;

		case ET_PEDQUEUE:
			pedqueue.type = effect->attr.q.type;
			pedqueue.queueDir = effect->attr.q.queueDir;
			pedqueue.useDir = effect->attr.q.useDir;
			pedqueue.forwardDir = effect->attr.q.forwardDir;

			strcpy(pedqueue.scriptName, effect->attr.q.scriptName);
			pedqueue.scriptName[7] = '\0';

			pedqueue.interest = effect->attr.q.interest;
			pedqueue.lookAt = effect->attr.q.lookAt;
			pedqueue.flags = effect->attr.q.flags;

			stream->write8(&pedqueue, sizeof(pedqueue));
			break;

		case ET_INTERIOR:
			stream->write8(&effect->attr.i, sizeof(effect->attr.i));
			break;

		case ET_ENTRYEXIT:
			stream->write8(&effect->attr.e, sizeof(effect->attr.e));
			break;

		case ET_ROADSIGN:
			stream->write8(&effect->attr.rs, sizeof(effect->attr.rs));
			break;

		case ET_TRIGGERPOINT:
			stream->write8(&effect->attr.t, sizeof(effect->attr.t));
			break;

		case ET_COVERPOINT:
			stream->write8(&effect->attr.c, sizeof(effect->attr.c));
			break;

		case ET_ESCALATOR:
			stream->write8(&effect->attr.es, sizeof(effect->attr.es));
			break;
		}
	}

	return stream;
}

static int32
getSize2dEffect(void *object, int32 offset, int32)
{
	int32 numEffects;
	Effect2d* effects;
	uint8* buffer;

	buffer = *PLUGINOFFSET(uint8*, object, offset);
	if (buffer == nil) return 0;
	
	numEffects = *(int32*)(buffer);
	effects = (Effect2d*)(buffer + 4);

	int32 size = 4;
	for (int32 i = 0; i < numEffects; i++) {
		Effect2d* effect;

		effect = &effects[i];
		size += sizeof(Effect2dHeader);

		switch (effect->type) {
		case ET_LIGHT: size += sizeof(LightAttrFileStream_2); break;
		case ET_PARTICLE: size += sizeof(effect->attr.p); break;
		case ET_PEDQUEUE: size += sizeof(PedQueueAttrStream); break;
		case ET_INTERIOR: size += sizeof(effect->attr.i); break;
		case ET_ENTRYEXIT: size += sizeof(effect->attr.e); break;
		case ET_ROADSIGN: size += sizeof(effect->attr.rs); break;
		case ET_TRIGGERPOINT: size += sizeof(effect->attr.t); break;
		case ET_COVERPOINT: size += sizeof(effect->attr.c); break;
		case ET_ESCALATOR: size += sizeof(effect->attr.es); break;
		}
	}

	return size;
}

int32
getNum2dEffects(rw::Geometry* geom) {
	uint8* buffer = *PLUGINOFFSET(uint8*, geom, twodEffectOffset);
	if (buffer == nil) return nil;

	return *(int32*)buffer;
}

Effect2d*
get2dEffects(rw::Geometry* geom)
{
	uint8* buffer = *PLUGINOFFSET(uint8*, geom, twodEffectOffset);
	if (buffer == nil) return nil;

	return (Effect2d*)(buffer + 4);
}

void
register2dEffectPlugin(void)
{
	twodEffectOffset = rw::Geometry::registerPlugin(sizeof(uint8*), ID_2DEFFECT,
	                                            create2dEffect,
                                                    destroy2dEffect,
                                                    copy2dEffect);
	rw::Geometry::registerPluginStream(ID_2DEFFECT, read2dEffect,
	                               write2dEffect, getSize2dEffect);
}

// Collision

struct SizedData
{
	uint32 size;
	uint8* data;
};

int32 collisionOffset;

static void*
createCollision(void *object, int32 offset, int32)
{
	SizedData *data;
	data = PLUGINOFFSET(SizedData, object, offset);
	data->size = 0;
	data->data = nil;
	return object;
}

static void*
destroyCollision(void *object, int32 offset, int32)
{
	SizedData *data;
	data = PLUGINOFFSET(SizedData, object, offset);
	delete[] data->data;
	data->data = nil;
	data->size = 0;
	return object;
}

static void*
copyCollision(void *dst, void *src, int32 offset, int32)
{
	SizedData *srcdata, *dstdata;
	dstdata = PLUGINOFFSET(SizedData, dst, offset);
	srcdata = PLUGINOFFSET(SizedData, src, offset);
	dstdata->size = srcdata->size;
	if(dstdata->size != 0){
		dstdata->data = new uint8[dstdata->size];
		memcpy(dstdata->data, srcdata->data, dstdata->size);
	}
	return dst;
}

static rw::Stream*
readCollision(rw::Stream *stream, int32 size, void *object, int32 offset, int32)
{
	SizedData *data = PLUGINOFFSET(SizedData, object, offset);
	data->size = size;
	data->data = new uint8[data->size];
	stream->read8(data->data, data->size);
	return stream;
}

static rw::Stream*
writeCollision(rw::Stream *stream, int32, void *object, int32 offset, int32)
{
	SizedData *data = PLUGINOFFSET(SizedData, object, offset);
	stream->write8(data->data, data->size);
	return stream;
}

static int32
getSizeCollision(void *object, int32 offset, int32)
{
	SizedData *data = PLUGINOFFSET(SizedData, object, offset);
	return data->size;
}

void
registerCollisionPlugin(void)
{
	collisionOffset = rw::Clump::registerPlugin(sizeof(SizedData), ID_COLLISION,
	                                        createCollision,
                                                destroyCollision,
                                                copyCollision);
	rw::Clump::registerPluginStream(ID_COLLISION, readCollision,
	                            writeCollision, getSizeCollision);
}

}
