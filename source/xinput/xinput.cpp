// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// xinput1_3.dll for HP5, HP6 and both Deathly Hallows, which import it by ordinal (2 GetState,
// 3 SetState, 4 GetCapabilities, 5 Enable). Two jobs:
//   - Xbox pads: every call goes to Windows' own xinput1_4.dll (xinput9_1_0.dll before Windows 8),
//     so the June 2010 DirectX runtime is no longer needed for them.
//   - DualShock 4 and DualSense: read from their HID reports (pad.cpp) and shown to the game as an
//     Xbox pad, on the first slot no Xbox pad holds. Rumble and the light bar go back to the pad.
// Wine maps PlayStation pads itself and keeps its own xinput1_3 (the launcher does not override
// it), so none of this runs on Linux.
//
// Threads: the game's own calls read a snapshot under one lock. A monitor thread looks for pads
// every 2 s; each pad has a thread that reads its reports and writes rumble when told to.
// Settings: [Accio.Controller] in d3d9.ini, next to this DLL.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <share.h>
#include "pad.h"
#include "../version.h"

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

namespace
{
// The XInput structures (<Xinput.h> is not included: its declarations would clash with ours).
struct XState { DWORD packet; pad::Gamepad gamepad; };
struct XVibration { WORD left, right; };
struct XCaps { BYTE type, subType; WORD flags; pad::Gamepad gamepad; XVibration vibration; };
struct XBattery { BYTE type, level; };
struct XKeystroke;

static_assert(sizeof(XState) == 16 && sizeof(XCaps) == 20, "XInput layout");

constexpr DWORD kSlots = 4;
constexpr DWORD kNotConnected = ERROR_DEVICE_NOT_CONNECTED;
constexpr DWORD kEmpty = ERROR_EMPTY;

using GetStateFn = DWORD(WINAPI*)(DWORD, XState*);
using SetStateFn = DWORD(WINAPI*)(DWORD, XVibration*);
using GetCapsFn = DWORD(WINAPI*)(DWORD, DWORD, XCaps*);
using EnableFn = void(WINAPI*)(BOOL);
using AudioFn = DWORD(WINAPI*)(DWORD, GUID*, GUID*);
using BatteryFn = DWORD(WINAPI*)(DWORD, BYTE, XBattery*);
using KeystrokeFn = DWORD(WINAPI*)(DWORD, DWORD, XKeystroke*);
using WaitGuideFn = DWORD(WINAPI*)(DWORD, DWORD, void*);
using UserFn = DWORD(WINAPI*)(DWORD);

struct System
{
	GetStateFn getState;
	SetStateFn setState;
	GetCapsFn getCaps;
	EnableFn enable;
	AudioFn audio;
	BatteryFn battery;
	KeystrokeFn keystroke;
	GetStateFn getStateEx;
	WaitGuideFn waitGuide;
	UserFn cancelGuide, powerOff;
} g_sys;

HMODULE g_self;
INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
FILE* g_log;
CRITICAL_SECTION g_logLock;

bool g_playstation = true;
bool g_rumble = true;
bool g_light = false;
uint8_t g_rgb[3];
volatile LONG g_enabled = 1;

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

// ---- PlayStation pads ----------------------------------------------------------------------

struct Pad
{
	bool used;                 // slot taken (the thread may have ended: see alive)
	volatile LONG alive;       // its thread still runs
	bool fresh;                // a report has been read: the pad is shown to the game
	bool heard;                // a report has been parsed, shown or not (see WaitFirstReports)
	uint16_t pid;
	uint64_t order;            // connection order, which decides the XInput slot
	wchar_t path[512];
	HANDLE read, write, wake, thread;
	DWORD inSize, outSize;
	pad::Gamepad state;
	DWORD packet;
	uint8_t big, small;        // rumble asked by the game
};

Pad g_pads[kSlots];
SRWLOCK g_lock = SRWLOCK_INIT;   // g_pads' shown state (the monitor thread alone adds and removes)
uint64_t g_order;

bool WriteOutput(Pad& p, uint8_t big, uint8_t small)
{
	if (p.write == INVALID_HANDLE_VALUE)
		return false;
	uint8_t report[64];
	const size_t n = pad::Output(p.pid, p.outSize, big, small, g_rgb, g_light, report, sizeof(report));
	if (!n)
		return false;
	DWORD written = 0;
	return WriteFile(p.write, report, static_cast<DWORD>(n), &written, nullptr) && written == n;
}

// The DS4 wireless adapter keeps sending reports with no controller paired: sticks at 0, which
// would read as a stick held up and to the left. Not seen on a real adapter (none here).
bool AdapterAlone(const Pad& p, const uint8_t* r, DWORD n)
{
	return p.pid == 0x0BA0 && n > 4 && r[1] == 0 && r[2] == 0 && r[3] == 0 && r[4] == 0;
}

void OnReport(Pad& p, const uint8_t* r, DWORD n)
{
	pad::Gamepad g{};
	if (!pad::Parse(p.pid, r, n, g))
		return;
	const bool shown = !AdapterAlone(p, r, n);
	AcquireSRWLockExclusive(&g_lock);
	p.heard = true;
	if (shown && (!p.fresh || memcmp(&g, &p.state, sizeof(g)) != 0))
	{
		p.state = g;
		p.packet++;
	}
	if (shown != p.fresh)
		Log("pad %04X: %s\n", p.pid, shown ? "reports" : "adapter without controller");
	p.fresh = shown;
	ReleaseSRWLockExclusive(&g_lock);
}

void WriteRumble(Pad& p, uint8_t& lastBig, uint8_t& lastSmall)
{
	AcquireSRWLockShared(&g_lock);
	const bool on = g_enabled && g_rumble;
	const uint8_t big = on ? p.big : 0, small = on ? p.small : 0;
	ReleaseSRWLockShared(&g_lock);
	if (big == lastBig && small == lastSmall)
		return;
	WriteOutput(p, big, small);
	lastBig = big;
	lastSmall = small;
}

DWORD WINAPI PadThread(void* arg)
{
	Pad& p = *static_cast<Pad*>(arg);
	OVERLAPPED ov{};
	ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	uint8_t buf[1024];
	const DWORD size = p.inSize < sizeof(buf) ? p.inSize : static_cast<DWORD>(sizeof(buf));
	uint8_t lastBig = 0, lastSmall = 0;
	if (!WriteOutput(p, 0, 0) && g_light)
		Log("pad %04X: no output report (Bluetooth?), no light bar or rumble\n", p.pid);

	bool pending = false;
	while (ov.hEvent)
	{
		if (!pending)
		{
			ResetEvent(ov.hEvent);
			DWORD got = 0;
			if (ReadFile(p.read, buf, size, &got, &ov))
			{
				OnReport(p, buf, got);
				if (WaitForSingleObject(p.wake, 0) == WAIT_OBJECT_0)
					WriteRumble(p, lastBig, lastSmall);
				continue;
			}
			if (GetLastError() != ERROR_IO_PENDING)
				break;
			pending = true;
		}
		HANDLE waits[2] = { ov.hEvent, p.wake };
		const DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
		if (w == WAIT_OBJECT_0)
		{
			pending = false;
			DWORD got = 0;
			if (!GetOverlappedResult(p.read, &ov, &got, FALSE))
				break;   // unplugged
			OnReport(p, buf, got);
		}
		else if (w == WAIT_OBJECT_0 + 1)
			WriteRumble(p, lastBig, lastSmall);
		else
			break;
	}
	if (pending)
	{
		DWORD got = 0;
		CancelIoEx(p.read, &ov);
		GetOverlappedResult(p.read, &ov, &got, TRUE);
	}
	if (ov.hEvent)
		CloseHandle(ov.hEvent);
	InterlockedExchange(&p.alive, 0);
	return 0;
}

void Close(HANDLE& h)
{
	if (h && h != INVALID_HANDLE_VALUE)
		CloseHandle(h);
	h = INVALID_HANDLE_VALUE;
}

// Frees the slots whose thread has ended (pad unplugged).
void Reap()
{
	for (Pad& p : g_pads)
	{
		if (!p.used || p.alive)
			continue;
		WaitForSingleObject(p.thread, INFINITE);
		Log("pad %04X: gone\n", p.pid);
		AcquireSRWLockExclusive(&g_lock);
		p.fresh = false;
		p.used = false;
		ReleaseSRWLockExclusive(&g_lock);
		Close(p.read);
		Close(p.write);
		Close(p.wake);
		Close(p.thread);
	}
}

bool Known(const wchar_t* path)
{
	for (const Pad& p : g_pads)
		if (p.used && _wcsicmp(p.path, path) == 0)
			return true;
	return false;
}

Pad* FreeSlot()
{
	for (Pad& p : g_pads)
		if (!p.used)
			return &p;
	return nullptr;
}

// Opens one HID interface if it is a Sony gamepad we read.
void TryOpen(const wchar_t* path)
{
	Pad* p = FreeSlot();
	if (!p || Known(path))
		return;

	HANDLE probe = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (probe == INVALID_HANDLE_VALUE)
		return;
	HIDD_ATTRIBUTES attr{ sizeof(attr) };
	const bool ok = HidD_GetAttributes(probe, &attr) && attr.VendorID == pad::kSony &&
		(pad::IsDualShock4(attr.ProductID) || pad::IsDualSense(attr.ProductID));
	CloseHandle(probe);
	if (!ok)
		return;

	// Opening fails if another program holds the pad exclusively (DS4Windows with HidHide, for
	// instance): it then shows the game its own Xbox pad, and ours would be a second one.
	HANDLE read = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
		OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
	if (read == INVALID_HANDLE_VALUE)
	{
		Log("pad %04X: cannot open (error %lu), left alone\n", attr.ProductID, GetLastError());
		return;
	}
	HIDP_CAPS caps{};
	PHIDP_PREPARSED_DATA data = nullptr;
	const bool gamepad = HidD_GetPreparsedData(read, &data) && HidP_GetCaps(data, &caps) == HIDP_STATUS_SUCCESS &&
		caps.UsagePage == 0x01 && caps.Usage == 0x05;
	if (data)
		HidD_FreePreparsedData(data);
	if (!gamepad)
	{
		CloseHandle(read);
		return;
	}

	*p = Pad{};
	wcsncpy_s(p->path, path, _TRUNCATE);
	p->pid = attr.ProductID;
	p->read = read;
	p->write = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	p->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	p->inSize = caps.InputReportByteLength;
	p->outSize = caps.OutputReportByteLength;
	p->order = ++g_order;
	p->alive = 1;
	AcquireSRWLockExclusive(&g_lock);
	p->used = true;
	ReleaseSRWLockExclusive(&g_lock);
	p->thread = CreateThread(nullptr, 0, PadThread, p, 0, nullptr);
	if (!p->thread)
		p->alive = 0;   // reaped on the next pass
	Log("pad %04X: opened, input %lu bytes, output %lu bytes%s\n", p->pid, p->inSize, p->outSize,
		p->write == INVALID_HANDLE_VALUE ? ", not writable" : "");
}

void Scan()
{
	GUID hid;
	HidD_GetHidGuid(&hid);
	HDEVINFO set = SetupDiGetClassDevsW(&hid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (set == INVALID_HANDLE_VALUE)
		return;
	SP_DEVICE_INTERFACE_DATA iface{ sizeof(iface) };
	alignas(8) BYTE detailBuf[sizeof(DWORD) + 512 * sizeof(wchar_t)];
	for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hid, i, &iface); i++)
	{
		auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuf);
		detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
		if (!SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, sizeof(detailBuf), nullptr, nullptr))
			continue;
		// Sony's vendor id is in the path over USB (vid_054c) and Bluetooth (..._vid&0002054c...):
		// no need to open a keyboard every two seconds.
		wchar_t lower[512];
		wcsncpy_s(lower, detail->DevicePath, _TRUNCATE);
		_wcslwr_s(lower);
		if (wcsstr(lower, L"054c"))
			TryOpen(detail->DevicePath);
	}
	SetupDiDestroyDeviceInfoList(set);
}

