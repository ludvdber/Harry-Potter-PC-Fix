// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// The game window. These engines only know exclusive full screen, where Alt+Tab destroys the
// device and freezes the game; here they get a window the size of the monitor instead, and are
// told they are still in front when another window is.

#include "hooks.h"
#include <cstring>

namespace
{
WNDPROC g_gameProc = nullptr;
HWND g_subclassed = nullptr;

// The monitor the window is on, or the primary one when asked.
MONITORINFO MonitorOf(HWND hwnd)
{
	MONITORINFO mi = { sizeof(mi) };
	HMONITOR m = (!g_cfg.primaryMonitor && hwnd)
		? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)
		: MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
	GetMonitorInfoA(m, &mi);
	return mi;
}

bool Ours(HWND other)
{
	if (!other)
		return false;
	DWORD pid = 0;
	GetWindowThreadProcessId(other, &pid);
	return pid == GetCurrentProcessId();
}

// The engines stop their clock on any sign that another program is in front. Measured on HP6
// (2026-09-25): hiding WM_ACTIVATEAPP alone left the game frozen, 0 frames while away. These
// four messages are the signs; each is kept from the game when the window taking over belongs to
// another process. Swallowed outright, not passed to Windows' default handling either, which
// would answer with the same news.
bool IsLeaving(UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg)
	{
	case WM_ACTIVATE:    return LOWORD(wp) == WA_INACTIVE && !Ours(reinterpret_cast<HWND>(lp));
	case WM_KILLFOCUS:   return !Ours(reinterpret_cast<HWND>(wp));
	case WM_ACTIVATEAPP: return !wp;
	case WM_NCACTIVATE:  return !wp;
	default:             return false;
	}
}

LRESULT CALLBACK GameWindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	if (g_cfg.keepRunning && IsLeaving(msg, wp, lp))
	{
		if (msg == WM_ACTIVATEAPP)
			Log("Window: another program in front, the game is not told (frame %ld)\n", g_frames);
		return 0;
	}
	return CallWindowProcA(g_gameProc, hwnd, msg, wp, lp);
}

void Subclass(HWND hwnd)
{
	if (!g_cfg.keepRunning || hwnd == g_subclassed)
		return;
	g_gameProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(GameWindowProc)));
	g_subclassed = hwnd;
	Log("Window: %p subclassed\n", hwnd);
}

bool GameWindowAlive()
{
	return g_gameWindow && IsWindow(g_gameWindow);
}

// The three ways to ask "who is in front", as the game asks them. While the game window exists,
// the answer is the game window.
HWND WINAPI ForegroundForGame()
{
	return GameWindowAlive() ? g_gameWindow : GetForegroundWindow();
}

// These two answer per thread, and answer nothing on a thread whose window is not active: only
// that empty answer, on the game window's own thread, is replaced.
HWND WINAPI ActiveWindowForGame()
{
	HWND real = GetActiveWindow();
	if (!real && GameWindowAlive() && GetWindowThreadProcessId(g_gameWindow, nullptr) == GetCurrentThreadId())
		return g_gameWindow;
	return real;
}

HWND WINAPI FocusForGame()
{
	HWND real = GetFocus();
	if (!real && GameWindowAlive() && GetWindowThreadProcessId(g_gameWindow, nullptr) == GetCurrentThreadId())
		return g_gameWindow;
	return real;
}

struct Substitute
{
	const char* name;
	void* function;
};
const Substitute kSubstitutes[] = {
	{ "GetForegroundWindow", reinterpret_cast<void*>(ForegroundForGame) },
	{ "GetActiveWindow", reinterpret_cast<void*>(ActiveWindowForGame) },
	{ "GetFocus", reinterpret_cast<void*>(FocusForGame) },
};

using GetProcAddressFn = FARPROC(WINAPI*)(HMODULE, LPCSTR);
GetProcAddressFn g_getProcAddress = nullptr;

