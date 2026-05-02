#ifndef _WEATHER_H_
#define _WEATHER_H_

class CWeather
{
public:
	static float InterpolationValue;
	static short NewWeatherType;
	static short OldWeatherType;
	static short ForcedWeatherType;
	static float WetRoads;
	static float Wind;
	static float CloudCoverage;
	static float Foggyness;
	static float Rainbow;

	static void Init(void);
	static void Update(void);
};

#endif