DWORD WINAPI Monitor(void*)
{
	for (;;)
	{
		Reap();
		Scan();
		Sleep(2000);
	}
}

// HP5 decides at its FIRST controller poll whether the pad chosen in its own menu is there
// (registry ControllerConfig\CurrentSelection = 4): if no pad answers, it falls back to the
// keyboard for the whole session and writes that choice back (hp.exe 0xE93C19, in a function
// run once a frame whose first pass polls the pads before reading the registry). That first poll
// is the game's first XInput call, the very one that opens our PlayStation pads — and an opened
// pad is only shown to the game once its first report has been read, a few milliseconds later.
// Hence "with the registry at 4, the menus answer the pad but the game stays on the keyboard"
// (Ludo, 2026-09-28). So the first call waits for those first reports, 300 ms at most: USB pads
// report every 4 ms, and the wait only happens when a Sony pad has just been opened.
void WaitFirstReports()
{
	const ULONGLONG start = GetTickCount64();
	for (;;)
	{
		int waiting = 0, opened = 0;
		AcquireSRWLockShared(&g_lock);
		for (const Pad& p : g_pads)
			if (p.used && p.alive)
			{
				opened++;
				waiting += p.heard ? 0 : 1;
			}
		ReleaseSRWLockShared(&g_lock);
		const ULONGLONG spent = GetTickCount64() - start;
		if (!waiting || spent >= 300)
		{
			if (opened)
				Log("first reports: %d pad(s) heard in %llu ms%s\n", opened - waiting, spent,
					waiting ? ", still waiting for the others (not shown yet)" : "");
			return;
		}
		Sleep(2);
	}
}

