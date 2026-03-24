#include "euryopa.h"
#include "modloader.h"

#include "minilzo/minilzo.h"

/*
 * Streaming limits:
 *
 * III PC:
 * 5500 models
 * 850 tex dicts
 *
 * VC PC:
 * 6500 models
 * 1385 tex dicts
 * 31 collisions
 * 35 ifps
 *
 * SA PC:
 * 20000 models
 * 5000 tex dicts
 * 255 collisions
 * 256 ipls
 * 64 dats
 * 180 ifps
 * 47 rrrs
 * 82 scms
 */

enum
{
	FILE_MODEL = 1,
	FILE_TXD,
	FILE_COL,
	FILE_IPL,
};

struct DirEntry
{
	uint32 position;
	uint32 archiveSize;
	uint32 readableSize;
	uint16 sizeInArchive;
	char name[24];

	// additional data
	int filetype;
	int overridden;

	GameFile *file;
};

struct CdImage
{
	char *logicalName;
	char *sourcePath;
	int index;

	DirEntry *directory;
	int directorySize;
	int directoryLimit;

	FILE *file;
};
static CdImage cdImages[NUMCDIMAGES];
static int numCdImages;

static uint32 maxFileSize;
static uint8 *streamingBuffer;
// for LZO compressed files
static uint8 *compressionBuf;
static uint32 compressionBufSize;

static CPtrList requestList;


void
uiShowCdImages(void)
{
	static const char *types[] = {
		"-", "DFF", "TXD", "COL", "IPL"
	};
	int i, j;
	CdImage *cdimg;
	DirEntry *de;

	for(i = 0; i < numCdImages; i++){
		cdimg = &cdImages[i];
		if(ImGui::TreeNode(cdimg->logicalName)){
			for(j = 0; j < cdimg->directorySize; j++){
				de = &cdimg->directory[j];

				if(de->overridden)
					ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(255, 0, 0));

				ImGui::Text("%-20s %s", de->name, types[de->filetype]);

				if(de->overridden)
					ImGui::PopStyleColor();
			}
			ImGui::TreePop();
		}
	}
}

struct DirEntryV1Disk
{
	uint32 position;
	uint32 size;
	char name[24];
};

struct DirEntryV2Disk
{
	uint32 position;
	uint16 streamingSize;
	uint16 sizeInArchive;
	char name[24];
};

static void
AddDirEntry(CdImage *cdimg, DirEntry *de)
{
	char *ext;
	ext = strrchr(de->name, '.');
	if(ext == nil){
		log("warning: no file extension: %s\n", de->name);
		return;
	}
	*ext++ = '\0';
	// VC introduced IFP COL
	// SA introduced IPL DAT RRR SCM
	if(rw::strncmp_ci(ext, "dff", 3) == 0)
		de->filetype = FILE_MODEL;
	else if(rw::strncmp_ci(ext, "txd", 3) == 0)
		de->filetype = FILE_TXD;
	else if(rw::strncmp_ci(ext, "col", 3) == 0){
		de->filetype = FILE_COL;
		de->file = NewGameFile(de->name);
	}else if(rw::strncmp_ci(ext, "ipl", 3) == 0){
		de->filetype = FILE_IPL;
		de->file = NewGameFile(de->name);
	}else{
//		log("warning: unknown file extension: %s %s\n", ext, de->name);
		return;
	}
	if(de->archiveSize > maxFileSize)
		maxFileSize = de->archiveSize;
	if(de->readableSize > maxFileSize)
		maxFileSize = de->readableSize;
	cdimg->directory[cdimg->directorySize++] = *de;
}

static void
ReadDirectory(CdImage *cdimg, FILE *f, int n, bool ver2)
{
	DirEntry de;
	int i;

	if(cdimg->directory == nil){
		cdimg->directoryLimit = 8000;
		cdimg->directory = rwNewT(DirEntry, cdimg->directoryLimit, 0);
	}
	while(cdimg->directorySize + n > cdimg->directoryLimit){
		cdimg->directoryLimit *= 2;
		cdimg->directory = rwResizeT(DirEntry, cdimg->directory, cdimg->directoryLimit, 0);
	}
	for(i = 0 ; i < n; i++){
		memset(&de, 0, sizeof(de));
		if(ver2){
			DirEntryV2Disk raw;
			fread(&raw, 1, sizeof(raw), f);
			de.position = raw.position;
			de.sizeInArchive = raw.sizeInArchive;
			de.readableSize = raw.streamingSize;
			de.archiveSize = raw.sizeInArchive != 0 ? raw.sizeInArchive : raw.streamingSize;
			if(de.readableSize == 0)
				de.readableSize = de.archiveSize;
			strncpy(de.name, raw.name, sizeof(de.name));
			de.name[sizeof(de.name)-1] = '\0';
		}else{
			DirEntryV1Disk raw;
			fread(&raw, 1, sizeof(raw), f);
			de.position = raw.position;
			de.archiveSize = raw.size;
			de.readableSize = raw.size;
			de.sizeInArchive = 0;
			strncpy(de.name, raw.name, sizeof(de.name));
			de.name[sizeof(de.name)-1] = '\0';
		}
		de.filetype = 0;
		de.overridden = 0;
		AddDirEntry(cdimg, &de);
	}
}

