#include "storiesview.h"

// sectors in III: -2000 - 2000: 100 sectors of size 40 in both directions
// sectors in VC: x: -2400 - 1600: 80 sectors of size 50
//                y: -2000 - 2000: 80 sectors of size 50

#ifdef LCS
const char *lvznames[] = {
	nil,
	"MODELS/INDUST.LVZ",
	"MODELS/COMMER.LVZ",
	"MODELS/SUBURB.LVZ",
	"MODELS/UNDERG.LVZ",
};
const char *imgnames[] = {
	nil,
	"MODELS/INDUST.IMG",
	"MODELS/COMMER.IMG",
	"MODELS/SUBURB.IMG",
	"MODELS/UNDERG.IMG",
};
#define XINC 100.0f
#define YINC 86.6f
#define XSTART (-2000.0f)
#define YSTART (-2000.0f)
#else
const char *lvznames[] = {
	nil,
	"BEACH.LVZ",
	"MAINLA.LVZ",
	"MALL.LVZ",
};
const char *imgnames[] = {
	nil,
	"BEACH.IMG",
	"MAINLA.IMG",
	"MALL.IMG",
};
#define XINC 125.0f
#define YINC 108.25f
#define XSTART (-2400.0f)
#define YSTART (-2000.0f)
#endif

LevelExt *gLevel;
rw::V3d worldSectorPositions[NUMSECTORSX][NUMSECTORSY];
SectorExt *worldSectors[NUMSECTORSX][NUMSECTORSY];

void
GetSectorForPosition(float x, float y, int *ix, int *iy)
{
	*iy = (y - YSTART)/YINC;
	*ix = (x - XSTART)/XINC + (*ix & 1)*0.5f;
}

Resource*
GetResource(int id)
{
	return &gLevel->chunk->resourceTable[id];
}

