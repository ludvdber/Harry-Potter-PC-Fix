// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// dinput8.dll for HP3 (Prisoner of Azkaban), next to the game's exe in its system folder. Its
// WinDrv.dll reads the pad through DirectInput 8, which nothing else of the fix reaches. Three
// jobs, each one switched in dinput8.ini (notes/HP3.md):
//   - Both sticks get a dead zone (pad.h says why the game has none).
//   - Options and Share (Start and Back on an Xbox pad) can press a key, held as long as the
//     button. The game opens its menu on Escape, and its menu listens to no pad button: without
//     the key, Options opened a menu nothing on the pad could close (Ludo, 2026-10-08).
//   - An Xbox pad is shown to the game with a PlayStation pad's layout (pad.h), the one the
//     launcher's bindings use.
// DirectInput8Create is Windows' own, then two methods are redirected in their tables, as the
// fix does for Direct3D 9 (no wrapper object): CreateDevice, to know which devices are pads, and
// GetDeviceState, to change a pad's state on its way to the game. The mouse goes through the same
// GetDeviceState: anything not known as a pad passes untouched. Every other export of dinput8.dll
// jumps straight to Windows' own.
//
// Threads: the engine creates its devices and reads its pad from its one thread. The keys are
// TYPED (SendInput), only while the game is in front: a key posted to the window opened the menu,
// but its release closed it again, where the keyboard's does not (Ludo, 2026-10-08) — the game
// reads its keys by more than the window's messages.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <initguid.h>   // IID_IDirectInput8W defined here, not taken from a library
#include "pad.h"
#include "../version.h"
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <share.h>

namespace
{
// Windows' functions, in the order of dinput8.def.
enum Proc { kCreate, kCanUnloadNow, kGetClassObject, kRegisterServer, kUnregisterServer, kGetdfJoystick, kProcs };
const char* const kProcNames[kProcs] = {"DirectInput8Create", "DllCanUnloadNow", "DllGetClassObject",
	"DllRegisterServer", "DllUnregisterServer", "GetdfDIJoystick"};
void* g_proc[kProcs];

HMODULE g_self;
HMODULE g_system;
FILE* g_log;
CRITICAL_SECTION g_logLock;

struct Settings
{
	bool log = true;
	bool xboxLayout = true;
	int deadZone = 15;          // percent of the travel, each side of the middle
	UINT shareKey = 0;
	UINT optionsKey = VK_ESCAPE;
} g_cfg;

// IDirectInput8W and IDirectInputDevice8W slots (dinput.h order).
constexpr int kSlotCreateDevice = 3;
constexpr int kSlotGetDeviceState = 9;

using CreateDeviceFn = HRESULT(STDMETHODCALLTYPE*)(IDirectInput8W*, REFGUID, LPDIRECTINPUTDEVICE8W*, LPUNKNOWN);
using GetDeviceStateFn = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8W*, DWORD, LPVOID);

// Windows' method, by table: nothing says the mouse's table is the pad's.
template <class Fn> struct Slot
{
	void** table;
	Fn old;
};
constexpr int kTables = 4;
Slot<CreateDeviceFn> g_createDevice[kTables];
Slot<GetDeviceStateFn> g_getDeviceState[kTables];

template <class Fn> Fn Original(const Slot<Fn> (&slots)[kTables], void* object)
{
	void** table = *static_cast<void***>(object);
	for (const Slot<Fn>& s : slots)
		if (s.table == table)
			return s.old;
	return nullptr;
}

struct Pad
{
	IDirectInputDevice8W* device;
	dpad::Kind kind;
	bool ranged;            // ranges asked (after the game set them)
	dpad::Ranges ranges;
	bool read;              // read once already (logged at the first reading)
};
constexpr int kPads = 8;
Pad g_pads[kPads];

// Share's and Options' key, ONE for the whole game: HP3 makes two devices of the same pad (it reads
// one, log of 2026-10-08); were it to read both, a key kept by device would go down twice.
struct Key
{
	bool down;
	const Pad* by;          // the device that pressed it: only its failure lets it up
};
Key g_keys[2];

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
	const wchar_t* s = L"Accio.Controller";
	g_cfg.log = GetPrivateProfileIntW(s, L"Log", g_cfg.log ? 1 : 0, ini) != 0;
	g_cfg.xboxLayout = GetPrivateProfileIntW(s, L"XboxLayout", g_cfg.xboxLayout ? 1 : 0, ini) != 0;
	const int zone = static_cast<int>(GetPrivateProfileIntW(s, L"DeadZone", g_cfg.deadZone, ini));
	g_cfg.deadZone = zone < 0 ? 0 : (zone > 90 ? 90 : zone);
	g_cfg.shareKey = GetPrivateProfileIntW(s, L"ShareKey", g_cfg.shareKey, ini) & 0xFF;
	g_cfg.optionsKey = GetPrivateProfileIntW(s, L"OptionsKey", g_cfg.optionsKey, ini) & 0xFF;
}

