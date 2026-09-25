// Loads the built d3d9.dll like a game would, presents frames in a window that is never shown,
// and checks that every frame still goes through the fix. No game is started.
//
// Why: after the first text drawn with D3DX, the system d3d9.dll writes its own Present back
// into the device's method table. The overlay was then drawn on the first frame only, and the
// frame limiter stopped with it (found 2026-09-25). KeepDeviceRedirects takes the slot back.
//
// Needs a Direct3D 9 graphics card: a local test, not one for CI.
// Usage: present_test.exe <folder holding d3d9.dll> <frames>
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdlib>
#include <string>

static std::string ModuleOf(const void* address)
{
	HMODULE m = nullptr;
	char name[MAX_PATH] = "?";
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		static_cast<const char*>(address), &m) && m)
		GetModuleFileNameA(m, name, MAX_PATH);
	return name;
}

int main(int argc, char** argv)
{
	if (argc < 3)
		return 2;
	const std::string dir = argv[1];
	const int frames = atoi(argv[2]);
	const std::string ours = dir + "\\d3d9.dll";
	HMODULE fix = LoadLibraryA(ours.c_str());
	if (!fix)
	{
		printf("FAIL d3d9.dll not loaded (%lu)\n", GetLastError());
		return 1;
	}
	char fixPath[MAX_PATH];
	GetModuleFileNameA(fix, fixPath, MAX_PATH);
	using CreateFn = IDirect3D9*(WINAPI*)(UINT);
	auto create = reinterpret_cast<CreateFn>(GetProcAddress(fix, "Direct3DCreate9"));
	IDirect3D9* d3d = create ? create(D3D_SDK_VERSION) : nullptr;
	if (!d3d)
	{
		printf("FAIL Direct3DCreate9\n");
		return 1;
	}
	WNDCLASSA wc = {};
	wc.lpfnWndProc = DefWindowProcA;
	wc.hInstance = GetModuleHandleA(nullptr);
	wc.lpszClassName = "AccioPresentTest";
	RegisterClassA(&wc);
	// WS_POPUP without WS_VISIBLE: never on screen.
	HWND wnd = CreateWindowExA(0, wc.lpszClassName, "present test", WS_POPUP, 0, 0, 640, 360, nullptr, nullptr, wc.hInstance, nullptr);
	D3DPRESENT_PARAMETERS pp = {};
	pp.Windowed = TRUE;
	pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp.BackBufferWidth = 640;
	pp.BackBufferHeight = 360;
	pp.BackBufferFormat = D3DFMT_X8R8G8B8;
	pp.hDeviceWindow = wnd;
	pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
	IDirect3DDevice9* dev = nullptr;
	const HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev);
	if (FAILED(hr) || !dev)
	{
		printf("FAIL CreateDevice 0x%08lx\n", hr);
		return 1;
	}
	int failures = 0;
	for (int i = 0; i < frames; ++i)
	{
		dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 40, 0), 1.0f, 0);
		dev->BeginScene();
		dev->EndScene();
		if (FAILED(dev->Present(nullptr, nullptr, nullptr, nullptr)))
			++failures;
		void* present = (*reinterpret_cast<void***>(dev))[17];
		if (_stricmp(ModuleOf(present).c_str(), fixPath) != 0)
		{
			printf("FAIL after frame %d, Present is in %s instead of the fix\n", i + 1, ModuleOf(present).c_str());
			++failures;
			break;
		}
	}
	if (!failures)
		printf("ok   %d frames, every one through the fix\n", frames);
	dev->Release();
	d3d->Release();
	DestroyWindow(wnd);
	printf("%d failure(s)\n", failures);
	return failures ? 1 : 0;
}