void
LoadLevel(eLevel lev)
{
	int i, j;
	int n;
	const char *imgname, *lvzname;
	imgname = imgnames[lev];
	lvzname = lvznames[lev];

	Zfile *zfile;
	zfile = zopen(lvzname, "rb");
	if(zfile == nil){
		sk::globals.quit = 1;
		return;
	}
	sChunkHeader header;
	zread(zfile, &header, sizeof(sChunkHeader));
	uint8 *data = (uint8*)malloc(header.fileSize-sizeof(sChunkHeader));
	zread(zfile, data, header.fileSize-sizeof(sChunkHeader));
	zclose(zfile);
	cRelocatableChunk(header.ident, header.shrink).Fixup(header, data);
	gLevel = new LevelExt;
	memset(gLevel, 0, sizeof(LevelExt));
	gLevel->chunk = (sLevelChunk*)data;
	gLevel->levelid = lev;
	gLevel->imgfile = fopen(imgname, "rb");
	if(gLevel->imgfile == nil){
		sk::globals.quit = 1;
		return;
	}
	sChunkHeader *pheader = gLevel->chunk->sectorRows[0].header;
	gLevel->numSectors = 0;
	gLevel->numWorldSectors = 0;
	for(i = 0; pheader->ident == WRLD_IDENT ; i++){
		gLevel->numSectors++;
		if(pheader < gLevel->chunk->sectorEnd.header){
			gLevel->numWorldSectors++;
		}
		pheader++;
	}

//	printf("%d %d\n", gLevel->numStreamingSectors, gLevel->numSectors);

	// Allocate extension data
	gLevel->sectors = (SectorExt*)malloc(gLevel->numSectors*sizeof(SectorExt));
	gLevel->res = (ResourceExt*)malloc(gLevel->chunk->numResources*sizeof(ResourceExt));
	memset(gLevel->sectors, 0, gLevel->numSectors*sizeof(SectorExt));
	memset(gLevel->res, 0, gLevel->chunk->numResources*sizeof(ResourceExt));

	SectorExt *se = gLevel->sectors;

	// Set up world sectors
	memset(worldSectors, 0, sizeof(worldSectors));
	for(i = 0; i < NUMSECTORSY; i++){
		// Get origin
		for(j = 0; j < NUMSECTORSX; j++){
			worldSectorPositions[j][i].x = XSTART + XINC/2 +
				XINC*j - (i&1)*XINC/2;
			worldSectorPositions[j][i].y = YSTART + YINC/2 +
				YINC*i;
			worldSectorPositions[j][i].z = 0.0f;
		}
		// Store sector x and y coords
		n = gLevel->chunk->sectorRows[i+1].header - gLevel->chunk->sectorRows[i].header;
		for(j = 0; j < n; j++){
			se->secx = gLevel->chunk->sectorRows[i].startOff + j;
			se->secy = i;
			se->origin = worldSectorPositions[se->secx][se->secy];
			se->type = SECTOR_WORLD;
			se++;
		}
	}

	// Set up timed objects and hide building swaps, whatever that is exactly
	sLevelSwap *swap = gLevel->chunk->levelSwaps;
	for(i = 0; i < gLevel->chunk->numLevelSwaps; i++){
		BuildingExt *be = GetBuildingExt(swap->id);
		be->swap = swap;
		swap++;
	}

	// Set up interior sectors
	sInteriorSwap *intr = gLevel->chunk->interiors;
	for(i = 0; i < gLevel->chunk->numInteriors; i++){
		se = &gLevel->sectors[intr->sectorId];
		se->secx = intr->secx;
		se->secy = intr->secy;
		se->origin = worldSectorPositions[se->secx][se->secy];
		se->type = SECTOR_INTERIOR;
//		printf("%d %d %d %d %d\n", intr->secx, intr->secy, intr->swapSlot, intr->swapState, intr->sectorId);
		intr++;
	}

#ifdef VCS
	gLevel->areas = (Area**)malloc(gLevel->chunk->numAreas*sizeof(void*));
	memset(gLevel->areas, 0, gLevel->chunk->numAreas*sizeof(void*));

	SwapInfo *swapinfo = gLevel->chunk->swapInfos;
//FILE *xxx = fopen("C:/vcsinteriors.txt", "w");
	for(i = 0; i < gLevel->chunk->numSwapInfos; i++){
		//CBaseModelInfo *miA = CModelInfo::Get(swapinfo->modelA);
		//CBaseModelInfo *miB = CModelInfo::Get(swapinfo->modelB);
		//fprintf(xxx, "%08X %08X %08X %08X %08X %08X %08X\n", swapinfo->entity, swapinfo->hash, swapinfo->swapState, swapinfo->modelA,
		//	swapinfo->modelB, swapinfo->swapSlot, swapinfo->field_18);
		//fprintf(xxx, "\t%s %s\n", miA->name, miB->name);
		swapinfo->building = pBuildingPool->GetSlot(swapinfo->buildingId);
		GetModelInfoExt(swapinfo->modelA)->isSwap = true;
		GetModelInfoExt(swapinfo->modelB)->isSwap = true;
		swapinfo++;
	}
//fclose(xxx);

//	AreaInfo *ai = gLevel->chunk->areas;
//	for(i = 0; i < gLevel->chunk->numAreas; i++){
//		printf("%x %x\n", ai->a, ai->b);
//		ai++;
//	}
#endif
}

