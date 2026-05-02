
struct sChunkHeader
{
	uint32   ident;
	bool32   shrink;
	uint32   fileSize;
	uint32   dataSize;
	uint32   relocTab;
	uint32   numRelocs;
	uint32   globalTab;
	uint16   numClasses;
	uint16   numFuncs;
};

class cRelocatableChunk
{
public:
	uint32	ident;
	bool32	shrink;

	cRelocatableChunk(uint32 ident, bool32 shrink) : ident(ident), shrink(shrink) { }
	void Fixup(sChunkHeader &header, void *data);
	void Fixup(void *data);
	void *Shrink(sChunkHeader &header, void *data);
	void *Shrink(void *data);
};
