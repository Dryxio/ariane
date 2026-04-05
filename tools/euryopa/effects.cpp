#include "euryopa.h"
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

void
Effect::JumpTo(ObjectInst *inst)
{
	rw::V3d p = pos;
	if(inst) rw::V3d::transformPoints(&p, &p, 1, &inst->m_matrix);
	TheCamera.setTarget(p);
}

namespace Effects {

std::vector<Effect> effects;
static std::unordered_map<std::string, rw::Texture*> coronaTextures;
float gEffectDrawDist = 150.0f;

enum LightFlags {
	LIGHTFLAG_CHECK_OBSTACLES = 1 << 0,
	LIGHTFLAG_FOG_TYPE_0      = 1 << 1,
	LIGHTFLAG_FOG_TYPE_1      = 1 << 2,
	LIGHTFLAG_WITHOUT_CORONA  = 1 << 3,
	LIGHTFLAG_ONLY_LONG_DIST  = 1 << 4,
	LIGHTFLAG_AT_DAY          = 1 << 5,
	LIGHTFLAG_AT_NIGHT        = 1 << 6,
	LIGHTFLAG_BLINKING_1      = 1 << 7,
	LIGHTFLAG_ONLY_FROM_BELOW = 1 << 8,
	LIGHTFLAG_BLINKING_2      = 1 << 9,
	LIGHTFLAG_UPDATE_HEIGHT   = 1 << 10,
	LIGHTFLAG_CHECK_DIR       = 1 << 11,
	LIGHTFLAG_BLINKING_3      = 1 << 12
};

void
AddEffect(Effect e)
{
	ObjectDef *def = GetObjectDef(e.id);
	if(def == nil){
		log("Effect for non-existing object %d\n", e.id);
		return;
	}
	if(def->m_effectIndex < 0){
		def->m_effectIndex = effects.size();
		def->m_numEffects = 0;
	}
	assert(def->m_effectIndex >= 0);
	def->m_numEffects++;
	effects.push_back(e);
}

Effect*
GetEffect(int idx)
{
	assert(idx >= 0);
	assert(idx < effects.size());
	return &effects[idx];
}

static rw::Texture*
GetCoronaTexture(const char *name)
{
	static int particleTxd = -2;
	const char *texName = name && name[0] ? name : "corona";
	std::unordered_map<std::string, rw::Texture*>::iterator it;

	it = coronaTextures.find(texName);
	if(it != coronaTextures.end())
		return it->second;

	if(particleTxd == -2)
		particleTxd = FindTxdSlot("particle");

	rw::Texture *tex = nil;
	if(particleTxd >= 0){
		TxdPush();
		TxdMakeCurrent(particleTxd);
		tex = rw::Texture::read(texName, nil);
		TxdPop();
	}

	if(tex == nil && rw::strcmp_ci(texName, "corona") != 0){
		it = coronaTextures.find("corona");
		if(it != coronaTextures.end())
			tex = it->second;
		else if(particleTxd >= 0){
			TxdPush();
			TxdMakeCurrent(particleTxd);
			tex = rw::Texture::read("corona", nil);
			TxdPop();
			coronaTextures["corona"] = tex;
		}
	}

	coronaTextures[texName] = tex;
	return tex;
}

static bool
IsLightVisible(const Effect *e, const rw::V3d &pos)
{
	bool atDay = !!(e->light.flags & LIGHTFLAG_AT_DAY);
	bool atNight = !!(e->light.flags & LIGHTFLAG_AT_NIGHT);

	if(e->light.flags & LIGHTFLAG_WITHOUT_CORONA)
		return false;
	if(e->light.flags & LIGHTFLAG_ONLY_FROM_BELOW && TheCamera.m_position.z > pos.z)
		return false;
	if(atDay && !atNight){
		if(currentHour < 6 || currentHour >= 19)
			return false;
	}else if(atNight && !atDay){
		if(currentHour >= 6 && currentHour < 19)
			return false;
	}
	return true;
}

static void
RenderLightEffect(Effect *e, ObjectInst *inst)
{
	rw::V3d pos, screen;
	float w, h;
	float dist;
	float maxDist;
	float fade;
	float brightness;
	float scale;
	int r, g, b, intensity;
	rw::Texture *tex;

	assert(e);
	assert(inst);

	rw::V3d::transformPoints(&pos, &e->pos, 1, &inst->m_matrix);
	if(!IsLightVisible(e, pos))
		return;

	dist = TheCamera.distanceTo(pos);
	maxDist = e->light.lodDist > 0.0f ? e->light.lodDist : gEffectDrawDist;
	if(dist > maxDist)
		return;
	if(!Sprite::CalcScreenCoors(pos, &screen, &w, &h, true))
		return;

	tex = GetCoronaTexture(e->light.coronaTex);
	if(tex == nil || tex->raster == nil)
		return;

	fade = 1.0f;
	if(maxDist > 0.0f)
		fade = clamp(1.0f - dist/maxDist, 0.0f, 1.0f);

	brightness = clamp(Timecycle::currentColours.sprBght / 10.0f, 0.1f, 2.0f);
	scale = 1.0f;

	r = (int)clamp(e->col.red * brightness, 0.0f, 255.0f);
	g = (int)clamp(e->col.green * brightness, 0.0f, 255.0f);
	b = (int)clamp(e->col.blue * brightness, 0.0f, 255.0f);
	intensity = (int)clamp(255.0f * fade, 0.0f, 255.0f);

	w *= std::max(e->light.coronaSize, 0.1f);
	h *= std::max(e->light.coronaSize * scale, 0.1f);

	rw::SetRenderStatePtr(rw::TEXTURERASTER, tex->raster);
	Sprite::RenderOneXLUSprite(screen.x, screen.y, screen.z, w, h,
	                           r, g, b, intensity, 20.0f/screen.z, 255);
}

void
RenderLights(void)
{
	DefinedState();
	rw::SetRenderState(rw::FOGENABLE, 0);
	rw::SetRenderState(rw::VERTEXALPHA, 1);
	rw::SetRenderState(rw::ZTESTENABLE, 1);
	rw::SetRenderState(rw::ZWRITEENABLE, 0);
	rw::SetRenderState(rw::SRCBLEND, rw::BLENDONE);
	rw::SetRenderState(rw::DESTBLEND, rw::BLENDONE);

	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		ObjectDef *obj = GetObjectDef(inst->m_objectId);
		if(obj == nil)
			continue;
		for(int i = 0; i < obj->m_numEffects; i++){
			Effect *e = GetEffect(obj->m_effectIndex+i);
			if(e->type == FX_LIGHT)
				RenderLightEffect(e, inst);
		}
	}

