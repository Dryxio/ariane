#include "storiesview.h"



struct DebugLine
{
	rw::V3d v1;
	rw::V3d v2;
	rw::RGBA col1;
	rw::RGBA col2;
};
#define MAXDEBUGLINES 40000
static DebugLine debugLines[MAXDEBUGLINES];
static int numDebugLines;

#define TEMPBUFFERVERTSIZE 256
#define TEMPBUFFERINDEXSIZE 1024
static int TempBufferIndicesStored;
static int TempBufferVerticesStored;
static rw::RWDEVICE::Im3DVertex TempVertexBuffer[TEMPBUFFERVERTSIZE];
static uint16 TempIndexBuffer[TEMPBUFFERINDEXSIZE];

struct DebugTri
{
	rw::V3d v1;
	rw::V3d v2;
	rw::V3d v3;
	rw::RGBA col;
};
#define MAXDEBUGTRIS 40000
static DebugTri debugTris[MAXDEBUGTRIS];
static int numDebugTris;

static int TempTriBufferIndicesStored;
static int TempTriBufferVerticesStored;
static rw::RWDEVICE::Im3DVertex TempTriVertexBuffer[TEMPBUFFERVERTSIZE];
static uint16 TempTriIndexBuffer[TEMPBUFFERINDEXSIZE];

static void
RenderAndEmptyRenderBuffer(void)
{
	assert(TempBufferVerticesStored <= TEMPBUFFERVERTSIZE);
	assert(TempBufferIndicesStored <= TEMPBUFFERINDEXSIZE);
	if(TempBufferVerticesStored){
		rw::im3d::Transform(TempVertexBuffer, TempBufferVerticesStored, nil, rw::im3d::EVERYTHING);
		rw::im3d::RenderIndexedPrimitive(rw::PRIMTYPELINELIST, TempIndexBuffer, TempBufferIndicesStored);
		rw::im3d::End();
	}
	TempBufferVerticesStored = 0;
	TempBufferIndicesStored = 0;
}

static void
RenderAndEmptyTriRenderBuffer(void)
{
	assert(TempTriBufferVerticesStored <= TEMPBUFFERVERTSIZE);
	assert(TempTriBufferIndicesStored <= TEMPBUFFERINDEXSIZE);
	if(TempTriBufferVerticesStored){
		rw::im3d::Transform(TempTriVertexBuffer, TempTriBufferVerticesStored, nil, rw::im3d::EVERYTHING);
		rw::im3d::RenderIndexedPrimitive(rw::PRIMTYPETRILIST, TempTriIndexBuffer, TempTriBufferIndicesStored);
		rw::im3d::End();
	}
	TempTriBufferVerticesStored = 0;
	TempTriBufferIndicesStored = 0;
}

static void
RenderLine(float x1, float y1, float z1, float x2, float y2, float z2, rw::RGBA c1, rw::RGBA c2)
{
	int i;
	if(TempBufferVerticesStored+2 >= TEMPBUFFERVERTSIZE ||
	   TempBufferIndicesStored+2 >= TEMPBUFFERINDEXSIZE)
		RenderAndEmptyRenderBuffer();

	i = TempBufferVerticesStored;
	TempVertexBuffer[i].setX(x1);
	TempVertexBuffer[i].setY(y1);
	TempVertexBuffer[i].setZ(z1);
	TempVertexBuffer[i].setColor(c1.red, c1.green, c1.blue, c1.alpha);
	TempVertexBuffer[i+1].setX(x2);
	TempVertexBuffer[i+1].setY(y2);
	TempVertexBuffer[i+1].setZ(z2);
	TempVertexBuffer[i+1].setColor(c2.red, c2.green, c2.blue, c2.alpha);
	TempBufferVerticesStored += 2;

	TempIndexBuffer[TempBufferIndicesStored++] = i;
	TempIndexBuffer[TempBufferIndicesStored++] = i+1;
}

