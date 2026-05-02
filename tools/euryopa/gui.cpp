#include "euryopa.h"
#include "autocol.h"
#include "modloader.h"
#include "imgui/imgui_internal.h"
#include "object_categories.h"
#include "telemetry.h"
#include "updater.h"
#include "icons.h"
#include <string>
#include <vector>
#include <unordered_map>

int gUILanguage = 0; // 0 = EN, 1 = RU
static std::unordered_map<std::string, const char*> gDictRU = {
	{"File", "Файл"},
	{" Save All IPLs", " Сохранить все IPL"},
	{"Saves all modified objects in their respective placement files (.ipl).", "Сохраняет все измененные объекты в их файлы размещения (.ipl)."},
	{" Test in Game", " Проверить в игре"},
	{"Launches your game and spawns you to the current camera position.\nRequires ariane.asi installed in your game folder.", "Запускает игру и перемещает вас в текущую позицию камеры.\nТребует ariane.asi в папке с игрой."},
	{"Save to Modloader", "Сохранять в Modloader"},
	{"When enabled, saves go to modloader/Ariane/ instead of\noverwriting original game files.", "Когда включено, сохранения идут в modloader/Ariane/ вместо перезаписи оригинальных файлов игры."},
	{"Save New Objects to Original IPL", "Сохранять новые объекты в оригинальный IPL"},
	{"When enabled, newly placed objects save to their original IPL file\ninstead of custom.ipl. Helps keep objects organized by area.", "Когда включено, новые объекты сохраняются в оригинальный файл IPL вместо custom.ipl."},
	{" Hot Reload", " Горячая перезагрузка"},
	{"Instantly apply your changes in a running SA game without restarting.\nRequires ariane.asi.", "Мгновенно применяет изменения в запущенной игре SA без перезапуска.\nТребует ariane.asi."},
	{" Export Prefab...", " Экспорт префаба..."},
	{"Saves the selected objects as a reusable prefab file (.ariane)\nthat you can import later or share.", "Сохраняет выделенные объекты как переиспользуемый файл префаба (.ariane)."},
	{" Import Prefab...", " Импорт префаба..."},
	{"Loads a previously exported prefab file and places those\nobjects into the current map.", "Загружает ранее экспортированный файл префаба на текущую карту."},
	{" Import Custom Object...", " Импорт своего объекта..."},
	{"Import a custom DFF/TXD into the editor as a new placeable object.\nAutomatically registers it in your game files, ready to use in game.", "Импортирует DFF/TXD в редактор как новый объект. Автоматически регистрирует в файлах игры."},
	{" Exit", " Выход"},
	{" Window", " Окно"},
	{" Time & Weather", " Время и Погода"},
	{" View", " Вид"},
	{" Rendering", " Рендеринг"},
	{" Tools", " Инструменты"},
	{" Object Info", " Информация об объекте"},
	{" Editor", " Редактор"},
	{" Object Browser", " Браузер объектов"},
	{" Changes", " Изменения"},
	{" Log ", " Журнал "},
	{" Help", " Справка"},
	{" Notifications", " Уведомления"},
	{"Interior", "Интерьер"},
	{"UI", "Интерфейс"},
	{"Translation", "Позиция"},
	{"Rotation", "Вращение"},
	{"Model", "Модель"},
	{"Options", "Настройки"},
	{"Language", "Язык (Language)"},
	{"What's New in 1.33", "Что нового в 1.33"},
	{"Russian Localization: Full UI translation with EN/RU toggle in Options.", "Русская локализация: Полный перевод интерфейса с переключателем в Настройках."},
	{"Manual Transform Input: Type precise Position and Rotation values in Object Info.", "Ручной ввод координат: Вводите точные значения позиции и поворота (XYZ) в Инфо объекта."},
	{"Fixed Keyboard Shortcuts: Hotkeys are now responsive even when windows are focused.", "Горячие клавиши: Быстрый отклик кнопок даже при фокусе на окнах."},
	{"Stability Fix: Corrected IDE manifest injection to prevent map load crashes.", "Стабильность: Исправлена запись IDE манифеста для предотвращения вылетов."},
	{"Rotation (deg)", "Вращение (град)"}
};

const char* _(const char* text) {
	if (gUILanguage == 1) {
		auto it = gDictRU.find(text);
		if (it != gDictRU.end())
			return it->second;
	}
	return text;
}

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <Psapi.h>
#pragma comment(lib, "Psapi.lib")
#else
#include <dirent.h>
#endif

static bool showDemoWindow;
static bool showEditorWindow;
static bool showInstanceWindow;
static bool showLogWindow;
static bool showHelpWindow;

static bool showTimeWeatherWindow;
static bool showViewWindow;
static bool showRenderingWindow;
static bool showBrowserWindow;
static bool showDiffWindow;
static bool showToolsWindow = true;
static bool gSaNodeJustSelected;
static bool gBrowserIdeListDirty = true;
static char gIplFilterSearch[128];
static char gEditorCameraName[256] = "default";

static bool gAutomaticBackupsEnabled = true;
static int gAutomaticBackupIntervalSeconds = 300;
static int gAutomaticBackupKeepCount = 10;
static int gCustomImportPreferredStartId = 18631;
static const float gAutomaticBackupIdleSeconds = 5.0f;
static float gAutomaticBackupSecondsSinceLastRun = 0.0f;
static float gAutomaticBackupSecondsSinceLastChange = 0.0f;
static uint32 gAutomaticBackupLastSeenSeq = 0;
static uint32 gAutomaticBackupLastHandledSeq = 0;
static char gAutomaticBackupLastSnapshot[1024];

static ImGuiTextFilter gEditorModelFilter;
static ImGuiTextFilter gEditorTxdFilter;
static bool gEditorHighlightMatches;

static ImGuiTextFilter gBrowserCategoryFilter;
static ImGuiTextFilter gBrowserIdeFilter;
static ImGuiTextFilter gBrowserSearchFilter;
static ImGuiTextFilter gBrowserFavFilter;
static int gBrowserSelectedCategory = -1;
static char gBrowserSelectedIde[256];
static bool gBrowserTabRestorePending;
static int gDiffFilter;
static int gRenderMode;

enum BrowserTabId
{
	BROWSER_TAB_CATEGORIES,
	BROWSER_TAB_IDE,
	BROWSER_TAB_SEARCH,
	BROWSER_TAB_FAVOURITES
};
static int gBrowserActiveTab = BROWSER_TAB_CATEGORIES;

struct SavedIplVisibilityState
{
	char key[256];
	bool visible;
};
static std::vector<SavedIplVisibilityState> gSavedIplVisibilityStates;

static bool gSavedWindowStateLoaded;
static bool gSavedWindowPlacementValid;
static bool gSavedWindowPlacementApplied;
static int gSavedWindowX;
static int gSavedWindowY;
static int gSavedWindowWidth = 1280;
static int gSavedWindowHeight = 800;
static bool gSavedWindowMaximized;
static float gSettingsAutosaveSeconds;
static bool gPersistentSettingsLoaded;

static void loadSaveSettings(void);
static void saveSaveSettings(void);
static void normalizePersistentSettings(void);
static bool splitSettingLine(const char *line, char *key, size_t keySize, const char **value);
static bool parseIntSetting(const char *value, int *out);

static uint32
sanitizeAASamples(uint32 samples, uint32 maxSamples = 0)
{
	if(samples <= 1)
		return 1;

	switch(samples){
	case 2:
	case 4:
	case 8:
	case 16:
		break;
	default:
		return 1;
	}

	if(maxSamples > 1){
		while(samples > maxSamples && samples > 1)
			samples >>= 1;
		if(samples < 2)
			return 1;
	}

	return samples;
}

static const char*
getAASamplesLabel(uint32 samples)
{
	switch(samples){
	case 1: return "Off";
	case 2: return "2x MSAA";
	case 4: return "4x MSAA";
	case 8: return "8x MSAA";
	case 16: return "16x MSAA";
	default: return "Custom";
	}
}

void
LoadInitialAntialiasingSettings(void)
{
	FILE *f;
	char line[1024];
	char key[128];
	const char *value;
	int intValue;

	gRequestedAASamples = 1;

	f = fopenArianeDataRead("savesettings.txt", "savesettings.txt");
	if(f == nil)
		return;

	while(fgets(line, sizeof(line), f)){
		if(!splitSettingLine(line, key, sizeof(key), &value))
			continue;
		if(strcmp(key, "aa_samples") == 0 && parseIntSetting(value, &intValue)){
			gRequestedAASamples = sanitizeAASamples((uint32)max(intValue, 1));
			break;
		}
	}

	fclose(f);
}

static int
getDefaultCustomImportStartId(void)
{
	return isSA() ? 18631 : 0;
}

static void
sanitizeCustomImportSettings(void)
{
	if(gCustomImportPreferredStartId < 0 || gCustomImportPreferredStartId >= NUMOBJECTDEFS)
		gCustomImportPreferredStartId = getDefaultCustomImportStartId();
}

static bool
getEditorRootDirectory(char *dir, size_t size)
{
	if(size == 0)
		return false;

#ifdef _WIN32
	DWORD len = GetModuleFileNameA(nil, dir, (DWORD)size);
	if(len > 0 && len < size){
		for(int i = (int)len - 1; i >= 0; i--){
			if(dir[i] == '\\' || dir[i] == '/'){
				dir[i] = '\0';
				return true;
			}
		}
	}

	len = GetCurrentDirectoryA((DWORD)size, dir);
	return len > 0 && len < size;
#else
	strncpy(dir, ".", size);
	dir[size - 1] = '\0';
	return true;
#endif
}

static bool
buildPath(char *dst, size_t size, const char *dir, const char *name)
{
	if(size == 0)
		return false;
	if(dir == nil || dir[0] == '\0')
		return snprintf(dst, size, "%s", name) < (int)size;

	size_t len = strlen(dir);
#ifdef _WIN32
	const char *sep = (dir[len-1] == '\\' || dir[len-1] == '/') ? "" : "\\";
#else
	const char *sep = (dir[len-1] == '/') ? "" : "/";
#endif
	return snprintf(dst, size, "%s%s%s", dir, sep, name) < (int)size;
}

static bool
getLegacyRootPath(char *dst, size_t size, const char *name)
{
	char rootDir[2048];
	return getEditorRootDirectory(rootDir, sizeof(rootDir)) &&
	       buildPath(dst, size, rootDir, name);
}

static const char*
skipSpaces(const char *p)
{
	while(*p && isspace((unsigned char)*p))
		p++;
	return p;
}

static bool
splitSettingLine(const char *line, char *key, size_t keySize, const char **value)
{
	size_t len;
	const char *p = skipSpaces(line);
	if(*p == '\0' || *p == '#')
		return false;

	len = 0;
	while(p[len] && !isspace((unsigned char)p[len]))
		len++;
	if(len == 0 || len >= keySize)
		return false;

	memcpy(key, p, len);
	key[len] = '\0';
	*value = skipSpaces(p + len);
	return true;
}

static bool
parseIntSetting(const char *value, int *out)
{
	return sscanf(skipSpaces(value), "%d", out) == 1;
}

static bool
parseFloatSetting(const char *value, float *out)
{
	return sscanf(skipSpaces(value), "%f", out) == 1;
}

static bool
parseBoolSetting(const char *value, bool *out)
{
	int i;
	if(!parseIntSetting(value, &i))
		return false;
	*out = i != 0;
	return true;
}

static bool
parseVec3Setting(const char *value, rw::V3d *out)
{
	return sscanf(skipSpaces(value), "%f %f %f", &out->x, &out->y, &out->z) == 3;
}

static bool
parseQuotedStringValue(const char *value, char *out, size_t outSize, const char **after = nil)
{
	size_t len = 0;
	const char *p = skipSpaces(value);
	if(*p != '"' || outSize == 0)
		return false;
	p++;

	while(*p && *p != '"'){
		char c = *p++;
		if(c == '\\'){
			c = *p++;
			if(c == '\0')
				return false;
			switch(c){
			case 'n': c = '\n'; break;
			case 'r': c = '\r'; break;
			case 't': c = '\t'; break;
			case '\\':
			case '"':
				break;
			default:
				break;
			}
		}
		if(len + 1 < outSize)
			out[len++] = c;
	}
	if(*p != '"')
		return false;

	out[len] = '\0';
	if(after)
		*after = p + 1;
	return true;
}

static void
writeQuotedSetting(FILE *f, const char *key, const char *value)
{
	fprintf(f, "%s \"", key);
	if(value){
		for(const char *p = value; *p; p++){
			switch(*p){
			case '\\':
			case '"':
				fputc('\\', f);
				fputc(*p, f);
				break;
			case '\n':
				fputs("\\n", f);
				break;
			case '\r':
				fputs("\\r", f);
				break;
			case '\t':
				fputs("\\t", f);
				break;
			default:
				fputc(*p, f);
				break;
			}
		}
	}
	fputs("\"\n", f);
}

static void
writeInlineQuotedString(FILE *f, const char *value)
{
	fputc('"', f);
	if(value){
		for(const char *p = value; *p; p++){
			switch(*p){
			case '\\':
			case '"':
				fputc('\\', f);
				fputc(*p, f);
				break;
			case '\n':
				fputs("\\n", f);
				break;
			case '\r':
				fputs("\\r", f);
				break;
			case '\t':
				fputs("\\t", f);
				break;
			default:
				fputc(*p, f);
				break;
			}
		}
	}
	fputc('"', f);
}

static void
setTextFilterValue(ImGuiTextFilter &filter, const char *value)
{
	if(value == nil || value[0] == '\0'){
		filter.Clear();
		return;
	}
	strncpy(filter.InputBuf, value, IM_ARRAYSIZE(filter.InputBuf)-1);
	filter.InputBuf[IM_ARRAYSIZE(filter.InputBuf)-1] = '\0';
	filter.Build();
}

static void
loadWindowStateFromSettingsFile(void)
{
	FILE *f;
	char line[512];
	char key[128];
	const char *value;
	bool haveWindowX = false;
	bool haveWindowY = false;

	if(gSavedWindowStateLoaded)
		return;
	gSavedWindowStateLoaded = true;

	f = fopenArianeDataRead("savesettings.txt", "savesettings.txt");
	if(f == nil)
		return;

	while(fgets(line, sizeof(line), f)){
		if(!splitSettingLine(line, key, sizeof(key), &value))
			continue;
		if(strcmp(key, "window_width") == 0){
			parseIntSetting(value, &gSavedWindowWidth);
		}else if(strcmp(key, "window_height") == 0){
			parseIntSetting(value, &gSavedWindowHeight);
		}else if(strcmp(key, "window_x") == 0){
			haveWindowX = parseIntSetting(value, &gSavedWindowX);
		}else if(strcmp(key, "window_y") == 0){
			haveWindowY = parseIntSetting(value, &gSavedWindowY);
		}else if(strcmp(key, "window_maximized") == 0){
			parseBoolSetting(value, &gSavedWindowMaximized);
		}
	}
	fclose(f);

	gSavedWindowWidth = clamp(gSavedWindowWidth, 640, 8192);
	gSavedWindowHeight = clamp(gSavedWindowHeight, 480, 8192);
	gSavedWindowPlacementValid = haveWindowX && haveWindowY;
}

void
LoadInitialEditorWindowState(int *width, int *height)
{
	loadWindowStateFromSettingsFile();
	if(width)
		*width = gSavedWindowWidth;
	if(height)
		*height = gSavedWindowHeight;
}

void
OnEditorWindowResized(int width, int height)
{
	gSavedWindowWidth = clamp(width, 1, 8192);
	gSavedWindowHeight = clamp(height, 1, 8192);
}

#ifdef _WIN32
static HWND
getEditorWindowHandle(void)
{
#ifdef RW_D3D9
	return (HWND)engineOpenParams.window;
#else
	return nil;
#endif
}
#endif

void
ApplyInitialEditorWindowState(void)
{
	loadWindowStateFromSettingsFile();
	if(gSavedWindowPlacementApplied)
		return;
	gSavedWindowPlacementApplied = true;

#ifdef _WIN32
	HWND hwnd = getEditorWindowHandle();
	if(hwnd && gSavedWindowPlacementValid){
		SetWindowPos(hwnd, nil, gSavedWindowX, gSavedWindowY,
			gSavedWindowWidth, gSavedWindowHeight,
			SWP_NOZORDER | SWP_NOACTIVATE);
		if(gSavedWindowMaximized)
			ShowWindow(hwnd, SW_MAXIMIZE);
	}
#endif
}

void
UpdateEditorWindowState(void)
{
#ifdef _WIN32
	HWND hwnd = getEditorWindowHandle();
	if(hwnd){
		WINDOWPLACEMENT placement;
		placement.length = sizeof(placement);
		if(GetWindowPlacement(hwnd, &placement)){
			RECT rect = placement.rcNormalPosition;
			if(placement.showCmd == SW_SHOWMAXIMIZED){
				gSavedWindowMaximized = true;
			}else if(placement.showCmd == SW_SHOWMINIMIZED || placement.showCmd == SW_MINIMIZE ||
			          placement.showCmd == SW_SHOWMINNOACTIVE){
				gSavedWindowMaximized = (placement.flags & WPF_RESTORETOMAXIMIZED) != 0;
			}else{
				GetWindowRect(hwnd, &rect);
				gSavedWindowMaximized = false;
			}

			gSavedWindowX = rect.left;
			gSavedWindowY = rect.top;
			gSavedWindowWidth = max((int)(rect.right - rect.left), 1);
			gSavedWindowHeight = max((int)(rect.bottom - rect.top), 1);
			gSavedWindowPlacementValid = true;
		}
	}
#endif
}

static void
normalizePersistentSettings(void)
{
	currentHour = ((currentHour % 24) + 24) % 24;
	currentMinute = ((currentMinute % 60) + 60) % 60;
	if(params.numAreas > 0)
		currentArea = clamp(currentArea, 0, params.numAreas-1);
	else
		currentArea = 0;
	if(params.numWeathers > 0){
		Weather::oldWeather = clamp(Weather::oldWeather, 0, params.numWeathers-1);
		Weather::newWeather = clamp(Weather::newWeather, 0, params.numWeathers-1);
	}else{
		Weather::oldWeather = 0;
		Weather::newWeather = 0;
	}
	if(params.timecycle != GAME_III && params.numExtraColours > 0 && params.numHours > 0)
		extraColours = clamp(extraColours, -1, params.numExtraColours*params.numHours - 1);
	else
		extraColours = -1;
	Weather::interpolation = clamp(Weather::interpolation, 0.0f, 1.0f);
	TheCamera.m_fov = clamp(TheCamera.m_fov, 1.0f, 150.0f);
	TheCamera.m_LODmult = clamp(TheCamera.m_LODmult, 0.5f, 3.0f);
	gFlyFastMul = clamp(gFlyFastMul, 1.0f, 10.0f);
	gFlySlowMul = clamp(gFlySlowMul, 0.05f, 1.0f);
	gFovWheelStep = clamp(gFovWheelStep, 0.1f, 15.0f);
	gNeoLightMapStrength = clamp(gNeoLightMapStrength, 0.0f, 1.0f);
	gDayNightBalance = clamp(gDayNightBalance, 0.0f, 1.0f);
	gWetRoadEffect = clamp(gWetRoadEffect, 0.0f, 1.0f);
	gSaPedPathWalkerCount = clamp(gSaPedPathWalkerCount, 1, 32);
	gSaCarPathTrafficCount = clamp(gSaCarPathTrafficCount, 1, 32);
	gSaCarPathTrafficSpeedScale = clamp(gSaCarPathTrafficSpeedScale, 0.25f, 3.0f);
	gSaCarPathParkedCarCount = clamp(gSaCarPathParkedCarCount, 1, 24);
	gRenderMode = clamp(gRenderMode, 0, 2);
	gRenderOnlyHD = gRenderMode == 1;
	gRenderOnlyLod = gRenderMode == 2;
	gGizmoMode = gGizmoMode == GIZMO_ROTATE ? GIZMO_ROTATE : GIZMO_TRANSLATE;
	gBrowserActiveTab = clamp(gBrowserActiveTab, (int)BROWSER_TAB_CATEGORIES, (int)BROWSER_TAB_FAVOURITES);
	gBrowserSelectedCategory = clamp(gBrowserSelectedCategory, -1, NUM_OBJ_CATEGORIES-1);
	gDiffFilter = max(gDiffFilter, 0);
	WaterLevel::gWaterSubMode = clamp(WaterLevel::gWaterSubMode, 0, 1);
	WaterLevel::gWaterCreateShape = clamp(WaterLevel::gWaterCreateShape, 0, 1);
	WaterLevel::gWaterSnapSize = clamp(WaterLevel::gWaterSnapSize, 1.0f, 100.0f);
	params.alphaRef = clamp(params.alphaRef, 0, 255);

	switch(params.timecycle){
	case GAME_SA:
		if(gColourFilter == PLATFORM_XBOX)
			gColourFilter = PLATFORM_PC;
		if(gColourFilter != 0 && gColourFilter != PLATFORM_PS2 &&
		   gColourFilter != PLATFORM_PC)
			gColourFilter = PLATFORM_PC;
		break;
	case GAME_LCS:
		gRadiosity = false;
		if(gColourFilter != 0 && gColourFilter != PLATFORM_PS2 &&
		   gColourFilter != PLATFORM_PSP)
			gColourFilter = 0;
		break;
	case GAME_VCS:
		if(gColourFilter != 0 && gColourFilter != PLATFORM_PS2 &&
		   gColourFilter != PLATFORM_PSP)
			gColourFilter = 0;
		break;
	default:
		gColourFilter = 0;
		gRadiosity = false;
		break;
	}

	if(params.daynightPipe){
		if(gBuildingPipeSwitch != PLATFORM_PS2 &&
		   gBuildingPipeSwitch != PLATFORM_PC &&
		   gBuildingPipeSwitch != PLATFORM_XBOX)
			gBuildingPipeSwitch = PLATFORM_PS2;
	}else if(params.leedsPipe){
		if(gBuildingPipeSwitch != PLATFORM_NULL &&
		   gBuildingPipeSwitch != PLATFORM_PSP &&
		   gBuildingPipeSwitch != PLATFORM_PS2 &&
		   gBuildingPipeSwitch != PLATFORM_PC)
			gBuildingPipeSwitch = PLATFORM_PS2;
	}else
		gBuildingPipeSwitch = PLATFORM_PS2;
}

static void
sanitizeAutomaticBackupSettings(void)
{
	if(gAutomaticBackupIntervalSeconds < 10)
		gAutomaticBackupIntervalSeconds = 10;
	if(gAutomaticBackupIntervalSeconds > 24*60*60)
		gAutomaticBackupIntervalSeconds = 24*60*60;
	if(gAutomaticBackupKeepCount < 1)
		gAutomaticBackupKeepCount = 1;
	if(gAutomaticBackupKeepCount > 100)
		gAutomaticBackupKeepCount = 100;
}

static bool
runAutomaticBackup(bool manual)
{
	char backupRoot[2048];
	FileLoader::AutomaticBackupResult result;
	uint32 latestSeq = GetLatestChangeSeq();

	sanitizeAutomaticBackupSettings();
	if(!GetArianeDataPath(backupRoot, sizeof(backupRoot), "automatic_backups")){
		if(manual)
			Toast(TOAST_SAVE, "Automatic Backup: failed to resolve backup folder");
		return false;
	}

	result = FileLoader::CreateAutomaticBackup(backupRoot, gAutomaticBackupKeepCount);
	gAutomaticBackupSecondsSinceLastRun = 0.0f;

	if(result.createdSnapshot){
		if(!result.hadWarnings)
			gAutomaticBackupLastHandledSeq = latestSeq;
		strncpy(gAutomaticBackupLastSnapshot, result.snapshotPath, sizeof(gAutomaticBackupLastSnapshot));
		gAutomaticBackupLastSnapshot[sizeof(gAutomaticBackupLastSnapshot)-1] = '\0';
		if(manual)
			Toast(TOAST_SAVE, "Automatic Backup: %d text + %d streamed file(s)",
			      result.numTextFiles, result.numBinaryFiles);
		else
			log("Automatic Backup: created %s (%d text, %d streamed, %d warning(s))\n",
			    result.snapshotPath, result.numTextFiles, result.numBinaryFiles, result.numErrors);
		if(!manual && result.hadWarnings)
			Toast(TOAST_SAVE, "Automatic Backup: created with %d warning(s)", result.numErrors);
		return true;
	}

	if(result.numErrors > 0){
		Toast(TOAST_SAVE, "Automatic Backup: failed with %d warning(s)", result.numErrors);
		return false;
	}

	gAutomaticBackupLastHandledSeq = latestSeq;
	if(manual)
		Toast(TOAST_SAVE, "Automatic Backup: nothing to back up");
	return false;
}

static void
automaticBackupTick(void)
{
	float dt = ImGui::GetIO().DeltaTime;
	uint32 latestSeq = GetLatestChangeSeq();

	if(latestSeq != gAutomaticBackupLastSeenSeq){
		gAutomaticBackupLastSeenSeq = latestSeq;
		gAutomaticBackupSecondsSinceLastChange = 0.0f;
	}else
		gAutomaticBackupSecondsSinceLastChange += dt;

	gAutomaticBackupSecondsSinceLastRun += dt;

	if(!gAutomaticBackupsEnabled)
		return;
	if(latestSeq == gAutomaticBackupLastHandledSeq)
		return;
	if(gAutomaticBackupSecondsSinceLastRun < (float)gAutomaticBackupIntervalSeconds)
		return;
	if(gAutomaticBackupSecondsSinceLastChange < gAutomaticBackupIdleSeconds)
		return;
	if(gGizmoUsing)
		return;

	runAutomaticBackup(false);
}

#ifdef _WIN32
static bool
findFileRecursive(const char *dir, const char *name)
{
	char searchPath[2048];
	if(!buildPath(searchPath, sizeof(searchPath), dir, "*"))
		return false;

	WIN32_FIND_DATAA entry;
	HANDLE handle = FindFirstFileA(searchPath, &entry);
	if(handle == INVALID_HANDLE_VALUE)
		return false;

	bool found = false;
	do{
		if(strcmp(entry.cFileName, ".") == 0 || strcmp(entry.cFileName, "..") == 0)
			continue;

		if(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){
			if(entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
				continue;

			char childDir[2048];
			if(buildPath(childDir, sizeof(childDir), dir, entry.cFileName) &&
			   findFileRecursive(childDir, name)){
				found = true;
				break;
			}
		}else if(_stricmp(entry.cFileName, name) == 0){
			found = true;
			break;
		}
	}while(FindNextFileA(handle, &entry));

	FindClose(handle);
	return found;
}
#endif

static const char*
getRequiredTeleportAsiName(void)
{
	if(isIII() || isVC() || isSA()) return "ariane.asi";
	return nil;
}

static bool
warnMissingTeleportAsi(const char *actionName)
{
	const char *asiName = getRequiredTeleportAsiName();
	if(asiName == nil)
		return true;

	char rootDir[2048];
	if(!getEditorRootDirectory(rootDir, sizeof(rootDir)))
		return true;

#ifdef _WIN32
	if(findFileRecursive(rootDir, asiName))
		return true;

	char message[1024];
	snprintf(message, sizeof(message),
		"%s will not work because %s was not found under:\n%s\n\n"
		"Install the Ariane plugin in the game folder (root or subfolder) and try again.",
		actionName, asiName, rootDir);
	MessageBoxA(nil, message, "Ariane", MB_OK | MB_ICONWARNING);
	Toast(TOAST_SAVE, "%s will not work: missing %s", actionName, asiName);
	return false;
#else
	return true;
#endif
}

static const char*
getCurrentGameExecutableName(void)
{
	if(isIII()) return "gta3.exe";
	if(isVC()) return "gta-vc.exe";
	if(isSA()) return "gta_sa.exe";
	return nil;
}

#ifdef _WIN32
static bool
isProcessRunningByName(const char *exeName)
{
	if(exeName == nil || exeName[0] == '\0')
		return false;

	DWORD processIds[2048];
	DWORD bytesReturned = 0;
	if(!EnumProcesses(processIds, sizeof(processIds), &bytesReturned))
		return false;

	const DWORD processCount = bytesReturned / sizeof(DWORD);
	for(DWORD i = 0; i < processCount; i++){
		const DWORD pid = processIds[i];
		if(pid == 0)
			continue;

		HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
		if(process == nil)
			continue;

		char imagePath[MAX_PATH];
		DWORD imagePathSize = sizeof(imagePath);
		bool match = false;
		if(QueryFullProcessImageNameA(process, 0, imagePath, &imagePathSize)){
			const char *baseName = strrchr(imagePath, '\\');
			if(baseName == nil)
				baseName = strrchr(imagePath, '/');
			baseName = baseName ? baseName + 1 : imagePath;
			match = _stricmp(baseName, exeName) == 0;
		}
		CloseHandle(process);

		if(match)
			return true;
	}
	return false;
}
#endif

static bool
isCurrentGameRunning(void)
{
#ifdef _WIN32
	const char *exeName = getCurrentGameExecutableName();
	return exeName && isProcessRunningByName(exeName);
#else
	return false;
#endif
}

// Toast notification system
#define TOAST_MAX 5
#define TOAST_DURATION 2.0f
#define TOAST_FADE_IN 0.15f
#define TOAST_FADE_OUT 0.4f

struct ToastEntry {
	char text[128];
	float timer;		// time remaining (counts down)
	float totalTime;	// total lifetime
	ToastCategory category;
};

static ToastEntry toasts[TOAST_MAX];
static int numToasts;
static bool toastEnabled = true;
static bool toastCategoryEnabled[TOAST_NUM_CATEGORIES] = { true, true, true, true, true, true };
static const char *toastCategoryNames[TOAST_NUM_CATEGORIES] = {
	"Undo / Redo", "Delete", "Copy / Paste", "Save", "Selection", "Spawn"
};

void
Toast(ToastCategory cat, const char *fmt, ...)
{
	if(!toastEnabled || !toastCategoryEnabled[cat])
		return;

	// Shift existing toasts down if full
	if(numToasts >= TOAST_MAX){
		memmove(&toasts[0], &toasts[1], (TOAST_MAX-1)*sizeof(ToastEntry));
		numToasts = TOAST_MAX - 1;
	}

	ToastEntry *t = &toasts[numToasts++];
	va_list args;
	va_start(args, fmt);
	vsnprintf(t->text, sizeof(t->text), fmt, args);
	va_end(args);
	t->totalTime = TOAST_DURATION + TOAST_FADE_IN + TOAST_FADE_OUT;
	t->timer = t->totalTime;
	t->category = cat;
}

static void
uiToasts(void)
{
	if(numToasts == 0) return;

	float dt = ImGui::GetIO().DeltaTime;
	float screenW = ImGui::GetIO().DisplaySize.x;
	float screenH = ImGui::GetIO().DisplaySize.y;

	// Update timers and remove expired
	for(int i = 0; i < numToasts; ){
		toasts[i].timer -= dt;
		if(toasts[i].timer <= 0.0f){
			memmove(&toasts[i], &toasts[i+1], (numToasts-i-1)*sizeof(ToastEntry));
			numToasts--;
		}else{
			i++;
		}
	}

	// Render from bottom up, centered horizontally
	float yBase = screenH - 60.0f;
	float spacing = 32.0f;

	for(int i = numToasts - 1; i >= 0; i--){
		ToastEntry *t = &toasts[i];
		float elapsed = t->totalTime - t->timer;

		// Compute alpha: fade in -> hold -> fade out
		float alpha;
		if(elapsed < TOAST_FADE_IN)
			alpha = elapsed / TOAST_FADE_IN;
		else if(t->timer < TOAST_FADE_OUT)
			alpha = t->timer / TOAST_FADE_OUT;
		else
			alpha = 1.0f;

		// Slide up slightly on appear
		float slideOffset = 0.0f;
		if(elapsed < TOAST_FADE_IN)
			slideOffset = (1.0f - elapsed / TOAST_FADE_IN) * 10.0f;

		ImVec2 textSize = ImGui::CalcTextSize(t->text);
		float padX = 16.0f, padY = 8.0f;
		float boxW = textSize.x + padX * 2;
		float boxH = textSize.y + padY * 2;
		float x = (screenW - boxW) * 0.5f;
		float y = yBase - (numToasts - 1 - i) * spacing + slideOffset;

		ImGui::SetNextWindowPos(ImVec2(x, y));
		ImGui::SetNextWindowSize(ImVec2(boxW, boxH));
		ImGui::SetNextWindowBgAlpha(0.0f);

		char winId[32];
		snprintf(winId, sizeof(winId), "##toast%d", i);
		ImGui::Begin(winId, nil,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing |
			ImGuiWindowFlags_NoBringToFrontOnFocus);

		ImDrawList *dl = ImGui::GetWindowDrawList();

		// Rounded rect background
		ImU32 bgCol = IM_COL32(20, 20, 20, (int)(200 * alpha));
		ImU32 borderCol = IM_COL32(80, 80, 80, (int)(150 * alpha));
		ImVec2 p0(x, y);
		ImVec2 p1(x + boxW, y + boxH);
		dl->AddRectFilled(p0, p1, bgCol, 6.0f);
		dl->AddRect(p0, p1, borderCol, 6.0f);

		// Text
		ImU32 textCol = IM_COL32(240, 240, 240, (int)(255 * alpha));
		dl->AddText(ImVec2(x + padX, y + padY), textCol, t->text);

		ImGui::End();
	}
}

static void
uiNotificationSettings(void)
{
	ImGui::Checkbox("Enable Notifications", &toastEnabled);
	if(toastEnabled){
		ImGui::Indent();
		for(int i = 0; i < TOAST_NUM_CATEGORIES; i++)
			ImGui::Checkbox(toastCategoryNames[i], &toastCategoryEnabled[i]);
		ImGui::Unindent();
	}
}

// From the demo, slightly changed
struct ExampleAppLog
{
	ImGuiTextBuffer     Buf;
	ImGuiTextFilter     Filter;
	ImVector<int>       LineOffsets;        // Index to lines offset
	bool                ScrollToBottom;

	void Clear() { Buf.clear(); LineOffsets.clear(); }

	void
	AddLog(const char *fmt, va_list args)
	{
		int old_size = Buf.size();
		Buf.appendfv(fmt, args);
		for(int new_size = Buf.size(); old_size < new_size; old_size++)
			if(Buf[old_size] == '\n')
				LineOffsets.push_back(old_size);
		ScrollToBottom = true;
	}

	void
	AddLog(const char *fmt, ...) IM_FMTARGS(2)
	{
		va_list args;
		va_start(args, fmt);
		AddLog(fmt, args);
		va_end(args);
	}

	void
	Draw(const char *title, bool *p_open = nil)
	{
		ImGui::Begin(title, p_open);
		if(ImGui::Button("Clear")) Clear();
		ImGui::SameLine();
		bool copy = ImGui::Button("Copy");
		ImGui::SameLine();
		Filter.Draw("Filter", -100.0f);
		ImGui::Separator();
		ImGui::BeginChild("scrolling", ImVec2(0,0), false, ImGuiWindowFlags_HorizontalScrollbar);
		if(copy) ImGui::LogToClipboard();

		if(Filter.IsActive()){
			const char *buf_begin = Buf.begin();
			const char *line = buf_begin;
			for(int line_no = 0; line != nil; line_no++){
				const char* line_end = (line_no < LineOffsets.Size) ? buf_begin + LineOffsets[line_no] : nil;
				if(Filter.PassFilter(line, line_end))
					ImGui::TextUnformatted(line, line_end);
				line = line_end && line_end[1] ? line_end + 1 : nil;
			}
		}else
			ImGui::TextUnformatted(Buf.begin());

		if(ScrollToBottom)
			ImGui::SetScrollHereY(1.0f);
		ScrollToBottom = false;
		ImGui::EndChild();
		ImGui::End();
	}
};

static ImVec4 mkColor(rw::RGBA &c) { return ImVec4(c.red/255.0f, c.green/255.0f, c.blue/255.0f, c.alpha/255.0f); }

static bool
binaryImageWasSaved(const FileLoader::BinaryIplSaveResult &result, int32 imageIndex)
{
	for(int i = 0; i < result.numSavedImages; i++)
		if(result.savedImages[i] == imageIndex)
			return true;
	return false;
}

static void
mergeBinarySaveResult(FileLoader::BinaryIplSaveResult *dst, const FileLoader::BinaryIplSaveResult &src)
{
	for(int i = 0; i < src.numSavedImages; i++){
		bool found = false;
		for(int j = 0; j < dst->numSavedImages; j++)
			if(dst->savedImages[j] == src.savedImages[i]){
				found = true;
				break;
			}
		if(!found && dst->numSavedImages < 256)
			dst->savedImages[dst->numSavedImages++] = src.savedImages[i];
	}

	for(int i = 0; i < src.numFailedImages; i++){
		bool found = false;
		for(int j = 0; j < dst->numFailedImages; j++)
			if(dst->failedImages[j] == src.failedImages[i]){
				found = true;
				break;
			}
		if(!found && dst->numFailedImages < 256)
			dst->failedImages[dst->numFailedImages++] = src.failedImages[i];
	}

	dst->numBlockedEmptyDeletes += src.numBlockedEmptyDeletes;
	dst->numFailedFiles += src.numFailedFiles;
}

static bool
buildStreamingFamilyPrefix(const char *scenePath, char *prefix, size_t size)
{
	const char *filename, *ext, *s;
	char *t;

	if(scenePath == nil || size == 0)
		return false;

	filename = strrchr(scenePath, '\\');
	if(filename == nil)
		filename = strrchr(scenePath, '/');
	if(filename == nil)
		filename = scenePath - 1;
	ext = strchr(filename+1, '.');
	if(ext == nil)
		return false;

	t = prefix;
	for(s = filename+1; s != ext && (size_t)(t - prefix) < size - 8; s++)
		*t++ = *s;
	*t = '\0';
	strcat(prefix, "_stream");
	return true;
}

static bool
sceneHasRelatedStreamingFamily(const char *scenePath)
{
	char prefix[256];
	CPtrNode *p;

	if(!isSA() || !buildStreamingFamilyPrefix(scenePath, prefix, sizeof(prefix)))
		return false;

	for(p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_imageIndex < 0 || inst->m_file == nil)
			continue;
		if(rw::strncmp_ci(inst->m_file->name, prefix, strlen(prefix)) == 0)
			return true;
	}
	return false;
}

