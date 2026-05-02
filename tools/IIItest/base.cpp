#include "III.h"
#include "Camera.h"
#include "Draw.h"
#include <cstdarg>

#include "Test.h"

Globals globals;

CEntity *debugent;

GlobalScene Scene;
rw::Light  *pAmbient;
rw::Light  *pDirect;
rw::Light  *pExtraDirectionals[4];
bool isRunning;

uchar work_buff[55000];


static uint randseed = 0x3ef7d0;	// this seems to be just a random address in III
void ps2_srand(uint s) { randseed = s; }
int
ps2_rand(void)
{
	randseed = randseed * 0x41c64e6d + 12345;
	return randseed & 0x7FFFFFFF;
}


char*
getPath(const char *path)
{
	static char cipath[1024];
	strncpy(cipath, path, 1024);
	rw::makePath(cipath);
	return cipath;
}

char*
skipWhite(char *s)
{
	while(isspace(*s))
		s++;
	return s;
}

int
StrAssoc::get(StrAssoc *desc, const char *key)
{
	for(; desc->key[0] != '\0'; desc++)
		if(strcmp(desc->key, key) == 0)
			return desc->val;
	return desc->val;
}

void*
DatDesc::get(DatDesc *desc, const char *name)
{
	for(; desc->name[0] != '\0'; desc++)
		if(strcmp(desc->name, name) == 0)
			return (void*)desc->handler;
	return (void*)desc->handler;
}

rw::Raster*
d3dToGl3(rw::Raster *raster)
{
	if(raster->platform != rw::PLATFORM_D3D8 &&
	   raster->platform != rw::PLATFORM_D3D9)
		return raster;
	rw::d3d::D3dRaster *natras = PLUGINOFFSET(rw::d3d::D3dRaster,
	                                      raster, rw::d3d::nativeRasterOffset);
	if(natras->customFormat)
		assert(0 && "no custom d3d formats");

	rw::Image *image = raster->toImage();
	raster->destroy();
	raster = rw::Raster::createFromImage(image, rw::PLATFORM_GL3);
	image->destroy();
	return raster;
}

// Simple function to convert a raster to the current platform.
// TODO: convert custom formats (DXT) properly.
rw::Raster*
convertTexRaster(rw::Raster *ras)
{
	rw::Image *img = ras->toImage();
	img->unpalettize();
	ras = rw::Raster::createFromImage(img);
	img->destroy();
	return ras;
}

void
convertTxd(rw::TexDictionary *txd)
{
	rw::Texture *tex;
	FORLIST(lnk, txd->textures){
		tex = rw::Texture::fromDict(lnk);
		rw::Raster *ras = tex->raster;
		if(ras && ras->platform != rw::platform){
			if(!(ras->platform == rw::PLATFORM_D3D8 && rw::platform == rw::PLATFORM_D3D9 ||
			     ras->platform == rw::PLATFORM_D3D9 && rw::platform == rw::PLATFORM_D3D8)){
				tex->raster = convertTexRaster(ras);
				ras->destroy();
			}
		}
		tex->setFilter(rw::Texture::LINEAR);
	}

}


CVector
FindPlayerCoors(void)
{
	return TheCamera.GetPosition();
}

//
// Lights
//

rw::RGBAf AmbientLightColourForFrame;
rw::RGBAf AmbientLightColourForFrame_PedsCarsAndObjects;
rw::RGBAf DirectionalLightColourForFrame;
rw::RGBAf AmbientLightColour;
rw::RGBAf DirectionalLightColour;

void
SetLightsWithTimeOfDayColour(rw::World*)
{
	// TODO: CCoronas::LightsMult
	AmbientLightColourForFrame.red = CTimeCycle::m_fCurrentAmbientRed;
	AmbientLightColourForFrame.green = CTimeCycle::m_fCurrentAmbientGreen;
	AmbientLightColourForFrame.blue = CTimeCycle::m_fCurrentAmbientBlue;
	AmbientLightColourForFrame_PedsCarsAndObjects.red = clamp(AmbientLightColourForFrame.red*1.3f);
	AmbientLightColourForFrame_PedsCarsAndObjects.green = clamp(AmbientLightColourForFrame.green*1.3f);
	AmbientLightColourForFrame_PedsCarsAndObjects.blue = clamp(AmbientLightColourForFrame.blue*1.3f);
	// TODO: flash and rain etc.
	pAmbient->setColor(AmbientLightColourForFrame.red,
	                   AmbientLightColourForFrame.green,
	                   AmbientLightColourForFrame.blue);

	// TODO: CCoronas::LightsMult
	DirectionalLightColourForFrame.red = CTimeCycle::m_fCurrentDirectionalRed;
	DirectionalLightColourForFrame.green = CTimeCycle::m_fCurrentDirectionalGreen;
	DirectionalLightColourForFrame.blue = CTimeCycle::m_fCurrentDirectionalBlue;
	pDirect->setColor(DirectionalLightColourForFrame.red,
	                  DirectionalLightColourForFrame.green,
	                  DirectionalLightColourForFrame.blue);
	// TODO: transform
}

