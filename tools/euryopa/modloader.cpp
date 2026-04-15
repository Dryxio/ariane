#include "euryopa.h"
#include "modloader.h"
#include <vector>
#include <string>
#include <algorithm>

#ifdef _WIN32
#include <io.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

struct ModFile {
	char logicalPath[256];   // GTA-relative, normalized (lowercase, / separator)
	char physicalPath[512];  // actual disk path
	char basename[64];       // name without extension, lowercase
	char ext[8];             // extension, lowercase
	char modName[128];
	int priority;
};

struct ImageEntryOverride {
	char archiveLogicalPath[256];
	char entryFilename[32];
	char physicalPath[512];
	char modName[128];
	int priority;
};

struct InternalAddition {
	ModloaderDatEntry entry;
	char modName[128];
	int priority;
};

static std::vector<ModFile> looseOverrides;   // effective loose basename overrides (after priority dedup)
static std::vector<ModFile> pathRedirects;    // effective IDE/IPL/COL/IMG redirects
static std::vector<ImageEntryOverride> imageEntryOverrides; // effective loose overrides for archive entries
static std::vector<InternalAddition> additionsInternal; // with priority for dedup
static std::vector<ModloaderDatEntry> additions; // final deduplicated additions (public)
// storage for strdup'd strings used by additions
static std::vector<char*> additionStrings;
static bool active = false;

static void NormalizePath(const char *in, char *out, int maxLen);
static void PreScanDatFile(const char *datPath, std::vector<std::string> &basePaths);

static bool
BuildGameRootedPath(char *dst, size_t size, const char *name)
{
	char rootDir[1024];
	return GetGameRootDirectory(rootDir, sizeof(rootDir)) &&
	       BuildPath(dst, size, rootDir, name);
}

static bool
HasRedirectForLogicalPath(const char *logicalPath)
{
	for(size_t i = 0; i < pathRedirects.size(); i++)
		if(strcmp(pathRedirects[i].logicalPath, logicalPath) == 0)
			return true;
	return false;
}

static void
NormalizeModName(const char *modName, char *out, size_t outSize)
{
	if(outSize == 0)
		return;
	out[0] = '\0';
	if(modName == nil)
		return;

	char normalized[128];
	NormalizePath(modName, normalized, sizeof(normalized));
	strncpy(out, normalized, outSize-1);
	out[outSize-1] = '\0';
}

static int
CompareModCandidates(const char *modNameA, int priorityA, const char *modNameB, int priorityB)
{
	if(priorityA != priorityB)
		return priorityA - priorityB;

	char normalizedA[128];
	char normalizedB[128];
	NormalizeModName(modNameA, normalizedA, sizeof(normalizedA));
	NormalizeModName(modNameB, normalizedB, sizeof(normalizedB));
	return strcmp(normalizedA, normalizedB);
}

static bool
IsBetterModFileCandidate(const ModFile &candidate, const ModFile &current)
{
	return CompareModCandidates(candidate.modName, candidate.priority,
	                            current.modName, current.priority) > 0;
}

static bool
IsBetterImageEntryOverrideCandidate(const ImageEntryOverride &candidate,
                                    const ImageEntryOverride &current)
{
	return CompareModCandidates(candidate.modName, candidate.priority,
	                            current.modName, current.priority) > 0;
}

static void
NormalizeStockArchiveAlias(const char *logicalPath, char *out, size_t outSize)
{
	if(outSize == 0)
		return;
	out[0] = '\0';
	if(logicalPath == nil)
		return;

	char normalized[256];
	NormalizePath(logicalPath, normalized, sizeof(normalized));
	if(strcmp(normalized, "gta3.img") == 0)
		strncpy(normalized, "models/gta3.img", sizeof(normalized)-1);
	else if(strcmp(normalized, "gta_int.img") == 0)
		strncpy(normalized, "models/gta_int.img", sizeof(normalized)-1);
	normalized[sizeof(normalized)-1] = '\0';

	strncpy(out, normalized, outSize-1);
	out[outSize-1] = '\0';
}

static bool
ExtractImageEntryOverrideKey(const char *logicalPath, char *archiveLogicalPath, size_t archiveSize,
                             char *entryFilename, size_t entrySize)
{
	if(logicalPath == nil || archiveSize == 0 || entrySize == 0)
		return false;

	char normalized[256];
	NormalizePath(logicalPath, normalized, sizeof(normalized));
	char *imgMarker = strstr(normalized, ".img/");
	if(imgMarker == nil)
		return false;

	size_t archiveLen = (imgMarker - normalized) + 4;
	if(archiveLen == 0 || archiveLen >= archiveSize)
		return false;
	memcpy(archiveLogicalPath, normalized, archiveLen);
	archiveLogicalPath[archiveLen] = '\0';
	NormalizeStockArchiveAlias(archiveLogicalPath, archiveLogicalPath, archiveSize);

	const char *entry = imgMarker + 5;
	if(entry[0] == '\0' || strchr(entry, '/') != nil)
		return false;

	strncpy(entryFilename, entry, entrySize-1);
	entryFilename[entrySize-1] = '\0';
	return true;
}

