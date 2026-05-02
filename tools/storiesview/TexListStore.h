
typedef CPool<TexListDef,TexListDef> TexListPool;


class CTexListStore
{
	static TexListPool *ms_pTexListPool;
	static rw::TexDictionary *ms_pStoredTexList;
public:
	static void Initialize(TexListPool *pool);
	static int AddTexListSlot(const char *name);
	static int FindTexListSlot(const char *name);
	static char *GetTexListName(int slot);
	static void PushCurrentTexList(void);
	static void PopCurrentTexList(void);
	static void SetCurrentTexList(int slot);
	static void Create(int slot);
	static void AddRef(int slot);
	static void RemoveRef(int slot);
	static void RemoveRefWithoutDelete(int slot);
	static bool LoadTexList(int slot, rw::Stream *stream);
	static bool LoadTexList(int slot, const char *filename);
	static void RemoveTexList(int slot);

	static TexListDef *GetSlot(int slot) { return ms_pTexListPool->GetSlot(slot); }
	static bool isTexListLoaded(int slot);

	static rw::TexDictionary *CTexListStore::GetRWTxd(int slot);
};
