// Checks dinput8.dll without a game or a pad:
//   - pad.cpp: which pads are recognised, the dead zone of both sticks (at rest, at the edge, just
//     beyond), and an Xbox pad read in a PlayStation pad's layout (buttons, right stick, triggers);
//   - the built DLL (path in argv[1]), loaded by this program: its six exports, Windows'
//     DirectInput reached through it, the keyboard made and read through the two redirected
//     methods, which now point into the DLL.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <initguid.h>   // the GUIDs below, without linking DirectInput
#include "../source/dinput8/pad.h"
#include <cstdio>

static int failures = 0;
static void Expect(bool ok, const char* what)
{
	if (!ok)
		printf("FAIL %s\n", what);
	failures += !ok;
}

static dpad::Ranges Full()
{
	return dpad::Ranges{};   // 0..65535 on every axis, as HP3 sets them
}

// guidProduct.Data1 of a pad: its product id over its vendor id.
static DWORD Product(WORD vendor, WORD product)
{
	return (static_cast<DWORD>(product) << 16) | vendor;
}

static void Identify()
{
	Expect(dpad::Identify(Product(0x054C, 0x09CC), 6, 14, 1) == dpad::Kind::PlayStation, "Sony pad recognised");
	Expect(dpad::Identify(Product(0x045E, 0x028E), 5, 10, 1) == dpad::Kind::Xbox, "XInput layout recognised");
	Expect(dpad::Identify(Product(0x0F0D, 0x0067), 5, 10, 1) == dpad::Kind::Xbox,
		"XInput layout recognised whatever the maker");
	Expect(dpad::Identify(Product(0x046D, 0xC216), 4, 12, 1) == dpad::Kind::Other, "another pad left alone");
	Expect(dpad::Identify(Product(0x045E, 0x028E), 6, 10, 1) == dpad::Kind::Other,
		"a sixth axis is not the XInput layout");
}

static void DeadZone()
{
	const dpad::Ranges r = Full();
	DIJOYSTATE s{};
	// Ludo's DualShock 4 at rest, read through DirectInput (2026-10-08).
	s.lX = 32769;
	s.lY = 33029;
	s.lZ = 33029;
	s.lRz = 31999;
	s.lRx = s.lRy = 0;
	dpad::DeadZone(s, r, 15, dpad::Kind::PlayStation);
	Expect(s.lX == 32768 && s.lY == 32768 && s.lZ == 32768 && s.lRz == 32768, "sticks at rest: the middle");
	Expect(s.lRx == 0 && s.lRy == 0, "triggers untouched");

	s.lX = 65535;
	s.lY = 0;
	dpad::DeadZone(s, r, 15, dpad::Kind::PlayStation);
	Expect(s.lX == 65535 && s.lY == 0, "the edge is still the edge");

	s.lX = 32768 + 32768 * 20 / 100;   // 20 % pushed, 15 % zone: 5/85 of the travel left
	dpad::DeadZone(s, r, 15, dpad::Kind::PlayStation);
	Expect(s.lX > 32768 + 1800 && s.lX < 32768 + 2050, "just beyond the zone: a small push");

	s = DIJOYSTATE{};
	s.lX = s.lY = s.lZ = s.lRx = s.lRy = s.lRz = 33029;
	dpad::DeadZone(s, r, 15, dpad::Kind::Xbox);
	Expect(s.lRx == 32768 && s.lRy == 32768, "Xbox: the right stick is on Rx and Ry");
	Expect(s.lZ == 33029, "Xbox: the triggers' axis is not a stick");

	s.lX = 33029;
	dpad::DeadZone(s, r, 0, dpad::Kind::PlayStation);
	Expect(s.lX == 33029, "0 = left as it is");

	dpad::Ranges narrow = r;
	narrow.x = {-1000, 1000};
	s.lX = 100;
	dpad::DeadZone(s, narrow, 15, dpad::Kind::PlayStation);
	Expect(s.lX == 0, "each axis against its own range");
}

static DIJOYSTATE XboxAtRest()
{
	DIJOYSTATE s{};
	s.lX = s.lY = s.lZ = s.lRx = s.lRy = 32767;
	s.rgdwPOV[0] = 0xFFFFFFFF;
	return s;
}

