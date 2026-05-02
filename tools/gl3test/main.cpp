#include "gldemo.h"

Camera *camera;
rw::World *world;
rw::Clump *clump;

rw::Animation *anim;

rw::RGBA clearcol = { 0x40, 0x40, 0x40, 0xFF };

rw::Raster*
d3dToGl3(rw::Raster *raster)
{
	using namespace rw;
	if(raster->platform != PLATFORM_D3D8 &&
	   raster->platform != PLATFORM_D3D9)
		return raster;
	d3d::D3dRaster *natras = PLUGINOFFSET(d3d::D3dRaster,
	                                      raster, d3d::nativeRasterOffset);
	if(natras->format)
		assert(0 && "no custom d3d formats");

	Image *image = raster->toImage();
	raster->destroy();
	raster = Raster::createFromImage(image, PLATFORM_GL3);
	image->destroy();
	return raster;
}

void
im2dshit(void)
{
	static rw::gl3::Im2DVertex verts[] = {
	  { -0.5, -0.5, 0.0,  255, 0, 0, 255,  0.0, 0.0 },
	  {  0.5, -0.5, 0.0,  0, 255, 0, 255,  0.0, 0.0 },
	  { -0.5,  0.5, 0.0,  0, 0, 255, 255,  0.0, 0.0 },
	  {  0.5,  0.5, 0.0,  0, 255, 255, 255,  0.0, 0.0 },
	};
	static short indices[] = {
		0, 1, 2, 3
	};
	rw::engine->im2DRenderIndexedPrimitive(rw::PRIMTYPETRISTRIP,
		&verts, 4, &indices, 4);
}

void
display(void)
{
	camera->update();

	camera->m_rwcam->clear(&clearcol,
		               rw::Camera::CLEARIMAGE | rw::Camera::CLEARZ);
	camera->m_rwcam->beginUpdate();

	clump->render();
	im2dshit();

	camera->m_rwcam->endUpdate();
}

void
update(double t)
{
/*
	rw::HAnimHierarchy *hier = rw::HAnimHierarchy::get(clump);
	hier->currentAnim->addTime(t);
	hier->updateMatrices();
*/
}

void
pollinput(GLFWwindow *window)
{
	Dualshock *ds = &ds3;
	if(ds->start && ds->select)
		glfwSetWindowShouldClose(window, GLFW_TRUE);
	float sensitivity = 1.0f;
	if(ds->r2){
		sensitivity = 2.0f;
		if(ds->l2)
			sensitivity = 4.0f;
	}
	if(ds->square) camera->zoom(0.4f*sensitivity);
	if(ds->cross) camera->zoom(-0.4f*sensitivity);
	camera->orbit(ds->leftX/30.0f*sensitivity,
	              -ds->leftY/30.0f*sensitivity);
	camera->turn(-ds->rightX/30.0f*sensitivity,
	             -ds->rightY/30.0f*sensitivity);
	if(ds->up)
		camera->dolly(0.4f*sensitivity);
	if(ds->down)
		camera->dolly(-0.4f*sensitivity);
}

void
keypress(GLFWwindow *window, int key, int scancode, int action, int mods)
{
	if(action == GLFW_RELEASE)
		return;
	switch(key){
	case GLFW_KEY_W:
		camera->orbit(0.0f, 0.1f);
		break;

	case GLFW_KEY_S:
		camera->orbit(0.0f, -0.1f);
		break;

	case GLFW_KEY_A:
		camera->orbit(-0.1f, 0.0f);
		break;

	case GLFW_KEY_D:
		camera->orbit(0.1f, 0.0f);
		break;

	case GLFW_KEY_UP:
		camera->turn(0.0f, 0.1f);
		break;

	case GLFW_KEY_DOWN:
		camera->turn(0.0f, -0.1f);
		break;

	case GLFW_KEY_LEFT:
		camera->turn(0.1f, 0.0f);
		break;

	case GLFW_KEY_RIGHT:
		camera->turn(-0.1f, 0.0f);
		break;

	case GLFW_KEY_R:
		camera->zoom(-0.4f);
		break;

	case GLFW_KEY_F:
		camera->zoom(0.4f);
		break;

	case GLFW_KEY_ESCAPE:
	case GLFW_KEY_Q:
		glfwSetWindowShouldClose(window, GLFW_TRUE);
		break;
	}
}

// primary:   0x3C 0xFF 0x00
// secondary: 0xFF 0x00 0xAF
void
setCarColor(rw::Clump *clump, rw::RGBA *c1, rw::RGBA *c2)
{
	using namespace rw;
	FORLIST(lnk, clump->atomics){
		Geometry *g = Atomic::fromClump(lnk)->geometry;
		for(int32 i = 0; i < g->matList.numMaterials; i++){
			Material *m = g->matList.materials[i];
			if(m->color.red ==   0x3C &&
			   m->color.green == 0xFF &&
			   m->color.blue ==  0x00){
				m->color.red =   c1->red;
				m->color.green = c1->green;
				m->color.blue =  c1->blue;
			}
			if(m->color.red ==   0xFF &&
			   m->color.green == 0x00 &&
			   m->color.blue ==  0xAF){
				m->color.red =   c2->red;
				m->color.green = c2->green;
				m->color.blue =  c2->blue;
			}
		}
	}
}

void
removeBodyTextures(rw::Clump *clump)
{
	using namespace rw;
	FORLIST(lnk, clump->atomics){
		Geometry *g = Atomic::fromClump(lnk)->geometry;
		for(int32 i = 0; i < g->matList.numMaterials; i++){
			Material *m = g->matList.materials[i];
			if(m->texture == nil) continue;
			if(strstr(m->texture->name, "body")){
				m->texture->destroy();
				m->texture = nil;
			}
		}
	}
}

