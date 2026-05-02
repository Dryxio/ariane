#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

#include <rw.h>
#include "rwgta.h"

using namespace rw;
using namespace xbox;

namespace gta {

rw::ObjPipeline *XboxCustomBuildingPipe;
rw::ObjPipeline *XboxCustomBuildingDNPipe;
rw::ObjPipeline *XboxCustomBuildingEnvMapPipe;
rw::ObjPipeline *XboxCustomBuildingDNEnvMapPipe;

void
customBuildingUninstanceCB(Geometry *geo, xbox::InstanceDataHeader *header)
{
	uint32 *vertexFmt = getVertexFmt(geo);
	uint32 fmt = *vertexFmt;
	assert(fmt != 0);
	uint8 *src = (uint8*)header->vertexBuffer;

	uint32 sel = fmt & 0xF;
	uninstV3d(v3dFormatMap[sel], geo->morphTargets[0].vertices, src,
	          header->numVertices, header->stride);
	src += sel == 4 ? 4 : 3*vertexFormatSizes[sel];

	sel = (fmt >> 4) & 0xF;
	if(sel){
		uninstV3d(v3dFormatMap[sel], geo->morphTargets[0].normals, src,
		          header->numVertices, header->stride);
		src += sel == 4 ? 4 : 3*vertexFormatSizes[sel];
	}

	if(fmt & 0x1000000){
		uninstColor(VERT_ARGB, geo->colors, src,
		            header->numVertices, header->stride);
		src += 4;
	}

	for(int i = 0; i < 4; i++){
		sel = (fmt >> (i*4 + 8)) & 0xF;
		if(sel == 0)
			break;
		uninstTexCoords(v2dFormatMap[sel], geo->texCoords[i], src,
		          header->numVertices, header->stride);
		src += sel == 4 ? 4 : 2*vertexFormatSizes[sel];
	}

	rw::V3d *extranormals = gta::allocateExtraNormals(geo);
	uninstV3d(VERT_FLOAT3, extranormals, src,
	          header->numVertices, header->stride);
}

void
registerXboxPipes(void)
{
	xbox::ObjPipeline *pipe;

	pipe = xbox::ObjPipeline::create();
	pipe->pluginID = gta::RSPIPE_XBOX_CustomBuilding_PipeID;
	pipe->pluginData = gta::RSPIPE_XBOX_CustomBuilding_PipeID;
	pipe->instanceCB = nil;
	pipe->uninstanceCB = defaultUninstanceCB;
	XboxCustomBuildingPipe = pipe;

	pipe = xbox::ObjPipeline::create();
	pipe->pluginID = gta::RSPIPE_XBOX_CustomBuildingDN_PipeID;
	pipe->pluginData = gta::RSPIPE_XBOX_CustomBuildingDN_PipeID;
	pipe->instanceCB = nil;
	pipe->uninstanceCB = defaultUninstanceCB;
	XboxCustomBuildingDNPipe = pipe;

	pipe = xbox::ObjPipeline::create();
	pipe->pluginID = gta::RSPIPE_XBOX_CustomBuildingEnvMap_PipeID;
	pipe->pluginData = gta::RSPIPE_XBOX_CustomBuildingEnvMap_PipeID;
	pipe->instanceCB = nil;
	pipe->uninstanceCB = customBuildingUninstanceCB;
	XboxCustomBuildingEnvMapPipe = pipe;

	pipe = xbox::ObjPipeline::create();
	pipe->pluginID = gta::RSPIPE_XBOX_CustomBuildingDNEnvMap_PipeID;
	pipe->pluginData = gta::RSPIPE_XBOX_CustomBuildingDNEnvMap_PipeID;
	pipe->instanceCB = nil;
	pipe->uninstanceCB = customBuildingUninstanceCB;
	XboxCustomBuildingDNEnvMapPipe = pipe;
}

}
