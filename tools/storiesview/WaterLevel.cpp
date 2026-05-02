#include "storiesview.h"

CWaterLevel_ *CWaterLevel_::mspInst;

#ifdef LCS
#define WATERSTARTX -2048.0f
#define WATERENDX 2048.0f
#else
#define WATERSTARTX (-2048.0f - 400.0f)
#define WATERENDX (2048.0f - 400.0f)
#endif

#define WATERSTARTY -2048.0f
#define WATERENDY 2048.0f
#define WATERSMALLSECTORSZX ((WATERENDX - WATERSTARTX)/128)
#define WATERSMALLSECTORSZY ((WATERENDY - WATERSTARTY)/128)
#define WATERSECTORSZX ((WATERENDX - WATERSTARTX)/64)
#define WATERSECTORSZY ((WATERENDY - WATERSTARTY)/64)

rw::Texture *gpWaterTex;

#define TEMPBUFFERVERTSIZE 256
#define TEMPBUFFERINDEXSIZE 1024
int TempBufferIndicesStored;
int TempBufferVerticesStored;
rw::RWDEVICE::Im3DVertex TempVertexBuffer[TEMPBUFFERVERTSIZE];
uint16 TempIndexBuffer[TEMPBUFFERINDEXSIZE];

float TEXTURE_ADDU, TEXTURE_ADDV;

void
CWaterLevel_::RenderAndEmptyRenderBuffer(void)
{
	assert(TempBufferVerticesStored <= TEMPBUFFERVERTSIZE);
	assert(TempBufferIndicesStored <= TEMPBUFFERINDEXSIZE);
	if(TempBufferVerticesStored){
		rw::im3d::Transform(TempVertexBuffer, TempBufferVerticesStored, nil, rw::im3d::EVERYTHING);
		rw::im3d::RenderIndexedPrimitive(rw::PRIMTYPETRILIST, TempIndexBuffer, TempBufferIndicesStored);
		rw::im3d::End();
	}
	TempBufferVerticesStored = 0;
	TempBufferIndicesStored = 0;
}

void
CWaterLevel_::Initialize(CWaterLevel *wl)
{
	mspInst = (CWaterLevel_*)wl;

	CTexListStore::PushCurrentTexList();
	int ptxd = CTexListStore::FindTexListSlot("particle");
	CTexListStore::SetCurrentTexList(ptxd);
	if(gpWaterTex == nil)
		gpWaterTex = rw::Texture::read("waterclear256", nil);
	CTexListStore::PopCurrentTexList();
}

void
CWaterLevel_::RenderOneFlatSmallWaterPoly(float x, float y, float z, rw::RGBA const &color)
{
	if(TempBufferVerticesStored+4 >= TEMPBUFFERVERTSIZE ||
	   TempBufferIndicesStored+6 >= TEMPBUFFERINDEXSIZE)
		RenderAndEmptyRenderBuffer();

	int i = TempBufferVerticesStored;
	TempVertexBuffer[i + 0].setX(x);
	TempVertexBuffer[i + 0].setY(y);
	TempVertexBuffer[i + 0].setZ(z);
	TempVertexBuffer[i + 0].setU(TEXTURE_ADDU);
	TempVertexBuffer[i + 0].setV(TEXTURE_ADDV);
	TempVertexBuffer[i + 0].setColor(color.red, color.green, color.blue, color.alpha);

	TempVertexBuffer[i + 1].setX(x);
	TempVertexBuffer[i + 1].setY(y + WATERSMALLSECTORSZY);
	TempVertexBuffer[i + 1].setZ(z);
	TempVertexBuffer[i + 1].setU(TEXTURE_ADDU);
	TempVertexBuffer[i + 1].setV(TEXTURE_ADDV + 1.0f);
	TempVertexBuffer[i + 1].setColor(color.red, color.green, color.blue, color.alpha);

	TempVertexBuffer[i + 2].setX(x + WATERSMALLSECTORSZX);
	TempVertexBuffer[i + 2].setY(y + WATERSMALLSECTORSZY);
	TempVertexBuffer[i + 2].setZ(z);
	TempVertexBuffer[i + 2].setU(TEXTURE_ADDU + 1.0f);
	TempVertexBuffer[i + 2].setV(TEXTURE_ADDV + 1.0f);
	TempVertexBuffer[i + 2].setColor(color.red, color.green, color.blue, color.alpha);

	TempVertexBuffer[i + 3].setX(x + WATERSMALLSECTORSZX);
	TempVertexBuffer[i + 3].setY(y);
	TempVertexBuffer[i + 3].setZ(z);
	TempVertexBuffer[i + 3].setU(TEXTURE_ADDU + 1.0f);
	TempVertexBuffer[i + 3].setV(TEXTURE_ADDV);
	TempVertexBuffer[i + 3].setColor(color.red, color.green, color.blue, color.alpha);

	TempBufferVerticesStored += 4;
	TempIndexBuffer[TempBufferIndicesStored++] = i+0;
	TempIndexBuffer[TempBufferIndicesStored++] = i+1;
	TempIndexBuffer[TempBufferIndicesStored++] = i+2;
	TempIndexBuffer[TempBufferIndicesStored++] = i+0;
	TempIndexBuffer[TempBufferIndicesStored++] = i+2;
	TempIndexBuffer[TempBufferIndicesStored++] = i+3;
}

