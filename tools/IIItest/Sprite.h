#ifndef _SPRITE_H_
#define _SPRITE_H_

class Sprite2d
{
	static rw::RWDEVICE::Im2DVertex ma_vertices[4];
public:
	static void DrawRect(CRect const &r, CRGBA const &c0, CRGBA const &c1, CRGBA const &c2, CRGBA const &c3);
	static void DrawRectXLU(CRect const &r, CRGBA const &c0, CRGBA const &c1, CRGBA const &c2, CRGBA const &c3);
	static void SetVertices(CRect const &r, CRGBA const &c0, CRGBA const &c1, CRGBA const &c2, CRGBA const &c3, bool far);
};

class CSprite
{
	static float m_f2DNearScreenZ;
	static float m_f2DFarScreenZ;
	static int m_bFlushSpriteBufferSwitchZTest;
public:
	static float CalcHorizonCoors(void);
	static bool CalcScreenCoors(const rw::V3d &in, rw::V3d *out, float *outw, float *outh, bool farclip);
	static void InitSpriteBuffer(void);
	static void FlushSpriteBuffer(void);
	static void RenderOneXLUSprite(float x, float y, float z, float w, float h, uint8 r, uint8 g, uint8 b, int16 intens, float recipz, uint8 a);
	static void RenderBufferedOneXLUSprite(float x, float y, float z, float w, float h, uint8 r, uint8 g, uint8 b, int16 intens, float recipz, uint8 a);
	static void RenderBufferedOneXLUSprite_Rotate_Dimension(float x, float y, float z, float w, float h, uint8 r, uint8 g, uint8 b, int16 intens, float recipz, float roll, uint8 a);
	static void RenderBufferedOneXLUSprite_Rotate_Aspect(float x, float y, float z, float w, float h, uint8 r, uint8 g, uint8 b, int16 intens, float recipz, float roll, uint8 a);
	// cx/y is the direction in which the colour changes
	static void RenderBufferedOneXLUSprite_Rotate_2Colours(float x, float y, float z, float w, float h, uint8 r1, uint8 g1, uint8 b1, uint8 r2, uint8 g2, uint8 b2, float cx, float cy, float recipz, float rotation, uint8 a);
};

#endif
