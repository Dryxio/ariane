#include "III.h"
#include "FileMgr.h"

#define WATERSTARTX -2048.0f
#define WATERENDX 2048.0f
#define WATERSTARTY -2048.0f
#define WATERENDY 2048.0f
#define WATERSMALLSECTORSZX ((WATERENDX - WATERSTARTX)/128)
#define WATERSMALLSECTORSZY ((WATERENDY - WATERSTARTY)/128)
#define WATERSECTORSZX ((WATERENDX - WATERSTARTX)/64)
#define WATERSECTORSZY ((WATERENDY - WATERSTARTY)/64)


int CWaterLevel::ms_nNoOfWaterLevels;
float CWaterLevel::ms_aWaterZs[48];
CRect CWaterLevel::ms_aWaterRects[48];	// Seems to be unused
uint8 CWaterLevel::aWaterBlockList[64][64];
uint8 CWaterLevel::aWaterFineBlockList[128][128];

rw::Texture *gpWaterTex;
rw::Raster *gpWaterRaster;

void
CWaterLevel::Initialise(const char*)
{
	FileHandle file;

	ms_nNoOfWaterLevels = 0;
	CFileMgr::SetDir("DATA");
	file = CFileMgr::OpenFile("WATERPRO.DAT", "rb");
	CFileMgr::SetDir("");
	if(file == nil)
		return;
	CFileMgr::Read(file, (uint8*)&ms_nNoOfWaterLevels, sizeof(ms_nNoOfWaterLevels));
	CFileMgr::Read(file, (uint8*)&ms_aWaterZs, sizeof(ms_aWaterZs));
	CFileMgr::Read(file, (uint8*)&ms_aWaterRects, sizeof(ms_aWaterRects));
	CFileMgr::Read(file, (uint8*)&aWaterBlockList, sizeof(aWaterBlockList));
	CFileMgr::Read(file, (uint8*)&aWaterFineBlockList, sizeof(aWaterFineBlockList));
	CFileMgr::CloseFile(file);

	CTxdStore::PushCurrentTxd();
	int ptxd = CTxdStore::FindTxdSlot("particle");
	CTxdStore::SetCurrentTxd(ptxd);
	if(gpWaterTex == nil)
		gpWaterTex = rw::Texture::read("water_old", nil);
	gpWaterRaster = gpWaterTex->raster;
	CTxdStore::PopCurrentTxd();
}

float TEXTURE_ADDU, TEXTURE_ADDV;

void
CWaterLevel::RenderAndEmptyRenderBuffer(void)
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
CWaterLevel::RenderOneFlatSmallWaterPoly(float x, float y, float z, rw::RGBA const &color)
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
CWaterLevel::RenderOneFlatLargeWaterPoly(float x, float y, float z, rw::RGBA const &color)
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
CWaterLevel::RenderWater(void)
{
	int i, j;

	rw::SetRenderStatePtr(rw::TEXTURERASTER, gpWaterRaster);
	rw::SetRenderState(rw::FOGENABLE, 1);
	rw::SetRenderState(rw::SRCBLEND, rw::BLENDONE);
	rw::SetRenderState(rw::DESTBLEND, rw::BLENDZERO);

	rw::RGBA color = { 255, 255, 255, 255 };
	color.red = (CTimeCycle::m_fCurrentAmbientRed + CTimeCycle::m_fCurrentDirectionalRed*0.5f)*255.0f;
	color.green = (CTimeCycle::m_fCurrentAmbientGreen + CTimeCycle::m_fCurrentDirectionalGreen*0.5f)*255.0f;
	color.blue = (CTimeCycle::m_fCurrentAmbientBlue + CTimeCycle::m_fCurrentDirectionalBlue*0.5f)*255.0f;
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

/*
	// Large polys
	sph.radius = sqrt(WATERSECTORSZX*WATERSECTORSZX*2.0f)/2.0f;
	for(i = 0; i < 64; i++)
		for(j = 0; j < 64; j++){
			if(aWaterBlockList[i][j] & 0x80)
				continue;
			x = WATERSECTORSZX*i + WATERSTARTX;
			y = WATERSECTORSZY*j + WATERSTARTY;
			z = ms_aWaterZs[aWaterBlockList[i][j]];
			sph.center.x = x + WATERSECTORSZX/2;
			sph.center.y = y + WATERSECTORSZY/2;
			sph.center.z = z;
			if(Scene.camera->frustumTestSphere(&sph) != rw::Camera::SPHEREOUTSIDE)
				RenderOneFlatLargeWaterPoly(x, y, z, color);
		}
*/
	RenderAndEmptyRenderBuffer();

	DefinedState();
}
