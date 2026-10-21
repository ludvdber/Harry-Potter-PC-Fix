// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// winmm.dll for HP2 (Chamber of Secrets) and HP1 (Philosopher's Stone), next to the game's exe in
// its system folder. Both are Unreal Engine 1 games with no Direct3D 9 of their own (their
// renderer is Direct3D 11), so d3d9.dll cannot reach them; winmm.dll is the one Windows file their
// Core.dll and WinDrv.dll load, and it holds the pad. Seven jobs, each one switched in winmm.ini
// (HP1's turns the pad keys and Alt+Enter off: no pad bindings there, Alt+Enter SEEN fine):
//   - HP2 only: Lumos. The walls near the lit wand fade out and show what they hide, as the game
//     meant: Render.dll's four reads of the wand's switch and radius are moved to where the actor
//     holds them, and the renderer's DrawComplexSurface goes through a relay that binds the fade
//     the engine computed (lumos.h, which says why; SEEN on Willow's first Lumos wall, 2026-10-21).
//   - HP1 only: its setup, run at EVERY start (a wizard, or a renderer test that writes D3DDrv into
//     HP.ini), is skipped (setup.h, which says why).
//   - HP2 only, under Wine only: its launcher window, black under Wine, keeps its menu page. The
//     exe's SetWindowPos import slot is pointed at a relay that drops the screen-centring call
//     that threw the page out of the window (launcher.h, which says why).
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
#include "launcher.h"
#include "lumos.h"
#include "remap.h"
#include "setup.h"
#include "../version.h"
#include <d3d11.h>
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
	bool wineLauncherFix = false;
	bool blockAltEnter = true;
	bool lumosFade = false;
	bool lumosLog = false;
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
	g_cfg.wineLauncherFix = flag(L"Accio.Window", L"WineLauncherFix", g_cfg.wineLauncherFix);
	g_cfg.blockAltEnter = flag(L"Accio.Window", L"BlockAltEnter", g_cfg.blockAltEnter);
	g_cfg.lumosFade = flag(L"Accio.Lumos", L"LumosFade", g_cfg.lumosFade);
	g_cfg.lumosLog = flag(L"Accio.Lumos", L"LumosLog", g_cfg.lumosLog);
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

// Wine's ntdll exports wine_get_version; Windows' does not.
bool UnderWine()
{
	HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	return ntdll && GetProcAddress(ntdll, "wine_get_version");
}

// What the exe's one SetWindowPos import slot now points at: the screen-centring call on a child
// window (the one that threw HP2's menu page out of the launcher's window) is dropped, everything
// else goes to SetWindowPos itself. The relay is live from before the exe's code runs.
decltype(&SetWindowPos) g_realSetWindowPos;

BOOL WINAPI RelaySetWindowPos(HWND hwnd, HWND after, int x, int y, int cx, int cy, UINT flags)
{
	if (launcher::Ignored(GetWindowLongW(hwnd, GWL_STYLE), flags, x, y, cx, cy,
			GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)))
	{
		Log("Launcher: SetWindowPos %d,%d %dx%d centring a child window on the screen ignored"
			" (WineLauncherFix)\n", x, y, cx, cy);
		return TRUE;
	}
	return g_realSetWindowPos(hwnd, after, x, y, cx, cy, flags);
}

// Same moment as SkipSetup: the exe's imports are bound, its code has not run. The slot is taken
// only if it holds SetWindowPos itself.
void FixLauncher()
{
	auto* image = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
	HMODULE user32 = GetModuleHandleW(L"user32.dll");
	void* setWindowPos = user32 ? reinterpret_cast<void*>(GetProcAddress(user32, "SetWindowPos")) : nullptr;
	void** slot = launcher::ImportSlot(image, "user32.dll", "SetWindowPos");
	if (!slot || !setWindowPos)
	{
		Log("Launcher: no SetWindowPos import in this exe, left as it is\n");
		return;
	}
	if (*slot != setWindowPos)
	{
		Log("Launcher: the exe's SetWindowPos slot holds something else, left as it is\n");
		return;
	}
	DWORD old;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old))
	{
		Log("Launcher: the slot at %p is not writable (error %lu)\n", slot, GetLastError());
		return;
	}
	g_realSetWindowPos = reinterpret_cast<decltype(&SetWindowPos)>(setWindowPos);
	*slot = reinterpret_cast<void*>(&RelaySetWindowPos);
	VirtualProtect(slot, sizeof(*slot), old, &old);
	Log("Launcher: the exe's SetWindowPos calls go through the fix (WineLauncherFix, under Wine)\n");
}

