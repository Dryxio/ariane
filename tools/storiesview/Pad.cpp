#include "storiesview.h"

CPad CPad::Pads[2];

uint16 CPad::oldKeystates[KEY_NUMKEYS];
uint16 CPad::newKeystates[KEY_NUMKEYS];
uint16 CPad::tempKeystates[KEY_NUMKEYS];

CMouseControllerState CPad::oldMouseState;
CMouseControllerState CPad::newMouseState;
CMouseControllerState CPad::tempMouseState;

int CPad::clickState;
int CPad::clickx, CPad::clicky;
int CPad::clickbtn;


void
CPad::UpdatePads(void)
{
	GetPad(0)->Update(0);
	GetPad(1)->Update(1);

	// keyboard
	memcpy(CPad::oldKeystates, CPad::newKeystates, sizeof(CPad::oldKeystates));
	memcpy(CPad::newKeystates, CPad::tempKeystates, sizeof(CPad::newKeystates));

	// mouse
	oldMouseState = newMouseState;
	newMouseState = tempMouseState;
	if(clickState == 2) clickState = 0;
	if(clickState == 0 && newMouseState.btns & (newMouseState.btns ^ oldMouseState.btns)){
		clickState = 1;
		clickx = newMouseState.x;
		clicky = newMouseState.y;
	}else if(clickState == 1 && newMouseState.btns == 0){
		if(newMouseState.x == clickx && newMouseState.y == clicky){
			clickState = 2;
			// get bit of leftmost released button
			clickbtn = oldMouseState.btns & ~(oldMouseState.btns-1);
			// get button number (this could be one lookup, but meh)
			static int logtab[] = { 0, 1, 2, 0, 3 };
			clickbtn = logtab[clickbtn&7];
		}else
			clickState = 0;
	}
}

void
CPad::Update(short n)
{
	plCapturePad(n);
	CPad::GetPad(n)->OldState = CPad::GetPad(n)->NewState;
	plUpdatePad(&CPad::GetPad(n)->NewState);
}

bool
CPad::IsKeyJustDown(int key)
{
	return oldKeystates[key] == 0 && newKeystates[key] == 1;
}

bool
CPad::IsKeyDown(int key)
{
	return newKeystates[key] == 1;
}

bool
CPad::IsMButtonJustDown(int btn)
{
	uint32 btnsjd = newMouseState.btns & (newMouseState.btns ^ oldMouseState.btns);
	if(btn > 0 && btn <= 3)
		return btnsjd & 1<<(btn-1);
	return 0;
}

bool
CPad::IsMButtonJustUp(int btn)
{
	uint32 btnsju = ~newMouseState.btns & (newMouseState.btns ^ oldMouseState.btns);
	if(btn > 0 && btn <= 3)
		return btnsju & 1<<(btn-1);
	return 0;
}

bool
CPad::IsMButtonClicked(int btn)
{
	return clickState == 2 && clickbtn == btn;
}

bool
CPad::IsMButtonDown(int btn)
{
	if(btn > 0 && btn <= 3)
		return newMouseState.btns & 1<<(btn-1);
	return 0;
}


void
CControllerState::Clear(void)
{
	leftX = 0;
	leftY = 0;
	rightX = 0;
	rightY = 0;
	l1 = 0;
	l2 = 0;
	r1 = 0;
	r2 = 0;
	up = 0;
	down = 0;
	left = 0;
	right = 0;
	start = 0;
	select = 0;
	square = 0;
	triangle = 0;
	cross = 0;
	circle = 0;
	leftshock = 0;
	rightshock = 0;
}
