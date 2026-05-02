#include "III.h"

uint32   CTimer::m_snPreviousTimeInMilliseconds;
uint32   CTimer::m_snTimeInMilliseconds;
uint32  CTimer::m_FrameCounter;
float CTimer::ms_fTimeScale;
float CTimer::ms_fTimeStep;
bool  CTimer::m_UserPause;
bool  CTimer::m_CodePause;

float CTimer::fpsHistory[100];
float CTimer::fpsTotal;
int CTimer::fpsN;
int CTimer::fpsI;
float CTimer::avgTimeStep;

int oldPcTimer;

void
CTimer::Initialise(void)
{
	debug("Initialising CTimer...\n");
	ms_fTimeScale = 1.0f;
	ms_fTimeStep = 1.0f;
	m_UserPause = false;
	m_CodePause = false;
	m_snPreviousTimeInMilliseconds = 0;
	m_snTimeInMilliseconds = m_snPreviousTimeInMilliseconds;
	m_FrameCounter = 0;
	oldPcTimer = plGetTimeInMS();

	fpsTotal = 0.0f;
	fpsN = 0;
	fpsI = 0;

	debug("CTimer ready\n");
}

void
CTimer::Update(void)
{



	m_snPreviousTimeInMilliseconds = m_snTimeInMilliseconds;
	int newtime = plGetTimeInMS();

	// update FPS average
	int step = newtime - oldPcTimer;
	fpsTotal += step - fpsHistory[fpsI];
	fpsHistory[fpsI] = step;
	fpsI = (fpsI+1) % 100;
	fpsN = fpsI > fpsN ? fpsI : fpsN;
	avgTimeStep = fpsTotal / fpsN;

	double diff = step * ms_fTimeScale;
	oldPcTimer = newtime;
	if(m_UserPause || m_CodePause)
		ms_fTimeStep = 0.0f;
	else{
		m_snTimeInMilliseconds += diff;
		ms_fTimeStep = diff*50.0f/1000.0f;
	}
	m_FrameCounter++;
}

void
CTimer::Stop(void)
{
	m_snPreviousTimeInMilliseconds = m_snTimeInMilliseconds;
}
