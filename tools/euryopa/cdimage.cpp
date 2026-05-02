#include "euryopa.h"
#include "modloader.h"

#include "minilzo/minilzo.h"
#include <vector>

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
	FILE_DAT,
	FILE_IFP,
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
static uint8 *looseOverrideBuffer;

static CPtrList requestList;

static const char*
GetDirEntryExtension(const DirEntry *de)
{
	switch(de->filetype){
	case FILE_MODEL: return "dff";
	case FILE_TXD: return "txd";
	case FILE_COL: return "col";
	case FILE_IPL: return "ipl";
	case FILE_DAT: return "dat";
	case FILE_IFP: return "ifp";
	default: return nil;
	}
}

static bool
BuildDirEntryFilename(const DirEntry *de, char *dst, size_t size)
{
	const char *ext = GetDirEntryExtension(de);
	if(dst == nil || size == 0 || ext == nil)
		return false;
	int written = snprintf(dst, size, "%s.%s", de->name, ext);
	return written >= 0 && (size_t)written < size;
}

static bool
BuildDirEntryLogicalPath(const CdImage *cdimg, const DirEntry *de, char *dst, size_t size)
{
	char entryFilename[32];
	size_t len;
#ifdef _WIN32
	const char *sep = "\\";
#else
	const char *sep = "/";
#endif

	if(cdimg == nil || de == nil || dst == nil || size == 0)
		return false;
	if(cdimg->logicalName == nil || !BuildDirEntryFilename(de, entryFilename, sizeof(entryFilename)))
		return false;

	len = strlen(cdimg->logicalName);
#ifdef _WIN32
	if(len > 0 && (cdimg->logicalName[len-1] == '\\' || cdimg->logicalName[len-1] == '/'))
		sep = "";
#else
	if(len > 0 && cdimg->logicalName[len-1] == '/')
		sep = "";
#endif

	int written = snprintf(dst, size, "%s%s%s", cdimg->logicalName, sep, entryFilename);
	return written >= 0 && (size_t)written < size;
}

static void
NormalizeLookupPath(const char *in, char *out, size_t size)
{
	size_t i;

	if(out == nil || size == 0)
		return;
	if(in == nil){
		out[0] = '\0';
		return;
	}

	for(i = 0; i < size-1 && in[i]; i++){
		char c = in[i];
		if(c == '\\')
			c = '/';
		if(c >= 'A' && c <= 'Z')
			c = c - 'A' + 'a';
		out[i] = c;
	}
	out[i] = '\0';
}

static uint8*
ReadLooseOverrideBuffer(const char *path, int *size)
{
	int looseSize = 0;
	uint8 *buf = ReadLooseFile(path, &looseSize);
	if(buf == nil)
		return nil;
	if(looseOverrideBuffer)
		free(looseOverrideBuffer);
	looseOverrideBuffer = buf;
	if(size)
		*size = looseSize;
	return looseOverrideBuffer;
}


void
uiShowCdImages(void)
{
	static const char *types[] = {
		"-", "DFF", "TXD", "COL", "IPL", "DAT", "IFP"
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
	char logicalPath[512];
	const char *srcPath;
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
	}else if(rw::strncmp_ci(ext, "dat", 3) == 0){
		de->filetype = FILE_DAT;
		de->file = NewGameFile(de->name);
	}else if(rw::strncmp_ci(ext, "ifp", 3) == 0){
		de->filetype = FILE_IFP;
	}else{
//		log("warning: unknown file extension: %s %s\n", ext, de->name);
		return;
	}
	if(de->archiveSize > maxFileSize)
		maxFileSize = de->archiveSize;
	if(de->readableSize > maxFileSize)
		maxFileSize = de->readableSize;

	if(de->file && BuildDirEntryLogicalPath(cdimg, de, logicalPath, sizeof(logicalPath))){
		srcPath = ModloaderGetSourcePath(logicalPath);
		if(srcPath){
			free(de->file->sourcePath);
			de->file->sourcePath = strdup(srcPath);
		}
	}

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

static bool
AddCdImageResolved(const char *path, const char *resolvedPath, bool useDirRedirect)
{
	FILE *img, *dir;
	char dirpath[1024];
	char resolvedDirPath[1024];
	char *p;
	int fourcc, n;
	int imgindex;
	CdImage *cdimg;

	img = fopen(resolvedPath, "rb");
	if(img == nil){
		log("warning: cdimage %s couldn't be opened\n", path);
		return false;
	}

	if(numCdImages < NUMCDIMAGES){
		imgindex = numCdImages++;
		cdimg = &cdImages[imgindex];
	}else{
		log("warning: no room for more than %d cdimages\n", NUMCDIMAGES);
		fclose(img);
		return false;
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
		const char *redirect = useDirRedirect ? ModloaderRedirectPath(dirpath) : nil;
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
			return false;
		}
		fseek(dir, 0, SEEK_END);
		n = ftell(dir);
		fseek(dir, 0, SEEK_SET);
		n /= 32;
		ReadDirectory(cdimg, dir, n, false);
		fclose(dir);
	}
	log("AddCdImage: opened %s from %s\n", cdimg->logicalName, cdimg->sourcePath);
	return true;
}

