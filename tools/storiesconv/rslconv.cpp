#include "storiesconv.h"

////////////
////////////
//////////// TODO: remove new
////////////
////////////

// TODO: figure out the REAL scaling factors

#define max(a,b) ((a) > (b) ? (a) : (b))
#define min(a,b) ((a) < (b) ? (a) : (b))

int32
mapID(int32 id)
{
	if(id == 255) return -1;
	if(id > 0x80) id |= 0x1300;
	return id;
}

static RslNode*
findNode(RslNode *f, int32 id)
{
	if(f == NULL) return NULL;
	// HACK flag nodes we've already seen
	if(!(f->object.privateFlags & 0x80)){
		if((f->nodeId & 0xFF) == (id & 0xFF)){
			f->object.privateFlags |= 0x80;
			return f;
		}
	}
	RslNode *ff = findNode(f->next, id);
	if(ff) return ff;
	return findNode(f->child, id);
}

static RslNode*
findChild(RslNode *f)
{
	for(RslNode *c = f->child; c; c = c->next)
		if(c->nodeId < 0)
			return c;
	return NULL;
}

int32 nextId;

struct Node {
	int32 id;
	int32 parent;
	RslNode *frame;
};

static int32 *remainingNodeIds;
void assignNodeIds(RslNode *f);
void
assignNodeIdsChild(RslNode *f)
{
	if(f->next)
		assignNodeIdsChild(f->next);
	assignNodeIds(f);
}
void
assignNodeIds(RslNode *f)
{
	// Let's hope frame an hanim hierarchy are in synch!
	f->nodeId = *remainingNodeIds++;
	if(f->nodeId == -1){
		f->nodeId = nextId++;
		remainingNodeIds[-1] = f->nodeId;
	}
	if(f->child)
		assignNodeIdsChild(f->child);
}

void
dumpFrameHier(RslNode *frame, int dumpMat, int ind = 0)
{
	for(int i = 0; i < ind; i++)
		printf("  ");
	const char *name = frame->name;
	printf("*%s %d %s\n", name && name[0] ? name : "---", frame->nodeId, frame->hier ? "HIERARCHY" : "");
	if(frame->hier){
		RslTAnimTree *h = frame->hier;
		int32 stack[100];
		int32 sp = 0;
		stack[sp] = 0;
		int nind = 0;
		stack[sp++] = ind;
		for(int i = 0; i < h->numNodes; i++){
			name = h->pNodeInfo[i].frame ? h->pNodeInfo[i].frame->name : "";
			if(h->pNodeInfo[i].flags & HAnimHierarchy::PUSH)
				stack[sp++] = nind;
			nind++;
			printf("\t\t");
			for(int j = 0; j < nind; j++) printf("  ");
			printf("%d %d\t%p %s\n", (uint8)h->pNodeInfo[i].id, h->pNodeInfo[i].flags, h->pNodeInfo[i].frame, name);
			if(h->pNodeInfo[i].flags & HAnimHierarchy::POP)
				nind = stack[--sp];
		}
	}
	for(RslNode *child = frame->child;
	    child; child = child->next)
		dumpFrameHier(child, dumpMat, ind+1);
}

Frame*
convertFrame(RslNode *f)
{
	Frame *rwf = Frame::create();
	rwf->matrix.right.x =  f->modelling.right.x;
	rwf->matrix.right.y =  f->modelling.right.y;
	rwf->matrix.right.z =  f->modelling.right.z;
	rwf->matrix.up.x =  f->modelling.up.x;
	rwf->matrix.up.y =  f->modelling.up.y;
	rwf->matrix.up.z =  f->modelling.up.z;
	rwf->matrix.at.x =  f->modelling.at.x;
	rwf->matrix.at.y =  f->modelling.at.y;
	rwf->matrix.at.z = f->modelling.at.z;
	rwf->matrix.pos.x = f->modelling.pos.x;
	rwf->matrix.pos.y = f->modelling.pos.y;
	rwf->matrix.pos.z = f->modelling.pos.z;

	if(f->name)
		strncpy(gta::getNodeName(rwf), f->name, 24);

#ifdef LCS
	HAnimData *hanim = PLUGINOFFSET(HAnimData, rwf, hAnimOffset);
	hanim->id = f->nodeId;
	if(f->hier){
		int32 numNodes = f->hier->numNodes;
		int32 *nodeFlags = new int32[numNodes];
		int32 *nodeIDs = new int32[numNodes];

		nextId = 2000;
		Node *nodehier = new Node[numNodes];
		int32 stack[100];
		int32 sp = 0;
		stack[sp] = -1;
		// Match up nodes with frames to fix and assign IDs
		// NOTE: assignment can only work reliably when not more
		//       than one child node needs an ID
		for(int32 i = 0; i < numNodes; i++){
			RslTAnimNodeInfo *ni = &f->hier->pNodeInfo[i];
			Node *n = &nodehier[i];
			n->parent = stack[sp];
			if(ni->flags & HAnimHierarchy::PUSH)
				sp++;
			stack[sp] = i;
//printf("trying to find %d from %s\n", (uint8)ni->id, f->name);
			RslNode *ff = findNode(f, (uint8)ni->id);
			assert(ff);
			n->id = ff->nodeId;
//printf("found %s: %d\n", ff->name, ff->nodeId);
//fflush(stdout);
			if(n->id < 0){
				// may not have found the right frame here
				// so get the parent frame instead and look for a -1 child
				//ff = findNode(f, nodehier[n->parent].id);
				ff = nodehier[n->parent].frame;
//printf("\t\tfound %s for parent id %d\n", ff->name, nodehier[n->parent].id);
//fflush(stdout);
				assert(ff);
				ff = findChild(ff);
				assert(ff);
				n->id = ff->nodeId = nextId++;
			}
			n->frame = ff;
			//printf("%d %s %d %d\n", i, ff->name, n->id, n->parent);
			if(ni->flags & HAnimHierarchy::POP)
				sp--;

			nodeFlags[i] = ni->flags;
			nodeIDs[i] = n->id;
		}

		HAnimHierarchy *hier = HAnimHierarchy::create(numNodes, nodeFlags, nodeIDs, f->hier->flags, f->hier->maxKeyFrameSize);
		hanim->hierarchy = hier;

		delete[] nodeFlags;
		delete[] nodeIDs;
		delete[] nodehier;
	}
#else
	HAnimData *hanim = PLUGINOFFSET(HAnimData, rwf, hAnimOffset);
	if(f->hier){
		int32 numNodes = f->hier->numNodes;
		int32 *nodeFlags = new int32[numNodes];
		int32 *nodeIDs = new int32[numNodes];

		nextId = 2000;
		Node *nodehier = new Node[numNodes];
		int32 stack[100];
		int32 sp = 0;
		stack[sp] = -1;

		for(int32 i = 0; i < numNodes; i++){
			RslTAnimNodeInfo *ni = &f->hier->pNodeInfo[i];
		//	printf("%d %d %d %p\n", ni->id, ni->index, ni->flags, ni->frame);
			nodeFlags[i] = ni->flags;
			nodeIDs[i] = ni->id;
		}
		// Frames have no IDs at all in VCS,
		// so just walk the hierarchy and hope topologies match
		remainingNodeIds = nodeIDs;
		assignNodeIds(f);

#ifdef LCS
		int kfsz = f->hier->maxKeyFrameSize
#else
		int kfsz = 0x24;
#endif
		HAnimHierarchy *hier = HAnimHierarchy::create(numNodes, nodeFlags, nodeIDs, f->hier->flags, kfsz);
		hanim->hierarchy = hier;

		delete[] nodeFlags;
		delete[] nodeIDs;
		delete[] nodehier;
	}
	hanim->id = f->nodeId;
#endif
	return rwf;
}