// ---- Lumos ---------------------------------------------------------------------------------

// The renderer's DrawComplexSurface is a thiscall with five arguments; a __fastcall with the same
// stack arguments takes "this" in ecx, ignores edx, and pops the same 0x14 bytes.
using DrawComplexSurface = void(__fastcall*)(void* self, void* edx, void* frame, lumos::SurfaceInfo* surface,
	void* facet, DWORD polyFlags, int alpha);

DrawComplexSurface g_realDrawComplexSurface;
uint8_t* g_renderer;             // d3d11drv.dll, once hooked
ID3D11BlendState* g_fadeState;   // SRC = factor, DEST = 1 - factor
bool g_lumosTried;

struct LumosStats
{
	unsigned surfaces, marked, faded, lines;
	int alphaMin = 255, alphaMax = 0;
	ULONGLONG lastDump;
} g_lumos;

ID3D11DeviceContext* RendererContext()
{
	return *reinterpret_cast<ID3D11DeviceContext**>(g_renderer + lumos::kContext);
}

// Made once, from the renderer's own device.
bool FadeState()
{
	if (g_fadeState)
		return true;
	ID3D11DeviceContext* context = RendererContext();
	if (!context)
		return false;
	ID3D11Device* device = nullptr;
	context->GetDevice(&device);
	if (!device)
		return false;
	D3D11_BLEND_DESC desc{};
	D3D11_RENDER_TARGET_BLEND_DESC& rt = desc.RenderTarget[0];
	rt.BlendEnable = TRUE;
	rt.SrcBlend = D3D11_BLEND_BLEND_FACTOR;
	rt.DestBlend = D3D11_BLEND_INV_BLEND_FACTOR;
	rt.BlendOp = D3D11_BLEND_OP_ADD;
	rt.SrcBlendAlpha = D3D11_BLEND_ONE;
	rt.DestBlendAlpha = D3D11_BLEND_ZERO;
	rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
	rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	const HRESULT hr = device->CreateBlendState(&desc, &g_fadeState);
	device->Release();
	if (FAILED(hr))
		Log("Lumos: no blend state (error 0x%08lX), surfaces left as the renderer draws them\n", hr);
	else
		Log("Lumos: blend state made, context %p (LumosFade)\n", context);
	return g_fadeState != nullptr;
}

void LumosProbe(DWORD surfaceFlags, DWORD polyFlags, int alpha)
{
	++g_lumos.surfaces;
	const bool marked = (surfaceFlags & lumos::kMarked) != 0;
	if (marked)
	{
		++g_lumos.marked;
		if (lumos::Fades(surfaceFlags, alpha))
		{
			++g_lumos.faded;
			if (alpha < g_lumos.alphaMin) g_lumos.alphaMin = alpha;
			if (alpha > g_lumos.alphaMax) g_lumos.alphaMax = alpha;
		}
		if (g_lumos.lines < 400)
		{
			++g_lumos.lines;
			Log("Lumos: surface flags=%08lX arg4=%08lX alpha=%d\n", surfaceFlags, polyFlags, alpha);
		}
	}
	const ULONGLONG now = GetTickCount64();
	if (now - g_lumos.lastDump >= 2000)
	{
		g_lumos.lastDump = now;
		Log("Lumos: %u surfaces, %u marked, %u with a fade alpha (%d..%d), cache=%08lX\n", g_lumos.surfaces,
			g_lumos.marked, g_lumos.faded, g_lumos.alphaMin, g_lumos.alphaMax,
			*reinterpret_cast<DWORD*>(g_renderer + lumos::kFlagsCache));
	}
}

