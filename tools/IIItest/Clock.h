#ifndef _CLOCK_H_
#define _CLOCK_H_

class CClock
{
public:
	static uchar  ms_nGameClockHours;
	static uchar  ms_nGameClockMinutes;
	static ushort ms_nGameClockSeconds;
	static uchar  ms_Stored_nGameClockHours;
	static uchar  ms_Stored_nGameClockMinutes;
	static ushort ms_Stored_nGameClockSeconds;
	static uint   ms_nMillisecondsPerGameMinute;
	static int    ms_nLastClockTick;
	static bool   ms_bClockHasBeenStored;

	static void Initialise(uint scale);
	static void Update(void);
	static bool GetIsTimeInRange(uchar h1, uchar h2);
};

#endif
