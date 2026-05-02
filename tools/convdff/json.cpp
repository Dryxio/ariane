#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cstdarg>

#include <rw.h>
#include <args.h>

#include <rwgta.h>

// shorter display, make examining the structure easier
//#define SKIPDATA

using namespace std;
using namespace rw;


static void
pr(int ind, FILE *f, char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	while(ind--)
		fprintf(f, "    ");
	vfprintf(f, fmt, ap);
	va_end(ap);
}

static void
dumptex(Texture *tex, FILE *f, int ind)
{
	pr(ind++, f, "{\n");
	pr(ind, f, "\"name\": \"%s\",\n", tex->name);
	pr(ind, f, "\"mask\": \"%s\"\n", tex->mask);
	pr(--ind, f, "}");
}

static void
dumpmaterial(Material *m, FILE *f, int ind)
{
	pr(ind++, f, "{\n");
	pr(ind, f, "\"color\": [ %d, %d, %d, %d ],\n",
		m->color.red, m->color.green, m->color.blue, m->color.alpha);
	pr(ind, f, "\"surfaceProperties\": [ %f, %f, %f ]",
		m->surfaceProps.ambient, m->surfaceProps.specular, m->surfaceProps.diffuse);
	if(m->texture){
		pr(0, f, ",\n");
		pr(ind, f, "\"texture\":\n");
		dumptex(m->texture, f, ind);
	}
	if(MatFX::getEffects(m)){
		MatFX *matfx = MatFX::get(m);
		pr(0, f, ",\n");
		pr(ind++, f, "\"matfx\": {\n");
		pr(ind, f, "\"type\": %d", matfx->type);
		if(matfx->type == MatFX::ENVMAP){
			pr(0, f, ",\n");
			pr(ind, f, "\"envCoefficient\": %f", matfx->getEnvCoefficient());
			if(matfx->getEnvTexture()){
				pr(0, f, ",\n");
				pr(ind, f, "\"envTex\":\n");
				dumptex(matfx->getEnvTexture(), f, ind);
			}
		}
		pr(0, f, "\n");
		pr(--ind, f, "}");
	}

	gta::EnvMat *env = gta::getEnvMat(m);
	if(env){
		pr(0, f, ",\n");
		pr(ind++, f, "\"envMap\": {\n");
		pr(ind, f, "\"scale\": [ %f, %f ],\n", env->getScaleX(), env->getScaleY());
		pr(ind, f, "\"transScale\": [ %f, %f ],\n", env->getTransScaleX(), env->getTransScaleY());
		pr(ind, f, "\"shininess\": %f\n", env->getShininess());
		pr(--ind, f, "}");
	}

	gta::SpecMat *spec = gta::getSpecMat(m);
	if(spec){
		pr(0, f, ",\n");
		pr(ind++, f, "\"specMap\": {\n");
		pr(ind, f, "\"specularity\": %f,\n", spec->specularity);
		assert(spec->texture);
		pr(ind, f, "\"texture\": \"%s\"\n", spec->texture->name);
		pr(--ind, f, "}");
	}

	pr(0, f, "\n");
	pr(--ind, f, "}");
}

