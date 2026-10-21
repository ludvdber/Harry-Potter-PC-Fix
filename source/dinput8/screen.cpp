// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// HP3 draws with Direct3D 8: its D3DDrv.dll calls Direct3DCreate8 of the d3d8.dll next to the exe
// (the graphics settings' translator, or Windows' own without it), makes one device and presents
// each frame through the device's Present (slot 15 of its table, read in D3DDrv.dll, notes/HP3.md).
// No wrapper, as everywhere in the fix: on the game's thread, from DirectInput8Create, we ask that
// same d3d8.dll for an IDirect3D8 of our own, point CreateDevice of its (shared) table at us, and
// let it go; the device D3DDrv makes later goes through our CreateDevice, where Present and Reset
// of its table are pointed at us in turn. Before each Present: frame counted, keys read, screenshot
// taken if asked, the panel drawn over the game's finished image.
//
// What the overlay of d3d9.dll has and this one has not: CPU, GPU, memory and latency lines, the
// frame limiter (HP3 holds itself at 60 in its engine) and our own pixel shaders (fixed pipeline
// only, which the translator runs). The numbers, keys, files and ini keys are the same.
#include "screen.h"
#include "d3d8min.h"
#include "perf.h"
#include "redirect.h"
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")

HMODULE AccioSelf();
bool AccioGameInFront();

namespace
{
screen::Settings g_s;
LARGE_INTEGER g_qpf = {};
perf::FrameClock g_clock;
perf::Benchmark g_bench;
char g_summary[160] = "";
double g_summaryUntil = 0;
bool g_visible = true;
LONG g_frames = 0;
thread_local int g_inside = 0;   // our own Direct3D calls, not the game's

double Now()
{
	if (!g_qpf.QuadPart)
		QueryPerformanceFrequency(&g_qpf);
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return static_cast<double>(t.QuadPart) / static_cast<double>(g_qpf.QuadPart);
}

struct Inside
{
	Inside() { ++g_inside; }
	~Inside() { --g_inside; }
};

bool Pressed(int key, bool& wasDown)
{
	if (!key)
		return false;
	const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
	const bool pressed = down && !wasDown;
	wasDown = down;
	return pressed && AccioGameInFront();
}

// A folder next to this DLL (the game's system folder): benchmarks\ or screenshots\.
bool Beside(const wchar_t* name, wchar_t (&out)[MAX_PATH])
{
	GetModuleFileNameW(AccioSelf(), out, MAX_PATH);
	wchar_t* slash = wcsrchr(out, L'\\');
	if (!slash)
		return false;
	wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - out), name);
	return true;
}

// Wine's ntdll exports wine_get_version; Windows' does not.
bool UnderWine()
{
	HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	return ntdll && GetProcAddress(ntdll, "wine_get_version");
}

// ---- Benchmark ---------------------------------------------------------------------------------

void StopBenchmark(double now)
{
	const perf::Summary s = g_bench.Stop(now);
	if (!s.valid)
		return;
	perf::Describe(g_summary, sizeof(g_summary), s);
	g_summaryUntil = now + 15;
	AccioLog("%s\n", g_summary);

	wchar_t dir[MAX_PATH], exe[MAX_PATH], file[MAX_PATH];
	if (!Beside(L"benchmarks", dir))
		return;
	CreateDirectoryW(dir, nullptr);
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	wchar_t* name = wcsrchr(exe, L'\\');
	name = name ? name + 1 : exe;
	if (wchar_t* dot = wcsrchr(name, L'.'))
		*dot = L'\0';
	SYSTEMTIME t;
	GetLocalTime(&t);
	swprintf_s(file, L"%s\\%s_%04u-%02u-%02u_%02u-%02u-%02u.csv", dir, name, t.wYear, t.wMonth, t.wDay, t.wHour,
		t.wMinute, t.wSecond);
	FILE* f = nullptr;
	if (_wfopen_s(&f, file, L"w") == 0 && f)
	{
		perf::WriteCsv(f, s, g_bench.Frames());
		fclose(f);
		AccioLog("Benchmark saved: %ls\n", file);
	}
}

// ---- Screenshots -------------------------------------------------------------------------------
// The picture is copied off the GPU at once, then written as PNG by Windows' own image codecs
// (WIC, which Wine has too) on a thread of its own: the game does not stall while it compresses.

struct Shot
{
	wchar_t file[MAX_PATH];
	UINT width = 0, height = 0;
	BYTE* bgr = nullptr;   // 24-bit rows, top to bottom, no padding
};

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
	AccioLog("Screenshot %s: %ls\n", ok ? "saved" : "FAILED", s->file);
	delete[] s->bgr;
	delete s;
	return 0;
}

