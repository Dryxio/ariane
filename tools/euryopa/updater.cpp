#include "euryopa.h"
#include "version.h"
#include "updater.h"

#ifdef _WIN32
#include <winhttp.h>
#include <shellapi.h>
#include <process.h>
#include <stdio.h>
#include <string.h>
#pragma comment(lib, "winhttp.lib")

// ---- State shared between background thread and main thread ----
// Threading model: write side uses InterlockedExchange (full barrier on x86),
// read side uses plain volatile loads. This is safe on MSVC/x86/x64 due to
// strong hardware memory ordering — once the interlocked write publishes a flag,
// all preceding stores (string buffers etc.) are visible to any core reading that flag.
// Not portable to ARM or non-MSVC compilers; fine for this D3D9-only Windows app.
//
// Update policy: only ARIANE_VERSION bumps trigger user-visible updates.
// SHA-suffixed release tags (v1.0.0-PE-abc1234) are CI bookkeeping only.

static volatile long gUpdateCheckDone = 0;
static volatile long gUpdateAvailable = 0;
static volatile long gUpdateDownloading = 0;
static volatile long gUpdateDownloaded = 0;
static volatile long gUpdateFailed = 0;
static volatile long gDownloadPercent = 0;
static char gNewVersion[64] = {};
static char gDownloadUrl[1024] = {};
static char gChangelog[2048] = {};

// ---- Helpers ----

static void
GetExePath(char *out, int size)
{
	GetModuleFileNameA(nil, out, size);
}

static void
GetUpdatePath(char *out, int size)
{
	char exePath[MAX_PATH] = {};
	GetExePath(exePath, MAX_PATH);
	snprintf(out, size, "%s.update", exePath);
}

static void
GetBatPath(char *out, int size)
{
	char exePath[MAX_PATH] = {};
	GetExePath(exePath, MAX_PATH);
	snprintf(out, size, "%s_update.bat", exePath);
}

// Apply a pending .update file: write the bat, run it, exit
static bool
ApplyPendingUpdate(void)
{
	char exePath[MAX_PATH] = {};
	char updatePath[MAX_PATH] = {};
	char batPath[MAX_PATH] = {};
	GetExePath(exePath, MAX_PATH);
	GetUpdatePath(updatePath, MAX_PATH);
	GetBatPath(batPath, MAX_PATH);

	// Check that the .update file actually exists
	DWORD attr = GetFileAttributesA(updatePath);
	if(attr == INVALID_FILE_ATTRIBUTES)
		return false;

	FILE *bat = fopen(batPath, "w");
	if(!bat){
		// Can't write bat — delete .update to avoid retry loop
		DeleteFileA(updatePath);
		return false;
	}
	fprintf(bat, "@echo off\r\n");
	fprintf(bat, "echo Updating Ariane...\r\n");
	fprintf(bat, "timeout /t 2 /nobreak >nul\r\n");
	fprintf(bat, "move /y \"%s\" \"%s\"\r\n", updatePath, exePath);
	fprintf(bat, "start \"\" \"%s\"\r\n", exePath);
	fprintf(bat, "del \"%%~f0\"\r\n");
	fclose(bat);

	HINSTANCE result = ShellExecuteA(nil, "open", batPath, nil, nil, SW_HIDE);
	if((INT_PTR)result <= 32){
		// Failed to launch — clean up to avoid retry loop on next launch
		DeleteFileA(batPath);
		DeleteFileA(updatePath);
		return false;
	}
	exit(0);
	return true; // not reached
}

// Simple version compare: returns true if remote > local
// Expects format "X.Y.Z"
static bool
IsNewerVersion(const char *remote, const char *local)
{
	int rmaj = 0, rmin = 0, rpat = 0;
	int lmaj = 0, lmin = 0, lpat = 0;
	sscanf(remote, "%d.%d.%d", &rmaj, &rmin, &rpat);
	sscanf(local, "%d.%d.%d", &lmaj, &lmin, &lpat);
	if(rmaj != lmaj) return rmaj > lmaj;
	if(rmin != lmin) return rmin > lmin;
	return rpat > lpat;
}

// Extract a JSON string value for a given key (very simple, no nesting)
static bool
JsonGetString(const char *json, const char *key, char *out, int outsize)
{
	char pattern[128];
	snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	const char *p = strstr(json, pattern);
	if(!p) return false;
	p += strlen(pattern);
	// skip whitespace and colon
	while(*p == ' ' || *p == '\t' || *p == ':' || *p == '\n' || *p == '\r') p++;
	if(*p != '"') return false;
	p++;
	int i = 0;
	while(*p && *p != '"' && i < outsize - 1){
		// handle escaped characters
		if(*p == '\\' && *(p+1)){
			p++;
			if(*p == 'n'){ out[i++] = '\n'; p++; continue; }
			if(*p == 'r'){ p++; continue; }
		}
		out[i++] = *p++;
	}
	out[i] = '\0';
	return i > 0;
}