static bool
instanceBelongsToStreamingFamily(ObjectInst *inst, const char *scenePath)
{
	char prefix[256];

	if(inst == nil || inst->m_file == nil || scenePath == nil)
		return false;

	if(inst->m_imageIndex < 0)
		return strcmp(inst->m_file->name, scenePath) == 0;

	if(!buildStreamingFamilyPrefix(scenePath, prefix, sizeof(prefix)))
		return false;
	return rw::strncmp_ci(inst->m_file->name, prefix, strlen(prefix)) == 0;
}

static bool
textInstNeedsSave(ObjectInst *inst)
{
	return inst &&
		(inst->m_isDirty ||
		 inst->m_isAdded ||
		 !inst->m_savedStateValid ||
		 inst->m_isDeleted != inst->m_wasSavedDeleted);
}

static bool
binaryInstNeedsDiskSave(ObjectInst *inst)
{
	return inst &&
		inst->m_imageIndex >= 0 &&
		(inst->m_isDirty || inst->m_isDeleted != inst->m_wasSavedDeleted || !inst->m_savedStateValid);
}

static bool
sceneWouldTouchStreamingBinaryOnSave(const char *scenePath)
{
	CPtrNode *p;

	if(scenePath == nil || !sceneHasRelatedStreamingFamily(scenePath))
		return false;

	for(p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(!instanceBelongsToStreamingFamily(inst, scenePath))
			continue;
		if(inst->m_imageIndex >= 0){
			if(binaryInstNeedsDiskSave(inst))
				return true;
		}else if(textInstNeedsSave(inst))
			return true;
	}
	return false;
}

static bool
sceneNeedsSave(const char *scenePath)
{
	CPtrNode *p;

	if(scenePath == nil)
		return false;

	for(p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst == nil || inst->m_file == nil || inst->m_imageIndex >= 0)
			continue;
		if(strcmp(inst->m_file->name, scenePath) != 0)
			continue;
		if(textInstNeedsSave(inst))
			return true;
	}

	return sceneWouldTouchStreamingBinaryOnSave(scenePath);
}

static bool
saveWouldNeedStreamingBinaryDiskWrite(void)
{
	CPtrNode *p;
	const char *checkedScenes[512];
	int numCheckedScenes = 0;

	for(p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_file == nil)
			continue;

		if(inst->m_imageIndex >= 0){
			if(binaryInstNeedsDiskSave(inst))
				return true;
			continue;
		}

		bool alreadyChecked = false;
		for(int i = 0; i < numCheckedScenes; i++)
			if(strcmp(checkedScenes[i], inst->m_file->name) == 0){
				alreadyChecked = true;
				break;
			}
		if(alreadyChecked)
			continue;
		if(numCheckedScenes < 512)
			checkedScenes[numCheckedScenes++] = inst->m_file->name;

		if(sceneWouldTouchStreamingBinaryOnSave(inst->m_file->name))
			return true;
	}
	return false;
}

struct StreamingBinarySaveSummary
{
	int numTouchedInstances;
	int numFamilies;
	int numBinaryIpls;
	bool touchesGta3Img;
	bool touchesGtaIntImg;
	const char *sampleObjects[3];
	int numSampleObjects;
	const char *sampleBinaryIpls[2];
	int numSampleBinaryIpls;
};

static void
addSummaryObjectName(StreamingBinarySaveSummary *summary, ObjectInst *inst)
{
	if(summary == nil || inst == nil || summary->numSampleObjects >= 3)
		return;

	const char *name = nil;
	ObjectDef *obj = GetObjectDef(inst->m_objectId);
	if(obj && obj->m_name[0] != '\0')
		name = obj->m_name;
	if(name == nil)
		return;

	for(int i = 0; i < summary->numSampleObjects; i++)
		if(strcmp(summary->sampleObjects[i], name) == 0)
			return;
	summary->sampleObjects[summary->numSampleObjects++] = name;
}

static void
addSummaryBinaryIplName(StreamingBinarySaveSummary *summary, ObjectInst *inst)
{
	if(summary == nil || inst == nil || inst->m_file == nil || summary->numSampleBinaryIpls >= 2)
		return;

	const char *name = inst->m_file->name;
	if(name == nil || name[0] == '\0')
		return;

	for(int i = 0; i < summary->numSampleBinaryIpls; i++)
		if(strcmp(summary->sampleBinaryIpls[i], name) == 0)
			return;
	summary->sampleBinaryIpls[summary->numSampleBinaryIpls++] = name;
}

static void
addSummaryArchiveUsage(StreamingBinarySaveSummary *summary, ObjectInst *inst)
{
	if(summary == nil || inst == nil || inst->m_imageIndex < 0)
		return;

	const char *archiveLogicalName = GetCdImageLogicalName(inst->m_imageIndex);
	if(archiveLogicalName == nil)
		return;
	if(strstr(archiveLogicalName, "gta_int.img") != nil)
		summary->touchesGtaIntImg = true;
	else if(strstr(archiveLogicalName, "gta3.img") != nil)
		summary->touchesGta3Img = true;
}

static const char*
getStreamingArchiveLabel(const StreamingBinarySaveSummary *summary)
{
	if(summary == nil)
		return "streamed IMG archives";
	if(summary->touchesGta3Img && summary->touchesGtaIntImg)
		return "multiple IMG archives";
	if(summary->touchesGtaIntImg)
		return "gta_int.img";
	return "gta3.img";
}

static bool
buildStreamingBinarySaveSummary(StreamingBinarySaveSummary *summary)
{
	CPtrNode *p;
	const char *checkedScenes[512];
	int numCheckedScenes = 0;
	bool needsDiskWrite = false;

	if(summary == nil)
		return false;
	memset(summary, 0, sizeof(*summary));

	for(p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst == nil || inst->m_file == nil)
			continue;

		if(inst->m_imageIndex >= 0){
			if(!binaryInstNeedsDiskSave(inst))
				continue;
			needsDiskWrite = true;
			summary->numTouchedInstances++;
			addSummaryArchiveUsage(summary, inst);
			addSummaryObjectName(summary, inst);
			addSummaryBinaryIplName(summary, inst);
			continue;
		}

		bool alreadyChecked = false;
		for(int i = 0; i < numCheckedScenes; i++)
			if(strcmp(checkedScenes[i], inst->m_file->name) == 0){
				alreadyChecked = true;
				break;
			}
		if(alreadyChecked)
			continue;
		if(numCheckedScenes < 512)
			checkedScenes[numCheckedScenes++] = inst->m_file->name;

		if(!sceneWouldTouchStreamingBinaryOnSave(inst->m_file->name))
			continue;

		needsDiskWrite = true;
		summary->numFamilies++;
		for(CPtrNode *q = instances.first; q; q = q->next){
			ObjectInst *familyInst = (ObjectInst*)q->item;
			if(!instanceBelongsToStreamingFamily(familyInst, inst->m_file->name))
				continue;
			if(familyInst->m_imageIndex >= 0){
				if(binaryInstNeedsDiskSave(familyInst)){
					summary->numTouchedInstances++;
					addSummaryArchiveUsage(summary, familyInst);
					addSummaryObjectName(summary, familyInst);
					addSummaryBinaryIplName(summary, familyInst);
				}
			}else if(textInstNeedsSave(familyInst)){
				summary->numTouchedInstances++;
				addSummaryObjectName(summary, familyInst);
			}
		}
	}

	summary->numBinaryIpls = summary->numSampleBinaryIpls;
	return needsDiskWrite;
}

static void
buildStreamingBinarySaveBlockedDetails(const StreamingBinarySaveSummary *summary, char *dst, size_t size)
{
	if(size == 0)
		return;

	dst[0] = '\0';
	if(summary == nil || summary->numTouchedInstances <= 0)
		return;

	char objectPart[256] = "";
	if(summary->numSampleObjects == 1)
		snprintf(objectPart, sizeof(objectPart), "%s", summary->sampleObjects[0]);
	else if(summary->numSampleObjects == 2)
		snprintf(objectPart, sizeof(objectPart), "%s, %s",
			summary->sampleObjects[0], summary->sampleObjects[1]);
	else if(summary->numSampleObjects >= 3)
		snprintf(objectPart, sizeof(objectPart), "%s, %s, %s",
			summary->sampleObjects[0], summary->sampleObjects[1], summary->sampleObjects[2]);

	if(summary->numTouchedInstances == 1 && summary->numSampleObjects >= 1)
		snprintf(dst, size, "Modified object: %s.", objectPart);
	else if(summary->numTouchedInstances > summary->numSampleObjects && summary->numSampleObjects >= 1)
		snprintf(dst, size, "Modified objects include %s and %d more.",
			objectPart, summary->numTouchedInstances - summary->numSampleObjects);
	else if(summary->numSampleObjects >= 1)
		snprintf(dst, size, "Modified objects: %s.", objectPart);
	else
		snprintf(dst, size, "%d streamed object change(s) are affected.", summary->numTouchedInstances);
}

static const char*
getSaveDestinationLabel(void)
{
	return gSaveDestination == SAVE_DESTINATION_MODLOADER ? "modloader/Ariane" : "original files";
}

static bool
warnStreamingBinarySaveBlockedByRunningGame(const char *actionName)
{
	StreamingBinarySaveSummary summary;
	if(!isCurrentGameRunning())
		return false;
	if(!buildStreamingBinarySaveSummary(&summary))
		return false;
	if(strcmp(actionName, "Hot Reload") != 0 &&
	   gSaveDestination == SAVE_DESTINATION_MODLOADER)
		return false;

	char details[256];
	buildStreamingBinarySaveBlockedDetails(&summary, details, sizeof(details));
	const char *archiveLabel = getStreamingArchiveLabel(&summary);

#ifdef _WIN32
	char message[1400];
	if(strcmp(actionName, "Hot Reload") == 0){
		snprintf(message, sizeof(message),
			"%s can't apply these changes while GTA San Andreas is running.\n\n"
			"%s\n\n"
			"These objects come from streamed map data stored inside %s.\n"
			"For this case, Ariane's current Hot Reload path would need to update %s, and GTA keeps that file in use while the game is open.\n\n"
			"This type of streamed-binary change is not supported by Hot Reload yet.\n"
			"To keep the change, close the game and use Save.",
			actionName,
			details[0] ? details : "One or more streamed objects were modified",
			archiveLabel,
			archiveLabel);
	}else{
		snprintf(message, sizeof(message),
			"%s can't continue while GTA San Andreas is running.\n\n"
			"%s\n\n"
			"These changes are stored in streamed map data inside %s.\n"
			"GTA keeps that file in use while the game is open, so Ariane can't update it safely.\n\n"
			"Close the game, then try %s again.",
			actionName,
			details[0] ? details : "One or more streamed objects were modified",
			archiveLabel,
			actionName);
	}
	MessageBoxA(nil, message, "Ariane", MB_OK | MB_ICONWARNING);
#endif
	if(strcmp(actionName, "Hot Reload") == 0){
		if(details[0])
			Toast(TOAST_SAVE, "%s blocked: %s This streamed-binary change is not supported by Hot Reload yet.", actionName, details);
		else
			Toast(TOAST_SAVE, "%s blocked: streamed binary map data is not supported by Hot Reload yet.", actionName);
	}else{
		if(details[0])
			Toast(TOAST_SAVE, "%s blocked: %s Close the game to update %s.", actionName, details, archiveLabel);
		else
			Toast(TOAST_SAVE, "%s blocked: streamed binary map data is in %s. Close the game first.", actionName, archiveLabel);
	}
	return true;
}

static bool
logicalPathsEqualCiNormalized(const char *a, const char *b)
{
	if(a == nil || b == nil)
		return false;
	while(*a || *b){
		char ca = *a++;
		char cb = *b++;
		if(ca == '\\') ca = '/';
		if(cb == '\\') cb = '/';
		if(ca >= 'A' && ca <= 'Z') ca = ca - 'A' + 'a';
		if(cb >= 'A' && cb <= 'Z') cb = cb - 'A' + 'a';
		if(ca != cb)
			return false;
	}
	return true;
}

static const char*
getGameDatLogicalPath(void)
{
	switch(gameversion){
	case GAME_III: return "data/gta3.dat";
	case GAME_VC: return "data/gta_vc.dat";
	case GAME_SA: return "data/gta.dat";
	case GAME_LCS: return "data/gta_lcs.dat";
	case GAME_VCS: return "data/gta_vcs.dat";
	default: return nil;
	}
}

static const char *WHOLE_MAP_BOOTSTRAP_IPL_LOGICAL_PATH = "data/maps/ariane/whole_map_bootstrap.ipl";

static void
removeLegacyWholeMapOverrides(void)
{
	const char *datLogicalPath = getGameDatLogicalPath();
	char exportPath[1024];
	if(datLogicalPath &&
	   BuildModloaderLogicalExportPath(datLogicalPath, exportPath, sizeof(exportPath)))
		remove(exportPath);
	if(BuildModloaderLogicalExportPath(WHOLE_MAP_BOOTSTRAP_IPL_LOGICAL_PATH, exportPath, sizeof(exportPath)))
		remove(exportPath);
}

static bool
saveAllIpls(void)
{
	if(warnStreamingBinarySaveBlockedByRunningGame("Save"))
		return false;

	// Collect unique IPL filenames from all instances
	CPtrNode *p;
	const char *saved[512];
	const char *checked[512];
	int numSaved = 0;
	int numChecked = 0;
	int numFailed = 0;
	FileLoader::BinaryIplSaveResult binaryResult = {};
	bool waterSaveOk = true;

	if(gSaveDestination == SAVE_DESTINATION_MODLOADER)
		removeLegacyWholeMapOverrides();

	for(p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_file == nil)
			continue;
		// Skip streaming IPL instances — those are saved via SaveBinaryIpls
		if(inst->m_imageIndex >= 0)
			continue;
		// Check if we already saved this file
		bool found = false;
		for(int i = 0; i < numChecked; i++)
			if(strcmp(checked[i], inst->m_file->name) == 0){
				found = true;
				break;
			}
		if(found)
			continue;
		if(numChecked < 512)
			checked[numChecked++] = inst->m_file->name;
		if(sceneNeedsSave(inst->m_file->name) && numSaved < 512){
			FileLoader::BinaryIplSaveResult sceneResult = FileLoader::SaveScene(inst->m_file->name);
			mergeBinarySaveResult(&binaryResult, sceneResult);
			if(sceneResult.numFailedFiles == 0 &&
			   sceneResult.numFailedImages == 0 &&
			   sceneResult.numBlockedEmptyDeletes == 0){
				saved[numSaved++] = inst->m_file->name;
			}else{
				numFailed++;
				// Show detailed error for this file
				if(sceneResult.numFailedFiles > 0)
					Toast(TOAST_SAVE, "Failed to save %s: file write error", inst->m_file->name);
				else if(sceneResult.numFailedImages > 0)
					Toast(TOAST_SAVE, "Failed to save %s: binary IPL error", inst->m_file->name);
			}
		}
	}

	FileLoader::BinaryIplSaveResult standaloneBinaryResult =
		FileLoader::SaveBinaryIpls(binaryResult.savedImages, binaryResult.numSavedImages);
	mergeBinarySaveResult(&binaryResult, standaloneBinaryResult);

	if(binaryResult.numBlockedEmptyDeletes)
		Toast(TOAST_SAVE, "Blocked %d binary delete(s): can't empty a streaming IPL", binaryResult.numBlockedEmptyDeletes);
	else if(binaryResult.numFailedImages)
		Toast(TOAST_SAVE, "Failed to save %d binary IPL(s)", binaryResult.numFailedImages);
	else if(binaryResult.numFailedFiles)
		Toast(TOAST_SAVE, "Failed to save %d file(s)", binaryResult.numFailedFiles);

	// Show summary if there were failures
	if(numFailed > 0)
		Toast(TOAST_SAVE, "Warning: %d file(s) failed to save. Check console for details.", numFailed);

	if(params.water == GAME_SA && WaterLevel::gWaterDirty){
		bool skippedEmptyModloaderWater =
			gSaveDestination == SAVE_DESTINATION_MODLOADER &&
			WaterLevel::GetNumQuads() == 0 &&
			WaterLevel::GetNumTris() == 0;
		waterSaveOk = WaterLevel::SaveWater();
		if(!waterSaveOk)
			Toast(TOAST_SAVE, "Failed to save water.dat");
		else if(skippedEmptyModloaderWater)
			Toast(TOAST_SAVE, "Skipped empty modloader water.dat override: SA crashes on zero-poly custom water");
	}

	if(gSaveDestination == SAVE_DESTINATION_MODLOADER &&
	   (numSaved > 0 || binaryResult.numSavedImages > 0)){
		int shadowedText = 0;
		int shadowedBinary = 0;
		for(int i = 0; i < numSaved; i++){
			char exportPath[1024];
			if(!BuildModloaderLogicalExportPath(saved[i], exportPath, sizeof(exportPath)))
				continue;
			const char *winningPath = ModloaderGetSourcePath(saved[i]);
			if(winningPath && strcmp(winningPath, exportPath) != 0)
				shadowedText++;
		}
		for(int i = 0; i < binaryResult.numSavedImages; i++){
			char exportPath[1024];
			char entryFilename[64];
			if(!BuildModloaderImageEntryExportPath(binaryResult.savedImages[i], exportPath, sizeof(exportPath)))
				continue;
			GameFile *file = GetGameFileFromImage(binaryResult.savedImages[i]);
			if(file == nil || file->name == nil)
				continue;
			if(snprintf(entryFilename, sizeof(entryFilename), "%s.ipl", file->name) >= (int)sizeof(entryFilename))
				continue;
			const char *winningPath = ModloaderFindImageEntryOverride(
				GetCdImageLogicalName(binaryResult.savedImages[i]), entryFilename);
			if(winningPath && strcmp(winningPath, exportPath) != 0)
				shadowedBinary++;
		}
		if(shadowedText || shadowedBinary)
			Toast(TOAST_SAVE,
			      "Saved to modloader/Ariane, but %d text + %d streamed override(s) are still shadowed at runtime",
			      shadowedText, shadowedBinary);
	}

	// Update saved-state snapshot for diff viewer
	for(p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_imageIndex < 0){
			bool textWasSaved = false;
			for(int i = 0; i < numSaved; i++)
				if(strcmp(saved[i], inst->m_file->name) == 0){
					textWasSaved = true;
					break;
				}
			if(!textWasSaved)
				continue;
			inst->m_savedTranslation = inst->m_translation;
			inst->m_savedRotation = inst->m_rotation;
			inst->m_wasSavedDeleted = inst->m_isDeleted;
			inst->m_savedStateValid = true;
			inst->m_isDirty = false;
			inst->m_isAdded = false;
		}else if(binaryImageWasSaved(binaryResult, inst->m_imageIndex)){
			if(!inst->m_isDeleted){
				inst->m_savedTranslation = inst->m_translation;
				inst->m_savedRotation = inst->m_rotation;
			}
			inst->m_wasSavedDeleted = inst->m_isDeleted;
			inst->m_savedStateValid = true;
			inst->m_isDirty = false;
			inst->m_origTranslation = inst->m_translation;
			inst->m_origRotation = inst->m_rotation;
		}
	}

	return waterSaveOk &&
	       binaryResult.numBlockedEmptyDeletes == 0 &&
	       binaryResult.numFailedImages == 0 &&
	       binaryResult.numFailedFiles == 0;
}

static void
testInGame(void)
{
	// Only III/VC/SA supported
	if(gameversion != GAME_III && gameversion != GAME_VC && gameversion != GAME_SA){
		Toast(TOAST_SAVE, "Test in Game: unsupported game");
		return;
	}
	if(!warnMissingTeleportAsi("Test in Game"))
		return;

	// Save all IPLs first
	if(!saveAllIpls())
		return;
	Toast(TOAST_SAVE, "Saved all IPL files to %s", getSaveDestinationLabel());

	// Camera position -> snap to ground
	rw::V3d pos = TheCamera.m_position;
	rw::V3d groundHit;
	if(GetGroundPlacementSurface(pos, &groundHit))
		pos.z = groundHit.z + 1.0f;
	// If no ground found: use camera pos as-is (player will fall)

	// Camera heading
	float heading = TheCamera.getHeading();

	char teleportPath[2048];
	char legacyTeleportPath[2048];
	if(!GetArianeDataPath(teleportPath, sizeof(teleportPath), "ariane_teleport.txt") ||
	   !getLegacyRootPath(legacyTeleportPath, sizeof(legacyTeleportPath), "ariane_teleport.txt")){
		Toast(TOAST_SAVE, "Failed to resolve game folder");
		return;
	}

	// Write the teleport handoff into ariane/ and mirror the legacy root
	// location so older in-game plugins continue to work.
	FILE *f = fopen(teleportPath, "w");
	if(f == nil){
		Toast(TOAST_SAVE, "Failed to write teleport file");
		return;
	}
	fprintf(f, "%f %f %f %f %d\n", pos.x, pos.y, pos.z, heading, currentArea);
	fclose(f);
	FILE *legacy = fopen(legacyTeleportPath, "w");
	if(legacy){
		fprintf(legacy, "%f %f %f %f %d\n", pos.x, pos.y, pos.z, heading, currentArea);
		fclose(legacy);
	}

	// Launch game executable
	const char *exeName = nil;
	if(isIII()) exeName = "gta3.exe";
	else if(isVC()) exeName = "gta-vc.exe";
	else if(isSA()) exeName = "gta_sa.exe";

#ifdef _WIN32
	char rootDir[2048];
	char exePath[2048];
	if(!getEditorRootDirectory(rootDir, sizeof(rootDir)) ||
	   !buildPath(exePath, sizeof(exePath), rootDir, exeName)){
		Toast(TOAST_SAVE, "Failed to resolve %s", exeName);
		return;
	}

	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	memset(&pi, 0, sizeof(pi));
	if(CreateProcessA(exePath, NULL, NULL, NULL, FALSE, 0, NULL, rootDir, &si, &pi)){
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		Toast(TOAST_SAVE, "Launching %s...", exeName);
	} else {
		Toast(TOAST_SAVE, "Failed to launch %s", exeName);
	}
#else
	Toast(TOAST_SAVE, "Game launch only supported on Windows");
#endif
}

static void
hotReloadIpls(void)
{
	if(!isSA()){
		Toast(TOAST_SAVE, "Hot Reload: only supported for SA");
		return;
	}
	if(warnStreamingBinarySaveBlockedByRunningGame("Hot Reload"))
		return;
	if(!warnMissingTeleportAsi("Hot Reload"))
		return;

	char rootDir[2048];
	char reloadPath[2048];
	char entityReloadPath[2048];
	char tracePath[2048];
	char legacyReloadPath[2048];
	char legacyEntityReloadPath[2048];
	if(!getEditorRootDirectory(rootDir, sizeof(rootDir)) ||
	   !GetArianeDataPath(tracePath, sizeof(tracePath), "ariane_hot_reload_log.txt") ||
	   !GetArianeDataPath(reloadPath, sizeof(reloadPath), "ariane_reload.txt") ||
	   !GetArianeDataPath(entityReloadPath, sizeof(entityReloadPath), "ariane_reload_entities.txt") ||
	   !getLegacyRootPath(legacyReloadPath, sizeof(legacyReloadPath), "ariane_reload.txt") ||
	   !getLegacyRootPath(legacyEntityReloadPath, sizeof(legacyEntityReloadPath), "ariane_reload_entities.txt")){
		Toast(TOAST_SAVE, "Hot Reload: failed to resolve game folder");
		return;
	}
	setHotReloadTracePath(tracePath);
	FILE *traceFile = fopen(tracePath, "w");
	if(traceFile){
		fprintf(traceFile, "HotReload begin\n");
		fclose(traceFile);
	}

	CPtrNode *p;
	int numStreamingIpls = 0;
	int numEntityCmds = 0;
	int totalBlockedDeletes = 0;
	int totalFailedImages = 0;
	const auto NeedsTransformReload = [](ObjectInst *inst) {
		float dist = length(sub(inst->m_translation, inst->m_origTranslation));
		if(dist >= 0.001f)
			return true;
		float dot = fabsf(inst->m_rotation.x * inst->m_origRotation.x +
		                   inst->m_rotation.y * inst->m_origRotation.y +
		                   inst->m_rotation.z * inst->m_origRotation.z +
		                   inst->m_rotation.w * inst->m_origRotation.w);
		return dot < 0.9999f;
	};
	const auto GetAreaFlags = [](ObjectInst *inst) {
		int area = inst->m_area;
		if(inst->m_isUnimportant) area |= 0x100;
		if(inst->m_isUnderWater) area |= 0x400;
		if(inst->m_isTunnel) area |= 0x800;
		if(inst->m_isTunnelTransition) area |= 0x1000;
		return area;
	};

	// --- Streaming IPLs (binary, reloaded via CIplStore) ---
	const char *iplNames[256];
	int numNames = 0;
	FileLoader::BinaryIplSaveResult binaryResult = FileLoader::SaveBinaryIpls();
	totalBlockedDeletes += binaryResult.numBlockedEmptyDeletes;
	totalFailedImages += binaryResult.numFailedImages;

	for(p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_imageIndex < 0) continue;
		if(inst->m_file == nil) continue;
		if(!binaryImageWasSaved(binaryResult, inst->m_imageIndex))
			continue;

		bool found = false;
		for(int i = 0; i < numNames; i++){
			if(strcmp(iplNames[i], inst->m_file->name) == 0){
				found = true;
				break;
			}
		}
		if(!found && numNames < 256)
			iplNames[numNames++] = inst->m_file->name;
	}

	if(numNames > 0){
		FILE *f = fopen(reloadPath, "w");
		FILE *legacy = fopen(legacyReloadPath, "w");
		if(f){
			for(int i = 0; i < numNames; i++)
				fprintf(f, "%s\n", iplNames[i]);
			fclose(f);
			if(legacy){
				for(int i = 0; i < numNames; i++)
					fprintf(legacy, "%s\n", iplNames[i]);
				fclose(legacy);
			}
			numStreamingIpls = numNames;
		}else if(legacy)
			fclose(legacy);
	}

	// --- Entity deletes/moves (manipulated directly in game memory) ---
	// Streaming moves also go through this path as a runtime fallback:
	// if binary IMG patching/reload fails for a given IPL, the live entity
	// still gets moved in-game.
	FILE *fe = fopen(entityReloadPath, "w");
	FILE *legacyFe = fopen(legacyEntityReloadPath, "w");
	if(fe){
		for(p = instances.first; p; p = p->next){
			ObjectInst *inst = (ObjectInst*)p->item;
			if(inst->m_imageIndex >= 0 &&
			   binaryImageWasSaved(binaryResult, inst->m_imageIndex))
				continue;

			bool needsAdd = !inst->m_isDeleted && !inst->m_gameEntityExists;
			bool needsDelete = inst->m_isDeleted && inst->m_gameEntityExists;
			bool needsMove = !inst->m_isDeleted &&
				inst->m_gameEntityExists &&
				NeedsTransformReload(inst);
			if(!needsAdd && !needsDelete && !needsMove)
				continue;

			if(needsAdd){
				if(inst->m_imageIndex >= 0)
					continue;

				int lodModelId = -1;
				float lodX = 0.0f, lodY = 0.0f, lodZ = 0.0f;
				if(inst->m_lod && !inst->m_lod->m_isDeleted){
					lodModelId = inst->m_lod->m_objectId;
					lodX = inst->m_lod->m_translation.x;
					lodY = inst->m_lod->m_translation.y;
					lodZ = inst->m_lod->m_translation.z;
				}

				// A modelId x y z qx qy qz qw area lodModelId lodX lodY lodZ
				fprintf(fe, "A %d %f %f %f %f %f %f %f %d %d %f %f %f\n",
					inst->m_objectId,
					inst->m_translation.x,
					inst->m_translation.y,
					inst->m_translation.z,
					inst->m_rotation.x,
					inst->m_rotation.y,
					inst->m_rotation.z,
					inst->m_rotation.w,
					GetAreaFlags(inst),
					lodModelId,
					lodX, lodY, lodZ);
				if(legacyFe)
					fprintf(legacyFe, "A %d %f %f %f %f %f %f %f %d %d %f %f %f\n",
						inst->m_objectId,
						inst->m_translation.x,
						inst->m_translation.y,
						inst->m_translation.z,
						inst->m_rotation.x,
						inst->m_rotation.y,
						inst->m_rotation.z,
						inst->m_rotation.w,
						GetAreaFlags(inst),
						lodModelId,
						lodX, lodY, lodZ);
				numEntityCmds++;
				inst->m_gameEntityExists = true;
				inst->m_isAdded = false;
				inst->m_origTranslation = inst->m_translation;
				inst->m_origRotation = inst->m_rotation;
				continue;
			}

			if(needsDelete){
				// D modelId oldX oldY oldZ
				fprintf(fe, "D %d %f %f %f\n",
					inst->m_objectId,
					inst->m_origTranslation.x,
					inst->m_origTranslation.y,
					inst->m_origTranslation.z);
				if(legacyFe)
					fprintf(legacyFe, "D %d %f %f %f\n",
						inst->m_objectId,
						inst->m_origTranslation.x,
						inst->m_origTranslation.y,
						inst->m_origTranslation.z);
				numEntityCmds++;
				inst->m_gameEntityExists = false;
			}else if(needsMove){
				// M modelId oldX oldY oldZ newX newY newZ qx qy qz qw
				fprintf(fe, "M %d %f %f %f %f %f %f %f %f %f %f\n",
					inst->m_objectId,
					inst->m_origTranslation.x,
					inst->m_origTranslation.y,
					inst->m_origTranslation.z,
					inst->m_translation.x,
					inst->m_translation.y,
					inst->m_translation.z,
					inst->m_rotation.x,
					inst->m_rotation.y,
					inst->m_rotation.z,
					inst->m_rotation.w);
				if(legacyFe)
					fprintf(legacyFe, "M %d %f %f %f %f %f %f %f %f %f %f\n",
						inst->m_objectId,
						inst->m_origTranslation.x,
						inst->m_origTranslation.y,
						inst->m_origTranslation.z,
						inst->m_translation.x,
						inst->m_translation.y,
						inst->m_translation.z,
						inst->m_rotation.x,
						inst->m_rotation.y,
						inst->m_rotation.z,
						inst->m_rotation.w);
				numEntityCmds++;
				// Update orig so next reload knows where the game entity now is
				inst->m_origTranslation = inst->m_translation;
				inst->m_origRotation = inst->m_rotation;
			}
		}
		fclose(fe);
		if(legacyFe)
			fclose(legacyFe);

		if(numEntityCmds == 0)
			remove(entityReloadPath);
	}else if(legacyFe)
		fclose(legacyFe);

	if(numStreamingIpls == 0 && numEntityCmds == 0){
		if(totalBlockedDeletes)
			Toast(TOAST_SAVE, "Blocked %d binary delete(s): can't empty a streaming IPL", totalBlockedDeletes);
		else if(totalFailedImages)
			Toast(TOAST_SAVE, "Failed to save %d binary IPL(s)", totalFailedImages);
		else
			Toast(TOAST_SAVE, "Hot Reload: nothing to reload");
		return;
	}

	if(totalBlockedDeletes)
		Toast(TOAST_SAVE, "Blocked %d binary delete(s): can't empty a streaming IPL", totalBlockedDeletes);
	else if(totalFailedImages)
		Toast(TOAST_SAVE, "Failed to save %d binary IPL(s)", totalFailedImages);
	else if(numStreamingIpls > 0 && numEntityCmds > 0)
		Toast(TOAST_SAVE, "Hot Reload: %d IPL(s) + %d entity(s)", numStreamingIpls, numEntityCmds);
	else if(numStreamingIpls > 0)
		Toast(TOAST_SAVE, "Hot Reload: %d streaming IPL(s)", numStreamingIpls);
	else
		Toast(TOAST_SAVE, "Hot Reload: %d entity(s)", numEntityCmds);
}

