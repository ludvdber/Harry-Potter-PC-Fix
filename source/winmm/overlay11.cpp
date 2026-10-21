// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// The frame of HP1 and HP2 (overlay11.h). The renderer ends each frame with Unlock(Blit), which
// calls Present: the relay in that slot (frame.h) takes the picture before it goes out, draws the
// panel on it, waits for the frame's slot when a limit is set, then lets the renderer present.
//
// What d3d9.dll's overlay.cpp does for the Direct3D 9 games, done here in Direct3D 11 with the
// renderer's own device: the same lines, the same keys, the same ini names, the same benchmark
// file. Frame times are measured at each frame; CPU, memory and GPU are read once a second on a
// thread of our own, never in the frame. Latency is the time from a key going down on the game's
// thread to the frame that follows leaving for the driver.

#define NOMINMAX
#include "overlay11.h"
#include "../shot.h"
// Compiled with the DLL by the FxCompile step of build/accio-winmm.vcxproj (vs_4_0, ps_4_0).
#include "overlay11_vs.h"
#include "overlay11_flat.h"
#include "overlay11_text.h"
#include <d3d11.h>
#include <dxgi1_4.h>
#include <psapi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace overlay11
{
Settings g_cfg;

namespace
{
HMODULE Self() { return reinterpret_cast<HMODULE>(&__ImageBase); }

// Windows' own winmm.dll, by its path: this DLL has the name, and must not import itself.
MMRESULT TimerPeriod(UINT ms)
{
	wchar_t path[MAX_PATH];
	const UINT n = GetSystemDirectoryW(path, MAX_PATH);
	if (!n || n + 11 >= MAX_PATH)
		return MMSYSERR_ERROR;
	wcscat_s(path, L"\\winmm.dll");
	HMODULE system = LoadLibraryW(path);
	auto begin = system ? reinterpret_cast<MMRESULT(WINAPI*)(UINT)>(GetProcAddress(system, "timeBeginPeriod")) : nullptr;
	return begin ? begin(ms) : MMSYSERR_ERROR;
}

LARGE_INTEGER g_qpf = {};
LONGLONG Ticks()
{
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return t.QuadPart;
}
double Seconds(LONGLONG ticks) { return static_cast<double>(ticks) / static_cast<double>(g_qpf.QuadPart); }

bool InFront()
{
	DWORD pid = 0;
	HWND front = GetForegroundWindow();
	return front && GetWindowThreadProcessId(front, &pid) && pid == GetCurrentProcessId();
}

bool Pressed(UINT key, bool& wasDown)
{
	if (!key)
		return false;
	const bool down = (GetAsyncKeyState(static_cast<int>(key)) & 0x8000) != 0;
	const bool pressed = down && !wasDown;
	wasDown = down;
	return pressed && InFront();
}

bool LineChosen()
{
	return g_cfg.showFps || g_cfg.showFrameTime || g_cfg.showGraph || g_cfg.showCpu || g_cfg.showGpu
		|| g_cfg.showVram || g_cfg.showRam || g_cfg.showLatency;
}

// ---- The renderer --------------------------------------------------------------------------

using Unlock = void(__fastcall*)(void* self, void* edx, int blit);
Unlock g_realUnlock;
uint8_t* g_renderer;

IDXGISwapChain* SwapChain() { return *reinterpret_cast<IDXGISwapChain**>(g_renderer + frame::kSwapChain); }
ID3D11DeviceContext* Context() { return *reinterpret_cast<ID3D11DeviceContext**>(g_renderer + frame::kContext); }

void Once(const char* why)
{
	static const char* last = nullptr;
	if (last != why)
		Log("Overlay: skipped, %s\n", why);
	last = why;
}

// ---- Measures ------------------------------------------------------------------------------

frame::Stats g_stats;
frame::Benchmark g_bench;
frame::Limiter g_limiter;
LONGLONG g_summaryUntil = 0;
char g_summary[160] = "";
volatile LONGLONG g_inputAt = 0;
float g_latencyMs = -1;
bool g_visible = true;

volatile LONG g_cpuTenths = -1, g_threadTenths = -1, g_gpuTenths = -1, g_vramMb = -1, g_ramMb = -1;
HANDLE g_renderThread = nullptr;
bool g_samplerStarted = false;

ULONGLONG FileTime(const FILETIME& f) { return (static_cast<ULONGLONG>(f.dwHighDateTime) << 32) | f.dwLowDateTime; }

typedef LONG(WINAPI* PdhOpenQueryFn)(LPCWSTR, DWORD_PTR, HANDLE*);
typedef LONG(WINAPI* PdhAddCounterFn)(HANDLE, LPCWSTR, DWORD_PTR, HANDLE*);
typedef LONG(WINAPI* PdhCollectFn)(HANDLE);
struct PdhValue { DWORD status; DWORD pad; union { LONG l; double d; LONGLONG ll; }; };
struct PdhItem { LPWSTR name; PdhValue value; };
typedef LONG(WINAPI* PdhArrayFn)(HANDLE, DWORD, LPDWORD, LPDWORD, PdhItem*);

// The adapter the renderer's device runs on, for its video memory (Direct3D 11 says which one).
IDXGIAdapter3* OpenVideoMemory()
{
	ID3D11DeviceContext* context = Context();
	ID3D11Device* device = nullptr;
	if (!context)
		return nullptr;
	context->GetDevice(&device);
	if (!device)
		return nullptr;
	IDXGIDevice* dxgi = nullptr;
	IDXGIAdapter* adapter = nullptr;
	IDXGIAdapter3* a3 = nullptr;
	if (SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi))) && dxgi
		&& SUCCEEDED(dxgi->GetAdapter(&adapter)) && adapter)
		adapter->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&a3));
	if (adapter) adapter->Release();
	if (dxgi) dxgi->Release();
	device->Release();
	return a3;
}

