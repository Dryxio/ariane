#ifndef _FILEMGR_H_
#define _FILEMGR_H_

typedef FILE *FileHandle;

class CFileMgr
{
	static char ms_dirName[512];
	static char ms_rootDirName[512];
public:
	static void Initialise(void);
	static void SetDir(const char *dir);
	static FileHandle OpenFile(const char *filename, const char *mode);
	static void CloseFile(FileHandle file);
	static uint32 Read(FileHandle file, uint8 *buf, int32 len);
	static uint32 Write(FileHandle file, uint8 *buf, int32 len);
	static void Seek(FileHandle file, int32 offset, uint32 origin);
	static int32 LoadFile(const char *filename, uint8 *buf, int32 len, const char *mode);
};

#endif
