#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

#include <rw.h>
#include "rwgta.h"

using rw::uint8;
using rw::uint16;
using rw::uint32;
using rw::float32;
using rw::int8;
using rw::int16;
using rw::int32;

namespace gta {

rw::ps2::PipeAttribute saXYZADC = {
	"saXYZADC",
	rw::ps2::AT_V4_16 | rw::ps2::AT_RW
};

rw::ps2::PipeAttribute saUV = {
	"saUV",
	rw::ps2::AT_V2_16 | rw::ps2::AT_RW
};

rw::ps2::PipeAttribute saUV2 = {
	"saUV2",
	rw::ps2::AT_V4_16 | rw::ps2::AT_RW
};

rw::ps2::PipeAttribute saRGBA = {
	"saRGBA",
	rw::ps2::AT_V4_8 | rw::ps2::AT_UNSGN | rw::ps2::AT_RW
};

rw::ps2::PipeAttribute saRGBA2 = {
	"saRGBA2",
	rw::ps2::AT_V4_16 | rw::ps2::AT_UNSGN | rw::ps2::AT_RW
};

rw::ps2::PipeAttribute saNormal = {
	"saNormal",
	rw::ps2::AT_V4_8 | rw::ps2::AT_RW
};

rw::ps2::PipeAttribute saWeights = {
	"saWeights",
	rw::ps2::AT_V4_32 | rw::ps2::AT_RW
};

static bool hasTex2(rw::uint32 id)
{
	return id == PDS_PS2_CustomCarEnvMapUV2_MatPipeID;
}
static bool hasNormals(rw::uint32 id)
{
	return id == PDS_PS2_CustomCar_MatPipeID || id == PDS_PS2_CustomCarEnvMap_MatPipeID || id == PDS_PS2_CustomCarEnvMapUV2_MatPipeID ||
		id == PDS_PS2_CustomSkinPed_MatPipeID ||
		id == PDS_PS2_CustomBuildingEnvMap_MatPipeID || id == PDS_PS2_CustomBuildingDNEnvMap_MatPipeID;
}
static bool hasColors(rw::uint32 id)
{
	return id == PDS_PS2_CustomBuilding_MatPipeID || id == PDS_PS2_CustomBuildingDN_MatPipeID ||
		id == PDS_PS2_CustomBuildingEnvMap_MatPipeID || id == PDS_PS2_CustomBuildingDNEnvMap_MatPipeID ||
		id == PDS_PS2_CustomBuildingUVA_MatPipeID || id == PDS_PS2_CustomBuildingDNUVA_MatPipeID;
}
static bool hasColors2(rw::uint32 id)
{
	return id == PDS_PS2_CustomBuildingDN_MatPipeID || id == PDS_PS2_CustomBuildingDNEnvMap_MatPipeID || id == PDS_PS2_CustomBuildingDNUVA_MatPipeID;
}

static void
saPreCB(rw::ps2::MatPipeline *p, rw::Geometry *geo)
{
	rw::ps2::allocateADC(geo);
	if(hasColors2(p->pluginData) && extraVertColorOffset)
		allocateExtraVertColors(geo);
	if(p->pluginData == PDS_PS2_CustomSkinPed_MatPipeID)
		skinPreCB(p, geo);
}

static void
saPostCB(rw::ps2::MatPipeline *p, rw::Geometry *geo)
{
	skinPostCB(p, geo);
}

rw::int32
findSAVertex(rw::Geometry *g, rw::uint32 flags[], rw::uint32 mask, SaVert *v)
{
	rw::Skin *skin = rw::Skin::get(g);
	rw::float32 *wghts = nil;
	rw::uint8 *inds    = nil;
	if(skin){
		wghts = skin->weights;
		inds = skin->indices;
	}
	rw::V3d *verts = g->morphTargets[0].vertices;
	rw::TexCoords *tex0  = g->texCoords[0];
	rw::TexCoords *tex1  = g->texCoords[1];
	rw::V3d *norms = g->morphTargets[0].normals;
	rw::RGBA *cols0   = g->colors;
	rw::RGBA *cols1   = nil;
	if(extraVertColorOffset)
		cols1 = PLUGINOFFSET(ExtraVertColors, g, extraVertColorOffset)->nightColors;

	for(rw::int32 i = 0; i < g->numVertices; i++){
		rw::uint32 flag = flags ? flags[i] : ~0;
		if(mask & flag & 0x1 && !equal(*verts, v->p))
			goto cont;
		if(mask & flag & 0x10 && !equal(*norms, v->n))
			goto cont;
		if(mask & flag & 0x100 && !equal(*cols0, v->c))
			goto cont;
		if(mask & flag & 0x200 && !equal(*cols1, v->c1))
			goto cont;
		if(mask & flag & 0x1000 && !equal(*tex0, v->t))
			goto cont;
		if(mask & flag & 0x2000 && !equal(*tex1, v->t1))
			goto cont;
		if(mask & flag & 0x10000 &&
		   !(wghts[0] == v->w[0] && wghts[1] == v->w[1] &&
		     wghts[2] == v->w[2] && wghts[3] == v->w[3] &&
		     inds[0] == v->i[0] && inds[1] == v->i[1] &&
		     inds[2] == v->i[2] && inds[3] == v->i[3]))
			goto cont;
		return i;
	cont:
		verts++;
		tex0++;
		tex1++;
		norms++;
		cols0++;
		cols1++;
		wghts += 4;
		inds += 4;
	}
	return -1;
}

void
insertSAVertex(rw::Geometry *geo, rw::int32 i, rw::uint32 mask, SaVert *v)
{
	insertVertex(geo, i, mask, v);
	if(mask & 0x200 && extraVertColorOffset){
		rw::RGBA *cols1 =
		 &PLUGINOFFSET(ExtraVertColors, geo, extraVertColorOffset)->nightColors[i];
		*cols1 = v->c1;
	}
	if(mask & 0x10000 && rw::skinGlobals.geoOffset){
		rw::Skin *skin = rw::Skin::get(geo);
		memcpy(&skin->weights[i*4], v->w, 16);
		memcpy(&skin->indices[i*4], v->i, 4);
	}
}

static void
saUninstanceCB(rw::ps2::MatPipeline *pipe, rw::Geometry *geo, uint32 flags[], rw::Mesh *mesh, uint8 *data[])
{
	uint32 id = pipe->pluginData;
	int16 *verts       = (int16*)data[0];
	int16 *texcoords   = (int16*)data[1];
	uint8 *colors      = (uint8*)data[2];
	int8 *norms        = (int8*)data[id == PDS_PS2_CustomSkinPed_MatPipeID ? 2 : 3];
	uint32 *wghts      = (uint32*)data[3];
	float vertScale = 1.0f/128.0f;
	if(id == PDS_PS2_CustomCar_MatPipeID || id == PDS_PS2_CustomCarEnvMap_MatPipeID ||
	   id == PDS_PS2_CustomCarEnvMapUV2_MatPipeID ||
	   id == PDS_PS2_CustomSkinPed_MatPipeID)
		vertScale = 1.0f/1024.0f;
	uint32 mask = 0x1;	// vertices
	int cinc = 4;
	int tinc = 2;
	if((geo->flags & rw::Geometry::NORMALS) && hasNormals(id))
		mask |= 0x10;
	if((geo->flags & rw::Geometry::PRELIT) && hasColors(id))
		mask |= 0x100;
	if(hasColors2(id)){
		mask |= 0x200;
		cinc *= 2;
	}
	if(geo->numTexCoordSets > 0)
		mask |= 0x1000;
	if(hasTex2(id)){
		if(geo->numTexCoordSets > 1)
			mask |= 0x2000;
		tinc *= 2;
	}
	if(id == PDS_PS2_CustomSkinPed_MatPipeID)
		mask |= 0x10000;
	SaVert v;
	int32 idxstart = 0;
	for(rw::Mesh *m = geo->meshHeader->getMeshes(); m < mesh; m++)
		idxstart += m->numIndices;
	int8 *adc = rw::ps2::getADCbitsForMesh(geo, mesh);
	for(uint32 i = 0; i < mesh->numIndices; i++){
		v.p.x = verts[0]*vertScale;
		v.p.y = verts[1]*vertScale;
		v.p.z = verts[2]*vertScale;
//printf("v %04X %04X %04X %04X\n", verts[0], verts[1], verts[2], verts[3]);
		if(mask & 0x10){
			v.n.x = norms[0]/127.0f;
			v.n.y = norms[1]/127.0f;
			v.n.z = norms[2]/127.0f;
//printf("n %02X %02X %02X\n", norms[0], norms[1], norms[2]);
		}
		if(mask & 0x200){
			v.c.red    = colors[0];
			v.c.green  = colors[2];
			v.c.blue   = colors[4];
			v.c.alpha  = colors[6];
			v.c1.red   = colors[1];
			v.c1.green = colors[3];
			v.c1.blue  = colors[5];
			v.c1.alpha = colors[7];
//printf("c2 %02X %02X %02X %02X %02X %02X %02X %02X\n", colors[0], colors[1], colors[2], colors[3], colors[4], colors[5], colors[6], colors[7]);
		}else if(mask & 0x100){
			v.c.red   = colors[0];
			v.c.green = colors[1];
			v.c.blue  = colors[2];
			v.c.alpha = colors[3];
//printf("c %02X %02X %02X %02X\n", colors[0], colors[1], colors[2], colors[3]);
		}
		if(mask & 0x1000){
			v.t.u = texcoords[0]/4096.0f;
			v.t.v = texcoords[1]/4096.0f;
//printf("t %04X %04X\n", texcoords[0], texcoords[1]);
		}
		if(mask & 0x2000){
			v.t1.u = texcoords[2]/4096.0f;
			v.t1.v = texcoords[3]/4096.0f;
//printf("t2 %04X %04X\n", texcoords[2], texcoords[3]);
		}
		if(mask & 0x10000){
			for(int j = 0; j < 4; j++){
				((uint32*)v.w)[j] = wghts[j] & ~0x3FF;
				v.i[j] = (wghts[j] & 0x3FF) >> 2;
				if(v.i[j]) v.i[j]--;
				if(v.w[j] == 0.0f) v.i[j] = 0;
			}
		}
		int32 idx = findSAVertex(geo, flags, mask, &v);
		if(idx < 0)
			idx = geo->numVertices++;
//printf("INDEX %d\n", idx);
		mesh->indices[i] = idx;
		adc[i] = !!verts[3];
		flags[idx] = mask;
		insertSAVertex(geo, idx, mask, &v);

		verts += 4;
		texcoords += tinc;
		colors += cinc;
		norms += 4;
		wghts += 4;
	}
}

static void
instanceSAPositions(rw::Geometry *g, rw::Mesh *m, int8 *adc, int16 *dst, float32 scale)
{
	rw::V3d *verts = g->morphTargets[0].vertices;
	uint16 j;
	for(uint32 i = 0; i < m->numIndices; i++){
		j = m->indices[i];
		dst[0] = verts[j].x*scale;
		dst[1] = verts[j].y*scale;
		dst[2] = verts[j].z*scale;
		dst[3] = adc ? 0x8000*adc[i] : 0;
		dst += 4;
	}
}

static void
instanceSATex(rw::Geometry *g, rw::Mesh *m, int16 *dst)
{
	rw::TexCoords *tex = g->texCoords[0];
	uint16 j;
	for(uint32 i = 0; i < m->numIndices; i++){
		j = m->indices[i];
		if(tex){
			dst[0] = tex[j].u*4096.0f;
			dst[1] = tex[j].v*4096.0f;
		}else{
			dst[0] = 0;
			dst[1] = 0;
		}
		dst += 2;
	}
}

static void
instanceSADualTex(rw::Geometry *g, rw::Mesh *m, int16 *dst)
{
	rw::TexCoords *tex0 = g->texCoords[0];
	rw::TexCoords *tex1 = g->texCoords[1];
	uint16 j;
	for(uint32 i = 0; i < m->numIndices; i++){
		j = m->indices[i];
		if(tex0){
			dst[0] = tex0[j].u*4096.0f;
			dst[1] = tex0[j].v*4096.0f;
		}else{
			dst[0] = 0;
			dst[1] = 0;
		}
		if(tex1){
			dst[2] = tex1[j].u*4096.0f;
			dst[3] = tex1[j].v*4096.0f;
		}else{
			dst[2] = 0;
			dst[3] = 0;
		}
		dst += 4;
	}
}

static void
instanceSAColors(rw::Geometry *g, rw::Mesh *m, uint8 *dst)
{
	rw::RGBA *c = g->colors;
	uint16 j;
	for(uint32 i = 0; i < m->numIndices; i++){
		j = m->indices[i];
		if(c)
			memcpy(dst, &c[j], 4);
		else
			memset(dst, 0xFF, 4);
		dst += 4;
	}
}

static void
instanceSADualColors(rw::Geometry *g, rw::Mesh *m, uint8 *dst)
{
	rw::RGBA *c0 = g->colors;
	rw::RGBA *c1 =
	 PLUGINOFFSET(ExtraVertColors, g, extraVertColorOffset)->nightColors;
	uint16 j;
	for(uint32 i = 0; i < m->numIndices; i++){
		j = m->indices[i];
		if(c0){
			dst[0] = c0[j].red;
			dst[2] = c0[j].green;
			dst[4] = c0[j].blue;
			dst[6] = c0[j].alpha;
		}else{
			dst[0] = 0xFF;
			dst[2] = 0xFF;
			dst[4] = 0xFF;
			dst[6] = 0xFF;
		}
		if(c1){
			dst[1] = c1[j].red;
			dst[3] = c1[j].green;
			dst[5] = c1[j].blue;
			dst[7] = c1[j].alpha;
		}else{
			dst[1] = 0xFF;
			dst[3] = 0xFF;
			dst[5] = 0xFF;
			dst[7] = 0xFF;
		}
		dst += 8;
	}
}

static void
instanceSANormals(rw::Geometry *g, rw::Mesh *m, int8 *dst)
{
	rw::V3d *norms = g->morphTargets[0].normals;
	uint16 j;
	for(uint32 i = 0; i < m->numIndices; i++){
		j = m->indices[i];
		if(norms){
			dst[0] = norms[j].x*127.0f;
			dst[1] = norms[j].y*127.0f;
			dst[2] = norms[j].z*127.0f;
			dst[3] = 0;
		}else
			memset(dst, 0, 4);
		dst += 4;
	}
}

static void
saInstanceCB(rw::ps2::MatPipeline *pipe, rw::Geometry *g, rw::Mesh *m, uint8 **data)
{
	uint32 id = pipe->pluginData;
	float vertScale = 128.0f;
	if(id == PDS_PS2_CustomCar_MatPipeID || id == PDS_PS2_CustomCarEnvMap_MatPipeID ||
	   id == PDS_PS2_CustomCarEnvMapUV2_MatPipeID ||
	   id == PDS_PS2_CustomSkinPed_MatPipeID)
		vertScale = 1024.0f;
	rw::ps2::ADCData *adc = PLUGINOFFSET(rw::ps2::ADCData, g, rw::ps2::adcOffset);

	for(uint32 i = 0; i < nelem(pipe->attribs); i++){
		rw::ps2::PipeAttribute *a = pipe->attribs[i];
		if(a == &saXYZADC)
			instanceSAPositions(g, m, rw::ps2::getADCbitsForMesh(g, m),
			                    (int16*)data[i], vertScale);
		if(a == &saUV)
			instanceSATex(g, m, (int16*)data[i]);
		if(a == &saUV2)
			instanceSADualTex(g, m, (int16*)data[i]);
		if(a == &saRGBA)
			instanceSAColors(g, m, (uint8*)data[i]);
		if(a == &saRGBA2)
			instanceSADualColors(g, m, (uint8*)data[i]);
		if(a == &saNormal)
			instanceSANormals(g, m, (int8*)data[i]);
		if(a == &saWeights){
			rw::Skin *skin = rw::Skin::get(g);
			rw::ps2::instanceSkinData(g, m, skin, (uint32*)data[i]);
		}
	}
}

void
registerPS2BuildingPipes(void)
{
	rw::Pipeline *pipe;
	rw::ps2::MatPipeline *mpipe;
	uint32 vertCount;

	pipe = rw::ps2::ObjPipeline::create();
	pipe->pluginID = rw::ID_PDS;
	pipe->pluginData = PDS_PS2_CustomBuilding_AtmPipeID;
	rw::ps2::registerPDSPipe(pipe);

	mpipe = rw::ps2::MatPipeline::create();
	mpipe->pluginID = rw::ID_PDS;
	mpipe->pluginData = PDS_PS2_CustomBuilding_MatPipeID;
	mpipe->attribs[0] = &saXYZADC;
	mpipe->attribs[1] = &saUV;
	mpipe->attribs[2] = &saRGBA;
	vertCount = rw::ps2::MatPipeline::getVertCount(rw::ps2::VU_Lights, 3, 3, 2);
	mpipe->setTriBufferSizes(3, vertCount);
	mpipe->vifOffset = mpipe->inputStride*vertCount;
	mpipe->instanceCB = saInstanceCB;
	mpipe->preUninstCB = saPreCB;
	mpipe->uninstanceCB = saUninstanceCB;
	rw::ps2::registerPDSPipe(mpipe);

	pipe = rw::ps2::ObjPipeline::create();
	pipe->pluginID = rw::ID_PDS;
	pipe->pluginData = PDS_PS2_CustomBuildingDN_AtmPipeID;
	rw::ps2::registerPDSPipe(pipe);

	mpipe = rw::ps2::MatPipeline::create();
	mpipe->pluginID = rw::ID_PDS;
	mpipe->pluginData = PDS_PS2_CustomBuildingDN_MatPipeID;
	mpipe->attribs[0] = &saXYZADC;
	mpipe->attribs[1] = &saUV;
	mpipe->attribs[2] = &saRGBA2;
	vertCount = rw::ps2::MatPipeline::getVertCount(rw::ps2::VU_Lights, 3, 3, 2);
	mpipe->setTriBufferSizes(3, vertCount);
	mpipe->vifOffset = mpipe->inputStride*vertCount;
	mpipe->instanceCB = saInstanceCB;
	mpipe->preUninstCB = saPreCB;
	mpipe->uninstanceCB = saUninstanceCB;
	rw::ps2::registerPDSPipe(mpipe);

	pipe = rw::ps2::ObjPipeline::create(); //unused in DFFs
	pipe->pluginID = rw::ID_PDS;
	pipe->pluginData = PDS_PS2_CustomBuildingEnvMap_AtmPipeID;
	rw::ps2::registerPDSPipe(pipe);

	mpipe = rw::ps2::MatPipeline::create();	// use with 0x53f20080
	mpipe->pluginID = rw::ID_PDS;
	mpipe->pluginData = PDS_PS2_CustomBuildingEnvMap_MatPipeID;
	mpipe->attribs[0] = &saXYZADC;
	mpipe->attribs[1] = &saUV;
	mpipe->attribs[2] = &saRGBA;
	mpipe->attribs[3] = &saNormal;
	vertCount = rw::ps2::MatPipeline::getVertCount(0x3BD, 4, 3, 3);
	mpipe->setTriBufferSizes(4, vertCount);
	mpipe->vifOffset = mpipe->inputStride*vertCount;
	mpipe->instanceCB = saInstanceCB;
	mpipe->preUninstCB = saPreCB;
	mpipe->uninstanceCB = saUninstanceCB;
	rw::ps2::registerPDSPipe(mpipe);

	pipe = rw::ps2::ObjPipeline::create(); //unused in DFFs
	pipe->pluginID = rw::ID_PDS;
	pipe->pluginData = PDS_PS2_CustomBuildingDNEnvMap_AtmPipeID;
	rw::ps2::registerPDSPipe(pipe);

	mpipe = rw::ps2::MatPipeline::create();	// use with 0x53f20082
	mpipe->pluginID = rw::ID_PDS;
	mpipe->pluginData = PDS_PS2_CustomBuildingDNEnvMap_MatPipeID;
	mpipe->attribs[0] = &saXYZADC;
	mpipe->attribs[1] = &saUV;
	mpipe->attribs[2] = &saRGBA2;
	mpipe->attribs[3] = &saNormal;
	vertCount = rw::ps2::MatPipeline::getVertCount(0x3BD, 4, 3, 3);
	mpipe->setTriBufferSizes(4, vertCount);
	mpipe->vifOffset = mpipe->inputStride*vertCount;
	mpipe->instanceCB = saInstanceCB;
	mpipe->preUninstCB = saPreCB;
	mpipe->uninstanceCB = saUninstanceCB;
	rw::ps2::registerPDSPipe(mpipe);

	pipe = rw::ps2::ObjPipeline::create(); //unused in DFFs
	pipe->pluginID = rw::ID_PDS;
	pipe->pluginData = PDS_PS2_CustomBuildingUVA_AtmPipeID;
	rw::ps2::registerPDSPipe(pipe);

	mpipe = rw::ps2::MatPipeline::create(); // unused in DFFs
	mpipe->pluginID = rw::ID_PDS;
	mpipe->pluginData = PDS_PS2_CustomBuildingUVA_MatPipeID;
	mpipe->attribs[0] = &saXYZADC;
	mpipe->attribs[1] = &saUV;
	mpipe->attribs[2] = &saRGBA;
	vertCount = 0x50; //MatPipeline::getVertCount(VU_Lights, 3, 3, 2);
	mpipe->setTriBufferSizes(3, vertCount);
	mpipe->vifOffset = mpipe->inputStride*vertCount;
	mpipe->instanceCB = saInstanceCB;
	mpipe->preUninstCB = saPreCB;
	mpipe->uninstanceCB = saUninstanceCB;
	rw::ps2::registerPDSPipe(mpipe);

	pipe = rw::ps2::ObjPipeline::create(); // unused in DFFs
	pipe->pluginID = rw::ID_PDS;
	pipe->pluginData = PDS_PS2_CustomBuildingDNUVA_AtmPipeID;
	rw::ps2::registerPDSPipe(pipe);

	mpipe = rw::ps2::MatPipeline::create(); // unused in DFFs
	mpipe->pluginID = rw::ID_PDS;
	mpipe->pluginData = PDS_PS2_CustomBuildingDNUVA_MatPipeID;
	mpipe->attribs[0] = &saXYZADC;
	mpipe->attribs[1] = &saUV;
	mpipe->attribs[2] = &saRGBA2;
	vertCount = 0x50; //MatPipeline::getVertCount(VU_Lights, 3, 3, 2);
	mpipe->setTriBufferSizes(3, vertCount);
	mpipe->vifOffset = mpipe->inputStride*vertCount;
	mpipe->instanceCB = saInstanceCB;
	mpipe->preUninstCB = saPreCB;
	mpipe->uninstanceCB = saUninstanceCB;
	rw::ps2::registerPDSPipe(mpipe);
}

void
registerPS2VehiclePipes(void)
{
	rw::Pipeline *pipe;
	rw::ps2::MatPipeline *mpipe;
	uint32 vertCount;

	pipe = rw::ps2::ObjPipeline::create();
	pipe->pluginID = rw::ID_PDS;
	pipe->pluginData = PDS_PS2_CustomCar_AtmPipeID;
	rw::ps2::registerPDSPipe(pipe);

	// No effects whatsoever
	mpipe = rw::ps2::MatPipeline::create();
	mpipe->pluginID = rw::ID_PDS;
	mpipe->pluginData = PDS_PS2_CustomCar_MatPipeID;
	mpipe->attribs[0] = &saXYZADC;
	mpipe->attribs[1] = &saUV;
	mpipe->attribs[3] = &saNormal;
	vertCount = rw::ps2::MatPipeline::getVertCount(0x3CD, 4, 3, 2);
	mpipe->setTriBufferSizes(4, vertCount);
	mpipe->vifOffset = mpipe->inputStride*vertCount;
	mpipe->instanceCB = saInstanceCB;
	mpipe->preUninstCB = saPreCB;
	mpipe->uninstanceCB = saUninstanceCB;
	rw::ps2::registerPDSPipe(mpipe);

	pipe = rw::ps2::ObjPipeline::create(); // unused in DFFs and game
	pipe->pluginID = rw::ID_PDS;
	pipe->pluginData = PDS_PS2_CustomCarEnvMap_AtmPipeID;
	rw::ps2::registerPDSPipe(pipe);

	// Environment map (+ Specular map)
	mpipe = rw::ps2::MatPipeline::create();	// use with 0x53f20084
	mpipe->pluginID = rw::ID_PDS;
	mpipe->pluginData = PDS_PS2_CustomCarEnvMap_MatPipeID;
	mpipe->attribs[0] = &saXYZADC;
	mpipe->attribs[1] = &saUV;
	mpipe->attribs[3] = &saNormal;
	vertCount = rw::ps2::MatPipeline::getVertCount(0x3BD, 4, 3, 3);
	mpipe->setTriBufferSizes(4, vertCount);
	mpipe->vifOffset = mpipe->inputStride*vertCount;
	mpipe->instanceCB = saInstanceCB;
	mpipe->preUninstCB = saPreCB;
	mpipe->uninstanceCB = saUninstanceCB;
	rw::ps2::registerPDSPipe(mpipe);

	// x-Environment map (+ Specular map)
	mpipe = rw::ps2::MatPipeline::create();	// use with 0x53f20084
	mpipe->pluginID = rw::ID_PDS;
	mpipe->pluginData = PDS_PS2_CustomCarEnvMapUV2_MatPipeID;
	mpipe->attribs[0] = &saXYZADC;
	mpipe->attribs[1] = &saUV2;
	mpipe->attribs[3] = &saNormal;
	vertCount = rw::ps2::MatPipeline::getVertCount(0x3BD, 4, 3, 3);
	mpipe->setTriBufferSizes(4, vertCount);
	mpipe->vifOffset = mpipe->inputStride*vertCount;
	mpipe->instanceCB = saInstanceCB;
	mpipe->preUninstCB = saPreCB;
	mpipe->uninstanceCB = saUninstanceCB;
	rw::ps2::registerPDSPipe(mpipe);
}

void
registerPS2SkinPipe(void)
{
	rw::Pipeline *pipe;
	rw::ps2::MatPipeline *mpipe;
	uint32 vertCount;

	pipe = rw::ps2::ObjPipeline::create();
	pipe->pluginID = rw::ID_PDS;
	pipe->pluginData = PDS_PS2_CustomSkinPed_AtmPipeID;
	rw::ps2::registerPDSPipe(pipe);

	mpipe = rw::ps2::MatPipeline::create();	// use with 0x53f20088
	mpipe->pluginID = rw::ID_PDS;
	mpipe->pluginData = PDS_PS2_CustomSkinPed_MatPipeID;
	mpipe->attribs[0] = &saXYZADC;
	mpipe->attribs[1] = &saUV;
	mpipe->attribs[2] = &saNormal;
	mpipe->attribs[3] = &saWeights;
//	these values give vertCount = 0x33 :/
//	vertCount = MatPipeline::getVertCount(0x2D0, 4, 3, 2);
	vertCount = 0x30;
	mpipe->setTriBufferSizes(4, vertCount);
	mpipe->vifOffset = mpipe->inputStride*vertCount;
	mpipe->instanceCB = saInstanceCB;
	mpipe->preUninstCB = saPreCB;
	mpipe->uninstanceCB = saUninstanceCB;
	mpipe->postUninstCB = saPostCB;
	rw::ps2::registerPDSPipe(mpipe);
}

void
registerPDSPipes(void)
{
	// 19 pipelines in total
	registerPS2BuildingPipes();
	registerPS2VehiclePipes();
	registerPS2SkinPipe();
}

}