void
RenderDebugLines(void)
{
	int i;

	rw::SetRenderStatePtr(rw::TEXTURERASTER, nil);

	for(i = 0; i < numDebugLines; i++)
		RenderLine(debugLines[i].v1.x, debugLines[i].v1.y, debugLines[i].v1.z,
			debugLines[i].v2.x, debugLines[i].v2.y, debugLines[i].v2.z,
			debugLines[i].col1, debugLines[i].col2);
	RenderAndEmptyRenderBuffer();
	numDebugLines = 0;
}

static void
AddDebugLine(float x1, float y1, float z1, float x2, float y2, float z2, rw::RGBA c1, rw::RGBA c2)
{
	int n;
	if(numDebugLines >= MAXDEBUGLINES)
		return;
	n = numDebugLines++;
	debugLines[n].v1.x = x1;
	debugLines[n].v1.y = y1;
	debugLines[n].v1.z = z1;
	debugLines[n].v2.x = x2;
	debugLines[n].v2.y = y2;
	debugLines[n].v2.z = z2;
	debugLines[n].col1 = c1;
	debugLines[n].col2 = c2;
}

void RenderLine(rw::V3d v1, rw::V3d v2, rw::RGBA c1, rw::RGBA c2) { AddDebugLine(v1.x, v1.y, v1.z, v2.x, v2.y, v2.z, c1, c2); }


static void
RenderTriangle(float x1, float y1, float z1, float x2, float y2, float z2, float x3, float y3, float z3, rw::RGBA c)
{
	int i;
	if(TempTriBufferVerticesStored+3 >= TEMPBUFFERVERTSIZE ||
	   TempTriBufferIndicesStored+3 >= TEMPBUFFERINDEXSIZE)
		RenderAndEmptyTriRenderBuffer();

	i = TempTriBufferVerticesStored;
	TempTriVertexBuffer[i].setX(x1);
	TempTriVertexBuffer[i].setY(y1);
	TempTriVertexBuffer[i].setZ(z1);
	TempTriVertexBuffer[i].setColor(c.red, c.green, c.blue, c.alpha);
	TempTriVertexBuffer[i+1].setX(x2);
	TempTriVertexBuffer[i+1].setY(y2);
	TempTriVertexBuffer[i+1].setZ(z2);
	TempTriVertexBuffer[i+1].setColor(c.red, c.green, c.blue, c.alpha);
	TempTriVertexBuffer[i+2].setX(x3);
	TempTriVertexBuffer[i+2].setY(y3);
	TempTriVertexBuffer[i+2].setZ(z3);
	TempTriVertexBuffer[i+2].setColor(c.red, c.green, c.blue, c.alpha);
	TempTriBufferVerticesStored += 3;

	TempTriIndexBuffer[TempTriBufferIndicesStored++] = i;
	TempTriIndexBuffer[TempTriBufferIndicesStored++] = i+1;
	TempTriIndexBuffer[TempTriBufferIndicesStored++] = i+2;
}

void
RenderDebugTris(void)
{
	int i;

	rw::SetRenderStatePtr(rw::TEXTURERASTER, nil);

	for(i = 0; i < numDebugTris; i++)
		RenderTriangle(debugTris[i].v1.x, debugTris[i].v1.y, debugTris[i].v1.z,
			debugTris[i].v2.x, debugTris[i].v2.y, debugTris[i].v2.z,
			debugTris[i].v3.x, debugTris[i].v3.y, debugTris[i].v3.z,
			debugTris[i].col);
	RenderAndEmptyTriRenderBuffer();
	numDebugTris = 0;
}

void
AddDebugTri(rw::V3d v1, rw::V3d v2, rw::V3d v3, rw::RGBA col)
{
	int n;
	if(numDebugTris >= MAXDEBUGTRIS)
		return;
	n = numDebugTris++;
	debugTris[n].v1 = v1;
	debugTris[n].v2 = v2;
	debugTris[n].v3 = v3;
	debugTris[n].col = col;
}