static bool gOpenExportPrefab = false;
static bool gOpenImportPrefab = false;
static bool gOpenCustomImport = false;
static bool gOpenDestroyMap = false;
static bool gShowExportPrefab = false;
static bool gShowImportPrefab = false;
static bool gShowDestroyMap = false;
static bool gDestroyMapIncludeWater = true;
static void uiExportPrefabPopup(void);
static void uiImportPrefabPopup(void);
static void uiCustomImportPopup(void);
static void uiDestroyMapPopup(void);
static void trimLineEnding(char *line);
static int findSuggestedCustomImportId(void);

static bool
BeginEditorDialog(const char *name, bool *open, ImGuiWindowFlags flags = 0)
{
	if(open && !*open)
		return false;

	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	flags |= ImGuiWindowFlags_AlwaysAutoResize |
	         ImGuiWindowFlags_NoCollapse;
	if(!ImGui::Begin(name, open, flags)){
		ImGui::End();
		return false;
	}
	return true;
}

static void
EndEditorDialog(void)
{
	ImGui::End();
}

template <size_t N>
static void
InputTextReadonly(const char *label, const char *value)
{
	char buf[N];
	if(value == nil)
		value = "";
	strncpy(buf, value, N - 1);
	buf[N - 1] = '\0';
	ImGui::InputText(label, buf, N,
		ImGuiInputTextFlags_ReadOnly |
		ImGuiInputTextFlags_AutoSelectAll);
}

static const char *CUSTOM_IMPORT_MANIFEST_LOGICAL_PATH = "ariane_custom.txt";
static const char *CUSTOM_IMPORT_IDE_LOGICAL_PATH = "data/maps/ariane/custom.ide";
static const char *CUSTOM_IMPORT_IPL_LOGICAL_PATH = "data/maps/ariane/custom.ipl";

struct CustomImportState
{
	bool active;
	char sourceBase[MODELNAMELEN];
	char modelName[MODELNAMELEN];
	char txdName[MODELNAMELEN];
	char sourceDir[1024];
	char dffSource[1024];
	char txdSource[1024];
	char colSource[1024];
	bool hasCol;
	bool preferAutoCol;
	int objectId;
	float drawDist;
	ObjectDef previewObj;
	char error[512];
	char warning[512];
	GameFile *targetIplFile;
};
static CustomImportState gCustomImport = {};
static GameFile *gCustomImportIdeFile = nil;
static GameFile *gCustomImportIplFile = nil;

static void
destroyEntireMap(bool includeWater)
{
	int numDeleted = DeleteAllInstances();
	int numWaterPolys = 0;
	if(includeWater && params.water == GAME_SA){
		numWaterPolys = WaterLevel::GetNumQuads() + WaterLevel::GetNumTris();
		WaterLevel::ClearAllWater();
	}

	if(numDeleted == 0 && numWaterPolys == 0){
		Toast(TOAST_DELETE, "Map is already empty");
		return;
	}

	if(includeWater && params.water == GAME_SA)
		Toast(TOAST_DELETE, "Destroyed map in editor: %d instance(s), %d water polygon(s)", numDeleted, numWaterPolys);
	else
		Toast(TOAST_DELETE, "Destroyed map in editor: %d instance(s)", numDeleted);
}

static void
uiDestroyMapPopup(void)
{
	if(gOpenDestroyMap){
		gDestroyMapIncludeWater = params.water == GAME_SA;
		gShowDestroyMap = true;
		gOpenDestroyMap = false;
		ImGui::SetNextWindowFocus();
	}

	if(!BeginEditorDialog("Destroy Entire Map", &gShowDestroyMap))
		return;

	ImGui::TextWrapped("This marks every loaded map instance as deleted in the editor.");
	if(params.water == GAME_SA)
		ImGui::Checkbox("Also clear water.dat", &gDestroyMapIncludeWater);
	else
		ImGui::TextDisabled("Water clearing is only available for SA maps.");

	ImGui::Separator();
	ImGui::Text("Current save target: %s", getSaveDestinationLabel());
	if(gSaveDestination != SAVE_DESTINATION_MODLOADER)
		ImGui::TextWrapped("Original game files stay untouched until you save. Enable Save to Modloader before saving if you want the empty map as a modloader override.");

	if(ImGui::Button("Destroy", ImVec2(140, 0))){
		destroyEntireMap(gDestroyMapIncludeWater && params.water == GAME_SA);
		gShowDestroyMap = false;
	}
	ImGui::SameLine();
	if(ImGui::Button("Cancel", ImVec2(140, 0)))
		gShowDestroyMap = false;

	EndEditorDialog();
}

struct FileRollbackEntry
{
	std::string path;
	bool existed;
	std::vector<char> data;
};

static const char*
pathFilename(const char *path)
{
	const char *slash = strrchr(path, '/');
	const char *backslash = strrchr(path, '\\');
	if(backslash && (slash == nil || backslash > slash))
		slash = backslash;
	return slash ? slash + 1 : path;
}

static bool
pathsEqualCiNormalized(const char *a, const char *b)
{
	if(a == nil || b == nil)
		return false;
	while(*a || *b){
		char ca = *a++;
		char cb = *b++;
		if(ca == '\\') ca = '/';
		if(cb == '\\') cb = '/';
		if(ca >= 'A' && ca <= 'Z') ca = ca - 'A' + 'a';
		if(cb >= 'A' && cb <= 'Z') cb = cb - 'A' + 'a';
		if(ca != cb)
			return false;
	}
	return true;
}

static bool
pathContainsModloaderDir(const char *path)
{
	if(path == nil || path[0] == '\0')
		return false;
	char normalized[1024];
	size_t i = 0;
	for(; path[i] && i < sizeof(normalized)-1; i++){
		char c = path[i];
		if(c == '\\') c = '/';
		if(c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
		normalized[i] = c;
	}
	normalized[i] = '\0';
	return strstr(normalized, "/modloader/") != nil ||
	       strcmp(normalized, "modloader") == 0 ||
	       strncmp(normalized, "modloader/", 10) == 0;
}

static bool
pickFileDialog(char *dst, size_t size, const char *expectedExt)
{
	if(dst == nil || size == 0)
		return false;

#ifdef _WIN32
	char filename[MAX_PATH] = "";
	OPENFILENAMEA ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.lpstrFile = filename;
	ofn.nMaxFile = sizeof(filename);
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
	if(!GetOpenFileNameA(&ofn))
		return false;
	strncpy(dst, filename, size-1);
	dst[size-1] = '\0';
	return true;
#else
	const char *command =
#ifdef __APPLE__
		"osascript -e 'POSIX path of (choose file)'";
#else
		"sh -lc 'if command -v zenity >/dev/null 2>&1; then zenity --file-selection; "
		"elif command -v kdialog >/dev/null 2>&1; then kdialog --getopenfilename; fi'";
#endif
	FILE *pipe = popen(command, "r");
	if(pipe == nil)
		return false;
	bool ok = fgets(dst, (int)size, pipe) != nil;
	pclose(pipe);
	if(!ok)
		return false;
	trimLineEnding(dst);
	return dst[0] != '\0';
#endif
}

static bool
pathHasExtensionCi(const char *path, const char *ext)
{
	size_t pathLen = strlen(path);
	size_t extLen = strlen(ext);
	if(pathLen < extLen)
		return false;
	return rw::strncmp_ci(path + pathLen - extLen, ext, (int)extLen) == 0;
}

static void
stripExtensionCopy(const char *path, char *dst, size_t size)
{
	const char *filename = pathFilename(path);
	const char *dot = strrchr(filename, '.');
	size_t len = dot && dot > filename ? (size_t)(dot - filename) : strlen(filename);
	if(len >= size)
		len = size - 1;
	memcpy(dst, filename, len);
	dst[len] = '\0';
}

static bool
getDirectoryCopy(const char *path, char *dst, size_t size)
{
	const char *slash = strrchr(path, '/');
	const char *backslash = strrchr(path, '\\');
	if(backslash && (slash == nil || backslash > slash))
		slash = backslash;
	if(slash == nil){
		if(size == 0) return false;
		dst[0] = '\0';
		return true;
	}
	size_t len = (size_t)(slash - path);
	if(len >= size)
		return false;
	memcpy(dst, path, len);
	dst[len] = '\0';
	return true;
}

static bool
buildSiblingPath(char *dst, size_t size, const char *dir, const char *base, const char *ext)
{
	if(dir == nil || dir[0] == '\0')
		return snprintf(dst, size, "%s%s", base, ext) < (int)size;
	return snprintf(dst, size, "%s/%s%s", dir, base, ext) < (int)size;
}

static bool
buildCustomImportModelLogicalPath(char *dst, size_t size, const char *name, const char *ext)
{
	return snprintf(dst, size, "models/gta3.img/%s.%s", name, ext) < (int)size;
}

static bool
buildCustomImportColLogicalPath(char *dst, size_t size, const char *name)
{
	return snprintf(dst, size, "data/maps/ariane/cols/%s.col", name) < (int)size;
}

static bool
copyFileExact(const char *src, const char *dst)
{
	FILE *in = fopen(src, "rb");
	if(in == nil)
		return false;
	if(!EnsureParentDirectoriesForPath(dst)){
		fclose(in);
		return false;
	}
	FILE *out = fopen(dst, "wb");
	if(out == nil){
		fclose(in);
		return false;
	}

	char buffer[64*1024];
	bool ok = true;
	while(!feof(in)){
		size_t n = fread(buffer, 1, sizeof(buffer), in);
		if(n == 0)
			break;
		if(fwrite(buffer, 1, n, out) != n){
			ok = false;
			break;
		}
	}
	fclose(in);
	fclose(out);
	return ok;
}

static bool
writeFileExact(const char *path, const char *data, size_t size)
{
	if(!EnsureParentDirectoriesForPath(path))
		return false;
	FILE *f = fopen(path, "wb");
	if(f == nil)
		return false;
	bool ok = fwrite(data, 1, size, f) == size;
	fclose(f);
	return ok;
}

static bool
isValidColFourcc(uint32 fourcc)
{
	return fourcc == 0x4C4C4F43 || fourcc == 0x324C4F43 ||
	       fourcc == 0x334C4F43 || fourcc == 0x344C4F43;
}

static bool
readFileExact(const char *path, std::vector<char> &data)
{
	FILE *f = fopen(path, "rb");
	if(f == nil)
		return false;

	if(fseek(f, 0, SEEK_END) != 0){
		fclose(f);
		return false;
	}
	long size = ftell(f);
	if(size < 0){
		fclose(f);
		return false;
	}
	if(fseek(f, 0, SEEK_SET) != 0){
		fclose(f);
		return false;
	}
	data.resize((size_t)size);
	bool ok = size == 0 || fread(data.data(), 1, (size_t)size, f) == (size_t)size;
	fclose(f);
	return ok;
}

static bool
forEachColEntry(std::vector<char> &data, bool (*cb)(ColFileHeader *header, size_t offset, void *ctx), void *ctx)
{
	size_t offset = 0;
	bool sawEntry = false;
	while(offset + 32 <= data.size()){
		ColFileHeader *header = (ColFileHeader*)&data[offset];
		if(!isValidColFourcc(header->fourcc) || header->modelsize < 24)
			return sawEntry;
		sawEntry = true;
		if(!cb(header, offset, ctx))
			return false;

		size_t nextOffset = offset + 8 + (size_t)header->modelsize;
		if(nextOffset <= offset)
			return false;
		if(nextOffset > data.size())
			nextOffset = data.size();
		offset = nextOffset;
	}
	return sawEntry;
}

struct ColInspectContext
{
	const char *modelName;
	int count;
	bool allMatch;
	char firstName[25];
};

static bool
inspectColEntry(ColFileHeader *header, size_t, void *ctx)
{
	ColInspectContext *inspect = (ColInspectContext*)ctx;
	char entryName[25];
	memcpy(entryName, header->name, sizeof(header->name));
	entryName[sizeof(header->name)] = '\0';
	if(inspect->count == 0){
		strncpy(inspect->firstName, entryName, sizeof(inspect->firstName)-1);
		inspect->firstName[sizeof(inspect->firstName)-1] = '\0';
	}
	if(rw::strncmp_ci(entryName, inspect->modelName, MODELNAMELEN) != 0)
		inspect->allMatch = false;
	inspect->count++;
	return true;
}

static bool
inspectColFileForImport(const char *path, int *entryCount, bool *allEntriesMatchModel,
                        bool *singleEntryNeedsRename, const char *modelName)
{
	std::vector<char> data;
	if(!readFileExact(path, data) || data.size() < 32)
		return false;

	ColInspectContext ctx = {};
	ctx.modelName = modelName;
	ctx.allMatch = true;
	ctx.firstName[0] = '\0';
	if(!forEachColEntry(data, inspectColEntry, &ctx) || ctx.count == 0)
		return false;

	if(entryCount) *entryCount = ctx.count;
	if(allEntriesMatchModel) *allEntriesMatchModel = ctx.allMatch;
	if(singleEntryNeedsRename) *singleEntryNeedsRename = ctx.count == 1 &&
		rw::strncmp_ci(ctx.firstName, modelName, MODELNAMELEN) != 0;
	return true;
}

static bool
copyColWithInternalRename(const char *src, const char *dst, const char *modelName)
{
	std::vector<char> data;
	if(!readFileExact(src, data) || data.size() < 32)
		return false;
	size_t offset = 0;
	bool sawEntry = false;
	while(offset + 32 <= data.size()){
		ColFileHeader *header = (ColFileHeader*)&data[offset];
		if(!isValidColFourcc(header->fourcc) || header->modelsize < 24)
			break;
		sawEntry = true;
		if(offset + 8 + (size_t)header->modelsize > data.size())
			header->modelsize = (uint32)(data.size() - offset - 8);
		size_t modelLen = strlen(modelName);
		if(modelLen >= sizeof(header->name))
			return false;
		memcpy(header->name, modelName, modelLen);
		header->name[modelLen] = '\0';

		size_t nextOffset = offset + 8 + (size_t)header->modelsize;
		if(nextOffset <= offset)
			return false;
		offset = nextOffset;
	}
	if(!sawEntry)
		return false;
	return writeFileExact(dst, data.data(), data.size());
}

static bool
ensureTextFileExists(const char *path, const char *contents)
{
	if(doesFileExist(path))
		return true;
	if(!EnsureParentDirectoriesForPath(path))
		return false;
	FILE *f = fopen(path, "w");
	if(f == nil)
		return false;
	fputs(contents, f);
	fclose(f);
	return true;
}

static bool
captureRollbackEntry(std::vector<FileRollbackEntry> &entries, const char *path)
{
	for(size_t i = 0; i < entries.size(); i++)
		if(entries[i].path == path)
			return true;

	FileRollbackEntry entry;
	entry.path = path;
	entry.existed = doesFileExist(path);
	if(entry.existed){
		FILE *f = fopen(path, "rb");
		if(f == nil)
			return false;
		if(fseek(f, 0, SEEK_END) != 0){
			fclose(f);
			return false;
		}
		long size = ftell(f);
		if(size < 0){
			fclose(f);
			return false;
		}
		if(fseek(f, 0, SEEK_SET) != 0){
			fclose(f);
			return false;
		}
		entry.data.resize((size_t)size);
		if(size > 0 && fread(entry.data.data(), 1, (size_t)size, f) != (size_t)size){
			fclose(f);
			return false;
		}
		fclose(f);
	}
	entries.push_back(entry);
	return true;
}

static void
rollbackTouchedFiles(const std::vector<FileRollbackEntry> &entries)
{
	for(size_t i = entries.size(); i > 0; i--){
		const FileRollbackEntry &entry = entries[i-1];
		if(entry.existed)
			writeFileExact(entry.path.c_str(), entry.data.data(), entry.data.size());
		else
			remove(entry.path.c_str());
	}
}

static void
trimLineEnding(char *line)
{
	char *p = line + strlen(line);
	while(p > line && (p[-1] == '\n' || p[-1] == '\r'))
		*--p = '\0';
}

static bool
appendUniqueLine(const char *path, const char *line)
{
	char existing[1024];
	FILE *f = fopen(path, "r");
	if(f){
		while(fgets(existing, sizeof(existing), f)){
			trimLineEnding(existing);
			if(strcmp(existing, line) == 0){
				fclose(f);
				return true;
			}
		}
		fclose(f);
	}
	if(!EnsureParentDirectoriesForPath(path))
		return false;
	f = fopen(path, "a");
	if(f == nil)
		return false;
	bool needNewline = false;
	if(fseek(f, 0, SEEK_END) == 0 && ftell(f) > 0){
		if(fseek(f, -1, SEEK_END) == 0)
			needNewline = fgetc(f) != '\n';
		fseek(f, 0, SEEK_END);
	}
	if(needNewline)
		fputc('\n', f);
	fprintf(f, "%s\n", line);
	fclose(f);
	return true;
}

static void
resetCustomImportPreview(void)
{
	memset(&gCustomImport.previewObj, 0, sizeof(gCustomImport.previewObj));
	gCustomImport.previewObj.m_type = ObjectDef::ATOMIC;
	gCustomImport.previewObj.m_numAtomics = 1;
	gCustomImport.previewObj.m_drawDist[0] = gCustomImport.drawDist;
}

static void
resetCustomImportState(void)
{
	memset(&gCustomImport, 0, sizeof(gCustomImport));
	gCustomImport.active = true;
	gCustomImport.objectId = findSuggestedCustomImportId();
	gCustomImport.drawDist = 300.0f;
	resetCustomImportPreview();
}

static void
beginEmptyCustomImport(void)
{
	resetCustomImportState();
	gOpenCustomImport = true;
}

static void
clearCustomImportMessages(void)
{
	gCustomImport.error[0] = '\0';
	gCustomImport.warning[0] = '\0';
}

static void
clearCustomImportColSelection(void)
{
	gCustomImport.colSource[0] = '\0';
	gCustomImport.hasCol = false;
	gCustomImport.preferAutoCol = true;
	clearCustomImportMessages();
}

static void
setCustomImportColPath(const char *path)
{
	if(path == nil || path[0] == '\0'){
		clearCustomImportColSelection();
		return;
	}
	strncpy(gCustomImport.colSource, path, sizeof(gCustomImport.colSource)-1);
	gCustomImport.colSource[sizeof(gCustomImport.colSource)-1] = '\0';
	gCustomImport.hasCol = true;
	gCustomImport.preferAutoCol = false;
	clearCustomImportMessages();
}

static void
autofillCustomImportSiblingPaths(const char *baseName)
{
	char txdPath[1024];
	char colPath[1024];
	if(gCustomImport.sourceDir[0] == '\0' || baseName == nil || baseName[0] == '\0')
		return;
	if(buildSiblingPath(txdPath, sizeof(txdPath), gCustomImport.sourceDir, baseName, ".txd") &&
	   doesFileExist(txdPath)){
		strncpy(gCustomImport.txdSource, txdPath, sizeof(gCustomImport.txdSource)-1);
		gCustomImport.txdSource[sizeof(gCustomImport.txdSource)-1] = '\0';
	}
	if(!gCustomImport.preferAutoCol &&
	   buildSiblingPath(colPath, sizeof(colPath), gCustomImport.sourceDir, baseName, ".col") &&
	   doesFileExist(colPath))
		setCustomImportColPath(colPath);
}

static void
setCustomImportDffPath(const char *path)
{
	char detectedBase[MODELNAMELEN];
	if(path == nil || path[0] == '\0')
		return;
	strncpy(gCustomImport.dffSource, path, sizeof(gCustomImport.dffSource)-1);
	gCustomImport.dffSource[sizeof(gCustomImport.dffSource)-1] = '\0';
	if(getDirectoryCopy(path, gCustomImport.sourceDir, sizeof(gCustomImport.sourceDir))){
		stripExtensionCopy(path, detectedBase, sizeof(detectedBase));
		strncpy(gCustomImport.sourceBase, detectedBase, sizeof(gCustomImport.sourceBase)-1);
		gCustomImport.sourceBase[sizeof(gCustomImport.sourceBase)-1] = '\0';
		strncpy(gCustomImport.modelName, detectedBase, sizeof(gCustomImport.modelName)-1);
		gCustomImport.modelName[sizeof(gCustomImport.modelName)-1] = '\0';
		gCustomImport.txdSource[0] = '\0';
		strncpy(gCustomImport.txdName, detectedBase, sizeof(gCustomImport.txdName)-1);
		gCustomImport.txdName[sizeof(gCustomImport.txdName)-1] = '\0';
		if(!gCustomImport.preferAutoCol){
			gCustomImport.colSource[0] = '\0';
			gCustomImport.hasCol = false;
		}
		autofillCustomImportSiblingPaths(detectedBase);
	}
	clearCustomImportMessages();
}

static void
setCustomImportTxdPath(const char *path)
{
	char detectedBase[MODELNAMELEN];
	if(path == nil || path[0] == '\0')
		return;
	strncpy(gCustomImport.txdSource, path, sizeof(gCustomImport.txdSource)-1);
	gCustomImport.txdSource[sizeof(gCustomImport.txdSource)-1] = '\0';
	stripExtensionCopy(path, detectedBase, sizeof(detectedBase));
	strncpy(gCustomImport.txdName, detectedBase, sizeof(gCustomImport.txdName)-1);
	gCustomImport.txdName[sizeof(gCustomImport.txdName)-1] = '\0';
	clearCustomImportMessages();
}

static bool
chooseCustomImportFile(const char *expectedExt, char *pickedPath, size_t pickedPathSize)
{
	char path[1024];
	path[0] = '\0';
	if(!pickFileDialog(path, sizeof(path), expectedExt))
		return false;
	if(!pathHasExtensionCi(path, expectedExt)){
		snprintf(gCustomImport.error, sizeof(gCustomImport.error),
		         "Please choose a %s file.", expectedExt);
		return false;
	}
	strncpy(pickedPath, path, pickedPathSize-1);
	pickedPath[pickedPathSize-1] = '\0';
	return true;
}

static int
computeFlagsFromObjectDef(const ObjectDef *obj)
{
	int flags = 0;
	switch(params.objFlagset){
	case GAME_III:
		if(obj->m_normalCull) flags |= 1;
		if(obj->m_noFade) flags |= 2;
		if(obj->m_drawLast) flags |= obj->m_additive ? 8 : 4;
		if(obj->m_isSubway) flags |= 0x10;
		if(obj->m_ignoreLight) flags |= 0x20;
		if(obj->m_noZwrite) flags |= 0x40;
		break;
	case GAME_VC:
		if(obj->m_wetRoadReflection) flags |= 1;
		if(obj->m_noFade) flags |= 2;
		if(obj->m_drawLast) flags |= obj->m_additive ? 8 : 4;
		if(obj->m_isSubway) flags |= 0x10;
		if(obj->m_ignoreLight) flags |= 0x20;
		if(obj->m_noZwrite) flags |= 0x40;
		if(obj->m_noShadows) flags |= 0x80;
		if(obj->m_ignoreDrawDist) flags |= 0x100;
		if(obj->m_isCodeGlass) flags |= 0x200;
		if(obj->m_isArtistGlass) flags |= 0x400;
		break;
	case GAME_SA:
		if(obj->m_drawLast) flags |= obj->m_additive ? 8 : 4;
		if(obj->m_noZwrite) flags |= 0x40;
		if(obj->m_noShadows) flags |= 0x80;
		if(obj->m_noBackfaceCulling) flags |= 0x200000;
		if(obj->m_type == ObjectDef::ATOMIC){
			if(obj->m_wetRoadReflection) flags |= 1;
			if(obj->m_dontCollideWithFlyer) flags |= 0x8000;
			if(obj->m_isCodeGlass) flags |= 0x200;
			if(obj->m_isArtistGlass) flags |= 0x400;
			if(obj->m_isGarageDoor) flags |= 0x800;
			if(obj->m_isDamageable && !obj->m_isTimed) flags |= 0x1000;
			if(obj->m_isTree) flags |= 0x2000;
			if(obj->m_isPalmTree) flags |= 0x4000;
			if(obj->m_isTag) flags |= 0x100000;
			if(obj->m_noCover) flags |= 0x400000;
			if(obj->m_wetOnly) flags |= 0x800000;
		}else if(obj->m_isDoor)
			flags |= 0x20;
		break;
	}
	return flags;
}

static void
uiObjectFlagsEditor(ObjectDef *obj)
{
	switch(params.objFlagset){
	case GAME_III:
		ImGui::Checkbox("Normal cull", &obj->m_normalCull);
		ImGui::Checkbox("No Fade", &obj->m_noFade);
		ImGui::Checkbox("Draw Last", &obj->m_drawLast);
		ImGui::Checkbox("Additive Blend", &obj->m_additive);
		if(obj->m_additive) obj->m_drawLast = true;
		ImGui::Checkbox("Is Subway", &obj->m_isSubway);
		ImGui::Checkbox("Ignore Light", &obj->m_ignoreLight);
		ImGui::Checkbox("No Z-write", &obj->m_noZwrite);
		break;

	case GAME_VC:
		ImGui::Checkbox("Wet Road Effect", &obj->m_wetRoadReflection);
		ImGui::Checkbox("No Fade", &obj->m_noFade);
		ImGui::Checkbox("Draw Last", &obj->m_drawLast);
		ImGui::Checkbox("Additive Blend", &obj->m_additive);
		if(obj->m_additive) obj->m_drawLast = true;
		ImGui::Checkbox("Ignore Light", &obj->m_ignoreLight);
		ImGui::Checkbox("No Z-write", &obj->m_noZwrite);
		ImGui::Checkbox("No shadows", &obj->m_noShadows);
		ImGui::Checkbox("Ignore Draw Dist", &obj->m_ignoreDrawDist);
		ImGui::Checkbox("Code Glass", &obj->m_isCodeGlass);
		ImGui::Checkbox("Artist Glass", &obj->m_isArtistGlass);
		break;

	case GAME_SA: {
		ImGui::Checkbox("Draw Last", &obj->m_drawLast);
		ImGui::Checkbox("Additive Blend", &obj->m_additive);
		if(obj->m_additive) obj->m_drawLast = true;
		ImGui::Checkbox("No Z-write", &obj->m_noZwrite);
		ImGui::Checkbox("No shadows", &obj->m_noShadows);
		ImGui::Checkbox("No Backface Culling", &obj->m_noBackfaceCulling);
		if(obj->m_type == ObjectDef::ATOMIC){
			ImGui::Checkbox("Wet Road Effect", &obj->m_wetRoadReflection);
			ImGui::Checkbox("Don't collide with Flyer", &obj->m_dontCollideWithFlyer);

			int flag = (int)obj->m_isCodeGlass |
				(int)obj->m_isArtistGlass<<1 |
				(int)obj->m_isGarageDoor<<2 |
				(int)obj->m_isDamageable<<3 |
				(int)obj->m_isTree<<4 |
				(int)obj->m_isPalmTree<<5 |
				(int)obj->m_isTag<<6 |
				(int)obj->m_noCover<<7 |
				(int)obj->m_wetOnly<<8;
			ImGui::RadioButton("None", &flag, 0);
			ImGui::RadioButton("Code Glass", &flag, 1);
			ImGui::RadioButton("Artist Glass", &flag, 2);
			ImGui::RadioButton("Garage Door", &flag, 4);
			if(!obj->m_isTimed)
				ImGui::RadioButton("Damageable", &flag, 8);
			ImGui::RadioButton("Tree", &flag, 0x10);
			ImGui::RadioButton("Palm Tree", &flag, 0x20);
			ImGui::RadioButton("Tag", &flag, 0x40);
			ImGui::RadioButton("No Cover", &flag, 0x80);
			ImGui::RadioButton("Wet Only", &flag, 0x100);
			obj->m_isCodeGlass = !!(flag & 1);
			obj->m_isArtistGlass = !!(flag & 2);
			obj->m_isGarageDoor = !!(flag & 4);
			obj->m_isDamageable = !!(flag & 8);
			obj->m_isTree = !!(flag & 0x10);
			obj->m_isPalmTree = !!(flag & 0x20);
			obj->m_isTag = !!(flag & 0x40);
			obj->m_noCover = !!(flag & 0x80);
			obj->m_wetOnly = !!(flag & 0x100);
		}else if(obj->m_type == ObjectDef::CLUMP)
			ImGui::Checkbox("Door", &obj->m_isDoor);
		break;
	}
	}
}

static int
findSuggestedCustomImportId(void)
{
	int limit = NUMOBJECTDEFS;
	int start = gCustomImportPreferredStartId;
	if(start < 0)
		start = 0;
	if(start >= limit)
		start = limit - 1;
	int maxExisting = start - 1;
	for(int i = start; i < limit; i++)
		if(GetObjectDef(i))
			maxExisting = i;
	for(int i = maxExisting + 1; i < limit; i++)
		if(GetObjectDef(i) == nil)
			return i;
	for(int i = start; i < limit; i++)
		if(GetObjectDef(i) == nil)
			return i;
	return -1;
}

static void
beginCustomImportFromPath(const char *path)
{
	char detectedBase[MODELNAMELEN];
	char dffPath[1024];
	char txdPath[1024];
	char colPath[1024];

	resetCustomImportState();
	stripExtensionCopy(path, detectedBase, sizeof(detectedBase));
	if(!getDirectoryCopy(path, gCustomImport.sourceDir, sizeof(gCustomImport.sourceDir)))
		return;
	if(!buildSiblingPath(dffPath, sizeof(dffPath), gCustomImport.sourceDir, detectedBase, ".dff") ||
	   !buildSiblingPath(txdPath, sizeof(txdPath), gCustomImport.sourceDir, detectedBase, ".txd") ||
	   !buildSiblingPath(colPath, sizeof(colPath), gCustomImport.sourceDir, detectedBase, ".col"))
		return;
	if(!doesFileExist(dffPath) || !doesFileExist(txdPath)){
		Toast(TOAST_SPAWN, "Custom import needs matching .dff and .txd in the same folder");
		return;
	}

	strncpy(gCustomImport.sourceBase, detectedBase, sizeof(gCustomImport.sourceBase)-1);
	strncpy(gCustomImport.modelName, detectedBase, sizeof(gCustomImport.modelName)-1);
	strncpy(gCustomImport.txdName, detectedBase, sizeof(gCustomImport.txdName)-1);
	strncpy(gCustomImport.dffSource, dffPath, sizeof(gCustomImport.dffSource)-1);
	strncpy(gCustomImport.txdSource, txdPath, sizeof(gCustomImport.txdSource)-1);
	if(doesFileExist(colPath)){
		strncpy(gCustomImport.colSource, colPath, sizeof(gCustomImport.colSource)-1);
		gCustomImport.hasCol = true;
	}
	gOpenCustomImport = true;
}

static bool
hasInvalidModelTokenChars(const char *s)
{
	if(s == nil || s[0] == '\0')
		return true;
	for(const char *p = s; *p; p++)
		if(!isalnum((unsigned char)*p) && *p != '_')
			return true;
	return false;
}

static GameFile*
getOrCreateCustomImportGameFile(GameFile **cache, const char *logicalPath)
{
	if(*cache){
		if((*cache)->sourcePath == nil){
			const char *src = ModloaderGetSourcePath(logicalPath);
			if(src)
				(*cache)->sourcePath = strdup(src);
			else{
				char exportPath[1024];
				if(BuildModloaderLogicalExportPath(logicalPath, exportPath, sizeof(exportPath)))
					(*cache)->sourcePath = strdup(exportPath);
			}
		}
		return *cache;
	}
	char mutablePath[256];
	strncpy(mutablePath, logicalPath, sizeof(mutablePath)-1);
	mutablePath[sizeof(mutablePath)-1] = '\0';
	*cache = NewGameFile(mutablePath);
	return *cache;
}

static GameFile*
FindMatchingIdeForIpl(GameFile *iplFile)
{
	if(!iplFile || !iplFile->name) return nil;
	char ideName[1024];
	strncpy(ideName, iplFile->name, sizeof(ideName)-1);
	ideName[sizeof(ideName)-1] = '\0';
	char *dot = strrchr(ideName, '.');
	if(!dot) return nil;
	
	strcpy(dot, ".ide");
	GameFile *ideFile = nil;
	for(int j = 0; j < NUMOBJECTDEFS; j++){
		ObjectDef *otherObj = GetObjectDef(j);
		if(otherObj && otherObj->m_file && rw::strncmp_ci(otherObj->m_file->name, ideName, 1024) == 0){
			ideFile = otherObj->m_file;
			break;
		}
	}
	if(!ideFile){
		char *streamMatch = strstr(ideName, "_stream");
		if(streamMatch){
			strcpy(streamMatch, ".ide");
			for(int j = 0; j < NUMOBJECTDEFS; j++){
				ObjectDef *otherObj = GetObjectDef(j);
				if(otherObj && otherObj->m_file && rw::strncmp_ci(otherObj->m_file->name, ideName, 1024) == 0){
					ideFile = otherObj->m_file;
					break;
				}
			}
		}
	}
	return ideFile;
}

static bool
spawnCustomImportedObject(int objectId, GameFile *targetIplOverride = nil)
{
	ObjectDef *obj = GetObjectDef(objectId);
	if(obj == nil)
		return false;

	rw::V3d position = GetPlacementPosition();
	GameFile *file = targetIplOverride
		? targetIplOverride
		: getOrCreateCustomImportGameFile(&gCustomImportIplFile, CUSTOM_IMPORT_IPL_LOGICAL_PATH);
	int maxIplIndex = -1;
	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *other = (ObjectInst*)p->item;
		if(other->m_file == file && other->m_imageIndex < 0 && other->m_iplIndex > maxIplIndex)
			maxIplIndex = other->m_iplIndex;
	}

	ObjectInst *inst = AddInstance();
	inst->m_objectId = objectId;
	inst->m_area = currentArea;
	inst->m_rotation.x = 0.0f;
	inst->m_rotation.y = 0.0f;
	inst->m_rotation.z = 0.0f;
	inst->m_rotation.w = 1.0f;
	inst->m_translation = position;
	inst->m_lodId = -1;
	inst->m_lod = nil;
	inst->m_numChildren = 0;
	inst->m_file = file;
	inst->m_imageIndex = -1;
	inst->m_binInstIndex = -1;
	inst->m_iplIndex = maxIplIndex + 1;
	SetInstIplFilterKey(inst, file ? file->name : nil);
	inst->m_isAdded = true;
	inst->m_isDirty = true;
	inst->m_savedStateValid = false;
	inst->m_wasSavedDeleted = false;
	inst->m_gameEntityExists = false;
	StampChangeSeq(inst);

	if(obj->m_isBigBuilding)
		inst->SetupBigBuilding();
	inst->UpdateMatrix();
	if(!obj->IsLoaded()){
		RequestObject(objectId);
		LoadAllRequestedObjects();
	}

	inst->CreateRwObject();
	if(obj->m_colModel)
		InsertInstIntoSectors(inst);
	else{
		CPtrList *list = inst->m_isBigBuilding
			? &outOfBoundsSector.bigbuildings : &outOfBoundsSector.buildings;
		list->InsertItem(inst);
	}

	ClearSelection();
	inst->Select();
	ObjectInst *pasted[1] = { inst };
	UndoRecordPaste(pasted, 1);
	return true;
}

void
HandleCustomImportDrop(const char *path)
{
	if(path == nil)
		return;
	if(pathHasExtensionCi(path, ".dff") ||
	   pathHasExtensionCi(path, ".txd") ||
	   pathHasExtensionCi(path, ".col"))
		beginCustomImportFromPath(path);
}

