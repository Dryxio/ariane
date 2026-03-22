#include "euryopa.h"

//#define XINPUT
#ifdef XINPUT
#include <Xinput.h>
#endif

float timeStep;
float avgTimeStep;

SceneGlobals Scene;
rw::EngineOpenParams engineOpenParams;
rw::Light *pAmbient, *pDirect;
rw::Texture *whiteTex;

// TODO: print to log as well
void
panic(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "error: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(1);
}

void
debug(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	addToLogWindow(fmt, ap);
	fflush(stdout);
	va_end(ap);
}

// TODO: do something fancy
void
log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	addToLogWindow(fmt, ap);
	fflush(stdout);
	va_end(ap);
}

char*
getPath(const char *path)
{
	static char cipath[1024];
	strncpy(cipath, path, 1024);
	rw::makePath(cipath);
	return cipath;
}

/* case insensitive fopen */
FILE*
fopen_ci(const char *path, const char *mode)
{
	char cipath[1024];
	strncpy(cipath, path, 1024);
	rw::makePath(cipath);
	return fopen(cipath, mode);
}

bool
doesFileExist(const char *path)
{
	FILE *f;
	f = fopen_ci(path, "r");
	if(f){
		fclose(f);
		return true;
	}
	return false;
}

float
clampFloat(float f, float min, float max)
{
	if(f < min) return min;
	if(f > max) return max;
	return f;
}

#ifdef XINPUT
int pads[4];
int numPads;
int currentPad;

void
plAttachInput(void)
{
	int i;
	XINPUT_STATE state;

	for(i = 0; i < 4; i++)
		if(XInputGetState(i, &state) == ERROR_SUCCESS)
			pads[numPads++] = i;
}

void
plCapturePad(int arg)
{
	currentPad = arg;
	return;
}

void
plUpdatePad(CControllerState *state)
{
	XINPUT_STATE xstate;
	int pad;

	pad = currentPad < numPads ? pads[currentPad] : -1;
	if(pad < 0 || XInputGetState(pad, &xstate) != ERROR_SUCCESS){
		memset(state, 0, sizeof(CControllerState));
		return;
	}

	state->leftX  = 0;
	state->leftY  = 0;
	state->rightX = 0;
	state->rightY = 0;
	if(xstate.Gamepad.sThumbLX > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE || xstate.Gamepad.sThumbLX < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)
		state->leftX = xstate.Gamepad.sThumbLX;
	if(xstate.Gamepad.sThumbLY > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE || xstate.Gamepad.sThumbLY < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)
		state->leftY = xstate.Gamepad.sThumbLY;
	if(xstate.Gamepad.sThumbRX > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE || xstate.Gamepad.sThumbRX < -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE)
		state->rightX = xstate.Gamepad.sThumbRX;
	if(xstate.Gamepad.sThumbRY > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE || xstate.Gamepad.sThumbRY < -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE)
		state->rightY = xstate.Gamepad.sThumbRY;

	state->triangle = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_Y);
	state->circle = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_B);
	state->cross = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_A);
	state->square = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_X);
	state->l1 = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER);
	state->l2 = xstate.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
	state->leftshock = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB);
	state->r1 = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER);
	state->r2 = xstate.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
	state->rightshock = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB);
	state->select = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_BACK);
	state->start = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_START);
	state->up = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP);
	state->right = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
	state->down = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN);
	state->left = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT);

}
#else
void
plAttachInput(void)
{
}
void
plCapturePad(int arg)
{
}
void
plUpdatePad(CControllerState *state)
{
}
#endif

void
Init(void)
{
	sk::globals.windowtitle = "Ariane - Map Editor (GTA III, VC, SA)";
	sk::globals.width = 1280;
	sk::globals.height = 800;
	sk::globals.quit = 0;
}

bool
attachPlugins(void)
{
	gta::attachPlugins();
	RegisterTexStorePlugin();
	RegisterPipes();
	return true;
}

void
DefinedState(void)
{
	SetRenderState(rw::ZTESTENABLE, 1);
	SetRenderState(rw::ZWRITEENABLE, 1);
	SetRenderState(rw::VERTEXALPHA, 0);
	SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
	SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
	SetRenderState(rw::FOGENABLE, 0);
	SetRenderState(rw::ALPHATESTREF, params.alphaRefDefault);
	SetRenderState(rw::ALPHATESTFUNC, rw::ALPHAGREATEREQUAL);
	SetRenderState(rw::GSALPHATEST, params.ps2AlphaTest);
	rw::RGBA fog;
	uint32 c;
	rw::convColor(&fog, &Timecycle::currentFogColour);
	c = RWRGBAINT(fog.red, fog.green, fog.blue, 255);
	SetRenderState(rw::FOGCOLOR, c);
	SetRenderState(rw::CULLMODE, rw::CULLBACK);
}

// Simple function to convert a raster to the current platform.
// TODO: convert custom formats (DXT) properly.
static rw::Raster*
ConvertTexRaster(rw::Raster *ras)
{
	using namespace rw;

	if(ras->platform == rw::platform)
		return ras;
	// compatible platforms
	if(ras->platform == PLATFORM_D3D8 && rw::platform == PLATFORM_D3D9 ||
	   ras->platform == PLATFORM_D3D9 && rw::platform == PLATFORM_D3D8)
		return ras;

	Image *img = ras->toImage();
	ras->destroy();
	img->unpalettize();
	ras = Raster::createFromImage(img);
	img->destroy();
	return ras;
}