// WinHTTP GET request, returns malloc'd response body (caller frees)
static char*
HttpGet(const wchar_t *host, const wchar_t *path, int *outSize)
{
	*outSize = 0;
	HINTERNET hSession = WinHttpOpen(L"Ariane-Updater/1.0",
		WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if(!hSession) return nil;

	HINTERNET hConnect = WinHttpConnect(hSession, host,
		INTERNET_DEFAULT_HTTPS_PORT, 0);
	if(!hConnect){
		WinHttpCloseHandle(hSession);
		return nil;
	}

	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path,
		nil, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
		WINHTTP_FLAG_SECURE);
	if(!hRequest){
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return nil;
	}

	if(!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
		WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
	   !WinHttpReceiveResponse(hRequest, nil)){
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return nil;
	}

	// Read response
	char *buf = nil;
	int total = 0;
	int capacity = 0;
	DWORD bytesRead;
	char tmp[4096];
	while(WinHttpReadData(hRequest, tmp, sizeof(tmp), &bytesRead) && bytesRead > 0){
		if(total + (int)bytesRead + 1 > capacity){
			capacity = (total + (int)bytesRead + 1) * 2;
			buf = (char*)realloc(buf, capacity);
		}
		memcpy(buf + total, tmp, bytesRead);
		total += bytesRead;
	}
	if(buf) buf[total] = '\0';
	*outSize = total;

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);
	return buf;
}

// Download a file from a full HTTPS URL, saving to disk
static bool
HttpDownloadFile(const char *url, const char *outPath)
{
	// Parse host and path from URL like https://github.com/...
	if(strncmp(url, "https://", 8) != 0) return false;
	const char *hostStart = url + 8;
	const char *pathStart = strchr(hostStart, '/');
	if(!pathStart) return false;

	int hostLen = (int)(pathStart - hostStart);
	wchar_t whost[256] = {};
	for(int i = 0; i < hostLen && i < 255; i++) whost[i] = hostStart[i];

	int pathLen = (int)strlen(pathStart);
	wchar_t wpath[1024] = {};
	for(int i = 0; i < pathLen && i < 1023; i++) wpath[i] = pathStart[i];

	HINTERNET hSession = WinHttpOpen(L"Ariane-Updater/1.0",
		WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if(!hSession) return false;

	HINTERNET hConnect = WinHttpConnect(hSession, whost,
		INTERNET_DEFAULT_HTTPS_PORT, 0);
	if(!hConnect){
		WinHttpCloseHandle(hSession);
		return false;
	}

	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wpath,
		nil, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
		WINHTTP_FLAG_SECURE);
	if(!hRequest){
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}

	// Follow redirects (GitHub uses them for release assets)
	DWORD opt = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
	WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &opt, sizeof(opt));

	if(!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
		WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
	   !WinHttpReceiveResponse(hRequest, nil)){
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}

	// Get content length for progress
	DWORD contentLength = 0;
	DWORD headerSize = sizeof(contentLength);
	WinHttpQueryHeaders(hRequest,
		WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &contentLength, &headerSize, nil);

	FILE *f = fopen(outPath, "wb");
	if(!f){
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}

	DWORD bytesRead;
	char tmp[8192];
	int totalRead = 0;
	bool ok = true;
	while(WinHttpReadData(hRequest, tmp, sizeof(tmp), &bytesRead) && bytesRead > 0){
		if(fwrite(tmp, 1, bytesRead, f) != bytesRead){
			ok = false;
			break;
		}
		totalRead += bytesRead;
		if(contentLength > 0)
			InterlockedExchange(&gDownloadPercent, (long)((totalRead * 100LL) / contentLength));
	}

	fclose(f);
	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);
	return ok;
}

// ---- Background threads ----