static void Xbox()
{
	const dpad::Ranges r = Full();
	DIJOYSTATE s = XboxAtRest();
	s.lRx = 65535;   // right stick right
	s.lRy = 0;       // right stick up
	dpad::AsPlayStation(s, r);
	Expect(s.lZ == 65535 && s.lRz == 0, "right stick moved to Z and Rz");
	Expect(s.lRx == 0 && s.lRy == 0, "triggers at rest: lowest, like a PlayStation pad");
	Expect(s.lX == 32767 && s.lY == 32767, "left stick unchanged");

	const struct { int xbox; int ps; const char* what; } buttons[] = {
		{0, 1, "A -> cross"}, {1, 2, "B -> circle"}, {2, 0, "X -> square"}, {3, 3, "Y -> triangle"},
		{4, 4, "LB -> L1"}, {5, 5, "RB -> R1"}, {6, dpad::kShare, "Back -> Share"},
		{7, dpad::kOptions, "Start -> Options"}, {8, 10, "L3"}, {9, 11, "R3"}};
	for (const auto& b : buttons)
	{
		s = XboxAtRest();
		s.rgbButtons[b.xbox] = 0x80;
		dpad::AsPlayStation(s, r);
		int down = 0;
		for (int i = 0; i < 14; ++i)
			down += (s.rgbButtons[i] & 0x80) != 0;
		Expect(s.rgbButtons[b.ps] == 0x80 && down == 1, b.what);
	}

	s = XboxAtRest();
	s.lZ = 65535;   // left trigger, fully
	dpad::AsPlayStation(s, r);
	Expect(s.lRx == 65535 && s.lRy == 0, "left trigger -> L2 axis");
	Expect(s.rgbButtons[dpad::kL2] == 0x80 && !s.rgbButtons[dpad::kR2], "left trigger -> L2 button");

	s = XboxAtRest();
	s.lZ = 0;   // right trigger, fully
	dpad::AsPlayStation(s, r);
	Expect(s.lRy == 65535 && s.lRx == 0, "right trigger -> R2 axis");
	Expect(s.rgbButtons[dpad::kR2] == 0x80 && !s.rgbButtons[dpad::kL2], "right trigger -> R2 button");

	s = XboxAtRest();
	s.lZ = 32767 + 32767 / 5;   // a fifth: the axis moves, not yet the button
	dpad::AsPlayStation(s, r);
	Expect(s.lRx > 0 && !s.rgbButtons[dpad::kL2], "a light press is not the button");
}

using CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

static bool Inside(HMODULE dll, void* code)
{
	HMODULE owner = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		static_cast<LPCWSTR>(code), &owner);
	return owner == dll;
}

static void Dll(const char* path)
{
	HMODULE dll = LoadLibraryA(path);
	Expect(dll != nullptr, "dinput8.dll loads");
	if (!dll)
		return;
	const char* names[] = {"DirectInput8Create", "DllCanUnloadNow", "DllGetClassObject", "DllRegisterServer",
		"DllUnregisterServer", "GetdfDIJoystick"};
	for (const char* name : names)
		Expect(GetProcAddress(dll, name) != nullptr, name);

	auto create = reinterpret_cast<CreateFn>(GetProcAddress(dll, "DirectInput8Create"));
	IDirectInput8W* di = nullptr;
	const HRESULT hr = create ? create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
		reinterpret_cast<void**>(&di), nullptr) : E_FAIL;
	Expect(SUCCEEDED(hr) && di, "Windows' DirectInput reached through it");
	if (!di)
		return;
	void** table = *reinterpret_cast<void***>(di);
	Expect(Inside(dll, table[3]), "CreateDevice redirected into the DLL");

	IDirectInputDevice8W* keyboard = nullptr;
	Expect(SUCCEEDED(di->CreateDevice(GUID_SysKeyboard, &keyboard, nullptr)) && keyboard,
		"a device made through the redirected CreateDevice");
	if (keyboard)
	{
		void** deviceTable = *reinterpret_cast<void***>(keyboard);
		Expect(Inside(dll, deviceTable[9]), "GetDeviceState redirected into the DLL");
		BYTE keys[256]{};
		const HRESULT state = keyboard->GetDeviceState(sizeof(keys), keys);
		// No format, not acquired: Windows' own refusal, passed through untouched.
		Expect(FAILED(state), "a keyboard read passes through");
		keyboard->Release();
	}
	di->Release();
}

int main(int argc, char** argv)
{
	Identify();
	DeadZone();
	Xbox();
	if (argc > 1)
		Dll(argv[1]);
	if (failures)
	{
		printf("%d failure(s)\n", failures);
		return 1;
	}
	puts("dinput8: all checks passed");
	return 0;
}
