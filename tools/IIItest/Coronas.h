#ifndef _CORONAS_H_
#define _CORONAS_H_

extern rw::Texture *gpCoronaTexture[9];

class CCoronas
{
public:
	static bool bSmallMoon;
	static float SunScreenX;
	static float SunScreenY;
	static bool SunBlockedByClouds;

	static void Init(void);
	static void DoSunAndMoon(void);
};

#endif
