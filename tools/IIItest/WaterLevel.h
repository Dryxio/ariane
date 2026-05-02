#ifndef _WATERLEVEL_H_
#define _WATERLEVEL_H_

class CWaterLevel
{
	static int ms_nNoOfWaterLevels;
	static float ms_aWaterZs[48];
	static CRect ms_aWaterRects[48];
	static uint8 aWaterBlockList[64][64];
	static uint8 aWaterFineBlockList[128][128];
public:
	static void Initialise(const char *unusedfilename);
	static void RenderWater(void);
	static void RenderAndEmptyRenderBuffer(void);
	static void RenderOneFlatSmallWaterPoly(float x, float y, float z, rw::RGBA const &color);
	static void RenderOneFlatLargeWaterPoly(float x, float y, float z, rw::RGBA const &color);
};

#endif