// ---- Slots ---------------------------------------------------------------------------------

// Which slots hold an Xbox pad. The slot being asked is known exactly (the call just went to
// Windows); the others are asked again at most once a second: XInputGetState on an empty slot is
// not free, and the game asks every frame.
volatile LONG g_xbox[kSlots];
ULONGLONG g_xboxAt;
SRWLOCK g_xboxLock = SRWLOCK_INIT;

void Refresh(DWORD asked, bool connected)
{
	InterlockedExchange(&g_xbox[asked], connected ? 1 : 0);
	const ULONGLONG now = GetTickCount64();
	if (now - g_xboxAt < 1000 || !TryAcquireSRWLockExclusive(&g_xboxLock))
		return;
	g_xboxAt = now;
	for (DWORD i = 0; i < kSlots; i++)
	{
		if (i == asked)
			continue;
		XState s;
		InterlockedExchange(&g_xbox[i], g_sys.getState && g_sys.getState(i, &s) == ERROR_SUCCESS ? 1 : 0);
	}
	ReleaseSRWLockExclusive(&g_xboxLock);
}

// The PlayStation pad shown on `user` (a slot no Xbox pad holds): the k-th free slot gets the
// k-th pad connected. Call with g_lock held.
Pad* Shown(DWORD user)
{
	if (!g_playstation || user >= kSlots)
		return nullptr;
	DWORD k = 0;
	for (DWORD i = 0; i < user; i++)
		if (!g_xbox[i])
			k++;
	Pad* order[kSlots];
	DWORD n = 0;
	for (Pad& p : g_pads)
		if (p.used && p.fresh)
			order[n++] = &p;
	for (DWORD i = 1; i < n; i++)   // by connection order (four at most)
		for (DWORD j = i; j > 0 && order[j]->order < order[j - 1]->order; j--)
		{
			Pad* t = order[j];
			order[j] = order[j - 1];
			order[j - 1] = t;
		}
	return k < n ? order[k] : nullptr;
}

