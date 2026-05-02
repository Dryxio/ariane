#include "III.h"
#include "FileMgr.h"

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

// case insensitive fopen
static FILE*
fopen_ci(const char *path, const char *mode)
{
	char cipath[1024];
	strncpy(cipath, path, 1024);
	rw::makePath(cipath);
	return fopen(cipath, mode);
}

char CFileMgr::ms_dirName[512];
char CFileMgr::ms_rootDirName[512];

void
CFileMgr::Initialise(void)
{
#ifdef _WIN32
	GetCurrentDirectory(sizeof(ms_rootDirName), ms_rootDirName);
#else
	getcwd(ms_rootDirName, sizeof(ms_rootDirName));
#endif
	strcat(ms_rootDirName, "/");
}

void
CFileMgr::SetDir(const char *dir)
{
	strcpy(ms_dirName, ms_rootDirName);
	if(*dir){
		strcat(ms_dirName, dir);
		if(ms_dirName[strlen(ms_dirName)-1] != '/')
			strcat(ms_dirName, "/");
	}
#ifdef _WIN32
	SetCurrentDirectory(ms_dirName);
#else
	chdir(ms_dirName);
#endif
}

FileHandle
CFileMgr::OpenFile(const char *filename, const char *mode)
{
	return fopen_ci(filename, mode);
}

void
CFileMgr::CloseFile(FileHandle file)
{
	fclose(file);
}

uint32
CFileMgr::Read(FileHandle file, uint8 *buf, int32 len)
{
	return fread(buf, 1, len, file);
}

uint32
CFileMgr::Write(FileHandle file, uint8 *buf, int32 len)
{
	return fwrite(buf, 1, len, file);
}

void
CFileMgr::Seek(FileHandle file, int32 offset, uint32 origin)
{
	fseek(file, offset, origin);
}

int32
CFileMgr::LoadFile(const char *filename, uint8 *buf, int32 len, const char *mode)
{
	FileHandle f;
	f = OpenFile(filename, mode);
	if(f == nil)
		return -1;
	len = Read(f, buf, len-1);
	if(len >= 0)
		buf[len] = '\0';
	CloseFile(f);
	return len;
}