DWORD WINAPI Sampler(LPVOID)
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	const double cores = si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 1;

	IDXGIAdapter3* adapter = g_cfg.showVram ? OpenVideoMemory() : nullptr;
	if (g_cfg.showVram)
		Log("Overlay: video memory %s\n", adapter ? "readable" : "NOT readable");

	HANDLE query = nullptr, counter = nullptr;
	PdhCollectFn collect = nullptr;
	PdhArrayFn array = nullptr;
	if (g_cfg.showGpu)
	{
		HMODULE pdh = LoadLibraryW(L"pdh.dll");
		auto open = pdh ? reinterpret_cast<PdhOpenQueryFn>(GetProcAddress(pdh, "PdhOpenQueryW")) : nullptr;
		auto add = pdh ? reinterpret_cast<PdhAddCounterFn>(GetProcAddress(pdh, "PdhAddEnglishCounterW")) : nullptr;
		collect = pdh ? reinterpret_cast<PdhCollectFn>(GetProcAddress(pdh, "PdhCollectQueryData")) : nullptr;
		array = pdh ? reinterpret_cast<PdhArrayFn>(GetProcAddress(pdh, "PdhGetFormattedCounterArrayW")) : nullptr;
		wchar_t path[128];
		swprintf_s(path, L"\\GPU Engine(pid_%lu_*engtype_3D)\\Utilization Percentage", GetCurrentProcessId());
		const bool ok = open && add && collect && array && open(nullptr, 0, &query) == 0 && add(query, path, 0, &counter) == 0;
		if (ok)
			collect(query);
		else
			query = nullptr;
		Log("Overlay: GPU load %s\n", ok ? "readable" : "NOT readable");
	}

	FILETIME c, e, k, u;
	ULONGLONG lastProcess = 0, lastThread = 0;
	LONGLONG lastAt = Ticks();
	if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u))
		lastProcess = FileTime(k) + FileTime(u);
	if (g_renderThread && GetThreadTimes(g_renderThread, &c, &e, &k, &u))
		lastThread = FileTime(k) + FileTime(u);
	std::vector<unsigned char> buffer;
	for (;;)
	{
		Sleep(1000);
		const LONGLONG at = Ticks();
		const double wall = Seconds(at - lastAt) * 1e7;   // in the 100 ns units of FILETIME
		lastAt = at;
		if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u))
		{
			const ULONGLONG t = FileTime(k) + FileTime(u);
			InterlockedExchange(&g_cpuTenths, static_cast<LONG>((t - lastProcess) * 1000.0 / (wall * cores)));
			lastProcess = t;
		}
		if (g_renderThread && GetThreadTimes(g_renderThread, &c, &e, &k, &u))
		{
			const ULONGLONG t = FileTime(k) + FileTime(u);
			InterlockedExchange(&g_threadTenths, static_cast<LONG>((t - lastThread) * 1000.0 / wall));
			lastThread = t;
		}
		PROCESS_MEMORY_COUNTERS pmc = { sizeof(pmc) };
		if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
			InterlockedExchange(&g_ramMb, static_cast<LONG>(pmc.WorkingSetSize >> 20));
		DXGI_QUERY_VIDEO_MEMORY_INFO vm;
		if (adapter && SUCCEEDED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vm)))
			InterlockedExchange(&g_vramMb, static_cast<LONG>(vm.CurrentUsage >> 20));
		if (query && collect(query) == 0)
		{
			DWORD size = 0, count = 0;
			array(counter, 0x00000200 /* PDH_FMT_DOUBLE */, &size, &count, nullptr);
			if (size)
			{
				buffer.resize(size);
				auto* items = reinterpret_cast<PdhItem*>(buffer.data());
				if (array(counter, 0x00000200, &size, &count, items) == 0)
				{
					double sum = 0;
					for (DWORD i = 0; i < count; ++i)
						if (items[i].value.status == 0)
							sum += items[i].value.d;
					InterlockedExchange(&g_gpuTenths, static_cast<LONG>(std::min(sum, 100.0) * 10));
				}
			}
		}
	}
}

