#ifndef _CLOUDS_H_
#define _CLOUDS_H_

class CClouds
{
public:
	static float CloudRotation;
	static float IndividualRotation;

	static void Init(void);
	static void Update(void);
	static void Render(void);
	static void RenderBackground(int16 topred, int16 topgreen, int16 topblue,
		int16 botred, int16 botgreen, int16 botblue, int16 alpha);
	static void RenderHorizon(void);

	static float ms_cameraRoll;
	static float ms_horizonZ;
	static CRGBA ms_colourTop;
	static CRGBA ms_colourBottom;
	static CRGBA ms_colourBkGrd;
};

#endif
