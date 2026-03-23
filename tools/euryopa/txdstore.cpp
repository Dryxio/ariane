#include "euryopa.h"
#include "modloader.h"

static TxdDef txdlist[NUMTEXDICTS];
static int numTxds;
static int32 txdStoreOffset;	// RW plugin
static rw::TexDictionary *pushedTxd;

rw::TexDictionary *defaultTxd;

int
FindTxdSlot(const char *name)
{
	int i;
	for(i = 0; i < numTxds; i++){
		if(rw::strncmp_ci(txdlist[i].name, name, MODELNAMELEN) == 0)
			return i;
	}
	return -1;
}

TxdDef*
GetTxdDef(int i)
{
	if(i < 0 || i >= numTxds){
//		log("warning: invalid Txd slot %d\n", i);
		return nil;
	}
	return &txdlist[i];
}

int
AddTxdSlot(const char *name)
{
	int i;
	i = FindTxdSlot(name);
	if(i >= 0)
		return i;
	i = numTxds++;
	strncpy(txdlist[i].name, name, MODELNAMELEN);
	txdlist[i].txd = nil;
	txdlist[i].parentId = -1;
	txdlist[i].imageIndex = -1;
	return i;
}

bool
IsTxdLoaded(int i)
{
	TxdDef *td = GetTxdDef(i);
	if(td) return td->txd != nil;
	return false;
}

void
CreateTxd(int i)
{
	TxdDef *td = GetTxdDef(i);
	if(td)
		td->txd = rw::TexDictionary::create();
}

void
LoadTxd(int i)
{
	uint8 *buffer;
	int size;
	bool looseFile = false;
	rw::StreamMemory stream;
	TxdDef *td = GetTxdDef(i);
	if(td->txd)
		return;
	if(td->parentId >= 0)
		LoadTxd(td->parentId);

	const char *loosePath = ModloaderFindOverride(td->name, "txd");
	if(loosePath){
		buffer = ReadLooseFile(loosePath, &size);
		looseFile = true;
	}else{
		if(td->imageIndex < 0){
			log("warning: no streaming info for txd %s\n", td->name);
			return;
		}
		buffer = ReadFileFromImage(td->imageIndex, &size);
	}

	stream.open((uint8*)buffer, size);
	if(findChunk(&stream, rw::ID_TEXDICTIONARY, nil, nil)){
		td->txd = rw::TexDictionary::streamRead(&stream);
		ConvertTxd(td->txd);
	}else
		td->txd = rw::TexDictionary::create();

	if(td->parentId >= 0){
		rw::TexDictionary *partxd = GetTxdDef(td->parentId)->txd;
		*PLUGINOFFSET(rw::TexDictionary*, td->txd, txdStoreOffset) = partxd;
	}

	stream.close();
	if(looseFile) free(buffer);
}

void
LoadTxd(int i, const char *path)
{
	TxdDef *td = GetTxdDef(i);
	if(td->txd)
		return;
	td->txd = FileLoader::LoadTexDictionary(path);
}

void
TxdMakeCurrent(int i)
{
	TxdDef *td = GetTxdDef(i);
	if(td)
		rw::TexDictionary::setCurrent(td->txd);
}

void
TxdPush(void)
{
	pushedTxd = rw::TexDictionary::getCurrent();
}

void
TxdPop(void)
{
	rw::TexDictionary::setCurrent(pushedTxd);
	pushedTxd = nil;
}

void
TxdSetParent(const char *child, const char *parent)
{
	int p, c;
	p = AddTxdSlot(parent);
	c = AddTxdSlot(child);
	GetTxdDef(c)->parentId = p;
}


rw::Texture*
TxdStoreFindCB(const char *name)
{
	rw::TexDictionary *txd = rw::TexDictionary::getCurrent();
	rw::Texture *tex;
	while(txd){
		tex = txd->find(name);
		if(tex) return tex;
		txd = *PLUGINOFFSET(rw::TexDictionary*, txd, txdStoreOffset);
	}
	return nil;
}

static void*
createTxdStore(void *object, int32 offset, int32)
{
	*PLUGINOFFSET(rw::TexDictionary*, object, offset) = nil;
	return object;
}

static void*
copyTxdStore(void *dst, void *src, int32 offset, int32)
{
	*PLUGINOFFSET(rw::TexDictionary*, dst, offset) = *PLUGINOFFSET(rw::TexDictionary*, src, offset);
	return dst;
}

static void*
destroyTxdStore(void *object, int32, int32)
{
	return object;
}

void
RegisterTexStorePlugin(void)
{
	txdStoreOffset = rw::TexDictionary::registerPlugin(sizeof(void*), gta::ID_TXDSTORE,
	                                       createTxdStore,
	                                       destroyTxdStore,
	                                       copyTxdStore);
	rw::Texture::findCB = TxdStoreFindCB;
}