Texture*
convertTexture(RslTexture *t)
{
	Texture *tex = Texture::read(t->name, t->mask);
	//tex->refCount++;	// ??
	if(tex && tex->refCount == 1)
		tex->filterAddressing = (Texture::WRAP << 12) | (Texture::WRAP << 8) | Texture::LINEAR;
	return tex;
}

Material*
convertMaterial(RslMaterial *m)
{
	Material *rwm;
	rwm = Material::create();

	rwm->color = m->color;
	if(m->texture){
		rwm->texture = convertTexture(m->texture);
		rwm->refCount++;
	}

	if(m->matfx){
		MatFX::setEffects(rwm, m->matfx->effectType);
		MatFX *matfx = MatFX::get(rwm);
		matfx->setEnvCoefficient(m->matfx->env.intensity);
		if(m->matfx->env.texture)
			matfx->setEnvTexture(convertTexture(m->matfx->env.texture));
		*PLUGINOFFSET(MatFX*, rwm, matFXGlobals.materialOffset) = matfx;
	}
	return rwm;
}

void
getWeightIndices(uint8 *w, int n, uint8 *indices)
{
	int i, j, m;

	m = 0;
	memset(indices, 0, n);
	for(i = 0; i < n; i++){
		for(j = 0; j < m; j++)
			if(w[i] >= w[indices[j]]){
				memmove(&indices[j+1], &indices[j], n-j-1);
				m++;
				indices[j] = i;
				goto out;
			}
		indices[m++] = i;
out:;
	}
}

uint align(uint o, uint n) { return o+n-1 & ~(n-1); }