	rw::SetRenderStatePtr(rw::TEXTURERASTER, nil);
	rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
	rw::SetRenderState(rw::ZWRITEENABLE, 1);
}

static const rw::RGBA red = { 255, 0, 0, 255 };
static const rw::RGBA green = { 0, 255, 0, 255 };
static const rw::RGBA blue = { 0, 0, 255, 255 };
static const rw::RGBA cyan = { 0, 255, 255, 255 };
static const rw::RGBA magenta = { 255, 0, 255, 255 };
static const rw::RGBA yellow = { 255, 255, 0, 255 };
static const rw::RGBA white = { 255, 255, 255, 255 };

Effect *hoveredEffect, *guiHoveredEffect;
Effect *selectedEffect;

static void
RenderEffect(Effect *e, ObjectInst *inst)
{
	assert(e);
	assert(inst);

	rw::V3d pos, dir;
	rw::V3d::transformPoints(&pos, &e->pos, 1, &inst->m_matrix);
	if(TheCamera.distanceTo(pos) > gEffectDrawDist)
		return;

	Ray ray;
	ray.start = TheCamera.m_position;
	ray.dir = normalize(TheCamera.m_mouseDir);

	CSphere sphere;
	sphere.center = pos;
	sphere.radius = 1.0f;
	rw::RGBA c = e->col;
	if(SphereIntersect(sphere, ray) || e == guiHoveredEffect){
		hoveredEffect = e;
		c = cyan;
	}
	switch(e->type){
	case FX_LIGHT:
		sphere.radius = e->light.coronaSize;
		RenderWireSphere(&sphere, c, nil);
		break;

	case FX_PARTICLE:
		RenderLine(pos, add(pos, scale(e->prtcl.dir,e->prtcl.size)), c, c);
		RenderWireSphere(&sphere, c, nil);
		break;

	case FX_LOOKATPOINT:
		RenderSphereAsCross(&sphere, c, nil);
		rw::V3d::transformVectors(&dir, &e->look.dir, 1, &inst->m_matrix);
		RenderLine(pos, add(pos, scale(dir,5)), c, c);
		break;

	case FX_PEDQUEUE:
		RenderSphereAsWireBox(&sphere, c, nil);
		rw::V3d::transformVectors(&dir, &e->queue.queueDir, 1, &inst->m_matrix);
		RenderLine(pos, add(pos, scale(dir,5)), c, c);
		rw::V3d::transformVectors(&dir, &e->queue.useDir, 1, &inst->m_matrix);
		RenderLine(pos, add(pos, scale(dir,5)), c, c);
		rw::V3d::transformVectors(&dir, &e->queue.forwardDir, 1, &inst->m_matrix);
		RenderLine(pos, add(pos, scale(dir,5)), c, c);
		break;

	case FX_SUNGLARE:
		RenderSphereAsCross(&sphere, c, nil);
		break;

	case FX_INTERIOR: {
		CBox box;
		float hx = std::max(e->interior.width * 0.5f, 0.5f);
		float hy = std::max(e->interior.depth * 0.5f, 0.5f);
		float hz = std::max(e->interior.height * 0.5f, 0.5f);
		box.min = { pos.x - hx, pos.y - hy, pos.z - hz };
		box.max = { pos.x + hx, pos.y + hy, pos.z + hz };
		RenderWireBox(&box, c, nil);
		break;
	}

	case FX_ENTRYEXIT: {
		rw::V3d exitPos;
		rw::V3d::transformPoints(&exitPos, &e->entryExit.exitPos, 1, &inst->m_matrix);
		CSphere exitSphere;
		exitSphere.center = exitPos;
		exitSphere.radius = 0.75f;
		RenderSphereAsCross(&sphere, c, nil);
		RenderSphereAsCross(&exitSphere, c, nil);
		RenderLine(pos, exitPos, c, c);
		break;
	}

	case FX_ROADSIGN:
		sphere.radius = std::max(std::max(e->roadsign.width, e->roadsign.height) * 0.5f, 0.75f);
		RenderSphereAsWireBox(&sphere, c, nil);
		break;

	case FX_TRIGGERPOINT:
		RenderSphereAsCross(&sphere, c, nil);
		break;

	case FX_COVERPOINT:
		RenderSphereAsCross(&sphere, c, nil);
		dir = { e->coverPoint.dirX, e->coverPoint.dirY, 0.0f };
		rw::V3d::transformVectors(&dir, &dir, 1, &inst->m_matrix);
		RenderLine(pos, add(pos, scale(dir, 3.0f)), c, c);
		break;

	case FX_ESCALATOR: {
		rw::V3d start, bottom, top, end;
		rw::V3d::transformPoints(&start, &e->pos, 1, &inst->m_matrix);
		rw::V3d::transformPoints(&bottom, &e->escalator.bottom, 1, &inst->m_matrix);
		rw::V3d::transformPoints(&top, &e->escalator.top, 1, &inst->m_matrix);
		rw::V3d::transformPoints(&end, &e->escalator.end, 1, &inst->m_matrix);
		RenderSphereAsCross(&sphere, c, nil);
		RenderLine(start, bottom, c, c);
		RenderLine(bottom, top, c, c);
		RenderLine(top, end, c, c);
		break;
	}
	}

	// this kinda sucks but we want to see color
	if(selectedEffect == e){
		sphere.radius += 0.1f;
		RenderSphereAsWireBox(&sphere, white, nil);
	}
}

void
Render(void)
{
	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		ObjectDef *obj = GetObjectDef(inst->m_objectId);
		if(obj == nil)
			continue;
		for(int i = 0; i < obj->m_numEffects; i++)
			RenderEffect(GetEffect(obj->m_effectIndex+i), inst);
	}
}

}