static void
uiMainmenu(void)
{
	if(ImGui::BeginMainMenuBar()){
		if(ImGui::BeginMenu((std::string(ICON_FA_FOLDER_OPEN " ") + _("File")).c_str())){
			if(ImGui::MenuItem((std::string(ICON_FA_FLOPPY_DISK " ") + _(" Save All IPLs")).c_str(), "Ctrl+S")){
				if(saveAllIpls())
					Toast(TOAST_SAVE, "Saved all IPL files to %s", getSaveDestinationLabel());
			}
			ImGui::SetItemTooltip("%s", _("Saves all modified objects in their respective placement files (.ipl)."));
			if(ImGui::MenuItem((std::string(ICON_FA_GAMEPAD " ") + _(" Test in Game")).c_str(), "Ctrl+G")){
				testInGame();
			}
			ImGui::SetItemTooltip("%s", _("Launches your game and spawns you to the current camera position.\nRequires ariane.asi installed in your game folder."));
			if(ImGui::MenuItem(_("Save to Modloader"), nil,
			                   gSaveDestination == SAVE_DESTINATION_MODLOADER)){
				gSaveDestination = gSaveDestination == SAVE_DESTINATION_MODLOADER ?
					SAVE_DESTINATION_ORIGINAL_FILES : SAVE_DESTINATION_MODLOADER;
				saveSaveSettings();
			}
			ImGui::SetItemTooltip("%s", _("When enabled, saves go to modloader/Ariane/ instead of\noverwriting original game files."));
			if(ImGui::MenuItem(_("Save New Objects to Original IPL"), nil, gSaveToOriginalIpl)){
				gSaveToOriginalIpl = !gSaveToOriginalIpl;
				saveSaveSettings();
			}
			ImGui::SetItemTooltip("%s", _("When enabled, newly placed objects save to their original IPL file\ninstead of custom.ipl. Helps keep objects organized by area."));
			if(ImGui::MenuItem((std::string(ICON_FA_BOLT " ") + _(" Hot Reload")).c_str(), "Ctrl+R")){
				hotReloadIpls();
			}
			ImGui::SetItemTooltip("%s", _("Instantly apply your changes in a running SA game without restarting.\nRequires ariane.asi."));
			ImGui::Separator();
			if(ImGui::MenuItem((std::string(ICON_FA_FILE_EXPORT " ") + _(" Export Prefab...")).c_str(), "Ctrl+Shift+E", false, selection.first != nil)){
				gOpenExportPrefab = true;
			}
			ImGui::SetItemTooltip("%s", _("Saves the selected objects as a reusable prefab file (.ariane)\nthat you can import later or share."));
			if(ImGui::MenuItem((std::string(ICON_FA_FILE_IMPORT " ") + _(" Import Prefab...")).c_str(), "Ctrl+Shift+I")){
				gOpenImportPrefab = true;
			}
			ImGui::SetItemTooltip("%s", _("Loads a previously exported prefab file and places those\nobjects into the current map."));
			if(ImGui::MenuItem((std::string(ICON_FA_CUBE " ") + _(" Import Custom Object...")).c_str())){
				beginEmptyCustomImport();
			}
			ImGui::SetItemTooltip("%s", _("Import a custom DFF/TXD into the editor as a new placeable object.\nAutomatically registers it in your game files, ready to use in game."));
			// TODO: restore once whole-map export is safe for runtime use.
			if(ImGui::MenuItem((std::string(ICON_FA_RIGHT_FROM_BRACKET " ") + _(" Exit")).c_str(), "Alt+F4")) sk::globals.quit = 1;
			ImGui::EndMenu();
		}
		if(ImGui::BeginMenu((std::string(ICON_FA_WINDOW_MAXIMIZE " ") + _(" Window")).c_str())){
			if(ImGui::MenuItem((std::string(ICON_FA_CLOUD_SUN " ") + _(" Time & Weather")).c_str(), "T", showTimeWeatherWindow)) { showTimeWeatherWindow ^= 1; }
			if(ImGui::MenuItem((std::string(ICON_FA_EYE " ") + _(" View")).c_str(), "V", showViewWindow)) { showViewWindow ^= 1; }
			if(ImGui::MenuItem((std::string(ICON_FA_PAINTBRUSH " ") + _(" Rendering")).c_str(), "R", showRenderingWindow)) { showRenderingWindow ^= 1; }
			if(ImGui::MenuItem((std::string(ICON_FA_WRENCH " ") + _(" Tools")).c_str(), "X", showToolsWindow)) { showToolsWindow ^= 1; }
			if(ImGui::MenuItem((std::string(ICON_FA_CIRCLE_INFO " ") + _(" Object Info")).c_str(), "I", showInstanceWindow)) { showInstanceWindow ^= 1; }
			if(ImGui::MenuItem((std::string(ICON_FA_PEN " ") + _(" Editor")).c_str(), "E", showEditorWindow)) { showEditorWindow ^= 1; }
			if(ImGui::MenuItem((std::string(ICON_FA_MAGNIFYING_GLASS " ") + _(" Object Browser")).c_str(), "B", showBrowserWindow)) { showBrowserWindow ^= 1; }
			if(ImGui::MenuItem((std::string(ICON_FA_CODE_COMPARE " ") + _(" Changes")).c_str(), "F", showDiffWindow)) { showDiffWindow ^= 1; }
			if(ImGui::MenuItem((std::string(ICON_FA_LIST " ") + _(" Log ")).c_str(), nil, showLogWindow)) { showLogWindow ^= 1; }
			if(ImGui::MenuItem("Demo ", nil, showDemoWindow)) { showDemoWindow ^= 1; }
			if(ImGui::MenuItem((std::string(ICON_FA_CIRCLE_QUESTION " ") + _(" Help")).c_str(), nil, showHelpWindow)) { showHelpWindow ^= 1; }
			ImGui::Separator();
			if(ImGui::BeginMenu((std::string(ICON_FA_BELL " ") + _(" Notifications")).c_str())){
				uiNotificationSettings();
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		
		if(ImGui::BeginMenu(_("Options"))){
			if(ImGui::MenuItem(_("Language"), nil, gUILanguage == 1)){
				gUILanguage = gUILanguage == 1 ? 0 : 1;
				saveSaveSettings();
			}
			ImGui::EndMenu();
		}

		if(ImGui::ArrowButton("##intdec", ImGuiDir_Left) && currentArea > 0)
			currentArea--;
		ImGui::SameLine(0, 2);
		ImGui::PushItemWidth(40);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, ImGui::GetStyle().FramePadding.y));
		static char intbuf[16];
		sprintf(intbuf, "%d", currentArea);
		float tw = ImGui::CalcTextSize(intbuf).x;
		float pad = (40 - tw) * 0.5f;
		if(pad < 0) pad = 0;
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(pad, ImGui::GetStyle().FramePadding.y));
		ImGui::InputInt("##interior", &currentArea, 0, 0);
		if(currentArea < 0) currentArea = 0;
		ImGui::PopStyleVar(2);
		ImGui::PopItemWidth();
		ImGui::SameLine(0, 2);
		ImGui::ArrowButton("##intinc", ImGuiDir_Right);
		if(ImGui::IsItemClicked())
			currentArea++;
		ImGui::SameLine();
		ImGui::Text("%s", _("Interior"));


		ImGui::Separator();
		ImGui::Text("%s", _("UI"));
		ImGui::SameLine();
		if(ImGui::SmallButton("-")) ImGui::GetIO().FontGlobalScale *= 0.9f;
		ImGui::SameLine();
		if(ImGui::SmallButton("+")) ImGui::GetIO().FontGlobalScale *= 1.1f;
		ImGui::Separator();
		ImGui::Text("%.3f ms/frame %.1f FPS", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		ImGui::EndMainMenuBar();
	}
	uiExportPrefabPopup();
	uiImportPrefabPopup();
	uiCustomImportPopup();
}

static void
uiExportPrefabPopup(void)
{
	static char prefabName[128] = "";

	if(gOpenExportPrefab){
		gShowExportPrefab = true;
		gOpenExportPrefab = false;
		prefabName[0] = '\0';
		ImGui::SetNextWindowFocus();
	}

	if(BeginEditorDialog("Export Prefab", &gShowExportPrefab)){
		ImGui::Text("Save selection as prefab");
		ImGui::InputText("Name", prefabName, sizeof(prefabName));

		bool validName = prefabName[0] != '\0';
		if(ImGui::Button("Export", ImVec2(120, 0)) && validName){
			char path[512];
			char prefabDir[512];
			if(!GetArianeDataPath(prefabDir, sizeof(prefabDir), "prefabs") ||
			   snprintf(path, sizeof(path), "%s/%s.ariane", prefabDir, prefabName) >= (int)sizeof(path)){
				Toast(TOAST_SAVE, "Failed to resolve prefab path");
				gShowExportPrefab = false;
				EndEditorDialog();
				return;
			}
			int exported = ExportPrefab(path);
			if(exported > 0)
				Toast(TOAST_SAVE, "Exported %d instance(s) to %s", exported, path);
			else
				Toast(TOAST_SAVE, "Failed to export prefab");
			gShowExportPrefab = false;
		}
		ImGui::SameLine();
		if(ImGui::Button("Cancel", ImVec2(120, 0)))
			gShowExportPrefab = false;

		EndEditorDialog();
	}
}

static void
scanPrefabDir(const char *dir, char files[][256], char fullPaths[][512], int *numFiles, int maxFiles)
{
#ifdef _WIN32
	char pattern[512];
	snprintf(pattern, sizeof(pattern), "%s\\*.ariane", dir);
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(pattern, &fd);
	if(h == INVALID_HANDLE_VALUE) return;
	do {
		if(!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && *numFiles < maxFiles){
			bool duplicate = false;
			for(int i = 0; i < *numFiles; i++)
				if(strcmp(files[i], fd.cFileName) == 0){
					duplicate = true;
					break;
				}
			if(duplicate)
				continue;
			strncpy(files[*numFiles], fd.cFileName, 255);
			files[*numFiles][255] = '\0';
			snprintf(fullPaths[*numFiles], 512, "%s/%s", dir, fd.cFileName);
			fullPaths[*numFiles][511] = '\0';
			(*numFiles)++;
		}
	} while(FindNextFileA(h, &fd));
	FindClose(h);
#else
	DIR *d = opendir(dir);
	if(!d) return;
	struct dirent *ent;
	while((ent = readdir(d)) != nil){
		if(ent->d_name[0] == '.') continue;
		size_t len = strlen(ent->d_name);
		if(len > 7 && strcmp(ent->d_name + len - 7, ".ariane") == 0){
			if(*numFiles < maxFiles){
				bool duplicate = false;
				for(int i = 0; i < *numFiles; i++)
					if(strcmp(files[i], ent->d_name) == 0){
						duplicate = true;
						break;
					}
				if(duplicate)
					continue;
				strncpy(files[*numFiles], ent->d_name, 255);
				files[*numFiles][255] = '\0';
				snprintf(fullPaths[*numFiles], 512, "%s/%s", dir, ent->d_name);
				fullPaths[*numFiles][511] = '\0';
				(*numFiles)++;
			}
		}
	}
	closedir(d);
#endif
}

static void
uiImportPrefabPopup(void)
{
	static char prefabFiles[128][256];
	static char prefabPaths[128][512];
	static int numPrefabFiles = 0;
	static char manualPath[512] = "";
	static int selectedFile = -1;

	if(gOpenImportPrefab){
		char prefabDir[512];
		gShowImportPrefab = true;
		gOpenImportPrefab = false;
		manualPath[0] = '\0';
		selectedFile = -1;
		numPrefabFiles = 0;
		if(GetArianeDataPath(prefabDir, sizeof(prefabDir), "prefabs"))
			scanPrefabDir(prefabDir, prefabFiles, prefabPaths, &numPrefabFiles, 128);
		scanPrefabDir("prefabs", prefabFiles, prefabPaths, &numPrefabFiles, 128);
		ImGui::SetNextWindowFocus();
	}

	if(BeginEditorDialog("Import Prefab", &gShowImportPrefab)){
		ImGui::Text("Load prefab in front of camera");

		if(numPrefabFiles > 0){
			ImGui::Text("Available prefabs:");
			ImGui::BeginChild("PrefabList", ImVec2(350, 150), true);
			for(int i = 0; i < numPrefabFiles; i++)
				if(ImGui::Selectable(prefabFiles[i], selectedFile == i))
					selectedFile = i;
			ImGui::EndChild();
		}else
			ImGui::TextDisabled("No .ariane files found in ariane/prefabs/ or prefabs/");

		ImGui::Separator();
		ImGui::InputText("Or path", manualPath, sizeof(manualPath));

		bool canImport = selectedFile >= 0 || manualPath[0] != '\0';
		if(ImGui::Button("Import", ImVec2(120, 0)) && canImport){
			char path[512];
			if(manualPath[0] != '\0')
				strncpy(path, manualPath, sizeof(path));
			else
				strncpy(path, prefabPaths[selectedFile], sizeof(path));
			path[sizeof(path)-1] = '\0';

			int imported = ImportPrefab(path);
			if(imported > 0)
				Toast(TOAST_SPAWN, "Imported %d instance(s) from prefab", imported);
			else
				Toast(TOAST_SPAWN, "Failed to import prefab");
			gShowImportPrefab = false;
		}
		ImGui::SameLine();
		if(ImGui::Button("Cancel", ImVec2(120, 0)))
			gShowImportPrefab = false;

		EndEditorDialog();
	}
}

static bool
finalizeCustomImport(void)
{
	gCustomImport.error[0] = '\0';
	gCustomImport.warning[0] = '\0';

	if(!isSA()){
		snprintf(gCustomImport.error, sizeof(gCustomImport.error),
		         "Custom import is wired for GTA San Andreas only in this v1.");
		return false;
	}
	if(gCustomImport.objectId < 0){
		snprintf(gCustomImport.error, sizeof(gCustomImport.error), "No free stock-range ID was found.");
		return false;
	}
	if(GetObjectDef(gCustomImport.objectId)){
		snprintf(gCustomImport.error, sizeof(gCustomImport.error), "ID %d is already in use.", gCustomImport.objectId);
		return false;
	}
	if(hasInvalidModelTokenChars(gCustomImport.modelName) || hasInvalidModelTokenChars(gCustomImport.txdName)){
		snprintf(gCustomImport.error, sizeof(gCustomImport.error),
		         "Model/TXD names must use only letters, digits, and underscores.");
		return false;
	}
	if(GetObjectDef(gCustomImport.modelName, nil)){
		snprintf(gCustomImport.error, sizeof(gCustomImport.error),
		         "Model name %s already exists. Change the model name first.", gCustomImport.modelName);
		return false;
	}
	if(FindTxdSlot(gCustomImport.txdName) >= 0){
		snprintf(gCustomImport.error, sizeof(gCustomImport.error),
		         "TXD name %s already exists. Change the TXD name first.", gCustomImport.txdName);
		return false;
	}
	if(strlen(gCustomImport.modelName) >= 24){
		snprintf(gCustomImport.error, sizeof(gCustomImport.error),
		         "Model name %s is too long for COL internal name/export (max 23 chars).", gCustomImport.modelName);
		return false;
	}

	char dffLogical[256], txdLogical[256], colLogical[256];
	char dffTarget[1024], txdTarget[1024], colTarget[1024];
	char manifestPath[1024], iplPath[1024];
	if(!buildCustomImportModelLogicalPath(dffLogical, sizeof(dffLogical), gCustomImport.modelName, "dff") ||
	   !buildCustomImportModelLogicalPath(txdLogical, sizeof(txdLogical), gCustomImport.txdName, "txd") ||
	   !buildCustomImportColLogicalPath(colLogical, sizeof(colLogical), gCustomImport.modelName) ||
	   !BuildModloaderLogicalExportPath(CUSTOM_IMPORT_MANIFEST_LOGICAL_PATH, manifestPath, sizeof(manifestPath)) ||
	   !BuildModloaderLogicalExportPath(CUSTOM_IMPORT_IPL_LOGICAL_PATH, iplPath, sizeof(iplPath)) ||
	   !BuildModloaderLogicalExportPath(dffLogical, dffTarget, sizeof(dffTarget)) ||
	   !BuildModloaderLogicalExportPath(txdLogical, txdTarget, sizeof(txdTarget)) ||
	   !BuildModloaderLogicalExportPath(colLogical, colTarget, sizeof(colTarget))){
		snprintf(gCustomImport.error, sizeof(gCustomImport.error), "Couldn't build export paths.");
		return false;
	}

	bool importCol = gCustomImport.hasCol;
	std::vector<FileRollbackEntry> rollbackEntries;
	if(importCol){
		int colEntryCount = 0;
		bool colAllMatch = false;
		bool colNeedsRename = false;
		if(!inspectColFileForImport(gCustomImport.colSource, &colEntryCount, &colAllMatch,
		                            &colNeedsRename, gCustomImport.modelName)){
			snprintf(gCustomImport.error, sizeof(gCustomImport.error), "Couldn't parse COL file %s.", gCustomImport.colSource);
			return false;
		}
		if(colEntryCount != 1 && !colAllMatch){
			snprintf(gCustomImport.error, sizeof(gCustomImport.error),
			         "COL import only supports automatic renaming for single-entry COL files.");
			return false;
		}
	}

	if(!captureRollbackEntry(rollbackEntries, dffTarget) ||
	   !captureRollbackEntry(rollbackEntries, txdTarget) ||
	   !captureRollbackEntry(rollbackEntries, manifestPath) ||
	   !captureRollbackEntry(rollbackEntries, iplPath) ||
	   !captureRollbackEntry(rollbackEntries, colTarget)){
		snprintf(gCustomImport.error, sizeof(gCustomImport.error), "Couldn't snapshot files for rollback.");
		return false;
	}

	char idePath[1024];
	if(!BuildModloaderLogicalExportPath(CUSTOM_IMPORT_IDE_LOGICAL_PATH, idePath, sizeof(idePath))){
		snprintf(gCustomImport.error, sizeof(gCustomImport.error), "Failed to resolve custom IDE path.");
		return false;
	}
	if(!captureRollbackEntry(rollbackEntries, idePath)){
		snprintf(gCustomImport.error, sizeof(gCustomImport.error), "Couldn't snapshot files for rollback.");
		return false;
	}

	if(!copyFileExact(gCustomImport.dffSource, dffTarget) ||
	   !copyFileExact(gCustomImport.txdSource, txdTarget)){
		snprintf(gCustomImport.error, sizeof(gCustomImport.error), "Failed to copy one or more source files.");
		rollbackTouchedFiles(rollbackEntries);
		ModloaderInit();
		return false;
	}

	ModloaderInit();
	const char *winningDff = ModloaderFindOverride(gCustomImport.modelName, "dff");
	const char *winningTxd = ModloaderFindOverride(gCustomImport.txdName, "txd");
	bool dffOk = winningDff && (pathsEqualCiNormalized(winningDff, dffTarget) || pathsEqualCiNormalized(winningDff, gCustomImport.dffSource));
	bool txdOk = winningTxd && (pathsEqualCiNormalized(winningTxd, txdTarget) || pathsEqualCiNormalized(winningTxd, gCustomImport.txdSource));
	if(!dffOk || !txdOk){
		rollbackTouchedFiles(rollbackEntries);
		ModloaderInit();
		const char *shownDff = winningDff ? winningDff : "<none>";
		const char *shownTxd = winningTxd ? winningTxd : "<none>";
		bool sourceInModloader = pathContainsModloaderDir(gCustomImport.dffSource) ||
		                        pathContainsModloaderDir(gCustomImport.txdSource);
		if(sourceInModloader)
			snprintf(gCustomImport.error, sizeof(gCustomImport.error),
			         "Import is shadowed before Ariane wins override resolution. DFF winner: %s | TXD winner: %s. "
			         "Tip: importing files from another modloader folder can cause this.",
			         shownDff, shownTxd);
		else
			snprintf(gCustomImport.error, sizeof(gCustomImport.error),
			         "Import is shadowed before Ariane wins override resolution. DFF winner: %s | TXD winner: %s.",
			         shownDff, shownTxd);
		return false;
	}

	if(!ensureTextFileExists(iplPath, "inst\nend\n") ||
	   (importCol && !copyColWithInternalRename(gCustomImport.colSource, colTarget, gCustomImport.modelName))){
		snprintf(gCustomImport.error, sizeof(gCustomImport.error), "Failed to prepare custom import files.");
		rollbackTouchedFiles(rollbackEntries);
		ModloaderInit();
		return false;
	}

	if(!doesFileExist(idePath)){
		FILE *ide = fopen(idePath, "w");
		if(ide == nil){
			snprintf(gCustomImport.error, sizeof(gCustomImport.error), "Couldn't create %s", idePath);
			rollbackTouchedFiles(rollbackEntries);
			ModloaderInit();
			return false;
		}
		fprintf(ide, "objs\nend\n");
		fclose(ide);
	}

	std::vector<std::string> lines;
	FILE *ide = fopen(idePath, "r");
	if(ide){
		char line[512];
		while(fgets(line, sizeof(line), ide)){
			trimLineEnding(line);
			lines.push_back(line);
		}
		fclose(ide);
	}
	bool inserted = false;
	char ideEntry[512];
	gCustomImport.previewObj.m_drawDist[0] = gCustomImport.drawDist;
	int ideFlags = computeFlagsFromObjectDef(&gCustomImport.previewObj);
	snprintf(ideEntry, sizeof(ideEntry), "%d, %s, %s, %.1f, %d",
	         gCustomImport.objectId, gCustomImport.modelName, gCustomImport.txdName,
	         gCustomImport.drawDist, ideFlags);
	for(size_t i = 0; i < lines.size(); i++){
		if(strcmp(lines[i].c_str(), ideEntry) == 0){
			inserted = true;
			break;
		}
		if(strcmp(lines[i].c_str(), "end") == 0){
			lines.insert(lines.begin() + (long)i, ideEntry);
			inserted = true;
			break;
		}
	}
	if(!inserted){
		lines.push_back("objs");
		lines.push_back(ideEntry);
		lines.push_back("end");
	}
	ide = fopen(idePath, "w");
	if(ide == nil){
		snprintf(gCustomImport.error, sizeof(gCustomImport.error), "Couldn't rewrite %s", idePath);
		rollbackTouchedFiles(rollbackEntries);
		ModloaderInit();
		return false;
	}
	for(size_t i = 0; i < lines.size(); i++)
		fprintf(ide, "%s\n", lines[i].c_str());
	fclose(ide);

	ObjectDef *obj = AddObjectDef(gCustomImport.objectId);
	if(obj == nil){
		snprintf(gCustomImport.error, sizeof(gCustomImport.error), "Failed to allocate custom object.");
		rollbackTouchedFiles(rollbackEntries);
		ModloaderInit();
		return false;
	}
	int createdTxdSlot = -1;
	auto rollbackRegisteredState = [&](){
		RemoveObjectDef(gCustomImport.objectId);
		if(createdTxdSlot >= 0)
			RemoveTxdSlot(createdTxdSlot);
	};
	obj->m_type = ObjectDef::ATOMIC;
	strncpy(obj->m_name, gCustomImport.modelName, MODELNAMELEN);
	obj->m_name[MODELNAMELEN-1] = '\0';
	obj->m_txdSlot = AddTxdSlot(gCustomImport.txdName);
	createdTxdSlot = obj->m_txdSlot;
	obj->m_numAtomics = 1;
	obj->m_drawDist[0] = gCustomImport.drawDist;
	obj->SetFlags(ideFlags);
	obj->m_isTimed = false;
	
	GameFile *targetIde = nil;
	if(gCustomImport.targetIplFile)
		targetIde = FindMatchingIdeForIpl(gCustomImport.targetIplFile);
	
	obj->m_file = targetIde ? targetIde : getOrCreateCustomImportGameFile(&gCustomImportIdeFile, CUSTOM_IMPORT_IDE_LOGICAL_PATH);
	
	obj->SetupBigBuilding(gCustomImport.objectId, gCustomImport.objectId + 1);

	RequestObject(gCustomImport.objectId);
	LoadAllRequestedObjects();
	if(obj->m_atomics[0] == nil){
		snprintf(gCustomImport.error, sizeof(gCustomImport.error),
		         "Failed to load imported DFF/TXD for %s.", gCustomImport.modelName);
		rollbackRegisteredState();
		rollbackTouchedFiles(rollbackEntries);
		ModloaderInit();
		return false;
	}

	if(!importCol){
		AutoColStats stats = {};
		std::vector<char> generatedCol;
		char autoColError[256];
		autoColError[0] = '\0';
		if(!GenerateCol3FromAtomic(obj->m_atomics[0], gCustomImport.modelName, generatedCol, &stats,
		                           autoColError, sizeof(autoColError)) ||
		   !writeFileExact(colTarget, generatedCol.data(), generatedCol.size())){
			snprintf(gCustomImport.error, sizeof(gCustomImport.error), "%s",
			         autoColError[0] ? autoColError : "Failed to auto-generate COL from DFF geometry.");
			rollbackRegisteredState();
			rollbackTouchedFiles(rollbackEntries);
			ModloaderInit();
			return false;
		}
		int removedTriangles = stats.removedDuplicateIndexTriangles +
		                      stats.removedZeroAreaTriangles +
		                      stats.removedCollinearTriangles;
		if(stats.exceededSoftTriangleThreshold || removedTriangles > 0){
			snprintf(gCustomImport.warning, sizeof(gCustomImport.warning),
			         "Auto-generated COL from DFF geometry (%d -> %d tris, %d -> %d verts).",
			         stats.originalTriangles, stats.finalTriangles,
			         stats.originalVertices, stats.finalVertices);
		}
	}

	char manifestLine[512];
	snprintf(manifestLine, sizeof(manifestLine), "IDE %s", CUSTOM_IMPORT_IDE_LOGICAL_PATH);
	if(!appendUniqueLine(manifestPath, manifestLine)){
		snprintf(gCustomImport.error, sizeof(gCustomImport.error), "Failed to update %s", manifestPath);
		rollbackRegisteredState();
		rollbackTouchedFiles(rollbackEntries);
		ModloaderInit();
		return false;
	}
	
	if(!gCustomImport.targetIplFile){
		snprintf(manifestLine, sizeof(manifestLine), "IPL %s", CUSTOM_IMPORT_IPL_LOGICAL_PATH);
		if(!appendUniqueLine(manifestPath, manifestLine)){
			snprintf(gCustomImport.error, sizeof(gCustomImport.error), "Failed to update %s", manifestPath);
			rollbackRegisteredState();
			rollbackTouchedFiles(rollbackEntries);
			ModloaderInit();
			return false;
		}
	}
	snprintf(manifestLine, sizeof(manifestLine), "COLFILE 0 %s", colLogical);
	if(!appendUniqueLine(manifestPath, manifestLine)){
		snprintf(gCustomImport.error, sizeof(gCustomImport.error), "Failed to register COLFILE addition.");
		rollbackRegisteredState();
		rollbackTouchedFiles(rollbackEntries);
		ModloaderInit();
		return false;
	}

	ModloaderInit();

	{
		char mutableColPath[256];
		strncpy(mutableColPath, colLogical, sizeof(mutableColPath)-1);
		mutableColPath[sizeof(mutableColPath)-1] = '\0';
		GameFile *prevFile = FileLoader::currentFile;
		FileLoader::currentFile = NewGameFile(mutableColPath);
		FileLoader::LoadCollisionFile(colLogical);
		FileLoader::currentFile = prevFile;
		if(obj->m_colModel == nil && !importCol){
			snprintf(gCustomImport.error, sizeof(gCustomImport.error),
			         "Auto-generated COL did not attach to model name %s.", gCustomImport.modelName);
			rollbackRegisteredState();
			rollbackTouchedFiles(rollbackEntries);
			ModloaderInit();
			return false;
		}
		if(obj->m_colModel == nil && gCustomImport.warning[0] == '\0'){
			snprintf(gCustomImport.warning, sizeof(gCustomImport.warning),
			         importCol ?
			         "COL imported, but no collision matched model name %s." :
			         "COL auto-generated, but no collision matched model name %s.",
			         gCustomImport.modelName);
		}
	}

	if(!gCustomImport.targetIplFile)
		SetCustomPlacementIpl(CUSTOM_IMPORT_IPL_LOGICAL_PATH, iplPath, false);
	SetSpawnObjectId(gCustomImport.objectId);
	if(!spawnCustomImportedObject(gCustomImport.objectId, gCustomImport.targetIplFile)){
		snprintf(gCustomImport.error, sizeof(gCustomImport.error), "The object was imported but couldn't be spawned.");
		return false;
	}

	gBrowserIdeListDirty = true;
	if(gCustomImport.warning[0])
		Toast(TOAST_SPAWN, "Imported %s (%d) with warning: %s",
		      gCustomImport.modelName, gCustomImport.objectId, gCustomImport.warning);
	else
		Toast(TOAST_SPAWN, "Imported %s (%d)", gCustomImport.modelName, gCustomImport.objectId);
	gCustomImport.active = false;
	return true;
}

static void
uiCustomImportPopup(void)
{
	if(gOpenCustomImport){
		gOpenCustomImport = false;
		ImGui::SetNextWindowFocus();
	}

	if(!BeginEditorDialog("Import Custom Object", &gCustomImport.active))
		return;

	ImGui::Text("Import custom object in front of camera");
	ImGui::TextDisabled("v1 exports to modloader/Ariane");
	ImGui::Separator();
	ImGui::Text("Files");
	if(ImGui::Button(gCustomImport.dffSource[0] ? pathFilename(gCustomImport.dffSource) : "Choose DFF...")){
		char picked[1024];
		if(chooseCustomImportFile(".dff", picked, sizeof(picked)))
			setCustomImportDffPath(picked);
	}
	ImGui::SameLine();
	ImGui::TextDisabled("Required");
	if(gCustomImport.dffSource[0]){
		ImGui::SameLine();
		if(ImGui::SmallButton("Clear##CustomImportDff")){
			gCustomImport.dffSource[0] = '\0';
			gCustomImport.txdSource[0] = '\0';
			gCustomImport.txdName[0] = '\0';
			if(!gCustomImport.preferAutoCol){
				gCustomImport.colSource[0] = '\0';
				gCustomImport.hasCol = false;
			}
			clearCustomImportMessages();
		}
	}

	if(ImGui::Button(gCustomImport.txdSource[0] ? pathFilename(gCustomImport.txdSource) : "Choose TXD...")){
		char picked[1024];
		if(chooseCustomImportFile(".txd", picked, sizeof(picked)))
			setCustomImportTxdPath(picked);
	}
	ImGui::SameLine();
	ImGui::TextDisabled("Required");
	if(gCustomImport.txdSource[0]){
		ImGui::SameLine();
		if(ImGui::SmallButton("Clear##CustomImportTxd")){
			gCustomImport.txdSource[0] = '\0';
			gCustomImport.txdName[0] = '\0';
			clearCustomImportMessages();
		}
	}

	if(ImGui::Button(gCustomImport.hasCol && gCustomImport.colSource[0] ? pathFilename(gCustomImport.colSource) : "Choose COL...")){
		char picked[1024];
		if(chooseCustomImportFile(".col", picked, sizeof(picked)))
			setCustomImportColPath(picked);
	}
	ImGui::SameLine();
	ImGui::TextDisabled("Optional");
	if(gCustomImport.hasCol && gCustomImport.colSource[0]){
		ImGui::SameLine();
		if(ImGui::SmallButton("Clear##CustomImportCol"))
			clearCustomImportColSelection();
	}

	if(!gCustomImport.dffSource[0] || !gCustomImport.txdSource[0]){
		ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "DFF and TXD are required.");
	}else if(gCustomImport.hasCol && gCustomImport.colSource[0]){
		ImGui::TextDisabled("Using provided COL: %s", pathFilename(gCustomImport.colSource));
	}else{
		ImGui::TextDisabled("No COL selected. Collision will be auto-generated.");
	}
	ImGui::TextDisabled("Tip: you can also drag & drop .dff/.txd/.col files anywhere in Ariane.");

	ImGui::Separator();
	ImGui::InputInt("Object ID", &gCustomImport.objectId);
	int previousPreferredStartId = gCustomImportPreferredStartId;
	ImGui::InputInt("Preferred ID Start", &gCustomImportPreferredStartId);
	sanitizeCustomImportSettings();
	if(gCustomImportPreferredStartId != previousPreferredStartId)
		saveSaveSettings();
	ImGui::SameLine();
	if(ImGui::Button("Suggest Free ID"))
		gCustomImport.objectId = findSuggestedCustomImportId();
	ImGui::InputText("Model", gCustomImport.modelName, sizeof(gCustomImport.modelName));
	ImGui::InputText("TXD", gCustomImport.txdName, sizeof(gCustomImport.txdName));
	ImGui::DragFloat("Draw Distance", &gCustomImport.drawDist, 1.0f, 1.0f, 10000.0f, "%.1f");
	gCustomImport.previewObj.m_drawDist[0] = gCustomImport.drawDist;

	ImGui::Separator();
	ImGui::Text("IDE Flags");
	uiObjectFlagsEditor(&gCustomImport.previewObj);
	ImGui::TextDisabled("Raw flags: 0x%X", computeFlagsFromObjectDef(&gCustomImport.previewObj));

	ImGui::Separator();
	ImGui::Text("Target IPL");
	{
		const char *targetLabel = gCustomImport.targetIplFile
			? gCustomImport.targetIplFile->name
			: CUSTOM_IMPORT_IPL_LOGICAL_PATH;
		if(ImGui::BeginCombo("##TargetIPL", targetLabel)){
			// Default option: custom.ipl
			if(ImGui::Selectable(CUSTOM_IMPORT_IPL_LOGICAL_PATH, gCustomImport.targetIplFile == nil))
				gCustomImport.targetIplFile = nil;
			ImGui::Separator();
			// Collect unique IPL files from loaded instances
			GameFile *uniqueFiles[512];
			int numUnique = 0;
			for(CPtrNode *p = instances.first; p && numUnique < 512; p = p->next){
				ObjectInst *o = (ObjectInst*)p->item;
				if(o && o->m_file && o->m_imageIndex < 0 && pathHasExtensionCi(o->m_file->name, ".ipl")){
					bool found = false;
					for(int i = 0; i < numUnique; i++){
						if(uniqueFiles[i] == o->m_file){ found = true; break; }
					}
					if(!found)
						uniqueFiles[numUnique++] = o->m_file;
				}
			}
			for(int i = 0; i < numUnique; i++){
				if(ImGui::Selectable(uniqueFiles[i]->name, gCustomImport.targetIplFile == uniqueFiles[i]))
					gCustomImport.targetIplFile = uniqueFiles[i];
			}
			ImGui::EndCombo();
		}
		ImGui::TextDisabled(gCustomImport.targetIplFile
			? "Object will be placed into the selected IPL"
			: "Object will be placed into modloader/Ariane custom.ipl");
	}

	if(gCustomImport.hasCol){
		ImGui::Separator();
		ImGui::TextDisabled("COL will be exported as %s.col and renamed internally if needed", gCustomImport.modelName);
	}else{
		ImGui::Separator();
		ImGui::TextDisabled("COL will be auto-generated from DFF geometry as %s.col", gCustomImport.modelName);
	}

	if(gCustomImport.error[0]){
		ImGui::Separator();
		ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", gCustomImport.error);
	}
	if(gCustomImport.warning[0]){
		ImGui::Separator();
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", gCustomImport.warning);
	}

	ImGui::Separator();
	bool canImport = gCustomImport.dffSource[0] != '\0' && gCustomImport.txdSource[0] != '\0';
	ImGui::BeginDisabled(!canImport);
	if(ImGui::Button("Import", ImVec2(120, 0))){
		if(finalizeCustomImport())
			gCustomImport.active = false;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if(ImGui::Button("Cancel", ImVec2(120, 0))){
		gCustomImport.active = false;
		gCustomImport.error[0] = '\0';
		gCustomImport.warning[0] = '\0';
	}

	EndEditorDialog();
}