void
LoadSector(int n, int interior)
{
	int i;

	if(n >= gLevel->numSectors)
		return;
	sChunkHeader *pheader = gLevel->chunk->sectorRows[0].header + n;
	uint8 *s = (uint8*)malloc(pheader->fileSize-sizeof(sChunkHeader));
	fseek(gLevel->imgfile, pheader->globalTab, SEEK_SET);
	fread(s, 1, pheader->fileSize-sizeof(sChunkHeader), gLevel->imgfile);
	cRelocatableChunk(pheader->ident, pheader->shrink).Fixup(*pheader, s);

	SectorExt *se = &gLevel->sectors[n];
	se->sect = (Sector*)s;
	if(se->type == SECTOR_WORLD)
		worldSectors[se->secx][se->secy] = se;

	se->numInstances = 0;
	se->instances = nil;
	se->dummies = nil;
//	printf("%d %x, %p %p %p %p %p %p %p %p\n", n, se->sect->unk1,
//		se->sect->sectionA, se->sect->sectionB, se->sect->sectionC, se->sect->sectionD,
//		se->sect->sectionE, se->sect->sectionF, se->sect->sectionG, se->sect->sectionEnd);

	// Add new resources to global resource table
	for(i = 0; i < se->sect->numResources; i++){
		OverlayResource *or = &se->sect->resources[i];
		Resource *res = &gLevel->chunk->resourceTable[or->id];
		ResourceExt *re = &gLevel->res[or->id];
		res->raw = or->raw;
		re->sector = se;
	}

	// Make some room for our RW data
	if(se->type == SECTOR_WORLD || se->type == SECTOR_INTERIOR){
		sGeomInstance *inst;
		for(inst = se->sect->passes[0]; inst != se->sect->passes[SECLIST_END]; inst++){
			BuildingExt *be = GetBuildingExt(inst->GetId());
			be->inst = inst;
			be->sect = se;

//useless		GetBuildingExt(inst->GetId())->interior = interior;
//XX			dumpInstBS(gLevel->levelid, inst);
			se->numInstances++;
		}
		se->instances = (rw::Atomic**)malloc(se->numInstances*sizeof(void*));
		memset(se->instances, 0, se->numInstances*sizeof(void*));
		se->dummies = (rw::Atomic**)malloc(se->numInstances*sizeof(void*));
		memset(se->dummies, 0, se->numInstances*sizeof(void*));

		sBuildingSwapInfo *swap = se->sect->swaps;
		for(i = 0; i < se->sect->numSwaps; i++){
			SectorExt *se = &gLevel->sectors[swap->id];
			se->swap = swap;

			LoadSector(swap->id, -1);

//			printf("%d %d %d\t%d\n", trig->timeOff & 0x80, trig->timeOn, trig->timeOff & 0x7F, trig->id);
			swap++;
		}

	}
}

#ifdef VCS
void
LoadArea(int n)
{
	if(n >= gLevel->chunk->numAreas)
		return;
	AreaInfo *ai = &gLevel->chunk->areas[n];

	uint8 *data = (uint8*)malloc(ai->fileSize);
	fseek(gLevel->imgfile, ai->fileOffset, SEEK_SET);
	fread(data, 1, ai->fileSize, gLevel->imgfile);
	sChunkHeader *header = (sChunkHeader*)data;
	cRelocatableChunk(header->ident, header->shrink).Fixup(data);

	Area *a = (Area*)(header+1);
	gLevel->areas[n] = a;

	int i;
	AreaResource *ar = a->resources;
	for(i = 0; i < a->numResources; i++){
//		printf("%x %x \n", ar->id1, ar->id2);
		gLevel->chunk->resourceTable[ar->id1].raw = (uint8*)ar->data;
		ar++;
	}
}
#endif


static uint32
unpackSize(uint32 unpack)
{
	if((unpack&0x6F000000) == 0x6F000000)
		return 2;
	static uint32 size[] = { 32, 16, 8, 16 };
	return ((unpack>>26 & 3)+1)*size[unpack>>24 & 3]/8;
}

static uint32*
skipUnpack(uint32 *p)
{
	int32 n = (p[0] >> 16) & 0xFF;
	return p + (n*unpackSize(p[0])+3 >> 2) + 1;
}

int
countStripVertices(uint32 *data, int32 size)
{
	uint32 *end;
	uint32 *w;
	uint32 nvert;
	uint32 totalVerts;
	bool firstBatch;

	end = &data[size/4];
	w = data;
	totalVerts = 0;
	firstBatch = true;
	while(w < end){
		// GIFtag probably
		assert(w[0] == 0x6C018000);	// UNPACK
		nvert = w[4] & 0x7FFF;
		if(!firstBatch) nvert -=2;
		w += 5;

		// positions
		assert(w[0] == 0x20000000);	// STMASK
		w += 2;
		assert(w[0] == 0x30000000);	// STROW
		w += 5;
		assert((w[0] & 0xFF004000) == 0x79000000);
		w = skipUnpack(w);

		// tex coords
		assert(w[0] == 0x20000000);	// STMASK
		w += 2;
		assert(w[0] == 0x30000000);	// STROW
		w += 5;
		assert((w[0] & 0xFF004000) == 0x76004000);
		w = skipUnpack(w);

		// prelight
		assert((w[0] & 0xFF004000) == 0x6F000000);
		w = skipUnpack(w);

		assert(w[0] == 0x14000006);	// MSCAL
		w++;
		while(w[0] == 0) w++;

		totalVerts += nvert;
	}
	return totalVerts;
}

