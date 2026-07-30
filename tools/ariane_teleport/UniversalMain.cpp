#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdarg>

#include "universal_resource.h"

static HMODULE gUniversalModule = NULL;

enum class ArianeGame {
	Unknown,
	III,
	ViceCity,
	SanAndreas
};

struct GamePayload {
	ArianeGame game;
	int resourceId;
	const char *tag;
};

static bool
EnsureDirectory(const char *path)
{
	if(CreateDirectoryA(path, NULL))
		return true;
	if(GetLastError() != ERROR_ALREADY_EXISTS)
		return false;
	DWORD attributes = GetFileAttributesA(path);
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static bool
GetArianeDirectory(const char *gameDirectory, char *out, size_t size)
{
	int length = snprintf(out, size, "%s\\ariane", gameDirectory);
	if(length < 0 || static_cast<size_t>(length) >= size)
		return false;
	if(EnsureDirectory(out))
		return true;

	// The macOS editor executable is named `ariane`. When it lives beside
	// the game through Parallels Shared Folders, use the editor's existing
	// cross-platform data-directory fallback instead.
	length = snprintf(out, size, "%s\\ariane_data", gameDirectory);
	if(length < 0 || static_cast<size_t>(length) >= size)
		return false;
	return EnsureDirectory(out);
}

static bool
GetUniversalDirectory(char *out, size_t size)
{
	if(out == NULL || size == 0)
		return false;
	DWORD length = GetModuleFileNameA(gUniversalModule, out, static_cast<DWORD>(size));
	if(length == 0 || length >= size)
		return false;
	char *separator = strrchr(out, '\\');
	if(separator == NULL)
		return false;
	*separator = '\0';
	return true;
}

static void
AppendLoaderLog(const char *gameDirectory, const char *format, ...)
{
	char arianeDirectory[MAX_PATH];
	char logPath[MAX_PATH];
	if(!GetArianeDirectory(gameDirectory, arianeDirectory, sizeof(arianeDirectory)))
		return;
	if(snprintf(logPath, sizeof(logPath), "%s\\universal_loader.log", arianeDirectory) >= sizeof(logPath))
		return;

	FILE *file = fopen(logPath, "a");
	if(file == NULL)
		return;
	va_list args;
	va_start(args, format);
	vfprintf(file, format, args);
	va_end(args);
	fputc('\n', file);
	fclose(file);
}

static ArianeGame
DetectGame(void)
{
	char executable[MAX_PATH];
	DWORD length = GetModuleFileNameA(NULL, executable, sizeof(executable));
	if(length == 0 || length >= sizeof(executable))
		return ArianeGame::Unknown;
	const char *name = strrchr(executable, '\\');
	name = name ? name + 1 : executable;
	if(_stricmp(name, "gta3.exe") == 0)
		return ArianeGame::III;
	if(_stricmp(name, "gta-vc.exe") == 0)
		return ArianeGame::ViceCity;
	if(_stricmp(name, "gta_sa.exe") == 0)
		return ArianeGame::SanAndreas;
	return ArianeGame::Unknown;
}

static GamePayload
PayloadForGame(ArianeGame game)
{
	switch(game){
	case ArianeGame::III:
		return { game, IDR_ARIANE_III, "iii" };
	case ArianeGame::ViceCity:
		return { game, IDR_ARIANE_VC, "vc" };
	case ArianeGame::SanAndreas:
		return { game, IDR_ARIANE_SA, "sa" };
	default:
		return { ArianeGame::Unknown, 0, "unknown" };
	}
}

static uint32_t
HashPayload(const unsigned char *data, DWORD size)
{
	uint32_t hash = 2166136261u;
	for(DWORD i = 0; i < size; i++){
		hash ^= data[i];
		hash *= 16777619u;
	}
	return hash;
}

static bool
WritePayloadFile(const char *path, const unsigned char *data, DWORD size)
{
	HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_TEMPORARY, NULL);
	if(file == INVALID_HANDLE_VALUE)
		return false;
	DWORD written = 0;
	bool ok = WriteFile(file, data, size, &written, NULL) != FALSE && written == size;
	if(ok)
		ok = FlushFileBuffers(file) != FALSE;
	CloseHandle(file);
	if(!ok)
		DeleteFileA(path);
	return ok;
}

