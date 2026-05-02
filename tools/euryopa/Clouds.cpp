#include "euryopa.h"

namespace Clouds
{

rw::Texture *gpCloudTex[5];
float CloudRotation;
float IndividualRotation;

#define SMALLSTRIPHEIGHT 4.0f
#define HORIZSTRIPHEIGHT 32.0f	// Vice City, actually 48 in III

static float ms_cameraRoll;
static float ms_horizonZ;
static CRGBA ms_colourTop;
static CRGBA ms_colourBottom;
// Vice City
static CRGBA ms_colourBkGrd;

void
Init(void)
{
	TxdPush();
	int ptxd = FindTxdSlot("particle");
	TxdMakeCurrent(ptxd);
	gpCloudTex[0] = rw::Texture::read("cloud1", nil);
	gpCloudTex[1] = rw::Texture::read("cloud2", nil);
	gpCloudTex[2] = rw::Texture::read("cloud3", nil);
	if(params.timecycle == GAME_SA){
		gpCloudTex[1] = gpCloudTex[0];
		gpCloudTex[2] = gpCloudTex[0];
	}
	gpCloudTex[3] = rw::Texture::read("cloudhilit", nil);
	gpCloudTex[4] = rw::Texture::read("cloudmasked", nil);
	TxdPop();

}

bool
UseDarkBackground(void)
{
	return TheCamera.m_rwcam->getFrame()->getLTM()->up.z < -0.9f;
}

void
RenderBackground(int16 topred, int16 topgreen, int16 topblue,
	int16 botred, int16 botgreen, int16 botblue, int16 alpha)
{
	int w, h;
// currently unused, only for VC where horizon can tilt
//	rw::Matrix *mat = TheCamera.m_rwcam->getFrame()->getLTM();
//	float c = sqrt(mat->right.x * mat->right.x + mat->right.y * mat->right.y);
//	if(c > 1.0f)
//		c = 1.0f;
//	ms_cameraRoll = acos(c);
//	if(mat->right.z < 0.0f)
//		ms_cameraRoll = -ms_cameraRoll;
	ms_cameraRoll= 0.0f;

	w = sk::globals.width;
	h = sk::globals.height;

	if(UseDarkBackground()){
		ms_colourTop.r = 50;
		ms_colourTop.g = 50;
		ms_colourTop.b = 50;
		ms_colourTop.a = 255;
		ms_colourBottom = ms_colourTop;
		CRect r(0, 0, w, h);
		Sprite2d::DrawRect(r, ms_colourBottom, ms_colourBottom, ms_colourTop, ms_colourTop);
	}else{
		ms_horizonZ = Sprite::CalcHorizonCoors();

		// Draw top/bottom gradient
		float gradheight = h/2.0f;
		float topedge = ms_horizonZ - gradheight;
		float botpos, toppos;
		if(ms_horizonZ > 0.0f && topedge < h){
			ms_colourTop.r = topred;
			ms_colourTop.g = topgreen;
			ms_colourTop.b = topblue;
			ms_colourTop.a = alpha;
			ms_colourBottom.r = botred;
			ms_colourBottom.g = botgreen;
			ms_colourBottom.b = botblue;
			ms_colourBottom.a = alpha;

			if(ms_horizonZ < h)
				botpos = ms_horizonZ;
			else{
				float f = (ms_horizonZ - h)/gradheight;
				ms_colourBottom.r = topred*f + (1.0f-f)*botred;
				ms_colourBottom.g = topgreen*f + (1.0f-f)*botgreen;
				ms_colourBottom.b = topblue*f + (1.0f-f)*botblue;
				botpos = h;
			}
			if(topedge >= 0.0f)
				toppos = topedge;
			else{
				float f = (0.0f - topedge)/gradheight;
				ms_colourTop.r = botred*f + (1.0f-f)*topred;
				ms_colourTop.g = botgreen*f + (1.0f-f)*topgreen;
				ms_colourTop.b = botblue*f + (1.0f-f)*topblue;
				toppos = 0.0f;
			}
			Sprite2d::DrawRect(CRect(0, toppos, w, botpos),
				ms_colourBottom, ms_colourBottom, ms_colourTop, ms_colourTop);
		}

		// draw the small stripe (whatever it's supposed to be)
		if(ms_horizonZ > -SMALLSTRIPHEIGHT && ms_horizonZ < h){
			// Same colour as fog
			ms_colourTop.r = (topred + 2 * botred) / 3;
			ms_colourTop.g = (topgreen + 2 * botgreen) / 3;
			ms_colourTop.b = (topblue + 2 * botblue) / 3;
			Sprite2d::DrawRect(CRect(0, ms_horizonZ, w, ms_horizonZ+SMALLSTRIPHEIGHT),
				ms_colourTop, ms_colourTop, ms_colourTop, ms_colourTop);
		}

		// Only top
		if(topedge > 0.0f){
			ms_colourTop.r = topred;
			ms_colourTop.g = topgreen;
			ms_colourTop.b = topblue;
			ms_colourTop.a = alpha;
			ms_colourBottom.r = topred;
			ms_colourBottom.g = topgreen;
			ms_colourBottom.b = topblue;
			ms_colourBottom.a = alpha;

			if(topedge > h)
				botpos = h;
			else
				botpos = topedge;
			Sprite2d::DrawRect(CRect(0, 0, w, botpos),
				ms_colourBottom, ms_colourBottom, ms_colourTop, ms_colourTop);
		}

		// Set both to fog colour for RenderHorizon
		ms_colourTop.r = (topred + 2 * botred) / 3;
		ms_colourTop.g = (topgreen + 2 * botgreen) / 3;
		ms_colourTop.b = (topblue + 2 * botblue) / 3;
		ms_colourBottom.r = (topred + 2 * botred) / 3;
		ms_colourBottom.g = (topgreen + 2 * botgreen) / 3;
		ms_colourBottom.b = (topblue + 2 * botblue) / 3;
	}
}

void
RenderHorizon(void)
{
	int w, h;
	w = sk::globals.width;
	h = sk::globals.height;

	if(UseDarkBackground())
		return;

	ms_colourBottom.a = 230;
	ms_colourTop.a = 80;

	if(ms_horizonZ >= h)
		return;

	float z1 = ms_horizonZ + SMALLSTRIPHEIGHT;
	if(z1 > h)
		z1 = h;
	Sprite2d::DrawRectXLU(CRect(0, ms_horizonZ, w, z1),
		ms_colourBottom, ms_colourBottom, ms_colourTop, ms_colourTop);

	// The rest is more or less from Vice City. III does strange things

	float z2 = z1 + HORIZSTRIPHEIGHT;
	if(z2 > h)
		z2 = h;

	// comes from ambient colour in VC
	ms_colourBkGrd.r = 100;
	ms_colourBkGrd.g = 100;
	ms_colourBkGrd.b = 100;
	ms_colourBkGrd.a = 255;

	Sprite2d::DrawRectXLU(CRect(0, z1, w, z2),
		ms_colourBkGrd, ms_colourBkGrd, ms_colourTop, ms_colourTop);

	Sprite2d::DrawRectXLU(CRect(0, z2, w, h),
		ms_colourBkGrd, ms_colourBkGrd, ms_colourBkGrd, ms_colourBkGrd);
}

void
RenderLowClouds(void)
{
	int i;
	float intensity;
	float szx, szy;
	rw::V3d screenpos;

	rw::SetRenderState(rw::ZWRITEENABLE, 0);
	rw::SetRenderState(rw::ZTESTENABLE, 0);
	rw::SetRenderState(rw::VERTEXALPHA, 1);
	rw::SetRenderState(rw::SRCBLEND, rw::BLENDONE);
	rw::SetRenderState(rw::DESTBLEND, rw::BLENDONE);
	Sprite::InitSpriteBuffer();

	rw::V3d campos = TheCamera.m_position;

	static float LowCloudsX[12] = { 1.0f, 0.7f, 0.0f, -0.7f, -1.0f, -0.7f,
		0.0f, 0.7f, 0.8f, -0.8f, 0.4f, -0.4f };
	static float LowCloudsY[12] = { 0.0f, -0.7f, -1.0f, -0.7f, 0.0f, 0.7f,
		1.0f, 0.7f, 0.4f, 0.4f, -0.8f, -0.8f };
	static float LowCloudsZ[12] = { 0.0f, 1.0f, 0.5f, 0.0f, 1.0f, 0.3f,
		0.9f, 0.4f, 1.3f, 1.4f, 1.2f, 1.7f };
	intensity = 1.0f - max(max(Weather::foggyness,  Weather::cloudCoverage), Weather::extraSunnyness);
	int r = Timecycle::currentColours.lowCloud.red*255 * intensity;
	int g = Timecycle::currentColours.lowCloud.green*255 * intensity;
	int b = Timecycle::currentColours.lowCloud.blue*255 * intensity;
	for(int cloudtype = 0; cloudtype < 3; cloudtype++){
		for(i = cloudtype; i < 12; i += 3){
			rw::SetRenderStatePtr(rw::TEXTURERASTER, gpCloudTex[cloudtype]->raster);
			rw::V3d pos = { 800.0f*LowCloudsX[i], 800.0f*LowCloudsY[i], 60.0f*LowCloudsZ[i] + 40.0f };
			rw::V3d worldpos = { campos.x + pos.x, campos.y + pos.y, pos.z };
			if(Sprite::CalcScreenCoors(worldpos, &screenpos, &szx, &szy, false))
				Sprite::RenderBufferedOneXLUSprite_Rotate_Dimension(screenpos.x, screenpos.y, screenpos.z,
					szx*320.0f, szy*40.0f, r, g, b, 255, 1.0f/screenpos.z, ms_cameraRoll, 255);
		}
		Sprite::FlushSpriteBuffer();
	}

	rw::SetRenderState(rw::VERTEXALPHA, 0);
	rw::SetRenderState(rw::ZWRITEENABLE, 1);
	rw::SetRenderState(rw::ZTESTENABLE, 1);
	rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
	rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
}

void
RenderFluffyClouds(void)
{
	int i;
	float szx, szy;
	rw::V3d screenpos;

	int fluffyalpha = 160 * (1.0f - max(Weather::foggyness, Weather::extraSunnyness));
	if(fluffyalpha == 0)
		return;

	rw::SetRenderState(rw::ZWRITEENABLE, 0);
	rw::SetRenderState(rw::ZTESTENABLE, 0);
	rw::SetRenderState(rw::VERTEXALPHA, 1);
	rw::SetRenderState(rw::SRCBLEND, rw::BLENDONE);
	rw::SetRenderState(rw::DESTBLEND, rw::BLENDONE);
	Sprite::InitSpriteBuffer();

	rw::V3d campos = TheCamera.m_position;

	float rot_sin = sin(CloudRotation);
	float rot_cos = cos(CloudRotation);

	static float CoorsOffsetX[37] = {
		0.0f, 60.0f, 72.0f, 48.0f, 21.0f, 12.0f,
		9.0f, -3.0f, -8.4f, -18.0f, -15.0f, -36.0f,
		-40.0f, -48.0f, -60.0f, -24.0f, 100.0f, 100.0f,
		100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f,
		100.0f, 100.0f, -30.0f, -20.0f, 10.0f, 30.0f,
		0.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f, -100.0f
	};
	static float CoorsOffsetY[37] = {
		100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f,
		100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f,
		100.0f, 100.0f, 100.0f, 100.0f, -30.0f, 10.0f,
		-25.0f, -5.0f, 28.0f, -10.0f, 10.0f, 0.0f,
		15.0f, 40.0f, -100.0f, -100.0f, -100.0f, -100.0f,
		-100.0f, -40.0f, -20.0f, 0.0f, 10.0f, 30.0f, 35.0f
	};
	static float CoorsOffsetZ[37] = {
		2.0f, 1.0f, 0.0f, 0.3f, 0.7f, 1.4f,
		1.7f, 0.24f, 0.7f, 1.3f, 1.6f, 1.0f,
		1.2f, 0.3f, 0.7f, 1.4f, 0.0f, 0.1f,
		0.5f, 0.4f, 0.55f, 0.75f, 1.0f, 1.4f,
		1.7f, 2.0f, 2.0f, 2.3f, 1.9f, 2.4f,
		2.0f, 2.0f, 1.5f, 1.2f, 1.7f, 1.5f, 2.1f
	};
	static bool bCloudOnScreen[37];
	float hilight;

	rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
	rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
	rw::SetRenderStatePtr(rw::TEXTURERASTER, gpCloudTex[4]->raster);

	for(i = 0; i < 37; i++){
		rw::V3d pos = { 2.0f*CoorsOffsetX[i], 2.0f*CoorsOffsetY[i], 40.0f*CoorsOffsetZ[i] + 40.0f };
		rw::V3d worldpos = {
			pos.x*rot_cos + pos.y*rot_sin + campos.x,
			pos.x*rot_sin - pos.y*rot_cos + campos.y,
			pos.z };

			if(Sprite::CalcScreenCoors(worldpos, &screenpos, &szx, &szy, false)){
//				float sundist = sqrt(sq(screenpos.x-CCoronas::SunScreenX) + sq(screenpos.y-CCoronas::SunScreenY));
				int tr = Timecycle::currentColours.fluffyCloudTop.red*255;
				int tg = Timecycle::currentColours.fluffyCloudTop.green*255;
				int tb = Timecycle::currentColours.fluffyCloudTop.blue*255;
				int br = Timecycle::currentColours.fluffyCloudBottom.red*255;
				int bg = Timecycle::currentColours.fluffyCloudBottom.green*255;
				int bb = Timecycle::currentColours.fluffyCloudBottom.blue*255;
//				if(sundist < sk::globals.width/2){
//					hilight = (1.0f - coverage) * (1.0f - sundist/(sk::globals.width/2));
//					tr = tr*(1.0f-hilight) + 255*hilight;
//					tg = tg*(1.0f-hilight) + 190*hilight;
//					tb = tb*(1.0f-hilight) + 190*hilight;
//					br = br*(1.0f-hilight) + 255*hilight;
//					bg = bg*(1.0f-hilight) + 190*hilight;
//					bb = bb*(1.0f-hilight) + 190*hilight;
//					if(sundist < sk::globals.width/10)
//						CCoronas::SunBlockedByClouds = true;
//				}else
					hilight = 0.0f;
				Sprite::RenderBufferedOneXLUSprite_Rotate_2Colours(screenpos.x, screenpos.y, screenpos.z,
					szx*55.0f, szy*55.0f,
					tr, tg, tb, br, bg, bb, 0.0f, -1.0f,
					1.0f/screenpos.z,
					IndividualRotation * 0.000095825199f + ms_cameraRoll,
					fluffyalpha);
				bCloudOnScreen[i] = true;
			}else
				bCloudOnScreen[i] = false;
	}
	Sprite::FlushSpriteBuffer();


	rw::SetRenderState(rw::VERTEXALPHA, 0);
	rw::SetRenderState(rw::ZWRITEENABLE, 1);
	rw::SetRenderState(rw::ZTESTENABLE, 1);
	rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
	rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
}

#define TEMPBUFFERVERTSIZE 40
#define TEMPBUFFERINDEXSIZE 60
static int TempBufferIndicesStored;
static int TempBufferVerticesStored;
static rw::RWDEVICE::Im3DVertex TempVertexBuffer[TEMPBUFFERVERTSIZE];
static uint16 TempIndexBuffer[TEMPBUFFERINDEXSIZE];

static void
RenderAndEmptyRenderBuffer(void)
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
SetUpOneSkyPoly(rw::V3d tl, rw::V3d tr, rw::V3d bl, rw::V3d br, rw::RGBA coltop, rw::RGBA colbot)
{
	if(TempBufferVerticesStored+4 >= TEMPBUFFERVERTSIZE ||
	   TempBufferIndicesStored+6 >= TEMPBUFFERINDEXSIZE)
		RenderAndEmptyRenderBuffer();

	int i = TempBufferVerticesStored;
	TempVertexBuffer[i + 0].setX(tl.x);
	TempVertexBuffer[i + 0].setY(tl.y);
	TempVertexBuffer[i + 0].setZ(tl.z);
	TempVertexBuffer[i + 0].setU(0.0f);
	TempVertexBuffer[i + 0].setV(0.0f);
	TempVertexBuffer[i + 0].setColor(coltop.red, coltop.green, coltop.blue, coltop.alpha);

	TempVertexBuffer[i + 1].setX(tr.x);
	TempVertexBuffer[i + 1].setY(tr.y);
	TempVertexBuffer[i + 1].setZ(tr.z);
	TempVertexBuffer[i + 1].setU(0.0f);
	TempVertexBuffer[i + 1].setV(0.0f);
	TempVertexBuffer[i + 1].setColor(coltop.red, coltop.green, coltop.blue, coltop.alpha);

	TempVertexBuffer[i + 2].setX(bl.x);
	TempVertexBuffer[i + 2].setY(bl.y);
	TempVertexBuffer[i + 2].setZ(bl.z);
	TempVertexBuffer[i + 2].setU(0.0f);
	TempVertexBuffer[i + 2].setV(0.0f);
	TempVertexBuffer[i + 2].setColor(colbot.red, colbot.green, colbot.blue, colbot.alpha);

	TempVertexBuffer[i + 3].setX(br.x);
	TempVertexBuffer[i + 3].setY(br.y);
	TempVertexBuffer[i + 3].setZ(br.z);
	TempVertexBuffer[i + 3].setU(0.0f);
	TempVertexBuffer[i + 3].setV(0.0f);
	TempVertexBuffer[i + 3].setColor(colbot.red, colbot.green, colbot.blue, colbot.alpha);

	TempBufferVerticesStored += 4;
	TempIndexBuffer[TempBufferIndicesStored++] = i+0;
	TempIndexBuffer[TempBufferIndicesStored++] = i+2;
	TempIndexBuffer[TempBufferIndicesStored++] = i+1;
	TempIndexBuffer[TempBufferIndicesStored++] = i+1;
	TempIndexBuffer[TempBufferIndicesStored++] = i+2;
	TempIndexBuffer[TempBufferIndicesStored++] = i+3;
}

void
RenderSkyPolys(void)
{
	const float dist = 30.0f;

	rw::SetRenderStatePtr(rw::TEXTURERASTER, nil);
	rw::SetRenderState(rw::VERTEXALPHA, 0);
	rw::SetRenderState(rw::FOGENABLE, 0);
	rw::SetRenderState(rw::ZTESTENABLE, 0);
	rw::SetRenderState(rw::ZWRITEENABLE, 0);
	rw::SetRenderState(rw::CULLMODE, rw::CULLNONE);

	TempBufferVerticesStored = 0;
	TempBufferIndicesStored = 0;

	rw::V3d at = TheCamera.m_rwcam->getFrame()->getLTM()->at;
	rw::V3d pos = TheCamera.m_position;
	at.z = 0.0f;
	at = normalize(at);
	// Scale sky-box half-width to current horizontal FOV so the box doesn't
	// leave visible gaps at wide FOV. Default 1.4 (~108° hfov) is the floor.
	float halfH = atan(TheCamera.m_aspectRatio *
	                   tan(TheCamera.m_fov * 0.5f * 3.14159265359f / 180.0f));
	float sideScale = tan(halfH) * 1.2f;
	if(sideScale < 1.4f) sideScale = 1.4f;
	rw::V3d right = { at.y*sideScale, -at.x*sideScale, 0.0f };
	float f = (pos.z - 25.0f)/80.0f;	// below horizon fog
	f = clamp(f, 0.0f, 1.0f);

	rw::RGBA skytop, skybot, belowhoriz;
	rw::convColor(&skytop, &Timecycle::currentColours.skyTop);
	rw::convColor(&skybot, &Timecycle::currentColours.skyBottom);
	belowhoriz.red = Timecycle::belowHorizonColour.red*(1.0f-f) + skybot.red*f;
	belowhoriz.green = Timecycle::belowHorizonColour.green*(1.0f-f) + skybot.green*f;
	belowhoriz.blue = Timecycle::belowHorizonColour.blue*(1.0f-f) + skybot.blue*f;


	rw::V3d frontright = add(pos, scale(add(at, right), dist));
	rw::V3d frontleft = add(pos, scale(sub(at, right), dist));
	rw::V3d backright = sub(pos, scale(sub(at, right), dist));
	rw::V3d backleft = sub(pos, scale(add(at, right), dist));
	rw::V3d topmost = { 0.0f, 0.0f, 0.5f*dist };
	rw::V3d middle = { 0.0f, 0.0f, -0.1f*dist };
	rw::V3d botmost = { 0.0f, 0.0f, -0.3f*dist };

	rw::V3d tl, tr, bl, br;
	tl = add(frontleft, topmost);
	tr = add(frontright, topmost);
	bl = frontleft;
	br = frontright;
	SetUpOneSkyPoly(tl, tr, bl, br, skytop, skybot);

	tl = frontleft;
	tr = frontright;
	bl = add(frontleft, middle);
	br = add(frontright, middle);
	SetUpOneSkyPoly(tl, tr, bl, br, skybot, skybot);

	tl = add(frontleft, middle);
	tr = add(frontright, middle);
	bl = add(frontleft, botmost);
	br = add(frontright, botmost);
	SetUpOneSkyPoly(tl, tr, bl, br, skybot, belowhoriz);

	tl = add(frontleft, topmost);
	tr = add(frontright, topmost);
	bl = add(backleft, topmost);
	br = add(backright, topmost);
	SetUpOneSkyPoly(tl, tr, bl, br, skytop, skytop);

	tl = add(frontleft, botmost);
	tr = add(frontright, botmost);
	bl = add(backleft, botmost);
	br = add(backright, botmost);
	SetUpOneSkyPoly(tl, tr, bl, br, belowhoriz, belowhoriz);

	RenderAndEmptyRenderBuffer();

	rw::SetRenderState(rw::ZTESTENABLE, 1);
	rw::SetRenderState(rw::ZWRITEENABLE, 1);
}

}
