#include "euryopa.h"

rw::RWDEVICE::Im2DVertex Sprite2d::ma_vertices[4];

// Arguments:
// 2---3
// |   |
// 0---1
void
Sprite2d::SetVertices(CRect const &r, CRGBA const &c0, CRGBA const &c1, CRGBA const &c2, CRGBA const &c3, bool far_)
{
	float screenz, z, recipz;

	if(far_){
		screenz = rw::im2d::GetFarZ();
		z = Scene.camera->farPlane;
	}else{
		screenz = rw::im2d::GetNearZ();
		z = Scene.camera->nearPlane;
	}
	recipz = 1.0f/z;

	// This is what we draw:
	// 0---1
	// | / |
	// 3---2
	ma_vertices[0].setScreenX(r.left);
	ma_vertices[0].setScreenY(r.bottom);
	ma_vertices[0].setScreenZ(screenz);
	ma_vertices[0].setCameraZ(z);
	ma_vertices[0].setRecipCameraZ(recipz);
	ma_vertices[0].setColor(c2.r, c2.g, c2.b, c2.a);
	ma_vertices[0].setU(0.0f, recipz);
	ma_vertices[0].setV(0.0f, recipz);

	ma_vertices[1].setScreenX(r.right);
	ma_vertices[1].setScreenY(r.bottom);
	ma_vertices[1].setScreenZ(screenz);
	ma_vertices[1].setCameraZ(z);
	ma_vertices[1].setRecipCameraZ(recipz);
	ma_vertices[1].setColor(c3.r, c3.g, c3.b, c3.a);
	ma_vertices[1].setU(1.0f, recipz);
	ma_vertices[1].setV(0.0f, recipz);

	ma_vertices[2].setScreenX(r.right);
	ma_vertices[2].setScreenY(r.top);
	ma_vertices[2].setScreenZ(screenz);
	ma_vertices[2].setCameraZ(z);
	ma_vertices[2].setRecipCameraZ(recipz);
	ma_vertices[2].setColor(c1.r, c1.g, c1.b, c1.a);
	ma_vertices[2].setU(1.0f, recipz);
	ma_vertices[2].setV(1.0f, recipz);

	ma_vertices[3].setScreenX(r.left);
	ma_vertices[3].setScreenY(r.top);
	ma_vertices[3].setScreenZ(screenz);
	ma_vertices[3].setCameraZ(z);
	ma_vertices[3].setRecipCameraZ(recipz);
	ma_vertices[3].setColor(c0.r, c0.g, c0.b, c0.a);
	ma_vertices[3].setU(0.0f, recipz);
	ma_vertices[3].setV(1.0f, recipz);
}

void
Sprite2d::DrawRect(CRect const &r, CRGBA const &c0, CRGBA const &c1, CRGBA const &c2, CRGBA const &c3)
{
	static short quadindices[] = { 0, 1, 2, 0, 2, 3 };
	SetVertices(r, c0, c1, c2, c3, false);
	rw::SetRenderStatePtr(rw::TEXTURERASTER, nil);
	rw::SetRenderState(rw::VERTEXALPHA, 0);
	rw::SetRenderState(rw::ZTESTENABLE, 0);
	rw::SetRenderState(rw::ZWRITEENABLE, 0);
	rw::im2d::RenderIndexedPrimitive(rw::PRIMTYPETRILIST,
		&ma_vertices, 4, &quadindices, 6);
	rw::SetRenderState(rw::ZTESTENABLE, 1);
	rw::SetRenderState(rw::ZWRITEENABLE, 1);
}

void
Sprite2d::DrawRectXLU(CRect const &r, CRGBA const &c0, CRGBA const &c1, CRGBA const &c2, CRGBA const &c3)
{
	static short quadindices[] = { 0, 1, 2, 0, 2, 3 };
	SetVertices(r, c0, c1, c2, c3, false);
	rw::SetRenderStatePtr(rw::TEXTURERASTER, nil);
	rw::SetRenderState(rw::VERTEXALPHA, 1);
	rw::SetRenderState(rw::ZTESTENABLE, 0);
	rw::SetRenderState(rw::ZWRITEENABLE, 0);
	rw::im2d::RenderIndexedPrimitive(rw::PRIMTYPETRILIST,
		&ma_vertices, 4, &quadindices, 6);
	rw::SetRenderState(rw::ZTESTENABLE, 1);
	rw::SetRenderState(rw::ZWRITEENABLE, 1);
}




