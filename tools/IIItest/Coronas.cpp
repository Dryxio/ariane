#include "III.h"
#include "Coronas.h"
#include "Camera.h"

rw::Texture *gpCoronaTexture[9];

bool CCoronas::bSmallMoon;
float CCoronas::SunScreenX;
float CCoronas::SunScreenY;
bool CCoronas::SunBlockedByClouds;

void
CCoronas::Init(void)
{
	static char texnames[9][32] = {
		"coronastar",
		"corona",
		"coronamoon",
		"coronareflect",
		"coronaheadlightline",
		"coronahex",
		"coronacircle",
		"coronaringa",
		"streek",
	};

	CTxdStore::PushCurrentTxd();
	CTxdStore::SetCurrentTxd(CTxdStore::FindTxdSlot("particle"));
	for(int i = 0; i < 9; i++)
		if(gpCoronaTexture[i] == nil)
			gpCoronaTexture[i] = rw::Texture::read(texnames[i], nil);
	CTxdStore::PopCurrentTxd();

	// TODO
}

void
CCoronas::DoSunAndMoon(void)
{
	CVector sunpos = CTimeCycle::m_VectorToSun[CTimeCycle::m_CurrentStoredValue]*150.f + TheCamera.GetPosition();
	// TODO: register coronas

	rw::V3d screenpos;
	float w, h;
	if(CSprite::CalcScreenCoors(*(rw::V3d*)&sunpos, &screenpos, &w, &h, true)){
		SunScreenX = screenpos.x;
		SunScreenY = screenpos.y;
	}else{
		SunScreenX = 1000000.0f;
		SunScreenY = 1000000.0f;
	}
}