void
RenderWireBoxVerts(rw::V3d *verts, rw::RGBA col)
{
	RenderLine(verts[0], verts[1], col, col);
	RenderLine(verts[1], verts[3], col, col);
	RenderLine(verts[3], verts[2], col, col);
	RenderLine(verts[2], verts[0], col, col);

	RenderLine(verts[0+4], verts[1+4], col, col);
	RenderLine(verts[1+4], verts[3+4], col, col);
	RenderLine(verts[3+4], verts[2+4], col, col);
	RenderLine(verts[2+4], verts[0+4], col, col);

	RenderLine(verts[0], verts[4], col, col);
	RenderLine(verts[1], verts[5], col, col);
	RenderLine(verts[2], verts[6], col, col);
	RenderLine(verts[3], verts[7], col, col);
}

void
RenderSolidBoxVerts(rw::V3d *verts, rw::RGBA col)
{
	// ignore winding
	// bottom
	AddDebugTri(verts[0], verts[1], verts[2], col);
	AddDebugTri(verts[2], verts[1], verts[3], col);
	// top
	AddDebugTri(verts[4], verts[5], verts[6], col);
	AddDebugTri(verts[6], verts[5], verts[7], col);
	// front
	AddDebugTri(verts[0], verts[1], verts[4], col);
	AddDebugTri(verts[4], verts[1], verts[5], col);
	// back
	AddDebugTri(verts[2], verts[3], verts[6], col);
	AddDebugTri(verts[6], verts[3], verts[7], col);
	// left
	AddDebugTri(verts[2], verts[0], verts[6], col);
	AddDebugTri(verts[6], verts[0], verts[4], col);
	// right
	AddDebugTri(verts[3], verts[1], verts[7], col);
	AddDebugTri(verts[7], verts[1], verts[5], col);
}

void
RenderWireBox(CBox *box, rw::RGBA col, rw::Matrix *xform)
{
	rw::V3d verts[8];
	verts[0].x = box->min.x;
	verts[0].y = box->min.y;
	verts[0].z = box->min.z;
	verts[1].x = box->max.x;
	verts[1].y = box->min.y;
	verts[1].z = box->min.z;
	verts[2].x = box->min.x;
	verts[2].y = box->max.y;
	verts[2].z = box->min.z;
	verts[3].x = box->max.x;
	verts[3].y = box->max.y;
	verts[3].z = box->min.z;
	verts[4].x = box->min.x;
	verts[4].y = box->min.y;
	verts[4].z = box->max.z;
	verts[5].x = box->max.x;
	verts[5].y = box->min.y;
	verts[5].z = box->max.z;
	verts[6].x = box->min.x;
	verts[6].y = box->max.y;
	verts[6].z = box->max.z;
	verts[7].x = box->max.x;
	verts[7].y = box->max.y;
	verts[7].z = box->max.z;
	rw::V3d::transformPoints(verts, verts, 8, xform);

	RenderWireBoxVerts(verts, col);
}

void
RenderSolidBox(CBox *box, rw::RGBA col, rw::Matrix *xform)
{
	rw::V3d verts[8];
	verts[0].x = box->min.x;
	verts[0].y = box->min.y;
	verts[0].z = box->min.z;
	verts[1].x = box->max.x;
	verts[1].y = box->min.y;
	verts[1].z = box->min.z;
	verts[2].x = box->min.x;
	verts[2].y = box->max.y;
	verts[2].z = box->min.z;
	verts[3].x = box->max.x;
	verts[3].y = box->max.y;
	verts[3].z = box->min.z;
	verts[4].x = box->min.x;
	verts[4].y = box->min.y;
	verts[4].z = box->max.z;
	verts[5].x = box->max.x;
	verts[5].y = box->min.y;
	verts[5].z = box->max.z;
	verts[6].x = box->min.x;
	verts[6].y = box->max.y;
	verts[6].z = box->max.z;
	verts[7].x = box->max.x;
	verts[7].y = box->max.y;
	verts[7].z = box->max.z;
	rw::V3d::transformPoints(verts, verts, 8, xform);

	RenderSolidBoxVerts(verts, col);
}