void
convertStripPSP(Geometry *rwg, RslGeometry *g, int32 si)
{
	sPspGeometry *header = (sPspGeometry*)(g+1);
	sPspGeometryMesh *strip = (sPspGeometryMesh*)(header+1);
	strip += si;
	uint8 *p = (uint8*)header + header->offset + strip->offset;
	Mesh *m = &rwg->meshHeader->getMeshes()[strip->matID];
	ps2::Vertex v;
	uint o;
	uint16 col;
	uint8 indices[8], *w;
	int j;

	int uvfmt = header->flags & 3;
	int colfmt = header->flags>>2 & 7;
	int normfmt = header->flags>>5 & 3;
	int posfmt = header->flags>>7 & 3;
	int wghtfmt = header->flags>>9 & 3;
	int idxfmt = header->flags>>11 & 3;
	int nwght = (header->flags>>14 & 7) + 1;
	assert(idxfmt == 0);

	uint32 mask = 0;
	if(posfmt)	mask |= 0x1;
	if(normfmt)	mask |= 0x10;
	if(colfmt)	mask |= 0x100;
	if(uvfmt)	mask |= 0x1000;
	if(wghtfmt)	mask |= 0x10000;

	bool firstStrip = m->numIndices == 0;
	o = 0;
	for(uint32 i = 0; i < strip->numTriangles+2; i++){
		if(wghtfmt){
			assert(wghtfmt == 1);
			w = p+o;
			o += nwght;
			getWeightIndices(w, nwght, indices);
			// /128.0f
			for(j = 0; j < 4 && j < nwght; j++){
				v.w[j] = w[indices[j]]/128.0f;
				v.i[j] = strip->bonemap[indices[j]];
			}
			for(; j < 4; j++){
				v.w[j] = 0.0f;
				v.i[j] = 0;
			}
			for(; j < nwght; j++)
				assert(w[indices[j]] == 0);
		}

		/* Tex coords */
		switch(uvfmt){
		case 0: break;
		case 1:
			v.t.u = *(uint8*)(p+o+0)/128.0f*strip->uvScale[0];
			v.t.v = *(uint8*)(p+o+1)/128.0f*strip->uvScale[1];
			o += 2;
			break;
		default:
			assert(0 && "unsupported tex coord format");
		}

		/* Vertex colour */
		switch(colfmt){
		case 0: break;
		case 5:
			o = align(o, 2);
			col = *(uint16*)(p+o);
			o += 2;
			v.c.red   = (col & 0x1f) * 255 / 0x1F;
			v.c.green = (col>>5 & 0x1f) * 255 / 0x1F;
			v.c.blue  = (col>>10 & 0x1f) * 255 / 0x1F;
			v.c.alpha = col&0x8000 ? 0xFF : 0;
			break;
		default:
			assert(0 && "unsupported colour format");
		}

		/* Normal */
		switch(normfmt){
		case 0: break;
		case 1:
			v.n.x = *(int8*)(p+o+0)/128.0f;
			v.n.y = *(int8*)(p+o+1)/128.0f;
			v.n.z = *(int8*)(p+o+2)/128.0f;
			o += 3;
			break;
		default:
			assert(0 && "unsupported normal format");
		}

		/* Vertex */
		switch(posfmt){
		case 1:
			v.p.x = *(int8*)(p+o+0)/128.0f*header->scale[0] + header->pos[0];
			v.p.y = *(int8*)(p+o+1)/128.0f*header->scale[1] + header->pos[1];
			v.p.z = *(int8*)(p+o+2)/128.0f*header->scale[2] + header->pos[2];
			o += 3;
			break;
		case 2:
			o = align(o, 2);
			v.p.x = *(int16*)(p+o+0)/32768.0f*header->scale[0] + header->pos[0];
			v.p.y = *(int16*)(p+o+2)/32768.0f*header->scale[1] + header->pos[1];
			v.p.z = *(int16*)(p+o+4)/32768.0f*header->scale[2] + header->pos[2];
			o += 6;
			break;
		default:
			assert(0 && "unsupported vertex format");
		}

		int32 idx = ps2::findVertexSkin(rwg, NULL, mask, &v);
		if(idx < 0)
			idx = rwg->numVertices++;

		if(i == 0 && !firstStrip){
			m->indices[m->numIndices++] = m->indices[m->numIndices-1];
			m->indices[m->numIndices++] = idx;
			if(m->numIndices % 2)
				m->indices[m->numIndices++] = idx;
		}

		m->indices[m->numIndices++] = idx;
		ps2::insertVertexSkin(rwg, idx, mask, &v);
	}
}

Atomic*
convertAtomicPSP(RslElement *atomic)
{
	Atomic *rwa = Atomic::create();
	RslGeometry *g = atomic->geometry;
	Geometry *rwg = Geometry::create(0, 0, 0);
	rwa->geometry = rwg;

	*PLUGINOFFSET(RslElement*, rwa, atmOffset) = atomic;

	rwg->matList.numMaterials = g->matList.numMaterials;
	rwg->matList.space = rwg->matList.numMaterials;
	rwg->matList.materials = rwMallocT(Material*, rwg->matList.numMaterials, MEMDUR_EVENT | ID_MATERIAL);
	for(int32 i = 0; i < rwg->matList.numMaterials; i++)
		rwg->matList.materials[i] = convertMaterial(g->matList.materials[i]);

	sPspGeometry *header = (sPspGeometry*)(g+1);
	sPspGeometryMesh *strip = (sPspGeometryMesh*)(header+1);

	// allocate empty meshes
	rwg->allocateMeshes(rwg->matList.numMaterials, 0, 1);
	rwg->meshHeader->flags = MeshHeader::TRISTRIP;
	Mesh *meshes = rwg->meshHeader->getMeshes();

	for(uint32 i = 0; i < header->numStrips; i++){
		rwg->numVertices += strip[i].numTriangles+2;
		meshes[strip[i].matID].numIndices += strip[i].numTriangles+2 +3;
	}
	for(uint32 i = 0; i < rwg->meshHeader->numMeshes; i++){
		meshes[i].material = rwg->matList.materials[i];
		rwg->meshHeader->totalIndices += meshes[i].numIndices;
	}
switch(header->flags){
case 0x120: case 0x121: case 0x115: case 0x114: case 0xA1: case 0x1c321: break;
default: printf("unknown format %X\n", header->flags); exit(0);
}
	rwg->flags = Geometry::TRISTRIP |
	                Geometry::LIGHT;
	if(rwg->hasColoredMaterial())
		rwg->flags |= Geometry::MODULATE;
	if((header->flags & 0x3) != 0){
		rwg->flags |= Geometry::TEXTURED;
		rwg->numTexCoordSets = 1;
	}
	if((header->flags & 0x1C) != 0)
		rwg->flags |= Geometry::PRELIT;
	if((header->flags & 0x60) != 0)
		rwg->flags |= Geometry::NORMALS;
	if((header->flags & 0x180) != 0)
		rwg->flags |= Geometry::POSITIONS;

	rwg->numTriangles = rwg->meshHeader->guessNumTriangles();
	rwg->allocateData();
	rwg->allocateMeshes(rwg->meshHeader->numMeshes, rwg->meshHeader->totalIndices, 0);
	meshes = rwg->meshHeader->getMeshes();

	Skin *skin = NULL;
	if(header->flags>>9 & 3)
		assert(g->skin);
	if(g->skin){
		skin = rwNewT(Skin, 1, MEMDUR_EVENT | ID_SKIN);
		*PLUGINOFFSET(Skin*, rwg, skinGlobals.geoOffset) = skin;
		skin->init(g->skin->numBones, g->skin->numBones, rwg->numVertices);
		memcpy(skin->inverseMatrices, g->skin->invMatrices, skin->numBones*64);
	}

	for(uint32 i = 0; i < rwg->meshHeader->numMeshes; i++)
		meshes[i].numIndices = 0;
	rwg->meshHeader->totalIndices = rwg->numVertices = 0;
	for(int32 i = 0; i < header->numStrips; i++)
		convertStripPSP(rwg, g, i);
	if(skin){
		skin->findNumWeights(rwg->numVertices);
		skin->findUsedBones(rwg->numVertices);
	}
	for(uint32 i = 0; i < rwg->meshHeader->numMeshes; i++)
		rwg->meshHeader->totalIndices += meshes[i].numIndices;
	rwg->calculateBoundingSphere();
	rwg->generateTriangles();
	rwg->removeUnusedMaterials();
	return rwa;
}

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