// The benchmark's frames to benchmarks\<exe>_<date>.csv next to this DLL, as d3d9.dll writes them.
void SaveBenchmark()
{
	char dir[MAX_PATH], exe[MAX_PATH], file[MAX_PATH];
	GetModuleFileNameA(Self(), dir, MAX_PATH);
	char* slash = strrchr(dir, '\\');
	if (!slash)
		return;
	strcpy_s(slash + 1, MAX_PATH - (slash + 1 - dir), "benchmarks");
	CreateDirectoryA(dir, nullptr);
	GetModuleFileNameA(nullptr, exe, MAX_PATH);
	char* name = strrchr(exe, '\\');
	name = name ? name + 1 : exe;
	if (char* dot = strrchr(name, '.'))
		*dot = '\0';
	SYSTEMTIME t;
	GetLocalTime(&t);
	sprintf_s(file, "%s\\%s_%04u-%02u-%02u_%02u-%02u-%02u.csv", dir, name, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
	FILE* f = nullptr;
	if (fopen_s(&f, file, "w") == 0 && f)
	{
		fprintf(f, "# %s\n# fps_limit=%d\nframe,ms\n", g_summary, g_cfg.fpsLimit);
		const std::vector<float>& frames = g_bench.Frames();
		for (size_t i = 0; i < frames.size(); ++i)
			fprintf(f, "%zu,%.3f\n", i + 1, frames[i]);
		fclose(f);
		Log("Benchmark saved: %s\n", file);
	}
}

// ---- Screenshot ----------------------------------------------------------------------------

DWORD WINAPI SaveShot(void* param)
{
	auto* s = static_cast<shot::Shot*>(param);
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

bool EightBits(DXGI_FORMAT f, bool& rgba)
{
	switch (f)
	{
	case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		rgba = true;
		return true;
	case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8X8_TYPELESS:
		rgba = false;
		return true;
	default:
		return false;
	}
}

// The back buffer as it is now (without the panel), off the GPU and to the saving thread. A
// multisampled picture is resolved first; then a staging copy the CPU can read.
void TakeScreenshot(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* back)
{
	D3D11_TEXTURE2D_DESC d;
	back->GetDesc(&d);
	bool rgba = false;
	if (!EightBits(d.Format, rgba))
	{
		Log("Screenshot: back buffer format %d not handled\n", static_cast<int>(d.Format));
		return;
	}
	ID3D11Texture2D* flat = nullptr;
	ID3D11Texture2D* staging = nullptr;
	ID3D11Texture2D* source = back;
	if (d.SampleDesc.Count > 1)
	{
		D3D11_TEXTURE2D_DESC fd = d;
		fd.SampleDesc.Count = 1;
		fd.SampleDesc.Quality = 0;
		fd.BindFlags = D3D11_BIND_RENDER_TARGET;
		fd.MiscFlags = 0;
		if (FAILED(device->CreateTexture2D(&fd, nullptr, &flat)))
		{
			Log("Screenshot: no texture to resolve the multisampled picture into\n");
			return;
		}
		context->ResolveSubresource(flat, 0, back, 0, d.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS ? DXGI_FORMAT_R8G8B8A8_UNORM
			: d.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS ? DXGI_FORMAT_B8G8R8A8_UNORM : d.Format);
		source = flat;
	}
	D3D11_TEXTURE2D_DESC sd = d;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Usage = D3D11_USAGE_STAGING;
	sd.BindFlags = 0;
	sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	sd.MiscFlags = 0;
	shot::Shot* s = nullptr;
	D3D11_MAPPED_SUBRESOURCE m = {};
	if (SUCCEEDED(device->CreateTexture2D(&sd, nullptr, &staging)))
	{
		context->CopyResource(staging, source);
		if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &m)))
		{
			s = new shot::Shot;
			if (shot::FileName(Self(), g_cfg.screenshotFolder, s->file))
			{
				s->width = d.Width;
				s->height = d.Height;
				s->bgr = new BYTE[static_cast<size_t>(d.Width) * d.Height * 3];
				shot::Pack(static_cast<const BYTE*>(m.pData), m.RowPitch, rgba, *s);
			}
			context->Unmap(staging, 0);
			if (s->bgr)
			{
				if (HANDLE worker = CreateThread(nullptr, 0, SaveShot, s, 0, nullptr))
				{
					CloseHandle(worker);
					s = nullptr;   // the worker owns it now
				}
				else
					Log("Screenshot: no thread to save it\n");
			}
		}
		else
			Log("Screenshot: the staging copy cannot be read\n");
	}
	else
		Log("Screenshot: no staging texture\n");
	if (s)
	{
		delete[] s->bgr;
		delete s;
	}
	if (staging) staging->Release();
	if (flat) flat->Release();
}

// ---- Drawing -------------------------------------------------------------------------------

struct Vertex { float x, y, u, v; uint32_t c; };   // clip space, glyph coordinate, ABGR (R8G8B8A8_UNORM)

uint32_t Colour(int a, int r, int g, int b) { return (static_cast<uint32_t>(a) << 24) | (b << 16) | (g << 8) | r; }

// The panel's text, from a sheet of glyphs that Windows' own text renderer (GDI) paints once:
// printable ASCII is all the panel ever writes. White everywhere, the painted grey as coverage.
class GlyphFont
{
public:
	bool Ready(int height) const { return m_view && m_height == height; }

