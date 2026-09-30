// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// Keyboard and mouse after a trip to another window. The games read DirectInput devices opened
// exclusive + foreground, and nothing tells them when they are back in front: the keyboard then
// stayed dead for up to 15 s (HP6) or 30 s (HP5). Here the return is seen by watching which
// process owns the foreground, and every device is taken back at its next read.

#define DIRECTINPUT_VERSION 0x0800
#include "hooks.h"
#include <dinput.h>
#include <cstring>

#pragma comment(lib, "dxguid.lib")

void RemapKeyboardEvents(DIDEVICEOBJECTDATA* events, DWORD count, DWORD size); // keys.cpp

namespace
{
// ---- Who is in front --------------------------------------------------------------------------

// Asked of the kernel call behind user32's GetForegroundWindow: the answer the game gets from
// user32 is the one window.cpp gives it on purpose, and this module needs the real one.
using ForegroundFn = HWND(WINAPI*)();
ForegroundFn RealForeground()
{
	static ForegroundFn fn = []() -> ForegroundFn {
		HMODULE w = GetModuleHandleA("win32u.dll");
		if (!w)
			w = LoadLibraryA("win32u.dll");
		auto f = w ? reinterpret_cast<ForegroundFn>(GetProcAddress(w, "NtUserGetForegroundWindow")) : nullptr;
		return f ? f : GetForegroundWindow; // Windows 7 and 8, or Wine without win32u
	}();
	return fn;
}

volatile LONG g_inFront = 1;
volatile LONG g_returns = 0; // how many times the game came back to the front

bool OwnsForeground()
{
	DWORD pid = 0;
	GetWindowThreadProcessId(RealForeground()(), &pid);
	return pid == GetCurrentProcessId();
}

void SampleForeground()
{
	const LONG now = OwnsForeground() ? 1 : 0;
	const LONG before = InterlockedExchange(&g_inFront, now);
	if (now && !before)
		Log("Input: back in front (return %ld, frame %ld)\n", InterlockedIncrement(&g_returns), g_frames);
	else if (!now && before)
		Log("Input: another window in front (frame %ld)\n", g_frames);
}

// The loss of focus has to be seen even while the game stops reading its devices, so the
// sampling runs on its own thread; device reads only compare counters.
DWORD WINAPI WatchForeground(LPVOID)
{
	// Diagnosis (HP5, 2026-09-27): after a few minutes, Ludo's mouse stayed in the game although every
	// exclusive device had been let go, and a scripted mouse never reproduced it. One second into each
	// departure, the log says what could hold the cursor: its clip, whether it is shown, a capture.
	LONG awayFor = 0;
	for (;;)
	{
		SampleForeground();
		awayFor = g_inFront ? 0 : awayFor + 1;
		if (awayFor == 10) // once per departure
		{
			RECT clip = {};
			GetClipCursor(&clip);
			CURSORINFO ci = { sizeof(ci) };
			GetCursorInfo(&ci);
			GUITHREADINFO gui = { sizeof(gui) };
			GetGUIThreadInfo(0, &gui);
			DWORD capturePid = 0;
			if (gui.hwndCapture)
				GetWindowThreadProcessId(gui.hwndCapture, &capturePid);
			Log("Input: away 1 s: cursor clip %ld,%ld-%ld,%ld (screen %d,%d %dx%d), cursor %s at %ld,%ld, capture %s\n",
				clip.left, clip.top, clip.right, clip.bottom, GetSystemMetrics(SM_XVIRTUALSCREEN),
				GetSystemMetrics(SM_YVIRTUALSCREEN), GetSystemMetrics(SM_CXVIRTUALSCREEN),
				GetSystemMetrics(SM_CYVIRTUALSCREEN), ci.flags & CURSOR_SHOWING ? "shown" : "hidden",
				ci.ptScreenPos.x, ci.ptScreenPos.y,
				!gui.hwndCapture ? "none" : capturePid == GetCurrentProcessId() ? "THE GAME" : "another program");
		}
		Sleep(100);
	}
}

void StartWatching()
{
	static volatile LONG started = 0;
	if (InterlockedExchange(&started, 1))
		return;
	HANDLE t = CreateThread(nullptr, 0, WatchForeground, nullptr, 0, nullptr);
	Log("Input: foreground watch %s\n", t ? "started" : "FAILED");
	if (t)
		CloseHandle(t);
}

// ---- Devices ----------------------------------------------------------------------------------

struct Device
{
	void* object;
	bool keyboard;
	LONG seenReturns;
	BYTE held[256]; // keys down when the game came back, hidden until really released
	DWORD cooperation; // DISCL_* flags the game asked for
	bool letGo;        // released by us while another window is in front
};

constexpr int kMaxDevices = 16;
Device g_devices[kMaxDevices] = {};
CRITICAL_SECTION g_lock;

Device* FindDevice(void* object)
{
	for (Device& d : g_devices)
		if (d.object == object)
			return &d;
	return nullptr;
}

void Remember(void* object, bool keyboard)
{
	EnterCriticalSection(&g_lock);
	Device* d = FindDevice(object);
	if (!d)
		d = FindDevice(nullptr);
	if (d)
		*d = Device{ object, keyboard, g_returns, {}, 0, false };
	LeaveCriticalSection(&g_lock);
	Log("Input: %s %p %s\n", keyboard ? "keyboard" : "device", object, d ? "followed" : "NOT followed (table full)");
}

void Forget(void* object)
{
	EnterCriticalSection(&g_lock);
	if (Device* d = FindDevice(object))
		d->object = nullptr;
	LeaveCriticalSection(&g_lock);
}

// Takes the device back if the game has returned to the front since its last read. Returns the
// device record when that just happened (the caller then looks at held keys), else nullptr.
Device* RetakeIfReturned(IDirectInputDevice8A* dev, bool& returned)
{
	returned = false;
	StartWatching();
	SampleForeground();
	EnterCriticalSection(&g_lock);
	Device* d = FindDevice(dev);
	if (d && d->seenReturns != g_returns)
	{
		d->seenReturns = g_returns;
		d->letGo = false;
		returned = true;
	}
	// An exclusive mouse keeps the cursor inside the game window for as long as it is acquired.
	// DirectInput lets go when the window loses the front, but the game is kept running and
	// never hears of it (window.cpp), so the device stayed acquired and the cursor could not
	// leave (HP5, 2026-09-27: Alt+Tab worked, the mouse stayed in the game). Let go here, on the
	// game's own thread, at its first read once another window is in front — and let go of EVERY
	// exclusive device at once: HP5 keeps one it never reads, which held the cursor after a few
	// minutes of play (same evening, 5th device in the log, never let go). Under the lock, so a
	// device the game releases meanwhile is not touched. Once per departure (g_returns counts the
	// returns): a device never read keeps its letGo flag, and must be let go again next time.
	static LONG s_letGoFor = -1;
	void* released[kMaxDevices];
	int count = 0;
	if (d && g_cfg.retakeInput && !g_inFront && s_letGoFor != g_returns)
	{
		s_letGoFor = g_returns;
		for (Device& other : g_devices)
			if (other.object && (other.cooperation & DISCL_EXCLUSIVE))
			{
				other.letGo = true;
				static_cast<IDirectInputDevice8A*>(other.object)->Unacquire();
				released[count++] = other.object;
			}
	}
	LeaveCriticalSection(&g_lock);
	for (int i = 0; i < count; i++)
		Log("Input: %p let go while another window is in front\n", released[i]);
	if (returned && g_cfg.retakeInput)
	{
		dev->Unacquire();
		const HRESULT hr = dev->Acquire();
		Log("Input: %p taken back, hr=0x%lX\n", dev, static_cast<unsigned long>(hr));
	}
	return d;
}

// While another window is in front, an exclusive device stays let go: the game (or our own
// retry after a lost read) asking for it again would take the cursor back at once.
bool HeldAway(IDirectInputDevice8A* dev)
{
	if (!g_cfg.retakeInput || g_inFront)
		return false;
	EnterCriticalSection(&g_lock);
	const Device* d = FindDevice(dev);
	const bool held = d && d->letGo;
	LeaveCriticalSection(&g_lock);
	return held;
}

// A key released while another window was in front never reaches DirectInput, which reports it
// down after the return: on HP6 Harry kept walking on his own (2026-09-25, W down 4.7 s after
// the return). Keys down at the return are hidden from the game until DirectInput sees them
// released, unless Windows says they are really held.
void HideStaleKeys(Device& d, BYTE* keys, bool returned)
{
	if (returned)
	{
		int n = 0;
		for (int k = 0; k < 256; k++)
		{
			d.held[k] = (keys[k] & 0x80) ? 1 : 0;
			n += d.held[k];
		}
		if (n)
			Log("Input: %d key(s) reported down at the return, hidden until released\n", n);
	}
	for (int k = 0; k < 256; k++)
	{
		if (!d.held[k])
			continue;
		if (!(keys[k] & 0x80))
		{
			d.held[k] = 0;
			continue;
		}
		// DirectInput key codes are scan codes, with 0x80 standing for the E0 prefix.
		const UINT scan = (k & 0x80) ? (0xE000u | (k & 0x7F)) : static_cast<UINT>(k);
		const UINT vk = MapVirtualKeyA(scan, MAPVK_VSC_TO_VK_EX);
		if (vk && (GetAsyncKeyState(static_cast<int>(vk)) & 0x8000))
		{
			d.held[k] = 0;
			continue;
		}
		keys[k] = 0;
	}
}

bool Lost(HRESULT hr) { return hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED; }

// ---- Redirected methods -----------------------------------------------------------------------
// The A and W interfaces have the same layout for everything used here, so one function serves
// both; MethodRedirect keeps each table's original.

using GetStateFn = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8A*, DWORD, LPVOID);
using GetDataFn = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8A*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
using PollFn = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8A*);
using ReleaseFn = ULONG(STDMETHODCALLTYPE*)(IUnknown*);
using CreateDeviceFn = HRESULT(STDMETHODCALLTYPE*)(IDirectInput8A*, REFGUID, LPDIRECTINPUTDEVICE8A*, LPUNKNOWN);
using CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