void
LightsCreate(rw::World *world)
{
	pAmbient = rw::Light::create(rw::Light::AMBIENT);
	pAmbient->setColor(0.25f, 0.2f, 0.25f);
	pAmbient->setFlags(rw::Light::LIGHTATOMICS);

	pDirect = rw::Light::create(rw::Light::DIRECTIONAL);
	pDirect->setFlags(rw::Light::LIGHTATOMICS);
	pDirect->setColor(1.0f, 0.45f, 0.85f);
	pDirect->radius = 2.0f;
	rw::Frame *frm = rw::Frame::create();
	pDirect->setFrame(frm);
	rw::V3d axis = { 1.0f, 1.0f, 0.0f };
	frm->rotate(&axis, 160.0f, rw::COMBINEPRECONCAT);

	world->addLight(pAmbient);
	world->addLight(pDirect);

	for(int i = 0; i < nelem(pExtraDirectionals); i++){
		rw::Light *l = rw::Light::create(rw::Light::DIRECTIONAL);
		pExtraDirectionals[i] = l;
		l->setFlags(0);
		l->setColor(1.0f, 0.5f, 0.0f);
		l->radius = 2.0f;
		l->setFrame(rw::Frame::create());
		world->addLight(l);
	}
}

void
SetAmbientAndDirectionalColours(float mult)
{
	AmbientLightColour.red = clamp(AmbientLightColourForFrame.red * mult);
	AmbientLightColour.green = clamp(AmbientLightColourForFrame.green * mult);
	AmbientLightColour.blue = clamp(AmbientLightColourForFrame.blue * mult);

	DirectionalLightColour.red = clamp(DirectionalLightColourForFrame.red * mult);
	DirectionalLightColour.green = clamp(DirectionalLightColourForFrame.green * mult);
	DirectionalLightColour.blue = clamp(DirectionalLightColourForFrame.blue * mult);

	pAmbient->setColor(AmbientLightColour.red,
	                   AmbientLightColour.green,
	                   AmbientLightColour.blue);
	pDirect->setColor(DirectionalLightColour.red,
	                  DirectionalLightColour.green,
	                  DirectionalLightColour.blue);
}

void
ReSetAmbientAndDirectionalColours(void)
{
	pAmbient->setColor(AmbientLightColourForFrame.red,
	                   AmbientLightColourForFrame.green,
	                   AmbientLightColourForFrame.blue);
	pDirect->setColor(DirectionalLightColourForFrame.red,
	                  DirectionalLightColourForFrame.green,
	                  DirectionalLightColourForFrame.blue);
}

void
DeActivateDirectional(void)
{
	pDirect->setFlags(0);
}

void
ActivateDirectional(void)
{
	pDirect->setFlags(rw::Light::LIGHTATOMICS);
}

void
SetAmbientColours(void)
{
	pAmbient->setColor(AmbientLightColourForFrame.red,
	                   AmbientLightColourForFrame.green,
	                   AmbientLightColourForFrame.blue);
}

void
SetAmbientColoursForPedsCarsAndObjects(void)
{
	pAmbient->setColor(AmbientLightColourForFrame_PedsCarsAndObjects.red,
	                   AmbientLightColourForFrame_PedsCarsAndObjects.green,
	                   AmbientLightColourForFrame_PedsCarsAndObjects.blue);
}

rw::Camera*
CameraCreate(int width, int height, int z)
{
	rw::Camera *cam;
	cam = rw::Camera::create();
	cam->setFrame(rw::Frame::create());
	cam->frameBuffer = rw::Raster::create(width, height, 0, rw::Raster::CAMERA);
	cam->zBuffer = rw::Raster::create(width, height, 0, rw::Raster::ZBUFFER);
	return cam;
}

void
WindowResize(rw::Rect *r)
{
	using namespace rw;

	globals.width = r->w;
	globals.height = r->h;

	rw::Camera *cam = Scene.camera;
	if(cam){
		if(cam->frameBuffer){
			cam->frameBuffer->destroy();
			cam->frameBuffer = nil;
		}
		if(cam->zBuffer){
			cam->zBuffer->destroy();
			cam->zBuffer = nil;
		}
		cam->frameBuffer = Raster::create(r->w, r->h, 0, Raster::CAMERA);
		cam->zBuffer = Raster::create(r->w, r->h, 0, Raster::ZBUFFER);
	}
}