bool LoadSystemDinput8()
{
	wchar_t path[MAX_PATH];
	// SysWOW64 for this 32-bit game (redirected by Windows); under Wine, Wine's own dinput8.
	const UINT n = GetSystemDirectoryW(path, MAX_PATH);
	if (!n || n + 13 >= MAX_PATH)
		return false;
	wcscat_s(path, L"\\dinput8.dll");
	g_system = LoadLibraryW(path);
	if (!g_system)
	{
		Log("System dinput8.dll not found at %ls (error %lu)\n", path, GetLastError());
		return false;
	}
	int found = 0;
	for (int i = 0; i < kProcs; ++i)
	{
		g_proc[i] = reinterpret_cast<void*>(GetProcAddress(g_system, kProcNames[i]));
		if (g_proc[i])
			++found;
		else
			Log("Not in the system dinput8.dll: %s\n", kProcNames[i]);
	}
	Log("System dinput8.dll: %ls, %d of %d functions\n", path, found, static_cast<int>(kProcs));
	return g_proc[kCreate] != nullptr;
}

// One slot of a COM table pointed at our function, Windows' one kept by table. A table is shared
// by every object of its class: done once per table, later objects find our function there.
template <class Fn> bool Redirect(Slot<Fn> (&slots)[kTables], void* object, int slot, Fn ours, const char* name)
{
	void** table = *static_cast<void***>(object);
	if (table[slot] == reinterpret_cast<void*>(ours))
		return true;
	Slot<Fn>* spare = nullptr;
	for (Slot<Fn>& s : slots)
		if (!s.table)
		{
			spare = &s;
			break;
		}
	DWORD protect;
	if (!spare || !VirtualProtect(&table[slot], sizeof(void*), PAGE_EXECUTE_READWRITE, &protect))
	{
		Log("%s: not redirected (error %lu)\n", name, spare ? GetLastError() : 0ul);
		return false;
	}
	spare->old = reinterpret_cast<Fn>(table[slot]);
	spare->table = table;
	table[slot] = reinterpret_cast<void*>(ours);
	VirtualProtect(&table[slot], sizeof(void*), protect, &protect);
	Log("%s: watched\n", name);
	return true;
}

// ---- Pad -----------------------------------------------------------------------------------

// A key typed as the keyboard types it: Windows' key state, the window's messages and any other
// reader of the keyboard all see it, as one press.
bool Type(UINT vk, bool up)
{
	INPUT in{};
	in.type = INPUT_KEYBOARD;
	in.ki.wVk = static_cast<WORD>(vk);
	in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
	in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
	return SendInput(1, &in, sizeof(in)) == 1;
}

// The game is the window in front: typed keys go to the window in front, never to another program.
bool GameInFront()
{
	DWORD process = 0;
	GetWindowThreadProcessId(GetForegroundWindow(), &process);
	return process == GetCurrentProcessId();
}

const char* const kKeyNames[2] = {"Share", "Options"};

// The key of one button (0 Share, 1 Options) follows it: down only while the game is in front, up
// whatever is in front (no key left stuck). Any device's reading moves it, once: a second device
// of the same pad finds it already where it should be.
void Follow(const Pad& pad, int which, UINT vk, bool down)
{
	Key& key = g_keys[which];
	if (down && !key.down)
	{
		if (GameInFront() && Type(vk, false))
		{
			key = Key{true, &pad};
			Log("%s down: key %u (device %p)\n", kKeyNames[which], vk, static_cast<void*>(pad.device));
		}
	}
	else if (!down && key.down)
	{
		Type(vk, true);
		key = Key{};
		Log("%s up: key %u (device %p)\n", kKeyNames[which], vk, static_cast<void*>(pad.device));
	}
}

// A device lost or made again: the keys IT pressed go up. Another device of the same pad failing
// (one the game no longer reads) must not let up a key the pad still holds.
void Release(const Pad& pad)
{
	if (g_keys[0].by == &pad)
		Follow(pad, 0, g_cfg.shareKey, false);
	if (g_keys[1].by == &pad)
		Follow(pad, 1, g_cfg.optionsKey, false);
}

Pad* Find(IDirectInputDevice8W* device)
{
	for (Pad& pad : g_pads)
		if (pad.device == device)
			return &pad;
	return nullptr;
}

