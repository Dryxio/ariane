#include "III.h"

uchar  CClock::ms_nGameClockHours;
uchar  CClock::ms_nGameClockMinutes;
ushort CClock::ms_nGameClockSeconds;
uchar  CClock::ms_Stored_nGameClockHours;
uchar  CClock::ms_Stored_nGameClockMinutes;
ushort CClock::ms_Stored_nGameClockSeconds;
uint   CClock::ms_nMillisecondsPerGameMinute;
int    CClock::ms_nLastClockTick;
bool   CClock::ms_bClockHasBeenStored;

void
CClock::Initialise(uint scale)
{
	debug("Initialising CClock...\n");
	ms_nGameClockHours = 12;
	ms_nGameClockMinutes = 0;
	ms_nGameClockSeconds = 0;
	ms_nMillisecondsPerGameMinute = scale;
	ms_nLastClockTick = CTimer::m_snTimeInMilliseconds;
	ms_bClockHasBeenStored = false;
	debug("CClock ready\n");
}

void
CClock::Update(void)
{
	if(CPad::GetPad(0)->NewState.r1){
		ms_nGameClockMinutes += 8;
		ms_nLastClockTick = CTimer::m_snTimeInMilliseconds;
		if(ms_nGameClockMinutes >= 60){
			ms_nGameClockHours++;
			ms_nGameClockMinutes = 0;
			if(ms_nGameClockHours >= 24)
				ms_nGameClockHours = 0;
		}
	}else
	if(CTimer::m_snTimeInMilliseconds - ms_nLastClockTick >
	   ms_nMillisecondsPerGameMinute){
		ms_nGameClockMinutes++;
		ms_nLastClockTick += ms_nMillisecondsPerGameMinute;
		if(ms_nGameClockMinutes >= 60){
			ms_nGameClockHours++;
			ms_nGameClockMinutes = 0;
			if(ms_nGameClockHours >= 24)
				ms_nGameClockHours = 0;
				// TODO: stats days passed
		}
	}
	ms_nGameClockSeconds +=
			60
			* (CTimer::m_snTimeInMilliseconds - ms_nLastClockTick)
			/ ms_nMillisecondsPerGameMinute;
}

bool
CClock::GetIsTimeInRange(uchar h1, uchar h2)
{
	if(h1 > h2)
		return ms_nGameClockHours >= h1 || ms_nGameClockHours < h2;
	else
		return ms_nGameClockHours >= h1 && ms_nGameClockHours < h2;
}