void
convertBuildingMesh(rw::Geometry *geo, rw::Mesh *m, sClippableBuildingMesh *mesh, uint32 *data, int32 size)
{
	uint32 *end;
	uint32 *w;
	uint32 nvert;
	float uvscale[2];
	rw::ps2::Vertex v;

	int16 *vuVerts = nil;
	uint8 *vuTex = nil;
	uint16 *vuCols = nil;

	uvscale[0] = halfFloatToFloat(mesh->uvScale[0]);
	uvscale[1] = halfFloatToFloat(mesh->uvScale[1]);

	uint32 mask = 0x1101;	// tex coords, prelight, vertices

	end = &data[size/4];
	w = data;
	bool firstInst = m->numIndices == 0;
	bool firstBatch = true;
	while(w < end){
		// GIFtag probably
		assert(w[0] == 0x6C018000);	// UNPACK
		nvert = w[4] & 0x7FFF;
		if(!firstBatch) nvert -=2;
		w += 5;

		// positions
		assert(w[0] == 0x20000000);	// STMASK
		w += 2;
		assert(w[0] == 0x30000000);	// STROW
		w += 5;
		assert((w[0] & 0xFF004000) == 0x79000000);
		vuVerts = (int16*)(w+1);
		if(!firstBatch) vuVerts += 2*3;
		w = skipUnpack(w);

		// tex coords
		assert(w[0] == 0x20000000);	// STMASK
		w += 2;
		assert(w[0] == 0x30000000);	// STROW
		w += 5;
		assert((w[0] & 0xFF004000) == 0x76004000);
		vuTex = (uint8*)(w+1);
		if(!firstBatch) vuTex += 2*2;
		w = skipUnpack(w);

		// prelight
		assert((w[0] & 0xFF004000) == 0x6F000000);
		vuCols = (uint16*)(w+1);
		if(!firstBatch) vuCols += 2;
		w = skipUnpack(w);

		assert(w[0] == 0x14000006);	// MSCAL
		w++;
		while(w[0] == 0) w++;

		/* Insert Data */
		for(uint32 i = 0; i < nvert; i++){
			v.p.x = vuVerts[0]/32767.0f;
			v.p.y = vuVerts[1]/32767.0f;
			v.p.z = vuVerts[2]/32767.0f;
			v.t.u = vuTex[0]/128.0f*uvscale[0];
			v.t.v = vuTex[1]/128.0f*uvscale[1];
			v.c.red   = (vuCols[0] & 0x1f) * 255 / 0x1F;
			v.c.green = (vuCols[0]>>5 & 0x1f) * 255 / 0x1F;
			v.c.blue  = (vuCols[0]>>10 & 0x1f) * 255 / 0x1F;
			v.c.alpha = vuCols[0]&0x8000 ? 0xFF : 0;

			int32 idx = rw::ps2::findVertexSkin(geo, nil, mask, &v);
			if(idx < 0)
				idx = geo->numVertices++;

			if(i == 0 && firstBatch && !firstInst){
				m->indices[m->numIndices++] = m->indices[m->numIndices-1];
				m->indices[m->numIndices++] = idx;
	//			if(inst->numTriangles % 2)
	//				m->indices[m->numIndices++] = idx;
				if(m->numIndices % 2)
					m->indices[m->numIndices++] = idx;
	//			if(inst[-1].numTriangles % 2)
	//				m->indices[m->numIndices++] = idx;
			}

			m->indices[m->numIndices++] = idx;
			rw::ps2::insertVertexSkin(geo, idx, mask, &v);

			vuVerts += 3;
			vuTex += 2;
			vuCols++;
		}
		firstInst = false;
		firstBatch = false;
	}
}

bool
isTextureTransparent(rw::Texture *tex)
{
	if(tex == nil)
		return false;
	return (tex->raster->format & 0xF00) == rw::Raster::C8888 ||
		(tex->raster->format & 0xF00) == rw::Raster::C1555;
}

bool
isGeoTransparent(rw::Geometry *geo)
{
	int i;
	rw::MaterialList *mlist = &geo->matList;
	for(i = 0; i < mlist->numMaterials; i++)
		if(isTextureTransparent(mlist->materials[i]->texture))
			return true;
	return false;
}