static void
AddStockArchiveAliasRedirect(const char *stockLogicalPath, const char *aliasBasename,
                             std::vector<ModFile> &allModFiles)
{
	if(HasRedirectForLogicalPath(stockLogicalPath))
		return;

	int bestIdx = -1;
	for(size_t i = 0; i < allModFiles.size(); i++){
		ModFile &mf = allModFiles[i];
		if(strcmp(mf.basename, aliasBasename) != 0 || strcmp(mf.ext, "img") != 0)
			continue;
		if(bestIdx < 0 || IsBetterModFileCandidate(mf, allModFiles[bestIdx]))
			bestIdx = (int)i;
	}
	if(bestIdx < 0)
		return;

	ModFile alias = allModFiles[bestIdx];
	strncpy(alias.logicalPath, stockLogicalPath, sizeof(alias.logicalPath)-1);
	alias.logicalPath[sizeof(alias.logicalPath)-1] = '\0';
	pathRedirects.push_back(alias);
}

static bool
IsRedirectExt(const char *ext)
{
	return strcmp(ext, "ide") == 0 || strcmp(ext, "ipl") == 0 ||
	       strcmp(ext, "col") == 0 || strcmp(ext, "img") == 0 ||
	       strcmp(ext, "dir") == 0;
}

static bool
IsLooseBasenameOverrideExt(const char *ext)
{
	return strcmp(ext, "dff") == 0 || strcmp(ext, "txd") == 0;
}

static int
FindBestExactLogicalPathCandidate(const char *logicalPath, std::vector<ModFile> &allModFiles)
{
	int bestIdx = -1;
	for(size_t i = 0; i < allModFiles.size(); i++){
		ModFile &mf = allModFiles[i];
		if(strcmp(mf.logicalPath, logicalPath) != 0)
			continue;
		if(bestIdx < 0 || IsBetterModFileCandidate(mf, allModFiles[bestIdx]))
			bestIdx = (int)i;
	}
	return bestIdx;
}

static void
PreScanDatWithOverride(const char *logicalPath, std::vector<ModFile> &allModFiles,
                       std::vector<std::string> &basePaths)
{
	int bestIdx = FindBestExactLogicalPathCandidate(logicalPath, allModFiles);
	if(bestIdx >= 0){
		PreScanDatFile(allModFiles[bestIdx].physicalPath, basePaths);
		return;
	}
	if(doesFileExist(logicalPath))
		PreScanDatFile(logicalPath, basePaths);
}

static void
AddMainDatRedirects(std::vector<ModFile> &allModFiles)
{
	static const char *mainDatPaths[] = {
		"data/default.dat",
		"data/gta3.dat",
		"data/gta_vc.dat",
		"data/gta.dat",
		"data/gta_lcs.dat",
		"data/gta_vcs.dat",
	};

	for(size_t i = 0; i < sizeof(mainDatPaths)/sizeof(mainDatPaths[0]); i++){
		const char *logicalPath = mainDatPaths[i];
		if(HasRedirectForLogicalPath(logicalPath))
			continue;

		int bestIdx = FindBestExactLogicalPathCandidate(logicalPath, allModFiles);
		if(bestIdx < 0)
			continue;

		ModFile redirectMf = allModFiles[bestIdx];
		strncpy(redirectMf.logicalPath, logicalPath, sizeof(redirectMf.logicalPath)-1);
		redirectMf.logicalPath[sizeof(redirectMf.logicalPath)-1] = '\0';
		pathRedirects.push_back(redirectMf);
	}
}

static bool
HasWrappedLogicalSuffix(const char *wrappedPath, const char *stockLogicalPath)
{
	size_t wrappedLen = strlen(wrappedPath);
	size_t stockLen = strlen(stockLogicalPath);
	if(wrappedLen <= stockLen)
		return false;
	if(strcmp(wrappedPath + wrappedLen - stockLen, stockLogicalPath) != 0)
		return false;
	return wrappedPath[wrappedLen - stockLen - 1] == '/';
}

static void
AddWrappedStockPathRedirects(const std::vector<std::string> &basePaths,
                             std::vector<ModFile> &allModFiles)
{
	for(size_t bi = 0; bi < basePaths.size(); bi++){
		const char *stockLogicalPath = basePaths[bi].c_str();
		if(HasRedirectForLogicalPath(stockLogicalPath))
			continue;

		int bestIdx = -1;
		for(size_t fi = 0; fi < allModFiles.size(); fi++){
			ModFile &mf = allModFiles[fi];
			if(!IsRedirectExt(mf.ext))
				continue;
			if(!HasWrappedLogicalSuffix(mf.logicalPath, stockLogicalPath))
				continue;
			if(bestIdx < 0 || IsBetterModFileCandidate(mf, allModFiles[bestIdx]))
				bestIdx = (int)fi;
		}
		if(bestIdx < 0)
			continue;

		ModFile redirectMf = allModFiles[bestIdx];
		strncpy(redirectMf.logicalPath, stockLogicalPath, sizeof(redirectMf.logicalPath)-1);
		redirectMf.logicalPath[sizeof(redirectMf.logicalPath)-1] = '\0';
		pathRedirects.push_back(redirectMf);
	}
}