// ---- Start ---------------------------------------------------------------------------------

void ReadSettings()
{
	wchar_t ini[MAX_PATH];
	Beside(L"d3d9.ini", ini);
	g_playstation = GetPrivateProfileIntW(L"Accio.Controller", L"PlayStation", 1, ini) != 0;
	g_rumble = GetPrivateProfileIntW(L"Accio.Controller", L"Rumble", 1, ini) != 0;
	wchar_t light[64];
	GetPrivateProfileStringW(L"Accio.Controller", L"LightBar", L"", light, 64, ini);
	int r, g, b;
	wchar_t tail;
	// "255,110,0"; empty leaves the pad's colour alone.
	if (swscanf_s(light, L" %d , %d , %d %c", &r, &g, &b, &tail, 1) == 3 && r >= 0 && r <= 255 && g >= 0 &&
		g <= 255 && b >= 0 && b <= 255)
	{
		g_light = true;
		g_rgb[0] = static_cast<uint8_t>(r);
		g_rgb[1] = static_cast<uint8_t>(g);
		g_rgb[2] = static_cast<uint8_t>(b);
	}
	Log("settings: PlayStation=%d Rumble=%d LightBar=%s\n", g_playstation, g_rumble,
		g_light ? "set" : "left alone");
}

template <class T>
T Proc(HMODULE m, const char* name)
{
	return m ? reinterpret_cast<T>(GetProcAddress(m, name)) : nullptr;
}

