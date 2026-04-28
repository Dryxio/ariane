#include "euryopa.h"
#include "modloader.h"

static TxdDef txdlist[NUMTEXDICTS];
static int numTxds;
static int32 txdStoreOffset;	// RW plugin
static rw::TexDictionary *pushedTxd;

rw::TexDictionary *defaultTxd;

static rw::Texture*
FindTextureCaseInsensitive(rw::TexDictionary *txd, const char *name)
{
	if(txd == nil || name == nil)
		return nil;
	FORLIST(lnk, txd->textures){
		rw::Texture *tex = rw::Texture::fromDict(lnk);
		if(tex && rw::strcmp_ci(tex->name, name) == 0)
			return tex;
	}
	return nil;
}

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
RemoveTxdSlot(int i)
{
	if(i < 0 || i >= numTxds)
		return false;
	if(i != numTxds-1){
		log("warning: refusing to remove non-tail TXD slot %d (%s)\n", i, txdlist[i].name);
		return false;
	}

	TxdDef *td = &txdlist[i];
	if(td->txd){
		if(rw::TexDictionary::getCurrent() == td->txd)
			rw::TexDictionary::setCurrent(defaultTxd);
		if(pushedTxd == td->txd)
			pushedTxd = defaultTxd;
		td->txd->destroy();
		td->txd = nil;
	}

	memset(td, 0, sizeof(*td));
	td->parentId = -1;
	td->imageIndex = -1;
	numTxds--;
	return true;
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

static bool
ReadTxdFromBuffer(TxdDef *td, uint8 *buffer, int size, const char *sourcePath, rw::TexDictionary **outTxd)
{
	rw::StreamMemory stream;
	rw::TexDictionary *txd = nil;
	const char *source = sourcePath && sourcePath[0] ? sourcePath : "unknown source";
	bool ok = false;

	if(outTxd)
		*outTxd = nil;
	if(buffer == nil || size <= 0){
		log("warning: failed to read txd %s from %s\n", td->name, source);
		return false;
	}

	log("LoadTxd: %s from %s (%d bytes)\n", td->name, source, size);
	stream.open((uint8*)buffer, size);
	if(findChunk(&stream, rw::ID_TEXDICTIONARY, nil, nil)){
		txd = rw::TexDictionary::streamRead(&stream);
		if(txd){
			ConvertTxd(txd);
			ok = true;
		}else{
			log("warning: failed to parse txd %s from %s\n", td->name, source);
		}
	}else{
		log("warning: no TXD dictionary chunk for txd %s from %s\n", td->name, source);
	}
	stream.close();

	if(ok && outTxd)
		*outTxd = txd;
	return ok;
}

void
LoadTxd(int i)
{
	uint8 *buffer = nil;
	int size = 0;
	char sourcePath[1024];
	TxdDef *td = GetTxdDef(i);
	if(td == nil)
		return;
	if(td->txd)
		return;
	if(td->parentId >= 0)
		LoadTxd(td->parentId);

	sourcePath[0] = '\0';
	const char *loosePath = ModloaderFindOverride(td->name, "txd");
	if(loosePath){
		buffer = ReadLooseFile(loosePath, &size);
		strncpy(sourcePath, loosePath, sizeof(sourcePath)-1);
		sourcePath[sizeof(sourcePath)-1] = '\0';
		ReadTxdFromBuffer(td, buffer, size, sourcePath, &td->txd);
		if(buffer)
			free(buffer);
	}

	for(int imageIndex = td->imageIndex; td->txd == nil && imageIndex >= 0; ){
		buffer = ReadFileFromImage(imageIndex, &size);
		if(!GetCdImageEntrySourcePath(imageIndex, sourcePath, sizeof(sourcePath)))
			snprintf(sourcePath, sizeof(sourcePath), "image index %d", imageIndex);
		if(ReadTxdFromBuffer(td, buffer, size, sourcePath, &td->txd))
			break;

		int previous = -1;
		if(!GetPreviousCdImageEntryIndex(imageIndex, &previous))
			break;
		char previousPath[1024];
		if(!GetCdImageEntrySourcePath(previous, previousPath, sizeof(previousPath)))
			snprintf(previousPath, sizeof(previousPath), "image index %d", previous);
		log("warning: falling back txd %s from %s to previous entry %s\n",
			td->name, sourcePath[0] ? sourcePath : "unknown source", previousPath);
		imageIndex = previous;
	}

	if(td->txd == nil){
		if(td->imageIndex < 0 && loosePath == nil){
			log("warning: no streaming info for txd %s\n", td->name);
		}else{
			log("warning: using empty txd %s after all sources failed\n", td->name);
		}
		td->txd = rw::TexDictionary::create();
	}

	if(td->parentId >= 0){
		rw::TexDictionary *partxd = GetTxdDef(td->parentId)->txd;
		*PLUGINOFFSET(rw::TexDictionary*, td->txd, txdStoreOffset) = partxd;
	}
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
		if(tex == nil)
			tex = FindTextureCaseInsensitive(txd, name);
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
