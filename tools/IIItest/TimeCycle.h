#ifndef _TIMECYCLE_H_
#define _TIMECYCLE_H_

class CTimeCycle {
public:
	static int   m_nAmbientRed[NUMHOURS][NUMWEATHERS];
	static float m_fCurrentAmbientRed;
	static int   m_nAmbientGreen[NUMHOURS][NUMWEATHERS];
	static float m_fCurrentAmbientGreen;
	static int   m_nAmbientBlue[NUMHOURS][NUMWEATHERS];
	static float m_fCurrentAmbientBlue;
	static int   m_nDirectionalRed[NUMHOURS][NUMWEATHERS];
	static float m_fCurrentDirectionalRed;
	static int   m_nDirectionalGreen[NUMHOURS][NUMWEATHERS];
	static float m_fCurrentDirectionalGreen;
	static int   m_nDirectionalBlue[NUMHOURS][NUMWEATHERS];
	static float m_fCurrentDirectionalBlue;
	static int   m_nSkyTopRed[NUMHOURS][NUMWEATHERS];
	static int   m_nCurrentSkyTopRed;
	static int   m_nSkyTopGreen[NUMHOURS][NUMWEATHERS];
	static int   m_nCurrentSkyTopGreen;
	static int   m_nSkyTopBlue[NUMHOURS][NUMWEATHERS];
	static int   m_nCurrentSkyTopBlue;
	static int   m_nSkyBottomRed[NUMHOURS][NUMWEATHERS];
	static int   m_nCurrentSkyBottomRed;
	static int   m_nSkyBottomGreen[NUMHOURS][NUMWEATHERS];
	static int   m_nCurrentSkyBottomGreen;
	static int   m_nSkyBottomBlue[NUMHOURS][NUMWEATHERS];
	static int   m_nCurrentSkyBottomBlue;
	static int   m_nSunCoreRed[NUMHOURS][NUMWEATHERS];
	static int   m_nCurrentSunCoreRed;
	static int   m_nSunCoreGreen[NUMHOURS][NUMWEATHERS];
	static int   m_nCurrentSunCoreGreen;
	static int   m_nSunCoreBlue[NUMHOURS][NUMWEATHERS];
	static int   m_nCurrentSunCoreBlue;
	static int   m_nSunCoronaRed[NUMHOURS][NUMWEATHERS];
	static int   m_nCurrentSunCoronaRed;
	static int   m_nSunCoronaGreen[NUMHOURS][NUMWEATHERS];
	static int   m_nCurrentSunCoronaGreen;
	static int   m_nSunCoronaBlue[NUMHOURS][NUMWEATHERS];
	static int   m_nCurrentSunCoronaBlue;
	static float m_fSunSize[NUMHOURS][NUMWEATHERS];
	static float m_fCurrentSunSize;
	static float m_fSpriteSize[NUMHOURS][NUMWEATHERS];
	static float m_fCurrentSpriteSize;
	static float m_fSpriteBrightness[NUMHOURS][NUMWEATHERS];
	static float m_fCurrentSpriteBrightness;
	static int   m_nShadowStrength[NUMHOURS][NUMWEATHERS];
	static int   m_nCurrentShadowStrength;
	static int   m_nLightShadowStrength[NUMHOURS][NUMWEATHERS];
	static int   m_nCurrentLightShadowStrength;
	static int   m_nTreeShadowStrength[NUMHOURS][NUMWEATHERS];
	static int   m_nCurrentTreeShadowStrength;
	static float m_fFogStart[NUMHOURS][NUMWEATHERS];
	static float m_fCurrentFogStart;
	static float m_fFarClip[NUMHOURS][NUMWEATHERS];
	static float m_fCurrentFarClip;
	static float m_fLightsOnGroundBrightness[NUMHOURS][NUMWEATHERS];
	static float m_fCurrentLightsOnGroundBrightness;
	static int   m_nLowCloudsRed[NUMHOURS][NUMWEATHERS];
	static int   m_nCurrentLowCloudsRed;
	static int   m_nLowCloudsGreen[NUMHOURS][NUMWEATHERS];
	static int   m_nCurrentLowCloudsGreen;
	static int   m_nLowCloudsBlue[NUMHOURS][NUMWEATHERS];
	static int   m_nCurrentLowCloudsBlue;
	static int   m_nFluffyCloudsTopRed[NUMHOURS][NUMWEATHERS];
	static int   m_nCurrentFluffyCloudsTopRed;
	static int   m_nFluffyCloudsTopGreen[NUMHOURS][NUMWEATHERS];
	static int   m_nCurrentFluffyCloudsTopGreen;
	static int   m_nFluffyCloudsTopBlue[NUMHOURS][NUMWEATHERS];
	static int   m_nCurrentFluffyCloudsTopBlue;
	static int   m_nFluffyCloudsBottomRed[NUMHOURS][NUMWEATHERS];
	static int   m_nCurrentFluffyCloudsBottomRed;
	static int   m_nFluffyCloudsBottomGreen[NUMHOURS][NUMWEATHERS];
	static int   m_nCurrentFluffyCloudsBottomGreen;
	static int   m_nFluffyCloudsBottomBlue[NUMHOURS][NUMWEATHERS];
	static int   m_nCurrentFluffyCloudsBottomBlue;
	static float m_fBlurRed[NUMHOURS][NUMWEATHERS];
	static float m_fCurrentBlurRed;
	static float m_fBlurGreen[NUMHOURS][NUMWEATHERS];
	static float m_fCurrentBlurGreen;
	static float m_fBlurBlue[NUMHOURS][NUMWEATHERS];
	static float m_fCurrentBlurBlue;
	static float m_fBlurAlpha[NUMHOURS][NUMWEATHERS];
	static float m_fCurrentBlurAlpha;
	static int   m_nCurrentFogColourRed;
	static int   m_nCurrentFogColourGreen;
	static int   m_nCurrentFogColourBlue;

	static int m_CurrentStoredValue;
	static CVector m_VectorToSun[16];

	static void Initialise(void);
	static void Update(void);
};

#endif
