#define WITH_D3D
#include "euryopa.h"

using namespace rw;

#ifdef RW_D3D9

using namespace d3d;
using namespace d3d9;

enum {
	PSLOC_lightfactor = 1
};


rw::ObjPipeline *neoWorldPipe;

static void *neoWorld_VS;
static void *neoWorldIII_PS;
static void *neoWorldVC_PS;

static void
neoWorldRenderCB(Atomic *atomic, d3d9::InstanceDataHeader *header)
{
	int vsBits;
	setStreamSource(0, header->vertexStream[0].vertexBuffer, 0, header->vertexStream[0].stride);
	setIndices(header->indexBuffer);
	setVertexDeclaration(header->vertexDeclaration);

	vsBits = lightingCB_Shader(atomic);
	uploadMatrices(atomic->getFrame()->getLTM());

	d3ddevice->SetVertexShaderConstantF(VSLOC_fogData, (float*)&d3dShaderState.fogData, 1);
	d3ddevice->SetPixelShaderConstantF(PSLOC_fogColor, (float*)&d3dShaderState.fogColor, 1);

	setVertexShader(neoWorld_VS);

	float surfProps[4];
	surfProps[3] = 0.0f;

	if(params.neoWorldPipe == GAME_III)
		setPixelShader(neoWorldIII_PS);
	else
		setPixelShader(neoWorldVC_PS);

	float lightfactor[4];

	InstanceData *inst = header->inst;
	for(uint32 i = 0; i < header->numMeshes; i++){
		Material *m = inst->material;

		if(MatFX::getEffects(m) == MatFX::DUAL){
			MatFX *matfx = MatFX::get(m);
			Texture *dualtex = matfx->getDualTexture();
			if(dualtex == nil)
				goto notex;
			d3d::setTexture(1, dualtex);
			lightfactor[0] = lightfactor[1] = lightfactor[2] = gNeoLightMapStrength;
		}else{
		notex:
			d3d::setTexture(1, nil);
			lightfactor[0] = lightfactor[1] = lightfactor[2] = 0.0f;
		}
		lightfactor[3] = inst->material->color.alpha/255.0f;
		d3d::setTexture(0, m->texture);
		d3ddevice->SetPixelShaderConstantF(PSLOC_lightfactor, lightfactor, 1);

		SetRenderState(VERTEXALPHA, inst->vertexAlpha || m->color.alpha != 255);

		rw::RGBAf col = { 1.0f, 1.0f, 1.0f, m->color.alpha/255.0f };
		d3ddevice->SetVertexShaderConstantF(VSLOC_matColor, (float*)&col, 1);

		surfProps[0] = m->surfaceProps.ambient;
		surfProps[1] = m->surfaceProps.specular;
		surfProps[2] = m->surfaceProps.diffuse;
		d3ddevice->SetVertexShaderConstantF(VSLOC_surfProps, surfProps, 1);

		drawInst(header, inst);
		inst++;
	}
}


void
MakeNeoWorldPipe(void)
{
#include "d3d_shaders/default_UV2_VS.inc"
	neoWorld_VS = createVertexShader(default_UV2_VS_cso);
	assert(neoWorld_VS);


#include "d3d_shaders/neoWorldIII_PS.inc"
#include "d3d_shaders/neoWorldVC_PS.inc"
	neoWorldIII_PS = createPixelShader(neoWorldIII_PS_cso);
	neoWorldVC_PS = createPixelShader(neoWorldVC_PS_cso);

	d3d9::ObjPipeline *pipe;
	pipe = d3d9::ObjPipeline::create();
	pipe->instanceCB = d3d9::defaultInstanceCB;
	pipe->uninstanceCB = nil;
	pipe->renderCB = neoWorldRenderCB;
	neoWorldPipe = pipe;
}

#endif
