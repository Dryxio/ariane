// Reversed from LCS mobile

struct CAnimBlendSequence;
struct CAnimBlendTree;
struct CAnimBlock;
struct CAnimBlendNode;
struct CAnimBlendAssociation;
struct CAnimBlendAssocGroup;

// A blend sequence is the animation data for one node
struct CAnimBlendSequence
{
};

// A blend tree is the animation data for a node tree
struct CAnimBlendTree
{
	CAnimBlendSequence *blendSequences;
	char name[24];
	int16 numSequences;
	int8 loadSpecial;     // still not sure about those two...copied from vc_anim
	int8 compressed;      //  where I already didn't know where they came from
	float totalLength;
#ifdef VCS
	int unknown;
#endif
};
//#ifdef LCS
//static_assert(sizeof(CAnimBlendTree) == 0x24, "CAnimBlendTree: error");
//#else
//static_assert(sizeof(CAnimBlendTree) == 0x28, "CAnimBlendTree: error");
//#endif

// A block is a collection of blend trees
struct CAnimBlock
{
	char name[20];
#ifdef PS2
	// not so sure...
	int32 isLoaded;
	int32 refCount;
#else
	// reversed from mobile
	int8 isLoaded;
	int16 refCount;
#endif
	int32 animBase;	// into the blend tree array
	int32 numAnims;
	void *chunkData;	// pointer to relocatable chunk, includes header
#ifdef VCS
	int32 unk0;
	int32 unk1;
#endif
};
//#ifdef LCS
//static_assert(sizeof(CAnimBlock) == 0x28, "CAnimBlock: error");
//#else
//static_assert(sizeof(CAnimBlock) == 0x30, "CAnimBlock: error");
//#endif

// A blend node holds the interpolation state for one node
struct CAnimBlendNode
{
	float theta0;
	float theta1;
	int frame0;
	int frame1;
	float time;
	CAnimBlendSequence *sequence;
	CAnimBlendAssociation *blendAssoc;
};
//static_assert(sizeof(CAnimBlendNode) == 0x1C, "CAnimBlendNode: error");

// A blend association holds the interpolation state for a tree
// only confirmed for mobile LCS
struct CAnimBlendAssociation
{
	void *next;
	void *prev;
	int16 flags;
	float timeStep;
	float blendAmount;
	int8 field_14;
	int32 numNodes;
	CAnimBlendNode *nodes;
	CAnimBlendTree *tree;
	float blendDelta;
	float currentTime;
	float speed;
	int16 animId;
	int16 groupId;
	int32 callbackType;
	int32 callback;
	int32 callbackArg;
	int8 field_40;
};
//static_assert(sizeof(CAnimBlendAssociation) == 0x44, "CAnimBlendAssociation: error");

struct CAnimBlendAssocGroup
{
	CAnimBlock *animBlock;
	CAnimBlendAssociation *assocList;
	int32 numAssociations;
	int32 baseIndex;	// index of first assoc in assocList
	int32 groupId;		// index in CAnimManager::m_aAnimAssocGroups
};
//static_assert(sizeof(CAnimBlendAssocGroup) == 0x14, "CAnimBlendAssocGroup: error");

// CAnimManager::ms_aAnimAssocDefinitions[84] at 0x399c68 in LCS SLES_541.35

struct AnimAssocInfo
{
	int32 animId;
	int32 flags;
};

struct AnimAssocDefinition
{
	const char *name;
	const char *blockName;
	int32 modelIndex;
	int32 animCount;
	const char **animNames;
	AnimAssocInfo *animInfoList;
};



struct CAnimAssoc_vcs
{
	int32 id;
	char name[44];	// this is most certainly too long but the data is uninitialized
	int8 unk1;
	// 3 bytes uninitialized
	int16 unk2;
	int16 unk3;
};
//static_assert(sizeof(CAnimAssoc_vcs) == 0x38, "CAnimAssoc_vcs: error");

struct CAnimAssocGroup_vcs
{
	char groupname[24];
	char blockname[20];
	int32 animBase;	// first ID, index into CAnimManager_vcs::associations
	int32 numAnims;
};
//static_assert(sizeof(CAnimAssocGroup_vcs) == 0x34, "CAnimAssocGroup_vcs: error");

struct CAnimManager
{
#ifdef VCS
	CAnimAssocGroup_vcs assocGroups[200];
	CAnimAssoc_vcs associations[990];
	int32 numAssocGroups;
	int32 numAssocs;
	int32 numUnk;
#endif
	CAnimBlendTree *m_aAnimations;
	CAnimBlock *m_aAnimBlocks;
	int32 m_numAnimations;
	int32 m_numAnimBlocks;
	CAnimBlendAssocGroup *m_aAnimAssocGroups;	// LCS: [84]
};
