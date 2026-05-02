#ifdef _WIN32
#include <Windows.h>
#include "III.h"

#ifdef RW_D3D9
#include <Xinput.h>

int keymap[256];
int pads[4];
int numPads;
int currentPad;

rw::EngineOpenParams engineOpenParams;
bool shouldClose = false;

bool
plWindowclosed(void)
{
	MSG msg;
	if(shouldClose) return shouldClose;
	if(PeekMessage(&msg, 0, 0, 0, PM_NOREMOVE) && msg.message == WM_QUIT){
		GetMessage(&msg, 0, 0, 0);
		shouldClose = true;
	}
	return shouldClose;
}

void
plHandleEvents(void)
{
	MSG msg;
	while(PeekMessage(&msg, 0, 0, 0, PM_REMOVE)){
		if(msg.message == WM_QUIT){
			shouldClose = true;
			break;
		}else{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
}

void
plCapturePad(int arg)
{
	currentPad = arg;
	return;
}

void
plUpdatePad(CControllerState *state)
{
	XINPUT_STATE xstate;
	int pad;

	pad = currentPad < numPads ? pads[currentPad] : -1;
	if(pad < 0 || XInputGetState(pad, &xstate) != ERROR_SUCCESS){
		memset(state, 0, sizeof(CControllerState));
		return;
	}

	state->leftX  = 0;
	state->leftY  = 0;
	state->rightX = 0;
	state->rightY = 0;
	if(xstate.Gamepad.sThumbLX > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE || xstate.Gamepad.sThumbLX < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)
		state->leftX = xstate.Gamepad.sThumbLX;
	if(xstate.Gamepad.sThumbLY > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE || xstate.Gamepad.sThumbLY < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)
		state->leftY = xstate.Gamepad.sThumbLY;
	if(xstate.Gamepad.sThumbRX > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE || xstate.Gamepad.sThumbRX < -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE)
		state->rightX = xstate.Gamepad.sThumbRX;
	if(xstate.Gamepad.sThumbRY > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE || xstate.Gamepad.sThumbRY < -XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE)
		state->rightY = xstate.Gamepad.sThumbRY;

	state->triangle = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_Y);
	state->circle = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_B);
	state->cross = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_A);
	state->square = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_X);
	state->l1 = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER);
	state->l2 = xstate.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
	state->leftshock = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB);
	state->r1 = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER);
	state->r2 = xstate.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
	state->rightshock = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB);
	state->select = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_BACK);
	state->start = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_START);
	state->up = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP);
	state->right = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
	state->down = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN);
	state->left = !!(xstate.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT);

}

int
plGetTimeInMS(void)
{
	return (int)GetTickCount();
}

static void
initkeymap(void)
{
	int i;
	for(i = 0; i < 256; i++)
		keymap[i] = KEY_NULL;
	keymap[VK_SPACE] = ' ';
	keymap[VK_OEM_7] = '\'';
	keymap[VK_OEM_COMMA] = ',';
	keymap[VK_OEM_MINUS] = '-';
	keymap[VK_OEM_PERIOD] = '.';
	keymap[VK_OEM_2] = '/';
	for(i = '0'; i <= '9'; i++)
		keymap[i] = i;
	keymap[VK_OEM_1] = ';';
	keymap[VK_OEM_NEC_EQUAL] = '=';
	for(i = 'A'; i <= 'Z'; i++)
		keymap[i] = i;
	keymap[VK_OEM_4] = '[';
	keymap[VK_OEM_5] = '\\';
	keymap[VK_OEM_6] = ']';
	keymap[VK_OEM_3] = '`';
	keymap[VK_ESCAPE] = KEY_ESC;
	keymap[VK_RETURN] = KEY_ENTER;
	keymap[VK_TAB] = KEY_TAB;
	keymap[VK_BACK] = KEY_BACKSP;
	keymap[VK_INSERT] = KEY_INS;
	keymap[VK_DELETE] = KEY_DEL;
	keymap[VK_RIGHT] = KEY_RIGHT;
	keymap[VK_LEFT] = KEY_LEFT;
	keymap[VK_DOWN] = KEY_DOWN;
	keymap[VK_UP] = KEY_UP;
	keymap[VK_PRIOR] = KEY_PGUP;
	keymap[VK_NEXT] = KEY_PGDN;
	keymap[VK_HOME] = KEY_HOME;
	keymap[VK_END] = KEY_END;
	keymap[VK_MODECHANGE] = KEY_CAPSLK;
	for(i = VK_F1; i <= VK_F24; i++)
		keymap[i] = i-VK_F1+KEY_F1;
	keymap[VK_LSHIFT] = KEY_LSHIFT;
	keymap[VK_LCONTROL] = KEY_LCTRL;
	keymap[VK_LMENU] = KEY_LALT;
	keymap[VK_RSHIFT] = KEY_RSHIFT;
	keymap[VK_RCONTROL] = KEY_RCTRL;
	keymap[VK_RMENU] = KEY_RALT;
}

void
plAttachInput(void)
{
	int i;
	XINPUT_STATE state;

	initkeymap();
	for(i = 0; i < 4; i++)
		if(XInputGetState(i, &state) == ERROR_SUCCESS)
			pads[numPads++] = i;
}