using AcquireFn = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8A*);
using CooperationFn = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8A*, HWND, DWORD);

HRESULT STDMETHODCALLTYPE Acquire(IDirectInputDevice8A* self);
HRESULT STDMETHODCALLTYPE SetCooperativeLevel(IDirectInputDevice8A* self, HWND wnd, DWORD flags);
HRESULT STDMETHODCALLTYPE GetDeviceState(IDirectInputDevice8A* self, DWORD size, LPVOID data);
HRESULT STDMETHODCALLTYPE GetDeviceData(IDirectInputDevice8A* self, DWORD size, LPDIDEVICEOBJECTDATA data, LPDWORD count, DWORD flags);
HRESULT STDMETHODCALLTYPE Poll(IDirectInputDevice8A* self);
ULONG STDMETHODCALLTYPE ReleaseDevice(IUnknown* self);
HRESULT STDMETHODCALLTYPE CreateDevice(IDirectInput8A* self, REFGUID guid, LPDIRECTINPUTDEVICE8A* out, LPUNKNOWN outer);

// Method slots, counted from the declarations in dinput.h.
MethodRedirect g_acquire(7, reinterpret_cast<void*>(Acquire));
MethodRedirect g_cooperation(13, reinterpret_cast<void*>(SetCooperativeLevel));
MethodRedirect g_getState(9, reinterpret_cast<void*>(GetDeviceState));
MethodRedirect g_getData(10, reinterpret_cast<void*>(GetDeviceData));
MethodRedirect g_poll(25, reinterpret_cast<void*>(Poll));
MethodRedirect g_release(2, reinterpret_cast<void*>(ReleaseDevice));
MethodRedirect g_createDevice(3, reinterpret_cast<void*>(CreateDevice));
CreateFn g_create = nullptr;