void
setGeoMaterialColor(rw::Geometry *geo, rw::RGBA col)
{
	int i;
	rw::MaterialList *mlist = &geo->matList;
	for(i = 0; i < mlist->numMaterials; i++)
		mlist->materials[i]->color = col;
}

int
lookupint(int *haystack, int n, int needle)
{
	int i;
	for(i = 0; i < n; i++)
		if(haystack[i] == needle)
			return i;
	return -1;
}

void
writetex(rw::Texture *tex)
{
	using namespace rw;
	static char filename[1024];

	Image *img = tex->raster->toImage();
	img->unpalettize();

	sprintf(filename, "C:/vcstmp/tex/%s.tga", tex->name);
	writeTGA(img, filename);

	img->destroy();
}

rw::Geometry*
makeWorldGeometry(sBuildingGeometry *bgeom)
{
	using namespace rw;

	int i;
	int matid;
	sClippableBuildingMesh *mesh;
	uint8 *dmaData;
	Geometry *geo;
	int *matmap;
	int nmat;

	geo = Geometry::create(0, 0, Geometry::TRISTRIP | Geometry::LIGHT |
		Geometry::PRELIT | Geometry::TEXTURED);

	mesh = (sClippableBuildingMesh*)(bgeom+1);
	dmaData = (uint8*)mesh + bgeom->size;

	matmap = (int*)malloc(bgeom->numMeshes*sizeof(int));
	nmat = 0;
	// Figure out a material ID for each mesh.
	// Maybe this is overkill, but better be safe.
	for(i = 0; i < bgeom->numMeshes; i++){
//printf("%x %x %x %x\n", i, mesh[i].texID, mesh[i].packetSize, mesh[i].unk1);
		matid = lookupint(matmap, nmat, mesh[i].texID);
		if(matid < 0){
			matid = nmat++;
			matmap[matid] = mesh[i].texID;
		}
	}

	// Manually create material list
	geo->matList.numMaterials = nmat;
	geo->matList.space = nmat;
	geo->matList.materials = rwMallocT(Material*, nmat, MEMDUR_EVENT | ID_MATERIAL);
	for(int32 i = 0; i < nmat; i++){
		geo->matList.materials[i] = Material::create();
		Resource *texres = GetResource(matmap[i]);
		if(texres->raw == nil)
			continue;
		if(texres->dmaChain == nil){
			RslRaster *ras = texres->raster;
			rw::Raster *raster = convertRasterPS2(&ras->ps2);
			if(raster == nil)
				continue;
			rw::Texture *tex = rw::Texture::create(raster);
			static char tmpname[20];
			sprintf(tmpname, "tex_%d_%04X", gLevel->levelid, matmap[i]);
			strncpy(tex->name, FindNameDef(TexName, tmpname), 32);
			tex->setFilter(rw::Texture::LINEAR);
#ifdef DUMPTEXTURES
writetex(tex);
#endif
			texres->dmaChain = tex;
		}
		geo->matList.materials[i]->setTexture((rw::Texture*)texres->dmaChain);
	}

	// allocate empty meshes
	geo->allocateMeshes(geo->matList.numMaterials, 0, 1);
	geo->meshHeader->flags = MeshHeader::TRISTRIP;
	Mesh *meshes = geo->meshHeader->getMeshes();

	// Find out number of indices for every mesh
	for(i = 0; i < bgeom->numMeshes; i++){
		matid = lookupint(matmap, nmat, mesh[i].texID);
		assert(matid >= 0);

		int size = mesh[i].GetPacketSize();
		int n = countStripVertices((uint32*)dmaData, size);
		assert(n != 0);
		geo->numVertices += n;
		meshes[matid].numIndices += n +3;
//		printf("%d %d\n", n, mesh[i].unk1);
		dmaData += size;
	}
	for(uint32 i = 0; i < geo->meshHeader->numMeshes; i++){
		meshes[i].material = geo->matList.materials[i];
		geo->meshHeader->totalIndices += meshes[i].numIndices;
	}

	// Allocate enough space for everything
	geo->numTriangles = geo->meshHeader->guessNumTriangles();
	geo->allocateData();
	geo->allocateMeshes(geo->meshHeader->numMeshes, geo->meshHeader->totalIndices, 0);
	meshes = geo->meshHeader->getMeshes();

	// Zero vertex counts again because the above were just estimates
	for(uint32 i = 0; i < geo->meshHeader->numMeshes; i++)
		meshes[i].numIndices = 0;
	geo->meshHeader->totalIndices = geo->numVertices = 0;

	// Now actually do the conversion
	dmaData = (uint8*)mesh + bgeom->size;
	for(i = 0; i < bgeom->numMeshes; i++){
		matid = lookupint(matmap, nmat, mesh[i].texID);

		int size = mesh[i].GetPacketSize();

		convertBuildingMesh(geo, &meshes[matid], &mesh[i], (uint32*)dmaData, size);
		dmaData += size;
	}

	for(uint32 i = 0; i < geo->meshHeader->numMeshes; i++)
		geo->meshHeader->totalIndices += meshes[i].numIndices;
	geo->calculateBoundingSphere();
	geo->generateTriangles();
//	geo->removeUnusedMaterials();	// this shouldn't be needed

	free(matmap);
	return geo;
};