// Where the next picture goes: the launcher's folder for this game, or screenshots\ next to us.
bool ShotFileName(wchar_t (&file)[MAX_PATH])
{
	wchar_t dir[MAX_PATH];
	if (g_s.screenshotFolder[0])
		wcscpy_s(dir, g_s.screenshotFolder);
	else if (!Beside(L"screenshots", dir))
		return false;
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

// 32-bit rows to packed 24-bit BGR.
void Pack(const D3DLOCKED_RECT8& r, UINT width, UINT height, BYTE* out, UINT x0 = 0, UINT y0 = 0)
{
	for (UINT y = 0; y < height; y++)
	{
		const BYTE* src = static_cast<const BYTE*>(r.pBits) + static_cast<size_t>(y + y0) * r.Pitch + static_cast<size_t>(x0) * 4;
		BYTE* dst = out + static_cast<size_t>(y) * width * 3;
		for (UINT x = 0; x < width; x++)
		{
			dst[x * 3] = src[x * 4];
			dst[x * 3 + 1] = src[x * 4 + 1];
			dst[x * 3 + 2] = src[x * 4 + 2];
		}
	}
}

// The back buffer copied to system memory (CopyRects, the Direct3D 8 way). A multisampled back
// buffer cannot be copied so: then the front buffer, which Direct3D 8 gives for the whole screen
// in a window (cut to the game's client area) and as it is full screen.
void TakeScreenshot(IDirect3DDevice8* dev)
{
	Inside inside;
	IDirect3DSurface8* bb = nullptr;
	if (FAILED(dev->GetBackBuffer(0, kD3D8BackBufferMono, &bb)) || !bb)
	{
		AccioLog("Screenshot: no back buffer\n");
		return;
	}
	D3DSURFACE_DESC8 d = {};
	bb->GetDesc(&d);
	Shot* shot = nullptr;
	IDirect3DSurface8* sys = nullptr;
	if ((d.Format == D3DFMT8_X8R8G8B8 || d.Format == D3DFMT8_A8R8G8B8)
		&& SUCCEEDED(dev->CreateImageSurface(d.Width, d.Height, d.Format, &sys))
		&& SUCCEEDED(dev->CopyRects(bb, nullptr, 0, sys, nullptr)))
	{
		D3DLOCKED_RECT8 r = {};
		if (SUCCEEDED(sys->LockRect(&r, nullptr, kD3D8LockReadOnly)))
		{
			shot = new Shot;
			shot->width = d.Width;
			shot->height = d.Height;
			shot->bgr = new BYTE[static_cast<size_t>(d.Width) * d.Height * 3];
			Pack(r, d.Width, d.Height, shot->bgr);
			sys->UnlockRect();
		}
	}
	else
	{
		if (sys)
			sys->Release();
		sys = nullptr;
		// GetFrontBuffer wants an A8R8G8B8 surface the size of the screen (windowed) or of the mode.
		HWND window = GetForegroundWindow();
		RECT client = {};
		const UINT sw = static_cast<UINT>(GetSystemMetrics(SM_CXSCREEN)), sh = static_cast<UINT>(GetSystemMetrics(SM_CYSCREEN));
		if (SUCCEEDED(dev->CreateImageSurface(sw, sh, D3DFMT8_A8R8G8B8, &sys)) && SUCCEEDED(dev->GetFrontBuffer(sys)))
		{
			D3DLOCKED_RECT8 r = {};
			if (SUCCEEDED(sys->LockRect(&r, nullptr, kD3D8LockReadOnly)))
			{
				UINT x0 = 0, y0 = 0, w = sw, h = sh;
				if (window && GetClientRect(window, &client))
				{
					POINT origin = { 0, 0 };
					ClientToScreen(window, &origin);
					const LONG left = std::clamp(origin.x, 0L, static_cast<LONG>(sw)), top = std::clamp(origin.y, 0L, static_cast<LONG>(sh));
					const LONG right = std::clamp(origin.x + client.right, 0L, static_cast<LONG>(sw)), bottom = std::clamp(origin.y + client.bottom, 0L, static_cast<LONG>(sh));
					if (right > left && bottom > top)
					{
						x0 = left; y0 = top; w = right - left; h = bottom - top;
					}
				}
				shot = new Shot;
				shot->width = w;
				shot->height = h;
				shot->bgr = new BYTE[static_cast<size_t>(w) * h * 3];
				Pack(r, w, h, shot->bgr, x0, y0);
				sys->UnlockRect();
				AccioLog("Screenshot: back buffer (format %u) not copied, front buffer taken instead\n", static_cast<unsigned>(d.Format));
			}
		}
	}
	if (shot)
	{
		if (ShotFileName(shot->file))
		{
			if (HANDLE worker = CreateThread(nullptr, 0, SaveShot, shot, 0, nullptr))
			{
				CloseHandle(worker);
				shot = nullptr;   // the worker owns it now
			}
			else
				AccioLog("Screenshot: no thread to save it\n");
		}
		if (shot)
		{
			delete[] shot->bgr;
			delete shot;
		}
	}
	else
		AccioLog("Screenshot: not taken (back buffer %u x %u, format %u)\n", d.Width, d.Height, static_cast<unsigned>(d.Format));
	if (sys)
		sys->Release();
	bb->Release();
}

// ---- Drawing -----------------------------------------------------------------------------------

struct Vertex { float x, y, z, w; D3DCOLOR c; };
struct GlyphVertex { float x, y, z, w; D3DCOLOR c; float u, v; };

// The panel's text, from a texture of glyphs that Windows' own text renderer (GDI) paints once:
// printable ASCII is all the panel ever writes. A managed texture: it survives a device Reset.
class GlyphFont
{
public:
	bool Ready(IDirect3DDevice8* dev, int height) const { return m_tex && m_dev == dev && m_height == height; }

	bool Create(IDirect3DDevice8* dev, int height)
	{
		Release();
		HDC dc = CreateCompatibleDC(nullptr);
		HFONT font = CreateFontA(height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
			CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
		if (!dc || !font)
		{
			if (font) DeleteObject(font);
			if (dc) DeleteDC(dc);
			return false;
		}
		HGDIOBJ oldFont = SelectObject(dc, font);
		TEXTMETRICA tm = {};
		GetTextMetricsA(dc, &tm);
		m_lineHeight = tm.tmHeight;
		const int sheetW = 512;
		int x = 1, y = 1;
		for (int ch = kFirst; ch <= kLast; ch++)
		{
			SIZE s = {};
			const char c = static_cast<char>(ch);
			GetTextExtentPoint32A(dc, &c, 1, &s);
			if (x + s.cx + 2 > sheetW)
			{
				x = 1;
				y += tm.tmHeight + 2;
			}
			m_glyphs[ch - kFirst] = { x, y, static_cast<int>(s.cx) };
			x += s.cx + 2;
		}
		int sheetH = 1;
		while (sheetH < y + tm.tmHeight + 1)
			sheetH <<= 1;

		BITMAPINFO bi = {};
		bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
		bi.bmiHeader.biWidth = sheetW;
		bi.bmiHeader.biHeight = -sheetH;   // top-down
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;
		void* pixels = nullptr;
		HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &pixels, nullptr, 0);
		bool ok = false;
		if (bmp && pixels)
		{
			HGDIOBJ oldBmp = SelectObject(dc, bmp);
			memset(pixels, 0, static_cast<size_t>(sheetW) * sheetH * 4);
			SetTextColor(dc, RGB(255, 255, 255));
			SetBkMode(dc, TRANSPARENT);
			for (int ch = kFirst; ch <= kLast; ch++)
			{
				const char c = static_cast<char>(ch);
				TextOutA(dc, m_glyphs[ch - kFirst].x, m_glyphs[ch - kFirst].y, &c, 1);
			}
			GdiFlush();
			// White everywhere, the painted grey as coverage.
			if (SUCCEEDED(dev->CreateTexture(sheetW, sheetH, 1, 0, D3DFMT8_A8R8G8B8, D3DPOOL8_MANAGED, &m_tex)))
			{
				D3DLOCKED_RECT8 r = {};
				if (SUCCEEDED(m_tex->LockRect(0, &r, nullptr, 0)))
				{
					for (int row = 0; row < sheetH; row++)
					{
						const DWORD* src = static_cast<const DWORD*>(pixels) + static_cast<size_t>(row) * sheetW;
						DWORD* dst = reinterpret_cast<DWORD*>(static_cast<BYTE*>(r.pBits) + static_cast<size_t>(row) * r.Pitch);
						for (int col = 0; col < sheetW; col++)
						{
							const DWORD p = src[col];
							const DWORD cover = std::max({ p & 0xFF, (p >> 8) & 0xFF, (p >> 16) & 0xFF });
							dst[col] = cover << 24 | 0x00FFFFFF;
						}
					}
					m_tex->UnlockRect(0);
					ok = true;
				}
			}
			SelectObject(dc, oldBmp);
		}
		if (bmp) DeleteObject(bmp);
		SelectObject(dc, oldFont);
		DeleteObject(font);
		DeleteDC(dc);
		if (!ok)
		{
			Release();
			return false;
		}
		m_dev = dev;
		m_height = height;
		m_sheetW = static_cast<float>(sheetW);
		m_sheetH = static_cast<float>(sheetH);
		return true;
	}

	void Release()
	{
		if (m_tex)
			m_tex->Release();
		m_tex = nullptr;
		m_dev = nullptr;
		m_height = 0;
	}

	int Width(const char* text) const
	{
		int w = 0;
		for (const char* p = text; *p; p++)
			w += Glyph(*p).width;
		return w;
	}

	void Add(std::vector<GlyphVertex>& v, float x, float y, const char* text, D3DCOLOR colour) const
	{
		for (const char* p = text; *p; p++)
		{
			const Cell& g = Glyph(*p);
			if (*p != ' ')
			{
				// Pixel centres: a quad edge on x - 0.5 samples each texel exactly once.
				const float l = x - 0.5f, t = y - 0.5f, r = l + g.width, b = t + m_lineHeight;
				const float u0 = g.x / m_sheetW, v0 = g.y / m_sheetH;
				const float u1 = (g.x + g.width) / m_sheetW, v1 = (g.y + m_lineHeight) / m_sheetH;
				const GlyphVertex a = { l, t, 0, 1, colour, u0, v0 }, bq = { r, t, 0, 1, colour, u1, v0 };
				const GlyphVertex c = { l, b, 0, 1, colour, u0, v1 }, d = { r, b, 0, 1, colour, u1, v1 };
				v.push_back(a); v.push_back(bq); v.push_back(c);
				v.push_back(bq); v.push_back(d); v.push_back(c);
			}
			x += g.width;
		}
	}

	IDirect3DTexture8* Texture() const { return m_tex; }

private:
	static constexpr int kFirst = 32, kLast = 126;
	struct Cell { int x, y, width; };
	const Cell& Glyph(char c) const
	{
		const int i = static_cast<unsigned char>(c);
		return m_glyphs[(i >= kFirst && i <= kLast ? i : '?') - kFirst];
	}

	IDirect3DTexture8* m_tex = nullptr;
	IDirect3DDevice8* m_dev = nullptr;
	int m_height = 0, m_lineHeight = 0;
	float m_sheetW = 1, m_sheetH = 1;
	Cell m_glyphs[kLast - kFirst + 1] = {};
};

GlyphFont g_font;

void Once(const char* why)
{
	static const char* last = nullptr;
	if (last != why)
		AccioLog("Overlay: skipped, %s (frame %ld)\n", why, g_frames);
	last = why;
}

void Rect(std::vector<Vertex>& v, float x, float y, float w, float h, D3DCOLOR c)
{
	const Vertex a = { x, y, 0, 1, c }, b = { x + w, y, 0, 1, c }, d = { x, y + h, 0, 1, c }, e = { x + w, y + h, 0, 1, c };
	v.push_back(a); v.push_back(b); v.push_back(d);
	v.push_back(b); v.push_back(e); v.push_back(d);
}

bool LineChosen() { return g_s.showFps || g_s.showFrameTime || g_s.showGraph; }

const char* KeyName(int vk)
{
	static char name[16];
	if (vk >= VK_F1 && vk <= VK_F24)
		sprintf_s(name, "F%d", vk - VK_F1 + 1);
	else if (!GetKeyNameTextA(static_cast<LONG>(MapVirtualKeyA(vk, MAPVK_VK_TO_VSC)) << 16, name, sizeof(name)))
		strcpy_s(name, "the key");
	return name;
}

void Draw(IDirect3DDevice8* dev, double now)
{
	IDirect3DSurface8* bb = nullptr;
	if (FAILED(dev->GetBackBuffer(0, kD3D8BackBufferMono, &bb)) || !bb)
	{
		Once("no back buffer");
		return;
	}
	D3DSURFACE_DESC8 desc = {};
	bb->GetDesc(&desc);
	const float scale = desc.Height / 1080.0f * g_s.size / 100.0f;
	const int fontHeight = std::max(10, static_cast<int>(22 * scale));

	char lines[4][160];
	int n = 0;
	const bool chosen = LineChosen();
	if (g_visible)
	{
		if (g_s.showFps || !chosen)
			sprintf_s(lines[n++], "%.0f FPS", g_clock.Fps());
		if (g_s.showFrameTime || !chosen)
			sprintf_s(lines[n++], "%.1f ms   1%% low %.0f FPS", g_clock.FrameMs(), g_clock.Low1());
	}
	if (g_bench.Recording())
		sprintf_s(lines[n++], "REC  %.0f s   %s to stop", g_bench.Since(now), KeyName(g_s.benchmarkKey));
	else if (g_summary[0] && now < g_summaryUntil)
		sprintf_s(lines[n++], "%s", g_summary);
	const bool graph = g_visible && g_s.showGraph;
	if (!n && !graph)
	{
		Once("nothing to show");
		bb->Release();
		return;
	}
	if (n && !g_font.Ready(dev, fontHeight) && !g_font.Create(dev, fontHeight))
	{
		Once("font not created");
		bb->Release();
		return;
	}

	const float pad = 8 * scale, lineH = fontHeight * 1.2f, graphH = 70 * scale;
	float width = 300 * scale;
	for (int i = 0; i < n; ++i)
		width = std::max(width, static_cast<float>(g_font.Width(lines[i])) + 2 * pad);
	const float height = pad * 2 + n * lineH + (graph ? graphH + (n ? pad : 0) : 0);
	const float margin = 12 * scale;
	const bool right = g_s.position == 2 || g_s.position == 4;
	const bool bottom = g_s.position == 3 || g_s.position == 4;
	const float x0 = right ? desc.Width - width - margin : margin;
	const float y0 = bottom ? desc.Height - height - margin : margin;

	std::vector<Vertex> v;
	Rect(v, x0, y0, width, height, D3DCOLOR_ARGB(150, 6, 6, 17));
	if (graph)
	{
		const float gx = x0 + pad, gy = y0 + pad + n * lineH + (n ? pad : 0), gw = width - 2 * pad;
		float top = 33.4f;   // the graph always shows 30 FPS; it grows for longer frames, up to 100 ms
		for (int i = 0; i < perf::FrameClock::kGraph; ++i)
			top = std::max(top, std::min(g_clock.Graph(i), 100.0f));
		Rect(v, gx, gy, gw, graphH, D3DCOLOR_ARGB(90, 0, 0, 0));
		for (float ms : { 1000.0f / 60, 1000.0f / 30 })
			Rect(v, gx, gy + graphH - graphH * ms / top, gw, std::max(1.0f, scale), D3DCOLOR_ARGB(110, 255, 255, 255));
		const float bar = gw / perf::FrameClock::kGraph;
		for (int i = 0; i < perf::FrameClock::kGraph; ++i)
		{
			const float ms = std::min(g_clock.Graph(i), top);
			if (ms <= 0)
				continue;
			const float h = std::max(1.0f, graphH * ms / top);
			const D3DCOLOR c = ms <= 17.0f ? D3DCOLOR_ARGB(230, 214, 167, 44)
				: ms <= 34.0f ? D3DCOLOR_ARGB(230, 232, 149, 90) : D3DCOLOR_ARGB(240, 220, 60, 50);
			Rect(v, gx + i * bar, gy + graphH - h, std::max(bar, 1.0f), h, c);
		}
	}

	// Our state around the drawing, the game's back afterwards. A state block holds neither the
	// render target nor the depth buffer: both are saved by hand.
	DWORD saved = 0;
	if (FAILED(dev->CreateStateBlock(kD3D8StateBlockAll, &saved)))
	{
		Once("no state block");
		bb->Release();
		return;
	}
	IDirect3DSurface8 *oldTarget = nullptr, *oldDepth = nullptr;
	dev->GetRenderTarget(&oldTarget);
	dev->GetDepthStencilSurface(&oldDepth);
	const bool ownScene = SUCCEEDED(dev->BeginScene());   // fails if the game is still inside its own

	dev->SetRenderTarget(bb, nullptr);
	D3DVIEWPORT8 vp = { 0, 0, desc.Width, desc.Height, 0.0f, 1.0f };
	dev->SetViewport(&vp);
	const DWORD states[][2] = {
		{ D3DRS8_ZENABLE, FALSE }, { D3DRS8_ZWRITEENABLE, FALSE }, { D3DRS8_STENCILENABLE, FALSE },
		{ D3DRS8_CLIPPLANEENABLE, 0 }, { D3DRS8_FOGENABLE, FALSE }, { D3DRS8_LIGHTING, FALSE },
		{ D3DRS8_CULLMODE, D3DCULL8_NONE }, { D3DRS8_ALPHATESTENABLE, FALSE }, { D3DRS8_ALPHABLENDENABLE, TRUE },
		{ D3DRS8_SRCBLEND, D3DBLEND8_SRCALPHA }, { D3DRS8_DESTBLEND, D3DBLEND8_INVSRCALPHA }, { D3DRS8_BLENDOP, D3DBLENDOP8_ADD },
		{ D3DRS8_COLORWRITEENABLE, 0xF }, { D3DRS8_FILLMODE, D3DFILL8_SOLID },
	};
	for (const auto& s : states)
		dev->SetRenderState(s[0], s[1]);
	dev->SetPixelShader(0);
	dev->SetTexture(0, nullptr);
	dev->SetTextureStageState(0, D3DTSS8_COLOROP, D3DTOP8_SELECTARG1);
	dev->SetTextureStageState(0, D3DTSS8_COLORARG1, D3DTA8_DIFFUSE);
	dev->SetTextureStageState(0, D3DTSS8_ALPHAOP, D3DTOP8_SELECTARG1);
	dev->SetTextureStageState(0, D3DTSS8_ALPHAARG1, D3DTA8_DIFFUSE);
	dev->SetTextureStageState(1, D3DTSS8_COLOROP, D3DTOP8_DISABLE);
	dev->SetTextureStageState(1, D3DTSS8_ALPHAOP, D3DTOP8_DISABLE);
	dev->SetVertexShader(kD3D8FvfXyzRhw | kD3D8FvfDiffuse);
	dev->DrawPrimitiveUP(kD3D8TriangleList, static_cast<UINT>(v.size() / 3), v.data(), sizeof(Vertex));

	if (n)
	{
		// Every shadow first, then every face: two quads per glyph, one draw.
		std::vector<GlyphVertex> glyphs;
		for (int pass = 0; pass < 2; pass++)
			for (int i = 0; i < n; ++i)
			{
				const float x = std::floor(x0 + pad), y = std::floor(y0 + pad + i * lineH);
				const bool rec = g_bench.Recording() && i == n - 1;
				const D3DCOLOR face = rec ? D3DCOLOR_ARGB(255, 230, 70, 60)
					: i == 0 && (g_s.showFps || !chosen) && g_visible ? D3DCOLOR_ARGB(255, 214, 167, 44) : D3DCOLOR_ARGB(255, 240, 240, 244);
				if (pass == 0)
					g_font.Add(glyphs, x + 1, y + 1, lines[i], D3DCOLOR_ARGB(200, 0, 0, 0));
				else
					g_font.Add(glyphs, x, y, lines[i], face);
			}
		if (!glyphs.empty())
		{
			dev->SetTexture(0, g_font.Texture());
			dev->SetTextureStageState(0, D3DTSS8_COLOROP, D3DTOP8_MODULATE);
			dev->SetTextureStageState(0, D3DTSS8_COLORARG1, D3DTA8_TEXTURE);
			dev->SetTextureStageState(0, D3DTSS8_COLORARG2, D3DTA8_DIFFUSE);
			dev->SetTextureStageState(0, D3DTSS8_ALPHAOP, D3DTOP8_MODULATE);
			dev->SetTextureStageState(0, D3DTSS8_ALPHAARG1, D3DTA8_TEXTURE);
			dev->SetTextureStageState(0, D3DTSS8_ALPHAARG2, D3DTA8_DIFFUSE);
			dev->SetTextureStageState(0, D3DTSS8_TEXCOORDINDEX, 0);
			dev->SetTextureStageState(0, D3DTSS8_TEXTURETRANSFORMFLAGS, D3DTTFF8_DISABLE);
			dev->SetTextureStageState(0, D3DTSS8_MINFILTER, D3DTEXF8_POINT);
			dev->SetTextureStageState(0, D3DTSS8_MAGFILTER, D3DTEXF8_POINT);
			dev->SetTextureStageState(0, D3DTSS8_MIPFILTER, D3DTEXF8_NONE);
			dev->SetTextureStageState(0, D3DTSS8_ADDRESSU, D3DTADDRESS8_CLAMP);
			dev->SetTextureStageState(0, D3DTSS8_ADDRESSV, D3DTADDRESS8_CLAMP);
			dev->SetVertexShader(kD3D8FvfXyzRhw | kD3D8FvfDiffuse | kD3D8FvfTex1);
			dev->DrawPrimitiveUP(kD3D8TriangleList, static_cast<UINT>(glyphs.size() / 3), glyphs.data(), sizeof(GlyphVertex));
			dev->SetTexture(0, nullptr);
		}
	}

	if (ownScene)
		dev->EndScene();
	dev->ApplyStateBlock(saved);
	dev->DeleteStateBlock(saved);
	dev->SetRenderTarget(oldTarget, oldDepth);
	if (oldTarget)
		oldTarget->Release();
	if (oldDepth)
		oldDepth->Release();
	bb->Release();

	static bool logged = false;
	if (!logged)
		AccioLog("Overlay: first drawn (%u x %u, own scene %d)\n", desc.Width, desc.Height, ownScene);
	logged = true;
}

// ---- Each frame --------------------------------------------------------------------------------

void BeforePresent(IDirect3DDevice8* dev)
{
	Inside inside;
	const double now = Now();
	g_clock.Frame(now);
	g_bench.Frame(now);

	static bool started = false;
	if (!started)
		g_visible = LineChosen();   // shown from the start only when the ini asks a line; else the key
	started = true;
	static bool overlayDown = false, benchDown = false, shotDown = false;
	if (Pressed(g_s.overlayKey, overlayDown))
		g_visible = !g_visible;
	if (Pressed(g_s.benchmarkKey, benchDown))
	{
		if (g_bench.Recording())
			StopBenchmark(now);
		else
		{
			g_bench.Start(now);
			g_summary[0] = '\0';
			AccioLog("Benchmark started\n");
		}
	}
	if (Pressed(g_s.screenshotKey, shotDown))
		TakeScreenshot(dev);   // the picture is taken without the overlay

	// Under Wine the first panel drawn by d3d9.dll hung an Intel GPU (Bazzite, 2026-10-20). Until
	// that is understood, the same caution here: keys and benchmarks work, nothing is drawn.
	static int wine = -1;
	if (wine < 0)
	{
		wine = UnderWine() ? 1 : 0;
		if (wine)
			AccioLog("Overlay: not drawn under Wine; benchmarks still written\n");
	}
	if (!wine)
		Draw(dev, now);

	const LONG n = InterlockedIncrement(&g_frames);
	if (n == 1 || n % 18000 == 0)
		AccioLog("Present: frame %ld\n", n);
}

// ---- The device's table ------------------------------------------------------------------------

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice8*, const RECT*, const RECT*, HWND, const RGNDATA*);
using ResetFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice8*, D3DPRESENT_PARAMETERS8*);
using CreateDeviceFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3D8*, UINT, DWORD, HWND, DWORD, D3DPRESENT_PARAMETERS8*, IDirect3DDevice8**);

