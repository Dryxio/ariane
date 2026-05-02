// same as RW skeleton
enum Key
{
	// ascii...

	KEY_ESC   = 128,

	KEY_F1    = 129,
	KEY_F2    = 130,
	KEY_F3    = 131,
	KEY_F4    = 132,
	KEY_F5    = 133,
	KEY_F6    = 134,
	KEY_F7    = 135,
	KEY_F8    = 136,
	KEY_F9    = 137,
	KEY_F10   = 138,
	KEY_F11   = 139,
	KEY_F12   = 140,

	KEY_INS   = 141,
	KEY_DEL   = 142,
	KEY_HOME  = 143,
	KEY_END   = 144,
	KEY_PGUP  = 145,
	KEY_PGDN  = 146,

	KEY_UP    = 147,
	KEY_DOWN  = 148,
	KEY_LEFT  = 149,
	KEY_RIGHT = 150,

	// some stuff ommitted

	KEY_BACKSP = 168,
	KEY_TAB    = 169,
	KEY_CAPSLK = 170,
	KEY_ENTER  = 171,
	KEY_LSHIFT = 172,
	KEY_RSHIFT = 173,
	KEY_LCTRL  = 174,
	KEY_RCTRL  = 175,
	KEY_LALT   = 176,
	KEY_RALT   = 177,

	KEY_NULL,	// unused
	KEY_NUMKEYS,
};


class CControllerState
{
public:
	short leftX, leftY;
	short rightX, rightY;
	short l1, l2;
	short r1, r2;
	short up, down, left, right;
	short start, select;
	short square, triangle, cross, circle;
	short leftshock, rightshock;
	float getLeftX(void) { return leftX/32767.0f; };
	float getLeftY(void) { return leftY/32767.0f; };
	float getRightX(void) { return rightX/32767.0f; };
	float getRightY(void) { return rightY/32767.0f; };

	void Clear(void);
};

struct CMouseControllerState
{
	uint32 btns;	// bit 0-2 button 1-3
	int x, y;
};

class CPad
{
public:
	CControllerState NewState;
	CControllerState OldState;

	void Update(short n);

	static CPad Pads[2];
	static uint16 oldKeystates[KEY_NUMKEYS];
	static uint16 newKeystates[KEY_NUMKEYS];
	static uint16 tempKeystates[KEY_NUMKEYS];
	static CMouseControllerState oldMouseState;
	static CMouseControllerState newMouseState;
	static CMouseControllerState tempMouseState;
	static int clickState;
	static int clickx, clicky;
	static int clickbtn;

	static CPad *GetPad(int n) { return &Pads[n]; }
	static void UpdatePads(void);
	static bool IsKeyJustDown(int key);
	static bool IsKeyDown(int key);
	static bool IsShiftDown(void) { return IsKeyDown(KEY_LSHIFT) || IsKeyDown(KEY_RSHIFT); }
	static bool IsCtrlDown(void) { return IsKeyDown(KEY_LCTRL) || IsKeyDown(KEY_RCTRL); }
	static bool IsAltDown(void) { return IsKeyDown(KEY_LALT) || IsKeyDown(KEY_RALT); }
	static bool IsMButtonJustDown(int btn);
	static bool IsMButtonJustUp(int btn);
	static bool IsMButtonClicked(int btn);
	static bool IsMButtonDown(int btn);
};

#define IsButtonJustDown(pad, btn) \
	(!(pad)->OldState.btn && (pad)->NewState.btn)
