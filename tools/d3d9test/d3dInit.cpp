#include <d3d9.h>
#include <rw.h>
#include "rwgta.h"
#include "camera.h"
#include <assert.h>

Camera *camera;
rw::Clump *clump;
rw::World *world;
rw::EngineOpenParams engineOpenParams;

bool
Init()
{
//	rw::platform = rw::PLATFORM_D3D8;
//	rw::version = 0x34000;
	rw::Engine::init();
	gta::attachPlugins();
	rw::Engine::open(&engineOpenParams);
	rw::Engine::start();
	rw::Texture::setLoadTextures(1);

	rw::TexDictionary::setCurrent(rw::TexDictionary::create());
	rw::Image::setSearchPath("Y:\\ps2\\gta3\\MODELS\\gta3_archive\\txd_extracted\\;"
	                         "Y:\\ps2\\gtavc\\MODELS\\gta3_archive\\txd_extracted\\;"
	                         "Y:\\ps2\\gtasa\\models\\gta3_archive\\txd_extracted\\");

	if(1){
		//char *filename = "Y:\\pc\\gtasa\\models\\gta3_archive\\admiral.txd";
//		char *filename = "Y:\\pc\\gtavc\\models\\gta3_archive\\admiral.txd";
		char *filename = "data\\admiral.txd";
		rw::StreamFile in;
		if(in.open(filename, "rb") == NULL){
			MessageBox(0, "couldn't open file\n", 0, 0);
			printf("couldn't open file\n");
		}
		rw::findChunk(&in, rw::ID_TEXDICTIONARY, NULL, NULL);
		rw::TexDictionary *txd;
		txd = rw::TexDictionary::streamRead(&in);
		assert(txd);
		in.close();
		rw::TexDictionary::setCurrent(txd);
	}

	char *filename = "data\\admiral.dff";
//	char *filename = "Y:\\pc\\gtavc\\models\\gta3_archive\\admiral.dff";
//	char *filename = "Y:\\pc\\gta3\\models\\gta3_archive\\kuruma.dff";
//	char *filename = "Y:\\pc\\gtavc\\models\\gta3_archive\\player.dff";
//	char *filename = "Y:\\pc\\gtavc\\models\\gta3_archive\\od_newscafe_dy.dff";
//	char *filename = "Y:\\pc\\gtasa\\models\\gta3_archive\\admiral.dff";
//	char *filename = "Y:\\pc\\gtasa\\models\\gta3_archive\\lae2_roads89.dff";
//	char *filename = "Y:\\pc\\gtasa\\models\\gta3_archive\\casinoblock41_nt.dff";
//	char *filename = "Y:\\pc\\gtasa\\models\\cutscene_archive\\csremington92.dff";
//	char *filename = "C:\\gtasa\\test\\hanger.dff";
//	char *filename = "C:\\Users\\aap\\Desktop\\tmp\\out.dff";
//	char *filename = "out2.dff";
//	char *filename = "C:\\Users\\aap\\src\\librw\\tools\\insttest\\out.dff";
	rw::StreamFile in;
	if(in.open(filename, "rb") == NULL){
		MessageBox(0, "couldn't open file\n", 0, 0);
		printf("couldn't open file\n");
	}
	rw::findChunk(&in, rw::ID_CLUMP, NULL, NULL);
	clump = rw::Clump::streamRead(&in);
	assert(clump);
	in.close();

	FORLIST(lnk, clump->atomics){
		rw::Atomic *a = rw::Atomic::fromClump(lnk);
		if(a->pipeline && a->pipeline->platform != rw::platform)
			a->pipeline = NULL;
		//a->getPipeline()->instance(a);

		char *name = gta::getNodeName(a->getFrame());
		if(strstr(name, "_dam") || strstr(name, "_vlo"))
			a->object.object.flags &= ~rw::Atomic::RENDER;
	}

	//rw::StreamFile out;
	//out.open("out.dff", "wb");
	//clump->streamWrite(&out);
	//out.close();

	//out.open("out.txd", "wb");
	//rw::currentTexDictionary->streamWrite(&out);
	//out.close();




	world = rw::World::create();

	rw::Light *ambient = rw::Light::create(rw::Light::AMBIENT);
	ambient->setColor(0.2f, 0.2f, 0.2f);
	world->addLight(ambient);

	rw::V3d xaxis = { 1.0f, 0.0f, 0.0f };
	rw::Light *direct = rw::Light::create(rw::Light::DIRECTIONAL);
	direct->setColor(0.8f, 0.8f, 0.8f);
	direct->setFrame(rw::Frame::create());
	direct->getFrame()->rotate(&xaxis, 180.0f, rw::COMBINEREPLACE);
	world->addLight(direct);

	camera = new Camera;
	camera->m_rwcam = rw::Camera::create();
	camera->m_rwcam->setFrame(rw::Frame::create());
	camera->m_aspectRatio = 640.0f/480.0f;
	camera->m_near = 0.1f;
	camera->m_far = 450.0f;
	camera->m_target.set(0.0f, 0.0f, 0.0f);
	camera->m_position.set(0.0f, -10.0f, 0.0f);
//	camera->setPosition(Vec3(0.0f, 5.0f, 0.0f));
//	camera->setPosition(Vec3(0.0f, -70.0f, 0.0f));
//	camera->setPosition(Vec3(0.0f, -1.0f, 3.0f));
	camera->update();

	world->addCamera(camera->m_rwcam);

	return true;
}

