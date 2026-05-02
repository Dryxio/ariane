#include "III.h"
#ifdef RW_GL3

rw::EngineOpenParams engineOpenParams;
GLFWwindow *glfwwindow;

//
// platform dependent implementations
//

int keymap[GLFW_KEY_LAST+1];
int numPads;
int currentPad;

int
plGetTimeInMS(void)
{
	return glfwGetTime()*1000;
}

bool
plWindowclosed(void)
{
	return glfwWindowShouldClose(glfwwindow) != 0;
}

void
plHandleEvents(void)
{
	glfwPollEvents();
}

void
plCapturePad(int n)
{
	currentPad = n;
}

void
plUpdatePad(CControllerState *state)
{
	if(currentPad >= numPads)
		return;

	int n = currentPad;
//	printf("joy: %s\n", glfwGetJoystickName(GLFW_JOYSTICK_1+n));
	int count;
	const float *axes = glfwGetJoystickAxes(GLFW_JOYSTICK_1+n, &count);
	state->leftX = axes[0] * 32767;
	state->leftY = axes[1] * 32767;
	state->rightX = axes[2] * 32767;
	state->rightY = axes[3] * 32767;
	const unsigned char *buttons = glfwGetJoystickButtons(GLFW_JOYSTICK_1+n, &count);
//	for(int i = 0; i < 6; i++)
//		printf("%f ", axes[i]);
//	printf("\n");
	if(strcmp(glfwGetJoystickName(GLFW_JOYSTICK_1+n), "Xbox 360 Controller") == 0){
		state->triangle   = buttons[3];
		state->circle     = buttons[1];
		state->cross      = buttons[0];
		state->square     = buttons[2];
		state->l1         = buttons[4];
		state->l2         = axes[4] > -0.8;
		state->leftshock  = buttons[8];
		state->r1         = buttons[5];
		state->r2         = axes[5] > -0.8;
		state->rightshock = buttons[9];
		state->select     = buttons[6];
		state->start      = buttons[7];
		state->up         = buttons[10];
		state->right      = buttons[11];
		state->down       = buttons[12];
		state->left       = buttons[13];
	}else{
		// linux dualshock 3
		state->triangle   = buttons[12];
		state->circle     = buttons[13];
		state->cross      = buttons[14];
		state->square     = buttons[15];
		state->l1         = buttons[10];
		state->l2         = buttons[8];
		state->leftshock  = buttons[1];
		state->r1         = buttons[11];
		state->r2         = buttons[9];
		state->rightshock = buttons[2];
		state->select     = buttons[0];
		state->start      = buttons[3];
		state->up         = buttons[4];
		state->right      = buttons[5];
		state->down       = buttons[6];
		state->left       = buttons[7];
	}
}