void
RenderWireSphere(CSphere *sphere, rw::RGBA col, rw::Matrix *xform)
{
	rw::V3d c;
	rw::V3d verts[6];
	rw::V3d::transformPoints(&c, (rw::V3d*)&sphere->center, 1, xform);
	verts[0] = verts[1] = verts[2] = verts[3] = verts[4] = verts[5] = c;
	verts[0].z += sphere->radius;	// top
	verts[1].z -= sphere->radius;	// bottom
	verts[2].x += sphere->radius;
	verts[3].x -= sphere->radius;
	verts[4].y += sphere->radius;
	verts[5].y -= sphere->radius;

	RenderLine(verts[0], verts[2], col, col);
	RenderLine(verts[0], verts[3], col, col);
	RenderLine(verts[0], verts[4], col, col);
	RenderLine(verts[0], verts[5], col, col);
	RenderLine(verts[1], verts[2], col, col);
	RenderLine(verts[1], verts[3], col, col);
	RenderLine(verts[1], verts[4], col, col);
	RenderLine(verts[1], verts[5], col, col);
}

void
RenderSolidSphere(CSphere *sphere, rw::RGBA col, rw::Matrix *xform)
{
	rw::V3d c;
	rw::V3d verts[6];
	rw::V3d::transformPoints(&c, (rw::V3d*)&sphere->center, 1, xform);
	verts[0] = verts[1] = verts[2] = verts[3] = verts[4] = verts[5] = c;
	verts[0].z += sphere->radius;	// top
	verts[1].z -= sphere->radius;	// bottom
	verts[2].x += sphere->radius;
	verts[3].x -= sphere->radius;
	verts[4].y += sphere->radius;
	verts[5].y -= sphere->radius;

	AddDebugTri(verts[0], verts[2], verts[4], col);
	AddDebugTri(verts[0], verts[4], verts[3], col);
	AddDebugTri(verts[0], verts[3], verts[5], col);
	AddDebugTri(verts[0], verts[5], verts[2], col);
	AddDebugTri(verts[1], verts[2], verts[4], col);
	AddDebugTri(verts[1], verts[4], verts[3], col);
	AddDebugTri(verts[1], verts[3], verts[5], col);
	AddDebugTri(verts[1], verts[5], verts[2], col);
}

void
RenderWireTriangle(rw::V3d *v1, rw::V3d *v2, rw::V3d *v3, rw::RGBA col, rw::Matrix *xform)
{
	rw::V3d verts[3];
	verts[0] = *v1;
	verts[1] = *v2;
	verts[2] = *v3;
	rw::V3d::transformPoints(verts, verts, 3, xform);
	RenderLine(verts[0], verts[1], col, col);
	RenderLine(verts[1], verts[2], col, col);
	RenderLine(verts[2], verts[0], col, col);
}

void
RenderSolidTriangle(rw::V3d *v1, rw::V3d *v2, rw::V3d *v3, rw::RGBA col, rw::Matrix *xform)
{
	rw::V3d verts[3];
	verts[0] = *v1;
	verts[1] = *v2;
	verts[2] = *v3;
	rw::V3d::transformPoints(verts, verts, 3, xform);
	AddDebugTri(verts[0], verts[1], verts[2], col);
}

void
RenderAxesWidget(rw::V3d pos, rw::V3d x, rw::V3d y, rw::V3d z)
{
	rw::RGBA red = { 255, 0, 0, 255 };
	rw::RGBA green = { 0, 255, 0, 255 };
	rw::RGBA blue = { 0, 0, 255, 255 };
	RenderLine(pos, add(pos, x), red, red);
	RenderLine(pos, add(pos, y), green, green);
	RenderLine(pos, add(pos, z), blue, blue);
}