void
hideSomeAtomics(rw::Clump *clump)
{
	using namespace rw;
	FORLIST(lnk, clump->atomics){
		Atomic *a = Atomic::fromClump(lnk);
		char *name = gta::getNodeName(a->getFrame());
		if(strstr(name, "_dam") || strstr(name, "_vlo"))
			a->object.object.flags &= ~Atomic::RENDER;
	}
}

void
setEnvFrame(rw::Clump *clump)
{
	using namespace rw;
	Frame *f = Frame::create();
	V3d axis = { 1.0, 0.0, 0.0 };
	f->matrix = Matrix::makeRotation(Quat::rotation(1.04f, axis));
	f->updateObjects();
	f->getLTM();

	FORLIST(lnk1, clump->atomics){
		Geometry *g = Atomic::fromClump(lnk1)->geometry;
		for(int32 i = 0; i < g->matList.numMaterials; i++){
			MatFX *mfx = MatFX::get(g->matList.materials[i]);
			if(mfx)
				mfx->setEnvFrame(f);
		}
	}
}

int
initrw(void)
{
	using namespace rw;

	const char *f;
	StreamFile in;

	rw::version = 0x34000;
	rw::platform = PLATFORM_GL3;

	rw::Engine::init();
	gta::attachPlugins();
	rw::Driver::open();
	gl3::initializeRender();
	d3d::isP8supported = 0;
	rw::engine->loadTextures = 1;

//	char path[] = "MODELS\\taxi.dff";
	char path[] = "/home/aap/src/librwgta/tools/gl3test/MODELS\\taxi.dff";
	makePath(path);
	printf("%s\n", path);

	f = "models/TAXI.TXD";
	if(in.open(f, "rb") == nil)
		return 0;
	findChunk(&in, ID_TEXDICTIONARY, nil, nil);
	TexDictionary *testtxd = TexDictionary::streamRead(&in);
	assert(testtxd);
	in.close();

	FORLIST(lnk, testtxd->textures){
		Texture *tex = Texture::fromDict(lnk);
		tex->raster = d3dToGl3(tex->raster);
	//	printf("%s %s\n", tex->name, tex->mask);
	}


//	rw::TexDictionary::setCurrent(TexDictionary::create());
	rw::TexDictionary::setCurrent(testtxd);
//	Image::setSearchPath("/home/aap/vc_textures/");

	// load clump
//	f = "models/od_newscafe_dy.dff";
//	f = "models/admiral.dff";
//	f = "models/player.dff";
//	f = "models/playerx.DFF";
//	f = "test/clump.dff";
//	f = "models/vegetationb03.dff";
//	f = "/home/aap/gamedata/pc/gta3/models/gta3_archive/player.DFF";
	f = "models/taxi.DFF";
	if(in.open(f, "rb") == nil)
		return 0;
	findChunk(&in, ID_CLUMP, nil, nil);
	clump = Clump::streamRead(&in);
	assert(clump);
	in.close();

	// set hierarchy
	HAnimHierarchy *hier = HAnimHierarchy::get(clump);
	if(hier)
		FORLIST(lnk, clump->atomics)
			Skin::setHierarchy(Atomic::fromClump(lnk), hier);
/*
	// load animation
	f = "test/anim0.anm";
	if(in.open(f, "rb") == nil)
		return 0;
	findChunk(&in, ID_ANIMANIMATION, nil, nil);
	anim = Animation::streamRead(&in);
	assert(anim);
	in.close();

	hier->currentAnim->setCurrentAnim(anim);
	hier->updateMatrices();
*/

	removeBodyTextures(clump);
	// taxi yellow
	RGBA c1 = { 243, 237, 71, 255 };
	RGBA c2 = { 243, 237, 71, 255 };
	setCarColor(clump, &c1, &c2);
	setEnvFrame(clump);
	hideSomeAtomics(clump);

	camera = new ::Camera;
	camera->m_rwcam = rw::Camera::create();
	camera->m_rwcam->setFrame(rw::Frame::create());
	camera->m_rwcam->setNearPlane(0.1f);
	camera->m_rwcam->setFarPlane(450.0f);
	camera->m_rwcam->fogPlane = 10.0f;
	camera->m_aspectRatio = 640.0f/480.0f;
	camera->m_target.set(0.0f, 0.0f, 0.0f);
//	camera->m_position.set(0.0f, -30.0f, 4.0f);
	camera->m_position.set(3.0f, 3.0f, 1.0f);

// hanim1 model
//	camera->m_position.set(0.0f, 4.0f, 80.0f);
//	camera->m_up.set(0.0f, 1.0f, 0.0f);
//	camera->m_localup.set(0.0f, 1.0f, 0.0f);

	rw::SetRenderState(rw::FOGENABLE, 1);
	rw::SetRenderState(rw::FOGCOLOR, *(rw::uint32*)&clearcol);

	world = rw::World::create();
	world->addCamera(camera->m_rwcam);

	// Ambient light
	rw::Light *light = rw::Light::create(rw::Light::AMBIENT);
	light->setColor(0.3f, 0.3f, 0.3f);
	world->addLight(light);

	// Diffuse light
	light = rw::Light::create(rw::Light::DIRECTIONAL);
	rw::Frame *frm = rw::Frame::create();
	light->setFrame(frm);
	frm->matrix.pointInDirection((rw::V3d){1.0, 0.0, -0.5},
	                             (rw::V3d){0.0, 0.0, 1.0});
	frm->updateObjects();
	world->addLight(light);

	return 1;
}

int
init(void)
{
	if(!initrw())
		return 0;

	return 1;
}

void
shutdown(void)
{
}