static void
uiHelpWindow(void)
{
	ImGui::Begin(ICON_FA_CIRCLE_QUESTION " Help", &showHelpWindow);

	if(ImGui::CollapsingHeader(_("What's New in 1.33"), ImGuiTreeNodeFlags_DefaultOpen)){
		ImGui::BulletText("%s", _("Russian Localization: Full UI translation with EN/RU toggle in Options."));
		ImGui::BulletText("%s", _("Manual Transform Input: Type precise Position and Rotation values in Object Info."));
		ImGui::BulletText("%s", _("Fixed Keyboard Shortcuts: Hotkeys are now responsive even when windows are focused."));
		ImGui::BulletText("%s", _("Stability Fix: Corrected IDE manifest injection to prevent map load crashes."));
	}
	ImGui::Separator();
	ImGui::BulletText("Camera controls:\n"
		"LMB: first person look around\n"
		"Ctrl+Alt+LMB; W/S: move forward/backward\n"
		"Shift+WASD: fast fly, Alt+WASD: slow fly (speeds set in Editor > Camera)\n"
		"Mouse wheel (over viewport): FOV zoom\n"
		"MMB: pan\n"
		"Alt+MMB: arc rotate around target\n"
		"Ctrl+Alt+MMB: zoom into target\n"
		"C: toggle viewer camera (longer far clip)"
		);
	ImGui::Separator();
	ImGui::BulletText("Selection: click on an object to select it,\n"
		"Shift+click to add to the selection,\n"
		"Alt+click to remove from the selection,\n"
		"Ctrl+click to toggle selection.\n"
		"Shift+LMB drag: marquee (rectangle) select.\n"
		"  +Ctrl: add to selection, +Alt: remove.");
	ImGui::BulletText("In the editor window, double click an instance to jump there,\n"
		"Right click a selection to deselect it.\n"
		"Right click a deleted instance to undelete it.");
	ImGui::BulletText("Use the filter in the instance list to find instances by name.");
	ImGui::Separator();
	ImGui::BulletText("Gizmo: W = Translate, Q = Rotate\n"
		"Hold Shift while dragging to use the selected snap increment.\n"
		"Select an object or SA path node to manipulate it.\n"
		"SA path nodes use translate only.");
	ImGui::BulletText("Delete/Backspace: delete selected building(s)\n"
		"Deleting also removes linked LOD instances.");
	ImGui::BulletText("Ctrl+C: Copy selected building(s)\n"
		"Ctrl+V: Paste (offset +10 on X), with LOD linking");
	ImGui::BulletText("G: snap selection to ground\n"
		"Shift+G: align selection to ground normal and preserve facing.");
	ImGui::BulletText("Ctrl+S: Save all modified IPL files\n"
		"Deleted instances are commented out with #.");
	ImGui::BulletText("B: Toggle Object Browser\n"
		"Click in 3D view to place selected object.\n"
		"RMB or Escape to exit place mode.");
	ImGui::Separator();
	if(ImGui::CollapsingHeader("Privacy & Telemetry")){
		bool telemetryEnabled = TelemetryIsEnabled();
		if(ImGui::Checkbox("Anonymous telemetry", &telemetryEnabled)){
			TelemetrySetEnabled(telemetryEnabled);
			if(telemetryEnabled){
				TelemetrySendPing();
				Toast(TOAST_SAVE, "Anonymous telemetry enabled");
			}else
				Toast(TOAST_SAVE, "Anonymous telemetry disabled");
		}
		ImGui::TextDisabled("Enabled by default. Disable if you do not want usage pings.");
	}

	if(ImGui::CollapsingHeader("Dear ImGUI help")){
		ImGui::ShowUserGuide();
	}

	ImGui::End();
}

