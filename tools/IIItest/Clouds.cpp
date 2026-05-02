#include "III.h"
#include "Sprite.h"
#include "General.h"
#include "Coronas.h"
#include "Camera.h"

#define SMALLSTRIPHEIGHT 4.0f
#define HORIZSTRIPHEIGHT 32.0f	// Vice City, actually 48 in III

rw::Texture *gpCloudTex[5];

float CClouds::CloudRotation;
float CClouds::IndividualRotation;

float CClouds::ms_cameraRoll;
float CClouds::ms_horizonZ;
CRGBA CClouds::ms_colourTop;
CRGBA CClouds::ms_colourBottom;
// Vice City
CRGBA CClouds::ms_colourBkGrd;

void
CClouds::Init(void)
{
	CTxdStore::PushCurrentTxd();
	CTxdStore::SetCurrentTxd(CTxdStore::FindTxdSlot("particle"));
	gpCloudTex[0] = rw::Texture::read("cloud1", nil);
	gpCloudTex[1] = rw::Texture::read("cloud2", nil);
	gpCloudTex[2] = rw::Texture::read("cloud3", nil);
	gpCloudTex[3] = rw::Texture::read("cloudhilit", nil);
	gpCloudTex[4] = rw::Texture::read("cloudmasked", nil);
	CTxdStore::PopCurrentTxd();
	CloudRotation = 0.0f;
}

void
CClouds::Update(void)
{
	float s = sin(TheCamera.Orientation - 0.85f);
	CClouds::CloudRotation += CWeather::Wind*s*0.0025f;
	CClouds::IndividualRotation += (CWeather::Wind*CTimer::ms_fTimeStep + 0.3f) * 60.0f;
}