void
convertStripPS2(Geometry *rwg, RslGeometry *g, int32 si)
{
	sPs2Geometry *resHeader = (sPs2Geometry*)(g+1);
	sPs2GeometryMesh *strip = (sPs2GeometryMesh*)(resHeader+1);
	int32 numStrips = resHeader->size >> 20;
	uint8 *p = (uint8*)(strip+numStrips);
	strip += si;
	p += strip->dmaPacket;
	Mesh *m = &rwg->meshHeader->getMeshes()[strip->matID];
	ps2::Vertex v;
	uint32 mask = 0x1001;	// tex coords, vertices
	if(rwg->flags & Geometry::NORMALS)
		mask |= 0x10;
	if(rwg->flags & Geometry::PRELIT)
		mask |= 0x100;
	Skin *skin = *PLUGINOFFSET(Skin*, rwg, skinGlobals.geoOffset);
	if(skin)
		mask |= 0x10000;

	int16 *vuVerts = NULL;
	int8 *vuNorms = NULL;
	uint8 *vuTex = NULL;
	uint16 *vuCols = NULL;
	uint32 *vuSkin = NULL;

	uint32 *w = (uint32*)p;
	uint32 *end = (uint32*)(p + ((w[0] & 0xFFFF) + 1)*0x10);
	w += 4;
	int32 nvert;
	bool firstInst = m->numIndices == 0;
	bool firstBatch = 1;
	while(w < end){
		/* Get data pointers */
//if(firstInst)
//printf("$$$$$$$$$$\n");
//if(firstBatch)
//printf("##########\n");

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

		if(rwg->flags & Geometry::PRELIT){
			assert((w[0] & 0xFF004000) == 0x6F000000);
			vuCols = (uint16*)(w+1);
			if(!firstBatch) vuCols += 2;
			w = skipUnpack(w);
		}

		if(rwg->flags & Geometry::NORMALS){
			assert((w[0] & 0xFF004000) == 0x6A000000);
			vuNorms = (int8*)(w+1);
			if(!firstBatch) vuNorms += 2*3;
			w = skipUnpack(w);
		}

		if(skin){
			assert((w[0] & 0xFF004000) == 0x6C000000);
			vuSkin = w+1;
			if(!firstBatch) vuSkin += 2*4;
			w = skipUnpack(w);
		}

		assert(w[0] == 0x14000006);	// MSCAL
		w++;
		while(w[0] == 0) w++;

		/* Insert Data */
		for(int32 i = 0; i < nvert; i++){
			// TODO: figure out the exact scaling here!!!
			// mobile divides by 32767
			v.p.x = vuVerts[0]/32767.5f*resHeader->scale[0] + resHeader->pos[0];
			v.p.y = vuVerts[1]/32767.5f*resHeader->scale[1] + resHeader->pos[1];
			v.p.z = vuVerts[2]/32767.5f*resHeader->scale[2] + resHeader->pos[2];
			// mobile divides by 255
			v.t.u = vuTex[0]/127.5f*strip->uvScale[0];
			v.t.v = vuTex[1]/127.5f*strip->uvScale[1];
			if(mask & 0x10){
				// mobile divides by 127
				v.n.x = vuNorms[0]/128.0f;
				v.n.y = vuNorms[1]/128.0f;
				v.n.z = vuNorms[2]/128.0f;
			}
			if(mask & 0x100){
				v.c.red   = (vuCols[0] & 0x1f) * 255 / 0x1F;
				v.c.green = (vuCols[0]>>5 & 0x1f) * 255 / 0x1F;
				v.c.blue  = (vuCols[0]>>10 & 0x1f) * 255 / 0x1F;
				v.c.alpha = vuCols[0]&0x8000 ? 0xFF : 0;
			}
			if(mask & 0x10000){
				for(int j = 0; j < 4; j++){
					// not sure how many bits are an index, 10 is too much for VCS
					((uint32*)v.w)[j] = vuSkin[j] & ~0xFF;
					v.i[j] = (vuSkin[j] >> 2) & 0x3F;
					//if(v.i[j]) v.i[j]--;
					if(v.w[j] == 0.0f) v.i[j] = 0;
				}
			}

			int32 idx = ps2::findVertexSkin(rwg, NULL, mask, &v);
			if(idx < 0)
				idx = rwg->numVertices++;

			// For some geometry
			// Insts normally overlap by two vertices (same as batches).
			// For some reason each inst is assumed to end in an odd vertex.
			// This is enforced by doubling the last vertex if necessary,
			// in which case we have one more overlapping vertex.
//			if(i == 0 && firstBatch && !firstInst){
//				m->numIndices -= 2;
//				if(inst->numTriangles % 2)
//					m->numIndices--;
//			}

//printf("%d %d\n", m->numIndices % 2, idx);

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
			ps2::insertVertexSkin(rwg, idx, mask, &v);

			vuVerts += 3;
			vuTex += 2;
			vuNorms += 3;
			vuCols++;
			vuSkin += 4;
		}
		firstInst = 0;
		firstBatch = 0;
	}
}