static void
dumpgeo(Geometry *g, FILE *f, int ind)
{
	int i, j;

	pr(ind++, f, "{\n");
	if(g->flags & Geometry::NATIVE){
		pr(ind, f, "NATIVE\n");
		pr(--ind, f, "}");
		return;
	}

	if(g->colors){
		pr(ind++, f, "\"prelit\": [\n");
#ifndef SKIPDATA
		for(j = 0; j < g->numVertices; j++)
			pr(ind, f, "[ %d, %d, %d, %d ]%s\n",
				g->colors[j].red,
				g->colors[j].green,
				g->colors[j].blue,
				g->colors[j].alpha,
				j == g->numVertices-1 ? "" : ",");
#endif
		pr(--ind, f, "],\n");
	}

	if(g->numTexCoordSets){
		pr(ind++, f, "\"texCoords\": [\n");
		for(i = 0; i < g->numTexCoordSets; i++){
			pr(ind++, f, "[\n");
#ifndef SKIPDATA
			for(j = 0; j < g->numVertices; j++)
				pr(ind, f, "[ %f, %f ]%s\n",
					g->texCoords[i][j].u,
					g->texCoords[i][j].v,
					j == g->numVertices-1 ? "" : ",");
#endif
			pr(--ind, f, "]%s\n", i==g->numTexCoordSets-1 ? "" : ",");
		}
		pr(--ind, f, "],\n");
	}

	pr(ind++, f, "\"triangles\": [\n");
#ifndef SKIPDATA
	for(i = 0; i < g->numTriangles; i++)
		pr(ind, f, "[ %d, %d, %d, %d ]%s\n",
			g->triangles[i].v[0],
			g->triangles[i].v[1],
			g->triangles[i].v[2],
			g->triangles[i].matId,
			i == g->numTriangles-1 ? "" : ",");
#endif
	pr(--ind, f, "],\n");

	pr(ind++, f, "\"morphTargets\": [\n");
	for(i = 0; i < g->numMorphTargets; i++){
		pr(ind++, f, "{\n");
		MorphTarget *mt = &g->morphTargets[i];
		if(mt->vertices){
			pr(ind++, f, "\"vertices\": [\n");
#ifndef SKIPDATA
			for(j = 0; j < g->numVertices; j++)
				pr(ind, f, "[ %f, %f, %f ]%s\n",
					mt->vertices[j].x,
					mt->vertices[j].y,
					mt->vertices[j].z,
					j == g->numVertices-1 ? "" : ",");
#endif
			pr(--ind, f, "]%s\n", mt->normals ? "," : "");
		}
		if(mt->normals){
			pr(ind++, f, "\"normals\": [\n");
#ifndef SKIPDATA
			for(j = 0; j < g->numVertices; j++)
				pr(ind, f, "[ %f, %f, %f ]%s\n",
					mt->normals[j].x,
					mt->normals[j].y,
					mt->normals[j].z,
					j == g->numVertices-1 ? "" : ",");
#endif
			pr(--ind, f, "]\n");
		}
		pr(--ind, f, "}%s\n", i == g->numMorphTargets-1 ? "" : ",");
	}
	pr(--ind, f, "],\n");

	pr(ind++, f, "\"materials\": [\n");
	for(i = 0; i < g->matList.numMaterials; i++){
		dumpmaterial(g->matList.materials[i], f, ind);
		pr(0, f, "%s\n", i == g->matList.numMaterials-1 ? "" : ",");
	}
	pr(--ind, f, "],\n");

	MeshHeader *h = g->meshHeader;
	pr(ind, f, "\"meshtype\": %d,\n", h->flags);
	pr(ind++, f, "\"meshes\": [\n");
	Mesh *m = h->getMeshes();
	for(i = 0; i < h->numMeshes; i++){
		pr(ind++, f, "{\n");
		pr(ind, f, "\"matId\": %d,\n", findPointer(m->material, (void**)g->matList.materials, g->matList.numMaterials));
		pr(ind++, f, "\"indices\": [\n");
#ifndef SKIPDATA
		for(j = 0; j < m->numIndices; j++)
			pr(ind, f, "%d%s\n", m->indices[j],
				j == m->numIndices-1 ? "" : ",");
#endif
		pr(--ind, f, "]\n");
		pr(--ind, f, "}%s\n", i == h->numMeshes-1 ? "" : ",");
		m++;
	}
	pr(--ind, f, "]");

	gta::Effect2d* twodfx = gta::get2dEffects(g);
	if(twodfx != nil) {
		pr(0, f, ",\n");
		pr(ind++, f, "\"2dfx\": [\n");
		for (i = 0; i < gta::getNum2dEffects(g); i++) {
			gta::Effect2d* effect2d = &twodfx[i];

			pr(ind++, f, "{\n");
			pr(ind, f, "\"type\": %d,\n", effect2d->type);
			pr(ind, f, "\"posn\": [ %f, %f, %f ],\n",
				effect2d->posn.x,
				effect2d->posn.y,
				effect2d->posn.z);

			switch (effect2d->type) {
			case gta::ET_LIGHT: {
				gta::LightAttr& l = effect2d->attr.l;
				pr(ind++, f, "\"light\": {\n");
				pr(ind, f, "\"color\": [%d, %d, %d, %d],\n", l.col.red, l.col.green, l.col.blue, l.col.alpha);
				pr(ind, f, "\"lodDist\": %.4f,\n", l.lodDist);
				pr(ind, f, "\"size\": %.4f,\n", l.size);
				pr(ind, f, "\"coronaSize\": %.4f,\n", l.coronaSize);
				pr(ind, f, "\"shadowSize\": %.4f,\n", l.shadowSize);
				pr(ind, f, "\"flags\": %u,\n", l.flags);
				pr(ind, f, "\"flashiness\": %d,\n", l.flashiness);
				pr(ind, f, "\"reflectionType\": %d,\n", l.reflectionType);
				pr(ind, f, "\"lensFlareType\": %d,\n", l.lensFlareType);
				pr(ind, f, "\"shadowAlpha\": %d,\n", l.shadowAlpha);
				pr(ind, f, "\"shadowDepth\": %d,\n", l.shadowDepth);
				pr(ind, f, "\"lightDir\": [%d, %d, %d],\n",
					l.lightDirX, l.lightDirY, l.lightDirZ);
				pr(ind, f, "\"coronaTex\": \"%s\",\n", l.coronaTex);
				pr(ind, f, "\"shadowTex\": \"%s\"\n", l.shadowTex);
				pr(--ind, f, "}\n");
				break;
			}

			case gta::ET_PARTICLE: {
				gta::ParticleAttr& p = effect2d->attr.p;
				pr(ind, f, "\"particle\": {\n");
				pr(ind + 1, f, "\"name\": \"%s\"\n", p.name);
				pr(ind, f, "}\n");
				break;
			}

			case gta::ET_PEDQUEUE: {
				gta::PedQueueAttr& q = effect2d->attr.q;
				pr(ind++, f, "\"pedqueue\": {\n");
				pr(ind, f, "\"queueDir\": [%.6f, %.6f, %.6f],\n",
					q.queueDir.x, q.queueDir.y, q.queueDir.z);
				pr(ind, f, "\"useDir\": [%.6f, %.6f, %.6f],\n",
					q.useDir.x, q.useDir.y, q.useDir.z);
				pr(ind, f, "\"forwardDir\": [%.6f, %.6f, %.6f],\n",
					q.forwardDir.x, q.forwardDir.y, q.forwardDir.z);
				pr(ind, f, "\"type\": %u,\n", q.type);
				pr(ind, f, "\"interest\": %u,\n", q.interest);
				pr(ind, f, "\"lookAt\": %u,\n", q.lookAt);
				pr(ind, f, "\"flags\": %u,\n", q.flags);
				pr(ind, f, "\"scriptName\": \"%s\"\n", q.scriptName);
				pr(--ind, f, "}\n");
				break;
			}

			case gta::ET_SUNGLARE:
				pr(ind, f, "\"sunglare\": {}\n");
				break;

			case gta::ET_INTERIOR: {
				gta::InteriorAttr& ii = effect2d->attr.i;
				pr(ind++, f, "\"interior\": {\n");
				pr(ind, f, "\"type\": %u,\n", ii.type);
				pr(ind, f, "\"group\": %d,\n", ii.group);
				pr(ind, f, "\"dimensions\": [%d, %d, %d],\n", ii.width, ii.depth, ii.height);
				pr(ind, f, "\"door\": %d,\n", ii.door);
				pr(ind, f, "\"leftDoor\": [%d, %d],\n", ii.lDoorStart, ii.lDoorEnd);
				pr(ind, f, "\"rightDoor\": [%d, %d],\n", ii.rDoorStart, ii.rDoorEnd);
				pr(ind, f, "\"topDoor\": [%d, %d],\n", ii.tDoorStart, ii.tDoorEnd);
				pr(ind, f, "\"leftWindow\": [%d, %d],\n", ii.lWindowStart, ii.lWindowEnd);
				pr(ind, f, "\"rightWindow\": [%d, %d],\n", ii.rWindowStart, ii.rWindowEnd);
				pr(ind, f, "\"topWindow\": [%d, %d],\n", ii.tWindowStart, ii.tWindowEnd);
				pr(ind, f, "\"noGoLeft\": [%d, %d, %d],\n",
					ii.noGoLeft[0], ii.noGoLeft[1], ii.noGoLeft[2]);
				pr(ind, f, "\"noGoBottom\": [%d, %d, %d],\n",
					ii.noGoBottom[0], ii.noGoBottom[1], ii.noGoBottom[2]);
				pr(ind, f, "\"noGoWidth\": [%d, %d, %d],\n",
					ii.noGoWidth[0], ii.noGoWidth[1], ii.noGoWidth[2]);
				pr(ind, f, "\"noGoDepth\": [%d, %d, %d],\n",
					ii.noGoDepth[0], ii.noGoDepth[1], ii.noGoDepth[2]);
				pr(ind, f, "\"seed\": %u,\n", ii.seed);
				pr(ind, f, "\"status\": %u,\n", ii.status);
				pr(ind, f, "\"rot\": %.6f\n", ii.rot);
				pr(--ind, f, "}\n");
				break;
			}

			case gta::ET_ENTRYEXIT: {
				gta::EntryExitAttr& e = effect2d->attr.e;
				pr(ind++, f, "\"entryexit\": {\n");
				pr(ind, f, "\"prot\": %.6f,\n", e.prot);
				pr(ind, f, "\"worldPos\": [%.6f, %.6f],\n", e.wx, e.wy);
				pr(ind, f, "\"spawn\": [%.6f, %.6f, %.6f],\n",
					e.spawn.x, e.spawn.y, e.spawn.z);
				pr(ind, f, "\"spawnrot\": %.6f,\n", e.spawnrot);
				pr(ind, f, "\"areacode\": %d,\n", e.areacode);
				pr(ind, f, "\"flags\": %u,\n", e.flags);
				pr(ind, f, "\"extracol\": %u,\n", e.extracol);
				pr(ind, f, "\"title\": \"%s\",\n", e.title);
				pr(ind, f, "\"openTime\": %u,\n", e.openTime);
				pr(ind, f, "\"shutTime\": %u,\n", e.shutTime);
				pr(ind, f, "\"extraFlags\": %u\n", e.extraFlags);
				pr(--ind, f, "}\n");
				break;
			}

			case gta::ET_ROADSIGN: {
				gta::RoadsignAttr& rs = effect2d->attr.rs;
				pr(ind++, f, "\"roadsign\": {\n");
				pr(ind, f, "\"size\": [%.4f, %.4f],\n", rs.width, rs.height);
				pr(ind, f, "\"rotation\": [%.6f, %.6f, %.6f],\n",
					rs.rotX, rs.rotY, rs.rotZ);
				pr(ind, f, "\"flags\": %u,\n", rs.flags);
				pr(ind++, f, "\"text\": [\n");
				for (int line = 0; line < 4; ++line) {
					pr(ind, f, "\"%.16s\"%s\n",
						rs.text[line],
						(line < 3) ? "," : "");
				}
				pr(--ind, f, "]\n");
				pr(--ind, f, "}\n");
				break;
			}

			case gta::ET_TRIGGERPOINT: {
				gta::TriggerPointAttr& t = effect2d->attr.t;
				pr(ind, f, "\"triggerpoint\": {\n");
				pr(ind + 1, f, "\"index\": %d\n", t.index);
				pr(ind, f, "}\n");
				break;
			}

			case gta::ET_COVERPOINT: {
				gta::CoverPointAttr& c = effect2d->attr.c;
				pr(ind, f, "\"coverpoint\": {\n");
				pr(ind + 1, f, "\"dirOfCover\": [%.6f, %.6f],\n", c.dirOfCoverX, c.dirOfCoverY);
				pr(ind + 1, f, "\"usage\": %d\n", c.usage);
				pr(ind, f, "}\n");
				break;
			}

			case gta::ET_ESCALATOR: {
				gta::EscalatorAttr& es = effect2d->attr.es;
				pr(ind++, f, "\"escalator\": {\n");
				pr(ind++, f, "\"coords\": [\n");
				for (int k = 0; k < 3; ++k) {
					pr(ind, f, "[%.6f, %.6f, %.6f]%s\n",
						es.coords[k].x, es.coords[k].y, es.coords[k].z,
						(k < 2) ? "," : "");
				}
				pr(--ind, f, "],\n");
				pr(ind, f, "\"goingUp\": %s\n", es.goingUp ? "true" : "false");
				pr(--ind, f, "}\n");
				break;
			}
			}
			pr(--ind, f, "}%s\n", i == gta::getNum2dEffects(g) - 1 ? "" : ",");
		}
		pr(--ind, f, "]\n");
	} else {
		pr(ind, f, "\n");
	}

	pr(--ind, f, "}");
}