void
CWaterLevel_::RenderOneFlatLargeWaterPoly(float x, float y, float z, rw::RGBA const &color)
{
	if(TempBufferVerticesStored+4 >= TEMPBUFFERVERTSIZE ||
	   TempBufferIndicesStored+6 >= TEMPBUFFERINDEXSIZE)
		RenderAndEmptyRenderBuffer();

	int i = TempBufferVerticesStored;
	TempVertexBuffer[i + 0].setX(x);
	TempVertexBuffer[i + 0].setY(y);
	TempVertexBuffer[i + 0].setZ(z);
	TempVertexBuffer[i + 0].setU(TEXTURE_ADDU);
	TempVertexBuffer[i + 0].setV(TEXTURE_ADDV);
	TempVertexBuffer[i + 0].setColor(color.red, color.green, color.blue, color.alpha);

	TempVertexBuffer[i + 1].setX(x);
	TempVertexBuffer[i + 1].setY(y + WATERSECTORSZY);
	TempVertexBuffer[i + 1].setZ(z);
	TempVertexBuffer[i + 1].setU(TEXTURE_ADDU);
	TempVertexBuffer[i + 1].setV(TEXTURE_ADDV + 2.0f);
	TempVertexBuffer[i + 1].setColor(color.red, color.green, color.blue, color.alpha);

	TempVertexBuffer[i + 2].setX(x + WATERSECTORSZX);
	TempVertexBuffer[i + 2].setY(y + WATERSECTORSZY);
	TempVertexBuffer[i + 2].setZ(z);
	TempVertexBuffer[i + 2].setU(TEXTURE_ADDU + 2.0f);
	TempVertexBuffer[i + 2].setV(TEXTURE_ADDV + 2.0f);
	TempVertexBuffer[i + 2].setColor(color.red, color.green, color.blue, color.alpha);

	TempVertexBuffer[i + 3].setX(x + WATERSECTORSZX);
	TempVertexBuffer[i + 3].setY(y);
	TempVertexBuffer[i + 3].setZ(z);
	TempVertexBuffer[i + 3].setU(TEXTURE_ADDU + 2.0f);
	TempVertexBuffer[i + 3].setV(TEXTURE_ADDV);
	TempVertexBuffer[i + 3].setColor(color.red, color.green, color.blue, color.alpha);

	TempBufferVerticesStored += 4;
	TempIndexBuffer[TempBufferIndicesStored++] = i+0;
	TempIndexBuffer[TempBufferIndicesStored++] = i+1;
	TempIndexBuffer[TempBufferIndicesStored++] = i+2;
	TempIndexBuffer[TempBufferIndicesStored++] = i+0;
	TempIndexBuffer[TempBufferIndicesStored++] = i+2;
	TempIndexBuffer[TempBufferIndicesStored++] = i+3;
}

void
CWaterLevel_::RenderWater(void)
{
	int i, j;

	rw::SetRenderState(rw::VERTEXALPHA, 1);
	rw::SetRenderState(rw::FOGENABLE, 1);
	rw::SetRenderStatePtr(rw::TEXTURERASTER, gpWaterTex->raster);

	rw::RGBA color = { 255, 255, 255, 255 };
	color.red = pTimecycle->m_fCurrentWaterRed;
	color.green = pTimecycle->m_fCurrentWaterGreen;
	color.blue = pTimecycle->m_fCurrentWaterBlue;
#ifdef LCS
	color.alpha = 255;
#else
	color.alpha = pTimecycle->m_fCurrentWaterAlpha;
#endif
	TEXTURE_ADDU = 0.0f;
	TEXTURE_ADDV = 0.0f;
	float x, y, z;

	rw::Sphere sph;
	// Small polys
	sph.radius = sqrt(WATERSMALLSECTORSZX*WATERSMALLSECTORSZX*2.0f)/2.0f;
	for(i = 0; i < 128; i++)
		for(j = 0; j < 128; j++){
			if(aWaterFineBlockList[i][j] & 0x80)
				continue;
			x = WATERSMALLSECTORSZX*i + WATERSTARTX;
			y = WATERSMALLSECTORSZY*j + WATERSTARTY;
			z = ms_aWaterZs[aWaterFineBlockList[i][j]];
			sph.center.x = x + WATERSMALLSECTORSZX/2;
			sph.center.y = y + WATERSMALLSECTORSZY/2;
			sph.center.z = z;
			if(Scene.camera->frustumTestSphere(&sph) != rw::Camera::SPHEREOUTSIDE)
				RenderOneFlatSmallWaterPoly(x, y, z, color);
		}
	RenderAndEmptyRenderBuffer();
}
