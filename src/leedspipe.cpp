#define WITH_D3D
#include <rw.h>
#include "rwgta.h"

#include <assert.h>

namespace gta {

rw::RGBAf leedsPipe_amb;
rw::RGBAf leedsPipe_emiss;
rw::int32 leedsPipe_platformSwitch = 1;	// ps2

rw::ObjPipeline *leedsPipe;

#ifdef RW_D3D9

using namespace rw;
using namespace d3d;
using namespace d3d9;

enum {
	VSLOC_emissive = VSLOC_afterLights,
	VSLOC_ambient,

	PSLOC_colorscale = 1
};

static void *leedsBuilding_VS;
static void *leedsBuilding_mobile_VS;
static void *scale_PS;

void
leedsRenderCB(rw::Atomic *atomic, rw::d3d9::InstanceDataHeader *header)
{
	setStreamSource(0, header->vertexStream[0].vertexBuffer, 0, header->vertexStream[0].stride);
	setIndices(header->indexBuffer);
	setVertexDeclaration(header->vertexDeclaration);

	if(leedsPipe_platformSwitch == 2)
		setVertexShader(leedsBuilding_mobile_VS);
	else
		setVertexShader(leedsBuilding_VS);
	setPixelShader(scale_PS);

	uploadMatrices(atomic->getFrame()->getLTM());

	d3ddevice->SetVertexShaderConstantF(VSLOC_ambient, (float*)&leedsPipe_amb, 1);
	d3ddevice->SetVertexShaderConstantF(VSLOC_emissive, (float*)&leedsPipe_emiss, 1);

	float colorscale[4];
	colorscale[3] = 1.0f;

	InstanceData *inst = header->inst;
	for(rw::uint32 i = 0; i < header->numMeshes; i++){
		Material *m = inst->material;

		float cs = 1.0f;
		if(leedsPipe_platformSwitch != 2 && m->texture)
			cs = 255/128.0f;
		colorscale[0] = colorscale[1] = colorscale[2] = cs;
		d3ddevice->SetPixelShaderConstantF(PSLOC_colorscale, colorscale, 1);

		if(m->texture)
			d3d::setTexture(0, m->texture);
/*
		else
			d3d::setTexture(0, gpWhiteTexture);	// actually we don't even render this
*/

		rw::RGBA color = m->color;
		setMaterial(color, m->surfaceProps, leedsPipe_platformSwitch == 1 ? 0.5f : 1.0f);

		SetRenderState(VERTEXALPHA, inst->vertexAlpha || color.alpha != 255);

		drawInst(header, inst);
		inst++;
	}
}

void
MakeLeedsPipe(void)
{
#include "d3d_shaders/leedsBuilding_VS.inc"
	leedsBuilding_VS = rw::d3d::createVertexShader(leedsBuilding_VS_cso);
	assert(leedsBuilding_VS);
#include "d3d_shaders/leedsBuilding_mobile_VS.inc"
	leedsBuilding_mobile_VS = rw::d3d::createVertexShader(leedsBuilding_mobile_VS_cso);
	assert(leedsBuilding_mobile_VS);
#include "d3d_shaders/scale_PS.inc"
	scale_PS = rw::d3d::createPixelShader(scale_PS_cso);
	assert(scale_PS);

	rw::d3d9::ObjPipeline *pipe = rw::d3d9::ObjPipeline::create();
	pipe->instanceCB = rw::d3d9::defaultInstanceCB;
	pipe->uninstanceCB = rw::d3d9::defaultUninstanceCB;
	pipe->renderCB = leedsRenderCB;
	leedsPipe = pipe;
}

#endif

#ifdef RW_GL3

using namespace rw;
using namespace gl3;

static gl3::Shader *leedsWorldShader;
static gl3::Shader *leedsWorldShader_mobile;
static int32 u_colorscale, u_amb, u_emiss;

#define U(i) currentShader->uniformLocations[i]

void
leedsRenderCB(Atomic *atomic, gl3::InstanceDataHeader *header)
{
	Material *m;

	setWorldMatrix(atomic->getFrame()->getLTM());

	setupVertexInput(header);

	InstanceData *inst = header->inst;
	rw::int32 n = header->numMeshes;

	if(leedsPipe_platformSwitch == 2)
		leedsWorldShader_mobile->use();
	else
		leedsWorldShader->use();

	glUniform4fv(U(u_amb), 1, (float*)&leedsPipe_amb);
	glUniform4fv(U(u_emiss), 1, (float*)&leedsPipe_emiss);

	float colorscale[4];
	colorscale[3] = 1.0f;

	while(n--){
		m = inst->material;

		float cs = 1.0f;
		if(leedsPipe_platformSwitch != 2 && m->texture)
			cs = 255/128.0f;
		colorscale[0] = colorscale[1] = colorscale[2] = cs;
		glUniform4fv(U(u_colorscale), 1, colorscale);

		setTexture(0, m->texture);

		rw::RGBA color = m->color;
		setMaterial(color, m->surfaceProps, leedsPipe_platformSwitch == 1 ? 0.5f : 1.0f);

		rw::SetRenderState(VERTEXALPHA, inst->vertexAlpha || color.alpha != 0xFF);

		drawInst(header, inst);
		inst++;
	}
	teardownVertexInput(header);

}

void
MakeLeedsPipe(void)
{
	u_colorscale = registerUniform("u_colorscale");
	u_amb = registerUniform("u_amb");
	u_emiss = registerUniform("u_emiss");

	{
#include "gl_shaders/scale_frag.inc"
#include "gl_shaders/leedsBuilding_vert.inc"
#include "gl_shaders/leedsBuilding_mobile_vert.inc"
	const char *vs[] = { shaderDecl, header_vert_src, leedsBuilding_vert_src, nil };
	const char *vs_mobile[] = { shaderDecl, header_vert_src, leedsBuilding_mobile_vert_src, nil };
	const char *fs[] = { shaderDecl, header_frag_src, scale_frag_src, nil };
	leedsWorldShader = Shader::create(vs, fs);
	assert(leedsWorldShader);
	leedsWorldShader_mobile = Shader::create(vs_mobile, fs);
	assert(leedsWorldShader_mobile);
	}

	gl3::ObjPipeline *pipe = gl3::ObjPipeline::create();
	pipe->instanceCB = defaultInstanceCB;
	pipe->uninstanceCB = defaultUninstanceCB;
	pipe->renderCB = leedsRenderCB;
	leedsPipe = pipe;
}

#endif

}