void
AddCdImage(const char *path)
{
	FILE *img, *dir;
	char dirpath[1024];
	char resolvedPath[1024];
	char resolvedDirPath[1024];
	char *p;
	int fourcc, n;
	int imgindex;
	CdImage *cdimg;

	if(numCdImages < NUMCDIMAGES){
		imgindex = numCdImages++;
		cdimg = &cdImages[imgindex];
	}else{
		log("warning: no room for more than %d cdimages\n", NUMCDIMAGES);
		return;
	}

	const char *redirect = ModloaderRedirectPath(path);
	if(redirect){
		strncpy(resolvedPath, redirect, sizeof(resolvedPath)-1);
		resolvedPath[sizeof(resolvedPath)-1] = '\0';
	}else{
		strncpy(resolvedPath, path, sizeof(resolvedPath)-1);
		resolvedPath[sizeof(resolvedPath)-1] = '\0';
		rw::makePath(resolvedPath);
	}

	img = fopen(resolvedPath, "rb");
	if(img == nil){
		log("warning: cdimage %s couldn't be opened\n", path);
		numCdImages--;
		return;
	}
	cdimg->logicalName = strdup(path);
	cdimg->sourcePath = strdup(resolvedPath);
	cdimg->file = img;
	cdimg->index = imgindex;
	cdimg->directory = nil;
	cdimg->directorySize = 0;
	cdimg->directoryLimit = 0;

	fread(&fourcc, 1, 4, img);
	if(fourcc == 0x32524556){	// VER2
		// Found a VER2 image, read its directory
		fread(&n, 1, 4, img);
		ReadDirectory(cdimg, img, n, true);
	}else{
		strncpy(dirpath, path, sizeof(dirpath)-1);
		dirpath[sizeof(dirpath)-1] = '\0';
		p = strrchr(dirpath, '.');
		strcpy(p+1, "dir");
		redirect = ModloaderRedirectPath(dirpath);
		if(redirect){
			strncpy(resolvedDirPath, redirect, sizeof(resolvedDirPath)-1);
			resolvedDirPath[sizeof(resolvedDirPath)-1] = '\0';
		}else{
			strncpy(resolvedDirPath, dirpath, sizeof(resolvedDirPath)-1);
			resolvedDirPath[sizeof(resolvedDirPath)-1] = '\0';
			rw::makePath(resolvedDirPath);
		}
		dir = fopen(resolvedDirPath, "rb");
		if(dir == nil){
			log("warning: directory %s couldn't be opened for %s (%s)\n",
			    resolvedDirPath, cdimg->logicalName, cdimg->sourcePath);
			numCdImages--;
			free(cdimg->logicalName);
			free(cdimg->sourcePath);
			fclose(img);
			return;
		}
		fseek(dir, 0, SEEK_END);
		n = ftell(dir);
		fseek(dir, 0, SEEK_SET);
		n /= 32;
		ReadDirectory(cdimg, dir, n, false);
		fclose(dir);
	}
	log("AddCdImage: opened %s from %s\n", cdimg->logicalName, cdimg->sourcePath);
}

