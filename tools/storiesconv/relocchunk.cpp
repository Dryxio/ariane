#include <rw.h>
using namespace rw;

#include "relocchunk.h"

#include <cstdlib>

void
cRelocatableChunk::Fixup(sChunkHeader &header, void *data)
{
	uintptr off = (uintptr)data - sizeof(sChunkHeader);
	uint8 **rel = (uint8**)(header.relocTab + off);
	uint8 ***tab = (uint8***)rel;
	for(uint32 i = 0; i < header.numRelocs; i++){
		rel[i] += off;
		*tab[i] += off;
	}
	// TODO: relocate functions and class vtables
}

void
cRelocatableChunk::Fixup(void *data)
{
	Fixup(*(sChunkHeader*)data, (uint8*)data + sizeof(sChunkHeader));
}

void*
cRelocatableChunk::Shrink(sChunkHeader &header, void *data)
{
	return realloc(data, header.dataSize - sizeof(sChunkHeader));
}

void*
cRelocatableChunk::Shrink(void *data)
{
	return realloc(data, ((sChunkHeader*)data)->dataSize);
}
