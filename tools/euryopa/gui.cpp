#include "euryopa.h"
#include "imgui/imgui_internal.h"
#include "object_categories.h"
#include "updater.h"

static bool showDemoWindow;
static bool showEditorWindow;
static bool showInstanceWindow;
static bool showLogWindow;
static bool showHelpWindow;

static bool showTimeWeatherWindow;
static bool showViewWindow;
static bool showRenderingWindow;
static bool showBrowserWindow;

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

static void
saveAllIpls(void)
{
	// Collect unique IPL filenames from all instances
	CPtrNode *p;
	const char *saved[512];
	int numSaved = 0;

	for(p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_file == nil)
			continue;
		// Skip streaming IPL instances — those are saved via SaveBinaryIpls
		if(inst->m_imageIndex >= 0)
			continue;
		// Check if we already saved this file
		bool found = false;
		for(int i = 0; i < numSaved; i++)
			if(strcmp(saved[i], inst->m_file->name) == 0){
				found = true;
				break;
			}
		if(!found && numSaved < 512){
			FileLoader::SaveScene(inst->m_file->name);
			saved[numSaved++] = inst->m_file->name;
		}
	}

	// Also patch binary IPLs in IMG for streaming instances
	FileLoader::SaveBinaryIpls();
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
	saveAllIpls();
	Toast(TOAST_SAVE, "Saved all IPL files");

	// Camera position -> snap to ground
	rw::V3d pos = TheCamera.m_position;
	rw::V3d groundHit;
	if(GetGroundPlacementSurface(pos, &groundHit))
		pos.z = groundHit.z + 1.0f;
	// If no ground found: use camera pos as-is (player will fall)

	// Camera heading
	float heading = TheCamera.getHeading();

	char rootDir[2048];
	char teleportPath[2048];
	if(!getEditorRootDirectory(rootDir, sizeof(rootDir)) ||
	   !buildPath(teleportPath, sizeof(teleportPath), rootDir, "ariane_teleport.txt")){
		Toast(TOAST_SAVE, "Failed to resolve game folder");
		return;
	}

	// Write the teleport file next to the editor executable.
	FILE *f = fopen(teleportPath, "w");
	if(f){
		fprintf(f, "%f %f %f %f %d\n", pos.x, pos.y, pos.z, heading, currentArea);
		fclose(f);
	} else {
		Toast(TOAST_SAVE, "Failed to write teleport file");
		return;
	}

	// Launch game executable
	const char *exeName = nil;
	if(isIII()) exeName = "gta3.exe";
	else if(isVC()) exeName = "gta-vc.exe";
	else if(isSA()) exeName = "gta_sa.exe";