static void
RegisterCdImageEntries(CdImage *cdimg, bool warnOnDuplicates)
{
	int i, slot;
	int32 idx;
	ObjectDef *obj;
	TxdDef *txd;
	ColDef *col;
	IplDef *ipl;

	for(i = 0; i < cdimg->directorySize; i++){
		DirEntry *de = &cdimg->directory[i];
		idx = i | cdimg->index<<24;
		switch(de->filetype){
		case FILE_MODEL:
			obj = GetObjectDef(de->name, nil);
			if(obj){
				if(obj->m_imageIndex >= 0){
					if(warnOnDuplicates){
						log("warning: model %s appears multiple times\n", obj->m_name);
						de->overridden = 1;
					}
				}else
					obj->m_imageIndex = idx;
			}
			break;

		case FILE_TXD:
			slot = AddTxdSlot(de->name);
			txd = GetTxdDef(slot);
			if(txd->imageIndex >= 0){
				if(warnOnDuplicates){
					log("warning: txd %s appears multiple times\n", txd->name);
					de->overridden = 1;
				}
			}else
				txd->imageIndex = idx;
			break;

		case FILE_COL:
			slot = AddColSlot(de->name);
			col = GetColDef(slot);
			if(col == nil)
				break;
			if(col->imageIndex >= 0){
				if(warnOnDuplicates){
					log("warning: col %s appears multiple times\n", col->name);
					de->overridden = 1;
				}
			}else
				col->imageIndex = idx;
			break;

		case FILE_IPL:
			slot = AddIplSlot(de->name);
			ipl = GetIplDef(slot);
			if(ipl->imageIndex >= 0){
				if(warnOnDuplicates){
					log("warning: ipl %s appears multiple times\n", ipl->name);
					de->overridden = 1;
				}
			}else
				ipl->imageIndex = idx;
			break;
		}
	}
}

static void
InitCdImage(CdImage *cdimg)
{
	RegisterCdImageEntries(cdimg, true);
	if(lzo_init() != LZO_E_OK)
		panic("LZO init failed");
}

void
InitCdImages(void)
{
	int i;
	if(isSA())
		for(i = 0; i < numCdImages; i++)
			InitCdImage(&cdImages[i]);
	else
		for(i = numCdImages-1; i >= 0; i--)
			InitCdImage(&cdImages[i]);
	streamingBuffer = (uint8*)malloc(maxFileSize*2048);
	compressionBufSize = maxFileSize*2048;
	compressionBuf = (uint8*)malloc(compressionBufSize);
}

void
RefreshCdImageMappings(void)
{
	int i;
	if(isSA())
		for(i = 0; i < numCdImages; i++)
			RegisterCdImageEntries(&cdImages[i], false);
	else
		for(i = numCdImages-1; i >= 0; i--)
			RegisterCdImageEntries(&cdImages[i], false);
}

//uint8 compressionbuf[4*1024*1024];

static uint8*
DecompressFile(uint8 *src, int *size)
{
	static uint8 blockbuf[128*1024];
	int32 total = *((int32*)src+2);
	total -= 12;
	src += 12;

	int sz = 0;
	while(total > 0){
		assert(*(uint32*)src == 4);
		uint32 blocksz = *((uint32*)src+2);
		src += 12;
		lzo_uint out_len = 128*1024;
		lzo_int r = lzo1x_decompress_safe(src, blocksz, blockbuf, &out_len, 0);
		if(r != LZO_E_OK){
			panic("LZO decompress error");
			return nil;
		}
		while(sz + out_len > compressionBufSize){
			compressionBufSize *= 2;
			compressionBuf = (uint8*)realloc(compressionBuf, compressionBufSize);
		}
		memcpy(compressionBuf+sz, blockbuf, out_len);
		sz += out_len;

		src += blocksz;
		total -= blocksz+12;
	}
	if(size)
		*size = sz;
	return compressionBuf;
}

GameFile*
GetGameFileFromImage(int i)
{
	int img;
	CdImage *cdimg;
	img = i>>24 & 0xFF;
	i = i & 0xFFFFFF;
	cdimg = &cdImages[img];
	DirEntry *de = &cdimg->directory[i];
	return de->file;
}

uint8*
ReadFileFromImage(int i, int *size)
{
	int img;
	CdImage *cdimg;
	img = i>>24 & 0xFF;
	i = i & 0xFFFFFF;
	cdimg = &cdImages[img];
	DirEntry *de = &cdimg->directory[i];
	fseek(cdimg->file, de->position*2048, SEEK_SET);
	fread(streamingBuffer, 1, de->archiveSize*2048, cdimg->file);
	if(*(uint32*)streamingBuffer == 0x67A3A1CE)
		return DecompressFile(streamingBuffer, size);
	if(size)
		*size = de->archiveSize*2048;
	return streamingBuffer;
}

