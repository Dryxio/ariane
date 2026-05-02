#define WITH_D3D
#include "euryopa.h"

using namespace rw;
using namespace RWDEVICE;

Texture *backBufferTex;
Raster *backBuffer;

static short indices[] = {
	2, 3, 1, 2, 1, 0
};
Im2DVertex verts[4];

static void
CreateImmediateData(void)
{
	Camera *cam = Scene.camera;
	int w = sk::globals.width;
	int h = sk::globals.height;
	float recipZ = 1.0f/cam->nearPlane;

	verts[0].setScreenX(0-HALFPX);
	verts[0].setScreenY(0-HALFPX);
	verts[0].setScreenZ(rw::im2d::GetNearZ());
	verts[0].setCameraZ(cam->nearPlane);
	verts[0].setRecipCameraZ(recipZ);
	verts[0].setColor(255, 255, 255, 255);
	verts[0].setU(0.0f, recipZ);
	verts[0].setV(0.0f, recipZ);

	verts[1].setScreenX(w-HALFPX);
	verts[1].setScreenY(0-HALFPX);
	verts[1].setScreenZ(rw::im2d::GetNearZ());
	verts[1].setCameraZ(cam->nearPlane);
	verts[1].setRecipCameraZ(recipZ);
	verts[1].setColor(255, 255, 255, 255);
	verts[1].setU(1.0f, recipZ);
	verts[1].setV(0.0f, recipZ);

	verts[2].setScreenX(0-HALFPX);
	verts[2].setScreenY(h-HALFPX);
	verts[2].setScreenZ(rw::im2d::GetNearZ());
	verts[2].setCameraZ(cam->nearPlane);
	verts[2].setRecipCameraZ(recipZ);
	verts[2].setColor(255, 255, 255, 255);
	verts[2].setU(0.0f, recipZ);
	verts[2].setV(1.0f, recipZ);

	verts[3].setScreenX(w-HALFPX);
	verts[3].setScreenY(h-HALFPX);
	verts[3].setScreenZ(rw::im2d::GetNearZ());
	verts[3].setCameraZ(cam->nearPlane);
	verts[3].setRecipCameraZ(recipZ);
	verts[3].setColor(255, 255, 255, 255);
	verts[3].setU(1.0f, recipZ);
	verts[3].setV(1.0f, recipZ);
}

static void
InitPostFX(void)
{
	if(backBuffer)
		backBuffer->destroy();
	backBuffer = Raster::create(sk::globals.width, sk::globals.height, 32, Raster::C888|Raster::CAMERATEXTURE);
	if(backBufferTex == nil)
		backBufferTex = Texture::create(backBuffer);
	else
		backBufferTex->raster = backBuffer;
}

static bool shadersInitialized;
#ifdef RW_D3D9
using namespace d3d;
using namespace d3d9;

static void *colourfilterIII;
static void *colourfilterVC;
static void *colourfilterLeedsPS2;
static void *colourfilterLeedsPSP;
static void *colourfilterSAPS2;
static void *colourfilterSAPC;
static void *radiosity;
static void *radiosityLeeds;

static void
CreateShaders(void)
{
#include "d3d_shaders/colourfilterIII_PS.inc"
	colourfilterIII = createPixelShader(colourfilterIII_PS_cso);
#include "d3d_shaders/colourfilterVC_PS.inc"
	colourfilterVC = createPixelShader(colourfilterVC_PS_cso);
#include "d3d_shaders/colourfilterLeedsPS2_PS.inc"
	colourfilterLeedsPS2 = createPixelShader(colourfilterLeedsPS2_PS_cso);
#include "d3d_shaders/colourfilterLeedsPSP_PS.inc"
	colourfilterLeedsPSP = createPixelShader(colourfilterLeedsPSP_PS_cso);
#include "d3d_shaders/colourfilterSAPS2_PS.inc"
	colourfilterSAPS2 = createPixelShader(colourfilterSAPS2_PS_cso);
#include "d3d_shaders/colourfilterSAPC_PS.inc"
	colourfilterSAPC = createPixelShader(colourfilterSAPC_PS_cso);
#include "d3d_shaders/radiosityPS.inc"
	radiosity = createPixelShader(radiosityPS_cso);
#include "d3d_shaders/radiosityLeedsPS.inc"
	radiosityLeeds = createPixelShader(radiosityLeedsPS_cso);

	shadersInitialized = true;
}