float Sprite::m_f2DNearScreenZ;
float Sprite::m_f2DFarScreenZ;
int Sprite::m_bFlushSpriteBufferSwitchZTest;

float 
Sprite::CalcHorizonCoors(void)
{
	rw::V3d pos = TheCamera.m_position;
	pos.z = 0.0f;
	pos.x += 3000.0f * TheCamera.m_at.x;
	pos.y += 3000.0f * TheCamera.m_at.y;
	rw::V3d::transformPoints(&pos, &pos, 1, &TheCamera.m_rwcam->viewMatrix);
	return pos.y * sk::globals.height / pos.z;
}

bool 
Sprite::CalcScreenCoors(const rw::V3d &in, rw::V3d *out, float *outw, float *outh, bool farclip)
{
	rw::V3d viewvec;

	rw::V3d::transformPoints(&viewvec, &in, 1, &TheCamera.m_rwcam->viewMatrix);
	*out = viewvec;
	if(out->z <= TheCamera.m_rwcam->nearPlane + 1.0f) return false;
	if(out->z >= TheCamera.m_rwcam->farPlane && farclip) return false;
	float recip = 1.0f/out->z;
	out->x *= sk::globals.width * recip;
	out->y *= sk::globals.height * recip;
	// What is this? size?
	*outw = 70.0f/TheCamera.m_fov;
	*outh = 70.0f/TheCamera.m_fov;
	*outw *= sk::globals.width * recip;
	*outh *= sk::globals.height * recip;
	return true;
}

float
Sprite::CalcScreenZ(float z)
{
	float camNearZ = TheCamera.m_rwcam->nearPlane;
	float camFarZ = TheCamera.m_rwcam->farPlane;
	// (DrawZ - DrawNear)/(DrawFar - DrawNear) = (SpriteZ-SpriteNear)/(SpriteFar-SpriteNear)
	// So to calculate SpriteZ:
	return rw::im2d::GetNearZ() +
		(z-camNearZ)*(rw::im2d::GetFarZ()-rw::im2d::GetNearZ())*camFarZ /
		((camFarZ-camNearZ)*z);
}

#define SPRITEBUFFERSIZE 64
static int nSpriteBufferIndex;
static rw::RWDEVICE::Im2DVertex SpriteBufferVerts[SPRITEBUFFERSIZE*6];
static rw::RWDEVICE::Im2DVertex verts[4];
static short indices[nelem(SpriteBufferVerts)];

void
Sprite::InitSpriteBuffer(void)
{
	m_f2DNearScreenZ = rw::im2d::GetNearZ();
	m_f2DFarScreenZ = rw::im2d::GetFarZ();

	// TODO: don't have to do this every frame, indices don't change
	// GTA doesn't draw indexed but we can't do that right now
	int i;
	for(i = 0; i < nelem(SpriteBufferVerts); i++)
		indices[i] = i;
}

void
Sprite::FlushSpriteBuffer(void)
{
	if(nSpriteBufferIndex > 0){
		if(m_bFlushSpriteBufferSwitchZTest){
			rw::SetRenderState(rw::ZTESTENABLE, 0);
			rw::im2d::RenderIndexedPrimitive(rw::PRIMTYPETRILIST, &SpriteBufferVerts, nSpriteBufferIndex*6, &indices, nSpriteBufferIndex*6);
			rw::SetRenderState(rw::ZTESTENABLE, 1);
		}else
			rw::im2d::RenderIndexedPrimitive(rw::PRIMTYPETRILIST, &SpriteBufferVerts, nSpriteBufferIndex*6, &indices, nSpriteBufferIndex*6);
		nSpriteBufferIndex = 0;
	}
}