void
AddCdImage(const char *path)
{
	char resolvedPath[1024];
	char stockPath[1024];

	const char *redirect = ModloaderRedirectPath(path);
	if(redirect){
		strncpy(stockPath, path, sizeof(stockPath)-1);
		stockPath[sizeof(stockPath)-1] = '\0';
		rw::makePath(stockPath);

		// Mod Loader IMG files are overlays: entries present in the mod archive
		// override stock entries, but missing stock TXDs/DFFs must remain visible.
		FILE *stock = fopen(stockPath, "rb");
		if(stock){
			fclose(stock);
			AddCdImageResolved(path, stockPath, false);
		}

		strncpy(resolvedPath, redirect, sizeof(resolvedPath)-1);
		resolvedPath[sizeof(resolvedPath)-1] = '\0';
		AddCdImageResolved(path, resolvedPath, true);
		return;
	}

	strncpy(resolvedPath, path, sizeof(resolvedPath)-1);
	resolvedPath[sizeof(resolvedPath)-1] = '\0';
	rw::makePath(resolvedPath);
	AddCdImageResolved(path, resolvedPath, true);
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
						log("warning: model %s appears multiple times, later archive wins\n", obj->m_name);
					}
				}
				obj->m_imageIndex = idx;
			}
			break;

		case FILE_TXD:
			slot = AddTxdSlot(de->name);
			txd = GetTxdDef(slot);
			if(txd->imageIndex >= 0){
				if(warnOnDuplicates){
					log("warning: txd %s appears multiple times, later archive wins\n", txd->name);
				}
			}
			txd->imageIndex = idx;
			break;

		case FILE_COL:
			slot = AddColSlot(de->name);
			col = GetColDef(slot);
			if(col->imageIndex >= 0){
				if(warnOnDuplicates){
					log("warning: col %s appears multiple times, later archive wins\n", col->name);
				}
			}
			col->imageIndex = idx;
			break;

		case FILE_IPL:
			slot = AddIplSlot(de->name);
			ipl = GetIplDef(slot);
			if(ipl->imageIndex >= 0){
				if(warnOnDuplicates){
					log("warning: ipl %s appears multiple times, later archive wins\n", ipl->name);
				}
			}
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
DecompressFile(uint8 *src, int *size);

static uint8*
ReadDirEntryData(CdImage *cdimg, DirEntry *de, int *size, const char **outLooseOverridePath)
{
	if(outLooseOverridePath)
		*outLooseOverridePath = nil;

	// When the directory entry already resolved to a loose override during
	// modloader scanning, prefer that exact physical file over reconstructing
	// the lookup key again here.
	if(de->file && de->file->sourcePath){
		uint8 *overrideBuffer = ReadLooseOverrideBuffer(de->file->sourcePath, size);
		if(overrideBuffer){
			de->overridden = 1;
			if(outLooseOverridePath)
				*outLooseOverridePath = de->file->sourcePath;
			return overrideBuffer;
		}
	}

	char entryFilename[32];
	if(BuildDirEntryFilename(de, entryFilename, sizeof(entryFilename))){
		const char *overridePath = ModloaderFindImageEntryOverride(cdimg->logicalName, entryFilename);
		if(overridePath){
			uint8 *overrideBuffer = ReadLooseOverrideBuffer(overridePath, size);
			if(overrideBuffer){
				de->overridden = 1;
				if(outLooseOverridePath)
					*outLooseOverridePath = overridePath;
				return overrideBuffer;
			}
		}
	}

	de->overridden = 0;
	fseek(cdimg->file, de->position*2048, SEEK_SET);
	fread(streamingBuffer, 1, de->archiveSize*2048, cdimg->file);
	if(*(uint32*)streamingBuffer == 0x67A3A1CE)
		return DecompressFile(streamingBuffer, size);
	if(size)
		*size = de->archiveSize*2048;
	return streamingBuffer;
}