bool
Draw(float timeDelta)
{
	static rw::RGBA clearcol = { 0x80, 0x80, 0x80, 0xFF };
	camera->m_rwcam->clear(&clearcol, rw::Camera::CLEARIMAGE|rw::Camera::CLEARZ);
	camera->update();
	camera->m_rwcam->beginUpdate();

	clump->render();

	camera->m_rwcam->endUpdate();
	camera->m_rwcam->showRaster(0);
	return true;
}

void
Shutdown()
{
	rw::Engine::stop();
}

bool running;

LRESULT CALLBACK
WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch(msg){
	case WM_DESTROY:
		printf("DESTROY\n");
		PostQuitMessage(0);
		break;

	case WM_KEYDOWN:
		switch(wParam){
		case 'W':
			camera->orbit(0.0f, 0.1f);
			break;
		case 'S':
			camera->orbit(0.0f, -0.1f);
			break;
		case 'A':
			camera->orbit(-0.1f, 0.0f);
			break;
		case 'D':
			camera->orbit(0.1f, 0.0f);
			break;
		case VK_UP:
			camera->turn(0.0f, 0.1f);
			break;
		case VK_DOWN:
			camera->turn(0.0f, -0.1f);
			break;
		case VK_LEFT:
			camera->turn(0.1f, 0.0f);
			break;
		case VK_RIGHT:
			camera->turn(-0.1f, 0.0f);
			break;
		case 'R':
			camera->zoom(0.1f);
			break;
		case 'F':
			camera->zoom(-0.1f);
			break;
		case VK_ESCAPE:
			DestroyWindow(hwnd);
			break;
		default:
			printf("%d\n", wParam);
		}
		break;
	case WM_CLOSE:
		printf("CLOSE\n");
		DestroyWindow(hwnd);
		break;

	case WM_QUIT:
		printf("QUIT\n");
		running = false;
		break;
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

HWND
MakeWindow(HINSTANCE instance, int width, int height)
{
	WNDCLASS wc;
	wc.style         = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc   = WndProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = instance;
	wc.hIcon         = LoadIcon(0, IDI_APPLICATION);
	wc.hCursor       = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wc.lpszMenuName  = 0;
	wc.lpszClassName = "librwD3D9";
	if(!RegisterClass(&wc)){
		MessageBox(0, "RegisterClass() - FAILED", 0, 0);
		return 0;
	}

	HWND win;
	win = CreateWindow("librwD3D9", "D3D9 test",
		WS_BORDER | WS_CAPTION | WS_SYSMENU |
		            WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
		0, 0, width, height, 0, 0, instance, 0);
	if(!win){
		MessageBox(0, "CreateWindow() - FAILED", 0, 0);
		return 0;
	}
	ShowWindow(win, SW_SHOW);
	UpdateWindow(win);
	return win;
}

void
pollEvents(void)
{
	MSG msg;
	while(PeekMessage(&msg, 0, 0, 0, PM_REMOVE)){
		if(msg.message == WM_QUIT){
			running = false;
			break;
		}else{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
}

void
MsgLoop(bool (*draw)(float timeDelta))
{
	float lastTime = (float)timeGetTime();
	running = true;
	while(pollEvents(), running){
		float currTime  = (float)timeGetTime();
		float timeDelta = (currTime - lastTime)*0.001f;

		draw(timeDelta);

		lastTime = currTime;
	}
}

int WINAPI
WinMain(HINSTANCE instance, HINSTANCE,
        PSTR cmdLine, int showCmd)
{
	AllocConsole();
	freopen("CONIN$", "r", stdin);
	freopen("CONOUT$", "w", stdout);
	freopen("CONOUT$", "w", stderr);

	HWND win = MakeWindow(instance, 640, 480);
	if(win == 0){
		MessageBox(0, "MakeWindow() - FAILED", 0, 0);
		return 0;
	}
	engineOpenParams.window = win;

	Init();

	MsgLoop(Draw);

	Shutdown();

	return 0;
}