LRESULT CALLBACK
WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int buttons = 0;

	switch(msg){
	case WM_DESTROY:
		printf("DESTROY\n");
		PostQuitMessage(0);
		break;

	case WM_SYSKEYDOWN:
	case WM_KEYDOWN:
		if(wParam == VK_MENU){
			if(GetKeyState(VK_LMENU) & 0x8000) KeyDown(keymap[VK_LMENU]);
			if(GetKeyState(VK_RMENU) & 0x8000) KeyDown(keymap[VK_RMENU]);
		}else if(wParam == VK_CONTROL){
			if(GetKeyState(VK_LCONTROL) & 0x8000) KeyDown(keymap[VK_LCONTROL]);
			if(GetKeyState(VK_RCONTROL) & 0x8000) KeyDown(keymap[VK_RCONTROL]);
		}else if(wParam == VK_SHIFT){
			if(GetKeyState(VK_LSHIFT) & 0x8000) KeyDown(keymap[VK_LSHIFT]);
			if(GetKeyState(VK_RSHIFT) & 0x8000) KeyDown(keymap[VK_RSHIFT]);
		}else
			KeyDown(keymap[wParam]);
		break;

	case WM_SYSKEYUP:
	case WM_KEYUP:
		if(wParam == VK_MENU){
			if((GetKeyState(VK_LMENU) & 0x8000) == 0) KeyUp(keymap[VK_LMENU]);
			if((GetKeyState(VK_RMENU) & 0x8000) == 0) KeyUp(keymap[VK_RMENU]);
		}else if(wParam == VK_CONTROL){
			if((GetKeyState(VK_LCONTROL) & 0x8000) == 0) KeyUp(keymap[VK_LCONTROL]);
			if((GetKeyState(VK_RCONTROL) & 0x8000) == 0) KeyUp(keymap[VK_RCONTROL]);
		}else if(wParam == VK_SHIFT){
			if((GetKeyState(VK_LSHIFT) & 0x8000) == 0) KeyUp(keymap[VK_LSHIFT]);
			if((GetKeyState(VK_RSHIFT) & 0x8000) == 0) KeyUp(keymap[VK_RSHIFT]);
		}else
			KeyUp(keymap[wParam]);
		break;

	case WM_CHAR:
		if(wParam > 0 && wParam < 0x10000)
			CharInput(wParam);
		break;

	case WM_MOUSEMOVE:
		POINTS p = MAKEPOINTS(lParam);
		MouseMove(p.x, p.y);
		break;

	case WM_LBUTTONDOWN:
		buttons |= 1; goto mbtn;
	case WM_LBUTTONUP:
		buttons &= ~1; goto mbtn;
	case WM_MBUTTONDOWN:
		buttons |= 2; goto mbtn;
	case WM_MBUTTONUP:
		buttons &= ~2; goto mbtn;
	case WM_RBUTTONDOWN:
		buttons |= 4; goto mbtn;
	case WM_RBUTTONUP:
		buttons &= ~4;
	mbtn:
		MouseButton(buttons);
		break;

	case WM_SIZE:
		rw::Rect r;
		r.x = 0;
		r.y = 0;
		r.w = LOWORD(lParam);
		r.h = HIWORD(lParam);
		WindowResize(&r);
		break;

	case WM_CLOSE:
		printf("CLOSE\n");
		DestroyWindow(hwnd);
		break;
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

HWND
MakeWindow(HINSTANCE instance, int width, int height, const char *title)
{
	WNDCLASS wc;
	wc.style         = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc   = WndProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = instance;
	wc.hIcon         = LoadIcon(0, IDI_APPLICATION);
	wc.hCursor       = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wc.lpszMenuName  = 0;
	wc.lpszClassName = "librwD3D9";
	if(!RegisterClass(&wc)){
		MessageBox(0, "RegisterClass() - FAILED", 0, 0);
		return 0;
	}

	int offx = 100;
	int offy = 100;
	RECT rect;
	rect.left = 0;
	rect.top = 0;
	rect.right = width;
	rect.bottom = height;
	DWORD style = WS_OVERLAPPEDWINDOW;
	AdjustWindowRect(&rect, style, FALSE);
	rect.right += -rect.left;
	rect.bottom += -rect.top;
	HWND win;
	win = CreateWindow("librwD3D9", title, style,
		offx, offy, rect.right, rect.bottom, 0, 0, instance, 0);
	if(!win){
		MessageBox(0, "CreateWindow() - FAILED", 0, 0);
		return 0;
	}
	ShowWindow(win, SW_SHOW);
	UpdateWindow(win);
	return win;
}

int WINAPI
WinMain(HINSTANCE instance, HINSTANCE,
        PSTR cmdLine, int showCmd)
{
	AllocConsole();
	freopen("CONIN$", "r", stdin);
	freopen("CONOUT$", "w", stdout);
	freopen("CONOUT$", "w", stderr);

	globals.width = 640;
	globals.height = 480;
	globals.windowtitle = "III test";
	globals.quit = 0;

	HWND win = MakeWindow(instance, globals.width, globals.height, globals.windowtitle);
	if(win == 0){
		MessageBox(0, "MakeWindow() - FAILED", 0, 0);
		return 0;
	}
	engineOpenParams.window = win;

	SystemInit();

	GameInit();

	TheGame();

	return 0;
}
#endif

#ifdef RW_OPENGL
// temporary hack
#include <windows.h>
int main(int argc, char *argv[]);

int WINAPI
WinMain(HINSTANCE hinstance, HINSTANCE prevInstance,
        PSTR cmdLine, int showCmd)
{
	AllocConsole();
	freopen("CONIN$", "r", stdin);
	freopen("CONOUT$", "w", stdout);
	freopen("CONOUT$", "w", stderr);
	return main(__argc, __argv);
}
#endif

#endif