// Probe only: what the engine's own Lumos pass reads (Render.dll, notes/HP2_LUMOS.md). Frame->Viewport
// at +0, Viewport->Actor at +0x30; on the actor, Location at +0x11C, the Lumos switch and radius at
// +0x94C/+0x950 (the offsets Render.dll reads).
void LumosActorProbe(void* frame)
{
	static ULONGLONG last;
	const ULONGLONG now = GetTickCount64();
	if (now - last < 2000 || !frame)
		return;
	last = now;
	void* viewport = *reinterpret_cast<void**>(frame);
	if (!viewport || IsBadReadPtr(viewport, 0x60))
		return;
	auto* actor = *reinterpret_cast<uint8_t**>(static_cast<uint8_t*>(viewport) + 0x30);
	if (!actor || IsBadReadPtr(actor, 0x960))
	{
		Log("Lumos probe: viewport %p, no actor\n", viewport);
		return;
	}
	const float* loc = reinterpret_cast<const float*>(actor + 0x11C);
	char hex[3 * 32 + 1] = {};
	for (int i = 0; i < 32; ++i)
		sprintf_s(hex + 3 * i, 4, "%02X ", actor[0x940 + i]);
	HMODULE render = GetModuleHandleW(L"Render.dll");
	DWORD editor = 0;
	void* cls = nullptr;
	if (render)
	{
		editor = *reinterpret_cast<DWORD*>(reinterpret_cast<uint8_t*>(render) + 0x75584);
		cls = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(render) + 0x75770);
	}
	Log("Lumos probe: actor %p at (%.0f, %.0f, %.0f), [94C]=%08lX [950]=%.1f, bytes 940..95F: %s; Render %p"
		" GIsEditor=%lu class=%p\n", actor, loc[0], loc[1], loc[2], *reinterpret_cast<DWORD*>(actor + 0x94C),
		*reinterpret_cast<float*>(actor + 0x950), hex, render, editor, cls);
}

void __fastcall RelayDrawComplexSurface(void* self, void* edx, void* frame, lumos::SurfaceInfo* surface,
	void* facet, DWORD polyFlags, int alpha)
{
	const DWORD flags = surface ? surface->PolyFlags : 0;
	if (g_cfg.lumosLog)
	{
		LumosProbe(flags, polyFlags, alpha);
		LumosActorProbe(frame);
	}
	const bool fade = g_cfg.lumosFade && lumos::Fades(flags, alpha) && FadeState();
	DWORD* cache = reinterpret_cast<DWORD*>(g_renderer + lumos::kFlagsCache);
	if (fade)
		*cache ^= lumos::kRefresh;   // the renderer flushes what it holds and sets its state
	g_realDrawComplexSurface(self, edx, frame, surface, facet, polyFlags, alpha);
	if (fade)
	{
		// The surface is buffered, not drawn yet: our state is the one its flush will use, and
		// the next surface sets the renderer's own state again.
		const float opacity = lumos::Opacity(alpha);
		const float factor[4] = { opacity, opacity, opacity, opacity };
		RendererContext()->OMSetBlendState(g_fadeState, factor, 0xFFFFFFFF);
		*cache ^= lumos::kRefresh;
	}
}

