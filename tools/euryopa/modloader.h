#ifndef MODLOADER_H
#define MODLOADER_H

#include <stddef.h>

void ModloaderInit(void);
bool ModloaderIsActive(void);
bool BuildModloaderLogicalExportPath(const char *logicalPath, char *dst, size_t size);

// DFF/TXD override: returns physical disk path or nil
const char *ModloaderFindOverride(const char *basename, const char *ext);

// Read a loose file into malloc'd buffer. Caller must free().
uint8 *ReadLooseFile(const char *path, int *size);

// Path redirect: returns physical path for a logical path, or nil
const char *ModloaderRedirectPath(const char *logicalPath);

// Get the physical source path for a logical path (for save-back).
// Returns physical path or nil (nil = base game file, use default save path).
const char *ModloaderGetSourcePath(const char *logicalPath);

// Get the winning loose override for an archive entry.
// archiveLogicalPath is GTA-relative (for example models/gta3.img).
// entryFilename includes extension (for example foo.ipl).
const char *ModloaderFindImageEntryOverride(const char *archiveLogicalPath, const char *entryFilename);

// Collect readme-defined gta.dat addition lines (post-LoadLevel injection)
// Returns count. Each entry has type ("IDE"/"IPL"/"COLFILE"/"IMG"/"CDIMAGE") and logical path.
struct ModloaderDatEntry { const char *type; const char *logicalPath; };
int ModloaderGetAdditions(ModloaderDatEntry *entries, int maxCount);

#endif