static void
uiWeatherBox(const char *id, int *weather)
{
	if(ImGui::BeginCombo(id, params.weatherInfo[*weather].name)){
		for(int n = 0; n < params.numWeathers; n++){
			bool is_selected = n == *weather;
			static char str[100];
			sprintf(str, "%d - %s", n, params.weatherInfo[n].name);
			if(ImGui::Selectable(str, is_selected))
				*weather = n;
			if(is_selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
}

static void
advanceHour(int diff)
{
	currentHour += diff;
	if(currentHour >= 24)
		currentHour = 0;
	else if(currentHour < 0)
		currentHour = 23;
}

static void
advanceMinute(int diff)
{
	currentMinute += diff;
	if(currentMinute >= 60){
		currentMinute = 0;
		advanceHour(1);
	}else if(currentMinute < 0){
		currentMinute = 59;
		advanceHour(-1);
	}
}

static void
uiTimeWeather(void)
{
	static int weatherWidth;
	if(weatherWidth == 0){
		int i, w;
		for(i = 0; i < params.numWeathers; i++){
			w = ImGui::CalcTextSize(params.weatherInfo[i].name).x;
			if(w > weatherWidth)
				weatherWidth = w;
		}
		weatherWidth += 30;
	}


	ImGui::PushItemWidth(100);
	ImGui::BeginGroup();
	ImGui::Text("Hour");
	ImGui::InputInt("##Hour", &currentHour, 1);
	advanceHour(0);
	ImGui::EndGroup();

	ImGui::SameLine();

	ImGui::BeginGroup();
	ImGui::Text("Minute");
	ImGui::InputInt("##Minute", &currentMinute, 1);
	advanceMinute(0);
	ImGui::EndGroup();


	ImGui::PushItemWidth(0);
	int totalMinute = currentHour*60 + currentMinute;
	ImGui::SliderInt("##TotalMinute", &totalMinute, 0, 24*60-1);
	currentHour = totalMinute/60;
	currentMinute = totalMinute%60;
	ImGui::PopItemWidth();

	if(params.daynightPipe){
		ImGui::SliderFloat("Day/Night Balance", &gDayNightBalance, 0.0f, 1.0f, "%.2f");
		if(gameplatform != PLATFORM_XBOX)
			ImGui::SliderFloat("Wet Road Effect", &gWetRoadEffect, 0.0f, 1.0f, "%.2f");
	}


	ImGui::PushItemWidth(weatherWidth);
	ImGui::BeginGroup();
	ImGui::Text("Weather A");
	uiWeatherBox("##WeatherA", &Weather::oldWeather);
	ImGui::EndGroup();
	ImGui::PopItemWidth();

	ImGui::SameLine();

	ImGui::BeginGroup();
	ImGui::Text("");
	ImGui::SliderFloat("##Interpolation", &Weather::interpolation, 0.0f, 1.0f, "%.2f");
	ImGui::EndGroup();

	ImGui::SameLine();

	ImGui::PushItemWidth(weatherWidth);
	ImGui::BeginGroup();
	ImGui::Text("Weather B");
	uiWeatherBox("##WeatherB", &Weather::newWeather);
	ImGui::EndGroup();
	ImGui::PopItemWidth();
	ImGui::PopItemWidth();

	if(params.timecycle != GAME_III)
		ImGui::SliderInt("Extracolour", &extraColours, -1, params.numExtraColours*params.numHours - 1);

	if(params.neoWorldPipe)
		ImGui::SliderFloat("Neo Light map", &gNeoLightMapStrength, 0.0f, 1.0f, "%.2f");

//	ImGui::SliderFloat("Cloud rotation", &Clouds::CloudRotation, 0.0f, 3.1415f, "%.2f");
}

static void
uiView(void)
{
	ImGui::Checkbox("Draw Collisions", &gRenderCollision);
	if(params.timecycle == GAME_SA)
		ImGui::Checkbox("Draw TimeCycle boxes", &gRenderTimecycleBoxes);
	ImGui::Checkbox("Draw Zones", &gRenderZones);
	if(gRenderZones){
		ImGui::Indent();
		ImGui::Checkbox("Map Zones", &gRenderMapZones);
		switch(gameversion){
		case GAME_III:
			ImGui::Checkbox("Zones", &gRenderNavigZones);
			ImGui::Checkbox("Cull Zones", &gRenderCullZones);
			break;
		case GAME_VC:
			ImGui::Checkbox("Navig Zones", &gRenderNavigZones);
			ImGui::Checkbox("Info Zones", &gRenderInfoZones);
			break;
		case GAME_SA:
			ImGui::Checkbox("Navig Zones", &gRenderNavigZones);
			break;
		}
		ImGui::Checkbox("Attrib Zones", &gRenderAttribZones);
		ImGui::Unindent();
	}
	ImGui::Checkbox("Render 2dfx Lights", &gRenderLightEffects);
	ImGui::Checkbox("Show 2dfx Markers", &gRenderEffects);
	ImGui::SeparatorText("Legacy Paths");
	ImGui::Checkbox("Draw Legacy Car Paths", &gRenderLegacyCarPaths);
	ImGui::Checkbox("Draw Legacy Ped Paths", &gRenderLegacyPedPaths);
	if(isSA()){
		ImGui::SeparatorText("San Andreas Streamed Paths");
		ImGui::Checkbox("Draw SA Car Paths", &gRenderSaCarPaths);
		if(gRenderSaCarPaths){
			ImGui::Indent();
			ImGui::Checkbox("Show Preview Traffic", &gRenderSaCarPathTraffic);
			if(gRenderSaCarPathTraffic){
				ImGui::SliderInt("Preview Traffic Count", &gSaCarPathTrafficCount, 1, 32);
				ImGui::SliderFloat("Preview Traffic Speed", &gSaCarPathTrafficSpeedScale, 0.25f, 3.0f, "%.2fx");
				ImGui::Checkbox("Freeze Preview Routes", &gSaCarPathTrafficFreezeRoutes);
			}
			ImGui::Checkbox("Show Parked Preview Cars", &gRenderSaCarPathParkedCars);
			if(gRenderSaCarPathParkedCars)
				ImGui::SliderInt("Parked Preview Count", &gSaCarPathParkedCarCount, 1, 24);
			ImGui::Unindent();
		}
		ImGui::Checkbox("Draw SA Ped Paths", &gRenderSaPedPaths);
		if(gRenderSaPedPaths){
			ImGui::Indent();
			ImGui::Checkbox("Show Preview Walkers", &gRenderSaPedPathWalkers);
			if(gRenderSaPedPathWalkers)
				ImGui::SliderInt("Preview Walker Count", &gSaPedPathWalkerCount, 1, 32);
			ImGui::Unindent();
		}
		ImGui::Checkbox("Draw SA Area Grid", &gRenderSaAreaGrid);
		ImGui::SetItemTooltip("Show the 8x8 area grid boundaries (750 unit cells).\nNodes cannot be moved across these boundaries.");
	}


	ImGui::Checkbox("Draw Water", &gRenderWater);
	if(params.water == GAME_SA){
		ImGui::SameLine();
		if(ImGui::Button("Edit Water (H)")){
			if(!WaterLevel::gWaterEditMode){
				WaterLevel::gWaterEditMode = true;
				ClearSelection();
				if(gPlaceMode)
					SpawnExitPlaceMode();
			}
		}
	}
	if(gameversion == GAME_SA)
		ImGui::Checkbox("Play Animations", &gPlayAnimations);

	ImGui::RadioButton("Render Normal", &gRenderMode, 0);
	ImGui::RadioButton("Render only HD", &gRenderMode, 1);
	ImGui::RadioButton("Render only LOD", &gRenderMode, 2);
	gRenderOnlyHD = gRenderMode == 1;
	gRenderOnlyLod = gRenderMode == 2;
	ImGui::SliderFloat("Draw Distance", &TheCamera.m_LODmult, 0.5f, 3.0f, "%.3f");
	ImGui::Checkbox("Render all Timed Objects", &gNoTimeCull);
	if(params.numAreas)
		ImGui::Checkbox("Render all Areas", &gNoAreaCull);

	ImGui::Separator();
	ImGui::Text("IPL Visibility");
	RefreshIplVisibilityEntries();

	int numIpls = GetIplVisibilityEntryCount();
	if(numIpls == 0){
		ImGui::TextDisabled("No loaded IPLs");
		return;
	}

	int numVisible = 0;
	for(int i = 0; i < numIpls; i++)
		if(GetIplVisibilityEntryVisible(i))
			numVisible++;
	ImGui::Text("%d visible / %d total", numVisible, numIpls);
	ImGui::InputTextWithHint("##ipl_filter_search", "Search IPLs", gIplFilterSearch, sizeof(gIplFilterSearch));
	if(ImGui::Button("Show All"))
		SetAllIplVisibilityEntries(true);
	ImGui::SameLine();
	if(ImGui::Button("Hide All"))
		SetAllIplVisibilityEntries(false);

	float listHeight = ImGui::GetContentRegionAvail().y;
	if(listHeight < 220.0f)
		listHeight = 220.0f;
	ImGui::BeginChild("##ipl_visibility_list", ImVec2(0, listHeight), true);
	for(int i = 0; i < numIpls; i++){
		const char *name = GetIplVisibilityEntryName(i);
		if(gIplFilterSearch[0] != '\0' && ImStristr(name, nil, gIplFilterSearch, nil) == nil)
			continue;

		bool visible = GetIplVisibilityEntryVisible(i);
		ImGui::PushID(i);
		if(ImGui::SmallButton("Only"))
			ShowOnlyIplVisibilityEntry(i);
		ImGui::SameLine();
		if(ImGui::Checkbox("##visible", &visible))
			SetIplVisibilityEntryVisible(i, visible);
		ImGui::SameLine();
		ImGui::TextUnformatted(name);
		ImGui::PopID();
	}
	ImGui::EndChild();
}

static void
uiRendering(void)
{
	uint32 activeAASamples = sanitizeAASamples(rw::Engine::getMultiSamplingLevels());
	uint32 maxAASamples = sanitizeAASamples(rw::Engine::getMaxMultiSamplingLevels());
	static const uint32 aaOptions[] = { 1, 2, 4, 8, 16 };

	ImGui::Checkbox("Draw PostFX", &gRenderPostFX);
	if(ImGui::BeginCombo("Anti-aliasing", getAASamplesLabel(gRequestedAASamples))){
		for(uint32 samples : aaOptions){
			if(samples > 1 && (maxAASamples <= 1 || samples > maxAASamples))
				continue;

			bool selected = gRequestedAASamples == samples;
			if(ImGui::Selectable(getAASamplesLabel(samples), selected)){
				if(gRequestedAASamples != samples){
					gRequestedAASamples = samples;
					// Note: AA configuration removed from librw skeleton API
					SaveEditorSettingsNow();
					Toast(TOAST_SAVE, "Anti-aliasing set to %s. Restart Ariane to apply it.",
						getAASamplesLabel(samples));
				}
			}
			if(selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	if(maxAASamples <= 1){
		ImGui::TextDisabled("MSAA is not reported by the current renderer/device.");
	}else{
		ImGui::TextDisabled("Current session: %s", getAASamplesLabel(activeAASamples));
		ImGui::SameLine();
		ImGui::TextDisabled("Max: %s", getAASamplesLabel(maxAASamples));
		if(activeAASamples != gRequestedAASamples)
			ImGui::TextDisabled("Restart Ariane to apply the new anti-aliasing level.");
	}
	if(params.timecycle == GAME_VC){
		ImGui::Checkbox("Use Blur Ambient", &gUseBlurAmb); ImGui::SameLine();
		ImGui::Checkbox("Override", &gOverrideBlurAmb);
	}
	if(params.timecycle == GAME_SA){
		ImGui::Text("Colour filter"); ImGui::SameLine();
		ImGui::RadioButton("None##NOPOSTFX", &gColourFilter, 0); ImGui::SameLine();
		ImGui::RadioButton("PS2##PS2POSTFX", &gColourFilter, PLATFORM_PS2); ImGui::SameLine();
		ImGui::RadioButton("PC/Xbox##PCPOSTFX", &gColourFilter, PLATFORM_PC); ImGui::SameLine();
		ImGui::Checkbox("Radiosity", &gRadiosity);
	}
	if(params.timecycle == GAME_LCS || params.timecycle == GAME_VCS){
		ImGui::Text("Colour filter"); ImGui::SameLine();
		ImGui::RadioButton("None##NOPOSTFX", &gColourFilter, 0); ImGui::SameLine();
		ImGui::RadioButton("PS2##PS2POSTFX", &gColourFilter, PLATFORM_PS2); ImGui::SameLine();
		ImGui::RadioButton("PSP##PCPOSTFX", &gColourFilter, PLATFORM_PSP);
		if(params.timecycle == GAME_VCS){
			 ImGui::SameLine();
			ImGui::Checkbox("Radiosity", &gRadiosity);
		}
	}
	if(params.daynightPipe){
		ImGui::Text("Building Pipe"); ImGui::SameLine();
		ImGui::RadioButton("PS2##PS2BUILD", &gBuildingPipeSwitch, PLATFORM_PS2); ImGui::SameLine();
		ImGui::RadioButton("PC##PCBUILD", &gBuildingPipeSwitch, PLATFORM_PC); ImGui::SameLine();
		ImGui::RadioButton("Xbox##XBOXBUILD", &gBuildingPipeSwitch, PLATFORM_XBOX);
	}
	if(params.leedsPipe){
		ImGui::Text("Building Pipe"); ImGui::SameLine();
		ImGui::RadioButton("Default##NONE", &gBuildingPipeSwitch, PLATFORM_NULL); ImGui::SameLine();
		ImGui::RadioButton("PSP##PSPBUILD", &gBuildingPipeSwitch, PLATFORM_PSP); ImGui::SameLine();
		ImGui::RadioButton("PS2##PS2BUILD", &gBuildingPipeSwitch, PLATFORM_PS2); ImGui::SameLine();
		ImGui::RadioButton("Mobile##MOBILEBUILD", &gBuildingPipeSwitch, PLATFORM_PC);
	}
	ImGui::Checkbox("Backface Culling", &gDoBackfaceCulling);
	// TODO: not params
	ImGui::Checkbox("PS2 Alpha test", &params.ps2AlphaTest);
	ImGui::InputInt("Alpha Ref", &params.alphaRef, 1);
	if(params.alphaRef < 0) params.alphaRef = 0;
	if(params.alphaRef > 255) params.alphaRef = 255;

	ImGui::Checkbox("Draw Background", &gRenderBackground);
	ImGui::Checkbox("Enable Fog", &gEnableFog);
	if(params.timecycle == GAME_SA)
		ImGui::Checkbox("Enable TimeCycle boxes", &gEnableTimecycleBoxes);
}

static void
uiFilteredInstanceList(ObjectDef *obj)
{
	static char buf[256];
	CPtrNode *p;
	ObjectInst *inst;
	for(p = instances.first; p; p = p->next){
		inst = (ObjectInst*)p->item;
		if(GetObjectDef(inst->m_objectId) != obj)
			continue;
		bool pop = false;
		if(inst->m_selected){
			ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(255, 0, 0));
			pop = true;
		}
		ImGui::PushID(inst);
		sprintf(buf, "%-20s %8.2f %8.2f %8.2f", obj->m_name,
			inst->m_translation.x, inst->m_translation.y, inst->m_translation.z);
		ImGui::Selectable(buf);
		ImGui::PopID();
		if(ImGui::IsItemHovered()){
			if(ImGui::IsMouseClicked(1))
				inst->Select();
			if(ImGui::IsMouseDoubleClicked(0))
				inst->JumpTo();
		}
		if(pop)
			ImGui::PopStyleColor();
		if(ImGui::IsItemHovered())
			inst->m_highlight = HIGHLIGHT_HOVER;
	}
}

int uiNumCarPathColumns(void) { return isIII() ? 9 : isSA() ? 14 : 13; }

void
uiCarPathHeader(void)
{
	ImGui::TableSetupColumn("idx");
	ImGui::TableSetupColumn("type");
	ImGui::TableSetupColumn("link");
	ImGui::TableSetupColumn("numLinks");
	ImGui::TableSetupColumn("x");
	ImGui::TableSetupColumn("y");
	ImGui::TableSetupColumn("z");
	ImGui::TableSetupColumn("lanesIn");
	ImGui::TableSetupColumn("lanesOut");
	if(!isIII()){
		ImGui::TableSetupColumn("width");
		ImGui::TableSetupColumn("speed");
		ImGui::TableSetupColumn("flags");
		ImGui::TableSetupColumn("density");
		if(isSA())
			ImGui::TableSetupColumn("special");
	}
	ImGui::TableHeadersRow();
}

void
uiCarPathNode(PathNode *nd, int i, ObjectInst *inst)
{
	int c = 0;
	ImGui::TableSetColumnIndex(c++);
	char str[50];
	sprintf(str, "%d", i);
	if(ImGui::Selectable(str, nd == Path::selectedNode, ImGuiSelectableFlags_SpanAllColumns))
		Path::selectedNode = nd;
	if(ImGui::IsItemHovered()){
		Path::guiHoveredNode = nd;
		if(ImGui::IsMouseDoubleClicked(0))
			nd->JumpTo(inst);
	}
	ImGui::TableSetColumnIndex(c++);
	ImGui::Text(nd->type == PathNode::NodeInternal ? "intern" : "extern");
	ImGui::TableSetColumnIndex(c++);
	ImGui::Text("%d", nd->link);
	ImGui::TableSetColumnIndex(c++);
	ImGui::Text("%d", nd->numLinks);
	ImGui::TableSetColumnIndex(c++);
	ImGui::Text("%g", nd->x*16);
	ImGui::TableSetColumnIndex(c++);
	ImGui::Text("%g", nd->y*16);
	ImGui::TableSetColumnIndex(c++);
	ImGui::Text("%g", nd->z*16);
	if(nd->type == PathNode::NodeExternal){
		ImGui::TableSetColumnIndex(c++);
		ImGui::Text("%d", nd->lanesIn);
		ImGui::TableSetColumnIndex(c++);
		ImGui::Text("%d", nd->lanesOut);
	}else{
		ImGui::TableSetColumnIndex(c++);
		ImGui::Text(" ");
		ImGui::TableSetColumnIndex(c++);
		ImGui::Text(" ");
	}

	if(!isIII()){
		ImGui::TableSetColumnIndex(c++);
		ImGui::Text("%g", nd->width);
		ImGui::TableSetColumnIndex(c++);
		ImGui::Text("%d", nd->speed);
		ImGui::TableSetColumnIndex(c++);
		ImGui::Text("%X", nd->flags);
		ImGui::TableSetColumnIndex(c++);
		ImGui::Text("%g", nd->density);
		if(isSA()){
			ImGui::TableSetColumnIndex(c++);
			ImGui::Text("%d", nd->special);
		}
	}
}

void
uiPedPathHeader(void)
{
	ImGui::TableSetupColumn("idx");
	ImGui::TableSetupColumn("type");
	ImGui::TableSetupColumn("link");
	ImGui::TableSetupColumn("cross");
	ImGui::TableSetupColumn("numLinks");
	ImGui::TableSetupColumn("x");
	ImGui::TableSetupColumn("y");
	ImGui::TableSetupColumn("z");
	ImGui::TableHeadersRow();
}

void
uiPedPathNode(PathNode *nd, int i, ObjectInst *inst)
{
	int c = 0;
	ImGui::TableSetColumnIndex(c++);
	char str[50];
	sprintf(str, "%d", i);
	if(ImGui::Selectable(str, nd == Path::selectedNode, ImGuiSelectableFlags_SpanAllColumns))
		Path::selectedNode = nd;
	if(ImGui::IsItemHovered()){
		Path::guiHoveredNode = nd;
		if(ImGui::IsMouseDoubleClicked(0))
			nd->JumpTo(inst);
	}
	ImGui::TableSetColumnIndex(c++);
	ImGui::Text(nd->type == PathNode::NodeInternal ? "intern" : "extern");
	ImGui::TableSetColumnIndex(c++);
	ImGui::Text("%d", nd->link);
	ImGui::TableSetColumnIndex(c++);
	ImGui::Text("%d", nd->linkType);
	ImGui::TableSetColumnIndex(c++);
	ImGui::Text("%d", nd->numLinks);
	ImGui::TableSetColumnIndex(c++);
	ImGui::Text("%g", nd->x*16);
	ImGui::TableSetColumnIndex(c++);
	ImGui::Text("%g", nd->y*16);
	ImGui::TableSetColumnIndex(c++);
	ImGui::Text("%g", nd->z*16);
}

static void
uiPathInfo(ObjectInst *inst)
{
	if(inst){
		ObjectDef *obj;
		obj = GetObjectDef(inst->m_objectId);

		ImGui::TextDisabled("Legacy object-attached path patches");

		if(obj->m_carPathIndex >= 0){
			PathNode *nd = Path::GetCarNode(obj->m_carPathIndex,0);
			ImGui::Text(nd->water ? "Legacy Water Path" : "Legacy Car Path");
			if(ImGui::BeginTable("Nodes", uiNumCarPathColumns(), ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)){
				uiCarPathHeader();
				for(int i = 0; nd = Path::GetCarNode(obj->m_carPathIndex,i); i++){
					ImGui::TableNextRow();
					uiCarPathNode(nd, i, inst);
				}
				ImGui::EndTable();
			}
		}
		if(obj->m_pedPathIndex >= 0){
			ImGui::Text("Legacy Ped Path");
			PathNode *nd;
			if(ImGui::BeginTable("Nodes", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)){
				uiPedPathHeader();
				for(int i = 0; nd = Path::GetPedNode(obj->m_pedPathIndex,i); i++){
					ImGui::TableNextRow();
					uiPedPathNode(nd, i, inst);
				}
				ImGui::EndTable();
			}
		}
	}else if(Path::selectedNode && !Path::selectedNode->isDetached()){
		ObjectDef *obj = GetObjectDef(Path::selectedNode->objId);
		ImGui::Text("Object %s", obj->m_name);
		ImGui::TextDisabled("Legacy object-attached path patch");
		uiFilteredInstanceList(obj);
	}else if(Path::selectedNode && Path::selectedNode->tabId == 1){
		int i = Path::selectedNode->idx;
		ImGui::Text(Path::selectedNode->water ? "Legacy Water Path %d" : "Legacy Car Path %d", i);
		ImGui::PushID(i);
		if(ImGui::BeginTable("Nodes", uiNumCarPathColumns(), ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)){
			uiCarPathHeader();
			for(int j = 0; j < 12; j++){
				PathNode *nd = Path::GetDetachedCarNode(i,j);
				if(nd == nil) break;
				ImGui::TableNextRow();
				uiCarPathNode(nd, j, nil);
			}
			ImGui::EndTable();
		}
		ImGui::PopID();
	}else if(Path::selectedNode && Path::selectedNode->tabId == 3){
		int i = Path::selectedNode->idx;
		ImGui::Text("Legacy Ped Path %d", i);
		ImGui::PushID(i);
		if(ImGui::BeginTable("Nodes", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)){
			uiPedPathHeader();
			for(int j = 0; j < 12; j++){
				PathNode *nd = Path::GetDetachedPedNode(i,j);
				if(nd == nil) break;
				ImGui::TableNextRow();
				uiPedPathNode(nd, j, nil);
			}
			ImGui::EndTable();
		}
		ImGui::PopID();
	}
}

static const char *fxTypeNames[] = {
	"Light",
	"Particle",
	"LookAtPoint",
	"PedQueue",
	"SunGlare",
	"Interior",
	"EntryExit",
	"Roadsign",
	"TriggerPoint",
	"CoverPoint",
	"Escalator"
};
static const char *flareTypeNames[] = { "None", "Sun", "Headlight" };

static const char*
GetEffectTypeName(int type)
{
	if(type < 0 || type >= (int)IM_ARRAYSIZE(fxTypeNames))
		return "Unknown";
	return fxTypeNames[type];
}

void
uiOneEffect(Effect *e)
{
	ImGui::Combo("Effect Type", &e->type, fxTypeNames, IM_ARRAYSIZE(fxTypeNames));
	ImGui::DragFloat3("Position", &e->pos.x, 0.1f);

	rw::RGBAf col;
	convColor(&col, &e->col);
	if(ImGui::ColorEdit4("Color", (float*)&col))
		convColor(&e->col, &col);

	ImGui::Separator();

	switch(e->type){
	case FX_LIGHT: {
		ImGui::DragFloat("LOD dist",     &e->light.lodDist,    1.f);
		ImGui::DragFloat("Size",         &e->light.size,       0.01f);
		ImGui::DragFloat("Corona size",  &e->light.coronaSize, 0.01f);
		ImGui::DragFloat("Shadow size",  &e->light.shadowSize, 0.01f);
		ImGui::Separator();
		ImGui::DragInt("Flashiness",     &e->light.flashiness);
		ImGui::DragInt("Shadow alpha",   &e->light.shadowAlpha, 1, 0, 255);

		ImGui::Combo("Lens flare", &e->light.lensFlareType, flareTypeNames, IM_ARRAYSIZE(flareTypeNames));

		bool refl = !!e->light.reflection;
		if(ImGui::Checkbox("Reflection", &refl))
			e->light.reflection = !!refl;

		ImGui::InputInt("Flags", &e->light.flags, 1, 1, ImGuiInputTextFlags_CharsHexadecimal);
		ImGui::Separator();
		ImGui::InputText("Corona tex", e->light.coronaTex, 32);
		ImGui::InputText("Shadow tex", e->light.shadowTex, 32);
		} break;

	case FX_PARTICLE:
		ImGui::DragInt   ("Particle type", &e->prtcl.particleType);
		ImGui::DragFloat3("Direction",     &e->prtcl.dir.x, 0.01f);
		ImGui::DragFloat ("Size",          &e->prtcl.size,  0.01f);
		break;

	case FX_LOOKATPOINT:
		ImGui::DragFloat3("Direction",   &e->look.dir.x, 0.01f);
		ImGui::DragInt   ("Type",        &e->look.type);
		ImGui::DragInt   ("Probability", &e->look.probability, 1, 0, 100);
		break;

	case FX_PEDQUEUE:
		ImGui::DragFloat3("Queue dir", &e->queue.queueDir.x, 0.01f);
		ImGui::DragFloat3("Use dir",   &e->queue.useDir.x,   0.01f);
		ImGui::DragFloat3("Forward dir", &e->queue.forwardDir.x, 0.01f);
		ImGui::DragInt   ("Type",      &e->queue.type);
		break;

	case FX_INTERIOR:
		ImGui::TextDisabled("Render-only preview");
		ImGui::Text("Type: %d", e->interior.type);
		ImGui::Text("Group: %d", e->interior.group);
		ImGui::Text("Size: %.1f x %.1f x %.1f", e->interior.width, e->interior.depth, e->interior.height);
		break;

	case FX_ENTRYEXIT:
		ImGui::TextDisabled("Render-only preview");
		ImGui::Text("Area: %d", e->entryExit.areaCode);
		ImGui::Text("Radius: %.2f x %.2f", e->entryExit.radiusX, e->entryExit.radiusY);
		ImGui::Text("Title: %.8s", e->entryExit.title);
		break;

	case FX_ROADSIGN:
		ImGui::TextDisabled("Render-only preview");
		ImGui::Text("Size: %.2f x %.2f", e->roadsign.width, e->roadsign.height);
		ImGui::Text("Line 1: %.16s", e->roadsign.text[0]);
		ImGui::Text("Line 2: %.16s", e->roadsign.text[1]);
		ImGui::Text("Line 3: %.16s", e->roadsign.text[2]);
		ImGui::Text("Line 4: %.16s", e->roadsign.text[3]);
		break;

	case FX_TRIGGERPOINT:
		ImGui::TextDisabled("Render-only preview");
		ImGui::Text("Index: %d", e->triggerPoint.index);
		break;

	case FX_COVERPOINT:
		ImGui::TextDisabled("Render-only preview");
		ImGui::Text("Direction: %.2f %.2f", e->coverPoint.dirX, e->coverPoint.dirY);
		ImGui::Text("Usage: %d", e->coverPoint.usage);
		break;

	case FX_ESCALATOR:
		ImGui::TextDisabled("Render-only preview");
		ImGui::Text("Direction: %s", e->escalator.goingUp ? "Up" : "Down");
		break;
	}
}

static void
uiFxTable(ObjectInst *inst)
{
	if(inst == nil)
		return;

	ObjectDef *obj;
	obj = GetObjectDef(inst->m_objectId);

	ImGui::Text("Effects (%d)", obj->m_numEffects);
	ImGui::Separator();

	ImGui::BeginChild("##effect_list", ImVec2(0, 0), false);

	for(int i = 0; i < obj->m_numEffects; i++) {
		Effect *e = Effects::GetEffect(obj->m_effectIndex+i);
		ImGui::PushID(i);
		ImGui::ColorButton("##col", mkColor(e->col),
				ImGuiColorEditFlags_NoTooltip |
				ImGuiColorEditFlags_NoBorder,
				ImVec2(12, 12));
		ImGui::SameLine();

		ImGui::TextDisabled("%2d", i);
		ImGui::SameLine();

		char label[64];
		snprintf(label, sizeof(label), "%s##eff%d", GetEffectTypeName(e->type), i);

		if(ImGui::Selectable(label, e == Effects::selectedEffect, ImGuiSelectableFlags_None, ImVec2(0, 0)))
			Effects::selectedEffect = e;
		if(ImGui::IsItemHovered()){
			Effects::guiHoveredEffect = e;
			if(ImGui::IsMouseClicked(1))
				Effects::selectedEffect = e;
			if(ImGui::IsMouseDoubleClicked(0))
				e->JumpTo(inst);
		}
		ImGui::PopID();
	}
	ImGui::EndChild();
}

static void
uiFxInfo(ObjectInst *inst)
{
	float listWidth = 200.f;
	ImGui::BeginChild("##left", ImVec2(listWidth, 0), true);
	uiFxTable(inst);
	ImGui::EndChild();

	ImGui::SameLine();

	ImGui::BeginChild("##right", ImVec2(0, 0), true);
	if(Effects::selectedEffect)
		uiOneEffect(Effects::selectedEffect);
	else
		ImGui::TextDisabled("Select an effect");
	ImGui::EndChild();
}


static void
uiInstInfo(ObjectInst *inst)
{
	ObjectDef *obj;
	obj = GetObjectDef(inst->m_objectId);

	InputTextReadonly<MODELNAMELEN>("Model##Inst", obj->m_name);
	
	if(ImGui::BeginCombo("IPL", inst->m_file ? inst->m_file->name : "None")){
		GameFile *uniqueFiles[512];
		int numUnique = 0;
		for(CPtrNode *p = instances.first; p && numUnique < 512; p = p->next){
			ObjectInst *o = (ObjectInst*)p->item;
			if(o && o->m_file && pathHasExtensionCi(o->m_file->name, ".ipl")){
				bool found = false;
				for(int i = 0; i < numUnique; i++){
					if(uniqueFiles[i] == o->m_file){
						found = true;
						break;
					}
				}
				if(!found)
					uniqueFiles[numUnique++] = o->m_file;
			}
		}
		for(int i = 0; i < numUnique; i++){
			if(ImGui::Selectable(uniqueFiles[i]->name, inst->m_file == uniqueFiles[i])){
				inst->m_file = uniqueFiles[i];
				inst->m_imageIndex = -1;
				inst->m_isDirty = true;
				StampChangeSeq(inst);
				
				// Automatically change the object's IDE to match the new IPL
				if(inst->m_file){
					GameFile *ideFile = FindMatchingIdeForIpl(inst->m_file);
					if(ideFile)
						obj->m_file = ideFile;
				}
			}
		}
		ImGui::EndCombo();
	}

	if(inst->m_isDeleted){
		ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(255, 80, 80));
		ImGui::Text("DELETED");
		ImGui::PopStyleColor();
		ImGui::SameLine();
		if(ImGui::Button("Undelete"))
			inst->Undelete();
	}else{
		if(ImGui::Button("Delete")){
			inst->Delete();
		}
		if(inst->m_lod){
			ImGui::SameLine();
			ImGui::TextDisabled("(LOD: %s)", GetObjectDef(inst->m_lod->m_objectId)->m_name);
		}
	}

	bool changed = false;
	changed |= ImGui::InputFloat3(_("Translation"), (float*)&inst->m_translation);

	// Rotation as Euler angles in degrees.
	// Cache prevents gimbal drift: we only decompose quat→euler when the instance changes.
	{
		const float PI = 3.14159265f;
		static ObjectInst *eulerCacheInst = nil;
		static float eulerCache[3] = {}; // Roll X, Pitch Y, Yaw Z

		if(inst != eulerCacheInst){
			eulerCacheInst = inst;
			float qx = inst->m_rotation.x, qy = inst->m_rotation.y;
			float qz = inst->m_rotation.z, qw = inst->m_rotation.w;
			float sinp = 2.0f*(qw*qy - qz*qx);
			if(sinp >  1.0f) sinp =  1.0f;
			if(sinp < -1.0f) sinp = -1.0f;
			eulerCache[0] = atan2f(2.0f*(qw*qx + qy*qz), 1.0f - 2.0f*(qx*qx + qy*qy)) * (180.0f/PI);
			eulerCache[1] = asinf(sinp) * (180.0f/PI);
			eulerCache[2] = atan2f(2.0f*(qw*qz + qx*qy), 1.0f - 2.0f*(qy*qy + qz*qz)) * (180.0f/PI);
		}

		if(ImGui::InputFloat3(_("Rotation (deg)"), eulerCache, "%.1f")){
			for(int i = 0; i < 3; i++){
				while(eulerCache[i] >  180.0f) eulerCache[i] -= 360.0f;
				while(eulerCache[i] < -180.0f) eulerCache[i] += 360.0f;
			}
			float rx = eulerCache[0]*(PI/180.0f)*0.5f;
			float ry = eulerCache[1]*(PI/180.0f)*0.5f;
			float rz = eulerCache[2]*(PI/180.0f)*0.5f;
			float cx = cosf(rx), sx = sinf(rx);
			float cy = cosf(ry), sy = sinf(ry);
			float cz = cosf(rz), sz = sinf(rz);
			inst->m_rotation.x = sx*cy*cz + cx*sy*sz;
			inst->m_rotation.y = cx*sy*cz - sx*cy*sz;
			inst->m_rotation.z = cx*cy*sz + sx*sy*cz;
			inst->m_rotation.w = cx*cy*cz - sx*sy*sz;
			changed = true;
		}
	}

	if(changed){
		StampChangeSeq(inst);
		inst->m_isDirty = true;
		inst->UpdateMatrix();
		if(inst->m_rwObject){
			rw::Frame *f;
			if(obj->m_type == ObjectDef::ATOMIC)
				f = ((rw::Atomic*)inst->m_rwObject)->getFrame();
			else
				f = ((rw::Clump*)inst->m_rwObject)->getFrame();
			f->transform(&inst->m_matrix, rw::COMBINEREPLACE);
		}
	}

	ImGui::InputInt(_("Interior"), &inst->m_area);
	if(inst->m_area < 0) inst->m_area = 0;

	if(params.objFlagset == GAME_SA){
		ImGui::Checkbox(_("Unimportant"), &inst->m_isUnimportant);
		ImGui::Checkbox(_("Underwater"), &inst->m_isUnderWater);
		ImGui::Checkbox(_("Tunnel"), &inst->m_isTunnel);
		ImGui::Checkbox(_("Tunnel Transition"), &inst->m_isTunnelTransition);
	}
}

static void
uiObjInfo(ObjectDef *obj)
{
	int i;
	TxdDef *txd;

	txd = GetTxdDef(obj->m_txdSlot);

	ImGui::Text("ID: %d\n", obj->m_id);
	InputTextReadonly<MODELNAMELEN>(_("Model"), obj->m_name);
	InputTextReadonly<MODELNAMELEN>("TXD", txd ? txd->name : "");

	InputTextReadonly<1024>("IDE", obj->m_file ? obj->m_file->name : "");
	if(obj->m_colModel && !obj->m_gotChildCol)
		InputTextReadonly<1024>("COL", obj->m_colModel->file ? obj->m_colModel->file->name : "");

	ImGui::Text("Draw dist:");
	for(i = 0; i < obj->m_numAtomics; i++){
		ImGui::SameLine();
		ImGui::Text("%.0f", obj->m_drawDist[i]);
	}
	ImGui::Text("Min Draw dist: %.0f", obj->m_minDrawDist);

	if(obj->m_isTimed){
		ImGui::Text("Time: %d %d (visible now: %s)",
			obj->m_timeOn, obj->m_timeOff,
			IsHourInRange(obj->m_timeOn, obj->m_timeOff) ? "yes" : "no");
	}

	if(obj->m_relatedModel)
		ImGui::Text("Related: %s\n", obj->m_relatedModel->m_name);
	if(obj->m_relatedTimeModel)
		ImGui::Text("Related timed: %s\n", obj->m_relatedTimeModel->m_name);

	uiObjectFlagsEditor(obj);

}

struct CamSetting {
	char name[256];
	rw::V3d pos;
	rw::V3d target;
	float fov;

	int hour, minute;
	int weather1, weather2;
	int extracolors;

	int area;
};

std::vector<CamSetting> camSettings;

static void
loadCamSettings(void)
{
	CamSetting cam;
	char line[256], *p, *pp;
	FILE *f;

	f = fopenArianeDataRead("camsettings.txt", "camsettings.txt");
	if(f == nil)
		return;
	camSettings.clear();
	while(fgets(line, sizeof(line), f)){
		p = line;
		while(*p && isspace(*p)) p++;
		if(*p != '"')
			continue;
		pp = ++p;
		while(*p && *p != '"') p++;
		if(*p != '"')
			continue;
		*p++ = '\0';
		strncpy(cam.name, pp, sizeof(cam.name));
		sscanf(p, "%f %f %f  %f %f %f  %f  %d %d %d %d  %d",
			&cam.pos.x, &cam.pos.y, &cam.pos.z,
			&cam.target.x, &cam.target.y, &cam.target.z,
			&cam.fov,
			&cam.hour, &cam.minute, &cam.weather1, &cam.weather2,
			&cam.area);
		if(cam.fov < 1.0f || cam.fov > 150.0f)
			cam.fov = 70.0f;
		if(cam.area < 0)
			cam.area = 0;
		cam.hour %= 24;
		cam.minute %= 60;
		cam.weather1 %= params.numWeathers;
		cam.weather2 %= params.numWeathers;
		camSettings.push_back(cam);
	}

	fclose(f);
}

static void
loadSaveSettings(void)
{
	FILE *f;
	char line[1024];
	char key[128];
	const char *value;
	int intValue;
	bool boolValue;
	rw::V3d vecValue;
	int savedSpawnObjectId = -1;

	sanitizeAutomaticBackupSettings();
	sanitizeCustomImportSettings();
	gAutomaticBackupLastSeenSeq = GetLatestChangeSeq();
	gAutomaticBackupLastHandledSeq = gAutomaticBackupLastSeenSeq;
	gAutomaticBackupLastSnapshot[0] = '\0';
	gRenderMode = gRenderOnlyHD ? 1 : gRenderOnlyLod ? 2 : 0;
	gSavedIplVisibilityStates.clear();
	gBrowserTabRestorePending = true;
	loadWindowStateFromSettingsFile();
	gPersistentSettingsLoaded = true;

	f = fopenArianeDataRead("savesettings.txt", "savesettings.txt");
	if(f == nil)
		return;

	while(fgets(line, sizeof(line), f)){
		if(!splitSettingLine(line, key, sizeof(key), &value))
			continue;
		if(strcmp(key, "save_destination") == 0){
			if(parseIntSetting(value, &intValue) && intValue == SAVE_DESTINATION_MODLOADER)
				gSaveDestination = SAVE_DESTINATION_MODLOADER;
			else
				gSaveDestination = SAVE_DESTINATION_ORIGINAL_FILES;
		}else if(strcmp(key, "save_to_original_ipl") == 0){
			if(parseBoolSetting(value, &boolValue))
				gSaveToOriginalIpl = boolValue;
		}else if(strcmp(key, "automatic_backups") == 0){
			if(parseBoolSetting(value, &boolValue))
				gAutomaticBackupsEnabled = boolValue;
		}else if(strcmp(key, "automatic_backup_interval") == 0){
			parseIntSetting(value, &gAutomaticBackupIntervalSeconds);
		}else if(strcmp(key, "automatic_backup_keep") == 0){
			parseIntSetting(value, &gAutomaticBackupKeepCount);
		}else if(strcmp(key, "custom_import_start_id") == 0){
			parseIntSetting(value, &gCustomImportPreferredStartId);
		}else if(strcmp(key, "show_demo_window") == 0){
			if(parseBoolSetting(value, &boolValue)) showDemoWindow = boolValue;
		}else if(strcmp(key, "show_editor_window") == 0){
			if(parseBoolSetting(value, &boolValue)) showEditorWindow = boolValue;
		}else if(strcmp(key, "show_instance_window") == 0){
			if(parseBoolSetting(value, &boolValue)) showInstanceWindow = boolValue;
		}else if(strcmp(key, "show_log_window") == 0){
			if(parseBoolSetting(value, &boolValue)) showLogWindow = boolValue;
		}else if(strcmp(key, "show_help_window") == 0){
			if(parseBoolSetting(value, &boolValue)) showHelpWindow = boolValue;
		}else if(strcmp(key, "show_time_weather_window") == 0){
			if(parseBoolSetting(value, &boolValue)) showTimeWeatherWindow = boolValue;
		}else if(strcmp(key, "show_view_window") == 0){
			if(parseBoolSetting(value, &boolValue)) showViewWindow = boolValue;
		}else if(strcmp(key, "show_rendering_window") == 0){
			if(parseBoolSetting(value, &boolValue)) showRenderingWindow = boolValue;
		}else if(strcmp(key, "show_browser_window") == 0){
			if(parseBoolSetting(value, &boolValue)) showBrowserWindow = boolValue;
		}else if(strcmp(key, "show_diff_window") == 0){
			if(parseBoolSetting(value, &boolValue)) showDiffWindow = boolValue;
		}else if(strcmp(key, "show_tools_window") == 0){
			if(parseBoolSetting(value, &boolValue)) showToolsWindow = boolValue;
		}else if(strcmp(key, "toast_enabled") == 0){
			if(parseBoolSetting(value, &boolValue)) toastEnabled = boolValue;
		}else if(strncmp(key, "toast_category_", 15) == 0){
			int idx = atoi(key + 15);
			if(idx >= 0 && idx < TOAST_NUM_CATEGORIES && parseBoolSetting(value, &boolValue))
				toastCategoryEnabled[idx] = boolValue;
		}else if(strcmp(key, "time_hour") == 0){
			parseIntSetting(value, &currentHour);
		}else if(strcmp(key, "time_minute") == 0){
			parseIntSetting(value, &currentMinute);
		}else if(strcmp(key, "current_area") == 0){
			parseIntSetting(value, &currentArea);
		}else if(strcmp(key, "weather_old") == 0){
			parseIntSetting(value, &Weather::oldWeather);
		}else if(strcmp(key, "weather_new") == 0){
			parseIntSetting(value, &Weather::newWeather);
		}else if(strcmp(key, "weather_interpolation") == 0){
			parseFloatSetting(value, &Weather::interpolation);
		}else if(strcmp(key, "extra_colours") == 0){
			parseIntSetting(value, &extraColours);
		}else if(strcmp(key, "day_night_balance") == 0){
			parseFloatSetting(value, &gDayNightBalance);
		}else if(strcmp(key, "wet_road_effect") == 0){
			parseFloatSetting(value, &gWetRoadEffect);
		}else if(strcmp(key, "neo_light_map_strength") == 0){
			parseFloatSetting(value, &gNeoLightMapStrength);
		}else if(strcmp(key, "camera_position") == 0){
			if(parseVec3Setting(value, &vecValue))
				TheCamera.m_position = vecValue;
		}else if(strcmp(key, "camera_target") == 0){
			if(parseVec3Setting(value, &vecValue))
				TheCamera.m_target = vecValue;
		}else if(strcmp(key, "camera_fov") == 0){
			parseFloatSetting(value, &TheCamera.m_fov);
		}else if(strcmp(key, "draw_target") == 0){
			if(parseBoolSetting(value, &boolValue)) gDrawTarget = boolValue;
		}else if(strcmp(key, "render_collision") == 0){
			if(parseBoolSetting(value, &boolValue)) gRenderCollision = boolValue;
		}else if(strcmp(key, "render_zones") == 0){
			if(parseBoolSetting(value, &boolValue)) gRenderZones = boolValue;
		}else if(strcmp(key, "render_map_zones") == 0){
			if(parseBoolSetting(value, &boolValue)) gRenderMapZones = boolValue;
		}else if(strcmp(key, "render_navig_zones") == 0){
			if(parseBoolSetting(value, &boolValue)) gRenderNavigZones = boolValue;
		}else if(strcmp(key, "render_info_zones") == 0){
			if(parseBoolSetting(value, &boolValue)) gRenderInfoZones = boolValue;
		}else if(strcmp(key, "render_cull_zones") == 0){
			if(parseBoolSetting(value, &boolValue)) gRenderCullZones = boolValue;
		}else if(strcmp(key, "render_attrib_zones") == 0){
			if(parseBoolSetting(value, &boolValue)) gRenderAttribZones = boolValue;
		}else if(strcmp(key, "render_light_effects") == 0){
			if(parseBoolSetting(value, &boolValue)) gRenderLightEffects = boolValue;
		}else if(strcmp(key, "render_effect_markers") == 0){
			if(parseBoolSetting(value, &boolValue)) gRenderEffects = boolValue;
		}else if(strcmp(key, "render_legacy_ped_paths") == 0){
			if(parseBoolSetting(value, &boolValue)) gRenderLegacyPedPaths = boolValue;
		}else if(strcmp(key, "render_legacy_car_paths") == 0){
			if(parseBoolSetting(value, &boolValue)) gRenderLegacyCarPaths = boolValue;
		}else if(strcmp(key, "render_sa_ped_paths") == 0){
			if(parseBoolSetting(value, &boolValue)) gRenderSaPedPaths = boolValue;
		}else if(strcmp(key, "render_sa_ped_path_walkers") == 0){
			if(parseBoolSetting(value, &boolValue)) gRenderSaPedPathWalkers = boolValue;
		}else if(strcmp(key, "render_sa_car_paths") == 0){
			if(parseBoolSetting(value, &boolValue)) gRenderSaCarPaths = boolValue;
		}else if(strcmp(key, "render_sa_car_path_traffic") == 0){
			if(parseBoolSetting(value, &boolValue)) gRenderSaCarPathTraffic = boolValue;
		}else if(strcmp(key, "render_sa_area_grid") == 0){
			if(parseBoolSetting(value, &boolValue)) gRenderSaAreaGrid = boolValue;
		}else if(strcmp(key, "sa_ped_path_walker_count") == 0){
			parseIntSetting(value, &gSaPedPathWalkerCount);
		}else if(strcmp(key, "sa_car_path_traffic_count") == 0){
			parseIntSetting(value, &gSaCarPathTrafficCount);
		}else if(strcmp(key, "sa_car_path_traffic_speed_scale") == 0){
			parseFloatSetting(value, &gSaCarPathTrafficSpeedScale);
		}else if(strcmp(key, "sa_car_path_traffic_freeze_routes") == 0){
			if(parseBoolSetting(value, &boolValue)) gSaCarPathTrafficFreezeRoutes = boolValue;
		}else if(strcmp(key, "render_sa_car_path_parked_cars") == 0){
			if(parseBoolSetting(value, &boolValue)) gRenderSaCarPathParkedCars = boolValue;
		}else if(strcmp(key, "sa_car_path_parked_car_count") == 0){
			parseIntSetting(value, &gSaCarPathParkedCarCount);
		}else if(strcmp(key, "render_water") == 0){
			if(parseBoolSetting(value, &boolValue)) gRenderWater = boolValue;
		}else if(strcmp(key, "play_animations") == 0){
			if(parseBoolSetting(value, &boolValue)) gPlayAnimations = boolValue;
		}else if(strcmp(key, "render_mode") == 0){
			parseIntSetting(value, &gRenderMode);
		}else if(strcmp(key, "draw_distance") == 0){
			parseFloatSetting(value, &TheCamera.m_LODmult);
		}else if(strcmp(key, "fly_fast_mul") == 0){
			parseFloatSetting(value, &gFlyFastMul);
		}else if(strcmp(key, "fly_slow_mul") == 0){
			parseFloatSetting(value, &gFlySlowMul);
		}else if(strcmp(key, "fov_wheel_step") == 0){
			parseFloatSetting(value, &gFovWheelStep);
		}else if(strcmp(key, "render_all_timed_objects") == 0){
			if(parseBoolSetting(value, &boolValue)) gNoTimeCull = boolValue;
		}else if(strcmp(key, "render_all_areas") == 0){
			if(parseBoolSetting(value, &boolValue)) gNoAreaCull = boolValue;
		}else if(strcmp(key, "ipl_filter_search") == 0){
			parseQuotedStringValue(value, gIplFilterSearch, sizeof(gIplFilterSearch));
		}else if(strcmp(key, "ipl_visible") == 0){
			SavedIplVisibilityState state;
			const char *after = nil;
			memset(&state, 0, sizeof(state));
			if(parseQuotedStringValue(value, state.key, sizeof(state.key), &after) &&
			   parseBoolSetting(after, &state.visible))
				gSavedIplVisibilityStates.push_back(state);
		}else if(strcmp(key, "render_postfx") == 0){
			if(parseBoolSetting(value, &boolValue)) gRenderPostFX = boolValue;
		}else if(strcmp(key, "aa_samples") == 0){
			if(parseIntSetting(value, &intValue))
				gRequestedAASamples = sanitizeAASamples((uint32)max(intValue, 1));
		}else if(strcmp(key, "use_blur_ambient") == 0){
			if(parseBoolSetting(value, &boolValue)) gUseBlurAmb = boolValue;
		}else if(strcmp(key, "override_blur_ambient") == 0){
			if(parseBoolSetting(value, &boolValue)) gOverrideBlurAmb = boolValue;
		}else if(strcmp(key, "colour_filter") == 0){
			parseIntSetting(value, &gColourFilter);
		}else if(strcmp(key, "radiosity") == 0){
			if(parseBoolSetting(value, &boolValue)) gRadiosity = boolValue;
		}else if(strcmp(key, "building_pipe") == 0){
			parseIntSetting(value, &gBuildingPipeSwitch);
		}else if(strcmp(key, "backface_culling") == 0){
			if(parseBoolSetting(value, &boolValue)) gDoBackfaceCulling = boolValue;
		}else if(strcmp(key, "ps2_alpha_test") == 0){
			if(parseBoolSetting(value, &boolValue)) params.ps2AlphaTest = boolValue;
		}else if(strcmp(key, "alpha_ref") == 0){
			parseIntSetting(value, &params.alphaRef);
		}else if(strcmp(key, "render_background") == 0){
			if(parseBoolSetting(value, &boolValue)) gRenderBackground = boolValue;
		}else if(strcmp(key, "enable_fog") == 0){
			if(parseBoolSetting(value, &boolValue)) gEnableFog = boolValue;
		}else if(strcmp(key, "enable_timecycle_boxes") == 0){
			if(parseBoolSetting(value, &boolValue)) gEnableTimecycleBoxes = boolValue;
		}else if(strcmp(key, "gizmo_enabled") == 0){
			if(parseBoolSetting(value, &boolValue)) gGizmoEnabled = boolValue;
		}else if(strcmp(key, "gizmo_mode") == 0){
			parseIntSetting(value, &gGizmoMode);
		}else if(strcmp(key, "gizmo_snap") == 0){
			if(parseBoolSetting(value, &boolValue)) gGizmoSnap = boolValue;
		}else if(strcmp(key, "gizmo_snap_angle") == 0){
			parseFloatSetting(value, &gGizmoSnapAngle);
		}else if(strcmp(key, "gizmo_snap_translate") == 0){
			parseFloatSetting(value, &gGizmoSnapTranslate);
		}else if(strcmp(key, "place_snap_to_objects") == 0){
			if(parseBoolSetting(value, &boolValue)) gPlaceSnapToObjects = boolValue;
		}else if(strcmp(key, "place_snap_to_ground") == 0){
			if(parseBoolSetting(value, &boolValue)) gPlaceSnapToGround = boolValue;
		}else if(strcmp(key, "drag_follow_ground") == 0){
			if(parseBoolSetting(value, &boolValue)) gDragFollowGround = boolValue;
		}else if(strcmp(key, "drag_align_to_surface") == 0){
			if(parseBoolSetting(value, &boolValue)) gDragAlignToSurface = boolValue;
		}else if(strcmp(key, "brush_z_offset") == 0){
			parseFloatSetting(value, &gBrushZOffset);
		}else if(strcmp(key, "brush_align_to_surface") == 0){
			if(parseBoolSetting(value, &boolValue)) gBrushAlignToSurface = boolValue;
		}else if(strcmp(key, "brush_random_yaw") == 0){
			// Legacy setting (pre-range): map to 0/360 range when enabled.
			if(parseBoolSetting(value, &boolValue) && boolValue){
				gBrushYawMin = 0.0f;
				gBrushYawMax = 360.0f;
			}
		}else if(strcmp(key, "brush_yaw_min") == 0){
			parseFloatSetting(value, &gBrushYawMin);
		}else if(strcmp(key, "brush_yaw_max") == 0){
			parseFloatSetting(value, &gBrushYawMax);
		}else if(strcmp(key, "brush_spacing") == 0){
			parseFloatSetting(value, &gBrushSpacing);
		}else if(strcmp(key, "brush_radius") == 0){
			parseFloatSetting(value, &gBrushRadius);
		}else if(strcmp(key, "brush_count") == 0){
			parseIntSetting(value, &gBrushCount);
		}else if(strcmp(key, "brush_delay_ms") == 0){
			parseFloatSetting(value, &gBrushDelayMs);
		}else if(strcmp(key, "editor_camera_name") == 0){
			parseQuotedStringValue(value, gEditorCameraName, sizeof(gEditorCameraName));
		}else if(strcmp(key, "editor_model_filter") == 0){
			char buf[256];
			if(parseQuotedStringValue(value, buf, sizeof(buf)))
				setTextFilterValue(gEditorModelFilter, buf);
		}else if(strcmp(key, "editor_txd_filter") == 0){
			char buf[256];
			if(parseQuotedStringValue(value, buf, sizeof(buf)))
				setTextFilterValue(gEditorTxdFilter, buf);
		}else if(strcmp(key, "editor_highlight_matches") == 0){
			if(parseBoolSetting(value, &boolValue)) gEditorHighlightMatches = boolValue;
		}else if(strcmp(key, "browser_selected_category") == 0){
			parseIntSetting(value, &gBrowserSelectedCategory);
		}else if(strcmp(key, "browser_selected_ide") == 0){
			parseQuotedStringValue(value, gBrowserSelectedIde, sizeof(gBrowserSelectedIde));
		}else if(strcmp(key, "browser_active_tab") == 0){
			parseIntSetting(value, &gBrowserActiveTab);
		}else if(strcmp(key, "browser_category_filter") == 0){
			char buf[256];
			if(parseQuotedStringValue(value, buf, sizeof(buf)))
				setTextFilterValue(gBrowserCategoryFilter, buf);
		}else if(strcmp(key, "browser_ide_filter") == 0){
			char buf[256];
			if(parseQuotedStringValue(value, buf, sizeof(buf)))
				setTextFilterValue(gBrowserIdeFilter, buf);
		}else if(strcmp(key, "browser_search_filter") == 0){
			char buf[256];
			if(parseQuotedStringValue(value, buf, sizeof(buf)))
				setTextFilterValue(gBrowserSearchFilter, buf);
		}else if(strcmp(key, "browser_favourites_filter") == 0){
			char buf[256];
			if(parseQuotedStringValue(value, buf, sizeof(buf)))
				setTextFilterValue(gBrowserFavFilter, buf);
		}else if(strcmp(key, "browser_selected_object") == 0){
			parseIntSetting(value, &savedSpawnObjectId);
		}else if(strcmp(key, "diff_filter") == 0){
			parseIntSetting(value, &gDiffFilter);
		}else if(strcmp(key, "water_snap_enabled") == 0){
			if(parseBoolSetting(value, &boolValue)) WaterLevel::gWaterSnapEnabled = boolValue;
		}else if(strcmp(key, "water_snap_size") == 0){
			parseFloatSetting(value, &WaterLevel::gWaterSnapSize);
		}else if(strcmp(key, "water_sub_mode") == 0){
			parseIntSetting(value, &WaterLevel::gWaterSubMode);
		}else if(strcmp(key, "water_create_shape") == 0){
			parseIntSetting(value, &WaterLevel::gWaterCreateShape);
		}else if(strcmp(key, "water_create_z") == 0){
			parseFloatSetting(value, &WaterLevel::gWaterCreateZ);
		}
	}

	sanitizeAutomaticBackupSettings();
	sanitizeCustomImportSettings();
	normalizePersistentSettings();
	gRequestedAASamples = sanitizeAASamples(gRequestedAASamples,
		rw::Engine::getMaxMultiSamplingLevels());
	// Note: AA configuration removed from librw skeleton API

	RefreshIplVisibilityEntries();
	for(size_t i = 0; i < gSavedIplVisibilityStates.size(); i++){
		for(int j = 0; j < GetIplVisibilityEntryCount(); j++){
			if(strcmp(GetIplVisibilityEntryName(j), gSavedIplVisibilityStates[i].key) == 0){
				SetIplVisibilityEntryVisible(j, gSavedIplVisibilityStates[i].visible);
				break;
			}
		}
	}
	if(savedSpawnObjectId >= 0 && savedSpawnObjectId < NUMOBJECTDEFS && GetObjectDef(savedSpawnObjectId))
		SetSpawnObjectId(savedSpawnObjectId);
	fclose(f);
}

static void
saveCamSettings(void)
{
	FILE *f;

	f = fopenArianeDataWrite("camsettings.txt");
	if(f == nil)
		return;

	for(int i = 0; i < camSettings.size(); i++){
		CamSetting *cam = &camSettings[i];
		fprintf(f, "\"%s\" %f %f %f  %f %f %f  %f  %d %d %d %d  %d\n",
			cam->name,
			cam->pos.x, cam->pos.y, cam->pos.z,
			cam->target.x, cam->target.y, cam->target.z,
			cam->fov,
			cam->hour, cam->minute, cam->weather1, cam->weather2,
			cam->area);
	}

	fclose(f);
}

static void
saveSaveSettings(void)
{
	FILE *f;

	sanitizeAutomaticBackupSettings();
	sanitizeCustomImportSettings();
	UpdateEditorWindowState();
	normalizePersistentSettings();
	f = fopenArianeDataWrite("savesettings.txt");
	if(f == nil)
		return;

	fprintf(f, "window_width %d\n", gSavedWindowWidth);
	fprintf(f, "window_height %d\n", gSavedWindowHeight);
	if(gSavedWindowPlacementValid){
		fprintf(f, "window_x %d\n", gSavedWindowX);
		fprintf(f, "window_y %d\n", gSavedWindowY);
	}
	fprintf(f, "window_maximized %d\n", gSavedWindowMaximized ? 1 : 0);
	fprintf(f, "save_destination %d\n", (int)gSaveDestination);
	fprintf(f, "save_to_original_ipl %d\n", gSaveToOriginalIpl ? 1 : 0);
	fprintf(f, "automatic_backups %d\n", gAutomaticBackupsEnabled ? 1 : 0);
	fprintf(f, "automatic_backup_interval %d\n", gAutomaticBackupIntervalSeconds);
	fprintf(f, "automatic_backup_keep %d\n", gAutomaticBackupKeepCount);
	fprintf(f, "custom_import_start_id %d\n", gCustomImportPreferredStartId);
	fprintf(f, "show_demo_window %d\n", showDemoWindow ? 1 : 0);
	fprintf(f, "show_editor_window %d\n", showEditorWindow ? 1 : 0);
	fprintf(f, "show_instance_window %d\n", showInstanceWindow ? 1 : 0);
	fprintf(f, "show_log_window %d\n", showLogWindow ? 1 : 0);
	fprintf(f, "show_help_window %d\n", showHelpWindow ? 1 : 0);
	fprintf(f, "show_time_weather_window %d\n", showTimeWeatherWindow ? 1 : 0);
	fprintf(f, "show_view_window %d\n", showViewWindow ? 1 : 0);
	fprintf(f, "show_rendering_window %d\n", showRenderingWindow ? 1 : 0);
	fprintf(f, "show_browser_window %d\n", showBrowserWindow ? 1 : 0);
	fprintf(f, "show_diff_window %d\n", showDiffWindow ? 1 : 0);
	fprintf(f, "show_tools_window %d\n", showToolsWindow ? 1 : 0);
	fprintf(f, "toast_enabled %d\n", toastEnabled ? 1 : 0);
	for(int i = 0; i < TOAST_NUM_CATEGORIES; i++)
		fprintf(f, "toast_category_%d %d\n", i, toastCategoryEnabled[i] ? 1 : 0);
	fprintf(f, "time_hour %d\n", currentHour);
	fprintf(f, "time_minute %d\n", currentMinute);
	fprintf(f, "current_area %d\n", currentArea);
	fprintf(f, "weather_old %d\n", Weather::oldWeather);
	fprintf(f, "weather_new %d\n", Weather::newWeather);
	fprintf(f, "weather_interpolation %.9g\n", Weather::interpolation);
	fprintf(f, "extra_colours %d\n", extraColours);
	fprintf(f, "day_night_balance %.9g\n", gDayNightBalance);
	fprintf(f, "wet_road_effect %.9g\n", gWetRoadEffect);
	fprintf(f, "neo_light_map_strength %.9g\n", gNeoLightMapStrength);
	fprintf(f, "camera_position %.9g %.9g %.9g\n", TheCamera.m_position.x, TheCamera.m_position.y, TheCamera.m_position.z);
	fprintf(f, "camera_target %.9g %.9g %.9g\n", TheCamera.m_target.x, TheCamera.m_target.y, TheCamera.m_target.z);
	fprintf(f, "camera_fov %.9g\n", TheCamera.m_fov);
	fprintf(f, "draw_target %d\n", gDrawTarget ? 1 : 0);
	fprintf(f, "render_collision %d\n", gRenderCollision ? 1 : 0);
	fprintf(f, "render_zones %d\n", gRenderZones ? 1 : 0);
	fprintf(f, "render_map_zones %d\n", gRenderMapZones ? 1 : 0);
	fprintf(f, "render_navig_zones %d\n", gRenderNavigZones ? 1 : 0);
	fprintf(f, "render_info_zones %d\n", gRenderInfoZones ? 1 : 0);
	fprintf(f, "render_cull_zones %d\n", gRenderCullZones ? 1 : 0);
	fprintf(f, "render_attrib_zones %d\n", gRenderAttribZones ? 1 : 0);
	fprintf(f, "render_light_effects %d\n", gRenderLightEffects ? 1 : 0);
	fprintf(f, "render_effect_markers %d\n", gRenderEffects ? 1 : 0);
	fprintf(f, "render_legacy_ped_paths %d\n", gRenderLegacyPedPaths ? 1 : 0);
	fprintf(f, "render_legacy_car_paths %d\n", gRenderLegacyCarPaths ? 1 : 0);
	fprintf(f, "render_sa_ped_paths %d\n", gRenderSaPedPaths ? 1 : 0);
	fprintf(f, "render_sa_ped_path_walkers %d\n", gRenderSaPedPathWalkers ? 1 : 0);
	fprintf(f, "render_sa_car_paths %d\n", gRenderSaCarPaths ? 1 : 0);
	fprintf(f, "render_sa_car_path_traffic %d\n", gRenderSaCarPathTraffic ? 1 : 0);
	fprintf(f, "render_sa_area_grid %d\n", gRenderSaAreaGrid ? 1 : 0);
	fprintf(f, "sa_ped_path_walker_count %d\n", gSaPedPathWalkerCount);
	fprintf(f, "sa_car_path_traffic_count %d\n", gSaCarPathTrafficCount);
	fprintf(f, "sa_car_path_traffic_speed_scale %.9g\n", gSaCarPathTrafficSpeedScale);
	fprintf(f, "sa_car_path_traffic_freeze_routes %d\n", gSaCarPathTrafficFreezeRoutes ? 1 : 0);
	fprintf(f, "render_sa_car_path_parked_cars %d\n", gRenderSaCarPathParkedCars ? 1 : 0);
	fprintf(f, "sa_car_path_parked_car_count %d\n", gSaCarPathParkedCarCount);
	fprintf(f, "render_water %d\n", gRenderWater ? 1 : 0);
	fprintf(f, "play_animations %d\n", gPlayAnimations ? 1 : 0);
	fprintf(f, "render_mode %d\n", gRenderMode);
	fprintf(f, "draw_distance %.9g\n", TheCamera.m_LODmult);
	fprintf(f, "fly_fast_mul %.9g\n", gFlyFastMul);
	fprintf(f, "fly_slow_mul %.9g\n", gFlySlowMul);
	fprintf(f, "fov_wheel_step %.9g\n", gFovWheelStep);
	fprintf(f, "render_all_timed_objects %d\n", gNoTimeCull ? 1 : 0);
	fprintf(f, "render_all_areas %d\n", gNoAreaCull ? 1 : 0);
	writeQuotedSetting(f, "ipl_filter_search", gIplFilterSearch);
	for(int i = 0; i < GetIplVisibilityEntryCount(); i++){
		fprintf(f, "ipl_visible ");
		writeInlineQuotedString(f, GetIplVisibilityEntryName(i));
		fprintf(f, " %d\n", GetIplVisibilityEntryVisible(i) ? 1 : 0);
	}
	fprintf(f, "render_postfx %d\n", gRenderPostFX ? 1 : 0);
	fprintf(f, "aa_samples %u\n", (unsigned)gRequestedAASamples);
	fprintf(f, "use_blur_ambient %d\n", gUseBlurAmb ? 1 : 0);
	fprintf(f, "override_blur_ambient %d\n", gOverrideBlurAmb ? 1 : 0);
	fprintf(f, "colour_filter %d\n", gColourFilter);
	fprintf(f, "radiosity %d\n", gRadiosity ? 1 : 0);
	fprintf(f, "building_pipe %d\n", gBuildingPipeSwitch);
	fprintf(f, "backface_culling %d\n", gDoBackfaceCulling ? 1 : 0);
	fprintf(f, "ps2_alpha_test %d\n", params.ps2AlphaTest ? 1 : 0);
	fprintf(f, "alpha_ref %d\n", params.alphaRef);
	fprintf(f, "render_background %d\n", gRenderBackground ? 1 : 0);
	fprintf(f, "enable_fog %d\n", gEnableFog ? 1 : 0);
	fprintf(f, "enable_timecycle_boxes %d\n", gEnableTimecycleBoxes ? 1 : 0);
	fprintf(f, "gizmo_enabled %d\n", gGizmoEnabled ? 1 : 0);
	fprintf(f, "gizmo_mode %d\n", gGizmoMode);
	fprintf(f, "gizmo_snap %d\n", gGizmoSnap ? 1 : 0);
	fprintf(f, "gizmo_snap_angle %.9g\n", gGizmoSnapAngle);
	fprintf(f, "gizmo_snap_translate %.9g\n", gGizmoSnapTranslate);
	fprintf(f, "place_snap_to_objects %d\n", gPlaceSnapToObjects ? 1 : 0);
	fprintf(f, "place_snap_to_ground %d\n", gPlaceSnapToGround ? 1 : 0);
	fprintf(f, "drag_follow_ground %d\n", gDragFollowGround ? 1 : 0);
	fprintf(f, "drag_align_to_surface %d\n", gDragAlignToSurface ? 1 : 0);
	fprintf(f, "brush_z_offset %.9g\n", gBrushZOffset);
	fprintf(f, "brush_align_to_surface %d\n", gBrushAlignToSurface ? 1 : 0);
	fprintf(f, "brush_yaw_min %.9g\n", gBrushYawMin);
	fprintf(f, "brush_yaw_max %.9g\n", gBrushYawMax);
	fprintf(f, "brush_spacing %.9g\n", gBrushSpacing);
	fprintf(f, "brush_radius %.9g\n", gBrushRadius);
	fprintf(f, "brush_count %d\n", gBrushCount);
	fprintf(f, "brush_delay_ms %.9g\n", gBrushDelayMs);
	writeQuotedSetting(f, "editor_camera_name", gEditorCameraName);
	writeQuotedSetting(f, "editor_model_filter", gEditorModelFilter.InputBuf);
	writeQuotedSetting(f, "editor_txd_filter", gEditorTxdFilter.InputBuf);
	fprintf(f, "editor_highlight_matches %d\n", gEditorHighlightMatches ? 1 : 0);
	fprintf(f, "browser_selected_category %d\n", gBrowserSelectedCategory);
	writeQuotedSetting(f, "browser_selected_ide", gBrowserSelectedIde);
	fprintf(f, "browser_active_tab %d\n", gBrowserActiveTab);
	writeQuotedSetting(f, "browser_category_filter", gBrowserCategoryFilter.InputBuf);
	writeQuotedSetting(f, "browser_ide_filter", gBrowserIdeFilter.InputBuf);
	writeQuotedSetting(f, "browser_search_filter", gBrowserSearchFilter.InputBuf);
	writeQuotedSetting(f, "browser_favourites_filter", gBrowserFavFilter.InputBuf);
	fprintf(f, "browser_selected_object %d\n", GetSpawnObjectId());
	fprintf(f, "diff_filter %d\n", gDiffFilter);
	fprintf(f, "water_snap_enabled %d\n", WaterLevel::gWaterSnapEnabled ? 1 : 0);
	fprintf(f, "water_snap_size %.9g\n", WaterLevel::gWaterSnapSize);
	fprintf(f, "water_sub_mode %d\n", WaterLevel::gWaterSubMode);
	fprintf(f, "water_create_shape %d\n", WaterLevel::gWaterCreateShape);
	fprintf(f, "water_create_z %.9g\n", WaterLevel::gWaterCreateZ);
	fclose(f);
}

void
SaveEditorSettingsNow(void)
{
	if(!gPersistentSettingsLoaded)
		return;
	saveSaveSettings();
}

static void
getCurrentCamSetting(CamSetting *cam)
{
	for(char *p = cam->name; *p; p++)
		if(*p == '"') *p = ' ';
	cam->pos = TheCamera.m_position;
	cam->target = TheCamera.m_target;
	cam->fov = TheCamera.m_fov;
	cam->hour = currentHour;
	cam->minute = currentMinute;
	cam->weather1 = Weather::oldWeather;
	cam->weather2 = Weather::newWeather;
	cam->area = currentArea;
}

static void
uiEditorWindow(void)
{
	static char buf[256];

	CPtrNode *p;
	ObjectInst *inst;
	ObjectDef *obj;
	TxdDef *txd;

	ImGui::Begin(ICON_FA_PEN " Editor", &showEditorWindow);

	if(ImGui::TreeNode("Camera")){
		ImGui::InputFloat3("Cam position", (float*)&TheCamera.m_position);
		ImGui::InputFloat3("Cam target", (float*)&TheCamera.m_target);
		ImGui::SameLine();
		ImGui::Checkbox("show", &gDrawTarget);
		ImGui::SliderFloat("FOV", (float*)&TheCamera.m_fov, 1.0f, 150.0f, "%.0f");
		ImGui::SameLine();
		if(ImGui::Button("Reset##fov"))
			TheCamera.m_fov = 70.0f;
		ImGui::SliderFloat("FOV wheel step", &gFovWheelStep, 0.1f, 15.0f, "%.2f deg");
		ImGui::SliderFloat("Fly speed fast (Shift)", &gFlyFastMul, 1.0f, 10.0f, "x%.2f");
		ImGui::SliderFloat("Fly speed slow (Alt)", &gFlySlowMul, 0.05f, 1.0f, "x%.2f");
		ImGui::Text("Far: %f", Timecycle::currentColours.farClp);
		ImGui::Text("mouse: %f %f", TheCamera.mx, TheCamera.my);

		ImGui::InputText("name", gEditorCameraName, sizeof(gEditorCameraName));
		if(ImGui::Button("Save")){
			CamSetting cam;
			strncpy(cam.name, gEditorCameraName, sizeof(cam.name));
			getCurrentCamSetting(&cam);
			camSettings.push_back(cam);
			saveCamSettings();
		}

		for(int i = 0; i < camSettings.size(); i++){
			CamSetting *cam = &camSettings[i];
			ImGui::PushID(i);
			sprintf(buf, "%-20s", cam->name);
			bool del = ImGui::Button("Delete");
			ImGui::SameLine();
			if(ImGui::Button("Replace")){
				strncpy(cam->name, gEditorCameraName, sizeof(cam->name));
				getCurrentCamSetting(cam);
				saveCamSettings();
			}
			ImGui::SameLine();
			if(ImGui::Selectable(buf)){
				strncpy(gEditorCameraName, cam->name, sizeof(gEditorCameraName));
				TheCamera.m_position = cam->pos;
				TheCamera.m_target = cam->target;
				TheCamera.m_fov = cam->fov;
				currentHour = cam->hour;
				currentMinute = cam->minute;
				Weather::oldWeather = cam->weather1;
				Weather::newWeather = cam->weather2;
				if(params.numAreas)
					currentArea = cam->area;
			}
			ImGui::PopID();
			if(del){
				memmove(&camSettings[i], &camSettings[i+1], (camSettings.size()-i-1)*sizeof(CamSetting));
				camSettings.pop_back();
				saveCamSettings();
				i--;
			}
		}
		ImGui::TreePop();
	}

	if(ImGui::TreeNode("CD images")){
		uiShowCdImages();
		ImGui::TreePop();
	}

	if(ImGui::TreeNode("Selection")){
		for(p = selection.first; p; p = p->next){
			inst = (ObjectInst*)p->item;
			obj = GetObjectDef(inst->m_objectId);
			ImGui::PushID(inst);
			ImGui::Selectable(obj->m_name);
			ImGui::PopID();
			if(ImGui::IsItemHovered()){
				inst->m_highlight = HIGHLIGHT_HOVER;
				if(ImGui::IsMouseClicked(1))
					inst->Deselect();
				if(ImGui::IsMouseDoubleClicked(0))
					inst->JumpTo();
			}
		}
		ImGui::TreePop();
	}

	if(ImGui::TreeNode("Instances")){
		gEditorModelFilter.Draw("Model (inc,-exc)"); ImGui::SameLine();
		if(ImGui::Button("Clear##Model"))
			gEditorModelFilter.Clear();
		gEditorTxdFilter.Draw("Txd (inc,-exc)"); ImGui::SameLine();
		if(ImGui::Button("Clear##Txd"))
			gEditorTxdFilter.Clear();
		ImGui::Checkbox("Highlight matches", &gEditorHighlightMatches);
		for(p = instances.first; p; p = p->next){
			inst = (ObjectInst*)p->item;
			obj = GetObjectDef(inst->m_objectId);
			txd = GetTxdDef(obj->m_txdSlot);
			if(gEditorModelFilter.PassFilter(obj->m_name) &&
			   gEditorTxdFilter.PassFilter(txd->name)){
				int numPops = 0;
				if(inst->m_isDeleted){
					ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(128, 128, 128));
					numPops++;
				}else if(inst->m_selected){
					ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(255, 0, 0));
					numPops++;
				}
				ImGui::PushID(inst);
				sprintf(buf, "%s%-20s %-20s %8.2f %8.2f %8.2f",
					inst->m_isDeleted ? "[X] " : "",
					obj->m_name, txd->name,
					inst->m_translation.x, inst->m_translation.y, inst->m_translation.z);
				ImGui::Selectable(buf);
				ImGui::PopID();
				if(ImGui::IsItemHovered()){
					if(ImGui::IsMouseClicked(1)){
						if(inst->m_isDeleted)
							inst->Undelete();
						else
							inst->Select();
					}
					if(ImGui::IsMouseDoubleClicked(0))
						inst->JumpTo();
				}
				if(numPops)
					ImGui::PopStyleColor(numPops);
				if(!inst->m_isDeleted){
					if(gEditorHighlightMatches)
						inst->m_highlight = HIGHLIGHT_FILTER;
					if(ImGui::IsItemHovered())
						inst->m_highlight = HIGHLIGHT_HOVER;
				}
			}
		}
		ImGui::TreePop();
	}

	PathNode *nd;
	if(nd = Path::GetDetachedCarNode(0,0))
	if(ImGui::TreeNode("Detached Legacy Car Paths")){
		for(int i = 0; nd = Path::GetDetachedCarNode(i,0); i++){
			static char str[32];
			sprintf(str, nd->water ? "Legacy Water Path %d" : "Legacy Car Path %d", i);
			ImGui::PushID(i);
			ImGui::Selectable(str);
			ImGui::PopID();
			if(ImGui::IsItemHovered()){
				Path::guiHoveredNode = nd;
				if(ImGui::IsMouseClicked(1))
					Path::selectedNode = nd;
				if(ImGui::IsMouseDoubleClicked(0))
					nd->JumpTo(nil);
			}
		}
		ImGui::TreePop();
	}

	if(nd = Path::GetDetachedPedNode(0,0))
	if(ImGui::TreeNode("Detached Legacy Ped Paths")){
		for(int i = 0; nd = Path::GetDetachedPedNode(i,0); i++){
			static char str[32];
			sprintf(str,"Legacy Ped Path %d", i);
			ImGui::PushID(i);
			ImGui::Selectable(str);
			ImGui::PopID();
			if(ImGui::IsItemHovered()){
				Path::guiHoveredNode = nd;
				if(ImGui::IsMouseClicked(1))
					Path::selectedNode = nd;
				if(ImGui::IsMouseDoubleClicked(0))
					nd->JumpTo(nil);
			}
		}
		ImGui::TreePop();
	}

	ImGui::End();
}

static void
uiToolsWindow(void)
{
	ImGui::Begin((std::string(ICON_FA_WRENCH " ") + _("Tools") + "###Tools").c_str(), &showToolsWindow);

	// Gizmo
	ImGui::Checkbox("Gizmo", &gGizmoEnabled);
	if(gGizmoEnabled){
		ImGui::SameLine();
		if(ImGui::RadioButton("Translate (W)", gGizmoMode == GIZMO_TRANSLATE))
			gGizmoMode = GIZMO_TRANSLATE;
		ImGui::SameLine();
		if(ImGui::RadioButton("Rotate (Q)", gGizmoMode == GIZMO_ROTATE))
			gGizmoMode = GIZMO_ROTATE;

		ImGui::Checkbox("Snap with Shift", &gGizmoSnap);
		ImGui::SetItemTooltip("Enable snap increments for the gizmo while Shift is held.");
		if(gGizmoSnap){
			ImGui::SameLine();
			ImGui::TextDisabled("(hold Shift to snap)");
			char buf[32];
			ImGui::SameLine();
			if(gGizmoMode == GIZMO_ROTATE){
				snprintf(buf, sizeof(buf), "%d\xC2\xB0", (int)gGizmoSnapAngle);
				ImGui::SetNextItemWidth(80);
				if(ImGui::BeginCombo("##snapangle", buf)){
					float angles[] = { 5, 10, 15, 30, 45, 90 };
					for(int i = 0; i < 6; i++){
						bool selected = gGizmoSnapAngle == angles[i];
						snprintf(buf, sizeof(buf), "%d\xC2\xB0", (int)angles[i]);
						if(ImGui::Selectable(buf, selected))
							gGizmoSnapAngle = angles[i];
						if(selected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
			}else{
				snprintf(buf, sizeof(buf), "%d", (int)gGizmoSnapTranslate);
				ImGui::SetNextItemWidth(80);
				if(ImGui::BeginCombo("##snaptrans", buf)){
					float intervals[] = { 1, 2, 5, 10, 25, 50 };
					for(int i = 0; i < 6; i++){
						bool selected = gGizmoSnapTranslate == intervals[i];
						snprintf(buf, sizeof(buf), "%d", (int)intervals[i]);
						if(ImGui::Selectable(buf, selected))
							gGizmoSnapTranslate = intervals[i];
						if(selected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
			}
		}
	}

	ImGui::Separator();

	// Placement
	ImGui::Text("Placement");
	ImGui::Checkbox("Snap to object", &gPlaceSnapToObjects);
	ImGui::SetItemTooltip("When placing objects, snap to the surface of existing objects under the cursor.");
	ImGui::Checkbox("Snap to ground", &gPlaceSnapToGround);
	ImGui::SetItemTooltip("When placing objects, snap to the ground below the cursor.");

	ImGui::Separator();

	// Brush tool — collapsed by default to keep the Tools window compact.
	if(ImGui::CollapsingHeader("Brush Tool")){
		int brushObjId = GetSpawnObjectId();
		ObjectDef *brushObj = brushObjId >= 0 ? GetObjectDef(brushObjId) : nil;

		if(gBrushMode){
			ImVec4 active(0.95f, 0.75f, 0.20f, 1.0f);
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.25f, 0.05f, 1.0f));
			if(ImGui::Button("Exit Brush (Esc)"))
				ExitBrushMode();
			ImGui::PopStyleColor();
			ImGui::SameLine();
			if(brushObj)
				ImGui::TextColored(active, "Painting: %s", brushObj->m_name);
			else
				ImGui::TextColored(active, "No object selected");
		}else{
			ImGui::BeginDisabled(brushObjId < 0);
			if(ImGui::Button("Start Brush"))
				EnterBrushMode(brushObjId);
			ImGui::EndDisabled();
			ImGui::SameLine();
			if(brushObj)
				ImGui::TextDisabled("Ready: %s", brushObj->m_name);
			else
				ImGui::TextDisabled("Select an object in the Browser first");
		}
		ImGui::TextDisabled("LMB = paint   \xC2\xB7   RMB-drag = look around   \xC2\xB7   Esc = cancel");

		ImGui::SetNextItemWidth(120);
		ImGui::InputFloat("Z offset (m)", &gBrushZOffset, 0.1f, 1.0f, "%.2f");
		ImGui::SetItemTooltip("Shift along Z after ground snap.\n"
			"Negative sinks the object into the ground (useful for trees on slopes).\n"
			"Positive lifts it off the surface.");

		ImGui::Checkbox("Align to surface##brush", &gBrushAlignToSurface);
		ImGui::SetItemTooltip("Rotate placed objects so their up-axis matches the surface normal.\n"
			"Use with care — some tree models look better upright on slopes.");

		// Brush size + scatter
		ImGui::SetNextItemWidth(120);
		ImGui::InputFloat("Radius (m)", &gBrushRadius, 0.5f, 5.0f, "%.2f");
		if(gBrushRadius < 0.0f) gBrushRadius = 0.0f;
		if(gBrushRadius > 500.0f) gBrushRadius = 500.0f;
		ImGui::SetItemTooltip("Brush disc size. 0 = single object under cursor.\n"
			"When > 0, each click scatters Count objects inside this radius.");

		ImGui::BeginDisabled(gBrushRadius <= 0.01f);
		ImGui::SetNextItemWidth(120);
		ImGui::InputInt("Count / click", &gBrushCount, 1, 5);
		if(gBrushCount < 1) gBrushCount = 1;
		if(gBrushCount > 128) gBrushCount = 128;
		ImGui::SetItemTooltip("Number of objects scattered per click when Radius > 0.\n"
			"Capped at 128 to keep a single click undoable as one step.");
		ImGui::EndDisabled();

		// Yaw range
		float yaw[2] = { gBrushYawMin, gBrushYawMax };
		ImGui::SetNextItemWidth(180);
		if(ImGui::InputFloat2("Yaw min/max (\xC2\xB0)", yaw, "%.0f")){
			gBrushYawMin = yaw[0];
			gBrushYawMax = yaw[1];
		}
		if(gBrushYawMin < -360.0f) gBrushYawMin = -360.0f;
		if(gBrushYawMax > 360.0f) gBrushYawMax = 360.0f;
		if(gBrushYawMax < gBrushYawMin) gBrushYawMax = gBrushYawMin;
		ImGui::SetItemTooltip("Yaw rotation range in degrees.\n"
			"Both equal = fixed angle (0/0 = no rotation).\n"
			"Different = uniform random between them (e.g. 0/360 for full random).");

		// Drag pacing
		ImGui::SetNextItemWidth(120);
		ImGui::InputFloat("Spacing (m)", &gBrushSpacing, 0.25f, 1.0f, "%.2f");
		if(gBrushSpacing < 0.0f) gBrushSpacing = 0.0f;
		ImGui::SetItemTooltip("Minimum world distance between drag-paint bursts.\n"
			"0 disables the distance gate (delay alone controls the rate).\n"
			"Single clicks always fire regardless.");

		ImGui::SetNextItemWidth(120);
		ImGui::InputFloat("Delay (ms)", &gBrushDelayMs, 10.0f, 100.0f, "%.0f");
		if(gBrushDelayMs < 0.0f) gBrushDelayMs = 0.0f;
		if(gBrushDelayMs > 10000.0f) gBrushDelayMs = 10000.0f;
		ImGui::SetItemTooltip("Minimum time between drag-paint bursts, in milliseconds.\n"
			"0 = no delay. Combines with Spacing — both constraints must pass.");
	}

	ImGui::Separator();

	// Dragging
	ImGui::Text("Dragging");
	ImGui::Checkbox("Follow ground", &gDragFollowGround);
	ImGui::SetItemTooltip("While dragging objects, keep them glued to the ground surface.");
	ImGui::BeginDisabled(!gDragFollowGround);
	ImGui::Indent();
	ImGui::Checkbox("Align to surface", &gDragAlignToSurface);
	ImGui::SetItemTooltip("While dragging, rotate the object to match the ground slope.");
	ImGui::Unindent();
	ImGui::EndDisabled();

	ImGui::Separator();

	// Automatic backups
	ImGui::Text("Automatic Backups");
	bool backupSettingsChanged = false;
	if(ImGui::Checkbox("Enabled", &gAutomaticBackupsEnabled))
		backupSettingsChanged = true;
	ImGui::SetItemTooltip("Periodically save a backup of all modified IPLs.");
	if(ImGui::InputInt("Interval (sec)", &gAutomaticBackupIntervalSeconds))
		backupSettingsChanged = true;
	ImGui::SetItemTooltip("Seconds between automatic backup snapshots.");
	if(ImGui::InputInt("Keep snapshots", &gAutomaticBackupKeepCount))
		backupSettingsChanged = true;
	ImGui::SetItemTooltip("Number of backup snapshots to keep. Oldest are deleted first.");
	sanitizeAutomaticBackupSettings();
	if(backupSettingsChanged)
		saveSaveSettings();
	ImGui::TextDisabled("Idle debounce: %.0f sec", gAutomaticBackupIdleSeconds);
	if(gAutomaticBackupLastSnapshot[0])
		ImGui::TextWrapped("Last snapshot: %s", gAutomaticBackupLastSnapshot);
	if(ImGui::Button("Create Backup Now"))
		runAutomaticBackup(true);

		ImGui::End();
	}

static bool waterPanelEditActive;

static void
waterPanelCheckUndoPush(void)
{
	if(ImGui::IsItemActivated() && !waterPanelEditActive){
		WaterLevel::WaterUndoPush();
		waterPanelEditActive = true;
	}
	if(!ImGui::IsAnyItemActive())
		waterPanelEditActive = false;
}

static void
uiWaterWindow(void)
{
	if(!ImGui::IsAnyItemActive())
		waterPanelEditActive = false;

	ImGui::Begin("Water Editor", nil);

	// Creation mode UI
	if(WaterLevel::gWaterCreateMode > 0){
		const char *shapeName = WaterLevel::gWaterCreateShape == 0 ? "QUAD" : "TRIANGLE";
		ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.4f, 1.0f), "CREATE %s", shapeName);
		ImGui::Separator();
		ImGui::DragFloat("Z Height", &WaterLevel::gWaterCreateZ, 0.5f);
		const char *shapeNames[] = { "Quad (2 clicks)", "Triangle (3 clicks)" };
		int prevShape = WaterLevel::gWaterCreateShape;
		ImGui::Combo("Shape", &WaterLevel::gWaterCreateShape, shapeNames, 2);
		if(WaterLevel::gWaterCreateShape != prevShape){
			// Restart placement with new shape
			WaterLevel::CancelCreateMode();
			WaterLevel::EnterCreateMode();
		}
		ImGui::Checkbox("Snap to Grid", &WaterLevel::gWaterSnapEnabled);
		if(WaterLevel::gWaterSnapEnabled){
			ImGui::SameLine();
			ImGui::SetNextItemWidth(60);
			ImGui::DragFloat("##SnapSize", &WaterLevel::gWaterSnapSize, 1.0f, 1.0f, 100.0f, "%.0f");
		}
		ImGui::Separator();
		int neededCorners = WaterLevel::gWaterCreateShape == 0 ? 2 : 3;
		ImGui::Text("Click corner %d of %d", WaterLevel::gWaterCreateMode, neededCorners);
		ImGui::Text("Shift+click: keep creating after placement");
		ImGui::Text("Right-click or Esc: cancel");
		ImGui::Separator();
		ImGui::Text("Quads: %d/%d  Tris: %d/%d  Verts: %d/%d",
			WaterLevel::GetNumQuads(), NUMWATERQUADS,
			WaterLevel::GetNumTris(), NUMWATERTRIS,
			WaterLevel::GetNumVertices(), NUMWATERVERTICES);
		ImGui::End();
		return;
	}

	const char *modeName = WaterLevel::gWaterSubMode == 0 ? "Polygon" : "Vertex";
	ImGui::Text("Mode: %s (Tab to switch)", modeName);
	ImGui::Separator();

	int numPolySel = WaterLevel::GetNumSelectedPolys();
	int numVertSel = WaterLevel::GetNumSelectedVertices();

	if(WaterLevel::gWaterSubMode == 0){
		// Polygon mode
		if(numPolySel == 0){
			ImGui::Text("Click a water polygon to select it");
			ImGui::Text("Shift+click: add, Ctrl+click: toggle");
			ImGui::Text("N: create new quad");
		}else if(numPolySel == 1){
			int ptype = WaterLevel::GetSelectedPolyType(0);
			int pidx = WaterLevel::GetSelectedPolyIndex(0);
			const char *shapeName = ptype == 0 ? "Quad" : "Triangle";
			ImGui::Text("Water %s #%d", shapeName, pidx);

			// Flags
			int *flagsPtr;
			int numVerts;
			int *indices;
			if(ptype == 0){
				WaterLevel::WaterQuad *q = WaterLevel::GetQuad(pidx);
				flagsPtr = &q->flags;
				numVerts = 4;
				indices = q->indices;
			}else{
				WaterLevel::WaterTri *t = WaterLevel::GetTri(pidx);
				flagsPtr = &t->flags;
				numVerts = 3;
				indices = t->indices;
			}

			bool visible = (*flagsPtr & 1) != 0;
			bool limited = (*flagsPtr & 2) != 0;
			if(ImGui::Checkbox("Visible", &visible)){
				WaterLevel::WaterUndoPush();
				*flagsPtr = (*flagsPtr & ~1) | (visible ? 1 : 0);
				WaterLevel::gWaterDirty = true;
			}
			ImGui::SameLine();
			if(ImGui::Checkbox("Limited Depth", &limited)){
				WaterLevel::WaterUndoPush();
				*flagsPtr = (*flagsPtr & ~2) | (limited ? 2 : 0);
				WaterLevel::gWaterDirty = true;
			}

			ImGui::Separator();

			// Per-vertex properties
			for(int j = 0; j < numVerts; j++){
				WaterLevel::WaterVertex *v = WaterLevel::GetVertex(indices[j]);
				ImGui::PushID(j);
				char label[32];
				snprintf(label, sizeof(label), "Vertex %d", j);
				if(ImGui::TreeNode(label)){
					bool changed = false;
					rw::V3d oldPos = v->pos;
					changed |= ImGui::DragFloat3("Position", (float*)&v->pos, 0.1f);
					waterPanelCheckUndoPush();
					changed |= ImGui::DragFloat2("Flow", (float*)&v->speed, 0.01f, -2.0f, 1.984375f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
					waterPanelCheckUndoPush();
					changed |= ImGui::DragFloat("Big waves", &v->waveunk, 0.01f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
					waterPanelCheckUndoPush();
					changed |= ImGui::DragFloat("Small waves", &v->waveheight, 0.01f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
					waterPanelCheckUndoPush();
					if(changed){
						WaterLevel::WeldCoincidentVertices(indices[j], oldPos);
						WaterLevel::gWaterDirty = true;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			ImGui::Separator();

			// Flatten Z button
			if(ImGui::Button("Flatten Z")){
				WaterLevel::WaterUndoPush();
				float avgZ = 0.0f;
				for(int j = 0; j < numVerts; j++)
					avgZ += WaterLevel::GetVertex(indices[j])->pos.z;
				avgZ /= (float)numVerts;
				for(int j = 0; j < numVerts; j++){
					rw::V3d op = WaterLevel::GetVertex(indices[j])->pos;
					WaterLevel::GetVertex(indices[j])->pos.z = avgZ;
					WaterLevel::WeldCoincidentVertices(indices[j], op);
				}
				WaterLevel::gWaterDirty = true;
			}

			// Set Z
			static float setZValue = 0.0f;
			ImGui::SameLine();
			ImGui::SetNextItemWidth(80);
			ImGui::DragFloat("##SetZ", &setZValue, 0.1f);
			ImGui::SameLine();
			if(ImGui::Button("Set Z")){
				WaterLevel::WaterUndoPush();
				for(int j = 0; j < numVerts; j++){
					rw::V3d op = WaterLevel::GetVertex(indices[j])->pos;
					WaterLevel::GetVertex(indices[j])->pos.z = setZValue;
					WaterLevel::WeldCoincidentVertices(indices[j], op);
				}
				WaterLevel::gWaterDirty = true;
			}
		}else{
			// Multiple polygons selected
			ImGui::Text("%d polygons selected", numPolySel);
			ImGui::Separator();

			// Bulk set Z
			static float bulkZ = 0.0f;
			ImGui::DragFloat("Z value", &bulkZ, 0.1f);
			if(ImGui::Button("Set All Z")){
				WaterLevel::WaterUndoPush();
				for(int i = 0; i < numPolySel; i++){
					int pt = WaterLevel::GetSelectedPolyType(i);
					int pi = WaterLevel::GetSelectedPolyIndex(i);
					int n; int *idx;
					if(pt == 0){
						n = 4; idx = WaterLevel::GetQuad(pi)->indices;
					}else{
						n = 3; idx = WaterLevel::GetTri(pi)->indices;
					}
					for(int j = 0; j < n; j++){
						rw::V3d op = WaterLevel::GetVertex(idx[j])->pos;
						WaterLevel::GetVertex(idx[j])->pos.z = bulkZ;
						WaterLevel::WeldCoincidentVertices(idx[j], op);
					}
				}
				WaterLevel::gWaterDirty = true;
			}
		}
	}else{
		// Vertex mode
		if(numVertSel == 0){
			ImGui::Text("Click a vertex to select it");
			ImGui::Text("Shift+click: add, Ctrl+click: toggle");
		}else if(numVertSel == 1){
			int vi = WaterLevel::GetSelectedVertexIndex(0);
			WaterLevel::WaterVertex *v = WaterLevel::GetVertex(vi);
			ImGui::Text("Water Vertex #%d", vi);
			ImGui::Separator();
			bool changed = false;
			rw::V3d oldPos = v->pos;
			changed |= ImGui::DragFloat3("Position", (float*)&v->pos, 0.1f);
			waterPanelCheckUndoPush();
			changed |= ImGui::DragFloat2("Flow", (float*)&v->speed, 0.01f, -2.0f, 1.984375f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
			waterPanelCheckUndoPush();
			changed |= ImGui::DragFloat("Big waves", &v->waveunk, 0.01f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
			waterPanelCheckUndoPush();
			changed |= ImGui::DragFloat("Small waves", &v->waveheight, 0.01f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
			waterPanelCheckUndoPush();
			if(changed){
				WaterLevel::WeldCoincidentVertices(vi, oldPos);
				WaterLevel::gWaterDirty = true;
			}
		}else{
			ImGui::Text("%d vertices selected", numVertSel);
			ImGui::Separator();
			static float bulkZ = 0.0f;
			ImGui::DragFloat("Z value", &bulkZ, 0.1f);
			if(ImGui::Button("Set All Z")){
				WaterLevel::WaterUndoPush();
				for(int i = 0; i < numVertSel; i++){
					int vi = WaterLevel::GetSelectedVertexIndex(i);
					rw::V3d op = WaterLevel::GetVertex(vi)->pos;
					WaterLevel::GetVertex(vi)->pos.z = bulkZ;
					WaterLevel::WeldCoincidentVertices(vi, op);
				}
				WaterLevel::gWaterDirty = true;
			}
		}
	}

	ImGui::Separator();

	// Tools
	ImGui::Checkbox("Snap to Grid", &WaterLevel::gWaterSnapEnabled);
	if(WaterLevel::gWaterSnapEnabled){
		ImGui::SameLine();
		ImGui::SetNextItemWidth(60);
		ImGui::DragFloat("##SnapSz", &WaterLevel::gWaterSnapSize, 1.0f, 1.0f, 100.0f, "%.0f");
	}

	ImGui::Separator();

	// Stats and actions
	int nq = WaterLevel::GetNumQuads(), nt = WaterLevel::GetNumTris(), nv = WaterLevel::GetNumVertices();
	ImGui::Text("Quads: %d/301  Tris: %d/6  Verts: %d", nq, nt, nv);
	if(nq > 301 || nt > 6)
		ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Exceeds game polygon limits!");
	ImGui::TextDisabled("Unique vertex limit (1021) checked on save");
	if(WaterLevel::gWaterDirty)
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Unsaved changes (Ctrl+S to save)");
	if(WaterLevel::WaterCanUndo()) ImGui::SameLine();
	if(WaterLevel::WaterCanUndo() && ImGui::SmallButton("Undo"))
		WaterLevel::WaterUndo();
	if(WaterLevel::WaterCanRedo()) ImGui::SameLine();
	if(WaterLevel::WaterCanRedo() && ImGui::SmallButton("Redo"))
		WaterLevel::WaterRedo();

	if(ImGui::Button("Reload water.dat")){
		WaterLevel::ReloadWater();
		Toast(TOAST_SAVE, "Reloaded water.dat");
	}

	ImGui::End();

}

static void
uiInstWindow(void)
{
	ImGui::Begin((std::string(ICON_FA_CIRCLE_INFO " ") + _("Object Info") + "###Object Info").c_str(), &showInstanceWindow);

	if(selection.first){
		int numSelected = 0;
		for(CPtrNode *sel = selection.first; sel; sel = sel->next)
			numSelected++;
		ImGui::Text("%d %s", numSelected, _("selected"));
		char exportDir[1024];
		bool haveExportDir = GetArianeDataPath(exportDir, sizeof(exportDir), "dff-txd-exports");
		if(ImGui::Button(_("Export DFF"))){
			if(!haveExportDir){
				Toast(TOAST_SAVE, "Failed to resolve dff-txd-exports path");
			}else{
				int failed = 0;
				int exported = ExportSelectedDffs(exportDir, &failed);
				if(exported > 0 || failed == 0)
					Toast(TOAST_SAVE, "Exported %d DFF(s) to %s%s", exported, exportDir,
					      failed > 0 ? " (some skipped)" : "");
				else
					Toast(TOAST_SAVE, "Failed to export DFF(s)");
			}
		}
		ImGui::SameLine();
		if(ImGui::Button("Export TXD")){
			if(!haveExportDir){
				Toast(TOAST_SAVE, "Failed to resolve dff-txd-exports path");
			}else{
				int failed = 0;
				int exported = ExportSelectedTxds(exportDir, &failed);
				if(exported > 0 || failed == 0)
					Toast(TOAST_SAVE, "Exported %d TXD(s) to %s%s", exported, exportDir,
					      failed > 0 ? " (some skipped)" : "");
				else
					Toast(TOAST_SAVE, "Failed to export TXD(s)");
			}
		}
		if(haveExportDir)
			ImGui::TextDisabled("%s", exportDir);
		ImGui::Separator();

		ObjectInst *inst = (ObjectInst*)selection.first->item;
		ObjectDef *obj = GetObjectDef(inst->m_objectId);
		if(ImGui::CollapsingHeader(_("Instance")))
			uiInstInfo(inst);
		if(ImGui::CollapsingHeader(_("Object")))
			uiObjInfo(obj);
		if(obj->m_numEffects)
			if(ImGui::CollapsingHeader(_("Effects")))
				uiFxInfo(inst);
		if(obj->m_carPathIndex >=0 || obj->m_pedPathIndex >= 0)
			if(ImGui::CollapsingHeader(_("Legacy Paths")))
				uiPathInfo(inst);
	}else{
		if(Path::selectedNode)// && Path::selectedNode->isDetached())
		if(ImGui::CollapsingHeader(_("Legacy Paths")))
			uiPathInfo(nil);
		if(SAPaths::HasInfoToShow()){
			if(gSaNodeJustSelected)
				ImGui::SetNextItemOpen(true);
			if(ImGui::CollapsingHeader(_("San Andreas Streamed Paths")))
				SAPaths::DrawInfoPanel();
		}

/*
		if(Effects::selectedEffect)
		if(ImGui::CollapsingHeader("Effects"))
			uiFxInfo(nil);
*/
	}
	ImGui::End();
}

static void
uiTest(void)
{
	ImGuiContext &g = *GImGui;
	int y = g.FontSizeBase + g.Style.FramePadding.y * 2.0f;	// height of main menu
	ImGui::SetNextWindowPos(ImVec2(0, y), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(200, sk::globals.height-y), ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::Begin("Dock", nil, ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize);
	ImGui::Text("hi there");
	if(ImGui::IsWindowFocused())
		ImGui::Text("focus");
	if(ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		ImGui::Text("drag");
	if(ImGui::IsWindowHovered())
		ImGui::Text("hover");
	ImGui::End();
	ImGui::PopStyleVar();
}

// Helper: check if category index is or is a child of parent
static bool
isCategoryOrChild(int cat, int parent)
{
	if(cat == parent) return true;
	// Check if cat's parent chain leads to parent
	if(cat >= 0 && cat < NUM_OBJ_CATEGORIES){
		int p = objCategories[cat].parent;
		if(p == parent) return true;
		// Only 2 levels deep max
		if(p >= 0 && objCategories[p].parent == parent) return true;
	}
	return false;
}

// Helper: build indented category name for dropdown
static void
buildCategoryLabel(int idx, char *buf, int bufsize)
{
	int depth = 0;
	if(objCategories[idx].parent >= 0){
		depth = 1;
		if(objCategories[objCategories[idx].parent].parent >= 0)
			depth = 2;
	}
	char prefix[16] = "";
	for(int d = 0; d < depth; d++) strcat(prefix, "  ");
	snprintf(buf, bufsize, "%s%s", prefix, objCategories[idx].name);
}

static void
selectBrowserObject(int i)
{
	SetSpawnObjectId(i);
	RequestObject(i);
	int lodId = GetLodForObject(i);
	if(lodId >= 0) RequestObject(lodId);
}

// Shared object list renderer with clipper
static void
uiObjectList(int *filtered, int numFiltered, int selId)
{
	ImGui::BeginChild("##ObjList", ImVec2(0, 0), true);
	ImGuiListClipper clipper;
	clipper.Begin(numFiltered);
	static char buf[256];
	while(clipper.Step()){
		for(int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++){
			int i = filtered[row];
			ObjectDef *obj = GetObjectDef(i);
			bool isSelected = (i == selId);
			sprintf(buf, "%5d  %s", obj->m_id, obj->m_name);

			ImGui::PushID(i);
			if(ImGui::Selectable(buf, isSelected))
				selectBrowserObject(i);
			// Right-click for favourites
			if(ImGui::BeginPopupContextItem()){
				if(IsFavourite(i)){
					if(ImGui::MenuItem("Remove from Favourites"))
						ToggleFavourite(i);
				}else{
					if(ImGui::MenuItem("Add to Favourites"))
						ToggleFavourite(i);
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}
	}
	ImGui::EndChild();
}

static void
uiBrowserWindow(void)
{
	ImGui::SetNextWindowSize(ImVec2(420, 700), ImGuiCond_FirstUseEver);
	ImGui::Begin(ICON_FA_MAGNIFYING_GLASS " Object Browser", &showBrowserWindow);

	int selId = GetSpawnObjectId();
	static int filtered[NUMOBJECTDEFS];
	int numFiltered = 0;

	// 3D Preview + selected info panel
	if(selId >= 0){
		ObjectDef *sel = GetObjectDef(selId);
		if(sel){
			// Preview (rendered in Draw() before main camera)
			if(gPreviewTexture && gPreviewTexture->raster){
				float previewW = ImGui::GetContentRegionAvail().x;
				float previewH = previewW * 0.75f;
				if(previewH > 200.0f) previewH = 200.0f;
				ImGui::Image((void*)(intptr_t)gPreviewTexture,
					ImVec2(previewW, previewH),
					ImVec2(0, 1), ImVec2(1, 0));
			}

			// Info line
			ImGui::TextColored(ImVec4(0,1,0,1), "%s (ID: %d)", sel->m_name, sel->m_id);
			ImGui::SameLine();
			ImGui::TextDisabled("%.0f", sel->GetLargestDrawDist());
			int lodId = GetLodForObject(selId);
			if(lodId >= 0){
				ObjectDef *lod = GetObjectDef(lodId);
				if(lod){
					ImGui::SameLine();
					ImGui::TextDisabled("LOD: %s", lod->m_name);
				}
			}

			// Action buttons — Place and Brush are mutually exclusive
			if(gPlaceMode){
				if(ImGui::Button("Exit Place Mode"))
					SpawnExitPlaceMode();
			}else{
				if(ImGui::Button("Place")){
					if(gBrushMode) ExitBrushMode();
					gPlaceMode = true;
				}
			}
			ImGui::SameLine();
			if(gBrushMode){
				if(ImGui::Button("Exit Brush"))
					ExitBrushMode();
			}else{
				if(ImGui::Button("Brush"))
					EnterBrushMode(selId);
			}
			ImGui::SetItemTooltip("Paint instances onto surfaces.\n"
				"Click to place one, drag to paint continuously.\n"
				"Configure Z offset, surface align and spacing in the Tools window.");
			ImGui::SameLine();
			if(IsFavourite(selId)){
				if(ImGui::Button("Unfavourite"))
					ToggleFavourite(selId);
			}else{
				if(ImGui::Button("Favourite"))
					ToggleFavourite(selId);
			}
			ImGui::Separator();
		}
	}

	// Tab bar
	if(ImGui::BeginTabBar("##BrowserTabs")){

		// === Categories tab ===
		if(ImGui::BeginTabItem("Categories", nil,
		   gBrowserTabRestorePending && gBrowserActiveTab == BROWSER_TAB_CATEGORIES ?
		   ImGuiTabItemFlags_SetSelected : 0)){
			gBrowserActiveTab = BROWSER_TAB_CATEGORIES;
			// Category dropdown
			char catLabel[128];
			if(gBrowserSelectedCategory >= 0 && gBrowserSelectedCategory < NUM_OBJ_CATEGORIES)
				snprintf(catLabel, sizeof(catLabel), "%s", objCategories[gBrowserSelectedCategory].name);
			else
				snprintf(catLabel, sizeof(catLabel), "All Categories");
			if(ImGui::BeginCombo("##CatCombo", catLabel)){
				if(ImGui::Selectable("All Categories", gBrowserSelectedCategory == -1)){
					gBrowserSelectedCategory = -1;
				}
				static char lb[128];
				for(int c = 0; c < NUM_OBJ_CATEGORIES; c++){
					buildCategoryLabel(c, lb, sizeof(lb));
					bool isSel = (c == gBrowserSelectedCategory);
					if(ImGui::Selectable(lb, isSel)){
						gBrowserSelectedCategory = c;
					}
				}
				ImGui::EndCombo();
			}

			gBrowserCategoryFilter.Draw("Filter##Cat");

			// Build filtered list
			numFiltered = 0;
			for(int i = 0; i < NUMOBJECTDEFS; i++){
				ObjectDef *obj = GetObjectDef(i);
				if(obj == nil) continue;
				if(gBrowserSelectedCategory >= 0){
					int cat = GetObjectCategory(i);
					if(cat < 0 || !isCategoryOrChild(cat, gBrowserSelectedCategory))
						continue;
				}
				if(!gBrowserCategoryFilter.PassFilter(obj->m_name)) continue;
				filtered[numFiltered++] = i;
			}
			ImGui::Text("%d objects", numFiltered);
			uiObjectList(filtered, numFiltered, selId);

			ImGui::EndTabItem();
		}

		// === IDE tab ===
		if(ImGui::BeginTabItem("IDE", nil,
		   gBrowserTabRestorePending && gBrowserActiveTab == BROWSER_TAB_IDE ?
		   ImGuiTabItemFlags_SetSelected : 0)){
			gBrowserActiveTab = BROWSER_TAB_IDE;
			// Collect unique IDE file names
			static const char *ideFiles[512];
			static int numIdeFiles = 0;
			if(gBrowserIdeListDirty){
				numIdeFiles = 0;
				for(int i = 0; i < NUMOBJECTDEFS; i++){
					ObjectDef *obj = GetObjectDef(i);
					if(obj == nil || obj->m_file == nil) continue;
					bool found = false;
					for(int j = 0; j < numIdeFiles; j++)
						if(strcmp(ideFiles[j], obj->m_file->name) == 0){
							found = true; break;
						}
					if(!found && numIdeFiles < 512)
						ideFiles[numIdeFiles++] = obj->m_file->name;
				}
				gBrowserIdeListDirty = false;
			}

			// IDE dropdown
			const char *ideLabel = gBrowserSelectedIde[0] ? gBrowserSelectedIde : "All IDE files";
			if(ImGui::BeginCombo("##IdeCombo", ideLabel)){
				if(ImGui::Selectable("All IDE files", gBrowserSelectedIde[0] == '\0'))
					gBrowserSelectedIde[0] = '\0';
				for(int j = 0; j < numIdeFiles; j++){
					bool isSel = strcmp(gBrowserSelectedIde, ideFiles[j]) == 0;
					if(ImGui::Selectable(ideFiles[j], isSel))
						snprintf(gBrowserSelectedIde, sizeof(gBrowserSelectedIde), "%s", ideFiles[j]);
				}
				ImGui::EndCombo();
			}

			gBrowserIdeFilter.Draw("Filter##Ide");

			numFiltered = 0;
			for(int i = 0; i < NUMOBJECTDEFS; i++){
				ObjectDef *obj = GetObjectDef(i);
				if(obj == nil) continue;
				if(gBrowserSelectedIde[0] != '\0' &&
				   (obj->m_file == nil || strcmp(obj->m_file->name, gBrowserSelectedIde) != 0))
					continue;
				if(!gBrowserIdeFilter.PassFilter(obj->m_name)) continue;
				filtered[numFiltered++] = i;
			}
			ImGui::Text("%d objects", numFiltered);
			uiObjectList(filtered, numFiltered, selId);

			ImGui::EndTabItem();
		}

		// === Search tab ===
		if(ImGui::BeginTabItem("Search", nil,
		   gBrowserTabRestorePending && gBrowserActiveTab == BROWSER_TAB_SEARCH ?
		   ImGuiTabItemFlags_SetSelected : 0)){
			gBrowserActiveTab = BROWSER_TAB_SEARCH;
			gBrowserSearchFilter.Draw("Search##All");
			ImGui::SameLine();
			if(ImGui::Button("Clear##SearchClear"))
				gBrowserSearchFilter.Clear();

			numFiltered = 0;
			if(gBrowserSearchFilter.IsActive()){
				for(int i = 0; i < NUMOBJECTDEFS; i++){
					ObjectDef *obj = GetObjectDef(i);
					if(obj == nil) continue;
					if(!gBrowserSearchFilter.PassFilter(obj->m_name)) continue;
					filtered[numFiltered++] = i;
				}
			}
			ImGui::Text("%d results", numFiltered);
			uiObjectList(filtered, numFiltered, selId);

			ImGui::EndTabItem();
		}

		// === Favourites tab ===
		if(ImGui::BeginTabItem("Favourites", nil,
		   gBrowserTabRestorePending && gBrowserActiveTab == BROWSER_TAB_FAVOURITES ?
		   ImGuiTabItemFlags_SetSelected : 0)){
			gBrowserActiveTab = BROWSER_TAB_FAVOURITES;
			gBrowserFavFilter.Draw("Filter##Fav");

			numFiltered = 0;
			for(int i = 0; i < NUMOBJECTDEFS; i++){
				if(!IsFavourite(i)) continue;
				ObjectDef *obj = GetObjectDef(i);
				if(obj == nil) continue;
				if(!gBrowserFavFilter.PassFilter(obj->m_name)) continue;
				filtered[numFiltered++] = i;
			}
			ImGui::Text("%d favourites", numFiltered);
			uiObjectList(filtered, numFiltered, selId);

			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
		gBrowserTabRestorePending = false;
	}

	ImGui::End();
}

static void
uiDiffWindow(void)
{
	ImGui::Begin(ICON_FA_CODE_COMPARE " Changes Since Last Save", &showDiffWindow);

	// Count changes by category
	int numAdded = 0, numDeleted = 0, numMoved = 0, numRotated = 0, numRestored = 0;
	for(CPtrNode *p = instances.first; p; p = p->next){
		int flags = GetInstanceDiffFlags((ObjectInst*)p->item);
		if(flags & DIFF_ADDED)    numAdded++;
		if(flags & DIFF_DELETED)  numDeleted++;
		if(flags & DIFF_MOVED)    numMoved++;
		if(flags & DIFF_ROTATED)  numRotated++;
		if(flags & DIFF_RESTORED) numRestored++;
	}

	int total = numAdded + numDeleted + numMoved + numRotated + numRestored;
	if(total == 0){
		ImGui::TextDisabled("No changes since last save.");
		ImGui::End();
		return;
	}

	ImGui::Text("%d added, %d deleted, %d moved, %d rotated", numAdded, numDeleted, numMoved, numRotated);
	if(numRestored > 0)
		ImGui::SameLine(), ImGui::Text(", %d restored", numRestored);
	ImGui::Separator();

	// Filter buttons (bitmask — 0 = show all)
	if(ImGui::RadioButton("All", gDiffFilter == 0)) gDiffFilter = 0;
	ImGui::SameLine();
	if(ImGui::RadioButton("Added", gDiffFilter == DIFF_ADDED)) gDiffFilter = DIFF_ADDED;
	ImGui::SameLine();
	if(ImGui::RadioButton("Deleted", gDiffFilter == DIFF_DELETED)) gDiffFilter = DIFF_DELETED;
	ImGui::SameLine();
	if(ImGui::RadioButton("Moved", gDiffFilter == DIFF_MOVED)) gDiffFilter = DIFF_MOVED;
	ImGui::SameLine();
	if(ImGui::RadioButton("Rotated", gDiffFilter == DIFF_ROTATED)) gDiffFilter = DIFF_ROTATED;
	if(numRestored > 0){
		ImGui::SameLine();
		if(ImGui::RadioButton("Restored", gDiffFilter == DIFF_RESTORED)) gDiffFilter = DIFF_RESTORED;
	}

	// Collect changed instances into temp array for sorting
	ObjectInst *changed[4096];
	int numChanged = 0;
	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		int flags = GetInstanceDiffFlags(inst);
		if(flags == 0) continue;
		if(gDiffFilter != 0 && !(flags & gDiffFilter)) continue;
		if(numChanged < 4096)
			changed[numChanged++] = inst;
	}

	// Sort by m_changeSeq descending (most recent first)
	for(int i = 0; i < numChanged - 1; i++)
		for(int j = i + 1; j < numChanged; j++)
			if(changed[j]->m_changeSeq > changed[i]->m_changeSeq){
				ObjectInst *tmp = changed[i];
				changed[i] = changed[j];
				changed[j] = tmp;
			}

	ImGui::BeginChild("DiffList", ImVec2(0, 0), true);
	for(int i = 0; i < numChanged; i++){
		ObjectInst *inst = changed[i];
		int flags = GetInstanceDiffFlags(inst);
		ObjectDef *obj = GetObjectDef(inst->m_objectId);
		const char *name = obj ? obj->m_name : "???";

		// Build prefix string from flags
		char prefix[8] = "";
		ImVec4 color = ImVec4(1,1,1,1);
		if(flags & DIFF_ADDED){
			strcat(prefix, "+");
			color = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
		}else if(flags & DIFF_DELETED){
			strcat(prefix, "-");
			color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
		}else if(flags & DIFF_RESTORED){
			strcat(prefix, "U");
			color = ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
		}else{
			if(flags & DIFF_MOVED)  strcat(prefix, "M");
			if(flags & DIFF_ROTATED) strcat(prefix, "R");
			color = ImVec4(1.0f, 1.0f, 0.3f, 1.0f);
			if(flags == DIFF_ROTATED)
				color = ImVec4(0.3f, 0.7f, 1.0f, 1.0f);
		}

		char buf[256];
		if(flags & DIFF_MOVED){
			float dist = length(sub(inst->m_translation, inst->m_savedTranslation));
			snprintf(buf, sizeof(buf), "%-3s %-20s  %.1f, %.1f, %.1f  (%.1fm)",
				prefix, name,
				inst->m_translation.x, inst->m_translation.y, inst->m_translation.z,
				dist);
		}else if(flags & DIFF_ROTATED){
			float dot = fabsf(inst->m_rotation.x * inst->m_savedRotation.x +
			                   inst->m_rotation.y * inst->m_savedRotation.y +
			                   inst->m_rotation.z * inst->m_savedRotation.z +
			                   inst->m_rotation.w * inst->m_savedRotation.w);
			if(dot > 1.0f) dot = 1.0f;
			float angleDeg = 2.0f * acosf(dot) * (180.0f / 3.14159265f);
			snprintf(buf, sizeof(buf), "%-3s %-20s  %.1f, %.1f, %.1f  (%.1f deg)",
				prefix, name,
				inst->m_translation.x, inst->m_translation.y, inst->m_translation.z,
				angleDeg);
		}else{
			snprintf(buf, sizeof(buf), "%-3s %-20s  %.1f, %.1f, %.1f",
				prefix, name,
				inst->m_translation.x, inst->m_translation.y, inst->m_translation.z);
		}

		ImGui::PushStyleColor(ImGuiCol_Text, color);
		ImGui::PushID(inst);
		if(ImGui::Selectable(buf)){
			inst->Select();
		}
		ImGui::PopID();
		ImGui::PopStyleColor();

		if(ImGui::IsItemHovered()){
			inst->m_highlight = HIGHLIGHT_HOVER;
			if(ImGui::IsMouseDoubleClicked(0))
				inst->JumpTo();
		}
	}
	ImGui::EndChild();

	ImGui::End();
}

static ExampleAppLog logwindow;
// TODO: this crashes for me on linux. should figure out how to fix
//void addToLogWindow(const char *fmt, va_list args) { logwindow.AddLog(fmt, args); }
void addToLogWindow(const char *fmt, va_list args) { }

void
gui(void)
{
	static bool show_another_window = false;
	static ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
	static bool camloaded = false;

	if(!camloaded){
		loadCamSettings();
		loadSaveSettings();
		camloaded = true;
	}

	Path::guiHoveredNode = nil;
	uiMainmenu();
	// TODO: restore when the Destroy Entire Map workflow is finished.
	UpdaterDrawGui();
	automaticBackupTick();

	// Ctrl+D duplicate in water mode
	if(WaterLevel::gWaterEditMode && CPad::IsCtrlDown() && CPad::IsKeyJustDown('D')){
		int count = WaterLevel::GetNumSelectedPolys();
		if(count > 0){
			WaterLevel::DuplicateSelectedWaterPolys();
			Toast(TOAST_COPY_PASTE, "Duplicated %d water polygon(s)", count);
		}
	}

	// Copy/Paste (not in water edit mode)
	if(!WaterLevel::gWaterEditMode){
		if(CPad::IsCtrlDown() && CPad::IsKeyJustDown('C')){
			int before = 0;
			for(CPtrNode *p = selection.first; p; p = p->next) before++;
			CopySelected();
			if(before > 0)
				Toast(TOAST_COPY_PASTE, "Copied %d instance(s)", before);
		}
		if(CPad::IsCtrlDown() && CPad::IsKeyJustDown('V')){
			int before = 0;
			for(CPtrNode *p = instances.first; p; p = p->next) before++;
			PasteClipboard();
			int after = 0;
			for(CPtrNode *p = instances.first; p; p = p->next) after++;
			int pasted = after - before;
			if(pasted > 0)
				Toast(TOAST_COPY_PASTE, "Pasted %d instance(s)", pasted);
		}
	}

	if(!CPad::IsCtrlDown() && CPad::IsKeyJustDown('C')) gUseViewerCam = !gUseViewerCam;

	// Prefabs
	if(CPad::IsCtrlDown() && CPad::IsShiftDown() && CPad::IsKeyJustDown('E')){
		if(selection.first)
			gOpenExportPrefab = true;
	}
	if(CPad::IsCtrlDown() && CPad::IsShiftDown() && CPad::IsKeyJustDown('I')){
		gOpenImportPrefab = true;
	}

	// Gizmo mode shortcuts (not in water edit mode — water gizmo is always translate)
	if(!WaterLevel::gWaterEditMode){
		if(CPad::IsKeyJustDown('W')) gGizmoMode = GIZMO_TRANSLATE;
		if(CPad::IsKeyJustDown('Q')) gGizmoMode = GIZMO_ROTATE;
	}

	// Delete
	if(CPad::IsKeyJustDown(KEY_DEL) || CPad::IsKeyJustDown(KEY_BACKSP)){
		if(WaterLevel::gWaterEditMode){
			int count = WaterLevel::GetNumSelectedPolys();
			if(count > 0){
				WaterLevel::DeleteSelectedWaterPolys();
				Toast(TOAST_DELETE, "Deleted %d water polygon(s)", count);
			}
		}else{
			int count = 0;
			for(CPtrNode *p = selection.first; p; p = p->next) count++;
			if(count > 0){
				DeleteSelected();
				Toast(TOAST_DELETE, "Deleted %d instance(s)", count);
			}
		}
	}

	// Undo/Redo
	if(CPad::IsCtrlDown() && CPad::IsKeyJustDown('Z')){
		if(WaterLevel::gWaterEditMode){
			if(WaterLevel::WaterCanUndo()){
				WaterLevel::WaterUndo();
				Toast(TOAST_UNDO_REDO, "Water Undo");
			}
		}else{
			Undo();
			Toast(TOAST_UNDO_REDO, "Undo");
		}
	}
	if(CPad::IsCtrlDown() && CPad::IsKeyJustDown('Y')){
		if(WaterLevel::gWaterEditMode){
			if(WaterLevel::WaterCanRedo()){
				WaterLevel::WaterRedo();
				Toast(TOAST_UNDO_REDO, "Water Redo");
			}
		}else{
			Redo();
			Toast(TOAST_UNDO_REDO, "Redo");
		}
	}

	// Ctrl+S to save
	if(CPad::IsCtrlDown() && CPad::IsKeyJustDown('S')){
		if(WaterLevel::gWaterEditMode){
			if(WaterLevel::gWaterDirty){
				if(WaterLevel::SaveWater())
					Toast(TOAST_SAVE, "Saved water.dat to %s", getSaveDestinationLabel());
			}
		}else{
			if(saveAllIpls())
				Toast(TOAST_SAVE, "Saved all IPL files to %s", getSaveDestinationLabel());
		}
	}

	// Ctrl+G to test in game
	if(CPad::IsCtrlDown() && CPad::IsKeyJustDown('G')){
		testInGame();
	}

	// Ctrl+R to hot reload streaming IPLs in running game
	if(CPad::IsCtrlDown() && CPad::IsKeyJustDown('R')){
		hotReloadIpls();
	}

	if(!CPad::IsCtrlDown() && CPad::IsKeyJustDown('G'))
		SnapSelectedToGround(CPad::IsShiftDown());

	if(CPad::IsKeyJustDown('T')) showTimeWeatherWindow ^= 1;
	if(showTimeWeatherWindow){
		ImGui::Begin(ICON_FA_CLOUD_SUN " Time & Weather", &showTimeWeatherWindow);
		uiTimeWeather();
		ImGui::End();
	}

	if(!CPad::IsCtrlDown() && CPad::IsKeyJustDown('V')) showViewWindow ^= 1;
	if(showViewWindow){
		ImGui::SetNextWindowSize(ImVec2(460.0f, 640.0f), ImGuiCond_FirstUseEver);
		ImGui::Begin(ICON_FA_EYE " View", &showViewWindow);
		uiView();
		ImGui::End();
	}

	if(!CPad::IsCtrlDown() && CPad::IsKeyJustDown('R')) showRenderingWindow ^= 1;
	if(showRenderingWindow){
		ImGui::Begin(ICON_FA_PAINTBRUSH " Rendering", &showRenderingWindow);
		uiRendering();
		ImGui::End();
	}

	if(CPad::IsKeyJustDown('X')) showToolsWindow ^= 1;
	if(showToolsWindow) uiToolsWindow();

	{
		static SAPaths::Node *prevSaNode = nil;
		gSaNodeJustSelected = SAPaths::selectedNode != nil && SAPaths::selectedNode != prevSaNode;
		prevSaNode = SAPaths::selectedNode;
		if(gSaNodeJustSelected)
			showInstanceWindow = true;
	}
	if(!CPad::IsCtrlDown() && !CPad::IsShiftDown() && CPad::IsKeyJustDown('I')) showInstanceWindow ^= 1;
	if(showInstanceWindow) uiInstWindow();

	if(!CPad::IsCtrlDown() && !CPad::IsShiftDown() && CPad::IsKeyJustDown('E')) showEditorWindow ^= 1;
	if(showEditorWindow) uiEditorWindow();

	if(!CPad::IsCtrlDown() && CPad::IsKeyJustDown('F')) showDiffWindow ^= 1;
	if(showDiffWindow) uiDiffWindow();

	if(CPad::IsKeyJustDown('B')){
		showBrowserWindow ^= 1;
		if(!showBrowserWindow){
			if(gPlaceMode) SpawnExitPlaceMode();
			if(gBrushMode) ExitBrushMode();
		}
	}
	if(showBrowserWindow){
		uiBrowserWindow();
		// ImGui X button can set showBrowserWindow to false
		if(!showBrowserWindow){
			if(gPlaceMode) SpawnExitPlaceMode();
			if(gBrushMode) ExitBrushMode();
		}
	}

	// Escape: cancel creation, exit water mode, exit brush, or exit place mode
	if(CPad::IsKeyJustDown(KEY_ESC)){
		if(WaterLevel::gWaterCreateMode > 0){
			WaterLevel::CancelCreateMode();
		}else if(WaterLevel::gWaterEditMode){
			WaterLevel::gWaterEditMode = false;
			WaterLevel::ClearWaterSelection();
			WaterLevel::gWaterSubMode = 0;
		}else if(gBrushMode){
			ExitBrushMode();
		}else if(gPlaceMode)
			SpawnExitPlaceMode();
	}

	// H toggles water edit mode (SA only)
	if(params.water == GAME_SA && CPad::IsKeyJustDown('H') && !CPad::IsCtrlDown()){
		WaterLevel::gWaterEditMode = !WaterLevel::gWaterEditMode;
		if(WaterLevel::gWaterEditMode){
			ClearSelection();
			if(gPlaceMode) SpawnExitPlaceMode();
			if(gBrushMode) ExitBrushMode();
		}else{
			WaterLevel::CancelCreateMode();
			WaterLevel::ClearWaterSelection();
			WaterLevel::gWaterSubMode = 0;
		}
	}

	// Tab switches water sub-mode (cancel creation first)
	if(WaterLevel::gWaterEditMode && CPad::IsKeyJustDown(KEY_TAB)){
		WaterLevel::CancelCreateMode();
		if(WaterLevel::gWaterSubMode == 0){
			WaterLevel::ClearWaterPolySelection();
			WaterLevel::gWaterSubMode = 1;
		}else{
			WaterLevel::ClearWaterVertexSelection();
			WaterLevel::gWaterSubMode = 0;
		}
	}

	// N enters quad creation mode
	if(WaterLevel::gWaterEditMode && !WaterLevel::gWaterCreateMode && CPad::IsKeyJustDown('N') && !CPad::IsCtrlDown()){
		WaterLevel::EnterCreateMode();
	}

	// Water editor window
	if(WaterLevel::gWaterEditMode)
		uiWaterWindow();

	if(showHelpWindow) uiHelpWindow();
	if(showDemoWindow){
		ImGui::SetNextWindowPos(ImVec2(650, 20), ImGuiCond_FirstUseEver);
		ImGui::ShowDemoWindow(&showDemoWindow);
	}

	if(showLogWindow) logwindow.Draw("Log", &showLogWindow);

	// Place mode overlay
	if(gPlaceMode && GetSpawnObjectId() >= 0){
		ObjectDef *obj = GetObjectDef(GetSpawnObjectId());
		if(obj){
			ImGui::SetNextWindowPos(ImVec2(10, ImGui::GetIO().DisplaySize.y - 40));
			ImGui::SetNextWindowBgAlpha(0.6f);
			ImGui::Begin("##PlaceMode", nil,
				ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoFocusOnAppearing);
			ImGui::TextColored(ImVec4(1,1,0,1),
				"PLACE: %s  [Click=Place | Shift+Click=Multi | RMB/Esc=Cancel]", obj->m_name);
			ImGui::End();
		}
	}

	// Brush mode overlay + on-surface preview marker
	if(gBrushMode && GetSpawnObjectId() >= 0){
		ObjectDef *obj = GetObjectDef(GetSpawnObjectId());
		if(obj){
			ImGui::SetNextWindowPos(ImVec2(10, ImGui::GetIO().DisplaySize.y - 40));
			ImGui::SetNextWindowBgAlpha(0.6f);
			ImGui::Begin("##BrushMode", nil,
				ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoFocusOnAppearing);
			char sizeBuf[64];
			if(gBrushRadius > 0.01f)
				snprintf(sizeBuf, sizeof(sizeBuf), "r=%.1fm x%d", gBrushRadius, gBrushCount);
			else
				snprintf(sizeBuf, sizeof(sizeBuf), "single");
			ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.20f, 1),
				"BRUSH: %s  [LMB=Paint | RMB=Look | MMB=Pan/Orbit | Esc=Cancel]  %s  z%+.2f%s",
				obj->m_name, sizeBuf, gBrushZOffset,
				gBrushAlignToSurface ? "  align" : "");
			ImGui::End();

			// On-surface preview — only if not over an ImGui panel
			ImGuiIO &brushIO = ImGui::GetIO();
			if(!brushIO.WantCaptureMouse){
				rw::V3d bHit, bNormal;
				if(GetBrushSurfaceHit(&bHit, &bNormal)){
					rw::V3d centerPos = bHit;
					centerPos.z += GetPlacementBaseOffset(GetSpawnObjectId());
					centerPos.z += gBrushZOffset;

					ImDrawList *dl = ImGui::GetForegroundDrawList();
					ImU32 outer = IM_COL32(240, 190, 50, 230);
					ImU32 inner = IM_COL32(255, 220, 100, 70);

					// Center marker: fixed-size crosshair in screen space
					rw::V3d centerScreen;
					float csw, csh;
					if(Sprite::CalcScreenCoors(centerPos, &centerScreen, &csw, &csh, false)){
						float cr = 4.0f;
						dl->AddCircleFilled(ImVec2(centerScreen.x, centerScreen.y), cr, outer, 12);
					}

					// World-radius disc: sample points around the brush circle in world XY,
					// ground-snap each so it hugs the terrain, project to screen and stroke.
					if(gBrushRadius > 0.01f){
						const int segs = 48;
						ImVec2 pts[segs + 1];
						int validPts = 0;
						bool continuous = true;
						for(int i = 0; i <= segs; i++){
							float theta = (float)i / (float)segs * 6.28318530718f;
							rw::V3d wp = bHit;
							wp.x += gBrushRadius * cosf(theta);
							wp.y += gBrushRadius * sinf(theta);
							rw::V3d snapped = wp;
							GetGroundPlacementSurface(wp, &snapped, nil, true);
							// lift slightly to avoid z-fighting with ground
							snapped.z += 0.05f;

							rw::V3d sp;
							float sw, sh;
							if(Sprite::CalcScreenCoors(snapped, &sp, &sw, &sh, false))
								pts[validPts++] = ImVec2(sp.x, sp.y);
							else
								continuous = false;
						}
						if(validPts > 2){
							dl->AddPolyline(pts, validPts, outer, continuous ? ImDrawFlags_Closed : 0, 2.0f);
							// faint inner shade so the disc reads as a filled region
							dl->AddConvexPolyFilled(pts, validPts, inner);
						}
					}else{
						// radius 0: small screen-space ring around the single-placement point
						if(Sprite::CalcScreenCoors(centerPos, &centerScreen, &csw, &csh, false)){
							float r = 14.0f * csw;
							if(r < 6.0f) r = 6.0f;
							if(r > 64.0f) r = 64.0f;
							dl->AddCircle(ImVec2(centerScreen.x, centerScreen.y), r, outer, 32, 1.5f);
						}
					}
				}
			}
		}
	}

	// Water hover hint (when not in water edit mode, mouse over water)
	if(!WaterLevel::gWaterEditMode && !gPlaceMode && params.water == GAME_SA){
		ImGuiIO &hintIO = ImGui::GetIO();
		if(!hintIO.WantCaptureMouse){
			Ray ray;
			ray.start = TheCamera.m_position;
			ray.dir = normalize(TheCamera.m_mouseDir);
			float waterT = 1.0e30f;
			int hit = WaterLevel::PickWaterPoly(ray, &waterT);
			bool showHint = hit != INT_MIN && waterT < 750.0f;
			if(showHint){
				float sceneT = 1.0e30f;
				if(GetVisibleInstUnderRay(ray, nil, &sceneT) && sceneT + 0.5f < waterT)
					showHint = false;
			}
			if(showHint){
				ImGui::SetNextWindowPos(ImVec2(hintIO.MousePos.x + 15, hintIO.MousePos.y + 15));
				ImGui::SetNextWindowBgAlpha(0.7f);
				ImGui::Begin("##WaterHint", nil,
					ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
					ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
					ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs);
				ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), "H - Edit Water");
				ImGui::End();
			}
		}
	}

	// Water edit mode overlay
	if(WaterLevel::gWaterEditMode){
		ImGui::SetNextWindowPos(ImVec2(10, ImGui::GetIO().DisplaySize.y - 40));
		ImGui::SetNextWindowBgAlpha(0.6f);
		ImGui::Begin("##WaterMode", nil,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoFocusOnAppearing);
		if(WaterLevel::gWaterCreateMode > 0){
			const char *shape = WaterLevel::gWaterCreateShape == 0 ? "QUAD" : "TRI";
			int needed = WaterLevel::gWaterCreateShape == 0 ? 2 : 3;
			ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.4f, 1.0f),
				"CREATE %s [corner %d/%d] Z=%.1f | RMB/Esc:cancel | Shift+click:multi",
				shape, WaterLevel::gWaterCreateMode, needed, WaterLevel::gWaterCreateZ);
		}else{
			const char *subMode = WaterLevel::gWaterSubMode == 0 ? "polygon" : "vertex";
			ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f),
				"WATER [%s] | H:exit | Tab:mode | N:new | Del:delete | Ctrl+D:dup | Ctrl+Z/Y:undo | Ctrl+S:save", subMode);
		}
		ImGui::End();
	}

	uiToasts();

	gSettingsAutosaveSeconds += ImGui::GetIO().DeltaTime;
	if(gSettingsAutosaveSeconds >= 1.0f){
		saveSaveSettings();
		gSettingsAutosaveSeconds = 0.0f;
	}

//	uiTest();
}
