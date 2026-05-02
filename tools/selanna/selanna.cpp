#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

#include <rw.h>
#include <args.h>

#include <rwgta.h>
#include "sol/sol.hpp"

using namespace std;
using namespace rw;

char*
getFrameUserName(rw::Frame *f)
{
	using namespace rw;
	int32 i;
	UserDataArray *ar;
	int32 n = UserDataArray::frameGetCount(f);
	for(i = 0; i < n; i++){
		ar = UserDataArray::frameGet(f, i);
		if(strcmp(ar->name, "name") == 0 && ar->datatype == rw::USERDATASTRING)
			return ar->getString(0);
	}
	return (char*)"";
}

char*
getFrameName(Frame *f)
{
	char *name = gta::getNodeName(f);
	if(name[0] == '\0')
		name = getFrameUserName(f);
	return name;
}

sol::state lua;

int
main(int argc, char *argv[])
{

	lua.open_libraries(sol::lib::base,
		sol::lib::io,
		sol::lib::os,
		sol::lib::string,
		sol::lib::math,
		sol::lib::table);

	sol::table rwtab = lua["rw"].get_or_create<sol::table>();
	sol::table gtatab = lua["gta"].get_or_create<sol::table>();

	rwtab.set_function("init", [](){
		rw::version = 0x34003;
		rw::platform = rw::PLATFORM_D3D8;

		rw::Engine::init();
		gta::attachPlugins();
		rw::Engine::open(nil);
		rw::Engine::start();

		rw::Texture::setCreateDummies(1);
	});

	rwtab.set_function("makePath", [](const char *path){
		char *x = strdup(path);
		rw::makePath(x);
		std::string s(x);
		free(x);
		return s;
	});

	rwtab.set_function("V3d", rw::makeV3d);
	lua.new_usertype<rw::V3d>("V3d",
		sol::no_constructor,
		"x", &rw::V3d::x,
		"y", &rw::V3d::y,
		"z", &rw::V3d::z,
		sol::meta_function::to_string, [](const rw::V3d &v) {
			return "V3d(" + std::to_string(v.x) + ", "
			+ std::to_string(v.y) + ", "
			+ std::to_string(v.z) + ")";
		},
		sol::meta_function::addition,       [](const rw::V3d& a, const rw::V3d& b) { return rw::add(a, b); },
		sol::meta_function::subtraction,    [](const rw::V3d& a, const rw::V3d& b) { return rw::sub(a, b); },
		sol::meta_function::multiplication, [](const rw::V3d& a, float s)          { return rw::scale(a, s); }
	);

	rwtab.set_function("RGBA", makeRGBA);
	lua.new_usertype<rw::RGBA>("RGBA",
		sol::no_constructor,
		"r", &rw::RGBA::red,
		"g", &rw::RGBA::green,
		"b", &rw::RGBA::blue,
		"a", &rw::RGBA::alpha,
		sol::meta_function::to_string, [](const rw::RGBA &c) {
			return "RGBA(" + std::to_string(c.red) + ", "
			+ std::to_string(c.green) + ", "
			+ std::to_string(c.blue) + ", "
			+ std::to_string(c.alpha) + ")";
		}
	);

	lua.new_usertype<rw::Clump>("Clump",
		sol::no_constructor,
		"destroy", &rw::Clump::destroy,
		"getFrame", &rw::Clump::getFrame,
		"atomics", [](rw::Clump *c) {
			rw::LLLink *next = nil;
			rw::LLLink *lnk = c->atomics.link.next;
			return sol::as_function([c,next,lnk]() mutable -> rw::Atomic* {
				if(lnk == c->atomics.end()) return nil;
				rw::Atomic *a = Atomic::fromClump(lnk);
				lnk = lnk->next;
				return a;
			});
		},
		"writeFile", [](rw::Clump *c, const char *path) -> rw::Clump* {
			StreamFile stream;
			if(!stream.open(path, "wb"))
				return nil;
			c->streamWrite(&stream);
			stream.close();
			return c;
		}
	);
	lua.new_usertype<rw::Frame>("Frame",
		sol::no_constructor,
		"getName", [](rw::Frame *f) -> std::string {
			return getFrameName(f);
		},
		"count", &rw::Frame::count,
		"getParent", &rw::Frame::getParent,
		"getRoot", [](rw::Frame *f) -> rw::Frame* { return f->root; },
		"children", [](rw::Frame *f) {
			rw::Frame *cur = f->child;
			rw::Frame *next = cur ? cur->next : nil;
			return sol::as_function([cur,next]() mutable -> rw::Frame* {
				rw::Frame *f = cur;
				cur = next;
				next = cur ? cur->next : nil;
				return f;
			});
		}
	);
	lua.new_usertype<rw::Atomic>("Atomic",
		sol::no_constructor,
		"destroy", &rw::Atomic::destroy,
		"getFrame", &rw::Atomic::getFrame,
		"getGeometry", [](rw::Atomic *a) -> rw::Geometry* { return a->geometry; }
	);

	lua.new_usertype<rw::Geometry>("Geometry",
		sol::no_constructor,
		"destroy", &rw::Geometry::destroy,
		"getFlags", [](rw::Geometry *g) { return g->flags; },
		"getNumTriangles", [](rw::Geometry *g) { return g->numTriangles; },
		"getNumVertices", [](rw::Geometry *g) { return g->numVertices; },
		"getNumTexCoordSets", [](rw::Geometry *g) { return g->numTexCoordSets; },
		"getNumMaterials", [](rw::Geometry *g) { return g->matList.numMaterials; },
		"getMaterial", [](rw::Geometry *g, int i) -> rw::Material* {
			if(i < 0 || i >= g->matList.numMaterials)
				return nil;
			return g->matList.materials[i];
		},
		"materials", [](rw::Geometry *g) {
			int i = 0;
			return sol::as_function([g,i]() mutable -> rw::Material* {
				if(i < g->matList.numMaterials)
					return g->matList.materials[i++];
				return nil;
			});
		}
	);

	lua.new_usertype<rw::Material>("Material",
		sol::no_constructor,
		"getColor", [](rw::Material *m) -> rw::RGBA { return m->color; },
		"getSurfaceProps", [](rw::Material *m) -> rw::SurfaceProperties* { return &m->surfaceProps; },
		"getTexture", [](rw::Material *m) -> rw::Texture* { return m->texture; },
		"getMatFX", [](rw::Material *m) -> rw::MatFX* { return MatFX::get(m); }
	);

	lua.new_usertype<rw::SurfaceProperties>("SurfaceProperties",
		sol::no_constructor,
		"ambient", &rw::SurfaceProperties::ambient,
		"specular", &rw::SurfaceProperties::specular,
		"diffuse", &rw::SurfaceProperties::diffuse,
		sol::meta_function::to_string, [](const rw::SurfaceProperties &s) {
			return "SurfaceProperties(" + std::to_string(s.ambient) + ", "
			+ std::to_string(s.specular) + ", "
			+ std::to_string(s.diffuse) + ")";
		}
	);

	lua.new_usertype<rw::Texture>("Texture",
		sol::no_constructor,
		"getName", [](rw::Texture *t) -> std::string { return std::string(t->name); },
		"getMaskName", [](rw::Texture *t) -> std::string { return std::string(t->mask); },
		sol::meta_function::to_string, [](const rw::Texture *t) {
			return "Texture(" + std::string(t->name) + ", " + std::string(t->mask) + ")";
		}
	);

	lua.new_usertype<rw::MatFX>("MatFX",
		sol::no_constructor,
		"getEnvCoefficient", &rw::MatFX::getEnvCoefficient,
		"getEnvTexture",  &rw::MatFX::getEnvTexture
	);

	rwtab.set_function("readClumpFile", [](sol::this_state s,
			const char *name, int offset, int size){
		sol::state_view lua(s);
		auto lose = sol::object(sol::lua_nil);
		StreamFile stream;
		if(!stream.open(name, "rb"))
			return lose;
		stream.seek(offset);

		ChunkHeaderInfo header;
		readChunkHeaderInfo(&stream, &header);
		if(header.type != ID_CLUMP)
			return lose;

		Clump *clump = Clump::streamRead(&stream);
		stream.close();
		if(clump == NULL)
			return lose;
		return sol::make_object(lua, clump);
	});

	lua.script_file("gta.lua");
	lua.script_file("init.lua");
	return 0;
}
