#ifndef _TXDSTORE_H_
#define _TXDSTORE_H_

struct TxdDef {
	rw::TexDictionary *texDict;
	int refCount;
	char name[20];
};

class CTxdStore
{
	static CPool<TxdDef,TxdDef> *ms_pTxdPool;
	static rw::TexDictionary *ms_pStoredTxd;
public:
	static void Initialize(void);
	static int AddTxdSlot(const char *name);
	static int FindTxdSlot(const char *name);
	static char *GetTxdName(int slot);
	static void PushCurrentTxd(void);
	static void PopCurrentTxd(void);
	static void SetCurrentTxd(int slot);
	static void Create(int slot);
	static void AddRef(int slot);
	static void RemoveRef(int slot);
	static void RemoveRefWithoutDelete(int slot);
	static bool LoadTxd(int slot, rw::Stream *stream);
	static bool LoadTxd(int slot, const char *filename);
	static void RemoveTxd(int slot);

	static TxdDef *GetSlot(int slot) { return ms_pTxdPool->GetSlot(slot); }
	static bool isTxdLoaded(int slot);
};

#endif
