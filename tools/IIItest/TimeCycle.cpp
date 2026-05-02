#include "III.h"

int   CTimeCycle::m_nAmbientRed[NUMHOURS][NUMWEATHERS];
float CTimeCycle::m_fCurrentAmbientRed;
int   CTimeCycle::m_nAmbientGreen[NUMHOURS][NUMWEATHERS];
float CTimeCycle::m_fCurrentAmbientGreen;
int   CTimeCycle::m_nAmbientBlue[NUMHOURS][NUMWEATHERS];
float CTimeCycle::m_fCurrentAmbientBlue;
int   CTimeCycle::m_nDirectionalRed[NUMHOURS][NUMWEATHERS];
float CTimeCycle::m_fCurrentDirectionalRed;
int   CTimeCycle::m_nDirectionalGreen[NUMHOURS][NUMWEATHERS];
float CTimeCycle::m_fCurrentDirectionalGreen;
int   CTimeCycle::m_nDirectionalBlue[NUMHOURS][NUMWEATHERS];
float CTimeCycle::m_fCurrentDirectionalBlue;
int   CTimeCycle::m_nSkyTopRed[NUMHOURS][NUMWEATHERS];
int   CTimeCycle::m_nCurrentSkyTopRed;
int   CTimeCycle::m_nSkyTopGreen[NUMHOURS][NUMWEATHERS];
int   CTimeCycle::m_nCurrentSkyTopGreen;
int   CTimeCycle::m_nSkyTopBlue[NUMHOURS][NUMWEATHERS];
int   CTimeCycle::m_nCurrentSkyTopBlue;
int   CTimeCycle::m_nSkyBottomRed[NUMHOURS][NUMWEATHERS];
int   CTimeCycle::m_nCurrentSkyBottomRed;
int   CTimeCycle::m_nSkyBottomGreen[NUMHOURS][NUMWEATHERS];
int   CTimeCycle::m_nCurrentSkyBottomGreen;
int   CTimeCycle::m_nSkyBottomBlue[NUMHOURS][NUMWEATHERS];
int   CTimeCycle::m_nCurrentSkyBottomBlue;
int   CTimeCycle::m_nSunCoreRed[NUMHOURS][NUMWEATHERS];
int   CTimeCycle::m_nCurrentSunCoreRed;
int   CTimeCycle::m_nSunCoreGreen[NUMHOURS][NUMWEATHERS];
int   CTimeCycle::m_nCurrentSunCoreGreen;
int   CTimeCycle::m_nSunCoreBlue[NUMHOURS][NUMWEATHERS];
int   CTimeCycle::m_nCurrentSunCoreBlue;
int   CTimeCycle::m_nSunCoronaRed[NUMHOURS][NUMWEATHERS];
int   CTimeCycle::m_nCurrentSunCoronaRed;
int   CTimeCycle::m_nSunCoronaGreen[NUMHOURS][NUMWEATHERS];
int   CTimeCycle::m_nCurrentSunCoronaGreen;
int   CTimeCycle::m_nSunCoronaBlue[NUMHOURS][NUMWEATHERS];
int   CTimeCycle::m_nCurrentSunCoronaBlue;
float CTimeCycle::m_fSunSize[NUMHOURS][NUMWEATHERS];
float CTimeCycle::m_fCurrentSunSize;
float CTimeCycle::m_fSpriteSize[NUMHOURS][NUMWEATHERS];
float CTimeCycle::m_fCurrentSpriteSize;
float CTimeCycle::m_fSpriteBrightness[NUMHOURS][NUMWEATHERS];
float CTimeCycle::m_fCurrentSpriteBrightness;
int   CTimeCycle::m_nShadowStrength[NUMHOURS][NUMWEATHERS];
int   CTimeCycle::m_nCurrentShadowStrength;
int   CTimeCycle::m_nLightShadowStrength[NUMHOURS][NUMWEATHERS];
int   CTimeCycle::m_nCurrentLightShadowStrength;
int   CTimeCycle::m_nTreeShadowStrength[NUMHOURS][NUMWEATHERS];
int   CTimeCycle::m_nCurrentTreeShadowStrength;
float CTimeCycle::m_fFogStart[NUMHOURS][NUMWEATHERS];
float CTimeCycle::m_fCurrentFogStart;
float CTimeCycle::m_fFarClip[NUMHOURS][NUMWEATHERS];
float CTimeCycle::m_fCurrentFarClip;
float CTimeCycle::m_fLightsOnGroundBrightness[NUMHOURS][NUMWEATHERS];
float CTimeCycle::m_fCurrentLightsOnGroundBrightness;
int   CTimeCycle::m_nLowCloudsRed[NUMHOURS][NUMWEATHERS];
int   CTimeCycle::m_nCurrentLowCloudsRed;
int   CTimeCycle::m_nLowCloudsGreen[NUMHOURS][NUMWEATHERS];
int   CTimeCycle::m_nCurrentLowCloudsGreen;
int   CTimeCycle::m_nLowCloudsBlue[NUMHOURS][NUMWEATHERS];
int   CTimeCycle::m_nCurrentLowCloudsBlue;
int   CTimeCycle::m_nFluffyCloudsTopRed[NUMHOURS][NUMWEATHERS];
int   CTimeCycle::m_nCurrentFluffyCloudsTopRed;
int   CTimeCycle::m_nFluffyCloudsTopGreen[NUMHOURS][NUMWEATHERS];
int   CTimeCycle::m_nCurrentFluffyCloudsTopGreen;
int   CTimeCycle::m_nFluffyCloudsTopBlue[NUMHOURS][NUMWEATHERS];
int   CTimeCycle::m_nCurrentFluffyCloudsTopBlue;
int   CTimeCycle::m_nFluffyCloudsBottomRed[NUMHOURS][NUMWEATHERS];
int   CTimeCycle::m_nCurrentFluffyCloudsBottomRed;
int   CTimeCycle::m_nFluffyCloudsBottomGreen[NUMHOURS][NUMWEATHERS];
int   CTimeCycle::m_nCurrentFluffyCloudsBottomGreen;
int   CTimeCycle::m_nFluffyCloudsBottomBlue[NUMHOURS][NUMWEATHERS];
int   CTimeCycle::m_nCurrentFluffyCloudsBottomBlue;
float CTimeCycle::m_fBlurRed[NUMHOURS][NUMWEATHERS];
float CTimeCycle::m_fCurrentBlurRed;
float CTimeCycle::m_fBlurGreen[NUMHOURS][NUMWEATHERS];
float CTimeCycle::m_fCurrentBlurGreen;
float CTimeCycle::m_fBlurBlue[NUMHOURS][NUMWEATHERS];
float CTimeCycle::m_fCurrentBlurBlue;
float CTimeCycle::m_fBlurAlpha[NUMHOURS][NUMWEATHERS];
float CTimeCycle::m_fCurrentBlurAlpha;
int   CTimeCycle::m_nCurrentFogColourRed;
int   CTimeCycle::m_nCurrentFogColourGreen;
int   CTimeCycle::m_nCurrentFogColourBlue;

