#include "euryopa.h"
#include "version.h"
#include "telemetry.h"

#ifdef _WIN32
#include <winhttp.h>
#include <wincrypt.h>
#include <process.h>
#include <stdio.h>
#include <string.h>
#include <shlobj.h>
#pragma comment(lib, "advapi32.lib")

#define TELEMETRY_HOST L"gtastuff.namecdsl.xyz"
#define TELEMETRY_PATH L"/ariane/telemetry/ping"

// ---- Machine UID ----
// Generates a random UID on first launch and persists it to a file.
// No hardware fingerprinting — just a random identifier for dedup.

static bool
GetUidPath(char *out, int size)
{
	char appdata[MAX_PATH] = {};
	if(SHGetFolderPathA(nil, CSIDL_LOCAL_APPDATA, nil, 0, appdata) != S_OK)
		return false;
	snprintf(out, size, "%s\\Ariane", appdata);
	CreateDirectoryA(out, nil);
	snprintf(out, size, "%s\\Ariane\\uid.txt", appdata);
	return true;
}

static bool
ReadUid(char *out, int size)
{
	char path[MAX_PATH] = {};
	if(!GetUidPath(path, MAX_PATH)) return false;

	FILE *f = fopen(path, "r");
	if(!f) return false;
	if(!fgets(out, size, f)){
		fclose(f);
		return false;
	}
	fclose(f);
	// Strip newline
	int len = (int)strlen(out);
	while(len > 0 && (out[len-1] == '\n' || out[len-1] == '\r'))
		out[--len] = '\0';
	return len > 0;
}

static void
GenerateUid(char *out, int size)
{
	// Simple random hex UID (32 chars)
	HCRYPTPROV hProv = 0;
	unsigned char bytes[16] = {};
	if(CryptAcquireContextA(&hProv, nil, nil, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)){
		CryptGenRandom(hProv, 16, bytes);
		CryptReleaseContext(hProv, 0);
	}else{
		// Fallback: use tick count + pid
		DWORD tick = GetTickCount();
		DWORD pid = GetCurrentProcessId();
		memcpy(bytes, &tick, 4);
		memcpy(bytes + 4, &pid, 4);
	}
	for(int i = 0; i < 16; i++)
		snprintf(out + i*2, size - i*2, "%02x", bytes[i]);
}

static bool
GetOrCreateUid(char *out, int size)
{
	if(ReadUid(out, size))
		return true;

	// Generate and persist — if we can't persist, skip telemetry
	// to avoid inflating unique user counts with ephemeral UIDs
	char path[MAX_PATH] = {};
	if(!GetUidPath(path, MAX_PATH))
		return false;

	GenerateUid(out, size);

	FILE *f = fopen(path, "w");
	if(!f)
		return false;
	fputs(out, f);
	fclose(f);
	return true;
}

// ---- HTTP POST ----

static bool
HttpPost(const wchar_t *host, const wchar_t *path, const char *body, int bodyLen)
{
	HINTERNET hSession = WinHttpOpen(L"Ariane-Telemetry/1.0",
		WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if(!hSession) return false;

	HINTERNET hConnect = WinHttpConnect(hSession, host,
		INTERNET_DEFAULT_HTTPS_PORT, 0);
	if(!hConnect){
		WinHttpCloseHandle(hSession);
		return false;
	}

	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path,
		nil, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
		WINHTTP_FLAG_SECURE);
	if(!hRequest){
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}

	const wchar_t *headers = L"Content-Type: application/json\r\n";
	BOOL ok = WinHttpSendRequest(hRequest, headers, -1,
		(LPVOID)body, bodyLen, bodyLen, 0);
	if(ok)
		ok = WinHttpReceiveResponse(hRequest, nil);

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);
	return ok != FALSE;
}

// ---- Background thread ----

static unsigned __stdcall
TelemetryThread(void *arg)
{
	char uid[64] = {};
	if(!GetOrCreateUid(uid, sizeof(uid)))
		return 0;

	char body[512];
	snprintf(body, sizeof(body),
		"{\"uid\":\"%s\",\"version\":\"%s\",\"channel\":\"%s\"}",
		uid, ARIANE_VERSION, ARIANE_CHANNEL);

	HttpPost(TELEMETRY_HOST, TELEMETRY_PATH, body, (int)strlen(body));
	return 0;
}

void
TelemetrySendPing(void)
{
	HANDLE h = (HANDLE)_beginthreadex(nil, 0, TelemetryThread, nil, 0, nil);
	if(h) CloseHandle(h);
}

#else
// Non-Windows stub
void TelemetrySendPing(void) {}
#endif