// The engine's Render.dll: its four reads of the Lumos switch and radius moved to where the
// actor really holds them (lumos.h). Another build: left as it is.
void FixRenderLumos()
{
	HMODULE render = GetModuleHandleW(L"Render.dll");
	if (!render)
	{
		Log("Lumos: no Render.dll in this process, left as it is\n");
		return;
	}
	auto* image = reinterpret_cast<uint8_t*>(render);
	if (!lumos::render::Recognised(image))
	{
		Log("Lumos: this Render.dll is not the build read (2020-06-15), left as it is\n");
		return;
	}
	const uint8_t* first = image + lumos::render::kSites[0].rva;
	const uint8_t* last = image + lumos::render::kSites[3].rva + 6;
	DWORD old;
	if (!VirtualProtect(const_cast<uint8_t*>(first), last - first, PAGE_EXECUTE_READWRITE, &old))
	{
		Log("Lumos: Render.dll's code at %p is not writable (error %lu)\n", first, GetLastError());
		return;
	}
	lumos::render::Patch(image);
	VirtualProtect(const_cast<uint8_t*>(first), last - first, old, &old);
	FlushInstructionCache(GetCurrentProcess(), first, last - first);
	Log("Lumos: Render.dll at %p, its four reads of the Lumos switch and radius moved to +0x950/+0x954 (LumosFade)\n",
		image);
}

// Once the renderer is loaded (the engine loads it when it opens its window): its virtual table's
// DrawComplexSurface slot is pointed at the relay. Another build of the DLL: left as it is.
void HookLumos()
{
	HMODULE renderer = GetModuleHandleW(L"d3d11drv.dll");
	if (!renderer)
		return;
	g_lumosTried = true;
	if (g_cfg.lumosFade)
		FixRenderLumos();
	auto* image = reinterpret_cast<uint8_t*>(renderer);
	if (!lumos::Recognised(image))
	{
		Log("Lumos: this d3d11drv.dll is not the build read (1.6.2 of 2020-12-29), left as it is\n");
		return;
	}
	void** slot = reinterpret_cast<void**>(image + lumos::kVtable + lumos::kDrawComplexSurfaceSlot * sizeof(void*));
	DWORD old;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old))
	{
		Log("Lumos: the slot at %p is not writable (error %lu)\n", slot, GetLastError());
		return;
	}
	g_renderer = image;
	g_realDrawComplexSurface = reinterpret_cast<DrawComplexSurface>(*slot);
	*slot = reinterpret_cast<void*>(&RelayDrawComplexSurface);
	VirtualProtect(slot, sizeof(*slot), old, &old);
	Log("Lumos: d3d11drv.dll at %p, DrawComplexSurface goes through the fix (LumosFade=%d LumosLog=%d)\n",
		image, g_cfg.lumosFade, g_cfg.lumosLog);
}

// Sees the game's thread take its messages: the renderer is looked for until it is there.
LRESULT CALLBACK OnMessageLumos(int code, WPARAM removal, LPARAM lParam)
{
	if (code == HC_ACTION && !g_lumosTried)
		HookLumos();
	return CallNextHookEx(nullptr, code, removal, lParam);
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
	Log("Settings: XboxLayout=%d ShareKey=%u OptionsKey=%u BlockAltEnter=%d FillScreen=%d SkipSetup=%d"
		" WineLauncherFix=%d LumosFade=%d LumosLog=%d\n",
		g_cfg.xboxLayout, g_cfg.shareKey, g_cfg.optionsKey, g_cfg.blockAltEnter, g_cfg.fillScreen,
		g_cfg.skipSetup, g_cfg.wineLauncherFix, g_cfg.lumosFade, g_cfg.lumosLog);

	// Every Windows has one. Without it the stubs would have nowhere to go: refuse to load, and
	// Windows says winmm.dll is broken, which is the truth.
	if (!LoadSystemWinmm())
		return FALSE;

	// Loaded by Core.dll when the game starts, so this is the game's own thread, the one that
	// will make the window and read its messages.
	if (g_cfg.skipSetup)
		SkipSetup();
	if (g_cfg.wineLauncherFix && UnderWine())
		FixLauncher();
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
	if (g_cfg.lumosFade || g_cfg.lumosLog)
	{
		if (SetWindowsHookExW(WH_GETMESSAGE, OnMessageLumos, nullptr, GetCurrentThreadId()))
			Log("Lumos: waiting for d3d11drv.dll\n");
		else
			Log("Lumos: hook refused (error %lu)\n", GetLastError());
	}
	return TRUE;
}