	bool Create(ID3D11Device* device, int height)
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
			std::vector<uint32_t> sheet(static_cast<size_t>(sheetW) * sheetH);
			const auto* src = static_cast<const uint32_t*>(pixels);
			for (size_t i = 0; i < sheet.size(); i++)
			{
				const uint32_t p = src[i];
				const uint32_t cover = std::max({ p & 0xFF, (p >> 8) & 0xFF, (p >> 16) & 0xFF });
				sheet[i] = cover << 24 | 0x00FFFFFF;
			}
			D3D11_TEXTURE2D_DESC td = {};
			td.Width = sheetW;
			td.Height = sheetH;
			td.MipLevels = 1;
			td.ArraySize = 1;
			td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_IMMUTABLE;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_SUBRESOURCE_DATA data = { sheet.data(), static_cast<UINT>(sheetW * 4), 0 };
			ID3D11Texture2D* tex = nullptr;
			if (SUCCEEDED(device->CreateTexture2D(&td, &data, &tex)) && tex)
			{
				ok = SUCCEEDED(device->CreateShaderResourceView(tex, nullptr, &m_view));
				tex->Release();
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
		m_height = height;
		m_sheetW = static_cast<float>(sheetW);
		m_sheetH = static_cast<float>(sheetH);
		return true;
	}

	void Release()
	{
		if (m_view)
			m_view->Release();
		m_view = nullptr;
		m_height = 0;
	}

	int Width(const char* text) const
	{
		int w = 0;
		for (const char* p = text; *p; p++)
			w += Glyph(*p).width;
		return w;
	}

	int LineHeight() const { return m_lineHeight; }
	ID3D11ShaderResourceView* View() const { return m_view; }

	// Quads for `text` at (x, y) in pixels, top-left of the line; `toClip` converts.
	template <class ToClip>
	void Add(std::vector<Vertex>& v, float x, float y, const char* text, uint32_t colour, ToClip toClip) const
	{
		for (const char* p = text; *p; p++)
		{
			const Cell& g = Glyph(*p);
			if (*p != ' ')
			{
				const float l = x, t = y, r = l + g.width, b = t + m_lineHeight;
				const float u0 = g.x / m_sheetW, v0 = g.y / m_sheetH;
				const float u1 = (g.x + g.width) / m_sheetW, v1 = (g.y + m_lineHeight) / m_sheetH;
				Vertex a = { l, t, u0, v0, colour }, bq = { r, t, u1, v0, colour };
				Vertex c = { l, b, u0, v1, colour }, d = { r, b, u1, v1, colour };
				toClip(a); toClip(bq); toClip(c); toClip(d);
				v.push_back(a); v.push_back(bq); v.push_back(c);
				v.push_back(bq); v.push_back(d); v.push_back(c);
			}
			x += g.width;
		}
	}

private:
	static constexpr int kFirst = 32, kLast = 126;
	struct Cell { int x, y, width; };
	const Cell& Glyph(char c) const
	{
		const int i = static_cast<unsigned char>(c);
		return m_glyphs[(i >= kFirst && i <= kLast ? i : '?') - kFirst];
	}

	ID3D11ShaderResourceView* m_view = nullptr;
	int m_height = 0, m_lineHeight = 0;
	float m_sheetW = 1, m_sheetH = 1;
	Cell m_glyphs[kLast - kFirst + 1] = {};
};

GlyphFont g_font;

// Made once, from the renderer's device: the shaders, the vertex buffer and the states.
struct Pipeline
{
	ID3D11Device* device = nullptr;   // the one these belong to (not owned)
	ID3D11VertexShader* vs = nullptr;
	ID3D11PixelShader* flat = nullptr;
	ID3D11PixelShader* text = nullptr;
	ID3D11InputLayout* layout = nullptr;
	ID3D11Buffer* vertices = nullptr;
	ID3D11BlendState* blend = nullptr;
	ID3D11DepthStencilState* noDepth = nullptr;
	ID3D11RasterizerState* raster = nullptr;
	ID3D11SamplerState* sampler = nullptr;
	static constexpr UINT kVertices = 65536;

	bool Ready(ID3D11Device* dev) const { return device == dev && vs; }