static bool
FindDirEntryByLogicalPath(const char *logicalPath, CdImage **outCdimg, DirEntry **outEntry)
{
	char normalizedWant[512];

	NormalizeLookupPath(logicalPath, normalizedWant, sizeof(normalizedWant));
	if(normalizedWant[0] == '\0')
		return false;

	for(int imgIdx = 0; imgIdx < numCdImages; imgIdx++){
		CdImage *cdimg = &cdImages[imgIdx];
		for(int dirIdx = 0; dirIdx < cdimg->directorySize; dirIdx++){
			DirEntry *de = &cdimg->directory[dirIdx];
			char entryLogicalPath[512];
			char normalizedEntryPath[512];

			if(!BuildDirEntryLogicalPath(cdimg, de, entryLogicalPath, sizeof(entryLogicalPath)))
				continue;
			NormalizeLookupPath(entryLogicalPath, normalizedEntryPath, sizeof(normalizedEntryPath));
			if(strcmp(normalizedWant, normalizedEntryPath) != 0)
				continue;

			if(outCdimg)
				*outCdimg = cdimg;
			if(outEntry)
				*outEntry = de;
			return true;
		}
	}
	return false;
}

bool
ReadCdImageEntryByLogicalPath(const char *logicalPath, std::vector<uint8> &data,
                              char *outSourcePath, size_t outSourcePathSize)
{
	CdImage *cdimg;
	DirEntry *de;
	const char *looseOverridePath = nil;
	int size = 0;
	uint8 *buf;

	data.clear();
	if(outSourcePath && outSourcePathSize > 0)
		outSourcePath[0] = '\0';
	if(!FindDirEntryByLogicalPath(logicalPath, &cdimg, &de))
		return false;

	buf = ReadDirEntryData(cdimg, de, &size, &looseOverridePath);
	if(buf == nil || size < 0)
		return false;

	data.assign(buf, buf + size);
	if(outSourcePath && outSourcePathSize > 0){
		if(looseOverridePath){
			strncpy(outSourcePath, looseOverridePath, outSourcePathSize-1);
			outSourcePath[outSourcePathSize-1] = '\0';
		}else{
			char entryFilename[32];
			if(BuildDirEntryFilename(de, entryFilename, sizeof(entryFilename))){
				snprintf(outSourcePath, outSourcePathSize, "%s::%s", cdimg->sourcePath, entryFilename);
			}else{
				strncpy(outSourcePath, cdimg->sourcePath, outSourcePathSize-1);
				outSourcePath[outSourcePathSize-1] = '\0';
			}
		}
	}
	return true;
}

bool
ReadCdImageEntryByLogicalPathMinSize(const char *logicalPath, size_t minSize,
                                     std::vector<uint8> &data,
                                     char *outSourcePath, size_t outSourcePathSize)
{
	CdImage *cdimg;
	DirEntry *de;
	const char *looseOverridePath = nil;
	int size = 0;
	uint8 *buf;

	data.clear();
	if(outSourcePath && outSourcePathSize > 0)
		outSourcePath[0] = '\0';
	if(!FindDirEntryByLogicalPath(logicalPath, &cdimg, &de))
		return false;

	buf = ReadDirEntryData(cdimg, de, &size, &looseOverridePath);
	if(buf == nil || size < 0)
		return false;

	if(outSourcePath && outSourcePathSize > 0){
		if(looseOverridePath){
			strncpy(outSourcePath, looseOverridePath, outSourcePathSize-1);
			outSourcePath[outSourcePathSize-1] = '\0';
		}else{
			char entryFilename[32];
			if(BuildDirEntryFilename(de, entryFilename, sizeof(entryFilename))){
				snprintf(outSourcePath, outSourcePathSize, "%s::%s", cdimg->sourcePath, entryFilename);
			}else{
				strncpy(outSourcePath, cdimg->sourcePath, outSourcePathSize-1);
				outSourcePath[outSourcePathSize-1] = '\0';
			}
		}
	}

	if((size_t)size >= minSize || looseOverridePath != nil || de->sizeInArchive != 0){
		data.assign(buf, buf + size);
		return true;
	}

	data.resize(minSize);
	fseek(cdimg->file, de->position*2048, SEEK_SET);
	size_t read = fread(&data[0], 1, minSize, cdimg->file);
	data.resize(read);
	return read > 0;
}

