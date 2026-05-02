#ifndef _GAME_H_
#define _GAME_H_

class CGame
{
public:
	static eLevelName currLevel;
	static void Initialise(void);
	static void InitialiseRW(void);
	static void Process(void);
};

#endif
