#include "euryopa.h"
#include "version.h"
#include "updater.h"
#include "telemetry.h"
#include "modloader.h"
#include <stdlib.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

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
static char gHotReloadTracePath[1024];
static char gImGuiIniPath[1024];

static bool
EnsureDirectoryTree(const char *path)
{
	if(path == nil || path[0] == '\0')
		return false;

	char dirpath[1024];
	strncpy(dirpath, path, sizeof(dirpath)-1);
	dirpath[sizeof(dirpath)-1] = '\0';

	for(char *p = dirpath; *p; p++){
		if(*p != '/' && *p != '\\')
			continue;
		char saved = *p;
		*p = '\0';
		if(dirpath[0] != '\0'){
#ifdef _WIN32
			_mkdir(dirpath);
#else
			mkdir(dirpath, 0777);
#endif
		}
		*p = saved;
	}
#ifdef _WIN32
	_mkdir(dirpath);
#else
	mkdir(dirpath, 0777);
#endif
	return true;
}

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

bool
GetEditorRootDirectory(char *dir, size_t size)
{
	if(size == 0)
		return false;

#ifdef _WIN32
	DWORD len = GetModuleFileNameA(nil, dir, (DWORD)size);
	if(len > 0 && len < size){
		for(int i = (int)len - 1; i >= 0; i--){
			if(dir[i] == '\\' || dir[i] == '/'){
				dir[i] = '\0';
				return true;
			}
		}
	}

	len = GetCurrentDirectoryA((DWORD)size, dir);
	return len > 0 && len < size;
#elif defined(__APPLE__)
	uint32_t bufsize = (uint32_t)size;
	if(_NSGetExecutablePath(dir, &bufsize) == 0){
		char *slash = strrchr(dir, '/');
		if(slash){ *slash = '\0'; return true; }
	}
	strncpy(dir, ".", size);
	dir[size - 1] = '\0';
	return true;
#else
	ssize_t len = readlink("/proc/self/exe", dir, size - 1);
	if(len > 0){
		dir[len] = '\0';
		char *slash = strrchr(dir, '/');
		if(slash){ *slash = '\0'; return true; }
	}
	strncpy(dir, ".", size);
	dir[size - 1] = '\0';
	return true;
#endif
}

static char gCachedGameRoot[1024] = "";

bool
GetGameRootDirectory(char *dir, size_t size)
{
	if(size == 0)
		return false;

	if(gCachedGameRoot[0] != '\0'){
		strncpy(dir, gCachedGameRoot, size-1);
		dir[size-1] = '\0';
		return true;
	}

#ifdef _WIN32
	DWORD len = GetCurrentDirectoryA((DWORD)size, dir);
	if(len > 0 && len < size){
		strncpy(gCachedGameRoot, dir, sizeof(gCachedGameRoot)-1);
		gCachedGameRoot[sizeof(gCachedGameRoot)-1] = '\0';
		return true;
	}
	return false;
#else
	if(getcwd(dir, size) == nil)
		return false;
	strncpy(gCachedGameRoot, dir, sizeof(gCachedGameRoot)-1);
	gCachedGameRoot[sizeof(gCachedGameRoot)-1] = '\0';
	return true;
#endif
}

bool
BuildPath(char *dst, size_t size, const char *dir, const char *name)
{
	if(size == 0)
		return false;
	if(dir == nil || dir[0] == '\0')
		return snprintf(dst, size, "%s", name) < (int)size;

	size_t len = strlen(dir);
#ifdef _WIN32
	const char *sep = (dir[len-1] == '\\' || dir[len-1] == '/') ? "" : "\\";
#else
	const char *sep = (dir[len-1] == '/') ? "" : "/";
#endif
	return snprintf(dst, size, "%s%s%s", dir, sep, name) < (int)size;
}

static bool
SetEditorWorkingDirectory(void)
{
#ifdef _WIN32
	char rootDir[1024];
	return GetEditorRootDirectory(rootDir, sizeof(rootDir)) &&
	       SetCurrentDirectoryA(rootDir) != 0;
#else
	return true;
#endif
}