const char* KindName(dpad::Kind kind)
{
	switch (kind)
	{
	case dpad::Kind::PlayStation: return "PlayStation layout";
	case dpad::Kind::Xbox: return g_cfg.xboxLayout ? "Xbox, shown with PlayStation's layout" : "Xbox, as it is";
	default: return "other: left as it is";
	}
}

// A device the game just made: a pad is kept in the table, anything else is left alone.
void Learn(IDirectInputDevice8W* device)
{
	DIDEVCAPS caps{};
	caps.dwSize = sizeof(caps);
	if (FAILED(device->GetCapabilities(&caps)))
		return;
	const BYTE type = GET_DIDEVICE_TYPE(caps.dwDevType);
	if (type == DI8DEVTYPE_MOUSE || type == DI8DEVTYPE_KEYBOARD)
		return;
	DIDEVICEINSTANCEW info{};
	info.dwSize = sizeof(info);
	device->GetDeviceInfo(&info);
	Pad* pad = Find(device);
	if (!pad)
		pad = Find(nullptr);
	if (!pad)
	{
		Log("Pad: no room left, left as it is\n");
		return;
	}
	Release(*pad);
	*pad = Pad{};
	pad->device = device;
	pad->kind = dpad::Identify(info.guidProduct.Data1, caps.dwAxes, caps.dwButtons, caps.dwPOVs);
	Log("Pad: %04X:%04X \"%ls\", %lu buttons, %lu axes, %lu hats -> %s\n", LOWORD(info.guidProduct.Data1),
		HIWORD(info.guidProduct.Data1), info.tszProductName, caps.dwButtons, caps.dwAxes, caps.dwPOVs,
		KindName(pad->kind));
}

// The range of one axis as the game left it (DirectInput's default when it cannot say).
dpad::Range AskRange(IDirectInputDevice8W* device, DWORD offset)
{
	DIPROPRANGE r{};
	r.diph.dwSize = sizeof(r);
	r.diph.dwHeaderSize = sizeof(DIPROPHEADER);
	r.diph.dwObj = offset;
	r.diph.dwHow = DIPH_BYOFFSET;
	dpad::Range out;
	if (SUCCEEDED(device->GetProperty(DIPROP_RANGE, &r.diph)) && r.lMax > r.lMin)
	{
		out.min = r.lMin;
		out.max = r.lMax;
	}
	return out;
}

// Asked at the first reading, once the game has set its ranges (it sets them before it reads).
void AskRanges(Pad& pad)
{
	pad.ranges.x = AskRange(pad.device, DIJOFS_X);
	pad.ranges.y = AskRange(pad.device, DIJOFS_Y);
	pad.ranges.z = AskRange(pad.device, DIJOFS_Z);
	pad.ranges.rx = AskRange(pad.device, DIJOFS_RX);
	pad.ranges.ry = AskRange(pad.device, DIJOFS_RY);
	pad.ranges.rz = AskRange(pad.device, DIJOFS_RZ);
	pad.ranged = true;
	const dpad::Ranges& r = pad.ranges;
	Log("Pad ranges: X %ld..%ld Y %ld..%ld Z %ld..%ld Rx %ld..%ld Ry %ld..%ld Rz %ld..%ld, dead zone %d %%\n",
		r.x.min, r.x.max, r.y.min, r.y.max, r.z.min, r.z.max, r.rx.min, r.rx.max, r.ry.min, r.ry.max,
		r.rz.min, r.rz.max, g_cfg.deadZone);
}

bool Converted(dpad::Kind kind) { return kind == dpad::Kind::Xbox && g_cfg.xboxLayout; }
bool PlayStationLayout(dpad::Kind kind) { return kind == dpad::Kind::PlayStation || Converted(kind); }

void Change(Pad& pad, DIJOYSTATE& state)
{
	if (!pad.read)
	{
		Log("Pad read: device %p\n", static_cast<void*>(pad.device));
		pad.read = true;
	}
	if (!pad.ranged)
		AskRanges(pad);
	dpad::DeadZone(state, pad.ranges, g_cfg.deadZone, Converted(pad.kind) ? dpad::Kind::Xbox : dpad::Kind::Other);
	if (Converted(pad.kind))
		dpad::AsPlayStation(state, pad.ranges);
	if (!PlayStationLayout(pad.kind))
		return;
	// The game sees no Share or Options button when they press a key: the key is the whole press.
	if (g_cfg.shareKey)
	{
		Follow(pad, 0, g_cfg.shareKey, (state.rgbButtons[dpad::kShare] & 0x80) != 0);
		state.rgbButtons[dpad::kShare] = 0;
	}
	if (g_cfg.optionsKey)
	{
		Follow(pad, 1, g_cfg.optionsKey, (state.rgbButtons[dpad::kOptions] & 0x80) != 0);
		state.rgbButtons[dpad::kOptions] = 0;
	}
}

