// Checks winmm.dll without a game or a pad:
//   - remap.cpp: an Xbox pad read in a PlayStation pad's numbers (buttons, right stick, triggers),
//     and which pads are recognised;
//   - the built DLL (path in argv[1]), loaded by this program: Windows' functions reached through
//     the stubs (named and by ordinal), and Alt+Enter swallowed on the thread that loaded it,
//     posted to a window that is never shown.
#include "../source/winmm/fill.h"
#include "../source/winmm/remap.h"
#include "../source/winmm/setup.h"
#include <cstdio>

static int failures = 0;
static void Expect(bool ok, const char* what)
{
	if (!ok)
		printf("FAIL %s\n", what);
	failures += !ok;
}

static JOYINFOEX XboxAtRest()
{
	JOYINFOEX j{};
	j.dwSize = sizeof(j);
	j.dwXpos = j.dwYpos = j.dwZpos = j.dwRpos = j.dwUpos = 32767;
	j.dwPOV = JOY_POVCENTERED;
	return j;
}

static void Remap()
{
	Expect(remap::Identify(0x054C, 14, JOYCAPS_HASZ | JOYCAPS_HASR | JOYCAPS_HASU | JOYCAPS_HASV | JOYCAPS_HASPOV)
		== remap::Kind::PlayStation, "Sony pad recognised");
	Expect(remap::Identify(0x045E, 10, JOYCAPS_HASZ | JOYCAPS_HASR | JOYCAPS_HASU | JOYCAPS_HASPOV | JOYCAPS_POV4DIR)
		== remap::Kind::Xbox, "XInput layout recognised");
	Expect(remap::Identify(0x0F0D, 10, JOYCAPS_HASZ | JOYCAPS_HASR | JOYCAPS_HASU | JOYCAPS_HASPOV)
		== remap::Kind::Xbox, "XInput layout recognised whatever the maker");
	Expect(remap::Identify(0x046D, 12, JOYCAPS_HASZ | JOYCAPS_HASR | JOYCAPS_HASPOV) == remap::Kind::Other,
		"another pad left alone");
	Expect(remap::Identify(0x045E, 10, JOYCAPS_HASZ | JOYCAPS_HASR | JOYCAPS_HASU | JOYCAPS_HASV | JOYCAPS_HASPOV)
		== remap::Kind::Other, "a V axis is not the XInput layout");

	JOYINFOEX j = XboxAtRest();
	remap::AsPlayStation(j);
	Expect(j.dwButtons == 0 && j.dwButtonNumber == 0, "at rest: no button");
	Expect(j.dwZpos == 32767 && j.dwRpos == 32767, "at rest: right stick centred on Z and R");
	Expect(j.dwUpos == 0 && j.dwVpos == 0, "at rest: triggers at 0 on U and V, as on a DualShock 4");

	j = XboxAtRest();
	j.dwButtons = 0x3FF;   // all ten
	remap::AsPlayStation(j);
	Expect(j.dwButtons == 0xF3F, "ten Xbox buttons -> square to R1, Share, Options, L3, R3");
	Expect(j.dwButtonNumber == 10, "button count follows");

	struct { DWORD xbox, playstation; const char* what; } buttons[] = {
		{ 1u << 0, 1u << 1, "A -> cross (2)" },     { 1u << 1, 1u << 2, "B -> circle (3)" },
		{ 1u << 2, 1u << 0, "X -> square (1)" },    { 1u << 3, 1u << 3, "Y -> triangle (4)" },
		{ 1u << 4, 1u << 4, "LB -> L1 (5)" },       { 1u << 5, 1u << 5, "RB -> R1 (6)" },
		{ 1u << 6, remap::kShare, "Back -> Share (9)" }, { 1u << 7, remap::kOptions, "Start -> Options (10)" },
		{ 1u << 8, 1u << 10, "L3 -> L3 (11)" },     { 1u << 9, 1u << 11, "R3 -> R3 (12)" },
	};
	for (const auto& b : buttons)
	{
		j = XboxAtRest();
		j.dwButtons = b.xbox;
		remap::AsPlayStation(j);
		Expect(j.dwButtons == b.playstation && j.dwButtonNumber == 1, b.what);
	}

	j = XboxAtRest();
	j.dwUpos = 65535;   // right stick fully right
	j.dwRpos = 0;       // and fully up
	remap::AsPlayStation(j);
	Expect(j.dwZpos == 65535 && j.dwRpos == 0, "right stick moves to Z (horizontal), R kept (vertical)");

	j = XboxAtRest();
	j.dwZpos = 65535;   // left trigger fully pressed
	remap::AsPlayStation(j);
	Expect(j.dwVpos == 65535 && j.dwUpos == 0, "left trigger -> V full");
	Expect(j.dwButtons == remap::kL2 && j.dwButtonNumber == 1, "left trigger -> L2 (7)");

	j = XboxAtRest();
	j.dwZpos = 0;       // right trigger fully pressed
	remap::AsPlayStation(j);
	Expect(j.dwUpos == 65534 && j.dwVpos == 0, "right trigger -> U full");
	Expect(j.dwButtons == remap::kR2, "right trigger -> R2 (8)");

	j = XboxAtRest();
	j.dwZpos = 32767 + 6000;   // left trigger touched: a fifth of its travel
	remap::AsPlayStation(j);
	Expect(j.dwVpos == 12000 && j.dwButtons == 0, "a light touch moves V without pressing L2");

	JOYCAPSA caps{};
	caps.wNumButtons = caps.wMaxButtons = 10;
	caps.wNumAxes = caps.wMaxAxes = 5;
	caps.wUmin = 0;
	caps.wUmax = 65535;
	caps.wCaps = JOYCAPS_HASZ | JOYCAPS_HASR | JOYCAPS_HASU | JOYCAPS_HASPOV | JOYCAPS_POV4DIR;
	remap::AsPlayStation(caps);
	Expect(caps.wNumButtons == 14 && caps.wMaxButtons >= 14, "caps: 14 buttons");
	Expect(caps.wNumAxes == 6 && (caps.wCaps & JOYCAPS_HASV) && (caps.wCaps & JOYCAPS_HASPOV), "caps: six axes, hat kept");
	Expect(caps.wZmax == 65535 && caps.wVmax == 65535, "caps: Z and V ranges");
}