void
ConvertTxd(rw::TexDictionary *txd)
{
	rw::Texture *tex;
	FORLIST(lnk, txd->textures){
		tex = rw::Texture::fromDict(lnk);
		rw::Raster *ras = tex->raster;
		if(ras)
			tex->raster = ConvertTexRaster(ras);
		tex->setFilter(rw::Texture::LINEAR);
	}
}

bool
InitRW(void)
{
	if(!sk::InitRW())
		return false;
	rw::d3d::isP8supported = false;

	rw::Image *img = rw::Image::create(1, 1, 32);
	uint32 white = 0xFFFFFFFF;
	img->pixels = (uint8*)&white;
	whiteTex = rw::Texture::create(rw::Raster::createFromImage(img));
	img->destroy();

	Scene.world = rw::World::create();

	pAmbient = rw::Light::create(rw::Light::AMBIENT);
	pAmbient->setColor(0.4f, 0.4f, 0.4f);
//	pAmbient->setColor(1.0f, 1.0f, 1.0f);
	Scene.world->addLight(pAmbient);

	rw::V3d xaxis = { 1.0f, 0.0f, 0.0f };
	pDirect = rw::Light::create(rw::Light::DIRECTIONAL);
	pDirect->setColor(0.8f, 0.8f, 0.8f);
	pDirect->setFrame(rw::Frame::create());
	pDirect->getFrame()->rotate(&xaxis, 180.0f, rw::COMBINEREPLACE);
	Scene.world->addLight(pDirect);

	TheCamera.m_rwcam = sk::CameraCreate(sk::globals.width, sk::globals.height, 1);
	TheCamera.m_rwcam->setFarPlane(5000.0f);
	TheCamera.m_rwcam->setNearPlane(0.1f);
	// Create a second camera to use with a larger draw distance
	TheCamera.m_rwcam_viewer = rw::Camera::create();
	TheCamera.m_rwcam_viewer->setFrame(TheCamera.m_rwcam->getFrame());
	TheCamera.m_rwcam_viewer->frameBuffer = TheCamera.m_rwcam->frameBuffer;
	TheCamera.m_rwcam_viewer->zBuffer = TheCamera.m_rwcam->zBuffer;
	TheCamera.m_rwcam_viewer->setFarPlane(5000.0f);
	TheCamera.m_rwcam_viewer->setNearPlane(0.1f);

	Scene.camera = TheCamera.m_rwcam;
	TheCamera.m_aspectRatio = 640.0f/480.0f;

	TheCamera.m_LODmult = 1.0f;

	Scene.world->addCamera(TheCamera.m_rwcam);
	Scene.world->addCamera(TheCamera.m_rwcam_viewer);

	ImGui_ImplRW_Init();
	ImGui::StyleColorsClassic();

	RenderInit();

	return true;
}


sk::EventStatus
AppEventHandler(sk::Event e, void *param)
{
	using namespace sk;
	Rect *r;
	MouseState *ms;

	ImGuiEventHandler(e, param);

	ImGuiIO &io = ImGui::GetIO();
//	if(io.WantCaptureMouse || ImGuizmo::IsOver())
//		CPad::tempMouseState.btns = 0;

	switch(e){
	case INITIALIZE:
/*
		AllocConsole();
		freopen("CONIN$", "r", stdin);
		freopen("CONOUT$", "w", stdout);
		freopen("CONOUT$", "w", stderr);
*/
		Init();
		plAttachInput();
		return EVENTPROCESSED;
	case RWINITIALIZE:
		return ::InitRW() ? EVENTPROCESSED : EVENTERROR;
	case PLUGINATTACH:
		return attachPlugins() ? EVENTPROCESSED : EVENTERROR;
	case KEYDOWN:
		if(!io.WantCaptureKeyboard && !io.WantTextInput && !ImGuizmo::IsOver())
			CPad::tempKeystates[*(int*)param] = 1;
		return EVENTPROCESSED;
	case KEYUP:
		CPad::tempKeystates[*(int*)param] = 0;
		return EVENTPROCESSED;
	case MOUSEBTN:
		if(!io.WantCaptureMouse && !ImGuizmo::IsOver()){
			ms = (MouseState*)param;
			CPad::tempMouseState.btns = ms->buttons;
		}else
			CPad::tempMouseState.btns = 0;
		return EVENTPROCESSED;
	case MOUSEMOVE:
		ms = (MouseState*)param;
		CPad::tempMouseState.x = ms->posx;
		CPad::tempMouseState.y = ms->posy;
		return EVENTPROCESSED;
	case RESIZE:
		r = (Rect*)param;
		// TODO: register when we're minimized
		if(r->w == 0) r->w = 1;
		if(r->h == 0) r->h = 1;

		sk::globals.width = r->w;
		sk::globals.height = r->h;
		if(TheCamera.m_rwcam){
			sk::CameraSize(TheCamera.m_rwcam, r);
			TheCamera.m_rwcam_viewer->frameBuffer = TheCamera.m_rwcam->frameBuffer;
			TheCamera.m_rwcam_viewer->zBuffer = TheCamera.m_rwcam->zBuffer;
			TheCamera.m_aspectRatio = (float)r->w/r->h;
		}
		break;
	case IDLE:
		timeStep = *(float*)param;
		Idle();
		return EVENTPROCESSED;
	}
	return sk::EVENTNOTPROCESSED;
}
