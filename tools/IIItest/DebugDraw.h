#ifndef _DEBUGDRAW_H_
#define _DEBUGDRAW_H_

class CDebugDraw
{
public:
	// more like an internal function
	static void RenderOneLine(float x1, float y1, float z1, float x2, float y2, float z2, uint32 c1, uint32 c2);

	static void RenderLine(const CVector &v1, const CVector &v2, rw::RGBA c1, rw::RGBA c2);
	static void RenderWireBoxVerts(CVector *verts, rw::RGBA col);
	static void RenderWireBox(const CMatrix &mat, const CVector &min, const CVector &max, rw::RGBA col);
	static void RenderWireSphere(const CVector &center, float radius, rw::RGBA col);
	static void RenderWireTri(CVector *verts, rw::RGBA col);
	static void RenderWireTri(CVector *verts, int a, int b, int c, rw::RGBA col);
	static void RenderAndEmptyRenderBuffer(void);
};


#endif