static void
dumpatomic(Atomic *a, FrameList_ *frmlist, FILE *f, int ind)
{
	pr(ind++, f, "{\n");
	pr(ind, f, "\"frame\": %d,\n", findPointer(a->getFrame(), (void**)frmlist->frames, frmlist->numFrames));
	pr(ind, f, "\"geometry\":\n");
	dumpgeo(a->geometry, f, ind);
	pr(0, f, ",\n");
	pr(ind, f, "\"matfx\": %d\n", MatFX::getEffects(a));
	pr(--ind, f, "}");
}

static void
dumpframe(Frame *frm, FrameList_ *frmlist, FILE *f, int ind)
{
	pr(ind++, f, "{\n");
	pr(ind, f, "\"parent\": %d,\n", findPointer(frm->getParent(), (void**)frmlist->frames, frmlist->numFrames));
	pr(ind, f, "\"matrix\": [ %f, %f, %f,  %f, %f, %f,  %f, %f, %f,  %f, %f, %f ],\n",
		frm->matrix.right.x, frm->matrix.right.y, frm->matrix.right.z,
		frm->matrix.up.x, frm->matrix.up.y, frm->matrix.up.z,
		frm->matrix.at.x, frm->matrix.at.y, frm->matrix.at.z,
		frm->matrix.pos.x, frm->matrix.pos.y, frm->matrix.pos.z);
	// TODO? hanim
	pr(ind, f, "\"name\": \"%s\"\n", gta::getNodeName(frm));
	pr(--ind, f, "}");
}