static void
RenderColourFilterGeneric(void *ps)
{
	float postfxvars[4];
	d3ddevice->SetPixelShaderConstantF(10, (float*)&Timecycle::currentColours.postfx1, 1);
	d3ddevice->SetPixelShaderConstantF(11, (float*)&Timecycle::currentColours.postfx2, 1);
	postfxvars[0] = Timecycle::currentColours.radiosityLimit/255.0f;
	postfxvars[1] = Timecycle::currentColours.radiosityIntensity/255.0f;
	postfxvars[2] = 2.0f;		// render passes
	d3ddevice->SetPixelShaderConstantF(12, (float*)postfxvars, 1);

	d3d::im2dOverridePS = ps;
	im2d::RenderIndexedPrimitive(rw::PRIMTYPETRILIST,
		&verts, 4, &indices, 6);
	d3d::im2dOverridePS = nil;
}
#endif

#ifdef RW_OPENGL

static Shader *colourfilterIII;
static Shader *colourfilterVC;
static Shader *colourfilterLeedsPS2;
static Shader *colourfilterLeedsPSP;
static Shader *colourfilterSAPS2;
static Shader *colourfilterSAPC;
static Shader *radiosity;
static Shader *radiosityLeeds;

int32 u_postfxCol1;
int32 u_postfxCol2;
int32 u_postfxParams;

static void
CreateShaders(void)
{
	u_postfxCol1 = registerUniform("u_postfxCol1");
	u_postfxCol2 = registerUniform("u_postfxCol2");
	u_postfxParams = registerUniform("u_postfxParams");

#include "gl_shaders/im2d_vert.inc"
	const char *vs[] = { shaderDecl, header_vert_src, im2d_vert_src, nil };
	{
#include "gl_shaders/colourfilterIII_frag.inc"
	const char *fs[] = { shaderDecl, header_frag_src, colourfilterIII_frag_src, nil };
	colourfilterIII = Shader::create(vs, fs);
	assert(colourfilterIII);
	}
	{
#include "gl_shaders/colourfilterVC_frag.inc"
	const char *fs[] = { shaderDecl, header_frag_src, colourfilterVC_frag_src, nil };
	colourfilterVC = Shader::create(vs, fs);
	assert(colourfilterVC);
	}
	{
#include "gl_shaders/colourfilterLeedsPS2_frag.inc"
	const char *fs[] = { shaderDecl, header_frag_src, colourfilterLeedsPS2_frag_src, nil };
	colourfilterLeedsPS2 = Shader::create(vs, fs);
	assert(colourfilterLeedsPS2);
	}
	{
#include "gl_shaders/colourfilterLeedsPSP_frag.inc"
	const char *fs[] = { shaderDecl, header_frag_src, colourfilterLeedsPSP_frag_src, nil };
	colourfilterLeedsPSP = Shader::create(vs, fs);
	assert(colourfilterLeedsPSP);
	}
	{
#include "gl_shaders/colourfilterSAPS2_frag.inc"
	const char *fs[] = { shaderDecl, header_frag_src, colourfilterSAPS2_frag_src, nil };
	colourfilterSAPS2 = Shader::create(vs, fs);
	assert(colourfilterSAPS2);
	}
	{
#include "gl_shaders/colourfilterSAPC_frag.inc"
	const char *fs[] = { shaderDecl, header_frag_src, colourfilterSAPC_frag_src, nil };
	colourfilterSAPC = Shader::create(vs, fs);
	assert(colourfilterSAPC);
	}
	{
#include "gl_shaders/radiosity_frag.inc"
	const char *fs[] = { shaderDecl, header_frag_src, radiosity_frag_src, nil };
	radiosity = Shader::create(vs, fs);
	assert(radiosity);
	}
	{
#include "gl_shaders/radiosityLeeds_frag.inc"
	const char *fs[] = { shaderDecl, header_frag_src, radiosityLeeds_frag_src, nil };
	radiosityLeeds = Shader::create(vs, fs);
	assert(radiosityLeeds);
	}

	shadersInitialized = true;
}