bool
WriteFileToImage(int i, uint8 *data, int size)
{
	int img;
	CdImage *cdimg;
	img = i>>24 & 0xFF;
	i = i & 0xFFFFFF;
	cdimg = &cdImages[img];
	DirEntry *de = &cdimg->directory[i];

	if(de->sizeInArchive != 0){
		log("WriteFileToImage: refusing write to compressed entry %s in %s (source %s): sizeInArchive=%u\n",
		    de->name, cdimg->logicalName, cdimg->sourcePath, de->sizeInArchive);
		hotReloadTrace("WriteFileToImage: refusing write to compressed entry %s in %s (source %s): sizeInArchive=%u\n",
		    de->name, cdimg->logicalName, cdimg->sourcePath, de->sizeInArchive);
		return false;
	}

	if(de->archiveSize == 0 || de->readableSize == 0 || de->archiveSize != de->readableSize){
		log("WriteFileToImage: refusing write to %s in %s (source %s): inconsistent entry metadata (archive=%u readable=%u sizeInArchive=%u)\n",
		    de->name, cdimg->logicalName, cdimg->sourcePath,
		    de->archiveSize, de->readableSize, de->sizeInArchive);
		hotReloadTrace("WriteFileToImage: refusing write to %s in %s (source %s): inconsistent entry metadata (archive=%u readable=%u sizeInArchive=%u)\n",
		    de->name, cdimg->logicalName, cdimg->sourcePath,
		    de->archiveSize, de->readableSize, de->sizeInArchive);
		return false;
	}

	int entryBytes = de->archiveSize*2048;
	if(size > entryBytes){
		log("WriteFileToImage: refusing write to %s in %s (source %s): %d bytes exceeds allocated span %d\n",
		    de->name, cdimg->logicalName, cdimg->sourcePath, size, entryBytes);
		hotReloadTrace("WriteFileToImage: refusing write to %s in %s (source %s): %d bytes exceeds allocated span %d\n",
		    de->name, cdimg->logicalName, cdimg->sourcePath, size, entryBytes);
		return false;
	}
	if(size != entryBytes){
		log("WriteFileToImage: refusing write to %s in %s (source %s): %d bytes does not match entry span %d\n",
		    de->name, cdimg->logicalName, cdimg->sourcePath, size, entryBytes);
		hotReloadTrace("WriteFileToImage: refusing write to %s in %s (source %s): %d bytes does not match entry span %d\n",
		    de->name, cdimg->logicalName, cdimg->sourcePath, size, entryBytes);
		return false;
	}

	// Reopen in r+b mode for in-place writing
	FILE *f = fopen(cdimg->sourcePath, "r+b");
	if(f == nil){
		log("WriteFileToImage: can't open %s (source %s) for writing\n",
		    cdimg->logicalName, cdimg->sourcePath);
		hotReloadTrace("WriteFileToImage: can't open %s (source %s) for writing\n",
		    cdimg->logicalName, cdimg->sourcePath);
		return false;
	}
	fseek(f, de->position*2048, SEEK_SET);
	size_t written = fwrite(data, 1, size, f);
	fclose(f);
	if(written != (size_t)size){
		log("WriteFileToImage: short write for %s in %s (source %s): wrote %d/%d bytes at sector %d\n",
		    de->name, cdimg->logicalName, cdimg->sourcePath,
		    (int)written, size, de->position);
		hotReloadTrace("WriteFileToImage: short write for %s in %s (source %s): wrote %d/%d bytes at sector %d\n",
		    de->name, cdimg->logicalName, cdimg->sourcePath,
		    (int)written, size, de->position);
		return false;
	}
	log("WriteFileToImage: wrote %d/%d bytes for %s at sector %d in %s (source %s)\n",
		(int)written, size, de->name, de->position, cdimg->logicalName, cdimg->sourcePath);
	hotReloadTrace("WriteFileToImage: wrote %d/%d bytes for %s at sector %d in %s (source %s)\n",
		(int)written, size, de->name, de->position, cdimg->logicalName, cdimg->sourcePath);

	// Also update the in-memory cached file handle
	// (re-read will pick up new data)
	return true;
}

void
RequestObject(int id)
{
	requestList.InsertItem((void*)(uintptr)id);
}

void
LoadAllRequestedObjects(void)
{
	CPtrNode *p;
	int id;
	for(p = requestList.first; p; p = p->next){
		id = (int)(uintptr)p->item;
		ObjectDef *obj = GetObjectDef(id);
		if(obj && !obj->IsLoaded())
			obj->Load();
	}
	requestList.Flush();
}
