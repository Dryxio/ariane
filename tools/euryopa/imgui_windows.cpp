// Windows/D3D9 adapter for detachable ImGui panels. The other platforms keep
// librw's existing renderer and input backend.
#if defined(_WIN32) && defined(RW_D3D9)
#define WITH_D3D
#include "euryopa.h"
#include "imgui/imgui_internal.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_dx9.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

void
InitializeEditorWindowDpi(void)
{
	// INITIALIZE runs before the skeleton creates the main native window.
	ImGui_ImplWin32_EnableDpiAwareness();
}

static bool bringPanelsHome;
void
BringEditorPanelsHome(void)
{
	bringPanelsHome = true;
}

static void
RestorePanelPositions(void)
{
	if(!bringPanelsHome)
		return;
	bringPanelsHome = false;
	ImGuiViewportP *viewport = (ImGuiViewportP*)ImGui::GetMainViewport();
	int index = 0;
	for(ImGuiWindow *window : ImGui::GetCurrentContext()->Windows){
		if(window->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_NoSavedSettings))
			continue;
		float offset = 20.0f + (index++ % 6) * 24.0f;
		ImVec2 size(ImMin(window->SizeFull.x, ImMax(100.0f, viewport->WorkSize.x - offset)),
		            ImMin(window->SizeFull.y, ImMax(100.0f, viewport->WorkSize.y - offset)));
		ImGui::SetWindowPos(window, ImVec2(viewport->WorkPos.x + offset, viewport->WorkPos.y + offset), ImGuiCond_Always);
		ImGui::SetWindowSize(window, size, ImGuiCond_Always);
		ImGui::SetWindowViewport(window, viewport);
	}
}

static HWND mainWindow;
static WNDPROC previousWindowProc;
static void (*previousReleaseResources)(void);
static bool deviceInvalidatedAfterNewFrame;

static LRESULT CALLBACK
EditorWindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
	// Win32 owns ImGui input, while the skeleton still supplies client-local
	// coordinates and events to the 3D editor. Do not feed ImGui twice.
	ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam);
	return CallWindowProc(previousWindowProc, hwnd, message, wparam, lparam);
}

static void*
ResolveEditorTexture(ImTextureID id)
{
	rw::Texture *texture = (rw::Texture*)(uintptr_t)id;
	return texture && texture->raster ? GETD3DRASTEREXT(texture->raster)->texture : nil;
}

static void
ReleaseGuiDeviceResources(void)
{
	deviceInvalidatedAfterNewFrame = true;
	ImGui_ImplDX9_InvalidateDeviceObjects();
	if(previousReleaseResources)
		previousReleaseResources();
}

bool
ImGui_ImplRW_Init(void)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	mainWindow = (HWND)engineOpenParams.window;
	if(!ImGui_ImplWin32_Init(mainWindow)){
		ImGui::DestroyContext();
		return false;
	}
	if(!ImGui_ImplDX9_Init(rw::d3d::d3ddevice)){
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		return false;
	}
	ImGui_ImplDX9_SetTextureResolver(ResolveEditorTexture);
	SetLastError(0);
	previousWindowProc = (WNDPROC)SetWindowLongPtr(mainWindow, GWLP_WNDPROC, (LONG_PTR)EditorWindowProc);
	if(!previousWindowProc){
		ImGui_ImplDX9_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		return false;
	}
	previousReleaseResources = rw::d3d::releaseDeviceResources;
	rw::d3d::releaseDeviceResources = ReleaseGuiDeviceResources;

	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	io.ConfigViewportsNoTaskBarIcon = true;
	io.ConfigDpiScaleFonts = true;
	io.ConfigDpiScaleViewports = true;
	io.ConfigWindowsMoveFromTitleBarOnly = true;
	return true;
}

void
ImGui_ImplRW_Shutdown(void)
{
	if(!ImGui::GetCurrentContext())
		return;
	// Save before platform windows are destroyed, preserving monitor positions.
	ImGuiIO &io = ImGui::GetIO();
	if(io.IniFilename)
		ImGui::SaveIniSettingsToDisk(io.IniFilename);
	rw::d3d::releaseDeviceResources = previousReleaseResources;
	if(IsWindow(mainWindow) && previousWindowProc)
		SetWindowLongPtr(mainWindow, GWLP_WNDPROC, (LONG_PTR)previousWindowProc);
	ImGui_ImplDX9_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	previousWindowProc = nil;
	mainWindow = nil;
}

void
ImGui_ImplRW_NewFrame(float timeDelta)
{
	if(rw::d3d::d3ddevice->TestCooperativeLevel() == D3D_OK)
		ImGui_ImplDX9_CreateDeviceObjects();
	RestorePanelPositions();
	ImGui_ImplDX9_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::GetIO().DeltaTime = timeDelta > 0.0f ? timeDelta : 1.0f/60.0f;
	ImGui::NewFrame();
	deviceInvalidatedAfterNewFrame = false;
}

sk::EventStatus
ImGuiEventHandler(sk::Event, void*)
{
	return sk::EVENTNOTPROCESSED;
}

void
ImGui_ImplRW_RenderDrawLists(ImDrawData *drawData)
{
	if(!deviceInvalidatedAfterNewFrame && rw::d3d::d3ddevice->TestCooperativeLevel() == D3D_OK)
		ImGui_ImplDX9_RenderDrawData(drawData);
}

void
RenderDetachedEditorWindows(void)
{
	// librw presents (and may Reset) before this call. Secondary draws must have
	// their own scene, and restore device state before the next librw frame.
	if(rw::d3d::d3ddevice->TestCooperativeLevel() == D3D_OK)
		ImGui_ImplDX9_CreateDeviceObjects();
	// Required after every ImGui frame, including frames with a lost device.
	// Swap-chain creation tolerates failure and is retried after recovery.
	ImGui::UpdatePlatformWindows();
	// Reset invalidates font texture references in this frame's draw data.
	// The next NewFrame rebuilds them; don't render stale commands meanwhile.
	if(deviceInvalidatedAfterNewFrame || rw::d3d::d3ddevice->TestCooperativeLevel() != D3D_OK)
		return;
	if(SUCCEEDED(rw::d3d::d3ddevice->BeginScene())){
		ImGui::RenderPlatformWindowsDefault();
		rw::d3d::d3ddevice->EndScene();
	}
}
#endif