static void
RenderColourFilterGeneric(Shader *sh)
{
if(sh == nil) return;
#define U(i) currentShader->uniformLocations[i]
	gl3::im2dOverrideShader = sh;
	sh->use();
	float postfxvars[4];
	postfxvars[0] = Timecycle::currentColours.radiosityLimit/255.0f;
	postfxvars[1] = Timecycle::currentColours.radiosityIntensity/255.0f;
	postfxvars[2] = 2.0f;		// render passes
	glUniform4fv(U(u_postfxCol1), 1, (float*)&Timecycle::currentColours.postfx1);
	glUniform4fv(U(u_postfxCol2), 1, (float*)&Timecycle::currentColours.postfx2);
	glUniform4fv(U(u_postfxParams), 1, postfxvars);
	im2d::RenderIndexedPrimitive(rw::PRIMTYPETRILIST,
		&verts, 4, &indices, 6);
	gl3::im2dOverrideShader = nil;
}
#endif


static void
GetBackbuffer(Raster *raster)
{
	Raster::pushContext(raster);
	engine->currentCamera->frameBuffer->renderFast(0, 0);
	Raster::popContext();
}

static void
RenderColourFilterIII(void)
{
	RenderColourFilterGeneric(colourfilterIII);
}

static void
RenderColourFilterVC(void)
{
	RenderColourFilterGeneric(colourfilterVC);
}

static void
RenderColourFilterLeedsPS2(void)
{
	RenderColourFilterGeneric(colourfilterLeedsPS2);
}

static void
RenderColourFilterLeedsPSP(void)
{
	RenderColourFilterGeneric(colourfilterLeedsPSP);
}

static void
RenderColourFilterSAPS2(void)
{
	RenderColourFilterGeneric(colourfilterSAPS2);
}

static void
RenderColourFilterSAPC(void)
{
	RenderColourFilterGeneric(colourfilterSAPC);
}

static void
RenderRadiosity(void)
{
	RenderColourFilterGeneric(radiosity);
}

static void
RenderRadiosityLeeds(void)
{
	RenderColourFilterGeneric(radiosityLeeds);
}

void
RenderPostFX(void)
{
	bool32 changedBackbuf;
	if(backBuffer == nil ||
	   backBuffer->width != sk::globals.width ||
	   backBuffer->height != sk::globals.height)
		InitPostFX();
	GetBackbuffer(backBuffer);
	changedBackbuf = 0;

	CreateImmediateData();

	if(!shadersInitialized)
		CreateShaders();

	rw::SetRenderStatePtr(TEXTURERASTER, backBufferTex->raster);
	rw::SetRenderState(rw::TEXTUREFILTER, Texture::LINEAR);
	// we may want to blur later on
	SetRenderState(TEXTUREADDRESS, Texture::CLAMP);

	SetRenderState(VERTEXALPHA, 0);
	SetRenderState(ZTESTENABLE, 0);
	SetRenderState(ZWRITEENABLE, 0);


	switch(params.timecycle){
	case GAME_III:
		RenderColourFilterIII();
		changedBackbuf = 1;
		break;
	case GAME_VC:
		RenderColourFilterVC();
		changedBackbuf = 1;
		break;
	case GAME_SA:
		if(gColourFilter == PLATFORM_PS2){
			RenderColourFilterSAPS2();
			changedBackbuf = 1;
		}else if(gColourFilter == PLATFORM_PC || gColourFilter == PLATFORM_XBOX){
			RenderColourFilterSAPC();
			changedBackbuf = 1;
		}
		break;
	}

	if(gRadiosity){
		if(changedBackbuf)
			GetBackbuffer(backBuffer);
		switch(params.timecycle){
		case GAME_SA:
			RenderRadiosity();
			changedBackbuf = 1;
			break;
		case GAME_VCS:
			RenderRadiosityLeeds();
			changedBackbuf = 1;
			break;
		}
	}

	switch(params.timecycle)
	case GAME_LCS:
	case GAME_VCS:
		if(gColourFilter == PLATFORM_PS2){
			if(changedBackbuf)
				GetBackbuffer(backBuffer);
			RenderColourFilterLeedsPS2();
			changedBackbuf = 1;
		}else if(gColourFilter == PLATFORM_PSP){
			if(changedBackbuf)
				GetBackbuffer(backBuffer);
			RenderColourFilterLeedsPSP();
			changedBackbuf = 1;
		}
}