void
Sprite::RenderOneXLUSprite(float x, float y, float z, float w, float h, uint8 r, uint8 g, uint8 b, int16 intens, float recipz, uint8 a)
{
	static short indices[] = { 0, 1, 2, 3 };
	// 0---3
	// |   |
	// 1---2
	float xs[4];
	float ys[4];
	float us[4];
	float vs[4];
	int i;

	xs[0] = x-w;	us[0] = 0.0f;
	xs[1] = x-w;	us[1] = 0.0f;
	xs[2] = x+w;	us[2] = 1.0f;
	xs[3] = x+w;	us[3] = 1.0f;

	ys[0] = y-h;	vs[0] = 0.0f;
	ys[1] = y+h;	vs[1] = 1.0f;
	ys[2] = y+h;	vs[2] = 1.0f;
	ys[3] = y-h;	vs[3] = 0.0f;

	// clip
	for(i = 0; i < 4; i++){
		if(xs[i] < 0.0f){
			us[i] = -xs[i] / (2.0f*w);
			xs[i] = 0.0f;
		}
		if(xs[i] > sk::globals.width){
			us[i] = 1.0f - (xs[i]-sk::globals.width) / (2.0f*w);
			xs[i] = sk::globals.width;
		}
		if(ys[i] < 0.0f){
			vs[i] = -ys[i] / (2.0f*h);
			ys[i] = 0.0f;
		}
		if(ys[i] > sk::globals.height){
			vs[i] = 1.0f - (ys[i]-sk::globals.height) / (2.0f*h);
			ys[i] = sk::globals.height;
		}
	}

	float screenz = CalcScreenZ(z);

	for(i = 0; i < 4; i++){
		verts[i].setScreenX(xs[i]);
		verts[i].setScreenY(ys[i]);
		verts[i].setScreenZ(screenz);
		verts[i].setCameraZ(z);
		verts[i].setRecipCameraZ(recipz);
		verts[i].setU(us[i], recipz);
		verts[i].setV(vs[i], recipz);
		verts[i].setColor(r*intens>>8, g*intens>>8, b*intens>>8, a);
	}
	rw::im2d::RenderIndexedPrimitive(rw::PRIMTYPETRIFAN,
		&verts, 4, &indices, 4);
}

void
Sprite::RenderBufferedOneXLUSprite(float x, float y, float z, float w, float h, uint8 r, uint8 g, uint8 b, int16 intens, float recipz, uint8 a)
{
	m_bFlushSpriteBufferSwitchZTest = 0;

	// 0---3
	// |   |
	// 1---2
	float xs[4];
	float ys[4];
	float us[4];
	float vs[4];
	int i;

	xs[0] = x-w;	us[0] = 0.0f;
	xs[1] = x-w;	us[1] = 0.0f;
	xs[2] = x+w;	us[2] = 1.0f;
	xs[3] = x+w;	us[3] = 1.0f;

	ys[0] = y-h;	vs[0] = 0.0f;
	ys[1] = y+h;	vs[1] = 1.0f;
	ys[2] = y+h;	vs[2] = 1.0f;
	ys[3] = y-h;	vs[3] = 0.0f;

	// clip
	for(i = 0; i < 4; i++){
		if(xs[i] < 0.0f){
			us[i] = -xs[i] / (2.0f*w);
			xs[i] = 0.0f;
		}
		if(xs[i] > sk::globals.width){
			us[i] = 1.0f - (xs[i]-sk::globals.width) / (2.0f*w);
			xs[i] = sk::globals.width;
		}
		if(ys[i] < 0.0f){
			vs[i] = -ys[i] / (2.0f*h);
			ys[i] = 0.0f;
		}
		if(ys[i] > sk::globals.height){
			vs[i] = 1.0f - (ys[i]-sk::globals.height) / (2.0f*h);
			ys[i] = sk::globals.height;
		}
	}

	float screenz = CalcScreenZ(z);

	rw::RWDEVICE::Im2DVertex *vert = &SpriteBufferVerts[nSpriteBufferIndex*6];
	static int indices[6] = { 0, 1, 2, 3, 0, 2 };
	for(i = 0; i < 6; i++){
		vert[i].setScreenX(xs[indices[i]]);
		vert[i].setScreenY(ys[indices[i]]);
		vert[i].setScreenZ(screenz);
		vert[i].setCameraZ(z);
		vert[i].setRecipCameraZ(recipz);
		vert[i].setU(us[indices[i]], recipz);
		vert[i].setV(vs[indices[i]], recipz);
		vert[i].setColor(r*intens>>8, g*intens>>8, b*intens>>8, a);
	}
	nSpriteBufferIndex++;
	if(nSpriteBufferIndex >= SPRITEBUFFERSIZE)
		FlushSpriteBuffer();
}

