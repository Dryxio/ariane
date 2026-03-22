#include "euryopa.h"
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

float gEffectDrawDist = 150.0f;

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
		rw::V3d::transformVectors(&dir, &e->prtcl.dir, 1, &inst->m_matrix);
		RenderLine(pos, add(pos, scale(dir,5)), c, c);
		break;

	case FX_PEDQUEUE:
		RenderSphereAsWireBox(&sphere, c, nil);
		rw::V3d::transformVectors(&dir, &e->queue.queueDir, 1, &inst->m_matrix);
		RenderLine(pos, add(pos, scale(dir,5)), c, c);
		rw::V3d::transformVectors(&dir, &e->queue.useDir, 1, &inst->m_matrix);
		RenderLine(pos, add(pos, scale(dir,5)), c, c);
		break;

	case FX_SUNGLARE:
		RenderSphereAsCross(&sphere, c, nil);
		break;
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