Atomic*
convertAtomicPS2(RslElement *atomic)
{
	Atomic *rwa = Atomic::create();
	RslGeometry *g = atomic->geometry;
	Geometry *rwg = Geometry::create(0, 0, 0);
	rwa->geometry = rwg;

	*PLUGINOFFSET(RslElement*, rwa, atmOffset) = atomic;

	rwg->matList.numMaterials = g->matList.numMaterials;
	rwg->matList.space = rwg->matList.numMaterials;
	rwg->matList.materials = rwMallocT(Material*, rwg->matList.numMaterials, MEMDUR_EVENT | ID_MATERIAL);
	for(int32 i = 0; i < rwg->matList.numMaterials; i++)
		rwg->matList.materials[i] = convertMaterial(g->matList.materials[i]);

	sPs2Geometry *resHeader = (sPs2Geometry*)(g+1);
	sPs2GeometryMesh *strip = (sPs2GeometryMesh*)(resHeader+1);
	int32 numStrips = resHeader->size >> 20;

	// allocate empty meshes
	rwg->allocateMeshes(rwg->matList.numMaterials, 0, 1);
	rwg->meshHeader->flags = MeshHeader::TRISTRIP;
	Mesh *meshes = rwg->meshHeader->getMeshes();

	for(int32 i = 0; i < numStrips; i++){
		rwg->numVertices += strip[i].numTriangles+2;
		meshes[strip[i].matID].numIndices += strip[i].numTriangles+2 +3;
	}
	for(uint32 i = 0; i < rwg->meshHeader->numMeshes; i++){
		meshes[i].material = rwg->matList.materials[i];
		rwg->meshHeader->totalIndices += meshes[i].numIndices;
	}
	rwg->flags = Geometry::TRISTRIP |
	                Geometry::LIGHT;
	if(rwg->hasColoredMaterial())
		rwg->flags |= Geometry::MODULATE;
	if(resHeader->flags & 0x1)
		rwg->flags |= Geometry::POSITIONS;
	if(resHeader->flags & 0x2)
		rwg->flags |= Geometry::NORMALS;
	if(resHeader->flags & 0x4){
		rwg->flags |= Geometry::TEXTURED;
		rwg->numTexCoordSets = 1;
	}
	if(resHeader->flags & 0x8)
		rwg->flags |= Geometry::PRELIT;

	rwg->numTriangles = rwg->meshHeader->guessNumTriangles();
	rwg->allocateData();
	rwg->allocateMeshes(rwg->meshHeader->numMeshes, rwg->meshHeader->totalIndices, 0);
	meshes = rwg->meshHeader->getMeshes();

	Skin *skin = NULL;
	if(resHeader->flags & 0x10)
		assert(g->skin);
	if(g->skin){
		skin = rwNewT(Skin, 1, MEMDUR_EVENT | ID_SKIN);
		*PLUGINOFFSET(Skin*, rwg, skinGlobals.geoOffset) = skin;
		skin->init(g->skin->numBones, g->skin->numBones, rwg->numVertices);
		memcpy(skin->inverseMatrices, g->skin->invMatrices, skin->numBones*64);
	}

	for(uint32 i = 0; i < rwg->meshHeader->numMeshes; i++)
		meshes[i].numIndices = 0;
	rwg->meshHeader->totalIndices = rwg->numVertices = 0;
	for(int32 i = 0; i < numStrips; i++)
		convertStripPS2(rwg, g, i);
	for(uint32 i = 0; i < rwg->meshHeader->numMeshes; i++)
		rwg->meshHeader->totalIndices += meshes[i].numIndices;
	if(skin){
		skin->findNumWeights(rwg->numVertices);
		skin->findUsedBones(rwg->numVertices);
	}
	rwg->calculateBoundingSphere();
	rwg->generateTriangles();
	rwg->removeUnusedMaterials();
	return rwa;
}

Atomic*
convertAtomic(RslElement *atomic)
{
	if(RslPSP)
		return convertAtomicPSP(atomic);
	else
		return convertAtomicPS2(atomic);
}

RslElement*
collectElements(RslElement *atomic, void *data)
{
	RslElement ***alist = (RslElement***)data;
	*(*alist)++ = atomic;
	return atomic;
}

Clump*
convertClump(RslElementGroup *c)
{
	Clump *rwc;
	Frame *rwf;
	Atomic *rwa;
	rslNodeList frameList;

	rwc = Clump::create();
	rslNodeListInitialize(&frameList, (RslNode*)c->object.parent);
	Frame **rwframes = new Frame*[frameList.numNodes];
//dumpFrameHier((RslNode*)c->object.parent, 0);
#ifdef VCS
	for(int32 i = 0; i < frameList.numNodes; i++)
		frameList.frames[i]->nodeId = -1;
#endif
	for(int32 i = 0; i < frameList.numNodes; i++){
		rwf = convertFrame(frameList.frames[i]);
		rwframes[i] = rwf;
		void *par = frameList.frames[i]->object.parent;
		int32 parent = findPointer(par, (void**)frameList.frames, frameList.numNodes);
		if(parent >= 0)
			rwframes[parent]->addChild(rwf);
	}
	rwc->object.parent = rwframes[0];

	int32 numElements = RslElementGroupGetNumElements(c);
	RslElement **alist = new RslElement*[numElements];
	RslElement **ap = &alist[0];
	RslElementGroupForAllElements(c, collectElements, &ap);
	for(int32 i = 0; i < numElements; i++){
		rwa = convertAtomic(alist[i]);
		int32 fi = findPointer(alist[i]->object.object.parent, (void**)frameList.frames, frameList.numNodes);
		rwa->setFrame(rwframes[fi]);
		rwc->addAtomic(rwa);
	}

	delete[] alist;
	delete[] rwframes;
	delete[] frameList.frames;
	return rwc;
}