Redirect<PresentFn> g_present;
Redirect<ResetFn> g_reset;
Redirect<CreateDeviceFn> g_createDevice;

HRESULT STDMETHODCALLTYPE OurPresent(IDirect3DDevice8* dev, const RECT* src, const RECT* dst, HWND window, const RGNDATA* dirty)
{
	PresentFn original = g_present.Original(dev);
	if (!original)
		return E_FAIL;
	if (!g_inside)
		BeforePresent(dev);
	return original(dev, src, dst, window, dirty);
}

HRESULT STDMETHODCALLTYPE OurReset(IDirect3DDevice8* dev, D3DPRESENT_PARAMETERS8* pp)
{
	ResetFn original = g_reset.Original(dev);
	if (!original)
		return E_FAIL;
	g_font.Release();   // managed, so it would live through; made again at the new size anyway
	const HRESULT hr = original(dev, pp);
	if (pp)
		AccioLog("Reset: %u x %u, format %u, windowed %d -> %s\n", pp->BackBufferWidth, pp->BackBufferHeight,
			static_cast<unsigned>(pp->BackBufferFormat), pp->Windowed, SUCCEEDED(hr) ? "ok" : "FAILED");
	return hr;
}

HRESULT STDMETHODCALLTYPE OurCreateDevice(IDirect3D8* d3d, UINT adapter, DWORD type, HWND focus, DWORD flags,
	D3DPRESENT_PARAMETERS8* pp, IDirect3DDevice8** out)
{
	CreateDeviceFn original = g_createDevice.Original(d3d);
	if (!original)
		return E_FAIL;
	const HRESULT hr = original(d3d, adapter, type, focus, flags, pp, out);
	if (FAILED(hr) || !out || !*out)
	{
		AccioLog("CreateDevice: failed (0x%08lX)\n", static_cast<unsigned long>(hr));
		return hr;
	}
	if (pp)
		AccioLog("CreateDevice: %u x %u, format %u, %u back buffer(s), multisample %lu, swap %lu, windowed %d, interval %u\n",
			pp->BackBufferWidth, pp->BackBufferHeight, static_cast<unsigned>(pp->BackBufferFormat), pp->BackBufferCount,
			pp->MultiSampleType, pp->SwapEffect, pp->Windowed, pp->FullScreen_PresentationInterval);
	g_present.Install(*out, kD3D8SlotPresent, &OurPresent, "IDirect3DDevice8::Present");
	g_reset.Install(*out, kD3D8SlotReset, &OurReset, "IDirect3DDevice8::Reset");
	return hr;
}
}