// ---- fill.h --------------------------------------------------------------------------------

static void Fill()
{
	const RECT screen{0, 0, 2560, 1440};
	const LONG framed = WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPSIBLINGS;   // HP1's, read 0x14CF0000
	Expect(framed == 0x14CF0000, "HP1's window style as read");
	Expect(fill::Wanted(framed, true, 2560, 1440, screen), "a screen-sized picture loses its frame");
	Expect(fill::Wanted(framed, true, 3840, 2160, screen), "a larger one too");
	Expect(!fill::Wanted(framed, true, 2542, 1333, screen), "a smaller picture stays a window");
	Expect(!fill::Wanted(framed, true, 2560, 1333, screen), "as wide but not as tall: a window");
	Expect(!fill::Wanted(framed, false, 2560, 1440, screen), "a child or owned window is left alone");
	Expect(!fill::Wanted(framed & ~WS_VISIBLE, true, 2560, 1440, screen), "a hidden window is left alone");
	Expect(!fill::Wanted(WS_POPUP | WS_VISIBLE, true, 2560, 1440, screen), "already without a frame: nothing to do");
	const RECT second{2560, 0, 6400, 2160};   // a second screen, to the right
	Expect(fill::Wanted(framed, true, 3840, 2160, second), "measured against ITS screen");
	Expect(static_cast<DWORD>(fill::Borderless(framed)) == 0x94000000, "pop-up, visible and clip flags kept");
}

// ---- setup.h -------------------------------------------------------------------------------

