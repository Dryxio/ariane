#include "euryopa.h"
#include "icons.h"
#include "font_data.h"

// Accent color — used for highlights, active elements, selection
static const ImVec4 kAccent       = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);  // soft blue
static const ImVec4 kAccentHover  = ImVec4(0.33f, 0.67f, 1.00f, 1.00f);
static const ImVec4 kAccentActive = ImVec4(0.20f, 0.50f, 0.88f, 1.00f);

void
SetupStyle(void)
{
	ImGuiStyle &style = ImGui::GetStyle();

	// ---- Geometry ----
	style.WindowRounding    = 6.0f;
	style.ChildRounding     = 4.0f;
	style.FrameRounding     = 4.0f;
	style.PopupRounding     = 4.0f;
	style.ScrollbarRounding = 6.0f;
	style.GrabRounding      = 3.0f;
	style.TabRounding       = 4.0f;

	style.WindowPadding     = ImVec2(10, 10);
	style.FramePadding      = ImVec2(6, 4);
	style.ItemSpacing       = ImVec2(8, 5);
	style.ItemInnerSpacing  = ImVec2(6, 4);
	style.ScrollbarSize     = 13.0f;
	style.GrabMinSize       = 10.0f;
	style.WindowBorderSize  = 1.0f;
	style.ChildBorderSize   = 1.0f;
	style.PopupBorderSize   = 1.0f;
	style.FrameBorderSize   = 0.0f;
	style.TabBorderSize     = 0.0f;

	style.WindowTitleAlign  = ImVec2(0.0f, 0.5f);
	style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);

	// ---- Colors ----
	ImVec4 *c = style.Colors;

	// Backgrounds
	c[ImGuiCol_WindowBg]             = ImVec4(0.11f, 0.11f, 0.13f, 0.95f);
	c[ImGuiCol_ChildBg]              = ImVec4(0.10f, 0.10f, 0.12f, 0.00f);
	c[ImGuiCol_PopupBg]              = ImVec4(0.12f, 0.12f, 0.14f, 0.96f);

	// Borders
	c[ImGuiCol_Border]               = ImVec4(0.22f, 0.22f, 0.26f, 0.60f);
	c[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

	// Text
	c[ImGuiCol_Text]                 = ImVec4(0.92f, 0.93f, 0.94f, 1.00f);
	c[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.50f, 0.54f, 1.00f);

	// Frames (input boxes, sliders, checkboxes)
	c[ImGuiCol_FrameBg]              = ImVec4(0.16f, 0.16f, 0.19f, 1.00f);
	c[ImGuiCol_FrameBgHovered]       = ImVec4(0.20f, 0.20f, 0.24f, 1.00f);
	c[ImGuiCol_FrameBgActive]        = ImVec4(0.24f, 0.24f, 0.28f, 1.00f);

	// Title bar
	c[ImGuiCol_TitleBg]              = ImVec4(0.09f, 0.09f, 0.11f, 1.00f);
	c[ImGuiCol_TitleBgActive]        = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
	c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.09f, 0.09f, 0.11f, 0.60f);

	// Menu bar
	c[ImGuiCol_MenuBarBg]            = ImVec4(0.13f, 0.13f, 0.15f, 1.00f);

	// Scrollbar
	c[ImGuiCol_ScrollbarBg]          = ImVec4(0.10f, 0.10f, 0.12f, 0.80f);
	c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.28f, 0.28f, 0.32f, 1.00f);
	c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35f, 0.35f, 0.40f, 1.00f);
	c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.42f, 0.42f, 0.48f, 1.00f);

	// Checkbox / Radio mark
	c[ImGuiCol_CheckMark]            = kAccent;

	// Slider
	c[ImGuiCol_SliderGrab]           = ImVec4(0.36f, 0.36f, 0.42f, 1.00f);
	c[ImGuiCol_SliderGrabActive]     = kAccent;

	// Buttons
	c[ImGuiCol_Button]               = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
	c[ImGuiCol_ButtonHovered]        = ImVec4(0.26f, 0.26f, 0.32f, 1.00f);
	c[ImGuiCol_ButtonActive]         = kAccentActive;

	// Headers (collapsing headers, selectable, menu items)
	c[ImGuiCol_Header]               = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
	c[ImGuiCol_HeaderHovered]        = ImVec4(0.24f, 0.24f, 0.30f, 1.00f);
	c[ImGuiCol_HeaderActive]         = kAccentActive;

	// Separators
	c[ImGuiCol_Separator]            = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);
	c[ImGuiCol_SeparatorHovered]     = kAccentHover;
	c[ImGuiCol_SeparatorActive]      = kAccent;

	// Resize grip
	c[ImGuiCol_ResizeGrip]           = ImVec4(0.22f, 0.22f, 0.26f, 0.40f);
	c[ImGuiCol_ResizeGripHovered]    = kAccentHover;
	c[ImGuiCol_ResizeGripActive]     = kAccent;

	// Tabs
	c[ImGuiCol_Tab]                  = ImVec4(0.13f, 0.13f, 0.16f, 1.00f);
	c[ImGuiCol_TabHovered]           = ImVec4(0.22f, 0.22f, 0.28f, 1.00f);
	c[ImGuiCol_TabSelected]          = ImVec4(0.18f, 0.18f, 0.24f, 1.00f);

	// Table
	c[ImGuiCol_TableHeaderBg]        = ImVec4(0.14f, 0.14f, 0.17f, 1.00f);
	c[ImGuiCol_TableBorderStrong]    = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);
	c[ImGuiCol_TableBorderLight]     = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
	c[ImGuiCol_TableRowBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	c[ImGuiCol_TableRowBgAlt]        = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);

	// Selection / Drag-drop
	c[ImGuiCol_TextSelectedBg]       = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);
	c[ImGuiCol_DragDropTarget]       = kAccent;

	// Nav highlight
	c[ImGuiCol_NavHighlight]         = kAccent;
	c[ImGuiCol_NavWindowingHighlight]= ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	c[ImGuiCol_NavWindowingDimBg]    = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);

	// Modal dimming
	c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.00f, 0.00f, 0.00f, 0.55f);
}

void
SetupFonts(void)
{
	ImGuiIO &io = ImGui::GetIO();
	float fontSize = 15.0f;

	// --- Main font: Inter (embedded) ---
	ImFontConfig fontCfg;
	fontCfg.FontDataOwnedByAtlas = false;
	io.Fonts->AddFontFromMemoryTTF(
		(void *)inter_font_data, inter_font_size,
		fontSize, &fontCfg);

	// --- Icon font: FontAwesome 6 (embedded, merged) ---
	ImFontConfig iconCfg;
	iconCfg.MergeMode = true;
	iconCfg.PixelSnapH = true;
	iconCfg.GlyphMinAdvanceX = fontSize;
	iconCfg.FontDataOwnedByAtlas = false;
	static const ImWchar iconRanges[] = { 0xe000, 0xf8ff, 0 };
	io.Fonts->AddFontFromMemoryTTF(
		(void *)fa_solid_font_data, fa_solid_font_size,
		fontSize - 1.0f, &iconCfg, iconRanges);
}