// Normalize a path: lowercase, backslash to forward slash, strip leading ./
static void
NormalizePath(const char *in, char *out, int maxLen)
{
	int i;
	for(i = 0; i < maxLen-1 && in[i]; i++){
		char c = in[i];
		if(c == '\\') c = '/';
		if(c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
		out[i] = c;
	}
	out[i] = '\0';
	// strip leading ./
	if(out[0] == '.' && out[1] == '/'){
		memmove(out, out+2, strlen(out+2)+1);
	}
}

// Extract basename (without extension) and extension from a filename
static void
ExtractBasenameExt(const char *filename, char *basename, int baseMax, char *ext, int extMax)
{
	const char *dot = strrchr(filename, '.');
	const char *slash = filename;
	const char *s;
	for(s = filename; *s; s++)
		if(*s == '/' || *s == '\\')
			slash = s + 1;

	if(dot && dot > slash){
		int len = (int)(dot - slash);
		if(len >= baseMax) len = baseMax - 1;
		for(int i = 0; i < len; i++){
			char c = slash[i];
			if(c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
			basename[i] = c;
		}
		basename[len] = '\0';

		dot++; // skip the .
		int elen = (int)strlen(dot);
		if(elen >= extMax) elen = extMax - 1;
		for(int i = 0; i < elen; i++){
			char c = dot[i];
			if(c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
			ext[i] = c;
		}
		ext[elen] = '\0';
	}else{
		int len = (int)strlen(slash);
		if(len >= baseMax) len = baseMax - 1;
		for(int i = 0; i < len; i++){
			char c = slash[i];
			if(c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
			basename[i] = c;
		}
		basename[len] = '\0';
		ext[0] = '\0';
	}
}

// Simple INI parser for modloader.ini priority
// Reads [Profiles.<profile>.Priority] section for mod_name=priority pairs
static void
ParseModloaderIni(const char *iniPath, std::vector<std::pair<std::string, int>> &priorities)
{
	FILE *f = fopen(iniPath, "r");
	if(!f) return;

	char line[512];
	char profileName[64] = "Default";

	// First pass: find Profile key in [Folder.Config]
	bool inFolderConfig = false;
	while(fgets(line, sizeof(line), f)){
		char *s = line;
		while(*s && isspace((unsigned char)*s)) s++;
		if(*s == '['){
			inFolderConfig = (strncmp(s, "[Folder.Config]", 15) == 0);
			continue;
		}
		if(inFolderConfig && strncmp(s, "Profile", 7) == 0){
			char *eq = strchr(s, '=');
			if(eq){
				eq++;
				while(*eq && isspace((unsigned char)*eq)) eq++;
				char *end = eq + strlen(eq) - 1;
				while(end > eq && isspace((unsigned char)*end)) *end-- = '\0';
				strncpy(profileName, eq, sizeof(profileName)-1);
				profileName[sizeof(profileName)-1] = '\0';
			}
		}
	}

	// Second pass: find [Profiles.<profile>.Priority] section
	fseek(f, 0, SEEK_SET);
	char sectionName[128];
	snprintf(sectionName, sizeof(sectionName), "[Profiles.%s.Priority]", profileName);
	bool inPriority = false;
	while(fgets(line, sizeof(line), f)){
		char *s = line;
		while(*s && isspace((unsigned char)*s)) s++;
		if(*s == '['){
			inPriority = (strncmp(s, sectionName, strlen(sectionName)) == 0);
			continue;
		}
		if(inPriority && *s && *s != '#' && *s != ';'){
			char *eq = strchr(s, '=');
			if(eq){
				*eq = '\0';
				char *name = s;
				char *end = eq - 1;
				while(end > name && isspace((unsigned char)*end)) *end-- = '\0';
				char *val = eq + 1;
				while(*val && isspace((unsigned char)*val)) val++;
				int prio = atoi(val);
				// lowercase mod name for matching
				for(char *c = name; *c; c++)
					if(*c >= 'A' && *c <= 'Z') *c = *c - 'A' + 'a';
				priorities.push_back(std::make_pair(std::string(name), prio));
			}
		}
	}
	fclose(f);
}

// Pre-scan gta.dat files to collect logical paths the base game will load
static void
PreScanDatFile(const char *datPath, std::vector<std::string> &basePaths)
{
	FILE *f = fopen_ci(datPath, "rb");
	if(!f) return;

	char linebuf[512];
	while(fgets(linebuf, sizeof(linebuf), f)){
		char *s = linebuf;
		while(*s && isspace((unsigned char)*s)) s++;
		if(*s == '#' || *s == '\0' || *s == '\n') continue;

		char normalized[256];
		if(strncmp(s, "IDE ", 4) == 0){
			NormalizePath(s+4, normalized, sizeof(normalized));
			// trim trailing whitespace
			int len = (int)strlen(normalized);
			while(len > 0 && (normalized[len-1] == '\n' || normalized[len-1] == '\r' || normalized[len-1] == ' '))
				normalized[--len] = '\0';
			basePaths.push_back(normalized);
		}else if(strncmp(s, "IPL ", 4) == 0){
			NormalizePath(s+4, normalized, sizeof(normalized));
			int len = (int)strlen(normalized);
			while(len > 0 && (normalized[len-1] == '\n' || normalized[len-1] == '\r' || normalized[len-1] == ' '))
				normalized[--len] = '\0';
			basePaths.push_back(normalized);
		}else if(strncmp(s, "COLFILE ", 8) == 0){
			// COLFILE <n> <path> — skip the number
			char *p = s + 8;
			while(*p && isspace((unsigned char)*p)) p++;
			while(*p && !isspace((unsigned char)*p)) p++; // skip number
			while(*p && isspace((unsigned char)*p)) p++;
			NormalizePath(p, normalized, sizeof(normalized));
			int len = (int)strlen(normalized);
			while(len > 0 && (normalized[len-1] == '\n' || normalized[len-1] == '\r' || normalized[len-1] == ' '))
				normalized[--len] = '\0';
			basePaths.push_back(normalized);
		}else if(strncmp(s, "IMG ", 4) == 0 || strncmp(s, "CDIMAGE ", 8) == 0){
			int off = (s[0] == 'I') ? 4 : 8;
			NormalizePath(s+off, normalized, sizeof(normalized));
			int len = (int)strlen(normalized);
			while(len > 0 && (normalized[len-1] == '\n' || normalized[len-1] == '\r' || normalized[len-1] == ' '))
				normalized[--len] = '\0';
			basePaths.push_back(normalized);
		}
	}
	fclose(f);
}

// Recursive directory scan
#ifdef _WIN32
static void
ScanDirectory(const char *dir, const char *relBase, std::vector<ModFile> &files, int priority, const char *modName)
{
	char pattern[512];
	snprintf(pattern, sizeof(pattern), "%s\\*", dir);

	WIN32_FIND_DATAA fd;
	HANDLE hFind = FindFirstFileA(pattern, &fd);
	if(hFind == INVALID_HANDLE_VALUE) return;

	do {
		if(fd.cFileName[0] == '.') continue;
		char fullPath[512];
		snprintf(fullPath, sizeof(fullPath), "%s\\%s", dir, fd.cFileName);
		char relPath[256];
		if(relBase[0])
			snprintf(relPath, sizeof(relPath), "%s/%s", relBase, fd.cFileName);
		else
			snprintf(relPath, sizeof(relPath), "%s", fd.cFileName);

		if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){
			ScanDirectory(fullPath, relPath, files, priority, modName);
		}else{
			ModFile mf;
			NormalizePath(relPath, mf.logicalPath, sizeof(mf.logicalPath));
			strncpy(mf.physicalPath, fullPath, sizeof(mf.physicalPath)-1);
			mf.physicalPath[sizeof(mf.physicalPath)-1] = '\0';
			ExtractBasenameExt(mf.logicalPath, mf.basename, sizeof(mf.basename),
			                   mf.ext, sizeof(mf.ext));
			strncpy(mf.modName, modName, sizeof(mf.modName)-1);
			mf.modName[sizeof(mf.modName)-1] = '\0';
			mf.priority = priority;
			files.push_back(mf);
		}
	} while(FindNextFileA(hFind, &fd));
	FindClose(hFind);
}
#else
static void
ScanDirectory(const char *dir, const char *relBase, std::vector<ModFile> &files, int priority, const char *modName)
{
	DIR *d = opendir(dir);
	if(!d) return;

	struct dirent *ent;
	while((ent = readdir(d)) != nil){
		if(ent->d_name[0] == '.') continue;
		char fullPath[512];
		snprintf(fullPath, sizeof(fullPath), "%s/%s", dir, ent->d_name);
		char relPath[256];
		if(relBase[0])
			snprintf(relPath, sizeof(relPath), "%s/%s", relBase, ent->d_name);
		else
			snprintf(relPath, sizeof(relPath), "%s", ent->d_name);

		struct stat st;
			if(stat(fullPath, &st) != 0) continue;
			if(S_ISDIR(st.st_mode)){
				ScanDirectory(fullPath, relPath, files, priority, modName);
			}else{
				ModFile mf;
				NormalizePath(relPath, mf.logicalPath, sizeof(mf.logicalPath));
				strncpy(mf.physicalPath, fullPath, sizeof(mf.physicalPath)-1);
				mf.physicalPath[sizeof(mf.physicalPath)-1] = '\0';
				ExtractBasenameExt(mf.logicalPath, mf.basename, sizeof(mf.basename),
				                   mf.ext, sizeof(mf.ext));
				strncpy(mf.modName, modName, sizeof(mf.modName)-1);
				mf.modName[sizeof(mf.modName)-1] = '\0';
				mf.priority = priority;
				files.push_back(mf);
			}
	}
	closedir(d);
}
#endif

// Parse a .txt file for gta.dat-style addition lines
static void
ParseReadmeFile(const char *txtPath, const char *modName,
                std::vector<ModFile> &allFiles,
                std::vector<InternalAddition> &additionList, int priority)
{
	FILE *f = fopen(txtPath, "r");
	if(!f) return;

	char line[512];
	while(fgets(line, sizeof(line), f)){
		char *s = line;
		while(*s && isspace((unsigned char)*s)) s++;
		if(*s == '#' || *s == '\0' || *s == '\n') continue;

		const char *type = nil;
		char *path = nil;
		if(strncmp(s, "IDE ", 4) == 0){ type = "IDE"; path = s+4; }
		else if(strncmp(s, "IPL ", 4) == 0){ type = "IPL"; path = s+4; }
		else if(strncmp(s, "IMG ", 4) == 0){ type = "IMG"; path = s+4; }
		else if(strncmp(s, "CDIMAGE ", 8) == 0){ type = "CDIMAGE"; path = s+8; }
		else if(strncmp(s, "COLFILE ", 8) == 0){
			// COLFILE <level> <path> — skip the level number
			type = "COLFILE";
			path = s+8;
			while(*path && isspace((unsigned char)*path)) path++;
			while(*path && !isspace((unsigned char)*path)) path++; // skip level number
		}

		if(!type || !path) continue;

		// trim
		while(*path && isspace((unsigned char)*path)) path++;
		int len = (int)strlen(path);
		while(len > 0 && (path[len-1] == '\n' || path[len-1] == '\r' || path[len-1] == ' '))
			path[--len] = '\0';
		if(len == 0) continue;

		// Store the addition entry (logical path as written, with backslashes)
		char *storedType = strdup(type);
		char *storedPath = strdup(path);
		additionStrings.push_back(storedType);
		additionStrings.push_back(storedPath);

		InternalAddition ia;
		ia.entry.type = storedType;
		ia.entry.logicalPath = storedPath;
		strncpy(ia.modName, modName, sizeof(ia.modName)-1);
		ia.modName[sizeof(ia.modName)-1] = '\0';
		ia.priority = priority;
		additionList.push_back(ia);

		// Try to find the physical file in the mod folder's scanned files
		char normalizedLogical[256];
		NormalizePath(path, normalizedLogical, sizeof(normalizedLogical));
		for(size_t i = 0; i < allFiles.size(); i++){
			if(strcmp(allFiles[i].logicalPath, normalizedLogical) == 0){
				// This file exists in the mod — add to pathRedirects
				// (done later in resolution phase)
				break;
			}
		}
	}
	fclose(f);
}

// Get list of mod subdirectories
#ifdef _WIN32
static void
ListModDirs(const char *modloaderDir, std::vector<std::string> &dirs)
{
	char pattern[512];
	snprintf(pattern, sizeof(pattern), "%s\\*", modloaderDir);

	WIN32_FIND_DATAA fd;
	HANDLE hFind = FindFirstFileA(pattern, &fd);
	if(hFind == INVALID_HANDLE_VALUE) return;

	do {
		if(fd.cFileName[0] == '.') continue;
		if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){
			dirs.push_back(fd.cFileName);
		}
	} while(FindNextFileA(hFind, &fd));
	FindClose(hFind);
}
#else
static void
ListModDirs(const char *modloaderDir, std::vector<std::string> &dirs)
{
	DIR *d = opendir(modloaderDir);
	if(!d) return;

	struct dirent *ent;
	while((ent = readdir(d)) != nil){
		if(ent->d_name[0] == '.') continue;
		char fullPath[512];
		snprintf(fullPath, sizeof(fullPath), "%s/%s", modloaderDir, ent->d_name);
		struct stat st;
		if(stat(fullPath, &st) == 0 && S_ISDIR(st.st_mode))
			dirs.push_back(ent->d_name);
	}
	closedir(d);
}
#endif

static bool
DirExists(const char *path)
{
#ifdef _WIN32
	DWORD attr = GetFileAttributesA(path);
	return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
#else
	struct stat st;
	return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
#endif
}

void
ModloaderInit(void)
{
	active = false;
	looseOverrides.clear();
	pathRedirects.clear();
	imageEntryOverrides.clear();
	additionsInternal.clear();
	additions.clear();
	for(size_t i = 0; i < additionStrings.size(); i++)
		free(additionStrings[i]);
	additionStrings.clear();

	char modloaderDir[1024];
	char modloaderIniPath[1024];
	if(!BuildGameRootedPath(modloaderDir, sizeof(modloaderDir), "modloader") ||
	   !BuildPath(modloaderIniPath, sizeof(modloaderIniPath), modloaderDir, "modloader.ini"))
		return;

	if(!DirExists(modloaderDir)){
		return;
	}

	// Parse modloader.ini for priorities
	std::vector<std::pair<std::string, int>> priorities;
	ParseModloaderIni(modloaderIniPath, priorities);

	// Enumerate mod subdirectories
	std::vector<std::string> modDirs;
	ListModDirs(modloaderDir, modDirs);
	std::sort(modDirs.begin(), modDirs.end(), [](const std::string &a, const std::string &b) {
		char normalizedA[128];
		char normalizedB[128];
		NormalizeModName(a.c_str(), normalizedA, sizeof(normalizedA));
		NormalizeModName(b.c_str(), normalizedB, sizeof(normalizedB));
		return strcmp(normalizedA, normalizedB) < 0;
	});

	// Collect all mod files
	std::vector<ModFile> allModFiles;
	int numMods = 0;

	for(size_t mi = 0; mi < modDirs.size(); mi++){
		char modPath[512];
		if(!BuildPath(modPath, sizeof(modPath), modloaderDir, modDirs[mi].c_str()))
			continue;

		// Get priority for this mod
		int priority = 50; // default
		std::string modNameLower = modDirs[mi];
		for(size_t c = 0; c < modNameLower.size(); c++)
			if(modNameLower[c] >= 'A' && modNameLower[c] <= 'Z')
				modNameLower[c] = modNameLower[c] - 'A' + 'a';
		for(size_t pi = 0; pi < priorities.size(); pi++){
			if(priorities[pi].first == modNameLower){
				priority = priorities[pi].second;
				break;
			}
		}

		// Scan all files in this mod
		std::vector<ModFile> modFiles;
			ScanDirectory(modPath, "", modFiles, priority, modDirs[mi].c_str());

		// Parse .txt files for gta.dat-style additions
		for(size_t fi = 0; fi < modFiles.size(); fi++){
			if(strcmp(modFiles[fi].ext, "txt") == 0){
				ParseReadmeFile(modFiles[fi].physicalPath, modDirs[mi].c_str(),
				                modFiles, additionsInternal, priority);
			}
		}

		for(size_t fi = 0; fi < modFiles.size(); fi++)
			allModFiles.push_back(modFiles[fi]);
		numMods++;
	}

	if(allModFiles.empty()){
		return;
	}

	// Pre-scan the winning main dat files so redirected child paths can come
	// from total conversions with custom data/default.dat and data/gta*.dat.
	std::vector<std::string> basePaths;
	PreScanDatWithOverride("data/default.dat", allModFiles, basePaths);
	PreScanDatWithOverride("data/gta3.dat", allModFiles, basePaths);
	PreScanDatWithOverride("data/gta_vc.dat", allModFiles, basePaths);
	PreScanDatWithOverride("data/gta.dat", allModFiles, basePaths);
	PreScanDatWithOverride("data/gta_lcs.dat", allModFiles, basePaths);
	PreScanDatWithOverride("data/gta_vcs.dat", allModFiles, basePaths);

	// Also add the default IMG paths
	char normBuf[256];
	NormalizePath("MODELS\\GTA3.IMG", normBuf, sizeof(normBuf));
	basePaths.push_back(normBuf);
	NormalizePath("MODELS\\GTA_INT.IMG", normBuf, sizeof(normBuf));
	basePaths.push_back(normBuf);

	// 0) Loose archive-entry overrides (for example models/gta3.img/foo.ipl)
	{
		std::vector<ImageEntryOverride> candidates;
		for(size_t i = 0; i < allModFiles.size(); i++){
			ModFile &mf = allModFiles[i];

			ImageEntryOverride ov = {};
			if(!ExtractImageEntryOverrideKey(mf.logicalPath,
			                                 ov.archiveLogicalPath, sizeof(ov.archiveLogicalPath),
			                                 ov.entryFilename, sizeof(ov.entryFilename)))
				continue;

			strncpy(ov.physicalPath, mf.physicalPath, sizeof(ov.physicalPath)-1);
			ov.physicalPath[sizeof(ov.physicalPath)-1] = '\0';
			strncpy(ov.modName, mf.modName, sizeof(ov.modName)-1);
			ov.modName[sizeof(ov.modName)-1] = '\0';
			ov.priority = mf.priority;
			candidates.push_back(ov);
		}

		for(size_t i = 0; i < candidates.size(); i++){
			bool dominated = false;
			for(size_t j = 0; j < candidates.size(); j++){
				if(i == j) continue;
				if(strcmp(candidates[i].archiveLogicalPath, candidates[j].archiveLogicalPath) == 0 &&
				   strcmp(candidates[i].entryFilename, candidates[j].entryFilename) == 0 &&
				   IsBetterImageEntryOverrideCandidate(candidates[j], candidates[i])){
					dominated = true;
					break;
				}
			}
			if(!dominated){
				bool dup = false;
				for(size_t k = 0; k < imageEntryOverrides.size(); k++){
					if(strcmp(imageEntryOverrides[k].archiveLogicalPath, candidates[i].archiveLogicalPath) == 0 &&
					   strcmp(imageEntryOverrides[k].entryFilename, candidates[i].entryFilename) == 0){
						dup = true;
						break;
					}
				}
				if(!dup)
					imageEntryOverrides.push_back(candidates[i]);
			}
		}
	}

	// Priority resolution

	// 1) Loose basename overrides: group by (basename, ext), keep highest priority.
	// Keep this restricted to DFF/TXD. IPL/COL names are not globally unique enough
	// for basename-only matching and can redirect unrelated assets/scenes.
	{
		std::vector<ModFile> looseCandidates;
		for(size_t i = 0; i < allModFiles.size(); i++){
			ModFile &mf = allModFiles[i];
			if(IsLooseBasenameOverrideExt(mf.ext))
				looseCandidates.push_back(mf);
		}
		for(size_t i = 0; i < looseCandidates.size(); i++){
			bool dominated = false;
			for(size_t j = 0; j < looseCandidates.size(); j++){
				if(i == j) continue;
				if(strcmp(looseCandidates[i].basename, looseCandidates[j].basename) == 0 &&
				   strcmp(looseCandidates[i].ext, looseCandidates[j].ext) == 0 &&
				   IsBetterModFileCandidate(looseCandidates[j], looseCandidates[i])){
					dominated = true;
					break;
				}
			}
			if(!dominated){
				bool dup = false;
				for(size_t k = 0; k < looseOverrides.size(); k++){
					if(strcmp(looseOverrides[k].basename, looseCandidates[i].basename) == 0 &&
					   strcmp(looseOverrides[k].ext, looseCandidates[i].ext) == 0){
						dup = true;
						break;
					}
				}
				if(!dup)
					looseOverrides.push_back(looseCandidates[i]);
			}
		}
	}

	// 2) Path redirects: IDE/IPL/COL/IMG that match base gta.dat entries
	{
		std::vector<ModFile> redirectCandidates;
		for(size_t i = 0; i < allModFiles.size(); i++){
			ModFile &mf = allModFiles[i];
			if(strcmp(mf.ext, "ide") == 0 || strcmp(mf.ext, "ipl") == 0 ||
			   strcmp(mf.ext, "col") == 0 || strcmp(mf.ext, "img") == 0 ||
			   strcmp(mf.ext, "dir") == 0){
				// Check if this logical path matches a base gta.dat entry
				for(size_t bi = 0; bi < basePaths.size(); bi++){
					if(strcmp(mf.logicalPath, basePaths[bi].c_str()) == 0){
						redirectCandidates.push_back(mf);
						break;
					}
				}
			}
		}
		// Deduplicate by logicalPath, highest priority wins
		for(size_t i = 0; i < redirectCandidates.size(); i++){
			bool dominated = false;
			for(size_t j = 0; j < redirectCandidates.size(); j++){
				if(i == j) continue;
				if(strcmp(redirectCandidates[i].logicalPath, redirectCandidates[j].logicalPath) == 0 &&
				   IsBetterModFileCandidate(redirectCandidates[j], redirectCandidates[i])){
					dominated = true;
					break;
				}
			}
			if(!dominated){
				bool dup = false;
				for(size_t k = 0; k < pathRedirects.size(); k++){
					if(strcmp(pathRedirects[k].logicalPath, redirectCandidates[i].logicalPath) == 0){
						dup = true;
						break;
					}
				}
				if(!dup)
					pathRedirects.push_back(redirectCandidates[i]);
			}
		}
	}

	// Main .dat files themselves must redirect so fopen_ci("data/gta.dat")
	// and friends resolve to the winning modded file.
	AddMainDatRedirects(allModFiles);

	// 3) Deduplicate additionsInternal by (type, normalized logical path), highest priority wins
	{
		std::vector<InternalAddition> deduped;
		for(size_t i = 0; i < additionsInternal.size(); i++){
			char normA[256];
			NormalizePath(additionsInternal[i].entry.logicalPath, normA, sizeof(normA));
			bool dominated = false;
			for(size_t j = 0; j < additionsInternal.size(); j++){
				if(i == j) continue;
				char normB[256];
				NormalizePath(additionsInternal[j].entry.logicalPath, normB, sizeof(normB));
				if(strcmp(normA, normB) != 0) continue;
				if(strcmp(additionsInternal[i].entry.type, additionsInternal[j].entry.type) != 0) continue;
				if(CompareModCandidates(additionsInternal[j].modName, additionsInternal[j].priority,
				                        additionsInternal[i].modName, additionsInternal[i].priority) > 0){
					dominated = true;
					break;
				}
			}
			if(!dominated){
				bool dup = false;
				for(size_t k = 0; k < deduped.size(); k++){
					char normK[256];
					NormalizePath(deduped[k].entry.logicalPath, normK, sizeof(normK));
					if(strcmp(normA, normK) == 0 &&
					   strcmp(additionsInternal[i].entry.type, deduped[k].entry.type) == 0){
						dup = true;
						break;
					}
				}
				if(!dup) deduped.push_back(additionsInternal[i]);
			}
		}
		additionsInternal = deduped;
	}

	// 3b) Resolve addition logical paths to physical files and add to pathRedirects
	//     Pick the highest-priority physical file across all mods
	for(size_t ai = 0; ai < additionsInternal.size(); ai++){
		char normalizedLogical[256];
		NormalizePath(additionsInternal[ai].entry.logicalPath, normalizedLogical, sizeof(normalizedLogical));

		// Extract basename+ext from the logical path for fallback matching
		char wantBase[64], wantExt[8];
		ExtractBasenameExt(normalizedLogical, wantBase, sizeof(wantBase),
		                   wantExt, sizeof(wantExt));

		// Find the physical file: exact match preferred, then basename+ext fallback.
		// Among candidates of the same match type, highest priority wins.
		int bestExact = -1, bestFallback = -1;
		for(size_t fi = 0; fi < allModFiles.size(); fi++){
			if(strcmp(allModFiles[fi].logicalPath, normalizedLogical) == 0){
				if(bestExact < 0 || IsBetterModFileCandidate(allModFiles[fi], allModFiles[bestExact]))
					bestExact = (int)fi;
			}else if(strcmp(allModFiles[fi].basename, wantBase) == 0 &&
			         strcmp(allModFiles[fi].ext, wantExt) == 0){
				if(bestFallback < 0 || IsBetterModFileCandidate(allModFiles[fi], allModFiles[bestFallback]))
					bestFallback = (int)fi;
			}
		}
		int bestIdx = (bestExact >= 0) ? bestExact : bestFallback;
		if(bestIdx >= 0){
			// Build a redirect with the *logical* path from the readme
			// mapped to the *physical* path of the winning file
			ModFile redirectMf = allModFiles[bestIdx];
			strncpy(redirectMf.logicalPath, normalizedLogical, sizeof(redirectMf.logicalPath)-1);
			redirectMf.logicalPath[sizeof(redirectMf.logicalPath)-1] = '\0';

			// Only add if not already redirected (structural redirect takes precedence)
			bool dup = false;
			for(size_t k = 0; k < pathRedirects.size(); k++){
				if(strcmp(pathRedirects[k].logicalPath, normalizedLogical) == 0){
					dup = true;
					break;
				}
			}
			if(!dup)
				pathRedirects.push_back(redirectMf);
		}

		// Copy to public additions list
		additions.push_back(additionsInternal[ai].entry);
	}

	// 4) For every IMG redirect/addition, also add a .dir companion redirect
	// Synthesize wrapped stock-path redirects like Map/data/... -> data/...
	AddWrappedStockPathRedirects(basePaths, allModFiles);

	// Also synthesize root-level aliases for stock SA archives before .dir generation.
	AddStockArchiveAliasRedirect("models/gta3.img", "gta3", allModFiles);
	AddStockArchiveAliasRedirect("models/gta_int.img", "gta_int", allModFiles);

	{
		size_t n = pathRedirects.size();
		for(size_t i = 0; i < n; i++){
			if(strcmp(pathRedirects[i].ext, "img") != 0) continue;
			ModFile dirMf = pathRedirects[i];
			// Replace .img with .dir in both logical and physical paths
			char *p;
			p = strrchr(dirMf.logicalPath, '.');
			if(p) strcpy(p+1, "dir");
			p = strrchr(dirMf.physicalPath, '.');
			if(p) strcpy(p+1, "dir");
			strcpy(dirMf.ext, "dir");

			bool dup = false;
			dup = HasRedirectForLogicalPath(dirMf.logicalPath);
			if(!dup)
				pathRedirects.push_back(dirMf);
		}
	}

	active = true;
	log("modloader: %d overrides, %d redirects, %d image-entry overrides, %d additions from %d mods\n",
	    (int)looseOverrides.size(), (int)pathRedirects.size(),
	    (int)imageEntryOverrides.size(),
	    (int)additions.size(), numMods);
}

bool
ModloaderIsActive(void)
{
	return active;
}

bool
BuildModloaderLogicalExportPath(const char *logicalPath, char *dst, size_t size)
{
	if(dst == nil || size == 0 || logicalPath == nil)
		return false;

	char normalized[256];
	NormalizePath(logicalPath, normalized, sizeof(normalized));
	if(normalized[0] == '\0')
		return false;

	char modloaderDir[1024];
	char arianeModDir[1024];
	if(!BuildGameRootedPath(modloaderDir, sizeof(modloaderDir), "modloader") ||
	   !BuildPath(arianeModDir, sizeof(arianeModDir), modloaderDir, "Ariane"))
		return false;

	int written = snprintf(dst, size, "%s/%s", arianeModDir, normalized);
	if(written < 0 || (size_t)written >= size)
		return false;
	rw::makePath(dst);
	return true;
}

const char*
ModloaderFindOverride(const char *basename, const char *ext)
{
	if(!active) return nil;
	char lowerBase[64], lowerExt[8];
	int i;
	for(i = 0; basename[i] && i < 63; i++){
		char c = basename[i];
		if(c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
		lowerBase[i] = c;
	}
	lowerBase[i] = '\0';
	for(i = 0; ext[i] && i < 7; i++){
		char c = ext[i];
		if(c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
		lowerExt[i] = c;
	}
	lowerExt[i] = '\0';
	if(!IsLooseBasenameOverrideExt(lowerExt))
		return nil;

	for(size_t j = 0; j < looseOverrides.size(); j++){
		if(strcmp(looseOverrides[j].basename, lowerBase) == 0 &&
		   strcmp(looseOverrides[j].ext, lowerExt) == 0)
			return looseOverrides[j].physicalPath;
	}
	return nil;
}

const char*
ModloaderRedirectPath(const char *logicalPath)
{
	if(!active) return nil;
	char normalized[256];
	NormalizePath(logicalPath, normalized, sizeof(normalized));
	for(size_t i = 0; i < pathRedirects.size(); i++){
		if(strcmp(pathRedirects[i].logicalPath, normalized) == 0)
			return pathRedirects[i].physicalPath;
	}
	return nil;
}

const char*
ModloaderGetSourcePath(const char *logicalPath)
{
	if(!active) return nil;
	// Check both redirect map and archive-entry override map.
	char normalized[256];
	NormalizePath(logicalPath, normalized, sizeof(normalized));
	for(size_t i = 0; i < pathRedirects.size(); i++){
		if(strcmp(pathRedirects[i].logicalPath, normalized) == 0)
			return pathRedirects[i].physicalPath;
	}

	char archiveLogicalPath[256];
	char entryFilename[32];
	if(ExtractImageEntryOverrideKey(normalized,
	                                archiveLogicalPath, sizeof(archiveLogicalPath),
	                                entryFilename, sizeof(entryFilename))){
		for(size_t i = 0; i < imageEntryOverrides.size(); i++){
			if(strcmp(imageEntryOverrides[i].archiveLogicalPath, archiveLogicalPath) == 0 &&
			   strcmp(imageEntryOverrides[i].entryFilename, entryFilename) == 0)
				return imageEntryOverrides[i].physicalPath;
		}
	}
	return nil;
}

const char*
ModloaderFindImageEntryOverride(const char *archiveLogicalPath, const char *entryFilename)
{
	if(!active || archiveLogicalPath == nil || entryFilename == nil)
		return nil;

	char normalizedArchive[256];
	char normalizedEntry[32];
	NormalizeStockArchiveAlias(archiveLogicalPath, normalizedArchive, sizeof(normalizedArchive));
	NormalizePath(entryFilename, normalizedEntry, sizeof(normalizedEntry));
	for(size_t i = 0; i < imageEntryOverrides.size(); i++){
		if(strcmp(imageEntryOverrides[i].archiveLogicalPath, normalizedArchive) == 0 &&
		   strcmp(imageEntryOverrides[i].entryFilename, normalizedEntry) == 0)
			return imageEntryOverrides[i].physicalPath;
	}
	return nil;
}

uint8*
ReadLooseFile(const char *path, int *size)
{
	FILE *f = fopen(path, "rb");
	if(!f){
		*size = 0;
		return nil;
	}
	fseek(f, 0, SEEK_END);
	int sz = (int)ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8 *buf = (uint8*)malloc(sz);
	fread(buf, 1, sz, f);
	fclose(f);
	*size = sz;
	return buf;
}

int
ModloaderGetAdditions(ModloaderDatEntry *entries, int maxCount)
{
	int n = (int)additions.size();
	if(n > maxCount) n = maxCount;
	for(int i = 0; i < n; i++)
		entries[i] = additions[i];
	return n;
}