static unsigned __stdcall
CheckThread(void *arg)
{
	// Query GitHub API for the latest release matching our channel
	// Releases are tagged as: v1.0.0-master or v1.0.0-PE
	wchar_t path[512];
	swprintf(path, 512, L"/repos/Dryxio/ariane/releases?per_page=20");

	int size = 0;
	char *json = HttpGet(L"api.github.com", path, &size);
	if(!json || size == 0){
		InterlockedExchange(&gUpdateCheckDone, 1);
		free(json);
		return 0;
	}

	// Find the first release whose tag_name ends with our channel suffix
	char suffix[64];
	snprintf(suffix, sizeof(suffix), "-%s", ARIANE_CHANNEL);

	const char *p = json;
	bool found = false;
	while((p = strstr(p, "\"tag_name\"")) != nil){
		char tag[128] = {};
		const char *cursor = p + 10;
		while(*cursor == ' ' || *cursor == ':' || *cursor == '\t' || *cursor == '"') cursor++;
		int i = 0;
		while(*cursor && *cursor != '"' && i < 127) tag[i++] = *cursor++;
		tag[i] = '\0';

		if(strstr(tag, suffix)){
			const char *ver = tag;
			if(*ver == 'v') ver++;
			char verClean[64] = {};
			for(int j = 0; ver[j] && ver[j] != '-' && j < 63; j++)
				verClean[j] = ver[j];

			if(IsNewerVersion(verClean, ARIANE_VERSION)){
				strncpy(gNewVersion, verClean, sizeof(gNewVersion) - 1);

				const char *assetSearch = p;
				for(int attempts = 0; attempts < 5; attempts++){
					const char *dlUrl = strstr(assetSearch, "\"browser_download_url\"");
					if(!dlUrl) break;
					char url[1024] = {};
					const char *u = dlUrl + 21;
					while(*u == ' ' || *u == ':' || *u == '\t' || *u == '"') u++;
					int k = 0;
					while(*u && *u != '"' && k < 1023) url[k++] = *u++;
					url[k] = '\0';

					if(strstr(url, "ariane.exe") || strstr(url, "ariane-windows")){
						strncpy(gDownloadUrl, url, sizeof(gDownloadUrl) - 1);
						found = true;
						break;
					}
					assetSearch = u;
				}

				const char *nextTag = strstr(p + 1, "\"tag_name\"");
				const char *bodyTag = strstr(p, "\"body\"");
				if(bodyTag && (!nextTag || bodyTag < nextTag)){
					JsonGetString(p, "body", gChangelog, sizeof(gChangelog));
				}

				if(found) break;
			}
		}
		p = cursor;
	}

	// Write data first, then publish the flags with a barrier
	MemoryBarrier();
	InterlockedExchange(&gUpdateAvailable, found ? 1 : 0);
	InterlockedExchange(&gUpdateCheckDone, 1);
	free(json);
	return 0;
}

static unsigned __stdcall
DownloadThread(void *arg)
{
	InterlockedExchange(&gUpdateDownloading, 1);
	InterlockedExchange(&gDownloadPercent, 0);

	char exePath[MAX_PATH] = {};
	char updatePath[MAX_PATH] = {};
	GetExePath(exePath, MAX_PATH);
	GetUpdatePath(updatePath, MAX_PATH);

	if(HttpDownloadFile(gDownloadUrl, updatePath)){
		MemoryBarrier();
		InterlockedExchange(&gUpdateDownloaded, 1);
	}else{
		DeleteFileA(updatePath);
		InterlockedExchange(&gUpdateFailed, 1);
	}

	InterlockedExchange(&gUpdateDownloading, 0);
	return 0;
}

// ---- Public API ----

void
UpdaterCheckForUpdate(void)
{
	// Check for a pending .update file from a previous "Later" click
	char updatePath[MAX_PATH] = {};
	GetUpdatePath(updatePath, MAX_PATH);
	DWORD attr = GetFileAttributesA(updatePath);
	if(attr != INVALID_FILE_ATTRIBUTES){
		ApplyPendingUpdate();
		// If ApplyPendingUpdate fails (e.g. can't write bat), just continue
	}

	_beginthreadex(nil, 0, CheckThread, nil, 0, nil);
}

void
UpdaterDrawGui(void)
{
	if(!gUpdateCheckDone || !gUpdateAvailable)
		return;

	static bool dismissed = false;
	if(dismissed) return;

	ImGuiIO &io = ImGui::GetIO();
	ImVec2 windowPos(io.DisplaySize.x - 10.0f, 10.0f);
	ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always, ImVec2(1.0f, 0.0f));
	ImGui::SetNextWindowSize(ImVec2(340, 0));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_AlwaysAutoResize;

	char title[128];
	snprintf(title, sizeof(title), "Update Available (%s)###updater", ARIANE_CHANNEL);

	if(ImGui::Begin(title, nil, flags)){
		ImGui::Text("New version: %s", gNewVersion);
		ImGui::Text("Current: %s", ARIANE_VERSION);
		ImGui::Text("Channel: %s", ARIANE_CHANNEL);

		if(gChangelog[0]){
			ImGui::Separator();
			ImGui::TextWrapped("%s", gChangelog);
		}

		ImGui::Separator();

		if(gUpdateDownloading){
			ImGui::ProgressBar(gDownloadPercent / 100.0f);
			ImGui::Text("Downloading...");
		}else if(gUpdateDownloaded){
			ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f),
				"Download complete!");
			if(ImGui::Button("Restart Now", ImVec2(-1, 0))){
				ApplyPendingUpdate();
			}
			if(ImGui::Button("Later (apply on next launch)", ImVec2(-1, 0))){
				dismissed = true;
			}
		}else if(gUpdateFailed){
			ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
				"Download failed. Try again later.");
			if(ImGui::Button("Dismiss", ImVec2(-1, 0))){
				dismissed = true;
			}
		}else{
			if(ImGui::Button("Update Now", ImVec2(-1, 0))){
				_beginthreadex(nil, 0, DownloadThread, nil, 0, nil);
			}
			if(ImGui::Button("Skip", ImVec2(-1, 0))){
				dismissed = true;
			}
		}
	}
	ImGui::End();
}

#else
// Non-Windows stubs
void UpdaterCheckForUpdate(void) {}
void UpdaterDrawGui(void) {}
#endif
