// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// Entry point and exports. The game imports Direct3DCreate9 from "d3d9.dll"; Windows finds this
// file in the game folder first. Every other export exists for the D3DX library the game loads
// later, and jumps straight to the system Direct3D 9.

#include "accio.h"
#include <cstring>

HMODULE g_self = nullptr;
HWND g_gameWindow = nullptr;
volatile LONG g_frames = 0;
thread_local int g_internal = 0;

namespace
{
HMODULE g_system = nullptr;

// Resolved at load time, jumped to by the stubs below.
struct SystemExports
{
	void* D3DPERF_BeginEvent;
	void* D3DPERF_EndEvent;
	void* D3DPERF_GetStatus;
	void* D3DPERF_QueryRepeatFrame;
	void* D3DPERF_SetMarker;
	void* D3DPERF_SetOptions;
	void* D3DPERF_SetRegion;
	void* DebugSetLevel;
	void* DebugSetMute;
	void* Direct3D9EnableMaximizedWindowedModeShim;
	void* Direct3DShaderValidatorCreate9;
	void* PSGPError;
	void* PSGPSampleTexture;
	void* Direct3DCreate9On12;
	void* Direct3DCreate9On12Ex;
	void* Direct3DCreate9;
	void* Direct3DCreate9Ex;
} g_sys = {};

bool LoadSystemDirect3D()
{
	char path[MAX_PATH];
	// System32 for a 64-bit process, SysWOW64 for these 32-bit games: the redirection is done
	// by Windows. Under Wine it is Wine's d3d9, or DXVK's.
	const UINT n = GetSystemDirectoryA(path, MAX_PATH);
	if (!n || n + 10 >= MAX_PATH)
		return false;
	strcat_s(path, "\\d3d9.dll");
	g_system = LoadLibraryA(path);
	if (!g_system)
	{
		Log("System Direct3D 9 not found at %s (error %lu)\n", path, GetLastError());
		return false;
	}
#define RESOLVE(name) g_sys.name = reinterpret_cast<void*>(GetProcAddress(g_system, #name))
	RESOLVE(D3DPERF_BeginEvent);
	RESOLVE(D3DPERF_EndEvent);
	RESOLVE(D3DPERF_GetStatus);
	RESOLVE(D3DPERF_QueryRepeatFrame);
	RESOLVE(D3DPERF_SetMarker);
	RESOLVE(D3DPERF_SetOptions);
	RESOLVE(D3DPERF_SetRegion);
	RESOLVE(DebugSetLevel);
	RESOLVE(DebugSetMute);
	RESOLVE(Direct3D9EnableMaximizedWindowedModeShim);
	RESOLVE(Direct3DShaderValidatorCreate9);
	RESOLVE(PSGPError);
	RESOLVE(PSGPSampleTexture);
	RESOLVE(Direct3DCreate9On12);
	RESOLVE(Direct3DCreate9On12Ex);
	RESOLVE(Direct3DCreate9);
	RESOLVE(Direct3DCreate9Ex);
#undef RESOLVE
	Log("System Direct3D 9: %s\n", path);
	return true;
}

void IniPath(char* out)
{
	GetModuleFileNameA(g_self, out, MAX_PATH);
	char* slash = strrchr(out, '\\');
	strcpy_s(slash ? slash + 1 : out, MAX_PATH - (slash ? slash + 1 - out : 0), "d3d9.ini");
}

void BecomeDpiAware()
{
	using SetContextFn = BOOL(WINAPI*)(HANDLE);
	auto setContext = reinterpret_cast<SetContextFn>(
		GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext"));
	// Per-monitor v2 (Windows 10 1703 and later), else the Vista-era system-wide flag.
	BOOL ok = setContext && setContext(reinterpret_cast<HANDLE>(-4));
	if (!ok)
		ok = SetProcessDPIAware();
	Log("DPI awareness: %s\n", ok ? "on" : "FAILED");
}
}

// ---------------------------------------------------------------------------------------------
// Exports
// ---------------------------------------------------------------------------------------------

// A stub that jumps to the system function with the caller's own arguments and return address:
// it needs no prototype, and the stack is exactly as if the game had called Direct3D itself.
#define FORWARD(name) \
	extern "C" __declspec(naked) void Accio_##name() { __asm jmp dword ptr [g_sys.name] }

FORWARD(D3DPERF_BeginEvent)
FORWARD(D3DPERF_EndEvent)
FORWARD(D3DPERF_GetStatus)
FORWARD(D3DPERF_QueryRepeatFrame)
FORWARD(D3DPERF_SetMarker)
FORWARD(D3DPERF_SetOptions)
FORWARD(D3DPERF_SetRegion)
FORWARD(DebugSetLevel)
FORWARD(DebugSetMute)
FORWARD(Direct3D9EnableMaximizedWindowedModeShim)
FORWARD(Direct3DShaderValidatorCreate9)
FORWARD(PSGPError)
FORWARD(PSGPSampleTexture)
FORWARD(Direct3DCreate9On12)
FORWARD(Direct3DCreate9On12Ex)

extern "C" IDirect3D9* WINAPI Accio_Direct3DCreate9(UINT sdkVersion)
{
	using Fn = IDirect3D9*(WINAPI*)(UINT);
	auto create = reinterpret_cast<Fn>(g_sys.Direct3DCreate9);
	IDirect3D9* d3d = create ? create(sdkVersion) : nullptr;
	return d3d ? HookDirect3D9(d3d) : nullptr;
}

extern "C" HRESULT WINAPI Accio_Direct3DCreate9Ex(UINT sdkVersion, IDirect3D9Ex** out)
{
	using Fn = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
	auto create = reinterpret_cast<Fn>(g_sys.Direct3DCreate9Ex);
	if (!create)
		return D3DERR_NOTAVAILABLE;
	const HRESULT hr = create(sdkVersion, out);
	if (SUCCEEDED(hr) && out && *out)
		HookDirect3D9(*out);
	return hr;
}

// ---------------------------------------------------------------------------------------------

BOOL WINAPI DllMain(HMODULE self, DWORD reason, LPVOID)
{
	if (reason != DLL_PROCESS_ATTACH)
		return TRUE;
	g_self = self;
	DisableThreadLibraryCalls(self);
	char ini[MAX_PATH];
	IniPath(ini);
	// Log=0 in [Accio.Window] writes nothing; read before anything else is.
	if (GetPrivateProfileIntA("Accio.Window", "Log", 1, ini))
		OpenLog(self);

	char exe[MAX_PATH];
	GetModuleFileNameA(nullptr, exe, MAX_PATH);
	Log("Accio Launcher PC fix, %s\n", exe);

	if (!LoadSystemDirect3D())
		return TRUE; // the game will say Direct3D is missing, which is the truth

	LoadSettings(ini);
	LoadKeyMap(ini);

	// Before the game creates its window: afterwards the window keeps the scale it was born with.
	if (g_cfg.dpiAware)
		BecomeDpiAware();

	ApplyGamePatches();
	InstallInputHooks();
	InstallFocusHooks();
	return TRUE;
}