void
DefinedState(void)
{
	SetRenderState(rw::TEXTUREADDRESS, rw::Texture::WRAP);
	SetRenderState(rw::TEXTUREFILTER, rw::Texture::LINEAR);
	SetRenderState(rw::ZTESTENABLE, 1);
	SetRenderState(rw::ZWRITEENABLE, 1);
	SetRenderState(rw::VERTEXALPHA, 0);
	SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
	SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
	SetRenderState(rw::FOGENABLE, 0);
	SetRenderState(rw::ALPHATESTREF, 10);
	SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAGREATEREQUAL);
	uint32 c = RWRGBAINT(CTimeCycle::m_nCurrentFogColourRed,
		CTimeCycle::m_nCurrentFogColourGreen,
		CTimeCycle::m_nCurrentFogColourBlue, 255);
	SetRenderState(rw::FOGCOLOR, c);
}

void
debug(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}

void
RenderScene(void)
{
	CClouds::Render();
	CClouds::RenderHorizon();
	CRenderer::RenderRoads();
	SetRenderState(rw::FOGENABLE, 1);
	CRenderer::RenderEverythingBarRoads();
	DefinedState();
	CWaterLevel::RenderWater();
	CRenderer::RenderFadingInEntities();

	CTest::Render();
}

void
RenderDebugShit(void)
{
	DrawDebugFrustum();

	if(CPad::IsKeyDown(KEY_TAB))
		CRenderer::RenderCollisionLines();
}

void
DoRWStuffStartOfFrame_Horizon(int16 topred, int16 topgreen, int16 topblue,
	int16 botred, int16 botgreen, int16 botblue, int16 alpha)
{
	// GTA uses CameraSize here but it kinda sucks
	Scene.camera->setFOV(CDraw::GetFOV(), (float)globals.width/globals.height);
	// TODO: visibility
	static rw::RGBA clearcol = { 0x40, 0x40, 0x40, 0xFF };
	Scene.camera->clear(&clearcol, rw::Camera::CLEARIMAGE|rw::Camera::CLEARZ);
	Scene.camera->beginUpdate();
	TheCamera.m_viewMatrix.Update();
	CClouds::RenderBackground(topred, topgreen, topblue, botred, botgreen, botblue, alpha);
}

void
DoRWStuffEndOfFrame(void)
{
	Scene.camera->endUpdate();
	Scene.camera->showRaster(0);
}

void
PluginAttach(void)
{
	rw::ps2::registerPDSPlugin(40);
	rw::ps2::registerPluginPDSPipes();

	rw::registerMeshPlugin();
	rw::registerNativeDataPlugin();
	rw::registerAtomicRightsPlugin();
	rw::registerMaterialRightsPlugin();
	rw::xbox::registerVertexFormatPlugin();
	rw::registerSkinPlugin();
	rw::registerHAnimPlugin();

	NodeNamePluginAttach();
	CVisibilityPlugins::PluginAttach();

	rw::registerMatFXPlugin();
}

void
Initialise3D(void)
{
	rw::Engine::init();
	PluginAttach();
	rw::Engine::open(&engineOpenParams);
	rw::Engine::start();
	plAttachInput();

	rw::Texture::setLoadTextures(1);
	rw::d3d::isP8supported = 0;
	//rw::engine->makeDummies = 1;

	rw::Image::setSearchPath("D:\\rockstargames\\ps2\\gta3\\MODELS\\gta3_archive\\txd_extracted\\;");


	CGame::InitialiseRW();
}

void
SystemInit(void)
{
	CFileMgr::Initialise();
}

void
GameInit(void)
{
	Initialise3D();
}

void
TheGame(void)
{
	debug("Into TheGame!!!\n");

	isRunning = 1;

	CGame::Initialise();

	while(isRunning && !plWindowclosed()){
		plHandleEvents();
		CTimer::Update();
		CGame::Process();

		SetLightsWithTimeOfDayColour(Scene.world);

		CRenderer::ConstructRenderList();
		// CRenderer::PreRender();

		// Set the planes before updating the RW cam. GTA (wrongly) does it afterwards.
		Scene.camera->setFarPlane(CTimeCycle::m_fCurrentFarClip);
		Scene.camera->fogPlane = CTimeCycle::m_fCurrentFogStart;
		DoRWStuffStartOfFrame_Horizon(CTimeCycle::m_nCurrentSkyTopRed, CTimeCycle::m_nCurrentSkyTopGreen, CTimeCycle::m_nCurrentSkyTopBlue,
			CTimeCycle::m_nCurrentSkyBottomRed, CTimeCycle::m_nCurrentSkyBottomGreen, CTimeCycle::m_nCurrentSkyBottomBlue, 255);

		DefinedState();

		RenderScene();
		RenderDebugShit();

		// RenderEffects
		// motion blur
		// Render2dStuff

		DoRWStuffEndOfFrame();
	}

	rw::Engine::stop();
}

void
KeyDown(int k)
{
	CPad::tempKeystates[k] = 1;
}

void
KeyUp(int k)
{
	CPad::tempKeystates[k] = 0;
}

void
CharInput(int c)
{
}

void
MouseMove(int x, int y)
{
	CPad::tempMouseState.x = x;
	CPad::tempMouseState.y = y;
}

void
MouseButton(int buttons)
{
	CPad::tempMouseState.btns = buttons;
}
