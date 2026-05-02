#include "euryopa.h"

using namespace rw;

rw::ObjPipeline *buildingPipe;
rw::ObjPipeline *buildingDNPipe;


void
UpdateDayNightBalance(void)
{
	float minute = currentHour*60.0f + currentMinute;
	const float morningStart = 6 * 60.0f;
	const float morningEnd = 7 * 60.0f;
	const float eveningStart = 20 * 60.0f;
	const float eveningEnd = 21 * 60.0f;

	// 1.0 is night, 0.0 is day
	if(minute < morningStart)
		gDayNightBalance = 1.0f;
	else if(minute < morningEnd)
		gDayNightBalance = (morningEnd - minute) / (morningEnd - morningStart);
	else if(minute < eveningStart)
		gDayNightBalance = 0.0f;
	else if(minute < eveningEnd)
		gDayNightBalance = 1.0f - (eveningEnd - minute) / (eveningEnd - eveningStart);
	else
		gDayNightBalance = 1.0f;
}

void
ConvertXboxGeometry(Atomic *atm)
{
	Geometry *geo = atm->geometry;
	// This shouldn't happen, but better check
	if((geo->flags & Geometry::PRELIT) == 0)
		return;

	// Convert normals to extra colors
	if(geo->flags & Geometry::NORMALS){
		RGBA *daycols = geo->colors;
		if(gta::getExtraVertColors(atm) == nil)
			gta::allocateExtraVertColors(geo);
		RGBA *nightcols = gta::getExtraVertColors(atm);
		V3d *normals = geo->morphTargets[0].normals;

		int32 i;
		for(i = 0; i < geo->numVertices; i++){
			nightcols[i].red = normals[i].x*255;
			nightcols[i].green = normals[i].y*255;
			nightcols[i].blue = normals[i].z*255;
			nightcols[i].alpha = daycols[i].alpha;
		}

		geo->flags &= ~Geometry::NORMALS;
	}

	// Move extranormals to normals?
	// But then we may have to reallocate the Geometry
	// Better handle in the instanceCB
}

// similar to GTA code, some useless stuff
void
GetBuildingEnvMatrix(Atomic *atomic, Frame *envframe, RawMatrix *envmat)
{
	Matrix inv, env;
	Clump *clump;
	Frame *frame;

	if(envframe == nil)
		envframe = ((Camera*)engine->currentCamera)->getFrame();

	clump = atomic->clump;

	Matrix::invert(&inv, envframe->getLTM());
	frame = clump ? clump->getFrame() : atomic->getFrame();
	Matrix::mult(&env, frame->getLTM(), &inv);
	convMatrix(envmat, &env);
}

void
SetupBuildingEnvMap(rw::Material *m)
{
	uint32 *flags = (uint32*)&m->surfaceProps.specular;
	*flags = 0;
	gta::EnvMat *env = gta::getEnvMat(m);
	if(env && MatFX::getEffects(m) == MatFX::ENVMAP)
		env->texture = MatFX::get(m)->getEnvTexture();
	if(env && env->getShininess() != 0.0f && env->texture)
		*flags = 1;
}

bool
IsBuildingPipeAttached(rw::Atomic *atm)
{
	uint32 id;

	// PS2 logic:
	if(atm->pipeline && atm->pipeline->pluginID == ID_PDS){
		id = atm->pipeline->pluginData;
		if(id == gta::PDS_PS2_CustomBuilding_AtmPipeID ||
		   id == gta::PDS_PS2_CustomBuildingDN_AtmPipeID ||
		   id == gta::PDS_PS2_CustomBuildingEnvMap_AtmPipeID ||
		   id == gta::PDS_PS2_CustomBuildingDNEnvMap_AtmPipeID ||
		   id == gta::PDS_PS2_CustomBuildingUVA_AtmPipeID ||
		   // this one is not checked by the game for some reason:
		   id == gta::PDS_PS2_CustomBuildingDNUVA_AtmPipeID)
			return true;
	}

	id = gta::getPipelineID(atm);
	// Xbox logic:
	if(id == gta::RSPIPE_XBOX_CustomBuilding_PipeID ||
	   id == gta::RSPIPE_XBOX_CustomBuildingDN_PipeID ||
	   id == gta::RSPIPE_XBOX_CustomBuildingEnvMap_PipeID ||
	   id == gta::RSPIPE_XBOX_CustomBuildingDNEnvMap_PipeID)
		return true;

	// PC logic:
	if(id == gta::RSPIPE_PC_CustomBuilding_PipeID ||
	   id == gta::RSPIPE_PC_CustomBuildingDN_PipeID)
		return true;
	if(gta::getExtraVertColors(atm) && atm->geometry->colors)
		return true;

	return false;
}

void
SetupBuildingPipe(rw::Atomic *atm)
{
	// ps2:
	//  obj: if has extra colors -> DN pipe else -> regular
	//  mat: if UV transform -> UVA pipe
	//       else if Env map && has env map && has normals -> envmap pipe
	//       else -> regular pipe
	// xbox: use pipeline ID but fall back to non-DN if no normals flag
	// pc: if two sets -> DN, else -> regular

	uint32 id = gta::getPipelineID(atm);
	if(id == gta::RSPIPE_XBOX_CustomBuilding_PipeID ||
	   id == gta::RSPIPE_XBOX_CustomBuildingDN_PipeID ||
	   id == gta::RSPIPE_XBOX_CustomBuildingEnvMap_PipeID ||
	   id == gta::RSPIPE_XBOX_CustomBuildingDNEnvMap_PipeID)
		ConvertXboxGeometry(atm);
	// Just do the PC thing for now
	if(gta::getExtraVertColors(atm) && atm->geometry->colors){
		atm->pipeline = buildingDNPipe;
		gta::setPipelineID(atm, gta::RSPIPE_PC_CustomBuildingDN_PipeID);
	}else{
		atm->pipeline = buildingPipe;
		gta::setPipelineID(atm, gta::RSPIPE_PC_CustomBuilding_PipeID);
	}
	int i;
	for(i = 0; i < atm->geometry->matList.numMaterials; i++)
		SetupBuildingEnvMap(atm->geometry->matList.materials[i]);
}

bool32
instWhite(int type, uint8 *dst, uint32 numVertices, uint32 stride)
{
	if(type == VERT_ARGB || type == VERT_RGBA){
		for(uint32 i = 0; i < numVertices; i++){
			dst[0] = 255;
			dst[1] = 255;
			dst[2] = 255;
			dst[3] = 255;
			dst += stride;
		}
	}else
		assert(0 && "unsupported color type");
	return 0;
}