#ifdef _WIN32
	char exePath[2048];
	if(!buildPath(exePath, sizeof(exePath), rootDir, exeName)){
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
	if(!warnMissingTeleportAsi("Hot Reload"))
		return;

	char rootDir[2048];
	char reloadPath[2048];
	char entityReloadPath[2048];
	if(!getEditorRootDirectory(rootDir, sizeof(rootDir)) ||
	   !buildPath(reloadPath, sizeof(reloadPath), rootDir, "ariane_reload.txt") ||
	   !buildPath(entityReloadPath, sizeof(entityReloadPath), rootDir, "ariane_reload_entities.txt")){
		Toast(TOAST_SAVE, "Hot Reload: failed to resolve game folder");
		return;
	}

	CPtrNode *p;
	int numStreamingIpls = 0;
	int numEntityCmds = 0;
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

	for(p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_imageIndex < 0) continue;
		if(!inst->m_isDirty && !inst->m_isDeleted) continue;
		if(inst->m_file == nil) continue;

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
		FileLoader::SaveBinaryIpls();

		FILE *f = fopen(reloadPath, "w");
		if(f){
			for(int i = 0; i < numNames; i++)
				fprintf(f, "%s\n", iplNames[i]);
			fclose(f);
			numStreamingIpls = numNames;
		}
	}

	// --- Entity deletes/moves (manipulated directly in game memory) ---
	// Streaming moves also go through this path as a runtime fallback:
	// if binary IMG patching/reload fails for a given IPL, the live entity
	// still gets moved in-game.
	FILE *fe = fopen(entityReloadPath, "w");
	if(fe){
		for(p = instances.first; p; p = p->next){
			ObjectInst *inst = (ObjectInst*)p->item;
			if(!inst->m_isDirty && !inst->m_isDeleted) continue;

			if(inst->m_isAdded){
				if(inst->m_isDeleted)
					continue;
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
				numEntityCmds++;
				inst->m_isAdded = false;
				inst->m_origTranslation = inst->m_translation;
				inst->m_origRotation = inst->m_rotation;
				continue;
			}

			if(inst->m_isDeleted){
				// D modelId oldX oldY oldZ
				fprintf(fe, "D %d %f %f %f\n",
					inst->m_objectId,
					inst->m_origTranslation.x,
					inst->m_origTranslation.y,
					inst->m_origTranslation.z);
				numEntityCmds++;
			}else if(inst->m_isDirty){
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
				numEntityCmds++;
				// Update orig so next reload knows where the game entity now is
				inst->m_origTranslation = inst->m_translation;
				inst->m_origRotation = inst->m_rotation;
			}
		}
		fclose(fe);

		if(numEntityCmds == 0)
			remove(entityReloadPath);
	}

	if(numStreamingIpls == 0 && numEntityCmds == 0){
		Toast(TOAST_SAVE, "Hot Reload: nothing to reload");
		return;
	}

	if(numStreamingIpls > 0 && numEntityCmds > 0)
		Toast(TOAST_SAVE, "Hot Reload: %d IPL(s) + %d entity(s)", numStreamingIpls, numEntityCmds);
	else if(numStreamingIpls > 0)
		Toast(TOAST_SAVE, "Hot Reload: %d streaming IPL(s)", numStreamingIpls);
	else
		Toast(TOAST_SAVE, "Hot Reload: %d entity(s)", numEntityCmds);
}