	bool Create(ID3D11Device* dev)
	{
		Release();
		device = dev;
		const D3D11_INPUT_ELEMENT_DESC elements[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = kVertices * sizeof(Vertex);
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		D3D11_BLEND_DESC bl = {};
		bl.RenderTarget[0].BlendEnable = TRUE;
		bl.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		D3D11_DEPTH_STENCIL_DESC ds = {};   // depth and stencil off
		D3D11_RASTERIZER_DESC rs = {};
		rs.FillMode = D3D11_FILL_SOLID;
		rs.CullMode = D3D11_CULL_NONE;
		rs.DepthClipEnable = TRUE;
		D3D11_SAMPLER_DESC sm = {};
		sm.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sm.MaxLOD = D3D11_FLOAT32_MAX;
		const bool ok = SUCCEEDED(dev->CreateVertexShader(g_overlay11_vs, sizeof(g_overlay11_vs), nullptr, &vs))
			&& SUCCEEDED(dev->CreatePixelShader(g_overlay11_flat, sizeof(g_overlay11_flat), nullptr, &flat))
			&& SUCCEEDED(dev->CreatePixelShader(g_overlay11_text, sizeof(g_overlay11_text), nullptr, &text))
			&& SUCCEEDED(dev->CreateInputLayout(elements, 3, g_overlay11_vs, sizeof(g_overlay11_vs), &layout))
			&& SUCCEEDED(dev->CreateBuffer(&bd, nullptr, &vertices))
			&& SUCCEEDED(dev->CreateBlendState(&bl, &blend))
			&& SUCCEEDED(dev->CreateDepthStencilState(&ds, &noDepth))
			&& SUCCEEDED(dev->CreateRasterizerState(&rs, &raster))
			&& SUCCEEDED(dev->CreateSamplerState(&sm, &sampler));
		if (!ok)
			Release();
		return ok;
	}

	void Release()
	{
		if (vs) vs->Release();
		if (flat) flat->Release();
		if (text) text->Release();
		if (layout) layout->Release();
		if (vertices) vertices->Release();
		if (blend) blend->Release();
		if (noDepth) noDepth->Release();
		if (raster) raster->Release();
		if (sampler) sampler->Release();
		vs = nullptr; flat = nullptr; text = nullptr; layout = nullptr; vertices = nullptr;
		blend = nullptr; noDepth = nullptr; raster = nullptr; sampler = nullptr;
		device = nullptr;
	}
} g_pipe;

// The renderer's state around our drawing, put back afterwards: everything we set.
struct SavedState
{
	ID3D11RenderTargetView* rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
	ID3D11DepthStencilView* dsv = nullptr;
	ID3D11BlendState* blend = nullptr;
	float blendFactor[4] = {};
	UINT sampleMask = 0;
	ID3D11DepthStencilState* depth = nullptr;
	UINT stencilRef = 0;
	ID3D11RasterizerState* raster = nullptr;
	UINT viewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
	D3D11_VIEWPORT viewport[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
	ID3D11InputLayout* layout = nullptr;
	D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
	ID3D11Buffer* vb = nullptr;
	UINT stride = 0, offset = 0;
	ID3D11VertexShader* vs = nullptr;
	ID3D11PixelShader* ps = nullptr;
	ID3D11GeometryShader* gs = nullptr;
	ID3D11HullShader* hs = nullptr;
	ID3D11DomainShader* ds = nullptr;
	ID3D11ShaderResourceView* srv = nullptr;
	ID3D11SamplerState* sampler = nullptr;

	void Take(ID3D11DeviceContext* c)
	{
		c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, &dsv);
		c->OMGetBlendState(&blend, blendFactor, &sampleMask);
		c->OMGetDepthStencilState(&depth, &stencilRef);
		c->RSGetState(&raster);
		c->RSGetViewports(&viewports, viewport);
		c->IAGetInputLayout(&layout);
		c->IAGetPrimitiveTopology(&topology);
		c->IAGetVertexBuffers(0, 1, &vb, &stride, &offset);
		c->VSGetShader(&vs, nullptr, nullptr);
		c->PSGetShader(&ps, nullptr, nullptr);
		c->GSGetShader(&gs, nullptr, nullptr);
		c->HSGetShader(&hs, nullptr, nullptr);
		c->DSGetShader(&ds, nullptr, nullptr);
		c->PSGetShaderResources(0, 1, &srv);
		c->PSGetSamplers(0, 1, &sampler);
	}

	void Give(ID3D11DeviceContext* c)
	{
		c->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, dsv);
		c->OMSetBlendState(blend, blendFactor, sampleMask);
		c->OMSetDepthStencilState(depth, stencilRef);
		c->RSSetState(raster);
		c->RSSetViewports(viewports, viewport);
		c->IASetInputLayout(layout);
		c->IASetPrimitiveTopology(topology);
		c->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
		c->VSSetShader(vs, nullptr, 0);
		c->PSSetShader(ps, nullptr, 0);
		c->GSSetShader(gs, nullptr, 0);
		c->HSSetShader(hs, nullptr, 0);
		c->DSSetShader(ds, nullptr, 0);
		c->PSSetShaderResources(0, 1, &srv);
		c->PSSetSamplers(0, 1, &sampler);
		for (auto*& r : rtv) if (r) { r->Release(); r = nullptr; }
		if (dsv) dsv->Release();
		if (blend) blend->Release();
		if (depth) depth->Release();
		if (raster) raster->Release();
		if (layout) layout->Release();
		if (vb) vb->Release();
		if (vs) vs->Release();
		if (ps) ps->Release();
		if (gs) gs->Release();
		if (hs) hs->Release();
		if (ds) ds->Release();
		if (srv) srv->Release();
		if (sampler) sampler->Release();
	}
};

void Line(char* out, size_t size, const char* fmt, LONG tenths, const char* unit)
{
	if (tenths < 0)
		sprintf_s(out, size, fmt, "n/a");
	else
	{
		char value[32];
		sprintf_s(value, "%ld%s", unit[0] == '%' ? (tenths + 5) / 10 : tenths, unit);
		sprintf_s(out, size, fmt, value);
	}
}

const char* KeyName(UINT vk)
{
	static char name[16];
	if (vk >= VK_F1 && vk <= VK_F24)
		sprintf_s(name, "F%u", vk - VK_F1 + 1);
	else if (!GetKeyNameTextA(static_cast<LONG>(MapVirtualKeyA(vk, MAPVK_VK_TO_VSC)) << 16, name, sizeof(name)))
		strcpy_s(name, "the key");
	return name;
}

