#include "III.h"

#define X(name, var) short var;
	MODELINDICES
#undef X

void
InitModelIndices(void)
{
#define X(name, var) var = -1;
	MODELINDICES
#undef X
}

void
MatchModelString(const char *modelname, short id)
{
#define X(name, var) \
	if(strcmp(name, modelname) == 0){ \
		var = id; \
		return; \
	}
	MODELINDICES
#undef X

}