HRESULT STDMETHODCALLTYPE OurGetDeviceState(IDirectInputDevice8W* self, DWORD size, LPVOID data)
{
	const GetDeviceStateFn real = Original(g_getDeviceState, self);
	if (!real)
		return DIERR_NOTINITIALIZED;   // not one of the tables we changed: cannot happen
	const HRESULT hr = real(self, size, data);
	Pad* pad = Find(self);
	if (!pad)
		return hr;
	if (FAILED(hr))
	{
		// Lost or unplugged: no key left down.
		Release(*pad);
		return hr;
	}
	// DIJOYSTATE and DIJOYSTATE2 begin alike; anything else is not a pad state.
	if (data && (size == sizeof(DIJOYSTATE) || size == sizeof(DIJOYSTATE2)))
		Change(*pad, *static_cast<DIJOYSTATE*>(data));
	return hr;
}

HRESULT STDMETHODCALLTYPE OurCreateDevice(IDirectInput8W* self, REFGUID guid, LPDIRECTINPUTDEVICE8W* out,
	LPUNKNOWN outer)
{
	const CreateDeviceFn real = Original(g_createDevice, self);
	if (!real)
		return DIERR_NOTINITIALIZED;   // not one of the tables we changed: cannot happen
	const HRESULT hr = real(self, guid, out, outer);
	if (FAILED(hr) || !out || !*out)
		return hr;
	if (Redirect(g_getDeviceState, *out, kSlotGetDeviceState, &OurGetDeviceState, "GetDeviceState"))
		Learn(*out);
	return hr;
}
}

// ---- Exports (names in dinput8.def) --------------------------------------------------------

// A stub that jumps to Windows' function with the caller's own arguments and return address: it
// needs no prototype, and the stack is exactly as if the game had called Windows itself.
#define STUB(name, index) \
	extern "C" __declspec(naked) void Accio_##name() { __asm jmp dword ptr [g_proc + index * 4] }
STUB(DllCanUnloadNow, 1)
STUB(DllGetClassObject, 2)
STUB(DllRegisterServer, 3)
STUB(DllUnregisterServer, 4)
STUB(GetdfDIJoystick, 5)
#undef STUB
static_assert(kCanUnloadNow == 1 && kGetClassObject == 2 && kRegisterServer == 3 && kUnregisterServer == 4
	&& kGetdfJoystick == 5, "the stubs' indexes follow enum Proc");

using CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

extern "C" HRESULT WINAPI Accio_DirectInput8Create(HINSTANCE instance, DWORD version, REFIID iid, LPVOID* out,
	LPUNKNOWN outer)
{
	auto real = reinterpret_cast<CreateFn>(g_proc[kCreate]);
	const HRESULT hr = real ? real(instance, version, iid, out, outer) : DIERR_NOTINITIALIZED;
	if (FAILED(hr) || !out || !*out)
		return hr;
	if (iid != IID_IDirectInput8W)
	{
		Log("DirectInput8Create: not the wide interface, left as it is\n");
		return hr;
	}
	Redirect(g_createDevice, *out, kSlotCreateDevice, &OurCreateDevice, "CreateDevice");
	return hr;
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, void*)
{
	if (reason != DLL_PROCESS_ATTACH)
		return TRUE;
	g_self = module;
	DisableThreadLibraryCalls(module);
	InitializeCriticalSection(&g_logLock);

	wchar_t path[MAX_PATH];
	Beside(L"dinput8.ini", path);
	LoadSettings(path);
	if (g_cfg.log)
	{
		Beside(L"dinput8_accio.log", path);
		g_log = _wfsopen(path, L"w", _SH_DENYWR);
	}
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	Log("Accio Launcher PC fix " ACCIO_VERSION_STR " (dinput8.dll), %ls\n", path);
	Log("Settings: XboxLayout=%d DeadZone=%d ShareKey=%u OptionsKey=%u\n", g_cfg.xboxLayout, g_cfg.deadZone,
		g_cfg.shareKey, g_cfg.optionsKey);

	// Every Windows has one. Without it the game would have no input at all: refuse to load, and
	// Windows says dinput8.dll is broken, which is the truth.
	return LoadSystemDinput8() ? TRUE : FALSE;
}