void Draw(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* back, LONGLONG now)
{
	D3D11_TEXTURE2D_DESC desc;
	back->GetDesc(&desc);
	const float W = static_cast<float>(desc.Width), H = static_cast<float>(desc.Height);
	const float scale = H / 1080.0f * g_cfg.size / 100.0f;
	const int fontHeight = std::max(10, static_cast<int>(22 * scale));

	// The lines, in the order they are shown; with no line chosen in the ini, the key shows the
	// frame rate and the 1 % low (an empty panel looked broken: Ludo, 2026-09-28).
	char lines[9][160];
	int n = 0;
	const bool chosen = LineChosen();
	if (g_visible)
	{
		if (g_cfg.showFps || !chosen)
			sprintf_s(lines[n++], "%.0f FPS", g_stats.Fps());
		if (g_cfg.showFrameTime || !chosen)
			sprintf_s(lines[n++], "%.1f ms   1%% low %.0f FPS", g_stats.FrameMs(), g_stats.Low1());
		if (g_cfg.showCpu)
		{
			char a[48], b[48];
			Line(a, sizeof(a), "CPU %s", g_cpuTenths, "%");
			Line(b, sizeof(b), "%s", g_threadTenths, "%");
			sprintf_s(lines[n++], "%s   game thread %s", a, b);
		}
		if (g_cfg.showGpu)
			Line(lines[n++], sizeof(lines[0]), "GPU %s", g_gpuTenths, "%");
		if (g_cfg.showVram)
			Line(lines[n++], sizeof(lines[0]), "VRAM %s", g_vramMb, " MB");
		if (g_cfg.showRam)
			Line(lines[n++], sizeof(lines[0]), "RAM %s", g_ramMb, " MB");
		if (g_cfg.showLatency)
		{
			if (g_latencyMs < 0)
				sprintf_s(lines[n++], "Latency: press a key");
			else
				sprintf_s(lines[n++], "Latency %.1f ms", g_latencyMs);
		}
	}
	if (g_bench.Recording())
		sprintf_s(lines[n++], "REC  %.0f s   %s to stop", Seconds(now) - g_bench.StartedAt(), KeyName(g_cfg.benchmarkKey));
	else if (g_summary[0] && now < g_summaryUntil)
		sprintf_s(lines[n++], "%s", g_summary);
	const bool graph = g_visible && g_cfg.showGraph;
	if (!n && !graph)
	{
		Once("nothing to show");
		return;
	}
	if (n && !g_font.Ready(fontHeight) && !g_font.Create(device, fontHeight))
	{
		Once("font NOT created");
		return;
	}
	if (!g_pipe.Ready(device) && !g_pipe.Create(device))
	{
		Once("shaders or states refused");
		return;
	}

	// Pixels to clip space; the pixel centre at -0.5 so each glyph texel is sampled once.
	auto toClip = [W, H](Vertex& v) {
		v.x = (v.x - 0.5f) / W * 2.0f - 1.0f;
		v.y = 1.0f - (v.y - 0.5f) / H * 2.0f;
	};
	auto rect = [&](std::vector<Vertex>& v, float x, float y, float w, float h, uint32_t c) {
		Vertex a = { x, y, 0, 0, c }, b = { x + w, y, 0, 0, c }, d = { x, y + h, 0, 0, c }, e = { x + w, y + h, 0, 0, c };
		toClip(a); toClip(b); toClip(d); toClip(e);
		v.push_back(a); v.push_back(b); v.push_back(d);
		v.push_back(b); v.push_back(e); v.push_back(d);
	};

	const float pad = 8 * scale, lineH = fontHeight * 1.2f, graphH = 70 * scale;
	float width = 300 * scale;
	for (int i = 0; i < n; ++i)
		width = std::max(width, static_cast<float>(g_font.Width(lines[i])) + 2 * pad);
	const float height = pad * 2 + n * lineH + (graph ? graphH + (n ? pad : 0) : 0);
	const float margin = 12 * scale;
	const bool right = g_cfg.position == 2 || g_cfg.position == 4;
	const bool bottom = g_cfg.position == 3 || g_cfg.position == 4;
	const float x0 = right ? W - width - margin : margin;
	const float y0 = bottom ? H - height - margin : margin;

	std::vector<Vertex> panel;
	rect(panel, x0, y0, width, height, Colour(150, 6, 6, 17));
	if (graph)
	{
		const float gx = x0 + pad, gy = y0 + pad + n * lineH + (n ? pad : 0), gw = width - 2 * pad;
		float top = 33.4f;   // the graph always shows 30 FPS; it grows for longer frames, up to 100 ms
		for (int i = 0; i < frame::Stats::kGraph; ++i)
			top = std::max(top, std::min(g_stats.Bar(i), 100.0f));
		rect(panel, gx, gy, gw, graphH, Colour(90, 0, 0, 0));
		for (float ms : { 1000.0f / 60, 1000.0f / 30 })   // guides at 60 and 30 FPS
			rect(panel, gx, gy + graphH - graphH * ms / top, gw, std::max(1.0f, scale), Colour(110, 255, 255, 255));
		const float bar = gw / frame::Stats::kGraph;
		for (int i = 0; i < frame::Stats::kGraph; ++i)
		{
			const float ms = std::min(g_stats.Bar(i), top);
			if (ms <= 0)
				continue;
			const float h = std::max(1.0f, graphH * ms / top);
			// Gold while the frame is on time for 60 FPS, orange up to 30, red beyond.
			const uint32_t c = ms <= 17.0f ? Colour(230, 214, 167, 44) : ms <= 34.0f ? Colour(230, 232, 149, 90) : Colour(240, 220, 60, 50);
			rect(panel, gx + i * bar, gy + graphH - h, std::max(bar, 1.0f), h, c);
		}
	}
	std::vector<Vertex> glyphs;
	for (int pass = 0; pass < 2; pass++)   // every shadow first, then every face
		for (int i = 0; i < n; ++i)
		{
			const float x = std::floor(x0 + pad), y = std::floor(y0 + pad + i * lineH);
			const bool rec = g_bench.Recording() && i == n - 1;
			const uint32_t face = rec ? Colour(255, 230, 70, 60)
				: i == 0 && (g_cfg.showFps || !chosen) && g_visible ? Colour(255, 214, 167, 44) : Colour(255, 240, 240, 244);
			if (pass == 0)
				g_font.Add(glyphs, x + 1, y + 1, lines[i], Colour(200, 0, 0, 0), toClip);
			else
				g_font.Add(glyphs, x, y, lines[i], face, toClip);
		}
	if (panel.size() + glyphs.size() > Pipeline::kVertices)
	{
		Once("too many vertices");
		return;
	}

	ID3D11RenderTargetView* target = nullptr;
	if (FAILED(device->CreateRenderTargetView(back, nullptr, &target)) || !target)
	{
		Once("no render target view on the back buffer");
		return;
	}
	D3D11_MAPPED_SUBRESOURCE m = {};
	if (FAILED(context->Map(g_pipe.vertices, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
	{
		Once("vertex buffer not writable");
		target->Release();
		return;
	}
	memcpy(m.pData, panel.data(), panel.size() * sizeof(Vertex));
	memcpy(static_cast<Vertex*>(m.pData) + panel.size(), glyphs.data(), glyphs.size() * sizeof(Vertex));
	context->Unmap(g_pipe.vertices, 0);

	SavedState saved;
	saved.Take(context);
	ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = { target };
	context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, nullptr);
	const float factor[4] = { 0, 0, 0, 0 };
	context->OMSetBlendState(g_pipe.blend, factor, 0xFFFFFFFF);
	context->OMSetDepthStencilState(g_pipe.noDepth, 0);
	context->RSSetState(g_pipe.raster);
	D3D11_VIEWPORT vp = { 0, 0, W, H, 0.0f, 1.0f };
	context->RSSetViewports(1, &vp);
	context->IASetInputLayout(g_pipe.layout);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const UINT stride = sizeof(Vertex), offset = 0;
	context->IASetVertexBuffers(0, 1, &g_pipe.vertices, &stride, &offset);
	context->VSSetShader(g_pipe.vs, nullptr, 0);
	context->GSSetShader(nullptr, nullptr, 0);
	context->HSSetShader(nullptr, nullptr, 0);
	context->DSSetShader(nullptr, nullptr, 0);
	context->PSSetShader(g_pipe.flat, nullptr, 0);
	context->Draw(static_cast<UINT>(panel.size()), 0);
	if (!glyphs.empty())
	{
		ID3D11ShaderResourceView* view = g_font.View();
		context->PSSetShader(g_pipe.text, nullptr, 0);
		context->PSSetShaderResources(0, 1, &view);
		context->PSSetSamplers(0, 1, &g_pipe.sampler);
		context->Draw(static_cast<UINT>(glyphs.size()), static_cast<UINT>(panel.size()));
	}
	saved.Give(context);
	target->Release();

	static bool logged = false;
	if (!logged)
		Log("Overlay: first drawn (%u x %u, format %d, %u samples)\n", desc.Width, desc.Height,
			static_cast<int>(desc.Format), desc.SampleDesc.Count);
	logged = true;
}

// ---- The frame -----------------------------------------------------------------------------

void WaitForFrameSlot()
{
	if (g_cfg.fpsLimit <= 0)
		return;
	static bool period = false;
	if (!period)
	{
		TimerPeriod(1);   // Sleep(1) sleeps 1 ms instead of 15
		period = true;
	}
	const LONGLONG step = g_qpf.QuadPart / g_cfg.fpsLimit;
	LONGLONG left = g_limiter.Due(Ticks(), step);
	while (left > 0)
	{
		// Sleep while more than 2 ms are left, give up the time slice for the rest.
		Sleep(left * 1000 / g_qpf.QuadPart > 2 ? 1 : 0);
		left = g_limiter.next - Ticks();
	}
	g_limiter.Taken(step);
}

void Frame()
{
	const LONGLONG now = Ticks();
	const double seconds = Seconds(now);
	g_stats.Count(seconds);
	g_bench.Count(seconds);

	if (!g_samplerStarted && (g_cfg.showCpu || g_cfg.showGpu || g_cfg.showVram || g_cfg.showRam))
	{
		g_samplerStarted = true;
		g_renderThread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentThreadId());
		if (HANDLE t = CreateThread(nullptr, 0, Sampler, nullptr, 0, nullptr))
			CloseHandle(t);
	}
	// Shown from the start only when the ini switches a line on; otherwise the key brings it up.
	static bool started = false;
	if (!started)
		g_visible = LineChosen();
	started = true;
	static bool overlayDown = false, benchDown = false, shotDown = false;
	if (Pressed(g_cfg.overlayKey, overlayDown))
		g_visible = !g_visible;
	if (Pressed(g_cfg.benchmarkKey, benchDown))
	{
		if (g_bench.Recording())
		{
			if (g_bench.Stop(seconds, g_summary, sizeof(g_summary)))
			{
				g_summaryUntil = now + g_qpf.QuadPart * 15;
				Log("%s\n", g_summary);
				SaveBenchmark();
			}
		}
		else
		{
			g_bench.Start(seconds);
			g_summary[0] = '\0';
			Log("Benchmark started\n");
		}
	}
	const bool shotAsked = Pressed(g_cfg.screenshotKey, shotDown);

	IDXGISwapChain* chain = SwapChain();
	ID3D11DeviceContext* context = Context();
	if (chain && context && (shotAsked || LineChosen() || g_cfg.overlayKey || g_cfg.benchmarkKey))
	{
		ID3D11Texture2D* back = nullptr;
		ID3D11Device* device = nullptr;
		context->GetDevice(&device);
		if (device && SUCCEEDED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) && back)
		{
			if (shotAsked)
				TakeScreenshot(device, context, back);   // the picture is taken without the panel
			if (LineChosen() || g_cfg.overlayKey || g_cfg.benchmarkKey)
				Draw(device, context, back, now);
		}
		else
			Once("no back buffer");
		if (back) back->Release();
		if (device) device->Release();
	}

	WaitForFrameSlot();
	const LONGLONG at = g_inputAt;
	if (at)
	{
		const float ms = static_cast<float>(Seconds(Ticks() - at) * 1000.0);
		g_latencyMs = g_latencyMs < 0 ? ms : g_latencyMs * 0.7f + ms * 0.3f;
		InterlockedExchange64(&g_inputAt, 0);
	}
}