int CTimeCycle::m_CurrentStoredValue;
CVector CTimeCycle::m_VectorToSun[16];

void
CTimeCycle::Initialise(void)
{
	FileHandle file;
	char *line;
	CFileMgr::SetDir("DATA");
	file = CFileMgr::OpenFile("TIMECYC.DAT", "rb");
	CFileMgr::SetDir("");
	if(file == nil)
		return;
	int h = 0, w = 0;
	while(line = CFileLoader::LoadLine(file)){
		if(line[0] == '/' && line[1] == '/')
			continue;
		sscanf(line, "%d %d %d  %d %d %d  %d %d %d  %d %d %d "
		             "%d %d %d  %d %d %d  %f %f %f %d %d %d %f %f %f "
		             "%d %d %d  %d %d %d  %d %d %d  %f %f %f %f",
		       &CTimeCycle::m_nAmbientRed[h][w],
		       &CTimeCycle::m_nAmbientGreen[h][w],
		       &CTimeCycle::m_nAmbientBlue[h][w],
		       &CTimeCycle::m_nDirectionalRed[h][w],
		       &CTimeCycle::m_nDirectionalGreen[h][w],
		       &CTimeCycle::m_nDirectionalBlue[h][w],
		       &CTimeCycle::m_nSkyTopRed[h][w],
		       &CTimeCycle::m_nSkyTopGreen[h][w],
		       &CTimeCycle::m_nSkyTopBlue[h][w],
		       &CTimeCycle::m_nSkyBottomRed[h][w],
		       &CTimeCycle::m_nSkyBottomGreen[h][w],
		       &CTimeCycle::m_nSkyBottomBlue[h][w],
		       &CTimeCycle::m_nSunCoreRed[h][w],
		       &CTimeCycle::m_nSunCoreGreen[h][w],
		       &CTimeCycle::m_nSunCoreBlue[h][w],
		       &CTimeCycle::m_nSunCoronaRed[h][w],
		       &CTimeCycle::m_nSunCoronaGreen[h][w],
		       &CTimeCycle::m_nSunCoronaBlue[h][w],
		       &CTimeCycle::m_fSunSize[h][w],
		       &CTimeCycle::m_fSpriteSize[h][w],
		       &CTimeCycle::m_fSpriteBrightness[h][w],
		       &CTimeCycle::m_nShadowStrength[h][w],
		       &CTimeCycle::m_nLightShadowStrength[h][w],
		       &CTimeCycle::m_nTreeShadowStrength[h][w],
		       &CTimeCycle::m_fFarClip[h][w],
		       &CTimeCycle::m_fFogStart[h][w],
		       &CTimeCycle::m_fLightsOnGroundBrightness[h][w],
		       &CTimeCycle::m_nLowCloudsRed[h][w],
		       &CTimeCycle::m_nLowCloudsGreen[h][w],
		       &CTimeCycle::m_nLowCloudsBlue[h][w],
		       &CTimeCycle::m_nFluffyCloudsTopRed[h][w],
		       &CTimeCycle::m_nFluffyCloudsTopGreen[h][w],
		       &CTimeCycle::m_nFluffyCloudsTopBlue[h][w],
		       &CTimeCycle::m_nFluffyCloudsBottomRed[h][w],
		       &CTimeCycle::m_nFluffyCloudsBottomGreen[h][w],
		       &CTimeCycle::m_nFluffyCloudsBottomBlue[h][w],
		       &CTimeCycle::m_fBlurRed[h][w],
		       &CTimeCycle::m_fBlurGreen[h][w],
		       &CTimeCycle::m_fBlurBlue[h][w],
		       &CTimeCycle::m_fBlurAlpha[h][w]);
		h++;
		if(h == NUMHOURS){
			h = 0;
			w++;
		}
		if(w == NUMWEATHERS)
			break;
	}
	CFileMgr::CloseFile(file);
}