HRESULT STDMETHODCALLTYPE Acquire(IDirectInputDevice8A* self)
{
	if (HeldAway(self))
		return DIERR_OTHERAPPHASPRIO; // what DirectInput itself answers an application in the back
	return g_acquire.Original<AcquireFn>(self)(self);
}

HRESULT STDMETHODCALLTYPE SetCooperativeLevel(IDirectInputDevice8A* self, HWND wnd, DWORD flags)
{
	const HRESULT hr = g_cooperation.Original<CooperationFn>(self)(self, wnd, flags);
	if (SUCCEEDED(hr))
	{
		EnterCriticalSection(&g_lock);
		if (Device* d = FindDevice(self))
			d->cooperation = flags;
		LeaveCriticalSection(&g_lock);
		Log("Input: %p %s, %s%s\n", self, flags & DISCL_EXCLUSIVE ? "exclusive" : "shared",
			flags & DISCL_FOREGROUND ? "foreground" : "background", flags & DISCL_NOWINKEY ? ", no Windows key" : "");
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE GetDeviceState(IDirectInputDevice8A* self, DWORD size, LPVOID data)
{
	const auto original = g_getState.Original<GetStateFn>(self);
	bool returned = false;
	Device* d = RetakeIfReturned(self, returned);
	HRESULT hr = original(self, size, data);
	if (Lost(hr) && SUCCEEDED(self->Acquire()))
		hr = original(self, size, data);
	if (SUCCEEDED(hr) && d && d->keyboard && size == 256 && data)
	{
		if (g_cfg.releaseStaleKeys)
			HideStaleKeys(*d, static_cast<BYTE*>(data), returned);
		RemapKeyboardState(static_cast<BYTE*>(data));
		// A key that was up at the previous read and is down now starts a latency measurement.
		static BYTE previous[256] = {};
		const BYTE* keys = static_cast<BYTE*>(data);
		for (int i = 0; i < 256; ++i)
			if ((keys[i] & 0x80) && !(previous[i] & 0x80))
			{
				NoteKeyPressed();
				break;
			}
		memcpy(previous, keys, 256);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE GetDeviceData(IDirectInputDevice8A* self, DWORD size, LPDIDEVICEOBJECTDATA data, LPDWORD count, DWORD flags)
{
	const auto original = g_getData.Original<GetDataFn>(self);
	bool returned = false;
	Device* d = RetakeIfReturned(self, returned);
	const DWORD asked = count ? *count : 0;
	HRESULT hr = original(self, size, data, count, flags);
	if (Lost(hr) && SUCCEEDED(self->Acquire()))
	{
		if (count)
			*count = asked;
		hr = original(self, size, data, count, flags);
	}
	if (SUCCEEDED(hr) && d && d->keyboard && count && data)
	{
		RemapKeyboardEvents(data, *count, size);
		for (DWORD i = 0; i < *count; ++i)
			if (reinterpret_cast<const DIDEVICEOBJECTDATA*>(reinterpret_cast<const BYTE*>(data) + i * size)->dwData & 0x80)
			{
				NoteKeyPressed();
				break;
			}
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE Poll(IDirectInputDevice8A* self)
{
	const auto original = g_poll.Original<PollFn>(self);
	HRESULT hr = original(self);
	if (Lost(hr) && SUCCEEDED(self->Acquire()))
		hr = original(self);
	return hr;
}

ULONG STDMETHODCALLTYPE ReleaseDevice(IUnknown* self)
{
	const auto original = g_release.Original<ReleaseFn>(self);
	const ULONG left = original(self);
	if (!left)
		Forget(self);
	return left;
}

HRESULT STDMETHODCALLTYPE CreateDevice(IDirectInput8A* self, REFGUID guid, LPDIRECTINPUTDEVICE8A* out, LPUNKNOWN outer)
{
	const HRESULT hr = g_createDevice.Original<CreateDeviceFn>(self)(self, guid, out, outer);
	if (SUCCEEDED(hr) && out && *out)
	{
		void* dev = *out;
		g_acquire.Install(dev);
		g_cooperation.Install(dev);
		g_getState.Install(dev);
		g_getData.Install(dev);
		g_poll.Install(dev);
		g_release.Install(dev);
		Remember(dev, guid == GUID_SysKeyboard);
	}
	return hr;
}

HRESULT WINAPI DirectInput8CreateForGame(HINSTANCE inst, DWORD version, REFIID iid, LPVOID* out, LPUNKNOWN outer)
{
	const HRESULT hr = g_create(inst, version, iid, out, outer);
	if (SUCCEEDED(hr) && out && *out)
		g_createDevice.Install(*out);
	return hr;
}

void RedirectCreate(HMODULE module, const char* name)
{
	void* previous = RedirectImport(module, "dinput8.dll", "DirectInput8Create", reinterpret_cast<void*>(DirectInput8CreateForGame));
	if (previous && !g_create)
		g_create = reinterpret_cast<CreateFn>(previous);
	Log("Input: %s %s DirectInput\n", name, previous ? "opens" : "does not import");
}
}

void InstallInputHooks()
{
	InitializeCriticalSection(&g_lock);
	HMODULE dinput = GetModuleHandleA("dinput8.dll");
	if (!dinput)
	{
		Log("Input: DirectInput not loaded, nothing to do\n");
		return;
	}
	g_create = reinterpret_cast<CreateFn>(GetProcAddress(dinput, "DirectInput8Create"));
	RedirectCreate(GetModuleHandleA(nullptr), "the game");
	// HP4 ships GofInput.dll, which imports DirectInput too. The game never names it, so it is
	// probably never loaded; covered anyway in case it is.
	if (HMODULE gof = GetModuleHandleA("GofInput.dll"))
		RedirectCreate(gof, "GofInput.dll");
}

bool ProcessInForeground()
{
	return g_inFront != 0;
}

bool ProcessInForegroundNow()
{
	SampleForeground();
	return g_inFront != 0;
}
