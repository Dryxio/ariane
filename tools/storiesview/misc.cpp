#include <Windows.h>
#include "storiesview.h"

FILE *logfile;

BuildingExt *GetBuildingExt(int id);

void
dumpEntityBS(FILE *f, int pid, CEntity *e)
{
	rw::Matrix mat;
	CBaseModelInfo *mi = CModelInfo::Get(e->modelIndex);
	if(mi == nil || mi->colModel == nil)
		return;
	if(mi->type != MODELINFO_SIMPLE && mi->type != MODELINFO_TIME)
		return;
	CSimpleModelInfo *smi = (CSimpleModelInfo*)mi;
	CTimeModelInfo *tmi = nil;
	if(mi->type == MODELINFO_TIME)
		tmi = (CTimeModelInfo*)mi;

	mat = *(rw::Matrix*)&e->placeable.matrix.matrix;
	mat.optimize();
	rw::V3d center;
	rw::V3d::transformPoints(&center, (rw::V3d*)&mi->colModel->boundingSphere.center, 1, &mat);

	fprintf(f, "%x  %d %d %s  %f %f %f %f", pid, e->modelIndex, !!(smi->flags & 0x10), mi->name, center.x, center.y, center.z, mi->colModel->boundingSphere.radius);
	if(tmi)
		fprintf(f, " %d %d", tmi->timeOn, tmi->timeOff);
	fprintf(f, "\n");
}

void
dumpInstBS(int level, sGeomInstance *inst)
{
	float x = halfFloatToFloat(inst->bound[0]);
	float y = halfFloatToFloat(inst->bound[1]);
	float z = halfFloatToFloat(inst->bound[2]);
	float r = halfFloatToFloat(inst->bound[3]);
	BuildingExt *be = GetBuildingExt(inst->GetId());

//	fprintf(logfile, "%X %d %d  %f %f %f %f", inst->GetId() | level<<16, inst->IsFlagged(), be->interior, x, y, z, r);
	//if(be->isTimed)
	//	fprintf(logfile, " %d %d", be->timeOn, be->timeOff);
	fprintf(logfile, "\n");
}

void
openLogFile(char *path)
{
	logfile = fopen(path, "w");
	assert(logfile);
}

void
closeLogFile(void)
{
	fclose(logfile);
}

void
dumpIPLBoundingSpheres(void)
{
	CEntity *e;
	int i, n;

	n = pBuildingPool->GetSize();
	for(i = 0; i < n; i++){
		e = pBuildingPool->GetSlot(i);
		if(e == nil)
			continue;
		dumpEntityBS(logfile, i, e);
	}

	n = pTreadablePool->GetSize();
	for(i = 0; i < n; i++){
		e = pTreadablePool->GetSlot(i);
		if(e == nil)
			continue;
		dumpEntityBS(logfile, 1<<16 | i, e);
	}
}


CEntity*
GetEntityById(int id)
{
	switch(id>>16){
	case 0: return pBuildingPool->GetSlot(id & 0xFFFF);
	case 1: return pTreadablePool->GetSlot(id & 0xFFFF);
	case 2: return pDummyPool->GetSlot(id & 0xFFFF);
	default: return nil;
	}
}

#ifdef VCS

struct Link
{
	int worldId;
	int iplId;
	int modelId;
} inactiveLinks[0x8000];
int numInactiveLinks;

#ifdef EDIT_LINKS
char *linkpath = "C:\\vcs_links.txt";
//char *linkpath = "C:\\Users\\aap\\Desktop\\stories\\vcs_map\\vcs_links.txt";
#else
Link filelinks[] = {
#include "vcs_links.inc"
	{ -1, -1, -1 }
};
#endif