void
LoadBuildingInst(SectorExt *se, int i)
{
	sGeomInstance *inst = &se->sect->passes[0][i];
	BuildingExt *be = GetBuildingExt(inst->GetId());

	if(se->instances[i])
		return;
	be->GetResourceInfo(inst->resId);	// to collect the models
	Resource *res = GetResource(inst->resId);

	rw::Geometry *geo = nil;
	rw::Matrix m = *(rw::Matrix*)&inst->matrix;
	if(res->raw == nil){
		// This shouldn't happen eventually probably
		m.right.set(1.0f, 0.0f, 0.0f);
		m.up.set(0.0f, 1.0f, 0.0f);
		m.at.set(0.0f, 0.0f, 1.0f);
		geo = cubeGeo;
	}else if(res->dmaChain){
		geo = (rw::Geometry*)res->dmaChain;
	}else{
		geo = makeWorldGeometry(res->geometry);
		res->dmaChain = geo;
	}
	be->isTransparent = isGeoTransparent(geo);

	rw::Atomic *a = rw::Atomic::create();
	rw::Frame *f = rw::Frame::create();
	a->setGeometry(geo, 0);
	a->setFrame(f);
	a->renderCB = Renderer::myRenderCB;
	a->pipeline = Renderer::buildingPipe;
	m.pos = add(m.pos, se->origin);

	m.optimize();
	be->matrix = m;
	f->transform(&m, rw::COMBINEREPLACE);
	se->instances[i] = a;
}

void
LoadSectorInsts(SectorExt *se)
{
	int i;
	for(i = 0; i < se->numInstances; i++)
		LoadBuildingInst(se, i);
}

bool
sLevelSwap::IsVisible(void)
{
	if(IsUnconditional()){
		// what here? Does this even happen?
		return false;
	}else if(IsTimed()){
		if(!ignoreTime && !GetIsTimeInRange(GetTimeOn_State(), GetTimeOff_Slot()))
			return false;
	}else{
		if(!ignoreSwapState && swapstate[GetTimeOff_Slot()] != GetTimeOn_State())
			return false;
	}
	return true;
}

