// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// winmm.dll for HP2 (Chamber of Secrets) and HP1 (Philosopher's Stone), next to the game's exe in
// its system folder. Both are Unreal Engine 1 games with no Direct3D 9 of their own (their
// renderer is Direct3D 11), so d3d9.dll cannot reach them; winmm.dll is the one Windows file their
// Core.dll and WinDrv.dll load, and it holds the pad. Five jobs, each one switched in winmm.ini
// (HP1's turns the pad keys and Alt+Enter off: no pad bindings there, Alt+Enter SEEN fine):
//   - HP1 only: its setup, run at EVERY start (a wizard, or a renderer test that writes D3DDrv into
//     HP.ini), is skipped (setup.h, which says why).
//   - A picture as large as the screen fills it: the window loses its title bar and border and
//     takes the screen's rectangle (fill.h, which says why).
//   - Options and Share (Start and Back on an Xbox pad) press Escape and Tab. The game opens its
//     menu on Escape and closes its map when Tab is RELEASED, both tested by key code: no binding
//     of a pad button can reach them (notes/HP2.md). The key goes down and up with the button.
//   - An Xbox pad is shown to the game with a PlayStation pad's numbers (remap.h), the ones the
//     launcher's bindings use.
//   - Alt+Enter is swallowed: the game's own switch rebuilds its menu at the wrong size and writes
//     that size into Game.ini.
// Every other export of winmm.dll jumps straight to Windows' own (exports.inc).
//
// Threads: the engine reads its pad from its one thread, the one that loads this DLL; the keys
// are posted to the window that has the focus on that thread. Nothing is posted when the game is
// not in front (no focus: GetFocus answers nothing).

#include "fill.h"
#include "remap.h"
#include "setup.h"
#include "../version.h"
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <share.h>

