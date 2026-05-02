#ifndef _GENERAL_H_
#define _GENERAL_H_

class CGeneral
{
public:
	static float GetATanOfXY(float x, float y){
		if(y >= 0.0f) return atan2(x, y);
		return atan2(x, y) + 2*PI;
	}
	static uint16 GetRandomNumber(void){
		return ps2_rand();
	}
};

#endif
