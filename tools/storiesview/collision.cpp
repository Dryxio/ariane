#include "storiesview.h"

int32
WriteCollisionFile(CColModel *colmodel, uint8 **bufp)
{
	using namespace rw;

	uint8 *buf;
	uint32 size;
	int32 numVertices = -1;
	for(int i = 0; i < colmodel->numTriangles; i++){
		if(colmodel->triangles[i].a/6 > numVertices)
			numVertices = colmodel->triangles[i].a/6;
		if(colmodel->triangles[i].b/6 > numVertices)
			numVertices = colmodel->triangles[i].b/6;
		if(colmodel->triangles[i].c/6 > numVertices)
			numVertices = colmodel->triangles[i].c/6;
	}
	numVertices++;
	size = 10*sizeof(float32) + 5*sizeof(int32) +
		colmodel->numSpheres*20 +
		colmodel->numBoxes*28 + numVertices*12 + colmodel->numTriangles*16;
	*bufp = buf = rwNewT(uint8, size, 0);
	memset(buf, 0, size);
	uint8 *end = buf + size;

	float *fp = (float*)buf;
	*fp++ = colmodel->boundingSphere.radius;
	*fp++ = colmodel->boundingSphere.center.x;
	*fp++ = colmodel->boundingSphere.center.y;
	*fp++ = colmodel->boundingSphere.center.z;
	*fp++ = colmodel->boundingBox.min.x;
	*fp++ = colmodel->boundingBox.min.y;
	*fp++ = colmodel->boundingBox.min.z;
	*fp++ = colmodel->boundingBox.max.x;
	*fp++ = colmodel->boundingBox.max.y;
	*fp++ = colmodel->boundingBox.max.z;
	buf = (uint8*)fp;

	*(int32*)buf = colmodel->numSpheres;
	buf += 4;
	for(int i = 0; i < colmodel->numSpheres; i++){
		*(float*)(buf+0) = colmodel->spheres[i].sph.radius;
//		*(CVector*)(buf+4) = colmodel->spheres[i].sph.center;
		*(float*)(buf+4) = colmodel->spheres[i].sph.center.x;
		*(float*)(buf+8) = colmodel->spheres[i].sph.center.y;
		*(float*)(buf+12) = colmodel->spheres[i].sph.center.z;
		buf[16] = colmodel->spheres[i].surface;
		buf[17] = colmodel->spheres[i].piece;
		buf[19] = 0;
		buf += 20;
	}

	*(int32*)buf = 0;
	buf += 4;

	*(int32*)buf = colmodel->numBoxes;
	buf += 4;
	for(int i = 0; i < colmodel->numBoxes; i++){
//		*(CVector*)(buf+0) = *(CVector*)&colmodel->boxes[i].box.min;
		*(float*)(buf+0) = colmodel->boxes[i].box.min.x;
		*(float*)(buf+4) = colmodel->boxes[i].box.min.y;
		*(float*)(buf+8) = colmodel->boxes[i].box.min.z;
//		*(CVector*)(buf+12) = *(CVector*)&colmodel->boxes[i].box.max;
		*(float*)(buf+12) = colmodel->boxes[i].box.max.x;
		*(float*)(buf+16) = colmodel->boxes[i].box.max.y;
		*(float*)(buf+20) = colmodel->boxes[i].box.max.z;
		buf[24] = colmodel->boxes[i].surface;
		buf[25] = colmodel->boxes[i].piece;
		buf[27] = 0;
		buf += 28;
	}

	*(int32*)buf = numVertices;
	buf += 4;
	for(int i = 0; i < numVertices; i++){
//		*(CVector*)buf = colmodel->vertices[i].Uncompress();
		CVector v = colmodel->vertices[i].Uncompress();
		*(float*)(buf+0) = v.x;
		*(float*)(buf+4) = v.y;
		*(float*)(buf+8) = v.z;
		buf += 12;
	}

	*(int32*)buf = colmodel->numTriangles;
	buf += 4;
	for(int i = 0; i < colmodel->numTriangles; i++){
		*(int32*)(buf+0) = colmodel->triangles[i].a/6;
		*(int32*)(buf+4) = colmodel->triangles[i].b/6;
		*(int32*)(buf+8) = colmodel->triangles[i].c/6;
		buf[12] = colmodel->triangles[i].surface;
		buf[15] = 0;
		buf += 16;
	}
	assert(buf == end);
	return size;
}

