// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// What happens to each finished frame before it goes to the screen: image effects, the frame
// counter, a screenshot when asked, then the wait that holds the chosen frame rate.

#include "hooks.h"
#include "render_state.h"
#include "d3dx9.h"
#include <timeapi.h>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "winmm.lib")

namespace
{
// ---- Frame rate limit --------------------------------------------------------------------------

LARGE_INTEGER g_freq = {};
LONGLONG g_nextFrame = 0;

void WaitForFrameSlot()
{
	if (g_cfg.fpsLimit <= 0)
		return;
	if (!g_freq.QuadPart)
	{
		QueryPerformanceFrequency(&g_freq);
		timeBeginPeriod(1); // Sleep(1) sleeps 1 ms instead of 15
	}
	const LONGLONG period = g_freq.QuadPart / g_cfg.fpsLimit;
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	if (!g_nextFrame || now.QuadPart - g_nextFrame > period)
		g_nextFrame = now.QuadPart; // first frame, or far behind: no catching up in a burst
	for (;;)
	{
		QueryPerformanceCounter(&now);
		const LONGLONG left = g_nextFrame - now.QuadPart;
		if (left <= 0)
			break;
		// Sleep while more than 2 ms are left, give up the time slice for the rest.
		Sleep(left * 1000 / g_freq.QuadPart > 2 ? 1 : 0);
	}
	g_nextFrame += period;
}

// ---- Frame counter overlay ---------------------------------------------------------------------

ID3DXFont* g_font = nullptr;
LONGLONG g_lastFrameTime = 0;
float g_smoothedMs = 0;

void DrawFrameCounter(IDirect3DDevice9* dev)
{
	if (!g_cfg.showFps)
		return;
	LARGE_INTEGER f, now;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&now);
	if (g_lastFrameTime)
	{
		const float ms = static_cast<float>(now.QuadPart - g_lastFrameTime) * 1000.0f / static_cast<float>(f.QuadPart);
		g_smoothedMs = g_smoothedMs ? g_smoothedMs * 0.9f + ms * 0.1f : ms;
	}
	g_lastFrameTime = now.QuadPart;
	if (!g_font)
	{
		const int height = g_backBufferHeight > 0 ? g_backBufferHeight / 36 : 24;
		if (FAILED(D3DXCreateFontA(dev, height, 0, FW_BOLD, 1, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
			ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI", &g_font)))
			return;
	}
	char text[48];
	sprintf_s(text, "%.0f fps  %.1f ms", g_smoothedMs > 0 ? 1000.0f / g_smoothedMs : 0.0f, g_smoothedMs);
	if (FAILED(dev->BeginScene()))
		return;
	RECT shadow = { 13, 11, 600, 200 }, face = { 12, 10, 600, 200 };
	g_font->DrawTextA(nullptr, text, -1, &shadow, DT_NOCLIP, D3DCOLOR_ARGB(200, 0, 0, 0));
	g_font->DrawTextA(nullptr, text, -1, &face, DT_NOCLIP, D3DCOLOR_ARGB(255, 214, 167, 44));
	dev->EndScene();
}

// ---- Screenshots -------------------------------------------------------------------------------
// The games take the keyboard exclusively, and Windows' own capture keys work badly over them.

void TakeScreenshotIfAsked(IDirect3DDevice9* dev)
{
	static bool wasDown = false;
	if (!g_cfg.screenshotKey)
		return;
	const bool down = (GetAsyncKeyState(g_cfg.screenshotKey) & 0x8000) != 0;
	const bool pressed = down && !wasDown;
	wasDown = down;
	if (!pressed || !ProcessInForeground())
		return;

	IDirect3DSurface9* bb = nullptr;
	if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb)
		return;
	D3DSURFACE_DESC d;
	bb->GetDesc(&d);
	// A multisampled image cannot be saved as it is: copying it resolves it.
	IDirect3DSurface9* flat = nullptr;
	if (SUCCEEDED(dev->CreateRenderTarget(d.Width, d.Height, d.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &flat, nullptr))
		&& SUCCEEDED(dev->StretchRect(bb, nullptr, flat, nullptr, D3DTEXF_NONE)))
	{
		char dir[MAX_PATH], exe[MAX_PATH], file[MAX_PATH];
		GetModuleFileNameA(g_self, dir, MAX_PATH);
		strcpy_s(strrchr(dir, '\\') + 1, MAX_PATH - (strrchr(dir, '\\') + 1 - dir), "screenshots");
		CreateDirectoryA(dir, nullptr);
		GetModuleFileNameA(nullptr, exe, MAX_PATH);
		char* name = strrchr(exe, '\\') + 1;
		if (char* dot = strrchr(name, '.'))
			*dot = '\0';
		SYSTEMTIME t;
		GetLocalTime(&t);
		sprintf_s(file, "%s\\%s_%04u-%02u-%02u_%02u-%02u-%02u.png", dir, name, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
		const HRESULT hr = D3DXSaveSurfaceToFileA(file, D3DXIFF_PNG, flat, nullptr, nullptr);
		Log("Screenshot %s: %s\n", SUCCEEDED(hr) ? "saved" : "FAILED", file);
	}
	if (flat)
		flat->Release();
	bb->Release();
}
}

void BeforePresent(IDirect3DDevice9* dev)
{
	InternalCalls inside;
	RunPostEffects(dev);
	DrawFrameCounter(dev);
	TakeScreenshotIfAsked(dev);
	WaitForFrameSlot();
	g_depth.aoDoneThisFrame = false;
	const LONG n = InterlockedIncrement(&g_frames);
	if (n == 1 || n % 18000 == 0)
		Log("Present: frame %ld\n", n);
}

void OnDeviceLost()
{
	ReleaseEffects();
	if (g_font)
		g_font->OnLostDevice();
}

void OnDeviceRestored(IDirect3DDevice9*)
{
	if (g_font)
		g_font->OnResetDevice();
}