void
LinkInstances(void)
{
	int i, j, n;
	CEntity *e;
	BuildingExt *be;
	EntityExt *ee;

	// reuse vtable to store world building ids
	n = pBuildingPool->GetSize();
	for(i = 0; i < n; i++){
		e = pBuildingPool->GetSlot(i);
		if(e == nil)
			continue;
		ee = rwNewT(EntityExt, 1, 0);
		memset(ee, 0, sizeof(*ee));
		ee->entity = e;
		e->vtable = ee;
	}
	n = pTreadablePool->GetSize();
	for(i = 0; i < n; i++){
		e = pTreadablePool->GetSlot(i);
		if(e == nil)
			continue;
		ee = rwNewT(EntityExt, 1, 0);
		memset(ee, 0, sizeof(*ee));
		ee->entity = e;
		e->vtable = ee;
	}
	// we don't expect to use this for much
	n = pDummyPool->GetSize();
	for(i = 0; i < n; i++){
		e = pDummyPool->GetSlot(i);
		if(e == nil)
			continue;
		ee = rwNewT(EntityExt, 1, 0);
		memset(ee, 0, sizeof(*ee));
		ee->entity = e;
		e->vtable = ee;
	}

	int levid = gLevel->levelid << 16;
	int worldId, iplId, modelId;

#ifdef EDIT_LINKS
	FILE *f = fopen(linkpath, "r");
	if(f == nil)
		return;//abort();

	while(fscanf(f, "%x %x %d", &worldId, &iplId, &modelId) == 3){
//	while(fscanf(f, "%x %x", &worldId, &iplId) == 2){
#else
	for(Link *l = filelinks;
	    worldId = l->worldId, iplId = l->iplId, modelId = l->modelId, worldId >= 0;
	    l++){
#endif
		if((worldId & ~0xFFFF) == levid){
			be = GetBuildingExt(worldId&0x7FFF);
			be->SetEntity(iplId);
			if(modelId >= 0)
				be->modelId = modelId;
		}else{
			inactiveLinks[numInactiveLinks].worldId = worldId;
			inactiveLinks[numInactiveLinks].iplId = iplId;
			inactiveLinks[numInactiveLinks].modelId = modelId;
			numInactiveLinks++;
		}
		if(modelId >= 0)	// pretty much guaranteed
			GetModelInfoExt(modelId)->buildingId = worldId;
	}

#ifdef EDIT_LINKS
	fclose(f);
#endif
// no longer useful
/*
	n = pBuildingPool->GetSize();
	for(i = 0; i < n; i++){
		e = pBuildingPool->GetSlot(i);
		if(e == nil)
			continue;
		ee = (EntityExt*)e->vtable;
		if(ee->n > 1){
			for(j = 0; j < ee->n; j++)
				ee->insts[j]->iplId = -1;
			ee->n = 0;
		}
	}
	n = pTreadablePool->GetSize();
	for(i = 0; i < n; i++){
		e = pTreadablePool->GetSlot(i);
		if(e == nil)
			continue;
		ee = (EntityExt*)e->vtable;
		if(ee->n > 1){
			for(j = 0; j < ee->n; j++)
				ee->insts[j]->iplId = -1;
			ee->n = 0;
		}
	}
*/
}

void
WriteLinks(void)
{
#ifdef EDIT_LINKS
	int i, j, n;
	CEntity *e;
	EntityExt *ee;
	int iplId;

	FILE *f = fopen(linkpath, "w");
	if(f == nil)
		abort();

	int levid = gLevel->levelid << 16;
	n = pBuildingPool->GetSize();
	for(i = 0; i < n; i++){
		e = pBuildingPool->GetSlot(i);
		if(e == nil)
			continue;
		ee = (EntityExt*)e->vtable;
		iplId = i;
		for(j = 0; j < ee->n; j++)
			fprintf(f, "%x %x %d\n", ee->insts[j]->id | levid, iplId, ee->insts[j]->modelId);
	}
	n = pTreadablePool->GetSize();
	for(i = 0; i < n; i++){
		e = pTreadablePool->GetSlot(i);
		if(e == nil)
			continue;
		ee = (EntityExt*)e->vtable;
		iplId = i | 0x10000;
		for(j = 0; j < ee->n; j++)
			fprintf(f, "%x %x %d\n", ee->insts[j]->id | levid, iplId, ee->insts[j]->modelId);
	}
	for(i = 0; i < numInactiveLinks; i++)
		fprintf(f, "%x %x %d\n", inactiveLinks[i].worldId, inactiveLinks[i].iplId, inactiveLinks[i].modelId);

	fclose(f);
#endif
}


//throw away function
void
dumpModelInfoLinks(void)
{
	int i;
	for(i = 0; i < CModelInfo::msNumModelInfos; i++){
		CBaseModelInfo *mi = CModelInfo::Get(i);
		ModelInfoExt *mie = GetModelInfoExt(i);
		if(mi == nil) continue;
		CStreamingInfo *si = &pStreaming->ms_aInfoForModel[i];
		char *txdname = CTexListStore::GetSlot(mi->txdSlot)->name;
		if(si->cdSize > 0)
			continue;
		if(mie->buildingId >= 0)
			continue;
		if(mie->inst != nil)
			continue;
		if(mie->isSwap)
			continue;
		if(strcmp(txdname, "knackers") != 0)
			continue;
//		if(strncmp(mi->name, "eb_", 3) == 0)	// empire dummies
//			continue;

		fprintf(logfile, "%d %s %s\n", i, mi->name, txdname);
	}
}

#else

void
LinkInstances(void)
{
	int i, n;
	CEntity *e;
	EntityExt *bl;

	// reuse vtable to store world building ids
	n = pBuildingPool->GetSize();
	for(i = 0; i < n; i++){
		e = pBuildingPool->GetSlot(i);
		if(e == nil)
			continue;
		bl = rwNewT(EntityExt, 1, 0);
		memset(bl, 0, sizeof(*bl));
		bl->entity = e;
		e->vtable = bl;
	}
	n = pTreadablePool->GetSize();
	for(i = 0; i < n; i++){
		e = pTreadablePool->GetSlot(i);
		if(e == nil)
			continue;
		bl = rwNewT(EntityExt, 1, 0);
		memset(bl, 0, sizeof(*bl));
		bl->entity = e;
		e->vtable = bl;
	}
}

#endif

static void
walkResources(void)
{
//	int i;
//	// TODO: this will have to be rewritten
//	drawFlagged = 2;
//	Renderer::reset();
//	for(i = 0; i < gLevel->numSectors; i++)
//		RenderSector(&gLevel->sectors[i]);
}

void
transformGeo(rw::Geometry *g, rw::Matrix *mat)
{
	rw::V3d::transformPoints(g->morphTargets[0].vertices, g->morphTargets[0].vertices, g->numVertices, mat);
}

rw::Geometry*
makeGeo(rw::Geometry *ga, rw::Geometry *gb)
{
	using namespace rw;

	Geometry *geo;
	int i, j;
	int voff, toff, moff;

	voff = ga->numVertices;
	toff = ga->numTriangles;
	moff = ga->matList.numMaterials;
	geo = Geometry::create(ga->numVertices + gb->numVertices,
		ga->numTriangles + gb->numTriangles,
		ga->flags);

	memcpy(geo->morphTargets[0].vertices, ga->morphTargets[0].vertices, ga->numVertices*sizeof(V3d));
	memcpy(geo->morphTargets[0].vertices+voff, gb->morphTargets[0].vertices, gb->numVertices*sizeof(V3d));
	assert((gb->flags & Geometry::NORMALS) == 0);
	if(ga->numTexCoordSets){
		memcpy(geo->texCoords[0], ga->texCoords[0], ga->numVertices*sizeof(TexCoords));
		memcpy(geo->texCoords[0]+voff, gb->texCoords[0], gb->numVertices*sizeof(TexCoords));
	}
	if(ga->flags & Geometry::PRELIT){
		memcpy(geo->colors, ga->colors, ga->numVertices*sizeof(RGBA));
		memcpy(geo->colors+voff, gb->colors, gb->numVertices*sizeof(RGBA));
	}
	memcpy(geo->triangles, ga->triangles, ga->numTriangles*sizeof(Triangle));
	memcpy(geo->triangles+toff, gb->triangles, gb->numTriangles*sizeof(Triangle));
	for(i = 0; i < gb->numTriangles; i++){
		geo->triangles[i+toff].v[0] += voff;
		geo->triangles[i+toff].v[1] += voff;
		geo->triangles[i+toff].v[2] += voff;
		geo->triangles[i+toff].matId += moff;
	}
	for(i = 0; i < ga->matList.numMaterials; i++)
		geo->matList.appendMaterial(ga->matList.materials[i]);
	for(i = 0; i < gb->matList.numMaterials; i++)
		geo->matList.appendMaterial(gb->matList.materials[i]);

	MeshHeader *ha, *hb, *h;
	Mesh *ma, *mb, *m;
	uint16 *indices;
	ha = ga->meshHeader;
	hb = gb->meshHeader;
	geo->allocateMeshes(ha->numMeshes + hb->numMeshes,
		ha->totalIndices + hb->totalIndices, 0);
	h = geo->meshHeader;
	h->flags = ha->flags;
	assert(ha->flags == hb->flags);
	ma = ha->getMeshes();
	mb = hb->getMeshes();
	m = h->getMeshes();
	indices = m->indices;
	for(i = 0; i < ha->numMeshes; i++){
		m->material = ma->material;
		m->numIndices = ma->numIndices;
		m->indices = indices;
		for(j = 0; j < m->numIndices; j++)
			m->indices[j] = ma->indices[j];
		indices += m->numIndices;
		m++;
		ma++;
	}
	for(i = 0; i < hb->numMeshes; i++){
		m->material = mb->material;
		m->numIndices = mb->numIndices;
		m->indices = indices;
		for(j = 0; j < m->numIndices; j++)
			m->indices[j] = mb->indices[j] + voff;
		indices += m->numIndices;
		m++;
		mb++;
	}

	geo->calculateBoundingSphere();

	return geo;
}

rw::Geometry*
cloneGeo(rw::Geometry *geo)
{
	using namespace rw;
	Geometry *g;
	g = Geometry::create(geo->numVertices, geo->numTriangles, geo->flags);
	return g;
}

void
dumpClump(rw::Clump *c)
{
	static char filename[256];
	rw::StreamFile out;
	char *name;

	name = gta::getNodeName(c->getFrame());
	sprintf(filename, "C:/vcstmp/mdl/%s.dff", name);
	printf("%s\t%s\n", filename, name);

	if(!out.open(filename, "wb")){
		fprintf(stderr, "Error: couldn't open %s\n", filename);
		return;
	}
	c->streamWrite(&out);
	out.close();
}

void
dumpCol(CBaseModelInfo *mi)
{
	static char filename[256];
	char colname[24];
	uint32 ident, size;
	uint8 *buf;
	rw::StreamFile stream;

	ColEntry *c = &pColPool->items[mi->colModel->colStoreId];

if(c->name[0]){
	sprintf(filename, "C:/vcstmp/col/%s", c->name);
	CreateDirectoryA(filename, nil);
	sprintf(filename, "C:/vcstmp/col/%s/%s.col", c->name, mi->name);
}else{
	sprintf(filename, "C:/vcstmp/col/col%d", mi->colModel->colStoreId);
	CreateDirectoryA(filename, nil);
	sprintf(filename, "C:/vcstmp/col/col%d/%s.col", mi->colModel->colStoreId, mi->name);
}
//	sprintf(filename, "C:/vcstmp/col/%s.col", mi->name);

	stream.open(filename, "wb");
	ident = 0x4C4C4F43;
	size = WriteCollisionFile(mi->colModel, &buf)+24;
	stream.write8(&ident, 4);
	stream.write8(&size, 4);
	memset(colname, 0, 24);
	strncpy(colname, mi->name, 24);
	stream.write8(colname, 24);
	stream.write8(buf, size-24);
	rwFree(buf);
	stream.close();
}

void
DumpCollisions(void)
{
	int i;
	CBaseModelInfo *mi;

	for(i = 0; i < CModelInfo::msNumModelInfos; i++){
		mi = CModelInfo::Get(i);
		if(mi == nil) continue;
		if(mi->colModel)
			dumpCol(mi);
	}
}

void
DumpModels(void)
{
	int i;
	CBaseModelInfo *mi;
	BuildingExt *be;
	BuildingExt::Model *mdl;
	ModelInfoExt *mie;
	CEntity *e;
//	EntityExt *ee;
	Resource *res;
	rw::Geometry *geoa, *geob;
	rw::Frame *f;
	rw::Atomic *a;
	rw::Clump *c;

//	walkResources();

	for(i = 0; i < CModelInfo::msNumModelInfos; i++){
		mi = CModelInfo::Get(i);
		mie = GetModelInfoExt(i);
		if(mi == nil) continue;

		if(mie->buildingId < 0)
			continue;		// no building we can convert
		if((mie->buildingId>>16) != gLevel->levelid)
			continue;		// model isn't in this level

		be = GetBuildingExt(mie->buildingId & 0x7FFF);
		assert(be->modelId == i);
		e = GetEntityById(be->iplId);
		assert(e);

//		e = mie->inst;
//		if(e == nil) continue;
//
//		ee = (EntityExt*)e->vtable;
//		if(ee->n != 1) continue;
//
//		be = ee->insts[0];
		/* never more than 2 */
		geoa = nil; geob = nil;
		for(mdl = be->resources; mdl; mdl = mdl->next){
			res = GetResource(mdl->resId);
			assert(res->dmaChain);
			if(geoa == nil)
				geoa = (rw::Geometry*)res->dmaChain;
			else if(geob == nil)
				geob = (rw::Geometry*)res->dmaChain;
			else
				assert(0);
		}

		/* get matrix to transform vertices */
		rw::Matrix m, inv;
		m = *(rw::Matrix*)&e->placeable.matrix.matrix;
		m.optimize();
		rw::Matrix::invert(&inv, &m);
		be->matrix.optimize();
		m = be->matrix;
		m.transform(&inv, rw::COMBINEPOSTCONCAT);
		//rw::Matrix::mult(&m, &be->matrix, &inv);

		bool isGeoTransparent(rw::Geometry *geo);
		if(geoa && geob){
			// want to have transparent objects last
			if(isGeoTransparent(geoa))
				geoa = makeGeo(geob, geoa);
			else
				geoa = makeGeo(geoa, geob);
		}
		transformGeo(geoa, &m);
		f = rw::Frame::create();
		a = rw::Atomic::create();
		c = rw::Clump::create();
		strncpy(gta::getNodeName(f), mi->name, 23);
		a->setFrame(f);
		a->setGeometry(geoa, 0);
		c->setFrame(f);
		c->addAtomic(a);

		dumpClump(c);
	}

	exit(0);
}




typedef struct NameDef NameDef;
struct NameDef
{
	int type;	// needed?
	char *gen;
	char *pretty;
	NameDef *next;
};

static NameDef *defs;

void
ReadDefFile(char *filename)
{
	static char *space = " \t\n\r";
	static char line[256];
	int type;
	char *a, *b;
	FILE *f;
	NameDef *def;

	f = fopen(filename, "r");
	if(f == nil)
		return;
	while(fgets(line, 256, f)){
		a = strtok(line, space);
		b = strtok(nil, space);
		if(a == nil || b == nil) continue;

		if(strstr(a, "tex_") == a)
			type = TexName;
		else
			continue;

		def = rwNewT(NameDef, 1, 0);
		def->type = type;
		def->gen = strdup(a);
		if(b[0] == '%') b++;
		def->pretty = strdup(b);
		for(b = def->pretty; *b; b++)
			if(*b == '^')
				*b = ' ';

		def->next = defs;
		defs = def;
	}
	fclose(f);
}

char*
FindNameDef(int type, char *name)
{
	NameDef *def;
	for(def = defs; def; def = def->next)
		if(def->type == type &&
		   strcmp(def->gen, name) == 0)
			return def->pretty;
	return name;
}