void
Sprite::RenderBufferedOneXLUSprite_Rotate_Dimension(float x, float y, float z, float w, float h, uint8 r, uint8 g, uint8 b, int16 intens, float recipz, float rotation, uint8 a)
{
	m_bFlushSpriteBufferSwitchZTest = 0;
	// TODO: replace with lookup
	float c = cos(rotation);
	float s = sin(rotation);

	float xs[4];
	float ys[4];
	float us[4];
	float vs[4];
	int i;

	xs[0] = x - c*w - s*h;	us[0] = 0.0f;
	xs[1] = x - c*w + s*h;	us[1] = 0.0f;
	xs[2] = x + c*w + s*h;	us[2] = 1.0f;
	xs[3] = x + c*w - s*h;	us[3] = 1.0f;

	ys[0] = y - c*h + s*w;	vs[0] = 0.0f;
	ys[1] = y + c*h + s*w;	vs[1] = 1.0f;
	ys[2] = y + c*h - s*w;	vs[2] = 1.0f;
	ys[3] = y - c*h - s*w;	vs[3] = 0.0f;

	// No clipping, just culling
	if(xs[0] < 0.0f && xs[1] < 0.0f && xs[2] < 0.0f && xs[3] < 0.0f) return;
	if(ys[0] < 0.0f && ys[1] < 0.0f && ys[2] < 0.0f && ys[3] < 0.0f) return;
	if(xs[0] > sk::globals.width && xs[1] > sk::globals.width && xs[2] > sk::globals.width && xs[3] > sk::globals.width) return;
	if(ys[0] > sk::globals.height && ys[1] > sk::globals.height && ys[2] > sk::globals.height && ys[3] > sk::globals.height) return;

	float screenz = CalcScreenZ(z);

	rw::RWDEVICE::Im2DVertex *vert = &SpriteBufferVerts[nSpriteBufferIndex*6];
	static int indices[6] = { 0, 1, 2, 3, 0, 2 };
	for(i = 0; i < 6; i++){
		vert[i].setScreenX(xs[indices[i]]);
		vert[i].setScreenY(ys[indices[i]]);
		vert[i].setScreenZ(screenz);
		vert[i].setCameraZ(z);
		vert[i].setRecipCameraZ(recipz);
		vert[i].setU(us[indices[i]], recipz);
		vert[i].setV(vs[indices[i]], recipz);
		vert[i].setColor(r*intens>>8, g*intens>>8, b*intens>>8, a);
	}
	nSpriteBufferIndex++;
	if(nSpriteBufferIndex >= SPRITEBUFFERSIZE)
		FlushSpriteBuffer();
}

void
Sprite::RenderBufferedOneXLUSprite_Rotate_Aspect(float x, float y, float z, float w, float h, uint8 r, uint8 g, uint8 b, int16 intens, float recipz, float rotation, uint8 a)
{
	m_bFlushSpriteBufferSwitchZTest = 0;
	float c = cos(rotation);
	float s = sin(rotation);

	float xs[4];
	float ys[4];
	float us[4];
	float vs[4];
	int i;

	xs[0] = x + w*(-c-s);	us[0] = 0.0f;
	xs[1] = x + w*(-c+s);	us[1] = 0.0f;
	xs[2] = x + w*(+c+s);	us[2] = 1.0f;
	xs[3] = x + w*(+c-s);	us[3] = 1.0f;

	ys[0] = y + h*(-c+s);	vs[0] = 0.0f;
	ys[1] = y + h*(+c+s);	vs[1] = 1.0f;
	ys[2] = y + h*(+c-s);	vs[2] = 1.0f;
	ys[3] = y + h*(-c-s);	vs[3] = 0.0f;

	// No clipping, just culling
	if(xs[0] < 0.0f && xs[1] < 0.0f && xs[2] < 0.0f && xs[3] < 0.0f) return;
	if(ys[0] < 0.0f && ys[1] < 0.0f && ys[2] < 0.0f && ys[3] < 0.0f) return;
	if(xs[0] > sk::globals.width && xs[1] > sk::globals.width && xs[2] > sk::globals.width && xs[3] > sk::globals.width) return;
	if(ys[0] > sk::globals.height && ys[1] > sk::globals.height && ys[2] > sk::globals.height && ys[3] > sk::globals.height) return;

	float screenz = CalcScreenZ(z);

	rw::RWDEVICE::Im2DVertex *vert = &SpriteBufferVerts[nSpriteBufferIndex*6];
	static int indices[6] = { 0, 1, 2, 3, 0, 2 };
	for(i = 0; i < 6; i++){
		vert[i].setScreenX(xs[indices[i]]);
		vert[i].setScreenY(ys[indices[i]]);
		vert[i].setScreenZ(screenz);
		vert[i].setCameraZ(z);
		vert[i].setRecipCameraZ(recipz);
		vert[i].setU(us[indices[i]], recipz);
		vert[i].setV(vs[indices[i]], recipz);
		vert[i].setColor(r*intens>>8, g*intens>>8, b*intens>>8, a);
	}
	nSpriteBufferIndex++;
	if(nSpriteBufferIndex >= SPRITEBUFFERSIZE)
		FlushSpriteBuffer();
}