void __fastcall RelayUnlock(void* self, void* edx, int blit)
{
	if (blit)
		Frame();
	g_realUnlock(self, edx, blit);
}
}

void Load(const wchar_t* ini)
{
	auto flag = [&](const wchar_t* section, const wchar_t* key, bool fallback) {
		return GetPrivateProfileIntW(section, key, fallback ? 1 : 0, ini) != 0;
	};
	auto number = [&](const wchar_t* section, const wchar_t* key, int fallback, int lo, int hi) {
		const int v = static_cast<int>(GetPrivateProfileIntW(section, key, fallback, ini));
		return std::min(std::max(v, lo), hi);
	};
	const wchar_t* o = L"Accio.Overlay";
	g_cfg.showFps = flag(o, L"ShowFPS", false);
	g_cfg.showFrameTime = flag(o, L"ShowFrameTime", false);
	g_cfg.showGraph = flag(o, L"ShowGraph", false);
	g_cfg.showCpu = flag(o, L"ShowCPU", false);
	g_cfg.showGpu = flag(o, L"ShowGPU", false);
	g_cfg.showVram = flag(o, L"ShowVRAM", false);
	g_cfg.showRam = flag(o, L"ShowRAM", false);
	g_cfg.showLatency = flag(o, L"ShowLatency", false);
	g_cfg.overlayKey = static_cast<UINT>(number(o, L"OverlayKey", VK_F10, 0, 255));
	g_cfg.benchmarkKey = static_cast<UINT>(number(o, L"BenchmarkKey", VK_F11, 0, 255));
	g_cfg.position = number(o, L"Position", 1, 1, 4);
	g_cfg.size = number(o, L"Size", 100, 50, 300);
	const wchar_t* w = L"Accio.Window";
	g_cfg.fpsLimit = number(w, L"FPSLimit", 0, 0, 1000);
	g_cfg.screenshotKey = static_cast<UINT>(number(w, L"ScreenshotKey", VK_F12, 0, 255));
	GetPrivateProfileStringW(w, L"ScreenshotFolder", L"", g_cfg.screenshotFolder, MAX_PATH, ini);
	QueryPerformanceFrequency(&g_qpf);
}