#define INTERP(v) v[h1][w1]*c0 + v[h2][w1]*c1 + v[h1][w2]*c2 + v[h2][w2]*c3;

void
CTimeCycle::Update(void)
{
	int h1 = CClock::ms_nGameClockHours;
	int h2 = (h1+1)%24;
	int w1 = CWeather::OldWeatherType;
	int w2 = CWeather::NewWeatherType;
	float timeInterp = CClock::ms_nGameClockMinutes/60.0f;
	// coefficients for a bilinear interpolation
	float c0 = (1.0f-timeInterp) * (1.0f-CWeather::InterpolationValue);
	float c1 = timeInterp * (1.0f-CWeather::InterpolationValue);
	float c2 = (1.0f-timeInterp) * CWeather::InterpolationValue;
	float c3 = timeInterp * CWeather::InterpolationValue;

	m_fCurrentAmbientRed = INTERP(m_nAmbientRed);
	m_fCurrentAmbientGreen = INTERP(m_nAmbientGreen);
	m_fCurrentAmbientBlue = INTERP(m_nAmbientBlue);
	m_fCurrentAmbientRed /= 255.0f;
	m_fCurrentAmbientGreen /= 255.0f;
	m_fCurrentAmbientBlue /= 255.0f;
	m_fCurrentDirectionalRed = INTERP(m_nDirectionalRed);
	m_fCurrentDirectionalGreen = INTERP(m_nDirectionalGreen);
	m_fCurrentDirectionalBlue = INTERP(m_nDirectionalBlue);
	m_fCurrentDirectionalRed /= 255.0f;
	m_fCurrentDirectionalGreen /= 255.0f;
	m_fCurrentDirectionalBlue /= 255.0f;
	m_nCurrentSkyTopRed = INTERP(m_nSkyTopRed);
	m_nCurrentSkyTopGreen = INTERP(m_nSkyTopGreen);
	m_nCurrentSkyTopBlue = INTERP(m_nSkyTopBlue);
	m_nCurrentSkyBottomRed = INTERP(m_nSkyBottomRed);
	m_nCurrentSkyBottomGreen = INTERP(m_nSkyBottomGreen);
	m_nCurrentSkyBottomBlue = INTERP(m_nSkyBottomBlue);
	m_nCurrentSunCoreRed = INTERP(m_nSunCoreRed);
	m_nCurrentSunCoreGreen = INTERP(m_nSunCoreGreen);
	m_nCurrentSunCoreBlue = INTERP(m_nSunCoreBlue);
	m_nCurrentSunCoronaRed = INTERP(m_nSunCoronaRed);
	m_nCurrentSunCoronaGreen = INTERP(m_nSunCoronaGreen);
	m_nCurrentSunCoronaBlue = INTERP(m_nSunCoronaBlue);
	m_fCurrentSunSize = INTERP(m_fSunSize);
	m_fCurrentSpriteSize = INTERP(m_fSpriteSize);
	m_fCurrentSpriteBrightness = INTERP(m_fSpriteBrightness);
	m_nCurrentShadowStrength = INTERP(m_nShadowStrength);
	m_nCurrentLightShadowStrength = INTERP(m_nLightShadowStrength);
	m_nCurrentTreeShadowStrength = INTERP(m_nTreeShadowStrength);
	m_fCurrentFogStart = INTERP(m_fFogStart);
	m_fCurrentFarClip = INTERP(m_fFarClip);
	m_fCurrentLightsOnGroundBrightness = INTERP(m_fLightsOnGroundBrightness);
	m_nCurrentLowCloudsRed = INTERP(m_nLowCloudsRed);
	m_nCurrentLowCloudsGreen = INTERP(m_nLowCloudsGreen);
	m_nCurrentLowCloudsBlue = INTERP(m_nLowCloudsBlue);
	m_nCurrentFluffyCloudsTopRed = INTERP(m_nFluffyCloudsTopRed);
	m_nCurrentFluffyCloudsTopGreen = INTERP(m_nFluffyCloudsTopGreen);
	m_nCurrentFluffyCloudsTopBlue = INTERP(m_nFluffyCloudsTopBlue);
	m_nCurrentFluffyCloudsBottomRed = INTERP(m_nFluffyCloudsBottomRed);
	m_nCurrentFluffyCloudsBottomGreen = INTERP(m_nFluffyCloudsBottomGreen);
	m_nCurrentFluffyCloudsBottomBlue = INTERP(m_nFluffyCloudsBottomBlue);
	m_fCurrentBlurRed = INTERP(m_fBlurRed);
	m_fCurrentBlurGreen = INTERP(m_fBlurGreen);
	m_fCurrentBlurBlue = INTERP(m_fBlurBlue);
	m_fCurrentBlurAlpha = INTERP(m_fBlurAlpha);

	m_nCurrentFogColourRed = (m_nCurrentSkyTopRed + 2*m_nCurrentSkyBottomRed)/3;
	m_nCurrentFogColourGreen = (m_nCurrentSkyTopGreen + 2*m_nCurrentSkyBottomGreen)/3;
	m_nCurrentFogColourBlue = (m_nCurrentSkyTopBlue + 2*m_nCurrentSkyBottomBlue)/3;

	m_CurrentStoredValue = (m_CurrentStoredValue+1)&0xF;

	float t = (CClock::ms_nGameClockMinutes + 60 * CClock::ms_nGameClockHours)/1440.0f * 2.0f*PI;
	m_VectorToSun[m_CurrentStoredValue] = CVector(sin(t), 1.0f, 0.2f - cos(t));
	m_VectorToSun[m_CurrentStoredValue].Normalise();
}