bool
GetCdImageEntryInfoByLogicalPath(const char *logicalPath, int *outEntryBytes,
                                 bool *outCompressed, bool *outLooseOverride,
                                 char *outSourcePath, size_t outSourcePathSize)
{
	CdImage *cdimg;
	DirEntry *de;
	const char *overridePath = nil;

	if(outEntryBytes)
		*outEntryBytes = 0;
	if(outCompressed)
		*outCompressed = false;
	if(outLooseOverride)
		*outLooseOverride = false;
	if(outSourcePath && outSourcePathSize > 0)
		outSourcePath[0] = '\0';
	if(!FindDirEntryByLogicalPath(logicalPath, &cdimg, &de))
		return false;

	char entryFilename[32];
	if(de->file && de->file->sourcePath && de->file->sourcePath[0] != '\0')
		overridePath = de->file->sourcePath;
	else if(BuildDirEntryFilename(de, entryFilename, sizeof(entryFilename)))
		overridePath = ModloaderFindImageEntryOverride(cdimg->logicalName, entryFilename);

	if(outEntryBytes)
		*outEntryBytes = de->archiveSize*2048;
	if(outCompressed)
		*outCompressed = de->sizeInArchive != 0;
	if(outLooseOverride)
		*outLooseOverride = overridePath != nil;
	if(outSourcePath && outSourcePathSize > 0){
		if(overridePath){
			strncpy(outSourcePath, overridePath, outSourcePathSize-1);
			outSourcePath[outSourcePathSize-1] = '\0';
		}else if(BuildDirEntryFilename(de, entryFilename, sizeof(entryFilename))){
			snprintf(outSourcePath, outSourcePathSize, "%s::%s", cdimg->sourcePath, entryFilename);
		}else{
			strncpy(outSourcePath, cdimg->sourcePath, outSourcePathSize-1);
			outSourcePath[outSourcePathSize-1] = '\0';
		}
	}
	return true;
}

bool
WriteCdImageEntryByLogicalPath(const char *logicalPath, uint8 *data, int size)
{
	CdImage *cdimg;
	DirEntry *de;
	const char *overridePath = nil;

	if(!FindDirEntryByLogicalPath(logicalPath, &cdimg, &de))
		return false;

	char entryFilename[32];
	if(de->file && de->file->sourcePath && de->file->sourcePath[0] != '\0')
		overridePath = de->file->sourcePath;
	else if(BuildDirEntryFilename(de, entryFilename, sizeof(entryFilename)))
		overridePath = ModloaderFindImageEntryOverride(cdimg->logicalName, entryFilename);

	if(overridePath){
		if(!EnsureParentDirectoriesForPath(overridePath))
			return false;
		FILE *f = fopen(overridePath, "wb");
		if(f == nil)
			return false;
		bool ok = size <= 0 || fwrite(data, 1, size, f) == (size_t)size;
		fclose(f);
		return ok;
	}

	int index = (cdimg->index << 24) | (int)(de - cdimg->directory);
	return WriteFileToImage(index, data, size);
}

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

const char*
GetCdImageLogicalName(int i)
{
	int img = i>>24 & 0xFF;
	return cdImages[img].logicalName;
}

const char*
GetCdImageSourcePath(int i)
{
	int img = i>>24 & 0xFF;
	return cdImages[img].sourcePath;
}