static void
uiMainmenu(void)
{
	if(ImGui::BeginMainMenuBar()){
		if(ImGui::BeginMenu("File")){
			if(ImGui::MenuItem("Save All IPLs", "Ctrl+S")){
				saveAllIpls();
				Toast(TOAST_SAVE, "Saved all IPL files");
			}
			if(ImGui::MenuItem("Test in Game", "Ctrl+G")){
				testInGame();
			}
			if(ImGui::MenuItem("Hot Reload", "Ctrl+R")){
				hotReloadIpls();
			}
			ImGui::Separator();
			if(ImGui::MenuItem("Exit", "Alt+F4")) sk::globals.quit = 1;
			ImGui::EndMenu();
		}
		if(ImGui::BeginMenu("Window")){
			if(ImGui::MenuItem("Time & Weather", "T", showTimeWeatherWindow)) { showTimeWeatherWindow ^= 1; }
			if(ImGui::MenuItem("View", "V", showViewWindow)) { showViewWindow ^= 1; }
			if(ImGui::MenuItem("Rendering", "R", showRenderingWindow)) { showRenderingWindow ^= 1; }
			if(ImGui::MenuItem("Object Info", "I", showInstanceWindow)) { showInstanceWindow ^= 1; }
			if(ImGui::MenuItem("Editor", "E", showEditorWindow)) { showEditorWindow ^= 1; }
			if(ImGui::MenuItem("Object Browser", "B", showBrowserWindow)) { showBrowserWindow ^= 1; }
			if(ImGui::MenuItem("Log ", nil, showLogWindow)) { showLogWindow ^= 1; }
			if(ImGui::MenuItem("Demo ", nil, showDemoWindow)) { showDemoWindow ^= 1; }
			if(ImGui::MenuItem("Help", nil, showHelpWindow)) { showHelpWindow ^= 1; }
			ImGui::Separator();
			if(ImGui::BeginMenu("Notifications")){
				uiNotificationSettings();
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}

		if(params.numAreas){
			ImGui::PushItemWidth(100);
			if(ImGui::BeginCombo("Area", params.areaNames[currentArea])){
				for(int n = 0; n < params.numAreas; n++){
					bool is_selected = n == currentArea;
					static char str[100];
					sprintf(str, "%d - %s", n, params.areaNames[n]);
					if(ImGui::Selectable(str, is_selected))
						currentArea = n;
					if(is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::PopItemWidth();
		}


		ImGui::Separator();
		ImGui::Text("UI");
		ImGui::SameLine();
		if(ImGui::SmallButton("-")) ImGui::GetIO().FontGlobalScale *= 0.9f;
		ImGui::SameLine();
		if(ImGui::SmallButton("+")) ImGui::GetIO().FontGlobalScale *= 1.1f;
		ImGui::Separator();
		ImGui::Text("%.3f ms/frame %.1f FPS", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		ImGui::EndMainMenuBar();
	}
}

static void
uiHelpWindow(void)
{
	ImGui::Begin("Help", &showHelpWindow);

	ImGui::BulletText("Camera controls:\n"
		"LMB: first person look around\n"
		"Ctrl+Alt+LMB; W/S: move forward/backward\n"
		"MMB: pan\n"
		"Alt+MMB: arc rotate around target\n"
		"Ctrl+Alt+MMB: zoom into target\n"
		"C: toggle viewer camera (longer far clip)"
		);
	ImGui::Separator();
	ImGui::BulletText("Selection: click on an object to select it,\n"
		"Shift+click to add to the selection,\n"
		"Alt+click to remove from the selection,\n"
		"Ctrl+click to toggle selection.");
	ImGui::BulletText("In the editor window, double click an instance to jump there,\n"
		"Right click a selection to deselect it.\n"
		"Right click a deleted instance to undelete it.");
	ImGui::BulletText("Use the filter in the instance list to find instances by name.");
	ImGui::Separator();
	ImGui::BulletText("Gizmo: W = Translate, Q = Rotate\n"
		"Select an object to manipulate it with the gizmo.");
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
	if(!isSA()) ImGui::Checkbox("Draw 2dfx", &gRenderEffects);
	ImGui::Checkbox("Draw Car Paths", &gRenderCarPaths);
	ImGui::Checkbox("Draw Ped Paths", &gRenderPedPaths);


	ImGui::Checkbox("Draw Water", &gRenderWater);
	if(gameversion == GAME_SA)
		ImGui::Checkbox("Play Animations", &gPlayAnimations);

	static int render = 0;
	ImGui::RadioButton("Render Normal", &render, 0);
	ImGui::RadioButton("Render only HD", &render, 1);
	ImGui::RadioButton("Render only LOD", &render, 2);
	gRenderOnlyHD = !!(render&1);
	gRenderOnlyLod = !!(render&2);
	ImGui::SliderFloat("Draw Distance", &TheCamera.m_LODmult, 0.5f, 3.0f, "%.3f");
	ImGui::Checkbox("Render all Timed Objects", &gNoTimeCull);
	if(params.numAreas)
		ImGui::Checkbox("Render all Areas", &gNoAreaCull);
}

static void
uiRendering(void)
{
	ImGui::Checkbox("Draw PostFX", &gRenderPostFX);
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

		if(obj->m_carPathIndex >= 0){
			PathNode *nd = Path::GetCarNode(obj->m_carPathIndex,0);
			ImGui::Text(nd->water ? "WaterPath" : "CarPath");
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
			ImGui::Text("Ped Path");
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
		uiFilteredInstanceList(obj);
	}else if(Path::selectedNode && Path::selectedNode->tabId == 1){
		int i = Path::selectedNode->idx;
		ImGui::Text(Path::selectedNode->water ? "WaterPath %d" : "CarPath %d", i);
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
		ImGui::Text("PedPath %d", i);
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

static const char *fxTypeNames[] = { "Light", "Particle", "LookAtPoint", "PedQueue", "SunGlare"};
static const char *flareTypeNames[] = { "None", "Sun", "Headlight" };

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
		ImGui::DragInt   ("Type",      &e->queue.type);
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
		ImGui::ColorButton("##col", mkColor(e->col),
				ImGuiColorEditFlags_NoTooltip |
				ImGuiColorEditFlags_NoBorder,
				ImVec2(12, 12));
		ImGui::SameLine();

		ImGui::TextDisabled("%2d", i);
		ImGui::SameLine();

		char label[64];
		snprintf(label, sizeof(label), "%s##eff%d", fxTypeNames[e->type], i);

		if(ImGui::Selectable(label, e == Effects::selectedEffect, ImGuiSelectableFlags_None, ImVec2(0, 0)))
			Effects::selectedEffect = e;
		if(ImGui::IsItemHovered()){
			Effects::guiHoveredEffect = e;
			if(ImGui::IsMouseClicked(1))
				Effects::selectedEffect = e;
			if(ImGui::IsMouseDoubleClicked(0))
				e->JumpTo(inst);
		}
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

	static char buf[MODELNAMELEN];
	strncpy(buf, obj->m_name, MODELNAMELEN);
	ImGui::InputText("Model##Inst", buf, MODELNAMELEN);

	ImGui::Text("IPL: %s", inst->m_file->name);

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
	changed |= ImGui::DragFloat3("Translation", (float*)&inst->m_translation, 0.1f);
	ImGui::Text("Rotation: %.3f %.3f %.3f %.3f",
		inst->m_rotation.x,
		inst->m_rotation.y,
		inst->m_rotation.z,
		inst->m_rotation.w);
	if(changed){
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

	if(params.numAreas)
		ImGui::Text("Area: %d", inst->m_area);

	if(params.objFlagset == GAME_SA){
		ImGui::Checkbox("Unimportant", &inst->m_isUnimportant);
		ImGui::Checkbox("Underwater", &inst->m_isUnderWater);
		ImGui::Checkbox("Tunnel", &inst->m_isTunnel);
		ImGui::Checkbox("Tunnel Transition", &inst->m_isTunnelTransition);
	}
}

static void
uiObjInfo(ObjectDef *obj)
{
	int i;
	TxdDef *txd;

	txd = GetTxdDef(obj->m_txdSlot);
	static char buf[MODELNAMELEN];

	ImGui::Text("ID: %d\n", obj->m_id);
	strncpy(buf, obj->m_name, MODELNAMELEN);
	ImGui::InputText("Model", buf, MODELNAMELEN);
	strncpy(buf, txd->name, MODELNAMELEN);
	ImGui::InputText("TXD", buf, MODELNAMELEN);

	ImGui::Text("IDE: %s", obj->m_file->name);
	if(obj->m_colModel && !obj->m_gotChildCol)
		ImGui::Text("COL: %s", obj->m_colModel->file->name);

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
//		ImGui::Checkbox("Is Subway", &obj->m_isSubway);
		ImGui::Checkbox("Ignore Light", &obj->m_ignoreLight);
		ImGui::Checkbox("No Z-write", &obj->m_noZwrite);
		ImGui::Checkbox("No shadows", &obj->m_noShadows);
		ImGui::Checkbox("Ignore Draw Dist", &obj->m_ignoreDrawDist);
		ImGui::Checkbox("Code Glass", &obj->m_isCodeGlass);
		ImGui::Checkbox("Artist Glass", &obj->m_isArtistGlass);
		break;

	case GAME_SA:
		ImGui::Checkbox("Draw Last", &obj->m_drawLast);
		ImGui::Checkbox("Additive Blend", &obj->m_additive);
		if(obj->m_additive) obj->m_drawLast = true;
		ImGui::Checkbox("No Z-write", &obj->m_noZwrite);
		ImGui::Checkbox("No shadows", &obj->m_noShadows);
		ImGui::Checkbox("No Backface Culling", &obj->m_noBackfaceCulling);
		if(obj->m_type == ObjectDef::ATOMIC){
			ImGui::Checkbox("Wet Road Effect", &obj->m_wetRoadReflection);
			ImGui::Checkbox("Don't collide with Flyer", &obj->m_dontCollideWithFlyer);

			static int flag = 0;
			flag = (int)obj->m_isCodeGlass |
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
		}else if(obj->m_type == ObjectDef::CLUMP){
			ImGui::Checkbox("Door", &obj->m_isDoor);
		}
		break;
	}

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

#include <vector>
std::vector<CamSetting> camSettings;

static void
loadCamSettings(void)
{
	CamSetting cam;
	char line[256], *p, *pp;
	FILE *f;

	f = fopen("camsettings.txt", "r");
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
		if(cam.area < 0 || cam.area >= params.numAreas)
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
saveCamSettings(void)
{
	FILE *f;

	f = fopen("camsettings.txt", "w");
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
	static char name[256] = "default";

	CPtrNode *p;
	ObjectInst *inst;
	ObjectDef *obj;
	TxdDef *txd;

	ImGui::Begin("Editor Window", &showEditorWindow);

	if(ImGui::TreeNode("Camera")){
		ImGui::InputFloat3("Cam position", (float*)&TheCamera.m_position);
		ImGui::InputFloat3("Cam target", (float*)&TheCamera.m_target);
		ImGui::SameLine();
		ImGui::Checkbox("show", &gDrawTarget);
		ImGui::SliderFloat("FOV", (float*)&TheCamera.m_fov, 1.0f, 150.0f, "%.0f");
		ImGui::Text("Far: %f", Timecycle::currentColours.farClp);
		ImGui::Text("mouse: %f %f", TheCamera.mx, TheCamera.my);

		ImGui::InputText("name", name, sizeof(name));
		if(ImGui::Button("Save")){
			CamSetting cam;
			strncpy(cam.name, name, sizeof(cam.name));
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
				strncpy(cam->name, name, sizeof(cam->name));
				getCurrentCamSetting(cam);
				saveCamSettings();
			}
			ImGui::SameLine();
			if(ImGui::Selectable(buf)){
				strncpy(name, cam->name, sizeof(name));
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

	if(ImGui::TreeNode("Placement")){
		ImGui::Checkbox("Snap to clicked object", &gPlaceSnapToObjects);
		ImGui::Checkbox("Snap to ground below", &gPlaceSnapToGround);
		ImGui::TextDisabled("Object snap uses the clicked collision surface.");
		ImGui::TextDisabled("Ground snap drops the placement point vertically as fallback.");
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
		static ImGuiTextFilter filter;
		static ImGuiTextFilter filter2;
		filter.Draw("Model (inc,-exc)"); ImGui::SameLine();
		if(ImGui::Button("Clear##Model"))
			filter.Clear();
		filter2.Draw("Txd (inc,-exc)"); ImGui::SameLine();
		if(ImGui::Button("Clear##Txd"))
			filter2.Clear();
		static bool highlight;
		ImGui::Checkbox("Highlight matches", &highlight);
		for(p = instances.first; p; p = p->next){
			inst = (ObjectInst*)p->item;
			obj = GetObjectDef(inst->m_objectId);
			txd = GetTxdDef(obj->m_txdSlot);
			if(filter.PassFilter(obj->m_name) && filter2.PassFilter(txd->name)){
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
					if(highlight)
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
	if(ImGui::TreeNode("Detached Car Paths")){
		for(int i = 0; nd = Path::GetDetachedCarNode(i,0); i++){
			static char str[20];
			sprintf(str, nd->water ? "WaterPath %d" : "CarPath %d", i);
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
	if(ImGui::TreeNode("Detached Ped Paths")){
		for(int i = 0; nd = Path::GetDetachedPedNode(i,0); i++){
			static char str[20];
			sprintf(str,"PedPath %d", i);
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
uiInstWindow(void)
{
	ImGui::Begin("Object Info", &showInstanceWindow);

	// Gizmo toolbar
	ImGui::Checkbox("Gizmo", &gGizmoEnabled);
	if(gGizmoEnabled){
		ImGui::SameLine();
		if(ImGui::RadioButton("Translate (W)", gGizmoMode == GIZMO_TRANSLATE))
			gGizmoMode = GIZMO_TRANSLATE;
		ImGui::SameLine();
		if(ImGui::RadioButton("Rotate (Q)", gGizmoMode == GIZMO_ROTATE))
			gGizmoMode = GIZMO_ROTATE;
		ImGui::Checkbox("Snap", &gGizmoSnap);
		if(gGizmoSnap){
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
		if(gGizmoMode == GIZMO_TRANSLATE){
			ImGui::Checkbox("Ground Follow While Dragging", &gDragFollowGround);
			ImGui::BeginDisabled(!gDragFollowGround);
			ImGui::Indent();
			ImGui::Checkbox("Align To Surface While Dragging", &gDragAlignToSurface);
			ImGui::Unindent();
			ImGui::EndDisabled();
		}
	}
	ImGui::Separator();

	if(selection.first){
		ObjectInst *inst = (ObjectInst*)selection.first->item;
		ObjectDef *obj = GetObjectDef(inst->m_objectId);
		if(ImGui::CollapsingHeader("Instance"))
			uiInstInfo(inst);
		if(ImGui::CollapsingHeader("Object"))
			uiObjInfo(obj);
		if(obj->m_numEffects)
			if(ImGui::CollapsingHeader("Effects"))
				uiFxInfo(inst);
		if(obj->m_carPathIndex >=0 || obj->m_pedPathIndex >= 0)
			if(ImGui::CollapsingHeader("Path"))
				uiPathInfo(inst);
	}else{
		if(Path::selectedNode)// && Path::selectedNode->isDetached())
		if(ImGui::CollapsingHeader("Path"))
			uiPathInfo(nil);

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
	ImGui::Begin("Object Browser", &showBrowserWindow);

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

			// Action buttons
			if(gPlaceMode){
				if(ImGui::Button("Exit Place Mode"))
					SpawnExitPlaceMode();
			}else{
				if(ImGui::Button("Place"))
					gPlaceMode = true;
			}
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
		if(ImGui::BeginTabItem("Categories")){
			static int selectedCat = -1;
			static ImGuiTextFilter catFilter;

			// Category dropdown
			static char catLabel[128] = "All Categories";
			if(ImGui::BeginCombo("##CatCombo", catLabel)){
				if(ImGui::Selectable("All Categories", selectedCat == -1)){
					selectedCat = -1;
					strcpy(catLabel, "All Categories");
				}
				static char lb[128];
				for(int c = 0; c < NUM_OBJ_CATEGORIES; c++){
					buildCategoryLabel(c, lb, sizeof(lb));
					bool isSel = (c == selectedCat);
					if(ImGui::Selectable(lb, isSel)){
						selectedCat = c;
						snprintf(catLabel, sizeof(catLabel), "%s", objCategories[c].name);
					}
				}
				ImGui::EndCombo();
			}

			catFilter.Draw("Filter##Cat");

			// Build filtered list
			numFiltered = 0;
			for(int i = 0; i < NUMOBJECTDEFS; i++){
				ObjectDef *obj = GetObjectDef(i);
				if(obj == nil) continue;
				if(selectedCat >= 0){
					int cat = GetObjectCategory(i);
					if(cat < 0 || !isCategoryOrChild(cat, selectedCat))
						continue;
				}
				if(!catFilter.PassFilter(obj->m_name)) continue;
				filtered[numFiltered++] = i;
			}
			ImGui::Text("%d objects", numFiltered);
			uiObjectList(filtered, numFiltered, selId);

			ImGui::EndTabItem();
		}

		// === IDE tab ===
		if(ImGui::BeginTabItem("IDE")){
			static const char *selectedIde = nil;
			static ImGuiTextFilter ideFilter;

			// Collect unique IDE file names
			static const char *ideFiles[512];
			static int numIdeFiles = 0;
			static bool ideCollected = false;
			if(!ideCollected){
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
				ideCollected = true;
			}

			// IDE dropdown
			const char *ideLabel = selectedIde ? selectedIde : "All IDE files";
			if(ImGui::BeginCombo("##IdeCombo", ideLabel)){
				if(ImGui::Selectable("All IDE files", selectedIde == nil))
					selectedIde = nil;
				for(int j = 0; j < numIdeFiles; j++){
					bool isSel = (selectedIde == ideFiles[j]);
					if(ImGui::Selectable(ideFiles[j], isSel))
						selectedIde = ideFiles[j];
				}
				ImGui::EndCombo();
			}

			ideFilter.Draw("Filter##Ide");

			numFiltered = 0;
			for(int i = 0; i < NUMOBJECTDEFS; i++){
				ObjectDef *obj = GetObjectDef(i);
				if(obj == nil) continue;
				if(selectedIde && (obj->m_file == nil ||
					strcmp(obj->m_file->name, selectedIde) != 0))
					continue;
				if(!ideFilter.PassFilter(obj->m_name)) continue;
				filtered[numFiltered++] = i;
			}
			ImGui::Text("%d objects", numFiltered);
			uiObjectList(filtered, numFiltered, selId);

			ImGui::EndTabItem();
		}

		// === Search tab ===
		if(ImGui::BeginTabItem("Search")){
			static ImGuiTextFilter searchFilter;
			searchFilter.Draw("Search##All");
			ImGui::SameLine();
			if(ImGui::Button("Clear##SearchClear"))
				searchFilter.Clear();

			numFiltered = 0;
			if(searchFilter.IsActive()){
				for(int i = 0; i < NUMOBJECTDEFS; i++){
					ObjectDef *obj = GetObjectDef(i);
					if(obj == nil) continue;
					if(!searchFilter.PassFilter(obj->m_name)) continue;
					filtered[numFiltered++] = i;
				}
			}
			ImGui::Text("%d results", numFiltered);
			uiObjectList(filtered, numFiltered, selId);

			ImGui::EndTabItem();
		}

		// === Favourites tab ===
		if(ImGui::BeginTabItem("Favourites")){
			static ImGuiTextFilter favFilter;
			favFilter.Draw("Filter##Fav");

			numFiltered = 0;
			for(int i = 0; i < NUMOBJECTDEFS; i++){
				if(!IsFavourite(i)) continue;
				ObjectDef *obj = GetObjectDef(i);
				if(obj == nil) continue;
				if(!favFilter.PassFilter(obj->m_name)) continue;
				filtered[numFiltered++] = i;
			}
			ImGui::Text("%d favourites", numFiltered);
			uiObjectList(filtered, numFiltered, selId);

			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

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
		camloaded = true;
	}

	Path::guiHoveredNode = nil;
	uiMainmenu();
	UpdaterDrawGui();

	// Copy/Paste
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

	if(!CPad::IsCtrlDown() && CPad::IsKeyJustDown('C')) gUseViewerCam = !gUseViewerCam;

	// Gizmo mode shortcuts
	if(CPad::IsKeyJustDown('W')) gGizmoMode = GIZMO_TRANSLATE;
	if(CPad::IsKeyJustDown('Q')) gGizmoMode = GIZMO_ROTATE;

	// Delete selected instances
	if(CPad::IsKeyJustDown(KEY_DEL) || CPad::IsKeyJustDown(KEY_BACKSP)){
		int count = 0;
		for(CPtrNode *p = selection.first; p; p = p->next) count++;
		if(count > 0){
			DeleteSelected();
			Toast(TOAST_DELETE, "Deleted %d instance(s)", count);
		}
	}

	// Undo/Redo
	if(CPad::IsCtrlDown() && CPad::IsKeyJustDown('Z')){
		Undo();
		Toast(TOAST_UNDO_REDO, "Undo");
	}
	if(CPad::IsCtrlDown() && CPad::IsKeyJustDown('Y')){
		Redo();
		Toast(TOAST_UNDO_REDO, "Redo");
	}

	// Ctrl+S to save all IPLs
	if(CPad::IsCtrlDown() && CPad::IsKeyJustDown('S')){
		saveAllIpls();
		Toast(TOAST_SAVE, "Saved all IPL files");
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
		ImGui::Begin("Time & Weather", &showTimeWeatherWindow);
		uiTimeWeather();
		ImGui::End();
	}

	if(!CPad::IsCtrlDown() && CPad::IsKeyJustDown('V')) showViewWindow ^= 1;
	if(showViewWindow){
		ImGui::Begin("View", &showViewWindow);
		uiView();
		ImGui::End();
	}

	if(!CPad::IsCtrlDown() && CPad::IsKeyJustDown('R')) showRenderingWindow ^= 1;
	if(showRenderingWindow){
		ImGui::Begin("Rendering", &showRenderingWindow);
		uiRendering();
		ImGui::End();
	}

	if(CPad::IsKeyJustDown('I')) showInstanceWindow ^= 1;
	if(showInstanceWindow) uiInstWindow();

	if(CPad::IsKeyJustDown('E')) showEditorWindow ^= 1;
	if(showEditorWindow) uiEditorWindow();

	if(CPad::IsKeyJustDown('B')){
		showBrowserWindow ^= 1;
		if(!showBrowserWindow && gPlaceMode)
			SpawnExitPlaceMode();
	}
	if(showBrowserWindow){
		uiBrowserWindow();
		// ImGui X button can set showBrowserWindow to false
		if(!showBrowserWindow && gPlaceMode)
			SpawnExitPlaceMode();
	}

	// Escape exits place mode
	if(CPad::IsKeyJustDown(KEY_ESC) && gPlaceMode)
		SpawnExitPlaceMode();

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

	uiToasts();

//	uiTest();
}