static void
dumpframes(FrameList_ *frmlist, FILE *f, int ind)
{
	int i;

	pr(ind++, f, "\"frames\": [\n");
	int first = 1;
	for(i = 0; i < frmlist->numFrames; i++){
		if(!first)
			pr(0, f, ",\n");
		first = 0;
		dumpframe(frmlist->frames[i], frmlist, f, ind);
	}
	pr(0, f, "\n");
	pr(--ind, f, "],\n");
}

static void
dumpclump(Clump *c, FILE *f, int ind)
{
	pr(ind++, f, "{\n");

	FrameList_ frmlst;
	frmlst.numFrames = c->getFrame()->count();
	frmlst.frames = (Frame**)rwMalloc(frmlst.numFrames*sizeof(Frame*), MEMDUR_FUNCTION | ID_CLUMP);
	makeFrameList(c->getFrame(), frmlst.frames);
	dumpframes(&frmlst, f, ind);

	pr(ind++, f, "\"atomics\": [\n");
	int first = 1;
	FORLIST(lnk, c->atomics){
		Atomic *a = Atomic::fromClump(lnk);
		if(!first)
			pr(0, f, ",\n");
		first = 0;
		dumpatomic(a, &frmlst, f, ind);
	}
	pr(0, f, "\n");
	pr(--ind, f, "]\n");

	rwFree(frmlst.frames);

	pr(--ind, f, "}\n");
}

void
dumpjson(Clump *c, const char *file)
{
	FILE *f;
	f = fopen(file, "w");
	if(f == nil)
		return;
	dumpclump(c, f, 0);
	fclose(f);
}