bool
GetCdImageEntrySourcePath(int i, char *outSourcePath, size_t outSourcePathSize)
{
	int img = i>>24 & 0xFF;
	int dir = i & 0xFFFFFF;
	CdImage *cdimg;
	DirEntry *de;

	if(outSourcePath && outSourcePathSize > 0)
		outSourcePath[0] = '\0';
	if(outSourcePath == nil || outSourcePathSize == 0)
		return false;
	if(img < 0 || img >= numCdImages)
		return false;
	cdimg = &cdImages[img];
	if(dir < 0 || dir >= cdimg->directorySize)
		return false;
	de = &cdimg->directory[dir];

	if(de->file && de->file->sourcePath && de->file->sourcePath[0] != '\0'){
		strncpy(outSourcePath, de->file->sourcePath, outSourcePathSize-1);
		outSourcePath[outSourcePathSize-1] = '\0';
		return true;
	}

	char entryFilename[32];
	if(BuildDirEntryFilename(de, entryFilename, sizeof(entryFilename))){
		snprintf(outSourcePath, outSourcePathSize, "%s::%s", cdimg->sourcePath, entryFilename);
		return true;
	}

	strncpy(outSourcePath, cdimg->sourcePath, outSourcePathSize-1);
	outSourcePath[outSourcePathSize-1] = '\0';
	return true;
}

bool
GetPreviousCdImageEntryIndex(int i, int *outIndex)
{
	int img = i>>24 & 0xFF;
	int dir = i & 0xFFFFFF;
	CdImage *cdimg;
	DirEntry *want;

	if(outIndex)
		*outIndex = -1;
	if(img < 0 || img >= numCdImages)
		return false;
	cdimg = &cdImages[img];
	if(dir < 0 || dir >= cdimg->directorySize)
		return false;
	want = &cdimg->directory[dir];

	for(int imgIdx = img; imgIdx >= 0; imgIdx--){
		CdImage *candidateImg = &cdImages[imgIdx];
		int dirIdx = imgIdx == img ? dir-1 : candidateImg->directorySize-1;
		for(; dirIdx >= 0; dirIdx--){
			DirEntry *candidate = &candidateImg->directory[dirIdx];
			if(candidate->filetype != want->filetype)
				continue;
			if(rw::strcmp_ci(candidate->name, want->name) != 0)
				continue;
			if(outIndex)
				*outIndex = dirIdx | (candidateImg->index << 24);
			return true;
		}
	}

	return false;
}

uint8*
ReadFileFromImage(int i, int *size)
{
	int img;
	CdImage *cdimg;
	const char *looseOverridePath = nil;
	img = i>>24 & 0xFF;
	i = i & 0xFFFFFF;
	cdimg = &cdImages[img];
	DirEntry *de = &cdimg->directory[i];
	return ReadDirEntryData(cdimg, de, size, &looseOverridePath);
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

	FILE *f = nil;
	if(cdimg->file){
		fclose(cdimg->file);
		cdimg->file = nil;
	}
	f = fopen(cdimg->sourcePath, "r+b");
	if(f == nil){
		log("WriteFileToImage: can't open %s (source %s) for writing\n",
		    cdimg->logicalName, cdimg->sourcePath);
		hotReloadTrace("WriteFileToImage: can't open %s (source %s) for writing\n",
		    cdimg->logicalName, cdimg->sourcePath);
		cdimg->file = fopen(cdimg->sourcePath, "rb");
		return false;
	}
	fseek(f, de->position*2048, SEEK_SET);
	size_t written = fwrite(data, 1, size, f);
	fflush(f);
	fclose(f);
	cdimg->file = fopen(cdimg->sourcePath, "rb");
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

bool
BuildModloaderImageEntryExportPath(int i, char *dst, size_t size)
{
	int img;
	CdImage *cdimg;
	char logicalPath[512];
	char entryFilename[32];

	if(dst == nil || size == 0)
		return false;

	img = i>>24 & 0xFF;
	i = i & 0xFFFFFF;
	cdimg = &cdImages[img];
	DirEntry *de = &cdimg->directory[i];
	if(!BuildDirEntryFilename(de, entryFilename, sizeof(entryFilename)))
		return false;
	if(!BuildModloaderLogicalExportPath(cdimg->logicalName, logicalPath, sizeof(logicalPath)))
		return false;

	size_t len = strlen(logicalPath);
#ifdef _WIN32
	const char *sep = (len > 0 && (logicalPath[len-1] == '\\' || logicalPath[len-1] == '/')) ? "" : "\\";
#else
	const char *sep = (len > 0 && logicalPath[len-1] == '/') ? "" : "/";
#endif
	int written = snprintf(dst, size, "%s%s%s", logicalPath, sep, entryFilename);
	return written >= 0 && (size_t)written < size;
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