static void
initkeymap(void)
{
	int i;
	for(i = 0; i < GLFW_KEY_LAST+1; i++)
		keymap[i] = KEY_NULL;
	keymap[GLFW_KEY_SPACE] = ' ';
	keymap[GLFW_KEY_APOSTROPHE] = '\'';
	keymap[GLFW_KEY_COMMA] = ',';
	keymap[GLFW_KEY_MINUS] = '-';
	keymap[GLFW_KEY_PERIOD] = '.';
	keymap[GLFW_KEY_SLASH] = '/';
	keymap[GLFW_KEY_0] = '0';
	keymap[GLFW_KEY_1] = '1';
	keymap[GLFW_KEY_2] = '2';
	keymap[GLFW_KEY_3] = '3';
	keymap[GLFW_KEY_4] = '4';
	keymap[GLFW_KEY_5] = '5';
	keymap[GLFW_KEY_6] = '6';
	keymap[GLFW_KEY_7] = '7';
	keymap[GLFW_KEY_8] = '8';
	keymap[GLFW_KEY_9] = '9';
	keymap[GLFW_KEY_SEMICOLON] = ';';
	keymap[GLFW_KEY_EQUAL] = '=';
	keymap[GLFW_KEY_A] = 'A';
	keymap[GLFW_KEY_B] = 'B';
	keymap[GLFW_KEY_C] = 'C';
	keymap[GLFW_KEY_D] = 'D';
	keymap[GLFW_KEY_E] = 'E';
	keymap[GLFW_KEY_F] = 'F';
	keymap[GLFW_KEY_G] = 'G';
	keymap[GLFW_KEY_H] = 'H';
	keymap[GLFW_KEY_I] = 'I';
	keymap[GLFW_KEY_J] = 'J';
	keymap[GLFW_KEY_K] = 'K';
	keymap[GLFW_KEY_L] = 'L';
	keymap[GLFW_KEY_M] = 'M';
	keymap[GLFW_KEY_N] = 'N';
	keymap[GLFW_KEY_O] = 'O';
	keymap[GLFW_KEY_P] = 'P';
	keymap[GLFW_KEY_Q] = 'Q';
	keymap[GLFW_KEY_R] = 'R';
	keymap[GLFW_KEY_S] = 'S';
	keymap[GLFW_KEY_T] = 'T';
	keymap[GLFW_KEY_U] = 'U';
	keymap[GLFW_KEY_V] = 'V';
	keymap[GLFW_KEY_W] = 'W';
	keymap[GLFW_KEY_X] = 'X';
	keymap[GLFW_KEY_Y] = 'Y';
	keymap[GLFW_KEY_Z] = 'Z';
	keymap[GLFW_KEY_LEFT_BRACKET] = '[';
	keymap[GLFW_KEY_BACKSLASH] = '\\';
	keymap[GLFW_KEY_RIGHT_BRACKET] = ']';
	keymap[GLFW_KEY_GRAVE_ACCENT] = '`';
	keymap[GLFW_KEY_ESCAPE] = KEY_ESC;
	keymap[GLFW_KEY_ENTER] = KEY_ENTER;
	keymap[GLFW_KEY_TAB] = KEY_TAB;
	keymap[GLFW_KEY_BACKSPACE] = KEY_BACKSP;
	keymap[GLFW_KEY_INSERT] = KEY_INS;
	keymap[GLFW_KEY_DELETE] = KEY_DEL;
	keymap[GLFW_KEY_RIGHT] = KEY_RIGHT;
	keymap[GLFW_KEY_LEFT] = KEY_LEFT;
	keymap[GLFW_KEY_DOWN] = KEY_DOWN;
	keymap[GLFW_KEY_UP] = KEY_UP;
	keymap[GLFW_KEY_PAGE_UP] = KEY_PGUP;
	keymap[GLFW_KEY_PAGE_DOWN] = KEY_PGDN;
	keymap[GLFW_KEY_HOME] = KEY_HOME;
	keymap[GLFW_KEY_END] = KEY_END;
	keymap[GLFW_KEY_CAPS_LOCK] = KEY_CAPSLK;
	keymap[GLFW_KEY_SCROLL_LOCK] = KEY_NULL;
	keymap[GLFW_KEY_NUM_LOCK] = KEY_NULL;
	keymap[GLFW_KEY_PRINT_SCREEN] = KEY_NULL;
	keymap[GLFW_KEY_PAUSE] = KEY_NULL;

	keymap[GLFW_KEY_F1] = KEY_F1;
	keymap[GLFW_KEY_F2] = KEY_F2;
	keymap[GLFW_KEY_F3] = KEY_F3;
	keymap[GLFW_KEY_F4] = KEY_F4;
	keymap[GLFW_KEY_F5] = KEY_F5;
	keymap[GLFW_KEY_F6] = KEY_F6;
	keymap[GLFW_KEY_F7] = KEY_F7;
	keymap[GLFW_KEY_F8] = KEY_F8;
	keymap[GLFW_KEY_F9] = KEY_F9;
	keymap[GLFW_KEY_F10] = KEY_F10;
	keymap[GLFW_KEY_F11] = KEY_F11;
	keymap[GLFW_KEY_F12] = KEY_F12;
	keymap[GLFW_KEY_F13] = KEY_NULL;
	keymap[GLFW_KEY_F14] = KEY_NULL;
	keymap[GLFW_KEY_F15] = KEY_NULL;
	keymap[GLFW_KEY_F16] = KEY_NULL;
	keymap[GLFW_KEY_F17] = KEY_NULL;
	keymap[GLFW_KEY_F18] = KEY_NULL;
	keymap[GLFW_KEY_F19] = KEY_NULL;
	keymap[GLFW_KEY_F20] = KEY_NULL;
	keymap[GLFW_KEY_F21] = KEY_NULL;
	keymap[GLFW_KEY_F22] = KEY_NULL;
	keymap[GLFW_KEY_F23] = KEY_NULL;
	keymap[GLFW_KEY_F24] = KEY_NULL;
	keymap[GLFW_KEY_F25] = KEY_NULL;
	keymap[GLFW_KEY_KP_0] = KEY_NULL;
	keymap[GLFW_KEY_KP_1] = KEY_NULL;
	keymap[GLFW_KEY_KP_2] = KEY_NULL;
	keymap[GLFW_KEY_KP_3] = KEY_NULL;
	keymap[GLFW_KEY_KP_4] = KEY_NULL;
	keymap[GLFW_KEY_KP_5] = KEY_NULL;
	keymap[GLFW_KEY_KP_6] = KEY_NULL;
	keymap[GLFW_KEY_KP_7] = KEY_NULL;
	keymap[GLFW_KEY_KP_8] = KEY_NULL;
	keymap[GLFW_KEY_KP_9] = KEY_NULL;
	keymap[GLFW_KEY_KP_DECIMAL] = KEY_NULL;
	keymap[GLFW_KEY_KP_DIVIDE] = KEY_NULL;
	keymap[GLFW_KEY_KP_MULTIPLY] = KEY_NULL;
	keymap[GLFW_KEY_KP_SUBTRACT] = KEY_NULL;
	keymap[GLFW_KEY_KP_ADD] = KEY_NULL;
	keymap[GLFW_KEY_KP_ENTER] = KEY_NULL;
	keymap[GLFW_KEY_KP_EQUAL] = KEY_NULL;
	keymap[GLFW_KEY_LEFT_SHIFT] = KEY_LSHIFT;
	keymap[GLFW_KEY_LEFT_CONTROL] = KEY_LCTRL;
	keymap[GLFW_KEY_LEFT_ALT] = KEY_LALT;
	keymap[GLFW_KEY_LEFT_SUPER] = KEY_NULL;
	keymap[GLFW_KEY_RIGHT_SHIFT] = KEY_RSHIFT;
	keymap[GLFW_KEY_RIGHT_CONTROL] = KEY_RCTRL;
	keymap[GLFW_KEY_RIGHT_ALT] = KEY_RALT;
	keymap[GLFW_KEY_RIGHT_SUPER] = KEY_NULL;
	keymap[GLFW_KEY_MENU] = KEY_NULL;
}

static void
keypress(GLFWwindow *window, int key, int scancode, int action, int mods)
{
	if(key >= 0 && key <= GLFW_KEY_LAST){
		if(action == GLFW_RELEASE) KeyUp(keymap[key]);
		if(action == GLFW_PRESS)   KeyDown(keymap[key]);
	}
}

void
plAttachInput(void)
{
	initkeymap();

	glfwSetKeyCallback(glfwwindow, keypress);

	numPads = 0;
	for(int i = 0; i < 16; i++){
		int present = glfwJoystickPresent(GLFW_JOYSTICK_1+i);
		if(present)
			numPads++;
	}
}

int
main(int argc, char *argv[])
{
	globals.width = 640;
	globals.height = 480;
	globals.windowtitle = "III test";
	globals.quit = 0;

	engineOpenParams.window = &glfwwindow;
	engineOpenParams.width = globals.width;
	engineOpenParams.height = globals.height;
	engineOpenParams.windowtitle = globals.windowtitle;

	SystemInit();

	GameInit();

	TheGame();

	return 0;
}

#endif