bool
EnsureParentDirectoriesForPath(const char *path)
{
	if(path == nil || path[0] == '\0')
		return false;

	char dirpath[1024];
	strncpy(dirpath, path, sizeof(dirpath)-1);
	dirpath[sizeof(dirpath)-1] = '\0';
	char *slash = strrchr(dirpath, '/');
#ifdef _WIN32
	char *backslash = strrchr(dirpath, '\\');
	if(backslash && (slash == nil || backslash > slash))
		slash = backslash;
#endif
	if(slash == nil)
		return true;
	*slash = '\0';
	if(dirpath[0] == '\0')
		return true;
	return EnsureDirectoryTree(dirpath);
}

bool
GetArianeDataDirectory(char *dir, size_t size)
{
	char rootDir[1024];
	if(!GetEditorRootDirectory(rootDir, sizeof(rootDir)) ||
	   !BuildPath(dir, size, rootDir, "ariane"))
		return false;
	return EnsureDirectoryTree(dir);
}

bool
GetArianeDataPath(char *dst, size_t size, const char *name)
{
	char dir[1024];
	return GetArianeDataDirectory(dir, sizeof(dir)) &&
	       BuildPath(dst, size, dir, name);
}

FILE*
fopenArianeDataRead(const char *name, const char *legacyName)
{
	char path[1024];
	FILE *f = nil;

	if(name && GetArianeDataPath(path, sizeof(path), name))
		f = fopen(path, "r");
	if(f == nil && legacyName)
		f = fopen(legacyName, "r");
	return f;
}

FILE*
fopenArianeDataWrite(const char *name)
{
	char path[1024];
	if(name == nil || !GetArianeDataPath(path, sizeof(path), name) ||
	   !EnsureParentDirectoriesForPath(path))
		return nil;
	return fopen(path, "w");
}

void
setHotReloadTracePath(const char *path)
{
	if(path == nil){
		gHotReloadTracePath[0] = '\0';
		return;
	}
	strncpy(gHotReloadTracePath, path, sizeof(gHotReloadTracePath));
	gHotReloadTracePath[sizeof(gHotReloadTracePath)-1] = '\0';
}

