#include <rw.h>
#include <skeleton.h>
#include <rwgta.h>
#include "camera.h"
#include <string.h>
#include <assert.h>

rw::V3d zero = { 0.0f, 0.0f, 0.0f };
Camera *camera;
struct SceneGlobals {
	rw::World *world;
	rw::Camera *camera;
	rw::Clump *clump;
} Scene;
rw::EngineOpenParams engineOpenParams;

void
Init(void)
{
	sk::globals.windowtitle = "GTA Clump viewer";
	sk::globals.width = 640;
	sk::globals.height = 480;
	sk::globals.quit = 0;
}

bool
attachPlugins(void)
{
	gta::attachPlugins();
	return true;
}

bool
InitRW(void)
{
//	rw::platform = rw::PLATFORM_D3D8;
	if(!sk::InitRW())
		return false;
	rw::Texture::setCreateDummies(1);
	rw::d3d::isP8supported = 0;
	rw::Image::setSearchPath("textures/");

	char *filename;
	if(sk::args.argc > 1)
		filename = sk::args.argv[1];
	else
		return false;
	rw::StreamFile in;
	if(in.open(filename, "rb") == NULL){
		printf("couldn't open file\n");
		return false;
	}
	rw::findChunk(&in, rw::ID_CLUMP, NULL, NULL);
	Scene.clump = rw::Clump::streamRead(&in);
	assert(Scene.clump);
	in.close();

	// TEST - Set texture to the all materials of the clump
//	FORLIST(lnk, Scene.clump->atomics){
//		rw::Atomic *a = rw::Atomic::fromClump(lnk);
//		for(int i = 0; i < a->geometry->matList.numMaterials; i++)
//			a->geometry->matList.materials[i]->setTexture(tex);
//	}
	FORLIST(lnk, Scene.clump->atomics){
		rw::Atomic *a = rw::Atomic::fromClump(lnk);
		char *nodename = gta::getNodeName(a->getFrame());
		if(strstr(nodename, "_vlo") || strstr(nodename, "_dam") ||
		   strstr(nodename, "_lo_"))
			a->object.object.flags &= ~rw::Atomic::RENDER;
	}

	Scene.clump->getFrame()->translate(&zero, rw::COMBINEREPLACE);

	Scene.world = rw::World::create();

	rw::Light *ambient = rw::Light::create(rw::Light::AMBIENT);
	ambient->setColor(0.2f, 0.2f, 0.2f);
	Scene.world->addLight(ambient);

	rw::V3d xaxis = { 1.0f, 0.0f, 0.0f };
	rw::Light *direct = rw::Light::create(rw::Light::DIRECTIONAL);
	direct->setColor(0.8f, 0.8f, 0.8f);
	direct->setFrame(rw::Frame::create());
	direct->getFrame()->rotate(&xaxis, 180.0f, rw::COMBINEREPLACE);
	Scene.world->addLight(direct);

	camera = new Camera;
	Scene.camera = sk::CameraCreate(sk::globals.width, sk::globals.height, 1);
	camera->m_rwcam = Scene.camera;
	camera->m_aspectRatio = 640.0f/480.0f;
	camera->m_near = 0.1f;
	camera->m_far = 450.0f;
	camera->m_target.set(0.0f, 0.0f, 0.0f);
	camera->m_position.set(0.0f, -10.0f, 0.0f);
//	camera->setPosition(Vec3(0.0f, 5.0f, 0.0f));
//	camera->setPosition(Vec3(0.0f, -70.0f, 0.0f));
//	camera->setPosition(Vec3(0.0f, -1.0f, 3.0f));
	camera->update();

	Scene.world->addCamera(camera->m_rwcam);

	return true;
}



void
Draw(float timeDelta)
{
	static rw::RGBA clearcol = { 0x80, 0x80, 0x80, 0xFF };
	camera->m_rwcam->clear(&clearcol, rw::Camera::CLEARIMAGE|rw::Camera::CLEARZ);
	camera->update();
	camera->m_rwcam->beginUpdate();

	Scene.clump->render();

	camera->m_rwcam->endUpdate();
	camera->m_rwcam->showRaster(0);
}


void
KeyUp(int key)
{
}

void
KeyDown(int key)
{
	switch(key){
	case 'W':
		camera->turn(0.0f, 0.1f);
		break;
	case 'S':
		camera->turn(0.0f, -0.1f);
		break;
	case 'A':
		camera->turn(-0.1f, 0.0f);
		break;
	case 'D':
		camera->turn(0.1f, 0.0f);
		break;
	case sk::KEY_UP:
		camera->orbit(0.0f, 0.1f);
		break;
	case sk::KEY_DOWN:
		camera->orbit(0.0f, -0.1f);
		break;
	case sk::KEY_LEFT:
		camera->orbit(0.1f, 0.0f);
		break;
	case sk::KEY_RIGHT:
		camera->orbit(-0.1f, 0.0f);
		break;
	case 'R':
		camera->zoom(0.1f);
		break;
	case 'F':
		camera->zoom(-0.1f);
		break;
	case sk::KEY_ESC:
		sk::globals.quit = 1;
		break;
	}
}

sk::EventStatus
AppEventHandler(sk::Event e, void *param)
{
	using namespace sk;
	Rect *r;
	switch(e){
	case INITIALIZE:
/*
		AllocConsole();
		freopen("CONIN$", "r", stdin);
		freopen("CONOUT$", "w", stdout);
		freopen("CONOUT$", "w", stderr);
*/

		Init();
		return EVENTPROCESSED;
	case RWINITIALIZE:
		return ::InitRW() ? EVENTPROCESSED : EVENTERROR;
	case PLUGINATTACH:
		return attachPlugins() ? EVENTPROCESSED : EVENTERROR;
	case KEYDOWN:
		KeyDown(*(int*)param);
		return EVENTPROCESSED;
	case KEYUP:
		KeyUp(*(int*)param);
		return EVENTPROCESSED;
	case RESIZE:
		r = (Rect*)param;
		sk::globals.width = r->w;
		sk::globals.height = r->h;
		// TODO: set aspect ratio
		if(Scene.camera)
			sk::CameraSize(Scene.camera, r);
		break;
	case IDLE:
		Draw(*(float*)param);
		return EVENTPROCESSED;
	}
	return sk::EVENTNOTPROCESSED;
}