void
CClouds::Render(void)
{
	int i;
	float szx, szy;
	rw::V3d screenpos;

	CCoronas::SunBlockedByClouds = false;

	rw::SetRenderState(rw::ZWRITEENABLE, 0);
	rw::SetRenderState(rw::ZTESTENABLE, 0);
	rw::SetRenderState(rw::VERTEXALPHA, 1);
	rw::SetRenderState(rw::SRCBLEND, rw::BLENDONE);
	rw::SetRenderState(rw::DESTBLEND, rw::BLENDONE);
	CSprite::InitSpriteBuffer();

	int minute = CClock::ms_nGameClockHours*60 + CClock::ms_nGameClockMinutes;
	rw::V3d campos = *(rw::V3d*)&TheCamera.GetPosition();

	float coverage = CWeather::CloudCoverage <= CWeather::Foggyness ? CWeather::Foggyness : CWeather::CloudCoverage;

	// Moon
	int moonfadeout = abs(minute - 180);	// fully visible at 3AM
	if(moonfadeout < 180){			// fade in/out 3 hours
		int brightness = (1.0f - coverage) * (180 - moonfadeout);
		rw::V3d pos = { 0.0f, -100.0f, 15.0f };
		rw::V3d worldpos = rw::add(campos, pos);
		if(CSprite::CalcScreenCoors(worldpos, &screenpos, &szx, &szy, false)){
			rw::SetRenderStatePtr(rw::TEXTURERASTER, gpCoronaTexture[2]->raster);
			if(CCoronas::bSmallMoon){
				szx *= 4.0f;
				szy *= 4.0f;
			}else{
				szx *= 10.0f;
				szy *= 10.0f;
			}
			CSprite::RenderOneXLUSprite(screenpos.x, screenpos.y, screenpos.z,
				szx, szy, brightness, brightness, brightness, 255, 1.0f/screenpos.z, 255);
		}
	}

	// The R* logo
	int starintens = 0;
	if(CClock::ms_nGameClockHours < 22 && CClock::ms_nGameClockHours > 5)
		starintens = 0;
	else if(CClock::ms_nGameClockHours > 22 || CClock::ms_nGameClockHours < 5)
		starintens = 255;
	else if(CClock::ms_nGameClockHours == 22)
		starintens = 255 * CClock::ms_nGameClockMinutes/60.0f;
	else if(CClock::ms_nGameClockHours == 5)
		starintens = 255 * (60 - CClock::ms_nGameClockMinutes)/60.0f;
	if(starintens != 0){
		// R
		static float StarCoorsX[9] = { 0.0f, 0.05f, 0.12f, 0.5f, 0.8f, 0.6f, 0.27f, 0.55f, 0.75f };
		static float StarCoorsY[9] = { 0.0f, 0.45f, 0.9f, 1.0f, 0.85f, 0.52f, 0.48f, 0.35f, 0.2f };
		static float StarSizes[9] = { 1.0f, 1.4f, 0.9f, 1.0f, 0.6f, 1.5f, 1.3f, 1.0f, 0.8f };
		int brightness = (1.0f - coverage) * starintens;
		rw::SetRenderStatePtr(rw::TEXTURERASTER, gpCoronaTexture[0]->raster);
		for(i = 0; i < 11; i++){
			rw::V3d pos = { 100.0f, 0.0f, 10.0f };
			if(i >= 9) pos.x = -pos.x;
			rw::V3d worldpos = rw::add(campos, pos);
			worldpos.y -= 90.0f*StarCoorsX[i%9];
			worldpos.z += 80.0f*StarCoorsY[i%9];
			if(CSprite::CalcScreenCoors(worldpos, &screenpos, &szx, &szy, false)){
				float sz = 0.8f*StarSizes[i%9];
				CSprite::RenderBufferedOneXLUSprite(screenpos.x, screenpos.y, screenpos.z,
					szx*sz, szy*sz, brightness, brightness, brightness, 255, 1.0f/screenpos.z, 255);
			}
		}
		CSprite::FlushSpriteBuffer();

		// *
		rw::SetRenderStatePtr(rw::TEXTURERASTER, gpCoronaTexture[0]->raster);
		rw::V3d pos = { 100.0f, 0.0f, 10.0f };
		rw::V3d worldpos = rw::add(campos, pos);
		worldpos.y -= 90.0f;
		if(CSprite::CalcScreenCoors(worldpos, &screenpos, &szx, &szy, false)){
			brightness *= (CGeneral::GetRandomNumber()&127) / 640.0f + 0.5f;
			CSprite::RenderOneXLUSprite(screenpos.x, screenpos.y, screenpos.z,
				szx*5.0f, szy*5.0f, brightness, brightness, brightness, 255, 1.0f/screenpos.z, 255);
		}
	}

	// Low clouds
	static float LowCloudsX[12] = { 1.0f, 0.7f, 0.0f, -0.7f, -1.0f, -0.7f,
		0.0f, 0.7f, 0.8f, -0.8f, 0.4f, -0.4f };
	static float LowCloudsY[12] = { 0.0f, -0.7f, -1.0f, -0.7f, 0.0f, 0.7f,
		1.0f, 0.7f, 0.4f, 0.4f, -0.8f, -0.8f };
	static float LowCloudsZ[12] = { 0.0f, 1.0f, 0.5f, 0.0f, 1.0f, 0.3f,
		0.9f, 0.4f, 1.3f, 1.4f, 1.2f, 1.7f };
	float lowcloudintensity = 1.0f - coverage;
	int r = CTimeCycle::m_nCurrentLowCloudsRed * lowcloudintensity;
	int g = CTimeCycle::m_nCurrentLowCloudsGreen * lowcloudintensity;
	int b = CTimeCycle::m_nCurrentLowCloudsBlue * lowcloudintensity;
	for(int cloudtype = 0; cloudtype < 3; cloudtype++){
		for(i = cloudtype; i < 12; i += 3){
			rw::SetRenderStatePtr(rw::TEXTURERASTER, gpCloudTex[cloudtype]->raster);
			rw::V3d pos = { 800.0f*LowCloudsX[i], 800.0f*LowCloudsY[i], 60.0f*LowCloudsZ[i] };
			rw::V3d worldpos = { campos.x + pos.x, campos.y + pos.y, 40.0f + pos.z };
			if(CSprite::CalcScreenCoors(worldpos, &screenpos, &szx, &szy, false))
				CSprite::RenderBufferedOneXLUSprite_Rotate_Dimension(screenpos.x, screenpos.y, screenpos.z,
					szx*320.0f, szy*40.0f, r, g, b, 255, 1.0f/screenpos.z, ms_cameraRoll, 255);
		}
		CSprite::FlushSpriteBuffer();
	}

	// Fluffy clouds
	float rot_sin = sin(CloudRotation);
	float rot_cos = cos(CloudRotation);
	int fluffyalpha = 160 * (1.0f - CWeather::Foggyness);
	if(fluffyalpha != 0){
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

				if(CSprite::CalcScreenCoors(worldpos, &screenpos, &szx, &szy, false)){
					float sundist = sqrt(sq(screenpos.x-CCoronas::SunScreenX) + sq(screenpos.y-CCoronas::SunScreenY));
					int tr = CTimeCycle::m_nCurrentFluffyCloudsTopRed;
					int tg = CTimeCycle::m_nCurrentFluffyCloudsTopGreen;
					int tb = CTimeCycle::m_nCurrentFluffyCloudsTopBlue;
					int br = CTimeCycle::m_nCurrentFluffyCloudsBottomRed;
					int bg = CTimeCycle::m_nCurrentFluffyCloudsBottomGreen;
					int bb = CTimeCycle::m_nCurrentFluffyCloudsBottomBlue;
					if(sundist < globals.width/2){
						hilight = (1.0f - coverage) * (1.0f - sundist/(globals.width/2));
						tr = tr*(1.0f-hilight) + 255*hilight;
						tg = tg*(1.0f-hilight) + 190*hilight;
						tb = tb*(1.0f-hilight) + 190*hilight;
						br = br*(1.0f-hilight) + 255*hilight;
						bg = bg*(1.0f-hilight) + 190*hilight;
						bb = bb*(1.0f-hilight) + 190*hilight;
						if(sundist < globals.width/10)
							CCoronas::SunBlockedByClouds = true;
					}else
						hilight = 0.0f;
					CSprite::RenderBufferedOneXLUSprite_Rotate_2Colours(screenpos.x, screenpos.y, screenpos.z,
						szx*55.0f, szy*55.0f,
						tr, tg, tb, br, bg, bb, 0.0f, -1.0f,
						1.0f/screenpos.z,
						CClouds::IndividualRotation * 0.000095825199f + CClouds::ms_cameraRoll,
						fluffyalpha);
					bCloudOnScreen[i] = true;
				}else
					bCloudOnScreen[i] = false;
		}
		CSprite::FlushSpriteBuffer();

		// Highlights
		rw::SetRenderState(rw::SRCBLEND, rw::BLENDONE);
		rw::SetRenderState(rw::DESTBLEND, rw::BLENDONE);
		rw::SetRenderStatePtr(rw::TEXTURERASTER, gpCloudTex[3]->raster);

		for(i = 0; i < 37; i++){
			rw::V3d pos = { 2.0f*CoorsOffsetX[i], 2.0f*CoorsOffsetY[i], 40.0f*CoorsOffsetZ[i] + 40.0f };
			rw::V3d worldpos = {
				campos.x*rot_cos + campos.y*rot_sin + pos.x,
				campos.x*rot_sin + campos.y*rot_cos + pos.y,
				pos.z };
			if(bCloudOnScreen[i] && CSprite::CalcScreenCoors(worldpos, &screenpos, &szx, &szy, false)){
				// BUG: this is stupid....would have to do this for each cloud individually
				if(hilight > 0.0f){
					CSprite::RenderBufferedOneXLUSprite_Rotate_Aspect(screenpos.x, screenpos.y, screenpos.z,
						szx*30.0f, szy*30.0f,
						200*hilight, 0, 0, 255, 1.0f/screenpos.z,
						1.7f - CGeneral::GetATanOfXY(screenpos.x-CCoronas::SunScreenX, screenpos.y-CCoronas::SunScreenY) + CClouds::ms_cameraRoll, 255);
				}
			}
		}
		CSprite::FlushSpriteBuffer();
	}

	// Rainbow
	if(CWeather::Rainbow != 0.0f){
		static uint8 BowRed[6] = { 30, 30, 30, 10, 0, 15 };
		static uint8 BowGreen[6] = { 0, 15, 30, 30, 0, 0 };
		static uint8 BowBlue[6] = { 0, 0, 0, 10, 30, 30 };
		rw::SetRenderStatePtr(rw::TEXTURERASTER, gpCoronaTexture[0]->raster);
		for(i = 0; i < 6; i++){
			rw::V3d pos = { i*1.5f, 100.0f, 5.0f };
			rw::V3d worldpos = rw::add(campos, pos);
			if(CSprite::CalcScreenCoors(worldpos, &screenpos, &szx, &szy, false))
				CSprite::RenderBufferedOneXLUSprite(screenpos.x, screenpos.y, screenpos.z,
					2.0f*szx, 50.0*szy,
					BowRed[i]*CWeather::Rainbow, BowGreen[i]*CWeather::Rainbow, BowBlue[i]*CWeather::Rainbow,
					255, 1.0f/screenpos.z, 255);

		}
		CSprite::FlushSpriteBuffer();
	}

	rw::SetRenderState(rw::VERTEXALPHA, 0);
	rw::SetRenderState(rw::ZWRITEENABLE, 1);
	rw::SetRenderState(rw::ZTESTENABLE, 1);
	rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
	rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
}

