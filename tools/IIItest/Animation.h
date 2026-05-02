#ifndef _ANIMATION_H_
#define _ANIMATION_H_

struct AnimAssocDefinition
{
	char *name;
	char *blockName;
	int modelIndex;
	int animCount;
	char **animNames;
	struct Info {
		int animId;
		int flags;
	} *animInfoList;
};

class CAnimManager
{
public:
	static void LoadAnimGroups(void);
	static char *GetAnimGroupName(int i);

	static AnimAssocDefinition *ms_aAnimAssocDefinitions; // 25 in III
	static int ms_numAnimAssocDefinitions;
};

#endif