RslElement*
makeTextures(RslElement *atomic, void*)
{
	RslGeometry *g = atomic->geometry;
	RslMaterial *m;
	for(int32 i = 0; i < g->matList.numMaterials; i++){
		m = g->matList.materials[i];
		if(m->texname){
			RslTexture *tex = RslTextureCreate(NULL);
			strncpy(tex->name, m->texname, 32);
			strncpy(tex->mask, m->texname, 32);
			m->texture = tex;
		}
		if(m->matfx && m->matfx->effectType == MatFX::ENVMAP &&
		   m->matfx->env.texname){
			RslTexture *tex = RslTextureCreate(NULL);
			strncpy(tex->name, m->matfx->env.texname, 32);
			strncpy(tex->mask, m->matfx->env.texname, 32);
			m->matfx->env.texture = tex;
		}
	}
	return atomic;
}

void
moveAtomics(Frame *f)
{
	static const char *names[] = { "", "hi_ok", "hi_dam" };
	if(f == NULL) return;
	int n = f->objectList.count();
	if(n > 1){
		char *oldname = gta::getNodeName(f);
		ObjectWithFrame **objs = new ObjectWithFrame*[n];
		int i = 0;
		FORLIST(lnk, f->objectList){
			ObjectWithFrame *obj = ObjectWithFrame::fromFrame(lnk);
			assert(obj->object.type == Atomic::ID);
			objs[i] = obj;
			obj->setFrame(NULL);
			i++;
		}
		for(i = 0; i < n; i++){
			Frame *ff = Frame::create();
			RslElement *rsla = *PLUGINOFFSET(RslElement*, objs[i], atmOffset);
			char *name = gta::getNodeName(ff);
			strncpy(name, oldname, 24);
			char *end = strrchr(name, '_');
			if(end){
				*(++end) = '\0';
				strcat(end, names[rsla->visIdFlag&3]);
			}
			f->addChild(ff);
			objs[i]->setFrame(ff);
		}
		delete[] objs;
	}
	moveAtomics(f->next);
	moveAtomics(f->child);
}

uint8*
getTexelPSP(RslRasterPSP *raster, int32 n)
{
	uint8 *data = raster->data;
	uint32 w = 1 << raster->logWidth;
	uint32 h = 1 << raster->logHeight;
	while(n--){
		w = max(w, raster->minWidth);
		data += w*h*raster->depth/8;
		w /= 2;
		h /= 2;
	}
	return data;
}

uint8*
getPalettePSP(RslRasterPSP *raster)
{
	if(raster->depth > 8)
		return nil;
	return getTexelPSP(raster, raster->mipmaps);
}

uint8*
getTexelPS2(RslRasterPS2 *raster, int32 n)
{
	uint32 f = raster->flags;
	uint32 w = 1 << (f & 0x3F);
	uint32 h = 1 << (f>>6 & 0x3F);
	uint32 d = f>>12 & 0x3F;
	uint8 *data = raster->data;
	while(n--){
		data += w*h*d/8;
		w /= 2;
		h /= 2;
	}
	return data;
}

uint8*
getPalettePS2(RslRasterPS2 *raster)
{
	uint32 f = raster->flags;
	uint32 d = f>>12 & 0x3F;
	uint32 mip = f>>20 & 0xF;
	if(d > 8)
		return NULL;
	return getTexelPS2(raster, mip);
}

void
convertCLUT(uint8 *texels, uint32 w, uint32 h)
{
	static uint8 map[4] = { 0x00, 0x10, 0x08, 0x18 };
	for(uint32 i = 0; i < w*h; i++)
		texels[i] = (texels[i] & ~0x18) | map[(texels[i] & 0x18) >> 3];
}

static uint32
swizzlePS2(uint32 x, uint32 y, uint32 logw)
{
#define X(n) ((x>>(n))&1)
#define Y(n) ((y>>(n))&1)

	uint32 nx, ny, n;
	x ^= (Y(1)^Y(2))<<2;
	nx = x&7 | (x>>1)&~7;
	ny = y&1 | (y>>1)&~1;
	n = Y(1) | X(3)<<1;
	return n | nx<<2 | ny<<logw-1+2;
}

void
unswizzle8PS2(uint8 *dst, uint8 *src, uint32 w, uint32 h)
{
	uint32 logw = 0;
	for(uint32 i = 1; i < w; i *= 2) logw++;
	for(uint32 y = 0; y < h; y++)
		for(uint32 x = 0; x < w; x++)
			dst[(y<<logw)+x] = src[swizzlePS2(x, y, logw)];
}

/* algorithm from PPSSPP */
void
unswizzlePSP(uint32 *dst, uint32 *src, uint32 w, uint32 h, uint32 d)
{
	uint32 stride = w * d / 8;
	uint32 nbx = stride / 16;
	uint32 nby = (h + 7) / 8;
	stride >>= 2;
	uint32 *row, *b;

	/* walk by blocks in y and x, then block rows */
	while(nby--){
		row = dst;
		for(uint32 x = 0; x < nbx; x++){
			b = row;
			for(uint32 n = 0; n < 8; n++){
				memcpy(b, src, 16);
				b += stride;
				src += 4;
			}
			row += 4;
		}
		dst += stride * 8;
	}
}

bool32 unswizzle = 1;

/* A bit wasteful maybe, but we don't have to allocate */
static uint8 mem[2*1024*1024];

