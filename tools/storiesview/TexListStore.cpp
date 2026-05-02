#include "storiesview.h"

// Taken from IIItest and adapted for the Rsl crap

RslTexture*
convertTexture(RslTexture *texture, void *pData)
{
	rw::TexDictionary *rwtxd = (rw::TexDictionary*)pData;
	rw::Raster *raster = convertRasterPS2(&texture->raster->ps2);
	rw::Texture *rwtex = rw::Texture::create(raster);
	strncpy(rwtex->name, texture->name, 32);
	strncpy(rwtex->mask, texture->mask, 32);
	rwtex->setFilter(rw::Texture::LINEAR);
	rwtxd->add(rwtex);
	return texture;
}

rw::TexDictionary*
convertTexList(RslTexList *txd)
{
	rw::TexDictionary *rwtxd = rw::TexDictionary::create();
	RslTexListForAllTextures(txd, convertTexture, rwtxd);
	return rwtxd;
}

RslTexList *dummytexlist;

// RSL
TexListPool *CTexListStore::ms_pTexListPool;

// RW
rw::TexDictionary *CTexListStore::ms_pStoredTexList;
// we use this to store our converted texlists
rw::TexDictionary **rwtxds;

// Get a TexDictionary. If it's not converted yet, do it
rw::TexDictionary*
CTexListStore::GetRWTxd(int slot)
{
	rw::TexDictionary *txd;
	txd = rwtxds[slot];
	if(txd)
		return txd;
	txd = convertTexList(GetSlot(slot)->texlist);
	return txd;
}

void
CTexListStore::Initialize(TexListPool *pool)
{
	ms_pTexListPool = pool;
	rwtxds = new rw::TexDictionary*[ms_pTexListPool->GetSize()];
	memset(rwtxds, 0, ms_pTexListPool->GetSize()*sizeof(void*));
}

int
CTexListStore::AddTexListSlot(const char *name)
{
	TexListDef *def = ms_pTexListPool->New();
	assert(def);
	def->texlist = nil;
	def->refCount = 0;
	strncpy(def->name, name, 20);
	return ms_pTexListPool->GetJustIndex(def);
}

int
CTexListStore::FindTexListSlot(const char *name)
{
	char *defname;
	int size = ms_pTexListPool->GetSize();
	for(int i = 0; i < size; i++){
		defname = GetTexListName(i);
		if(defname && rw::strncmp_ci(defname, name, 24) == 0)
			return i;
	}
	return -1;
}

char*
CTexListStore::GetTexListName(int slot)
{
	TexListDef *def = GetSlot(slot);
	return def ? def->name : nil;
}

void
CTexListStore::PushCurrentTexList(void)
{
	CTexListStore::ms_pStoredTexList = rw::TexDictionary::getCurrent();
}

void
CTexListStore::PopCurrentTexList(void)
{
	rw::TexDictionary::setCurrent(CTexListStore::ms_pStoredTexList);
	CTexListStore::ms_pStoredTexList = nil;
}

void
CTexListStore::SetCurrentTexList(int slot)
{
	TexListDef *def = GetSlot(slot);
	if(def)
		rw::TexDictionary::setCurrent(GetRWTxd(slot));
}

void
CTexListStore::Create(int slot)
{
	GetSlot(slot)->texlist = dummytexlist;
	rwtxds[slot] = rw::TexDictionary::create();
}

void
CTexListStore::AddRef(int slot)
{
	GetSlot(slot)->refCount++;
}

void
CTexListStore::RemoveRef(int slot)
{
	assert(0);
//	if(--GetSlot(slot)->refCount <= 0)
//		CStreaming::RemoveModel(slot + TexListOFFSET);
}

void
CTexListStore::RemoveRefWithoutDelete(int slot)
{
	GetSlot(slot)->refCount--;
}

bool
CTexListStore::LoadTexList(int slot, rw::Stream *stream)
{
	assert(0);
/*
	TexListDef *def = GetSlot(slot);
	if(!rw::findChunk(stream, rw::ID_TEXDICTIONARY, nil, nil)){
		return false;
	}else{
		def->texDict = rw::TexDictionary::streamRead(stream);
		convertTexList(def->texDict);
		return def->texDict != nil;
	}
*/
	return false;
}

bool
CTexListStore::LoadTexList(int slot, const char *filename)
{
	assert(0);
	return false;
/*
	rw::StreamFile stream;
	if(stream.open(getPath(filename), "rb")){
		LoadTexList(slot, &stream);
		stream.close();
		return true;
	}
	printf("Failed to open TexList\n");
	return false;
*/
}

void
CTexListStore::RemoveTexList(int slot)
{
	assert(0);
/*
	TexListDef *def = GetSlot(slot);
	if(def->texDict)
		def->texDict->destroy();
	def->texDict = nil;
*/
}

bool
CTexListStore::isTexListLoaded(int slot)
{
	return GetSlot(slot)->texlist != nil;
}
