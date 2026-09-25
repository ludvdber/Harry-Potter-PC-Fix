// Checks [Accio.Keys] parsing and the rewritten keyboard state, without a game.
#define DIRECTINPUT_VERSION 0x0800
#include "../source/accio.h"
#include <dinput.h>
#include <cstdarg>
#include <cstdio>

Settings g_cfg;
HMODULE g_self;
HWND g_gameWindow;
volatile LONG g_frames;
thread_local int g_internal;
void Log(const char* fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); }
bool ProcessInForeground() { return false; }
void RemapKeyboardEvents(DIDEVICEOBJECTDATA* events, DWORD count, DWORD size);

static int failures = 0;
static void Expect(bool ok, const char* what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	failures += !ok;
}

int main(int argc, char** argv)
{
	if (argc < 2)
		return 2;
	const char* ini = argv[1];
	FILE* f = nullptr;
	fopen_s(&f, ini, "w");
	fputs("[Accio.Keys]\nMoveUp=Z\nMoveLeft=Q\nMoveDown=S\nMoveRight=D\nCharm=MouseLeft, J\nExtremos=R\nNope=Z\nAccio=%%\nKey.C=Space\n", f);
	fclose(f);
	LoadKeyMap(ini);

	// Layout of this machine, as the player would type the names.
	const BYTE scanZ = static_cast<BYTE>(MapVirtualKeyA(LOBYTE(VkKeyScanA('Z')), MAPVK_VK_TO_VSC));
	const BYTE scanS = static_cast<BYTE>(MapVirtualKeyA(LOBYTE(VkKeyScanA('S')), MAPVK_VK_TO_VSC));
	printf("layout: Z is scan 0x%02X, S is scan 0x%02X (US Z = 0x2C, AZERTY Z = 0x11)\n", scanZ, scanS);

	BYTE keys[256] = {};
	keys[scanZ] = 0x80;
	RemapKeyboardState(keys);
	Expect(keys[DIK_UP] == 0x80, "Z pressed -> the game sees Up");
	Expect(keys[scanZ] == 0, "Z itself is no longer seen");

	BYTE k2[256] = {};
	k2[scanS] = 0x80;
	RemapKeyboardState(k2);
	Expect(k2[DIK_DOWN] == 0x80 && k2[DIK_S] == 0, "S moves down and no longer casts Extremos");

	BYTE k3[256] = {};
	k3[DIK_UP] = 0x80;
	RemapKeyboardState(k3);
	Expect(k3[DIK_UP] == 0x80, "the arrow keys keep working");

	BYTE k4[256] = {};
	k4[DIK_SPACE] = 0x80;
	RemapKeyboardState(k4);
	Expect(k4[DIK_C] == 0x80 && k4[DIK_SPACE] == 0, "Key.C=Space: Space becomes the game's C");

	DIDEVICEOBJECTDATA ev[2] = {};
	ev[0].dwOfs = scanZ;
	ev[1].dwOfs = DIK_ESCAPE;
	RemapKeyboardEvents(ev, 2, sizeof(DIDEVICEOBJECTDATA));
	Expect(ev[0].dwOfs == DIK_UP && ev[1].dwOfs == DIK_ESCAPE, "buffered events rewritten, others untouched");

	printf("%d failure(s)\n", failures);
	return failures;
}