BOOL CALLBACK Start(INIT_ONCE*, void*, void**)
{
	InitializeCriticalSection(&g_logLock);
	wchar_t path[MAX_PATH];
	Beside(L"xinput_accio.log", path);
	g_log = _wfsopen(path, L"w", _SH_DENYWR);
	ReadSettings();

	// Windows' own, never a copy in the game folder.
	const wchar_t* used = L"xinput1_4.dll";
	HMODULE sys = LoadLibraryExW(used, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!sys)
	{
		used = L"xinput9_1_0.dll";
		sys = LoadLibraryExW(used, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	}
	g_sys.getState = Proc<GetStateFn>(sys, "XInputGetState");
	g_sys.setState = Proc<SetStateFn>(sys, "XInputSetState");
	g_sys.getCaps = Proc<GetCapsFn>(sys, "XInputGetCapabilities");
	g_sys.enable = Proc<EnableFn>(sys, "XInputEnable");
	g_sys.audio = Proc<AudioFn>(sys, "XInputGetDSoundAudioDeviceGuids");
	g_sys.battery = Proc<BatteryFn>(sys, "XInputGetBatteryInformation");
	g_sys.keystroke = Proc<KeystrokeFn>(sys, "XInputGetKeystroke");
	g_sys.getStateEx = Proc<GetStateFn>(sys, MAKEINTRESOURCEA(100));
	g_sys.waitGuide = Proc<WaitGuideFn>(sys, MAKEINTRESOURCEA(101));
	g_sys.cancelGuide = Proc<UserFn>(sys, MAKEINTRESOURCEA(102));
	g_sys.powerOff = Proc<UserFn>(sys, MAKEINTRESOURCEA(103));
	Log("Accio xinput1_3 " ACCIO_VERSION_STR ": Xbox pads through %ls%s\n", used,
		sys ? "" : " (NOT FOUND)");

	if (g_playstation)
	{
		// Our threads run our code: stay loaded even if the game frees us.
		HMODULE pinned;
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
			reinterpret_cast<LPCWSTR>(&Start), &pinned);
		// The first scan here, so a pad already plugged in answers the game's very first call.
		Scan();
		if (HANDLE t = CreateThread(nullptr, 0, Monitor, nullptr, 0, nullptr))
			CloseHandle(t);
		WaitFirstReports();
	}
	return TRUE;
}

void Init() { InitOnceExecuteOnce(&g_once, Start, nullptr, nullptr); }

// The state of the pad on `user`, from Windows or from a PlayStation pad.
DWORD State(DWORD user, XState* out, bool guide)
{
	Init();
	if (!out)
		return ERROR_BAD_ARGUMENTS;
	GetStateFn fn = guide && g_sys.getStateEx ? g_sys.getStateEx : g_sys.getState;
	DWORD r = fn ? fn(user, out) : kNotConnected;
	if (user < kSlots)
		Refresh(user, r == ERROR_SUCCESS);
	if (r != kNotConnected)
		return r;

	static volatile LONG told[kSlots];
	AcquireSRWLockShared(&g_lock);
	if (Pad* p = Shown(user))
	{
		if (!InterlockedExchange(&told[user], 1))
			Log("slot %lu: PlayStation pad %04X given to the game\n", user, p->pid);
		out->packet = p->packet;
		out->gamepad = g_enabled ? p->state : pad::Gamepad{};
		if (!guide)
			out->gamepad.buttons &= ~pad::Guide;   // XInputGetState never shows it
		r = ERROR_SUCCESS;
	}
	ReleaseSRWLockShared(&g_lock);
	return r;
}

bool IsShown(DWORD user)
{
	AcquireSRWLockShared(&g_lock);
	const bool shown = Shown(user) != nullptr;
	ReleaseSRWLockShared(&g_lock);
	return shown;
}
}

// ---- Exports (names in xinput.def) ---------------------------------------------------------

extern "C" DWORD WINAPI AccioGetState(DWORD user, XState* state) { return State(user, state, false); }

extern "C" DWORD WINAPI AccioGetStateEx(DWORD user, XState* state) { return State(user, state, true); }