bool Wanted()
{
	return LineChosen() || g_cfg.overlayKey || g_cfg.benchmarkKey || g_cfg.screenshotKey || g_cfg.fpsLimit > 0;
}

void Hook(uint8_t* renderer, const frame::Build& build)
{
	void** slot = reinterpret_cast<void**>(renderer + build.vtable + frame::kUnlockSlot * sizeof(void*));
	DWORD old;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old))
	{
		Log("Overlay: the Unlock slot at %p is not writable (error %lu)\n", slot, GetLastError());
		return;
	}
	g_renderer = renderer;
	g_realUnlock = reinterpret_cast<Unlock>(*slot);
	*slot = reinterpret_cast<void*>(&RelayUnlock);
	VirtualProtect(slot, sizeof(*slot), old, &old);
	Log("Overlay: d3d11drv.dll (%s build) at %p, Unlock goes through the frame (FPSLimit=%d ScreenshotKey=%u"
		" OverlayKey=%u BenchmarkKey=%u lines=%d)\n", build.game, renderer, g_cfg.fpsLimit, g_cfg.screenshotKey,
		g_cfg.overlayKey, g_cfg.benchmarkKey, LineChosen());
}

void NoteKeyPressed()
{
	if (!g_inputAt)
		InterlockedCompareExchange64(&g_inputAt, Ticks(), 0);
}
}