void
RenderSector(SectorExt *se)
{
	int i, j;

	cubeMat->color.red = 255;
	cubeMat->color.green = 255;
	cubeMat->color.blue = 0;
//	pAmbient->setColor(1.0f, 1.0f, 1.0f);

	if(se == nil)
		return;

	// Replace this sector with its interior counterpart
/*
	for(i = 0; i < gLevel->chunk->numInteriors; i++){
		sInteriorSwap *intr = &gLevel->chunk->interiors[i];
		if(intr->secx == se->secx && intr->secy == se->secy &&
		   (drawAllInteriors || swapstate[intr->swapSlot] == intr->swapState)){
			se = &gLevel->sectors[intr->sectorId];
			break;
		}
	}
*/

	for(j = 0; j < SECLIST_END; j++){
	for(sGeomInstance *inst = &se->sect->passes[j][0]; passmask[j] && inst != &se->sect->passes[j+1][0]; inst++){
		i = inst - &se->sect->passes[0][0];

		BuildingExt *be = GetBuildingExt(inst->GetId());
		BuildingExt::Model *m = be->GetResourceInfo(inst->resId);

		if(m->lastFrame == frameCounter)
			continue;
		m->lastFrame = frameCounter;

		// remember matrix for dff dumping
		// shouldn't be needed anymore
		//be->matrix = *(rw::Matrix*)&inst->matrix;
		//be->matrix.pos = add(be->matrix.pos, se->origin);

		bool isSwap = false;
		if(be->swap){
			if(!be->swap->IsVisible())
				continue;
			isSwap = true;
		}

		if((int)inst->IsFlagged() == drawFlagged)
			continue;

		ResourceExt *re = &gLevel->res[inst->resId];
		if(re->sector && re->sector->swap){
			if(!re->sector->swap->IsVisible())
				continue;
			isSwap = true;
		}

		if(!isSwap && drawOnlySwaps)
			continue;

		if(se->instances[i] == nil)
			LoadBuildingInst(se, i);

		float x = halfFloatToFloat(inst->bound[0]);
		float y = halfFloatToFloat(inst->bound[1]);
		float z = halfFloatToFloat(inst->bound[2]);
		float r = halfFloatToFloat(inst->bound[3]);
		rw::Sphere sph = { { x, y, z }, r };
		if(TheCamera.m_rwcam->frustumTestSphere(&sph) == rw::Camera::SPHEREOUTSIDE)
			continue;

		if(drawUnmatched){
			if(be->iplId >= 0){
				CEntity *e = GetEntityById(be->iplId);
				EntityExt *ee = (EntityExt*)e->vtable;
			//	if(ee->n == 1)
				if(ee->n > 0)
					continue;
			}//else
			//	continue;
		}
		if(drawUnnamed && be->iplId >= 0){
			CEntity *e = GetEntityById(be->iplId);
			CBaseModelInfo *mi = CModelInfo::Get(e->modelIndex);
			if(mi->field0)
				continue;
		}

/*
		if(TheCamera.distanceTo(sph.center) < 400.0f){
			CSphere s = { { x, y, z }, r };
			static rw::RGBA c = { 0, 0, 255, 255 };
			static rw::Matrix ident;
			ident.setIdentity();
			RenderWireSphere(&s, c, &ident);
		}
*/

		be->highlight = HIGHLIGHT_NONE;
		if(be->isTransparent)
			Renderer::addToTransparentRenderList(inst, se->instances[i]);
		else
			Renderer::addToOpaqueRenderList(inst, se->instances[i]);
	}
	}
}

void
RenderCubesSector(SectorExt *se)
{
	int i;
	if(se == nil)
		return;

	cubeMat->color.red = 255;
	cubeMat->color.green = 0;
	cubeMat->color.blue = 0;
	pAmbient->setColor(1.0f, 1.0f, 1.0f);

	for(i = 0; i < se->numInstances; i++){
		sGeomInstance *inst = &se->sect->passes[0][i];
		float x = halfFloatToFloat(inst->bound[0]);
		float y = halfFloatToFloat(inst->bound[1]);
		float z = halfFloatToFloat(inst->bound[2]);
		rw::V3d pos = { x, y, z };
		if(se->dummies[i] == nil){
			float r = halfFloatToFloat(inst->bound[3]);

			rw::Atomic *a = rw::Atomic::create();
			rw::Frame *f = rw::Frame::create();
			a->setFrame(f);
			a->setGeometry(cubeGeo, 0);

			f->translate(&pos, rw::COMBINEREPLACE);
			se->dummies[i] = a;
		}
		se->dummies[i]->render();
	}
}

/*
void
dumpInstances(void)
{
	int i, j;
	SectorExt *se;

	for(i = 0; i < gLevel->numStreamingSectors; i++){
		se = gLevel->sectors[i];
		for(j = 0; j < se->numInstances; j++){
			sGeomInstance *inst = &se->sect->sectionA[j];
			float x = halfFloatToFloat(inst->bound[0]);
			float y = halfFloatToFloat(inst->bound[1]);
			float z = halfFloatToFloat(inst->bound[2]);
			printf("%d %d %f %f %f : %f %f %f : %d\n", inst->id & 0x7FFF, inst->resId, x, y, z,
				inst->matrix.pos.x, inst->matrix.pos.y, inst->matrix.pos.z,
				i);
		}
	}
}
*/