namespace screen
{
void LoadSettings(const wchar_t* ini, Settings& s)
{
	const wchar_t* o = L"Accio.Overlay";
	const wchar_t* w = L"Accio.Window";
	s.showFps = GetPrivateProfileIntW(o, L"ShowFPS", s.showFps ? 1 : 0, ini) != 0;
	s.showFrameTime = GetPrivateProfileIntW(o, L"ShowFrameTime", s.showFrameTime ? 1 : 0, ini) != 0;
	s.showGraph = GetPrivateProfileIntW(o, L"ShowGraph", s.showGraph ? 1 : 0, ini) != 0;
	s.overlayKey = GetPrivateProfileIntW(o, L"OverlayKey", s.overlayKey, ini) & 0xFF;
	s.benchmarkKey = GetPrivateProfileIntW(o, L"BenchmarkKey", s.benchmarkKey, ini) & 0xFF;
	s.position = std::clamp(static_cast<int>(GetPrivateProfileIntW(o, L"Position", s.position, ini)), 1, 4);
	s.size = std::clamp(static_cast<int>(GetPrivateProfileIntW(o, L"Size", s.size, ini)), 50, 300);
	s.screenshotKey = GetPrivateProfileIntW(w, L"ScreenshotKey", s.screenshotKey, ini) & 0xFF;
	// Written by Accio Launcher in UTF-8 (a user name can hold any letter); a path typed in by hand
	// in the system code page reads as that code page instead. The A function hands the bytes over as
	// they are.
	char folder[MAX_PATH * 3] = "";
	char iniA[MAX_PATH * 3];
	WideCharToMultiByte(CP_ACP, 0, ini, -1, iniA, sizeof(iniA), nullptr, nullptr);
	if (GetPrivateProfileStringA("Accio.Window", "ScreenshotFolder", "", folder, sizeof(folder), iniA) && folder[0])
	{
		for (char* p = folder + strlen(folder); p > folder && (p[-1] == ' ' || p[-1] == '\t'); )
			*--p = '\0';
		if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, folder, -1, s.screenshotFolder, MAX_PATH))
			MultiByteToWideChar(CP_ACP, 0, folder, -1, s.screenshotFolder, MAX_PATH);
	}
}