static void Setup()
{
	// HP1's HP.exe at 0x1090C8CB (read 2026-10-07): both jumps land at 0x1090D1ED.
	uint8_t hp1[] = {
		0x8B, 0x0D, 0xDC, 0xEA, 0x95, 0x10, 0x83, 0x39, 0x00, 0x0F, 0x85, 0x13, 0x09, 0x00, 0x00,
		0x8B, 0x15, 0xD8, 0xEA, 0x95, 0x10, 0x83, 0x3A, 0x00, 0x0F, 0x84, 0x04, 0x09, 0x00, 0x00,
		0x8D, 0x8D, 0x58, 0xFC, 0xFF, 0xFF, 0xFF, 0x15, 0x18, 0xF1, 0x95, 0x10};
	uint8_t code[64] = {0xCC, 0xCC, 0xCC};
	memcpy(code + 3, hp1, sizeof(hp1));
	Expect(setup::Find(code, sizeof(code)) == 3 + 9, "HP1's jump found");
	Expect(setup::Find(code, 3 + setup::kLength - 1) == -1, "cut short: not found");
	uint8_t other[sizeof(code)];
	memcpy(other, code, sizeof(code));
	other[3 + 26] = 0x05;   // the second jump lands elsewhere: a look-alike
	Expect(setup::Find(other, sizeof(other)) == -1, "jumps to two places: not taken");
	memcpy(other, code, sizeof(code));
	other[3 + 31] = 0x4D;   // lea ecx, [ebp + disp8]: not the wizard's
	Expect(setup::Find(other, sizeof(other)) == -1, "another instruction after: not taken");
	setup::Skip(code + 12);
	Expect(code[12] == 0x90 && code[13] == 0xE9 && code[14] == 0x13 && code[15] == 0x09,
		"jne made nop + jmp, same distance");
}

// ---- The built DLL -------------------------------------------------------------------------

