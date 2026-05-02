#ifndef _STREAMING_H_
#define _STREAMING_H_

#include "FileMgr.h"

extern int islandLODindust;
extern int islandLODcomInd;
extern int islandLODcomSub;
extern int islandLODsubInd;
extern int islandLODsubCom;
extern CEntity *pIslandLODsubComEntity;
extern CEntity *pIslandLODsubIndEntity;
extern CEntity *pIslandLODcomSubEntity;
extern CEntity *pIslandLODcomIndEntity;
extern CEntity *pIslandLODindustEntity;

class CdStream
{
public:
	static int numImages;
	static char imageNames[NUMCDIMAGES][64];
	static FileHandle images[NUMCDIMAGES];

	static void init();
	static void addImage(const char *name);
	static void removeImages(void);
	static void read(char *buf, uint pos, uint size);
};

class CDirectory
{
public:
	struct DirectoryInfo {
		uint32 position;
		uint32 size;
		char name[24];
	};
	DirectoryInfo *m_entries;
	int m_maxEntries;
	int m_numEntries;

	CDirectory(int size);
	void AddItem(DirectoryInfo *dirinfo);
};

enum StreamFlags
{
	STREAM_DONT_REMOVE = 0x01,
	STREAM_SCRIPTOWNED = 0x02,
	STREAM_DEPENDENCY  = 0x04,
	STREAM_PRIORITY    = 0x08,
	STREAM_NOFADE      = 0x10,
};

enum StreamLoadState
{
	STREAM_NOTLOADED = 0,
	STREAM_LOADED    = 1,
	STREAM_INQUEUE   = 2,
	STREAM_READING   = 3,	// what is this?
	STREAM_BIGFILE   = 4,
};

class CStreamingInfo
{
public:
	CStreamingInfo *m_next;
	CStreamingInfo *m_prev;
	uchar m_loadState;
	uchar m_flags;

	short m_nextID;
	uint  m_position;
	uint  m_size;

	bool GetCdPosnAndSize(uint *pos, uint *size);
	void SetCdPosnAndSize(uint pos, uint size);
	void AddToList(CStreamingInfo *link);
	void RemoveFromList(void);
};

enum {
	MODELOFFSET = 0,
	TXDOFFSET = MODELOFFSET+MODELINFOSIZE,
	NUMSTREAMINFO = TXDOFFSET+TXDSTORESIZE
};

class CStreaming
{
public:
	static CStreamingInfo ms_aInfoForModel[NUMSTREAMINFO];
	static CDirectory *ms_pExtraObjectsDir;
	static CStreamingInfo ms_startLoadedList, ms_endLoadedList;
	static CStreamingInfo ms_startRequestedList, ms_endRequestedList;
	static int ms_numModelsRequested;
	static int ms_numPriorityRequests;
	static uint ms_streamingBufferSize;
	static char *ms_pStreamingBuffer;
	static bool ms_hasLoadedLODs;

	static void Init(void);
	static void LoadCdDirectory(void);
	static void LoadCdDirectory(const char *dirname, int n);
	static void ConvertBufferToObject(char *buffer, int id);
	static void RequestModel(int id, int flags);
	static void RequestInitialVehicles(void);
	static void RequestInitialPeds(void);
	static void HaveAllBigBuildingsLoaded(eLevelName level);
	static void RequestBigBuildings(eLevelName level);
	static void RequestIslands(eLevelName level);
	static void RequestAllBuildings(eLevelName level);

	static void RemoveModel(int id);
	static void RemoveIslandsNotUsed(eLevelName level);
	static void RemoveBigBuildings(eLevelName level);
	static void RemoveBuildings(eLevelName level);
	static void RemoveUnusedBigBuildings(eLevelName level);
	static void RemoveUnusedBuildings(eLevelName level);

	static void LoadAllRequestedModels(void);
	static void dumpRequestList(void);
	static int getNextFile(void);

	static CStreamingInfo *Model(int n) { return &ms_aInfoForModel[MODELOFFSET+n]; };
	static CStreamingInfo *Txd(int n) { return &ms_aInfoForModel[TXDOFFSET+n]; };
};

#endif