bool Wanted(const Settings& s)
{
	return s.showFps || s.showFrameTime || s.showGraph || s.overlayKey || s.benchmarkKey || s.screenshotKey;
}

bool Install(const Settings& s)
{
	g_s = s;
	// The d3d8.dll the game runs on: already in the process (hppoa.exe imports it); by name
	// otherwise, which finds the one next to the exe first, as the game's own import would.
	HMODULE d3d8 = GetModuleHandleW(L"d3d8.dll");
	if (!d3d8)
		d3d8 = LoadLibraryW(L"d3d8.dll");
	auto create = d3d8 ? reinterpret_cast<Direct3DCreate8Fn>(GetProcAddress(d3d8, "Direct3DCreate8")) : nullptr;
	if (!create)
	{
		AccioLog("Direct3D 8: no d3d8.dll in the process, frames not watched\n");
		return false;
	}
	wchar_t path[MAX_PATH] = L"?";
	GetModuleFileNameW(d3d8, path, MAX_PATH);
	IDirect3D8* d3d = create(kD3D8SdkVersion);
	if (!d3d)
	{
		AccioLog("Direct3D 8: Direct3DCreate8 of %ls gave nothing, frames not watched\n", path);
		return false;
	}
	const bool ok = g_createDevice.Install(d3d, kD3D8SlotCreateDevice, &OurCreateDevice, "IDirect3D8::CreateDevice");
	d3d->Release();
	AccioLog("Direct3D 8: %ls, %s\n", path, ok ? "devices will be watched" : "NOT watched");
	return ok;
}
}