static void Dll(const char* path)
{
	HMODULE dll = LoadLibraryA(path);
	Expect(dll != nullptr, "winmm.dll loads");
	if (!dll)
		return;
	Expect(GetModuleHandleA("winmm.dll") == dll, "the process now knows it as winmm.dll");

	using TimeFn = DWORD(WINAPI*)();
	using PeriodFn = MMRESULT(WINAPI*)(UINT);
	using NumDevsFn = UINT(WINAPI*)();
	using PosExFn = MMRESULT(WINAPI*)(UINT, JOYINFOEX*);
	auto time = reinterpret_cast<TimeFn>(GetProcAddress(dll, "timeGetTime"));
	auto begin = reinterpret_cast<PeriodFn>(GetProcAddress(dll, "timeBeginPeriod"));
	auto end = reinterpret_cast<PeriodFn>(GetProcAddress(dll, "timeEndPeriod"));
	auto numDevs = reinterpret_cast<NumDevsFn>(GetProcAddress(dll, "joyGetNumDevs"));
	auto posEx = reinterpret_cast<PosExFn>(GetProcAddress(dll, "joyGetPosEx"));
	Expect(time && begin && end && numDevs && posEx, "exports found by name");
	Expect(GetProcAddress(dll, MAKEINTRESOURCEA(2)) != nullptr, "ordinal 2 exported");
	if (!(time && begin && end && numDevs && posEx))
		return;

	// Through a stub: the same clock as Windows' own, arguments and return value intact.
	const DWORD t0 = time();
	Sleep(30);
	const DWORD t1 = time();
	Expect(t1 - t0 >= 20 && t1 - t0 < 1000, "timeGetTime through a stub");
	Expect(begin(1) == TIMERR_NOERROR && end(1) == TIMERR_NOERROR, "timeBeginPeriod / timeEndPeriod through a stub");
	Expect(begin(0) == TIMERR_NOCANDO, "an error comes back as Windows gives it");
	Expect(numDevs() == 16, "joyGetNumDevs through a stub (16 slots)");

	// Written in C: answers like Windows for any slot, pad or not.
	JOYINFOEX j{};
	j.dwSize = sizeof(j);
	j.dwFlags = JOY_RETURNALL;
	const MMRESULT r = posEx(15, &j);
	Expect(r == JOYERR_NOERROR || r == JOYERR_UNPLUGGED || r == JOYERR_PARMS, "joyGetPosEx answers");
	Expect(posEx(99, &j) != JOYERR_NOERROR, "joyGetPosEx refuses a slot that cannot exist");
	for (UINT id = 0; id < 16; ++id)
	{
		j = JOYINFOEX{};
		j.dwSize = sizeof(j);
		j.dwFlags = JOY_RETURNALL;
		if (posEx(id, &j) == JOYERR_NOERROR)
			printf("pad %u: buttons 0x%lX, X %lu Y %lu Z %lu R %lu U %lu V %lu, hat %lu\n", id, j.dwButtons,
				j.dwXpos, j.dwYpos, j.dwZpos, j.dwRpos, j.dwUpos, j.dwVpos, j.dwPOV);
	}

	// Alt+Enter, posted to a window that is never shown, arrives as nothing; Alt+F4's key and a
	// plain Enter arrive as they were.
	HWND w = CreateWindowExA(0, "STATIC", "", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, nullptr, nullptr);
	Expect(w != nullptr, "hidden window");
	if (!w)
		return;
	PostMessageA(w, WM_SYSKEYDOWN, VK_RETURN, 1 | (0x1C << 16) | (1 << 29));
	PostMessageA(w, WM_SYSKEYDOWN, VK_F4, 1 | (0x3E << 16) | (1 << 29));
	PostMessageA(w, WM_KEYDOWN, VK_RETURN, 1 | (0x1C << 16));
	int altEnter = 0, altF4 = 0, enter = 0;
	MSG m;
	while (PeekMessageA(&m, w, 0, 0, PM_REMOVE))
	{
		altEnter += m.message == WM_SYSKEYDOWN && m.wParam == VK_RETURN;
		altF4 += m.message == WM_SYSKEYDOWN && m.wParam == VK_F4;
		enter += m.message == WM_KEYDOWN && m.wParam == VK_RETURN;
	}
	Expect(altEnter == 0, "Alt+Enter swallowed");
	Expect(altF4 == 1 && enter == 1, "other keys untouched");
	DestroyWindow(w);

	// A framed window whose picture covers the screen loses its frame and takes the screen's
	// rectangle; a smaller one keeps its frame. Fully transparent (layered, alpha 0) and never
	// activated: nothing shows on the screen of whoever runs the test.
	MONITORINFO mi{};
	mi.cbSize = sizeof(mi);
	GetMonitorInfoA(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
	const RECT s = mi.rcMonitor;
	auto framedWindow = [&](int clientW, int clientH) {
		RECT r{0, 0, clientW, clientH};
		AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
		HWND f = CreateWindowExA(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, "STATIC", "",
			WS_OVERLAPPEDWINDOW, s.left, s.top, r.right - r.left, r.bottom - r.top, nullptr, nullptr, nullptr, nullptr);
		if (f)
		{
			SetLayeredWindowAttributes(f, 0, 0, LWA_ALPHA);
			ShowWindow(f, SW_SHOWNOACTIVATE);
		}
		return f;
	};
	HWND big = framedWindow(s.right - s.left, s.bottom - s.top);
	Expect(big != nullptr, "screen-sized framed window");
	if (big)
	{
		RECT placed{};
		GetWindowRect(big, &placed);
		Expect(!(GetWindowLongA(big, GWL_STYLE) & WS_CAPTION), "its frame taken off (FillScreen)");
		Expect(EqualRect(&placed, &s) != 0, "it covers the screen exactly");
		DestroyWindow(big);
	}
	HWND small = framedWindow((s.right - s.left) / 2, (s.bottom - s.top) / 2);
	Expect(small != nullptr, "smaller framed window");
	if (small)
	{
		Expect((GetWindowLongA(small, GWL_STYLE) & WS_CAPTION) == WS_CAPTION, "a smaller picture keeps its frame");
		DestroyWindow(small);
	}
}

int main(int argc, char** argv)
{
	Remap();
	Fill();
	Setup();
	if (argc > 1)
		Dll(argv[1]);
	else
		printf("no DLL given: remap.cpp only\n");
	if (failures)
	{
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("winmm: all checks passed\n");
	return 0;
}