FARPROC WINAPI GetProcAddressForGame(HMODULE module, LPCSTR name)
{
	// Ordinals are small integers, never pointers to a name.
	if (reinterpret_cast<ULONG_PTR>(name) > 0xFFFF && module == GetModuleHandleA("user32.dll"))
		for (const Substitute& s : kSubstitutes)
			if (strcmp(name, s.name) == 0)
				return reinterpret_cast<FARPROC>(s.function);
	return g_getProcAddress(module, name);
}
}

void InstallFocusHooks()
{
	if (!g_cfg.keepRunning)
		return;
	HMODULE exe = GetModuleHandleA(nullptr);
	for (const Substitute& s : kSubstitutes)
		if (RedirectImport(exe, "user32.dll", s.name, s.function))
			Log("Window: %s answered for the game\n", s.name);
	g_getProcAddress = reinterpret_cast<GetProcAddressFn>(
		RedirectImport(exe, "kernel32.dll", "GetProcAddress", reinterpret_cast<void*>(GetProcAddressForGame)));
	if (!g_getProcAddress)
		g_getProcAddress = GetProcAddress;
}

void PrepareWindow(D3DPRESENT_PARAMETERS* pp, HWND focusWindow)
{
	if (!pp)
		return;
	HWND hwnd = pp->hDeviceWindow ? pp->hDeviceWindow : focusWindow;
	if (hwnd)
		g_gameWindow = hwnd;
	if (!g_cfg.windowed)
		return;

	pp->Windowed = TRUE;
	pp->FullScreen_RefreshRateInHz = 0; // must be 0 in a window
	if (!hwnd)
		return;

	const MONITORINFO mi = MonitorOf(hwnd);
	const RECT& mon = mi.rcMonitor;
	const int monW = mon.right - mon.left, monH = mon.bottom - mon.top;

	LONG style = GetWindowLongA(hwnd, GWL_STYLE);
	LONG exStyle = GetWindowLongA(hwnd, GWL_EXSTYLE) & ~WS_EX_TOPMOST;
	const LONG frame = WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_DLGFRAME | WS_BORDER;
	const LONG exFrame = WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE;
	switch (g_cfg.windowStyle)
	{
	case 1: // borderless, covering the monitor
	case 4: // borderless, the size of the image
		style = (style & ~frame & ~(WS_MINIMIZE | WS_MAXIMIZE)) | WS_POPUP;
		exStyle &= ~exFrame;
		break;
	case 2: // ordinary window
		style = (style & ~(WS_POPUP | WS_THICKFRAME | WS_MAXIMIZEBOX)) | WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
		break;
	case 3: // resizable window
		style = (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW;
		break;
	default:
		break;
	}
	if (g_cfg.windowStyle)
	{
		SetWindowLongA(hwnd, GWL_STYLE, style);
		SetWindowLongA(hwnd, GWL_EXSTYLE, exStyle | WS_EX_APPWINDOW);
	}

	int x, y, w, h;
	if (g_cfg.windowStyle == 1)
	{
		x = mon.left; y = mon.top; w = monW; h = monH;
	}
	else
	{
		// The client area is the image: the frame goes around it.
		RECT r = { 0, 0, static_cast<LONG>(pp->BackBufferWidth), static_cast<LONG>(pp->BackBufferHeight) };
		AdjustWindowRectEx(&r, style, FALSE, exStyle);
		w = r.right - r.left;
		h = r.bottom - r.top;
		RECT now = {};
		GetWindowRect(hwnd, &now);
		x = now.left; y = now.top;
		if (g_cfg.centerWindow)
		{
			x = mon.left + (monW - w) / 2;
			y = mon.top + (monH - h) / 2;
		}
	}
	SetWindowPos(hwnd, g_cfg.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, x, y, w, h,
		SWP_SHOWWINDOW | SWP_FRAMECHANGED);
	Log("Window: %p style %d, %dx%d at %d,%d, image %ux%u\n", hwnd, g_cfg.windowStyle, w, h, x, y,
		pp->BackBufferWidth, pp->BackBufferHeight);
	Subclass(hwnd);
}