void
hotReloadTrace(const char *fmt, ...)
{
	char defaultPath[1024];
	const char *path = gHotReloadTracePath;
	if(path[0] == '\0'){
		if(GetArianeDataPath(defaultPath, sizeof(defaultPath), "ariane_hot_reload_log.txt"))
			path = defaultPath;
		else
			path = "ariane_hot_reload_log.txt";
	}
	FILE *f = fopen(path, "a");
	if(f == nil)
		return;

	va_list ap;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fclose(f);
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
	const char *redirect = ModloaderRedirectPath(path);
	if(redirect)
		return fopen(redirect, mode);

	char cipath[1024];
	if(path[0] == '/' || path[0] == '\\' || (path[0] != '\0' && path[1] == ':')){
		strncpy(cipath, path, sizeof(cipath)-1);
		cipath[sizeof(cipath)-1] = '\0';
		rw::makePath(cipath);
	}else{
		char gameRoot[1024];
		if(GetGameRootDirectory(gameRoot, sizeof(gameRoot)) &&
		   BuildPath(cipath, sizeof(cipath), gameRoot, path)){
			rw::makePath(cipath);
		}else{
			strncpy(cipath, path, sizeof(cipath)-1);
			cipath[sizeof(cipath)-1] = '\0';
			rw::makePath(cipath);
		}
	}
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

static void
ClearEditorInputState(void)
{
	memset(CPad::oldKeystates, 0, sizeof(CPad::oldKeystates));
	memset(CPad::newKeystates, 0, sizeof(CPad::newKeystates));
	memset(CPad::tempKeystates, 0, sizeof(CPad::tempKeystates));
	memset(&CPad::oldMouseState, 0, sizeof(CPad::oldMouseState));
	memset(&CPad::newMouseState, 0, sizeof(CPad::newMouseState));
	memset(&CPad::tempMouseState, 0, sizeof(CPad::tempMouseState));
	CPad::clickState = 0;
	CPad::clickx = 0;
	CPad::clicky = 0;
	CPad::clickbtn = 0;
	for(int i = 0; i < 2; i++){
		CPad::Pads[i].OldState.Clear();
		CPad::Pads[i].NewState.Clear();
	}

	if(ImGui::GetCurrentContext()){
		ImGuiIO &io = ImGui::GetIO();
		io.MouseDown[0] = false;
		io.MouseDown[1] = false;
		io.MouseDown[2] = false;
		if(ImGui::IsKeyDown(ImGuiKey_LeftShift)) io.AddKeyEvent(ImGuiKey_LeftShift, false);
		if(ImGui::IsKeyDown(ImGuiKey_RightShift)) io.AddKeyEvent(ImGuiKey_RightShift, false);
		if(ImGui::IsKeyDown(ImGuiKey_LeftCtrl)) io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
		if(ImGui::IsKeyDown(ImGuiKey_RightCtrl)) io.AddKeyEvent(ImGuiKey_RightCtrl, false);
		if(ImGui::IsKeyDown(ImGuiKey_LeftAlt)) io.AddKeyEvent(ImGuiKey_LeftAlt, false);
		if(ImGui::IsKeyDown(ImGuiKey_RightAlt)) io.AddKeyEvent(ImGuiKey_RightAlt, false);
		io.KeyShift = false;
		io.KeyCtrl = false;
		io.KeyAlt = false;
		io.KeySuper = false;
	}
}

#ifdef _WIN32
static bool
isEditorWindowActive(void)
{
#ifdef RW_D3D9
	HWND hwnd = (HWND)engineOpenParams.window;
	return hwnd && GetForegroundWindow() == hwnd;
#else
	return true;
#endif
}

static bool
isVirtualKeyDown(int vk)
{
	return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

static void
SyncEditorInputState(void)
{
	static bool hadFocus = true;

	if(!isEditorWindowActive()){
		if(hadFocus)
			ClearEditorInputState();
		hadFocus = false;
		return;
	}
	hadFocus = true;

	CPad::tempKeystates[KEY_LSHIFT] = isVirtualKeyDown(VK_LSHIFT);
	CPad::tempKeystates[KEY_RSHIFT] = isVirtualKeyDown(VK_RSHIFT);
	CPad::tempKeystates[KEY_LCTRL] = isVirtualKeyDown(VK_LCONTROL);
	CPad::tempKeystates[KEY_RCTRL] = isVirtualKeyDown(VK_RCONTROL);
	CPad::tempKeystates[KEY_LALT] = isVirtualKeyDown(VK_LMENU);
	CPad::tempKeystates[KEY_RALT] = isVirtualKeyDown(VK_RMENU);

	uint32 physicalMouseBtns =
		(isVirtualKeyDown(VK_LBUTTON) ? 1 : 0) |
		(isVirtualKeyDown(VK_MBUTTON) ? 2 : 0) |
		(isVirtualKeyDown(VK_RBUTTON) ? 4 : 0);
	CPad::tempMouseState.btns = physicalMouseBtns;

	if(ImGui::GetCurrentContext()){
		ImGuiIO &io = ImGui::GetIO();
		io.MouseDown[0] = !!(physicalMouseBtns & 1);
		io.MouseDown[1] = !!(physicalMouseBtns & 4);
		io.MouseDown[2] = !!(physicalMouseBtns & 2);
		if(io.WantCaptureMouse || ImGuizmo::IsOver() || gGizmoHovered || gGizmoUsing)
			CPad::tempMouseState.btns = 0;
		if(ImGui::IsKeyDown(ImGuiKey_LeftShift) != (CPad::tempKeystates[KEY_LSHIFT] != 0))
			io.AddKeyEvent(ImGuiKey_LeftShift, CPad::tempKeystates[KEY_LSHIFT] != 0);
		if(ImGui::IsKeyDown(ImGuiKey_RightShift) != (CPad::tempKeystates[KEY_RSHIFT] != 0))
			io.AddKeyEvent(ImGuiKey_RightShift, CPad::tempKeystates[KEY_RSHIFT] != 0);
		if(ImGui::IsKeyDown(ImGuiKey_LeftCtrl) != (CPad::tempKeystates[KEY_LCTRL] != 0))
			io.AddKeyEvent(ImGuiKey_LeftCtrl, CPad::tempKeystates[KEY_LCTRL] != 0);
		if(ImGui::IsKeyDown(ImGuiKey_RightCtrl) != (CPad::tempKeystates[KEY_RCTRL] != 0))
			io.AddKeyEvent(ImGuiKey_RightCtrl, CPad::tempKeystates[KEY_RCTRL] != 0);
		if(ImGui::IsKeyDown(ImGuiKey_LeftAlt) != (CPad::tempKeystates[KEY_LALT] != 0))
			io.AddKeyEvent(ImGuiKey_LeftAlt, CPad::tempKeystates[KEY_LALT] != 0);
		if(ImGui::IsKeyDown(ImGuiKey_RightAlt) != (CPad::tempKeystates[KEY_RALT] != 0))
			io.AddKeyEvent(ImGuiKey_RightAlt, CPad::tempKeystates[KEY_RALT] != 0);
	}
}
#else
static void
SyncEditorInputState(void)
{
}
#endif

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
	static char windowTitle[256];
	static bool saveHookRegistered;
	snprintf(windowTitle, sizeof(windowTitle),
		"Ariane %s [%s] - Map Editor (GTA III, VC, SA)",
		ARIANE_VERSION, ARIANE_CHANNEL_DISPLAY);
	sk::globals.windowtitle = windowTitle;
	sk::globals.width = 1280;
	sk::globals.height = 800;
	LoadInitialEditorWindowState(&sk::globals.width, &sk::globals.height);
	sk::globals.quit = 0;
	if(!saveHookRegistered){
		atexit(SaveEditorSettingsNow);
		saveHookRegistered = true;
	}

	if(!SetEditorWorkingDirectory())
		debug("warning: couldn't set working directory to editor root\n");
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
static rw::Raster*
CreateFallbackRaster(void)
{
	rw::Image *img = rw::Image::create(1, 1, 32);
	uint32 white = 0xFFFFFFFF;
	img->pixels = (uint8*)&white;
	rw::Raster *ras = rw::Raster::createFromImage(img);
	img->destroy();
	return ras;
}

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

#ifndef RW_GL3
	Image *img = ras->toImage();
	if(img == nil){
		log("warning: failed to convert raster from platform %d to %d, using fallback texture\n",
			ras->platform, rw::platform);
		ras->destroy();
		return CreateFallbackRaster();
	}
	ras->destroy();
	img->unpalettize();
	ras = Raster::createFromImage(img);
	img->destroy();
	if(ras == nil){
		log("warning: failed to create converted raster for platform %d, using fallback texture\n",
			rw::platform);
		return CreateFallbackRaster();
	}
	return ras;
#else
	Raster *converted = Raster::convertTexToCurrentPlatform(ras);
	if(converted == nil){
		log("warning: failed to create converted raster for platform %d, using fallback texture\n",
			rw::platform);
		return CreateFallbackRaster();
	}
	return converted;
#endif
}

void
ConvertTxd(rw::TexDictionary *txd)
{
	rw::Texture *tex;
	if(txd == nil)
		return;
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
	LoadInitialAntialiasingSettings();
	// Note: Antialiasing configuration removed from librw skeleton API
	// AA settings are now handled through engine params if supported
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
	char rootDir[1024];
	if(GetEditorRootDirectory(rootDir, sizeof(rootDir)) &&
	   BuildPath(gImGuiIniPath, sizeof(gImGuiIniPath), rootDir, "imgui.ini")){
		ImGuiIO &io = ImGui::GetIO();
		io.IniFilename = gImGuiIniPath;
	}
	SetupFonts();
	SetupStyle();

	RenderInit();

	UpdaterCheckForUpdate();
	TelemetrySendPing();

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
		if(!::InitRW())
			return EVENTERROR;
		ApplyInitialEditorWindowState();
		return EVENTPROCESSED;
	case PLUGINATTACH:
		return attachPlugins() ? EVENTPROCESSED : EVENTERROR;
	case KEYDOWN:
		if(!io.WantTextInput && !ImGuizmo::IsOver())
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
		OnEditorWindowResized(r->w, r->h);
		if(TheCamera.m_rwcam){
			sk::CameraSize(TheCamera.m_rwcam, r);
			TheCamera.m_rwcam_viewer->frameBuffer = TheCamera.m_rwcam->frameBuffer;
			TheCamera.m_rwcam_viewer->zBuffer = TheCamera.m_rwcam->zBuffer;
			TheCamera.m_aspectRatio = (float)r->w/r->h;
		}
		break;
	// Note: FILEDROP event removed from librw skeleton
	// File drag & drop functionality disabled for now
	// TODO: Re-implement using platform-specific APIs if needed
	case IDLE:
		SyncEditorInputState();
		UpdateEditorWindowState();
		timeStep = *(float*)param;
		Idle();
		return EVENTPROCESSED;
	}
	return sk::EVENTNOTPROCESSED;
}
