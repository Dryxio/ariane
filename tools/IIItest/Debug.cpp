#include "III.h"

//
// Debug the RW camera
//

enum Planes
{
	FARPLANE,
	NEARPLANE,
	RIGHTPLANE,
	TOPPLANE,
	LEFTPLANE,
	BOTTOMPLANE
};

// When looking along normal
// 0---1
// |   |
// 3---2
int corners[6][4] = {
	{ 5, 4, 7, 6 },	// far
	{ 0, 1, 2, 3 },	// near
	{ 1, 5, 6, 2 },	// right
	{ 4, 5, 1, 0 },	// top
	{ 4, 0, 3, 7 }, // left
	{ 3, 2, 6, 7 }, // bottom
};

rw::Camera debugCamState;

void
DrawDebugFrustum(void)
{
	int i;
	static rw::RWDEVICE::Im3DVertex vertices[8];
	static uint16 indices[36];
	static int draw = 0;
	static int face = 0;

	if(CPad::IsKeyJustDown('G')){
		debugCamState = *Scene.camera;
		draw = 1;
	}
	if(CPad::IsKeyJustDown('B'))
		face = (face+1) % 6;

	if(draw){
		rw::V3d *c = debugCamState.frustumCorners;
		rw::FrustumPlane *p = debugCamState.frustumPlanes;

		for(i = 0; i < 8; i++){
			vertices[i].setX(c[i].x);
			vertices[i].setY(c[i].y);
			vertices[i].setZ(c[i].z);
			vertices[i].setU(0.0f);
			vertices[i].setV(0.0f);
			vertices[i].setColor(255, 0, 0, 128);
		}

		i = 0;
		indices[i++] = corners[face][3];
		indices[i++] = corners[face][2];
		indices[i++] = corners[face][1];
		indices[i++] = corners[face][3];
		indices[i++] = corners[face][1];
		indices[i++] = corners[face][0];

		rw::SetRenderState(rw::VERTEXALPHA, 1);
		rw::SetRenderState(rw::FOGENABLE, 0);
		rw::SetRenderState(rw::ZTESTENABLE, 1);
		rw::SetRenderState(rw::ZWRITEENABLE, 1);
		rw::SetRenderStatePtr(rw::TEXTURERASTER, nil);

		rw::im3d::Transform(vertices, 8, nil, rw::im3d::EVERYTHING);
		rw::im3d::RenderIndexedPrimitive(rw::PRIMTYPETRILIST, indices, i);
		rw::im3d::End();

		// Visualize normal
		rw::V3d v1 = scale(
			add(add(c[corners[face][0]], c[corners[face][1]]), add(c[corners[face][2]], c[corners[face][3]])), 0.25f);
		rw::V3d v2 = add(v1, scale(p[face].plane.normal, 200.0f));

		vertices[0].setX(v1.x);
		vertices[0].setY(v1.y);
		vertices[0].setZ(v1.z);
		vertices[0].setU(0.0f);
		vertices[0].setV(0.0f);
		vertices[0].setColor(0, 0, 255, 255);
		vertices[1].setX(v2.x);
		vertices[1].setY(v2.y);
		vertices[1].setZ(v2.z);
		vertices[1].setU(0.0f);
		vertices[1].setV(0.0f);
		vertices[1].setColor(0, 0, 255, 255);
		indices[0] = 0;
		indices[1] = 1;
		rw::im3d::Transform(vertices, 2, nil, rw::im3d::EVERYTHING);
		rw::im3d::RenderIndexedPrimitive(rw::PRIMTYPELINELIST, indices, 2);
		rw::im3d::End();

		rw::Sphere sph;
		sph.center = Scene.camera->getFrame()->getLTM()->pos;
		sph.radius = 1.0f;
	}
}
