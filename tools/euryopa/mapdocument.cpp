#include "euryopa.h"

static char gIplMapDocumentLogicalPath[1024];
static char gIplMapDocumentPhysicalPath[1024];
static bool gIplMapDocumentExternal;

static bool
buildStreamingFamilyPrefix(const char *scenePath, char *prefix, size_t size)
{
	const char *filename, *ext, *s;
	char *t;

	if(scenePath == nil || size < 8)
		return false;

	filename = strrchr(scenePath, '\\');
	if(filename == nil)
		filename = strrchr(scenePath, '/');
	if(filename == nil)
		filename = scenePath - 1;
	ext = strrchr(filename + 1, '.');
	if(ext == nil)
		return false;

	t = prefix;
	for(s = filename + 1; s != ext && (size_t)(t - prefix) < size - 8; s++)
		*t++ = *s;
	*t = '\0';
	strcat(prefix, "_stream");
	return true;
}

bool
IsIplMapDocumentOpen(void)
{
	return gIplMapDocumentLogicalPath[0] != '\0';
}

bool
IsIplMapDocumentExternal(void)
{
	return IsIplMapDocumentOpen() && gIplMapDocumentExternal;
}

bool
IsExternalIplMapLogicalPath(const char *logicalPath)
{
	return logicalPath != nil &&
	       rw::strncmp_ci(logicalPath, "ariane\\open_map_", 16) == 0;
}

const char*
GetIplMapDocumentLogicalPath(void)
{
	return gIplMapDocumentLogicalPath;
}

const char*
GetIplMapDocumentPhysicalPath(void)
{
	return gIplMapDocumentPhysicalPath;
}

bool
IsInstInIplMapDocument(const ObjectInst *inst)
{
	if(inst == nil || inst->m_file == nil || inst->m_file->name == nil)
		return false;
	// Runtime-opened external IPLs remain visible after leaving document mode,
	// but never silently join whole-world editing or game operations.
	if(!IsIplMapDocumentOpen())
		return !IsExternalIplMapLogicalPath(inst->m_file->name);
	if(inst->m_imageIndex < 0)
		return LogicalPathEquals(inst->m_file->name, gIplMapDocumentLogicalPath);

	char prefix[256];
	if(!isSA() || !buildStreamingFamilyPrefix(gIplMapDocumentLogicalPath, prefix, sizeof(prefix)))
		return false;
	return rw::strncmp_ci(inst->m_file->name, prefix, strlen(prefix)) == 0;
}

void
SetIplMapDocument(const char *logicalPath, const char *physicalPath, bool external)
{
	if(logicalPath == nil)
		logicalPath = "";
	if(physicalPath == nil)
		physicalPath = "";
	strncpy(gIplMapDocumentLogicalPath, logicalPath, sizeof(gIplMapDocumentLogicalPath) - 1);
	gIplMapDocumentLogicalPath[sizeof(gIplMapDocumentLogicalPath) - 1] = '\0';
	strncpy(gIplMapDocumentPhysicalPath, physicalPath, sizeof(gIplMapDocumentPhysicalPath) - 1);
	gIplMapDocumentPhysicalPath[sizeof(gIplMapDocumentPhysicalPath) - 1] = '\0';
	gIplMapDocumentExternal = external;
}

void
CloseIplMapDocument(void)
{
	gIplMapDocumentLogicalPath[0] = '\0';
	gIplMapDocumentPhysicalPath[0] = '\0';
	gIplMapDocumentExternal = false;
}