/* Again all this? */
void
convertTo32_PS2(uint8 *out, uint8 *pal, uint8 *tex,
            uint32 w, uint32 h, uint32 d, bool32 swiz)
{
	uint32 x;
	if(d == 32){
		// assumes texture width isn't less than 4
		uint32 *dat = new uint32[w*h];
		if(swiz && unswizzle)
			// Yes, PSP. must be leftover
			unswizzlePSP(dat, (uint32*)tex, w, h, d);
		else
			memcpy(dat, tex, w*h*4);
		tex = (uint8*)dat;
		for(uint32 i = 0; i < w*h; i++){
			out[i*4+0] = tex[i*4+0];
			out[i*4+1] = tex[i*4+1];
			out[i*4+2] = tex[i*4+2];
			out[i*4+3] = tex[i*4+3]*255/128;
		}
		delete[] dat;
	}
	if(d == 16) assert(0); return;	// TODO
	if(d == 8){
		uint8 *dat = new uint8[w*h];
		if(swiz && unswizzle)
			unswizzle8PS2(dat, tex, w, h);
		else
			memcpy(dat, tex, w*h);
		tex = dat;
		convertCLUT(tex, w, h);
		for(uint32 i = 0; i < h; i++)
			for(uint32 j = 0; j < w; j++){
				x = *tex++;
				*out++ = pal[x*4+0];
				*out++ = pal[x*4+1];
				*out++ = pal[x*4+2];
				*out++ = pal[x*4+3]*255/128;
			}
		delete[] dat;
	}
	if(d == 4){
		uint8 *dat = new uint8[w*h];
		for(uint32 i = 0; i < w*h/2; i++){
			dat[i*2+0] = tex[i] & 0xF;
			dat[i*2+1] = tex[i] >> 4;
		}
		if(swiz && unswizzle){
			uint8 *tmp = new uint8[w*h];
			unswizzle8PS2(tmp, dat, w, h);
			delete[] dat;
			dat = tmp;
		}
		tex = dat;
		for(uint32 i = 0; i < h; i++)
			for(uint32 j = 0; j < w; j++){
				x = *tex++;
				*out++ = pal[x*4+0];
				*out++ = pal[x*4+1];
				*out++ = pal[x*4+2];
				*out++ = pal[x*4+3]*255/128;
			}
		delete[] dat;
	}
}

void
convertTo32_PSP(uint8 *out, uint8 *pal, uint8 *tex,
            uint32 w, uint32 h, uint32 d, uint32 minWidth)
{
	uint32 bufw;
	uint32 x;

	bufw = max(w, minWidth);
	if(unswizzle)
		unswizzlePSP((uint32*)mem, (uint32*)tex, bufw, h, d);
	else
		memcpy(mem, tex, bufw*h*d/8);
	tex = mem;
	switch(d){
	case 32:
		for(uint32 i = 0; i < w*h; i++){
			out[i*4+0] = tex[i*4+1];
			out[i*4+1] = tex[i*4+1];
			out[i*4+2] = tex[i*4+2];
			out[i*4+3] = tex[i*4+3];
		}
		break;
	case 16: assert(0); return;
	case 8:
		for(uint32 i = 0; i < h; i++)
			for(uint32 j = 0; j < w; j++){
				x = *tex++;
				*out++ = pal[x*4+0];
				*out++ = pal[x*4+1];
				*out++ = pal[x*4+2];
				*out++ = pal[x*4+3];
			}
		break;
	case 4:
		uint8 *dat = new uint8[w*h];
		rw::expandPal4(dat, w, tex, bufw/2, w, h);
		tex = dat;
		for(uint32 i = 0; i < h; i++)
			for(uint32 j = 0; j < w; j++){
				x = *tex++;
				*out++ = pal[x*4+0];
				*out++ = pal[x*4+1];
				*out++ = pal[x*4+2];
				*out++ = pal[x*4+3];
			}
		delete[] dat;
	}
}

static void
dumpLevels(RslTexture *texture)
{
	uint32 f = texture->raster->ps2.flags;
	uint32 w = 1 << (f & 0x3F);
	uint32 h = 1 << (f>>6 & 0x3F);
	uint32 d = f>>12 & 0x3F;
	uint32 mip = f>>20 & 0xF;
	uint32 swizmask = f>>24;
	int i;

	for(i = 0; i < mip; i++){
		printf("%d %d %d %d %d\n", d, w, h, i, swizmask&1);
		swizmask >>= 1;
		w /= 2;
		h /= 2;
	}
}

RslTexture*
dumpTextureCB(RslTexture *texture, void*)
{
	Image *img;
	if(RslPSP){
		RslRasterPSP *r = &texture->raster->psp;
		r->minWidth = 128/r->depth;	// min 16 bytes, somehow this isn't always right?
		uint32 w = 1 << r->logWidth;
		uint32 h = 1 << r->logHeight;
		uint8 *palette = getPalettePSP(r);
		uint8 *texels = getTexelPSP(r, 0);
		img = Image::create(w, h, 32);
		img->allocate();
//printf("%-32s %d %d %d %d %X %X\n", texture->name, w, h, r->depth, r->mipmaps, r->unk1, r->unk2);
		convertTo32_PSP(img->pixels, palette, texels, w, h, r->depth, r->minWidth);
	}else{
		RslRasterPS2 *r = &texture->raster->ps2;
		uint32 f = r->flags;
		uint32 w = 1 << (f & 0x3F);
		uint32 h = 1 << (f>>6 & 0x3F);
		uint32 d = f>>12 & 0x3F;
		uint32 mip = f>>20 & 0xF;
		uint32 swizmask = f>>24;
		uint8 *palette = getPalettePS2(r);
		uint8 *texels = getTexelPS2(r, 0);
		img = Image::create(w, h, 32);
		img->allocate();
		convertTo32_PS2(img->pixels, palette, texels, w, h, d, swizmask&1);
	}
	char *name = new char[strlen(texture->name)+5];
	strcpy(name, texture->name);
	strcat(name, ".tga");
	writeTGA(img, name);
	img->destroy();
	delete[] name;
	return texture;
}

