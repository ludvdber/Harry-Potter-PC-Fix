// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// What happens to each finished frame before it goes to the screen: image effects, a screenshot
// when asked, the overlay (overlay.cpp), then the wait that holds the chosen frame rate.

#include "hooks.h"
#include "render_state.h"
#include "shot.h"
#include <timeapi.h>
#include <wincodec.h>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
// ---- Frame rate limit --------------------------------------------------------------------------

LARGE_INTEGER g_freq = {};
LONGLONG g_nextFrame = 0;

// FPSCeiling: HP7 part 1 plays its cut-scenes faster above 60 frames per second (Ludo,
// 2026-10-01), so a limit of 0 (none) or above the ceiling is held at the ceiling.
int EffectiveLimit()
{
	const int ceiling = g_cfg.fpsCeiling;
	if (ceiling > 0 && (g_cfg.fpsLimit <= 0 || g_cfg.fpsLimit > ceiling))
		return ceiling;
	return g_cfg.fpsLimit;
}

void WaitForFrameSlot()
{
	const int limit = EffectiveLimit();
	if (limit <= 0)
		return;
	if (!g_freq.QuadPart)
	{
		QueryPerformanceFrequency(&g_freq);
		timeBeginPeriod(1); // Sleep(1) sleeps 1 ms instead of 15
	}
	const LONGLONG period = g_freq.QuadPart / limit;
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

// ---- Screenshots -------------------------------------------------------------------------------
// The picture goes to the disk as shot.h says (shared with winmm.dll): the folder, the PNG, the
// saving thread. Here only the Direct3D 9 part: the back buffer off the GPU.

using shot::Shot;

DWORD WINAPI SaveShot(void* param)
{
	Shot* s = static_cast<Shot*>(param);
	const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	const bool ok = shot::WritePng(*s);
	if (SUCCEEDED(com))
		CoUninitialize();
	char name[MAX_PATH * 3];
	WideCharToMultiByte(CP_UTF8, 0, s->file, -1, name, sizeof(name), nullptr, nullptr);
	Log("Screenshot %s: %s\n", ok ? "saved" : "FAILED", name);
	delete[] s->bgr;
	delete s;
	return 0;
}

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
	if (d.Format != D3DFMT_X8R8G8B8 && d.Format != D3DFMT_A8R8G8B8)
	{
		Log("Screenshot: back buffer format %d not handled\n", static_cast<int>(d.Format));
		bb->Release();
		return;
	}
	// A multisampled image cannot be read as it is: copying it resolves it. Then off the GPU.
	IDirect3DSurface9* flat = nullptr;
	IDirect3DSurface9* sys = nullptr;
	Shot* shot = nullptr;
	if (SUCCEEDED(dev->CreateRenderTarget(d.Width, d.Height, d.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &flat, nullptr))
		&& SUCCEEDED(dev->StretchRect(bb, nullptr, flat, nullptr, D3DTEXF_NONE))
		&& SUCCEEDED(dev->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &sys, nullptr))
		&& SUCCEEDED(dev->GetRenderTargetData(flat, sys)))
	{
		D3DLOCKED_RECT r = {};
		shot = new Shot;
		if (shot::FileName(g_self, g_cfg.screenshotFolder, shot->file) && SUCCEEDED(sys->LockRect(&r, nullptr, D3DLOCK_READONLY)))
		{
			shot->width = d.Width;
			shot->height = d.Height;
			shot->bgr = new BYTE[static_cast<size_t>(d.Width) * d.Height * 3];
			shot::Pack(static_cast<const BYTE*>(r.pBits), static_cast<UINT>(r.Pitch), false, *shot);
			sys->UnlockRect();
			if (HANDLE worker = CreateThread(nullptr, 0, SaveShot, shot, 0, nullptr))
			{
				CloseHandle(worker);
				shot = nullptr;   // the worker owns it now
			}
			else
				Log("Screenshot: no thread to save it\n");
		}
	}
	if (shot)
	{
		delete[] shot->bgr;
		delete shot;
	}
	if (sys)
		sys->Release();
	if (flat)
		flat->Release();
	bb->Release();
}
}

bool g_compareOff = false;

namespace {
// CompareKey: the same frame with and without our image effects, a tenth of a second apart,
// instead of two game launches that never stop on quite the same picture.
void SwitchEffectsIfAsked()
{
	static bool wasDown = false;
	if (!g_cfg.compareKey)
		return;
	const bool down = (GetAsyncKeyState(g_cfg.compareKey) & 0x8000) != 0;
	const bool pressed = down && !wasDown;
	wasDown = down;
	if (!pressed || !ProcessInForeground())
		return;
	g_compareOff = !g_compareOff;
	Log("Compare: image effects %s\n", g_compareOff ? "OFF" : "ON");
}
}

void BeforePresent(IDirect3DDevice9* dev)
{
	InternalCalls inside;
	SwitchEffectsIfAsked();
	RunPostEffects(dev);
	TakeScreenshotIfAsked(dev);   // the picture is taken without the overlay
	DrawOverlay(dev);
	KeepDeviceRedirects(dev);
	WaitForFrameSlot();
	OverlayFrameSent();
	g_depth.aoDoneThisFrame = false;
	ReportHaze();
	PollHardcore();
	const LONG n = InterlockedIncrement(&g_frames);
	ApplyLateGamePatches(n);
	ReportMipmaps(n);
	if (n == 1 || n % 18000 == 0)
		Log("Present: frame %ld\n", n);
}

void OnDeviceLost()
{
	ReleaseEffects();
	OverlayDeviceLost();
}

void OnDeviceRestored(IDirect3DDevice9*)
{
	OverlayDeviceRestored();
}
