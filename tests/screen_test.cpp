// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
// Loads the built dinput8.dll like HP3 would, then does what the game's D3DDrv.dll does: asks the
// d3d8.dll of the process for a device and presents frames in a window that is never shown. Checks
// that the device's Present and Reset point into dinput8.dll, that the frames are counted and the
// panel drawn (dinput8_accio.log next to the DLL), and that a Reset still goes through. No game is
// started. Needs a Direct3D 8 capable card (Windows' d3d8.dll runs on Direct3D 9): a local test.
// Usage: screen_test.exe <folder holding dinput8.dll> <frames>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <initguid.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include "../source/dinput8/d3d8min.h"
#include <cstdio>
#include <cstdlib>
#include <string>

static int failures = 0;
static void Expect(bool ok, const char* what)
{
	if (!ok)
		printf("FAIL %s\n", what);
	failures += !ok;
}

static bool Inside(HMODULE m, const void* address)
{
	HMODULE owner = nullptr;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		static_cast<const char*>(address), &owner);
	return owner == m;
}

static std::string ReadLog(const std::string& folder)
{
	FILE* f = nullptr;
	if (fopen_s(&f, (folder + "\\dinput8_accio.log").c_str(), "rb") != 0 || !f)
		return "";
	std::string text;
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		text.append(buf, n);
	fclose(f);
	return text;
}

typedef HRESULT(WINAPI* CreateFn)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

int main(int argc, char** argv)
{
	if (argc < 3)
	{
		puts("usage: screen_test.exe <folder holding dinput8.dll> <frames>");
		return 2;
	}
	const std::string folder = argv[1];
	const int frames = atoi(argv[2]);

	HMODULE dll = LoadLibraryA((folder + "\\dinput8.dll").c_str());
	Expect(dll != nullptr, "dinput8.dll loads");
	if (!dll)
		return 1;
	// What WinDrv.dll does in UWindowsClient::Init: this is where the DLL hooks Direct3D 8.
	auto create = reinterpret_cast<CreateFn>(GetProcAddress(dll, "DirectInput8Create"));
	IDirectInput8W* di = nullptr;
	Expect(create && SUCCEEDED(create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
		reinterpret_cast<void**>(&di), nullptr)) && di, "DirectInput reached through the DLL");
	if (di)
		di->Release();

	// What D3DDrv.dll does next: Direct3DCreate8 of the d3d8.dll in the process, then CreateDevice.
	HMODULE d3d8 = GetModuleHandleW(L"d3d8.dll");
	Expect(d3d8 != nullptr, "d3d8.dll loaded by the DLL");
	auto d3dCreate = d3d8 ? reinterpret_cast<Direct3DCreate8Fn>(GetProcAddress(d3d8, "Direct3DCreate8")) : nullptr;
	IDirect3D8* d3d = d3dCreate ? d3dCreate(kD3D8SdkVersion) : nullptr;
	Expect(d3d != nullptr, "Direct3DCreate8");
	if (!d3d)
		return 1;
	void** d3dTable = *reinterpret_cast<void***>(d3d);
	Expect(Inside(dll, d3dTable[kD3D8SlotCreateDevice]), "IDirect3D8::CreateDevice points into dinput8.dll");

	WNDCLASSA wc = {};
	wc.lpfnWndProc = DefWindowProcA;
	wc.hInstance = GetModuleHandleA(nullptr);
	wc.lpszClassName = "AccioScreenTest";
	RegisterClassA(&wc);
	HWND window = CreateWindowA(wc.lpszClassName, "screen_test", WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, nullptr, nullptr,
		wc.hInstance, nullptr);
	Expect(window != nullptr, "window");

	D3DPRESENT_PARAMETERS8 pp = {};
	pp.BackBufferWidth = 640;
	pp.BackBufferHeight = 480;
	pp.BackBufferFormat = D3DFMT8_X8R8G8B8;
	pp.BackBufferCount = 1;
	pp.SwapEffect = 1;   // D3DSWAPEFFECT_DISCARD, what D3DDrv asks
	pp.hDeviceWindow = window;
	pp.Windowed = TRUE;
	pp.Flags = 1;        // D3DPRESENTFLAG_LOCKABLE_BACKBUFFER, what D3DDrv asks
	IDirect3DDevice8* dev = nullptr;
	HRESULT hr = d3d->CreateDevice(0, 1 /* HAL */, window, 0x20 /* software vertex processing */, &pp, &dev);
	if (FAILED(hr))
		printf("CreateDevice: 0x%08lX\n", static_cast<unsigned long>(hr));
	Expect(SUCCEEDED(hr) && dev, "a device made through the redirected CreateDevice");
	if (!dev)
		return 1;
	void** devTable = *reinterpret_cast<void***>(dev);
	Expect(Inside(dll, devTable[kD3D8SlotPresent]), "IDirect3DDevice8::Present points into dinput8.dll");
	Expect(Inside(dll, devTable[kD3D8SlotReset]), "IDirect3DDevice8::Reset points into dinput8.dll");

	for (int i = 0; i < frames; ++i)
	{
		dev->Clear(0, nullptr, 1 /* D3DCLEAR_TARGET */, D3DCOLOR_ARGB(255, 40, 60, 90), 1.0f, 0);
		dev->BeginScene();
		dev->EndScene();
		hr = dev->Present(nullptr, nullptr, nullptr, nullptr);
		if (FAILED(hr))
		{
			printf("Present %d: 0x%08lX\n", i, static_cast<unsigned long>(hr));
			break;
		}
		Sleep(10);
	}
	Expect(SUCCEEDED(hr), "every frame presented");
	Expect(Inside(dll, (*reinterpret_cast<void***>(dev))[kD3D8SlotPresent]), "Present still ours after the frames");

	pp.BackBufferWidth = 800;
	pp.BackBufferHeight = 600;
	hr = dev->Reset(&pp);
	Expect(SUCCEEDED(hr), "Reset goes through");
	for (int i = 0; i < 20 && SUCCEEDED(hr); ++i)
	{
		dev->Clear(0, nullptr, 1, D3DCOLOR_ARGB(255, 90, 60, 40), 1.0f, 0);
		hr = dev->Present(nullptr, nullptr, nullptr, nullptr);
		Sleep(10);
	}
	Expect(SUCCEEDED(hr), "frames after the Reset");
	dev->Release();
	d3d->Release();
	DestroyWindow(window);

	const std::string log = ReadLog(folder);
	Expect(!log.empty(), "dinput8_accio.log written");
	Expect(log.find("IDirect3D8::CreateDevice: watched") != std::string::npos, "log: CreateDevice watched");
	Expect(log.find("IDirect3DDevice8::Present: watched") != std::string::npos, "log: Present watched");
	Expect(log.find("Present: frame 1\n") != std::string::npos, "log: first frame counted");
	Expect(log.find("Overlay: first drawn (640 x 480") != std::string::npos, "log: panel drawn at 640 x 480");
	Expect(log.find("Reset: 800 x 600") != std::string::npos, "log: Reset seen");

	if (failures)
	{
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("screen: all checks passed (%d frames)\n", frames);
	return 0;
}