void
Sprite::RenderBufferedOneXLUSprite_Rotate_2Colours(float x, float y, float z, float w, float h, uint8 r1, uint8 g1, uint8 b1, uint8 r2, uint8 g2, uint8 b2, float cx, float cy, float recipz, float rotation, uint8 a)
{
	m_bFlushSpriteBufferSwitchZTest = 0;
	float c = cos(rotation);
	float s = sin(rotation);

	float xs[4];
	float ys[4];
	float us[4];
	float vs[4];
	float cf[4];
	int i;

	xs[0] = x + w*(-c-s);	us[0] = 0.0f;
	xs[1] = x + w*(-c+s);	us[1] = 0.0f;
	xs[2] = x + w*(+c+s);	us[2] = 1.0f;
	xs[3] = x + w*(+c-s);	us[3] = 1.0f;

	ys[0] = y + h*(-c+s);	vs[0] = 0.0f;
	ys[1] = y + h*(+c+s);	vs[1] = 1.0f;
	ys[2] = y + h*(+c-s);	vs[2] = 1.0f;
	ys[3] = y + h*(-c-s);	vs[3] = 0.0f;

	// No clipping, just culling
	if(xs[0] < 0.0f && xs[1] < 0.0f && xs[2] < 0.0f && xs[3] < 0.0f) return;
	if(ys[0] < 0.0f && ys[1] < 0.0f && ys[2] < 0.0f && ys[3] < 0.0f) return;
	if(xs[0] > sk::globals.width && xs[1] > sk::globals.width && xs[2] > sk::globals.width && xs[3] > sk::globals.width) return;
	if(ys[0] > sk::globals.height && ys[1] > sk::globals.height && ys[2] > sk::globals.height && ys[3] > sk::globals.height) return;

	// Colour factors, cx/y is the direction in which colours change from rgb1 to rgb2
	cf[0] = (cx*(-c-s) + cy*(-c+s))*0.5f + 0.5f;
	cf[0] = clamp(cf[0], 0.0f, 1.0f);
	cf[1] = (cx*(-c-s) + cy*(-c+s))*0.5f + 0.5f;
	cf[1] = clamp(cf[1], 0.0f, 1.0f);
	cf[2] = (cx*(-c-s) + cy*(-c+s))*0.5f + 0.5f;
	cf[2] = clamp(cf[2], 0.0f, 1.0f);
	cf[3] = (cx*(-c-s) + cy*(-c+s))*0.5f + 0.5f;
	cf[3] = clamp(cf[3], 0.0f, 1.0f);

	float screenz = CalcScreenZ(z);

	rw::RWDEVICE::Im2DVertex *vert = &SpriteBufferVerts[nSpriteBufferIndex*6];
	static int indices[6] = { 0, 1, 2, 3, 0, 2 };
	for(i = 0; i < 6; i++){
		vert[i].setScreenX(xs[indices[i]]);
		vert[i].setScreenY(ys[indices[i]]);
		vert[i].setScreenZ(screenz);
		vert[i].setCameraZ(z);
		vert[i].setRecipCameraZ(recipz);
		vert[i].setU(us[indices[i]], recipz);
		vert[i].setV(vs[indices[i]], recipz);
		vert[i].setColor(r1*cf[indices[i]] + r2*(1.0f - cf[indices[i]]),
			g1*cf[indices[i]] + g2*(1.0f - cf[indices[i]]),
			b1*cf[indices[i]] + b2*(1.0f - cf[indices[i]]),
			a);
	}
	nSpriteBufferIndex++;
	if(nSpriteBufferIndex >= SPRITEBUFFERSIZE)
		FlushSpriteBuffer();
}
