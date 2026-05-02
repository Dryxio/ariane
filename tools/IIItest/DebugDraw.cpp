#include "III.h"

#include "DebugDraw.h"

void
CDebugDraw::RenderAndEmptyRenderBuffer(void)
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

void
CDebugDraw::RenderOneLine(float x1, float y1, float z1, float x2, float y2, float z2, uint32 c1, uint32 c2)
{
	if(TempBufferVerticesStored+2 >= TEMPBUFFERVERTSIZE ||
	   TempBufferIndicesStored+2 >= TEMPBUFFERINDEXSIZE)
		RenderAndEmptyRenderBuffer();

	TempVertexBuffer[TempBufferVerticesStored + 0].setX(x1);
	TempVertexBuffer[TempBufferVerticesStored + 0].setY(y1);
	TempVertexBuffer[TempBufferVerticesStored + 0].setZ(z1);
	TempVertexBuffer[TempBufferVerticesStored + 0].setColor(c1, c1>>8, c1>>16, c1>>24);
	TempVertexBuffer[TempBufferVerticesStored + 1].setX(x2);
	TempVertexBuffer[TempBufferVerticesStored + 1].setY(y2);
	TempVertexBuffer[TempBufferVerticesStored + 1].setZ(z2);
	TempVertexBuffer[TempBufferVerticesStored + 1].setColor(c2, c2>>8, c2>>16, c2>>24);

	TempIndexBuffer[TempBufferIndicesStored++] = TempBufferVerticesStored++;
	TempIndexBuffer[TempBufferIndicesStored++] = TempBufferVerticesStored++;
}

void
CDebugDraw::RenderLine(const CVector &v1, const CVector &v2, rw::RGBA c1, rw::RGBA c2)
{
	uint32 c1_ = RWRGBAINT(c1.red, c1.green, c1.blue, c1.alpha);
	uint32 c2_ = RWRGBAINT(c2.red, c2.green, c2.blue, c2.alpha);
	RenderOneLine(v1.x, v1.y, v1.z, v2.x, v2.y, v2.z, c1_, c2_);
}

void
CDebugDraw::RenderWireBoxVerts(CVector *verts, rw::RGBA col)
{
	uint32 c = RWRGBAINT(col.red, col.green, col.blue, col.alpha);
	RenderOneLine(verts[0].x, verts[0].y, verts[0].z, verts[1].x, verts[1].y, verts[1].z, c, c);
	RenderOneLine(verts[1].x, verts[1].y, verts[1].z, verts[3].x, verts[3].y, verts[3].z, c, c);
	RenderOneLine(verts[3].x, verts[3].y, verts[3].z, verts[2].x, verts[2].y, verts[2].z, c, c);
	RenderOneLine(verts[2].x, verts[2].y, verts[2].z, verts[0].x, verts[0].y, verts[0].z, c, c);

	RenderOneLine(verts[0+4].x, verts[0+4].y, verts[0+4].z, verts[1+4].x, verts[1+4].y, verts[1+4].z, c, c);
	RenderOneLine(verts[1+4].x, verts[1+4].y, verts[1+4].z, verts[3+4].x, verts[3+4].y, verts[3+4].z, c, c);
	RenderOneLine(verts[3+4].x, verts[3+4].y, verts[3+4].z, verts[2+4].x, verts[2+4].y, verts[2+4].z, c, c);
	RenderOneLine(verts[2+4].x, verts[2+4].y, verts[2+4].z, verts[0+4].x, verts[0+4].y, verts[0+4].z, c, c);

	RenderOneLine(verts[0].x, verts[0].y, verts[0].z, verts[4].x, verts[4].y, verts[4].z, c, c);
	RenderOneLine(verts[1].x, verts[1].y, verts[1].z, verts[5].x, verts[5].y, verts[5].z, c, c);
	RenderOneLine(verts[2].x, verts[2].y, verts[2].z, verts[6].x, verts[6].y, verts[6].z, c, c);
	RenderOneLine(verts[3].x, verts[3].y, verts[3].z, verts[7].x, verts[7].y, verts[7].z, c, c);
}

void
CDebugDraw::RenderWireBox(const CMatrix &mat, const CVector &min, const CVector &max, rw::RGBA col)
{
	CVector verts[8];
	verts[0].x = min.x;
	verts[0].y = min.y;
	verts[0].z = min.z;
	verts[1].x = max.x;
	verts[1].y = min.y;
	verts[1].z = min.z;
	verts[2].x = min.x;
	verts[2].y = max.y;
	verts[2].z = min.z;
	verts[3].x = max.x;
	verts[3].y = max.y;
	verts[3].z = min.z;
	verts[4].x = min.x;
	verts[4].y = min.y;
	verts[4].z = max.z;
	verts[5].x = max.x;
	verts[5].y = min.y;
	verts[5].z = max.z;
	verts[6].x = min.x;
	verts[6].y = max.y;
	verts[6].z = max.z;
	verts[7].x = max.x;
	verts[7].y = max.y;
	verts[7].z = max.z;
	rw::V3d::transformPoints((rw::V3d*)verts, (rw::V3d*)verts, 8, &mat.m_matrix);
	RenderWireBoxVerts(verts, col);
}

void
CDebugDraw::RenderWireSphere(const CVector &center, float radius, rw::RGBA col)
{
	uint32 c = RWRGBAINT(col.red, col.green, col.blue, col.alpha);
	RenderOneLine(center.x, center.y, center.z - radius, center.x - radius, center.y - radius, center.z, c, c);
	RenderOneLine(center.x, center.y, center.z - radius, center.x - radius, center.y + radius, center.z, c, c);
	RenderOneLine(center.x, center.y, center.z - radius, center.x + radius, center.y - radius, center.z, c, c);
	RenderOneLine(center.x, center.y, center.z - radius, center.x + radius, center.y + radius, center.z, c, c);
	RenderOneLine(center.x - radius, center.y - radius, center.z, center.x, center.y, center.z + radius, c, c);
	RenderOneLine(center.x - radius, center.y + radius, center.z, center.x, center.y, center.z + radius, c, c);
	RenderOneLine(center.x + radius, center.y - radius, center.z, center.x, center.y, center.z + radius, c, c);
	RenderOneLine(center.x + radius, center.y + radius, center.z, center.x, center.y, center.z + radius, c, c);
}

void
CDebugDraw::RenderWireTri(CVector *verts, rw::RGBA col)
{
	uint32 c = RWRGBAINT(col.red, col.green, col.blue, col.alpha);
	RenderOneLine(verts[0].x, verts[0].y, verts[0].z, verts[1].x, verts[1].y, verts[1].z, c, c);
	RenderOneLine(verts[0].x, verts[0].y, verts[0].z, verts[2].x, verts[2].y, verts[2].z, c, c);
	RenderOneLine(verts[2].x, verts[2].y, verts[2].z, verts[1].x, verts[1].y, verts[1].z, c, c);
}

void
CDebugDraw::RenderWireTri(CVector *verts, int a, int b, int c, rw::RGBA col)
{
	RenderLine(verts[a], verts[b], col, col);
	RenderLine(verts[a], verts[c], col, col);
	RenderLine(verts[c], verts[b], col, col);
}