bool
UseDarkBackground(void)
{
	return TheCamera.m_pRwCamera->getFrame()->getLTM()->up.z < -0.9f;
}

void
CClouds::RenderBackground(int16 topred, int16 topgreen, int16 topblue,
	int16 botred, int16 botgreen, int16 botblue, int16 alpha)
{
	rw::Matrix *mat = TheCamera.m_pRwCamera->getFrame()->getLTM();
	float c = sqrt(mat->right.x * mat->right.x + mat->right.y * mat->right.y);
	if(c > 1.0f)
		c = 1.0f;
	ms_cameraRoll = acos(c);
	if(mat->right.z < 0.0f)
		ms_cameraRoll = -ms_cameraRoll;

	if(UseDarkBackground()){
		ms_colourTop.r = 50;
		ms_colourTop.g = 50;
		ms_colourTop.b = 50;
		ms_colourTop.a = 255;
		ms_colourBottom = ms_colourTop;
		CRect r(0, 0, globals.width, globals.height);
		Sprite2d::DrawRect(r, ms_colourBottom, ms_colourBottom, ms_colourTop, ms_colourTop);
	}else{
		ms_horizonZ = CSprite::CalcHorizonCoors();

		// Draw top/bottom gradient
		float gradheight = globals.height/2.0f;
		float topedge = ms_horizonZ - gradheight;
		float botpos, toppos;
		if(ms_horizonZ > 0.0f && topedge < globals.height){
			ms_colourTop.r = topred;
			ms_colourTop.g = topgreen;
			ms_colourTop.b = topblue;
			ms_colourTop.a = alpha;
			ms_colourBottom.r = botred;
			ms_colourBottom.g = botgreen;
			ms_colourBottom.b = botblue;
			ms_colourBottom.a = alpha;

			if(ms_horizonZ < globals.height)
				botpos = ms_horizonZ;
			else{
				float f = (ms_horizonZ - globals.height)/gradheight;
				ms_colourBottom.r = topred*f + (1.0f-f)*botred;
				ms_colourBottom.g = topgreen*f + (1.0f-f)*botgreen;
				ms_colourBottom.b = topblue*f + (1.0f-f)*botblue;
				botpos = globals.height;
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
			Sprite2d::DrawRect(CRect(0, toppos, globals.width, botpos),
				ms_colourBottom, ms_colourBottom, ms_colourTop, ms_colourTop);
		}

		// draw the small stripe (whatever it's supposed to be)
		if(ms_horizonZ > -SMALLSTRIPHEIGHT && ms_horizonZ < globals.height){
			// Same colour as fog
			ms_colourTop.r = (topred + 2 * botred) / 3;
			ms_colourTop.g = (topgreen + 2 * botgreen) / 3;
			ms_colourTop.b = (topblue + 2 * botblue) / 3;
			Sprite2d::DrawRect(CRect(0, ms_horizonZ, globals.width, ms_horizonZ+SMALLSTRIPHEIGHT),
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

			if(topedge > globals.height)
				botpos = globals.height;
			else
				botpos = topedge;
			Sprite2d::DrawRect(CRect(0, 0, globals.width, botpos),
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
CClouds::RenderHorizon(void)
{
	if(UseDarkBackground())
		return;

	ms_colourBottom.a = 230;
	ms_colourTop.a = 80;

	if(ms_horizonZ >= globals.height)
		return;

	float z1 = ms_horizonZ + SMALLSTRIPHEIGHT;
	if(z1 > globals.height)
		z1 = globals.height;
	Sprite2d::DrawRectXLU(CRect(0, ms_horizonZ, globals.width, z1),
		ms_colourBottom, ms_colourBottom, ms_colourTop, ms_colourTop);

	// The rest is more or less from Vice City. III does strange things

	float z2 = z1 + HORIZSTRIPHEIGHT;
	if(z2 > globals.height)
		z2 = globals.height;

	// comes from ambient colour in VC
	ms_colourBkGrd.r = 100;
	ms_colourBkGrd.g = 100;
	ms_colourBkGrd.b = 100;
	ms_colourBkGrd.a = 255;

	Sprite2d::DrawRectXLU(CRect(0, z1, globals.width, z2),
		ms_colourBkGrd, ms_colourBkGrd, ms_colourTop, ms_colourTop);

	Sprite2d::DrawRectXLU(CRect(0, z2, globals.width, globals.height),
		ms_colourBkGrd, ms_colourBkGrd, ms_colourBkGrd, ms_colourBkGrd);
}