static DWORD
LoadSelectedPayload(void)
{
	char gameDirectory[MAX_PATH];
	if(!GetUniversalDirectory(gameDirectory, sizeof(gameDirectory)))
		return 1;

	GamePayload payload = PayloadForGame(DetectGame());
	if(payload.game == ArianeGame::Unknown){
		AppendLoaderLog(gameDirectory, "unsupported executable");
		return 2;
	}

	HRSRC resource = FindResourceA(gUniversalModule, MAKEINTRESOURCEA(payload.resourceId), RT_RCDATA);
	if(resource == NULL){
		AppendLoaderLog(gameDirectory, "%s payload resource missing error=%lu", payload.tag, GetLastError());
		return 3;
	}
	HGLOBAL loadedResource = LoadResource(gUniversalModule, resource);
	DWORD payloadSize = SizeofResource(gUniversalModule, resource);
	const unsigned char *payloadData = static_cast<const unsigned char*>(LockResource(loadedResource));
	if(loadedResource == NULL || payloadData == NULL || payloadSize == 0){
		AppendLoaderLog(gameDirectory, "%s payload resource unreadable error=%lu", payload.tag, GetLastError());
		return 4;
	}

	char arianeDirectory[MAX_PATH];
	char runtimeDirectory[MAX_PATH];
	if(!GetArianeDirectory(gameDirectory, arianeDirectory, sizeof(arianeDirectory)) ||
	   snprintf(runtimeDirectory, sizeof(runtimeDirectory), "%s\\.runtime", arianeDirectory) >= sizeof(runtimeDirectory) ||
	   !EnsureDirectory(runtimeDirectory)){
		AppendLoaderLog(gameDirectory, "%s runtime directory unavailable error=%lu", payload.tag, GetLastError());
		return 5;
	}
	SetFileAttributesA(runtimeDirectory, FILE_ATTRIBUTE_HIDDEN);

	uint32_t payloadHash = HashPayload(payloadData, payloadSize);
	char payloadPath[MAX_PATH];
	if(snprintf(payloadPath, sizeof(payloadPath), "%s\\ariane_%s_%08x.dll",
		runtimeDirectory, payload.tag, payloadHash) >= sizeof(payloadPath)){
		AppendLoaderLog(gameDirectory, "%s payload path too long", payload.tag);
		return 6;
	}

	WIN32_FILE_ATTRIBUTE_DATA existing;
	bool fileReady = GetFileAttributesExA(payloadPath, GetFileExInfoStandard, &existing) != FALSE &&
		existing.nFileSizeHigh == 0 && existing.nFileSizeLow == payloadSize;
	if(!fileReady){
		char temporaryPath[MAX_PATH] = {};
		if(snprintf(temporaryPath, sizeof(temporaryPath), "%s.tmp.%lu", payloadPath, GetCurrentProcessId()) >= sizeof(temporaryPath) ||
		   !WritePayloadFile(temporaryPath, payloadData, payloadSize) ||
		   !MoveFileExA(temporaryPath, payloadPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)){
			if(temporaryPath[0] != '\0')
				DeleteFileA(temporaryPath);
			AppendLoaderLog(gameDirectory, "%s payload extraction failed error=%lu", payload.tag, GetLastError());
			return 7;
		}
	}

	HMODULE module = LoadLibraryA(payloadPath);
	if(module == NULL){
		AppendLoaderLog(gameDirectory, "%s payload load failed error=%lu", payload.tag, GetLastError());
		return 8;
	}
	AppendLoaderLog(gameDirectory, "%s payload loaded hash=%08x size=%lu", payload.tag, payloadHash, payloadSize);
	return 0;
}

BOOL WINAPI
DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
	if(reason == DLL_PROCESS_ATTACH){
		gUniversalModule = instance;
		DisableThreadLibraryCalls(instance);
		// plugin-sdk SA installs address-specific hooks during the ASI loader's
		// startup phase. Deferring the embedded payload to a worker thread can
		// race the game initialization and crash before the first frame.
		LoadSelectedPayload();
	}
	return TRUE;
}