namespace
{
#define X(index, ordinal, name)
#define OWN(index, ordinal, name) constexpr int k_##name = index;
#define NONAME(index, ordinal)
#include "exports.inc"
#undef X
#undef OWN
#undef NONAME

// Windows' functions, by the index of exports.inc; jumped to by the stubs below.
void* g_proc[ACCIO_WINMM_EXPORTS];

HMODULE g_self;
HMODULE g_system;
FILE* g_log;
CRITICAL_SECTION g_logLock;

struct Settings
{
	bool log = true;
	bool fillScreen = true;
	bool skipSetup = false;
	bool blockAltEnter = true;
	bool xboxLayout = true;
	UINT shareKey = VK_TAB;
	UINT optionsKey = VK_ESCAPE;
} g_cfg;

constexpr UINT kPads = 16;   // WinMM's joystick ids

struct Pad
{
	bool known;          // identified since it was last plugged in
	remap::Kind kind;
	HWND held[2];        // where Share's and Options' key went down, until it goes up
};
Pad g_pads[kPads];

void Log(const char* fmt, ...)
{
	if (!g_log)
		return;
	EnterCriticalSection(&g_logLock);
	fprintf(g_log, "[%llu] ", GetTickCount64());
	va_list args;
	va_start(args, fmt);
	vfprintf(g_log, fmt, args);
	va_end(args);
	fflush(g_log);
	LeaveCriticalSection(&g_logLock);
}

// A file next to this DLL.
void Beside(const wchar_t* name, wchar_t* out)
{
	GetModuleFileNameW(g_self, out, MAX_PATH);
	wchar_t* slash = wcsrchr(out, L'\\');
	wchar_t* tail = slash ? slash + 1 : out;
	wcscpy_s(tail, MAX_PATH - (tail - out), name);
}

void LoadSettings(const wchar_t* ini)
{
	auto flag = [&](const wchar_t* section, const wchar_t* key, bool fallback) {
		return GetPrivateProfileIntW(section, key, fallback ? 1 : 0, ini) != 0;
	};
	g_cfg.log = flag(L"Accio.Window", L"Log", g_cfg.log);
	g_cfg.fillScreen = flag(L"Accio.Window", L"FillScreen", g_cfg.fillScreen);
	g_cfg.skipSetup = flag(L"Accio.Window", L"SkipSetup", g_cfg.skipSetup);
	g_cfg.blockAltEnter = flag(L"Accio.Window", L"BlockAltEnter", g_cfg.blockAltEnter);
	g_cfg.xboxLayout = flag(L"Accio.Controller", L"XboxLayout", g_cfg.xboxLayout);
	g_cfg.shareKey = GetPrivateProfileIntW(L"Accio.Controller", L"ShareKey", g_cfg.shareKey, ini) & 0xFF;
	g_cfg.optionsKey = GetPrivateProfileIntW(L"Accio.Controller", L"OptionsKey", g_cfg.optionsKey, ini) & 0xFF;
}

bool LoadSystemWinmm()
{
	wchar_t path[MAX_PATH];
	// SysWOW64 for this 32-bit game (redirected by Windows); under Wine, Wine's own winmm.
	const UINT n = GetSystemDirectoryW(path, MAX_PATH);
	if (!n || n + 11 >= MAX_PATH)
		return false;
	wcscat_s(path, L"\\winmm.dll");
	g_system = LoadLibraryW(path);
	if (!g_system)
	{
		Log("System winmm.dll not found at %ls (error %lu)\n", path, GetLastError());
		return false;
	}
	int missing = 0;
#define X(index, ordinal, name) \
	if (!(g_proc[index] = reinterpret_cast<void*>(GetProcAddress(g_system, #name)))) \
		Log("Not in the system winmm.dll: %s\n", #name), ++missing;
#define OWN X
#define NONAME(index, ordinal) \
	if (!(g_proc[index] = reinterpret_cast<void*>(GetProcAddress(g_system, MAKEINTRESOURCEA(ordinal))))) \
		Log("Not in the system winmm.dll: ordinal %d\n", ordinal), ++missing;
#include "exports.inc"
#undef X
#undef OWN
#undef NONAME
	Log("System winmm.dll: %ls, %d of %d functions\n", path, ACCIO_WINMM_EXPORTS - missing, ACCIO_WINMM_EXPORTS);
	return true;
}

// ---- Alt+Enter -----------------------------------------------------------------------------

// Sees every message the game's thread takes from its queue, before the game does. Alt+Enter
// arrives as WM_SYSKEYDOWN with VK_RETURN; made WM_NULL, it reaches neither the engine nor the
// renderer, and no WM_SYSCHAR follows. Alt itself still goes through.
LRESULT CALLBACK OnMessage(int code, WPARAM removal, LPARAM lParam)
{
	if (code == HC_ACTION)
	{
		MSG* msg = reinterpret_cast<MSG*>(lParam);
		if (msg->message == WM_SYSKEYDOWN && msg->wParam == VK_RETURN)
		{
			if (removal == PM_REMOVE && !(msg->lParam & (1 << 30)))
				Log("Alt+Enter: ignored (BlockAltEnter)\n");
			msg->message = WM_NULL;
		}
	}
	return CallNextHookEx(nullptr, code, removal, lParam);
}

// ---- Fill the screen -----------------------------------------------------------------------

bool g_fitting;   // our own SetWindowPos sends WM_WINDOWPOSCHANGED again

// After the game placed or showed a window: if its picture covers the screen, take the frame off
// and put it on the screen's rectangle (fill.h). The engine may give the frame back (a new size
// from its menu): the next WM_WINDOWPOSCHANGED takes it off again.
void Fill(HWND hwnd)
{
	if (g_fitting)
		return;
	const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
	const bool topLevel = !GetParent(hwnd) && !GetWindow(hwnd, GW_OWNER);
	RECT client{};
	MONITORINFO screen{};
	screen.cbSize = sizeof(screen);
	if (!GetClientRect(hwnd, &client)
		|| !GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &screen)
		|| !fill::Wanted(style, topLevel, client.right, client.bottom, screen.rcMonitor))
		return;
	const RECT& r = screen.rcMonitor;
	g_fitting = true;
	SetWindowLongW(hwnd, GWL_STYLE, fill::Borderless(style));
	SetWindowPos(hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
		SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
	g_fitting = false;
	Log("Window %ldx%ld: frame taken off, now on the screen at %ld,%ld (FillScreen)\n", client.right,
		client.bottom, r.left, r.top);
}

// Sees every message the game's thread SENT to its windows, after the window handled it.
LRESULT CALLBACK OnWindow(int code, WPARAM wParam, LPARAM lParam)
{
	if (code == HC_ACTION)
	{
		const CWPRETSTRUCT* m = reinterpret_cast<const CWPRETSTRUCT*>(lParam);
		if (m->message == WM_WINDOWPOSCHANGED || m->message == WM_SHOWWINDOW)
			Fill(m->hwnd);
	}
	return CallNextHookEx(nullptr, code, wParam, lParam);
}

// ---- HP1's setup ---------------------------------------------------------------------------

// The game's exe is mapped and has not run yet when its Core.dll loads this DLL: its code can be
// changed before it gets there. Only in the exe's own code; a pattern not found (HP2, another
// edition) changes nothing and says so.
void SkipSetup()
{
	auto* image = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(image + dos->e_lfanew);
	const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
	for (WORD n = 0; n < nt->FileHeader.NumberOfSections; ++n, ++section)
	{
		if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE))
			continue;
		uint8_t* code = image + section->VirtualAddress;
		const ptrdiff_t at = setup::Find(code, section->Misc.VirtualSize);
		if (at < 0)
			continue;
		DWORD old;
		if (!VirtualProtect(code + at, 2, PAGE_EXECUTE_READWRITE, &old))
		{
			Log("Setup: found at %p, but not writable (error %lu)\n", code + at, GetLastError());
			return;
		}
		setup::Skip(code + at);
		VirtualProtect(code + at, 2, old, &old);
		FlushInstructionCache(GetCurrentProcess(), code + at, 2);
		Log("Setup: skipped at %p (SkipSetup): no wizard, no renderer test\n", code + at);
		return;
	}
	Log("Setup: not found in this exe, left as it is\n");
}

// ---- Pad -----------------------------------------------------------------------------------

using DevCapsA = MMRESULT(WINAPI*)(UINT_PTR, JOYCAPSA*, UINT);
using DevCapsW = MMRESULT(WINAPI*)(UINT_PTR, JOYCAPSW*, UINT);
using PosEx = MMRESULT(WINAPI*)(UINT, JOYINFOEX*);

LPARAM KeyParam(UINT vk, bool up)
{
	LPARAM l = 1 | (static_cast<LPARAM>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)) << 16);
	if (up)
		l |= (1u << 30) | (1u << 31);
	return l;
}

// The key of one button (0 Share, 1 Options) follows it: down on the focused window, up on the
// window it went down on (even if the game is no longer in front: no key left stuck).
void Follow(Pad& pad, int which, UINT vk, bool down)
{
	HWND& held = pad.held[which];
	if (down && !held)
	{
		if (HWND focus = GetFocus())
		{
			PostMessageW(focus, WM_KEYDOWN, vk, KeyParam(vk, false));
			held = focus;
		}
	}
	else if (!down && held)
	{
		PostMessageW(held, WM_KEYUP, vk, KeyParam(vk, true));
		held = nullptr;
	}
}

void Release(Pad& pad)
{
	Follow(pad, 0, g_cfg.shareKey, false);
	Follow(pad, 1, g_cfg.optionsKey, false);
}

void Identify(UINT id, Pad& pad)
{
	JOYCAPSA caps{};
	auto real = reinterpret_cast<DevCapsA>(g_proc[k_joyGetDevCapsA]);
	pad.known = true;
	pad.kind = remap::Kind::Other;
	if (!real || real(id, &caps, sizeof(caps)) != JOYERR_NOERROR)
		return;
	pad.kind = remap::Identify(caps.wMid, caps.wNumButtons, caps.wCaps);
	const char* kind = pad.kind == remap::Kind::PlayStation ? "PlayStation layout"
		: pad.kind == remap::Kind::Xbox ? (g_cfg.xboxLayout ? "Xbox, shown with PlayStation numbers" : "Xbox, as it is")
		: "other: left as it is";
	Log("Pad %u: %04X:%04X \"%s\", %u buttons, %u axes, caps 0x%X -> %s\n", id, caps.wMid, caps.wPid,
		caps.szPname, caps.wNumButtons, caps.wNumAxes, caps.wCaps, kind);
}

bool Converted(remap::Kind kind) { return kind == remap::Kind::Xbox && g_cfg.xboxLayout; }
bool PlayStationNumbers(remap::Kind kind) { return kind == remap::Kind::PlayStation || Converted(kind); }

template <class Caps> MMRESULT Capabilities(UINT_PTR id, Caps* caps, UINT size, MMRESULT result)
{
	if (result != JOYERR_NOERROR || !caps || size < sizeof(Caps) || id >= kPads)
		return result;
	if (Converted(remap::Identify(caps->wMid, caps->wNumButtons, caps->wCaps)))
		remap::AsPlayStation(*caps);
	return result;
}
}

// ---- Exports (names in winmm.def) ----------------------------------------------------------

// A stub that jumps to Windows' function with the caller's own arguments and return address: it
// needs no prototype, and the stack is exactly as if the game had called Windows itself.
#define X(index, ordinal, name) \
	extern "C" __declspec(naked) void Accio_##name() { __asm jmp dword ptr [g_proc + index * 4] }
#define OWN(index, ordinal, name)
#define NONAME(index, ordinal) \
	extern "C" __declspec(naked) void Accio_ordinal##ordinal() { __asm jmp dword ptr [g_proc + index * 4] }
#include "exports.inc"
#undef X
#undef OWN
#undef NONAME

extern "C" MMRESULT WINAPI Accio_joyGetDevCapsA(UINT_PTR id, JOYCAPSA* caps, UINT size)
{
	auto real = reinterpret_cast<DevCapsA>(g_proc[k_joyGetDevCapsA]);
	return Capabilities(id, caps, size, real ? real(id, caps, size) : MMSYSERR_NODRIVER);
}

extern "C" MMRESULT WINAPI Accio_joyGetDevCapsW(UINT_PTR id, JOYCAPSW* caps, UINT size)
{
	auto real = reinterpret_cast<DevCapsW>(g_proc[k_joyGetDevCapsW]);
	return Capabilities(id, caps, size, real ? real(id, caps, size) : MMSYSERR_NODRIVER);
}

extern "C" MMRESULT WINAPI Accio_joyGetPosEx(UINT id, JOYINFOEX* info)
{
	auto real = reinterpret_cast<PosEx>(g_proc[k_joyGetPosEx]);
	const MMRESULT r = real ? real(id, info) : MMSYSERR_NODRIVER;
	if (id >= kPads || !info)
		return r;
	Pad& pad = g_pads[id];
	if (r != JOYERR_NOERROR)
	{
		// Unplugged: identified again when it answers, and no key left down.
		if (pad.known)
			Release(pad);
		pad.known = false;
		return r;
	}
	if (!pad.known)
		Identify(id, pad);
	if (Converted(pad.kind))
		remap::AsPlayStation(*info);
	if (PlayStationNumbers(pad.kind))
	{
		const DWORD buttons = info->dwButtons;
		DWORD taken = 0;
		if (g_cfg.shareKey)
		{
			Follow(pad, 0, g_cfg.shareKey, (buttons & remap::kShare) != 0);
			taken |= remap::kShare;
		}
		if (g_cfg.optionsKey)
		{
			Follow(pad, 1, g_cfg.optionsKey, (buttons & remap::kOptions) != 0);
			taken |= remap::kOptions;
		}
		// The game sees no button 9 or 10 for them: the key is the whole press.
		info->dwButtons = buttons & ~taken;
		info->dwButtonNumber = remap::CountButtons(info->dwButtons);
	}
	return r;
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, void*)
{
	if (reason != DLL_PROCESS_ATTACH)
		return TRUE;
	g_self = module;
	DisableThreadLibraryCalls(module);
	InitializeCriticalSection(&g_logLock);

	wchar_t path[MAX_PATH];
	Beside(L"winmm.ini", path);
	LoadSettings(path);
	if (g_cfg.log)
	{
		Beside(L"winmm_accio.log", path);
		g_log = _wfsopen(path, L"w", _SH_DENYWR);
	}
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	Log("Accio Launcher PC fix " ACCIO_VERSION_STR " (winmm.dll), %ls\n", path);
	Log("Settings: XboxLayout=%d ShareKey=%u OptionsKey=%u BlockAltEnter=%d FillScreen=%d SkipSetup=%d\n",
		g_cfg.xboxLayout, g_cfg.shareKey, g_cfg.optionsKey, g_cfg.blockAltEnter, g_cfg.fillScreen,
		g_cfg.skipSetup);

	// Every Windows has one. Without it the stubs would have nowhere to go: refuse to load, and
	// Windows says winmm.dll is broken, which is the truth.
	if (!LoadSystemWinmm())
		return FALSE;

	// Loaded by Core.dll when the game starts, so this is the game's own thread, the one that
	// will make the window and read its messages.
	if (g_cfg.skipSetup)
		SkipSetup();
	if (g_cfg.blockAltEnter)
	{
		if (SetWindowsHookExW(WH_GETMESSAGE, OnMessage, nullptr, GetCurrentThreadId()))
			Log("Alt+Enter: watched\n");
		else
			Log("Alt+Enter: hook refused (error %lu)\n", GetLastError());
	}
	if (g_cfg.fillScreen)
	{
		if (SetWindowsHookExW(WH_CALLWNDPROCRET, OnWindow, nullptr, GetCurrentThreadId()))
			Log("Fill the screen: watched\n");
		else
			Log("Fill the screen: hook refused (error %lu)\n", GetLastError());
	}
	return TRUE;
}