void
LoadCollisionFile(int id, uint8 *data)
{
	struct ColChkEntry {
		int32 id;
		void *colmodel;
	} *col;

	uint8 *chunk;
	sChunkHeader *header = (sChunkHeader*)data;
	if(header->ident != COL2_IDENT)
		return;
	chunk = rwNewT(uint8, header->fileSize, 0);
	memcpy(chunk, data, header->fileSize);
	cRelocatableChunk(header->ident, header->shrink).Fixup(chunk);

	pColPool->items[id].chunkData = chunk;
	CBaseModelInfo *mi;
	for(col = (ColChkEntry*)(chunk + sizeof(sChunkHeader));
	    col->id >= 0;
	    col++){
		mi = CModelInfo::Get(col->id);
		assert(mi);
		mi->colModel = (CColModel*)col->colmodel;

		// The ID is a bit strange here. Probably has to do
		// with island01-03 merging.
		// Interestingly it does not seem to match the actual colstore id
		//mi->colModel->colStoreId = id;
	}
}

static const rw::RGBA red = { 255, 0, 0, 255 };
static const rw::RGBA green = { 0, 255, 0, 255 };
static const rw::RGBA blue = { 0, 0, 255, 255 };
static const rw::RGBA magenta = { 255, 0, 255, 255 };
static const rw::RGBA white = { 255, 255, 255, 255 };

void
RenderColBoundingSphere(CColModel *col, rw::Matrix *xform)
{
	RenderWireSphere(&col->boundingSphere, red, xform);
}

void
RenderColModelWire(CColModel *col, rw::Matrix *xform, bool onlyBounds)
{
	int i;
	CColTriangle *tri;

	RenderWireBox(&col->boundingBox, red, xform);
//	RenderWireSphere(&col->boundingSphere, red, xform);
	if(onlyBounds)
		return;
	for(i = 0; i < col->numBoxes; i++)
		RenderWireBox(&col->boxes[i].box, white, xform);
	for(i = 0; i < col->numSpheres; i++)
		RenderWireSphere(&col->spheres[i].sph, magenta, xform);
	for(i = 0; i < col->numTriangles; i++){
		tri = &col->triangles[i];
		rw::V3d v[3];
		v[0] = *(rw::V3d*)&col->vertices[tri->a/6].Uncompress();
		v[1] = *(rw::V3d*)&col->vertices[tri->b/6].Uncompress();
		v[2] = *(rw::V3d*)&col->vertices[tri->c/6].Uncompress();
		RenderWireTriangle(&v[0], &v[1], &v[2], green, xform);
	}
}

void
RenderColBoxSolid(CColModel *col, rw::Matrix *xform, rw::RGBA c)
{
	RenderSolidBox(&col->boundingBox, c, xform);
}

void
RenderColMeshSolid(CColModel *col, rw::Matrix *xform, rw::RGBA c)
{
	int i;
	CColTriangle *tri;

	for(i = 0; i < col->numBoxes; i++)
		RenderSolidBox(&col->boxes[i].box, c, xform);
	for(i = 0; i < col->numSpheres; i++)
		RenderSolidSphere(&col->spheres[i].sph, c, xform);
	for(i = 0; i < col->numTriangles; i++){
		tri = &col->triangles[i];
		rw::V3d v[3];
		v[0] = *(rw::V3d*)&col->vertices[tri->a/6].Uncompress();
		v[1] = *(rw::V3d*)&col->vertices[tri->b/6].Uncompress();
		v[2] = *(rw::V3d*)&col->vertices[tri->c/6].Uncompress();
		RenderSolidTriangle(&v[0], &v[1], &v[2], c, xform);
	}
}
