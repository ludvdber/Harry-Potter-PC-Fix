// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// What happens to each finished frame before it goes to the screen: image effects, a screenshot
// when asked, the overlay (overlay.cpp), then the wait that holds the chosen frame rate.

#include "hooks.h"
#include "render_state.h"
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
// The games take the keyboard exclusively, and Windows' own capture keys work badly over them.
// The picture is copied off the GPU at once, then written as PNG by Windows' own image codecs
// (WIC, which Wine has too) on a thread of its own: the game does not stall while it compresses.

struct Shot
{
	wchar_t file[MAX_PATH];
	UINT width = 0, height = 0;
	BYTE* bgr = nullptr;   // 24-bit rows, top to bottom, no padding
};

// Each missing folder of `path`, parent first.
void MakeFolders(const wchar_t* path)
{
	wchar_t part[MAX_PATH];
	for (size_t i = 0; path[i] && i < MAX_PATH - 1; i++)
	{
		part[i] = path[i];
		part[i + 1] = L'\0';
		if ((path[i + 1] == L'\\' || path[i + 1] == L'/' || !path[i + 1]) && !(i == 1 && path[1] == L':'))
			CreateDirectoryW(part, nullptr);
	}
}

bool WritePng(const Shot& s)
{
	IWICImagingFactory* factory = nullptr;
	IWICStream* stream = nullptr;
	IWICBitmapEncoder* encoder = nullptr;
	IWICBitmapFrameEncode* frame = nullptr;
	WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
	bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory1, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
		&& SUCCEEDED(factory->CreateStream(&stream))
		&& SUCCEEDED(stream->InitializeFromFilename(s.file, GENERIC_WRITE))
		&& SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))
		&& SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache))
		&& SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr))
		&& SUCCEEDED(frame->Initialize(nullptr))
		&& SUCCEEDED(frame->SetSize(s.width, s.height))
		&& SUCCEEDED(frame->SetPixelFormat(&format))
		&& IsEqualGUID(format, GUID_WICPixelFormat24bppBGR)
		&& SUCCEEDED(frame->WritePixels(s.height, s.width * 3, s.width * 3 * s.height, s.bgr))
		&& SUCCEEDED(frame->Commit())
		&& SUCCEEDED(encoder->Commit());
	if (frame) frame->Release();
	if (encoder) encoder->Release();
	if (stream) stream->Release();
	if (factory) factory->Release();
	if (!ok)
		DeleteFileW(s.file);   // no half-written picture left behind
	return ok;
}

DWORD WINAPI SaveShot(void* param)
{
	Shot* s = static_cast<Shot*>(param);
	const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	const bool ok = WritePng(*s);
	if (SUCCEEDED(com))
		CoUninitialize();
	char name[MAX_PATH * 3];
	WideCharToMultiByte(CP_UTF8, 0, s->file, -1, name, sizeof(name), nullptr, nullptr);
	Log("Screenshot %s: %s\n", ok ? "saved" : "FAILED", name);
	delete[] s->bgr;
	delete s;
	return 0;
}

// Where the next picture goes: the launcher's folder for this game, or "screenshots" next to it.
bool ShotFileName(wchar_t (&file)[MAX_PATH])
{
	wchar_t dir[MAX_PATH];
	if (g_cfg.screenshotFolder[0])
		wcscpy_s(dir, g_cfg.screenshotFolder);
	else
	{
		GetModuleFileNameW(g_self, dir, MAX_PATH);
		wchar_t* slash = wcsrchr(dir, L'\\');
		if (!slash)
			return false;
		wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - dir), L"screenshots");
	}
	size_t n = wcslen(dir);
	while (n && (dir[n - 1] == L'\\' || dir[n - 1] == L'/'))
		dir[--n] = L'\0';
	MakeFolders(dir);
	SYSTEMTIME t;
	GetLocalTime(&t);
	for (int k = 1; k < 100; k++)
	{
		wchar_t suffix[8] = L"";
		if (k > 1)
			swprintf_s(suffix, L"_%d", k);
		swprintf_s(file, L"%s\\%04u-%02u-%02u_%02u-%02u-%02u%s.png", dir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
			t.wSecond, suffix);
		if (GetFileAttributesW(file) == INVALID_FILE_ATTRIBUTES)
			return true;
	}
	return false;
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
		if (ShotFileName(shot->file) && SUCCEEDED(sys->LockRect(&r, nullptr, D3DLOCK_READONLY)))
		{
			shot->width = d.Width;
			shot->height = d.Height;
			shot->bgr = new BYTE[static_cast<size_t>(d.Width) * d.Height * 3];
			for (UINT y = 0; y < d.Height; y++)
			{
				const BYTE* src = static_cast<const BYTE*>(r.pBits) + static_cast<size_t>(y) * r.Pitch;
				BYTE* dst = shot->bgr + static_cast<size_t>(y) * d.Width * 3;
				for (UINT x = 0; x < d.Width; x++)
				{
					dst[x * 3] = src[x * 4];
					dst[x * 3 + 1] = src[x * 4 + 1];
					dst[x * 3 + 2] = src[x * 4 + 2];
				}
			}
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