extern "C" DWORD WINAPI AccioSetState(DWORD user, XVibration* vibration)
{
	Init();
	DWORD r = g_sys.setState ? g_sys.setState(user, vibration) : kNotConnected;
	if (r != kNotConnected || !vibration)
		return r;
	AcquireSRWLockExclusive(&g_lock);
	if (Pad* p = Shown(user))
	{
		p->big = static_cast<uint8_t>(vibration->left >> 8);
		p->small = static_cast<uint8_t>(vibration->right >> 8);
		SetEvent(p->wake);
		r = ERROR_SUCCESS;
	}
	ReleaseSRWLockExclusive(&g_lock);
	return r;
}

extern "C" DWORD WINAPI AccioGetCapabilities(DWORD user, DWORD flags, XCaps* caps)
{
	Init();
	DWORD r = g_sys.getCaps ? g_sys.getCaps(user, flags, caps) : kNotConnected;
	if (r != kNotConnected || !caps || !IsShown(user))
		return r;
	// What an Xbox 360 pad answers.
	*caps = XCaps{};
	caps->type = 1;       // XINPUT_DEVTYPE_GAMEPAD
	caps->subType = 1;    // XINPUT_DEVSUBTYPE_GAMEPAD
	caps->flags = 0;      // wired, no headset
	caps->gamepad.buttons = 0xF3FF;
	caps->gamepad.leftTrigger = caps->gamepad.rightTrigger = 0xFF;
	caps->gamepad.thumbLX = caps->gamepad.thumbLY = caps->gamepad.thumbRX = caps->gamepad.thumbRY =
		static_cast<int16_t>(0xFFC0);
	if (g_rumble)
		caps->vibration = { 0xFF, 0xFF };
	return ERROR_SUCCESS;
}

extern "C" void WINAPI AccioEnable(BOOL enable)
{
	Init();
	if (g_sys.enable)
		g_sys.enable(enable);
	InterlockedExchange(&g_enabled, enable ? 1 : 0);
	AcquireSRWLockShared(&g_lock);
	for (Pad& p : g_pads)
		if (p.used)
			SetEvent(p.wake);   // stops or restores the rumble
	ReleaseSRWLockShared(&g_lock);
}

extern "C" DWORD WINAPI AccioGetDSoundAudioDeviceGuids(DWORD user, GUID* render, GUID* capture)
{
	Init();
	if (g_sys.audio)
	{
		const DWORD r = g_sys.audio(user, render, capture);
		if (r != kNotConnected)
			return r;
	}
	// xinput1_4 has no such function: a pad with no headset answers GUID_NULL twice.
	XState s;
	if (State(user, &s, false) != ERROR_SUCCESS)
		return kNotConnected;
	if (render)
		*render = GUID{};
	if (capture)
		*capture = GUID{};
	return ERROR_SUCCESS;
}

extern "C" DWORD WINAPI AccioGetBatteryInformation(DWORD user, BYTE type, XBattery* info)
{
	Init();
	DWORD r = g_sys.battery ? g_sys.battery(user, type, info) : kNotConnected;
	if (r != kNotConnected || !info || !IsShown(user))
		return r;
	*info = type == 0 ? XBattery{ 0x01, 0x03 } : XBattery{ 0x00, 0x00 };   // wired, full / no headset
	return ERROR_SUCCESS;
}

extern "C" DWORD WINAPI AccioGetKeystroke(DWORD user, DWORD reserved, XKeystroke* key)
{
	Init();
	const DWORD r = g_sys.keystroke ? g_sys.keystroke(user, reserved, key) : kNotConnected;
	if (r == kNotConnected && IsShown(user))
		return kEmpty;   // no keystroke queue for our pads
	return r;
}

extern "C" DWORD WINAPI AccioWaitForGuideButton(DWORD user, DWORD flags, void* wait)
{
	Init();
	return g_sys.waitGuide ? g_sys.waitGuide(user, flags, wait) : kNotConnected;
}

extern "C" DWORD WINAPI AccioCancelGuideButtonWait(DWORD user)
{
	Init();
	return g_sys.cancelGuide ? g_sys.cancelGuide(user) : kNotConnected;
}

extern "C" DWORD WINAPI AccioPowerOffController(DWORD user)
{
	Init();
	return g_sys.powerOff ? g_sys.powerOff(user) : kNotConnected;
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, void*)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		g_self = module;
		DisableThreadLibraryCalls(module);
	}
	return TRUE;
}