rw::Raster*
convertRasterPSP(RslRasterPSP *ras)
{
	uint32 w, h, bufw;
	ras->minWidth = 128/ras->depth;	// min 16 bytes, somehow this isn't always right?
	uint8 *palette = getPalettePSP(ras);
	uint8 *texels = getTexelPSP(ras, 0);

	w = 1 << ras->logWidth;
	h = 1 << ras->logHeight;
	bufw = max(w, ras->minWidth);
	if(unswizzle)
		unswizzlePSP((uint32*)mem, (uint32*)texels, bufw, h, ras->depth);
	else
		memcpy(mem, texels, bufw*h*ras->depth/8);

	rw::Image *img = rw::Image::create(w, h, ras->depth);
	img->allocate();

	switch(ras->depth){
	case 4:
		rw::expandPal4(img->pixels, img->stride, mem, bufw/2, w, h);
		memcpy(img->palette, palette, 16*4);
		break;
	case 8:
		rw::copyPal8(img->pixels, img->stride, mem, bufw, w, h);
		memcpy(img->palette, palette, 256*4);
		break;
	case 16: assert(0);
	case 32:
		memcpy(img->pixels, mem, w*h*4);
		break;

	default: assert(0 && "unknown bit depth");
	}

//
// TODO: don't ALWAYS do this
//
	img->unpalettize();
	rw::Raster *rwras = rw::Raster::createFromImage(img);
	img->destroy();
	return rwras;
}

rw::Raster*
convertRasterPS2(RslRasterPS2 *ras)
{
	uint32 f = ras->flags;
	uint32 logw = f & 0x3F;
	uint32 logh = f>>6 & 0x3F;
	uint32 w = 1 << logw;
	uint32 h = 1 << logh;
	uint32 d = f>>12 & 0x3F;
	//uint32 mip = f>>20 & 0xF;
	uint32 swizmask = f>>24;

	uint8 *palette = getPalettePS2(ras);
	uint8 *texels = getTexelPS2(ras, 0);

	uint8 *convtex = NULL;
	if(d == 4){
		convtex = rwNewT(uint8, w*h, 0);
		for(uint32 i = 0; i < w*h/2; i++){
			int32 a = texels[i] & 0xF;
			int32 b = texels[i] >> 4;
			convtex[i*2+0] = a;
			convtex[i*2+1] = b;
		}
		if(swizmask & 1 && unswizzle){
			uint8 *tmp = rwNewT(uint8, w*h, 0);
			unswizzle8PS2(tmp, convtex, w, h);
			rwFree(convtex);
			convtex = tmp;
		}
	}else if(d == 8){
		convtex = rwNewT(uint8, w*h, 0);
		if(swizmask & 1 && unswizzle)
			unswizzle8PS2(convtex, texels, w, h);
		else
			memcpy(convtex, texels, w*h);
		convertCLUT(convtex, w, h);
	}

	rw::Image *img = rw::Image::create(w, h, d);
	img->allocate();
	int32 pallen = d == 4 ? 16 :
	               d == 8 ? 256 : 0;
	if(pallen){
		uint8 *p = img->palette;
		for(int32 i = 0; i < pallen; i++){
			p[i*4+0] = palette[i*4+0];
			p[i*4+1] = palette[i*4+1];
			p[i*4+2] = palette[i*4+2];
			p[i*4+3] = palette[i*4+3]*255/128;
		}
		memcpy(img->pixels, convtex, w*h);
	}else
		memcpy(img->pixels, texels, w*h*d/8);

	if(d == 32){
		uint32 *dat = new uint32[w*h];
		if(swizmask & 1 && unswizzle)
			// Yes, PSP. must be leftover
			unswizzlePSP(dat, (uint32*)texels, w, h, d);
		else
			memcpy(dat, texels, w*h*4);
		texels = (uint8*)dat;
		for(uint32 i = 0; i < w*h; i++){
			img->pixels[i*4+0] = texels[i*4+0];
			img->pixels[i*4+1] = texels[i*4+1];
			img->pixels[i*4+2] = texels[i*4+2];
			img->pixels[i*4+3] = texels[i*4+3]*255/128;
		}
		delete[] dat;
	}
//
// TODO: don't ALWAYS do this
//
	img->unpalettize();
	rw::Raster *rwras = rw::Raster::createFromImage(img);
	img->destroy();
	rwFree(convtex);
	return rwras;
}

rw::Raster*
convertRaster(RslRaster *ras)
{
	if(RslPSP)
		return convertRasterPSP(&ras->psp);
	else
		return convertRasterPS2(&ras->ps2);
}

RslTexture*
convertNativeTexture(RslTexture *texture, void *data)
{
	TexDictionary *txd = (TexDictionary*)data;

	Texture *rwtex = Texture::create(convertRaster(texture->raster));
	strncpy(rwtex->name, texture->name, 32);
	strncpy(rwtex->mask, texture->mask, 32);
	rwtex->setFilter(Texture::LINEAR);
	rwtex->setAddressU(Texture::WRAP);
	rwtex->setAddressV(Texture::WRAP);

	txd->add(rwtex);
	return texture;
}

TexDictionary*
convertTXD(RslTexList *txd)
{
	TexDictionary *rwtxd = TexDictionary::create();
	RslTexListForAllTextures(txd, convertNativeTexture, rwtxd);
	return rwtxd;
}

