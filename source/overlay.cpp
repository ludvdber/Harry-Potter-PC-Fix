// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// [Accio.Overlay]: what the game costs, drawn over the game like a benchmark tool. Every line has
// its own switch; one key shows or hides the whole panel, another records a benchmark.
//
// Frame times are measured at each Present. CPU, memory and GPU are read once a second on a
// thread of our own, never in the frame: the GPU counters of Windows take milliseconds to read.
// Latency is the time from the moment the game reads a newly pressed key to the moment the image
// that follows leaves for the driver; the driver queue and the screen itself are not in it.

#include "hooks.h"
#include "render_state.h"
#include "d3dx9.h"
#include <dxgi1_4.h>
#include <psapi.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
LARGE_INTEGER g_qpf = {};

double Seconds(LONGLONG ticks) { return static_cast<double>(ticks) / static_cast<double>(g_qpf.QuadPart); }
LONGLONG Now()
{
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return t.QuadPart;
}

// ---- Frame times -------------------------------------------------------------------------------

const int kGraph = 240;              // bars in the frame-time graph
const int kLowWindow = 3000;         // frames behind the "1% low"
float g_graph[kGraph] = {};
int g_graphHead = 0;
float g_window[kLowWindow] = {};
int g_windowHead = 0, g_windowCount = 0;
LONGLONG g_lastFrame = 0, g_statsFrom = 0;
int g_statsFrames = 0;
float g_fps = 0, g_frameMs = 0, g_low1 = 0;

void CountFrame(LONGLONG now)
{
	if (g_lastFrame)
	{
		const float ms = static_cast<float>(Seconds(now - g_lastFrame) * 1000.0);
		g_graph[g_graphHead] = ms;
		g_graphHead = (g_graphHead + 1) % kGraph;
		g_window[g_windowHead] = ms;
		g_windowHead = (g_windowHead + 1) % kLowWindow;
		g_windowCount = std::min(g_windowCount + 1, kLowWindow);
	}
	g_lastFrame = now;
	if (!g_statsFrom)
		g_statsFrom = now;
	++g_statsFrames;
	const double span = Seconds(now - g_statsFrom);
	if (span >= 0.5)   // the numbers change twice a second: readable, still current
	{
		g_fps = static_cast<float>(g_statsFrames / span);
		g_frameMs = static_cast<float>(span * 1000.0 / g_statsFrames);
		std::vector<float> sorted(g_window, g_window + g_windowCount);
		if (!sorted.empty())
		{
			// The 1% low: the frame rate of the slowest 1% of frames, i.e. the 99th percentile time.
			auto nth = sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() * 99 / 100);
			std::nth_element(sorted.begin(), nth, sorted.end());
			g_low1 = *nth > 0 ? 1000.0f / *nth : 0;
		}
		g_statsFrom = now;
		g_statsFrames = 0;
	}
}

// ---- Latency -----------------------------------------------------------------------------------

volatile LONGLONG g_inputAt = 0;
float g_latencyMs = -1;

// ---- Once a second, on our own thread ----------------------------------------------------------

volatile LONG g_cpuTenths = -1, g_threadTenths = -1, g_gpuTenths = -1, g_vramMb = -1, g_ramMb = -1;
HANDLE g_renderThread = nullptr;
bool g_samplerStarted = false;

ULONGLONG FileTime(const FILETIME& f) { return (static_cast<ULONGLONG>(f.dwHighDateTime) << 32) | f.dwLowDateTime; }

// PDH (the GPU counters of Windows) and DXGI (video memory) are loaded only when asked for.
typedef LONG(WINAPI* PdhOpenQueryFn)(LPCWSTR, DWORD_PTR, HANDLE*);
typedef LONG(WINAPI* PdhAddCounterFn)(HANDLE, LPCWSTR, DWORD_PTR, HANDLE*);
typedef LONG(WINAPI* PdhCollectFn)(HANDLE);
struct PdhValue { DWORD status; DWORD pad; union { LONG l; double d; LONGLONG ll; }; };
struct PdhItem { LPWSTR name; PdhValue value; };
typedef LONG(WINAPI* PdhArrayFn)(HANDLE, DWORD, LPDWORD, LPDWORD, PdhItem*);
typedef HRESULT(WINAPI* CreateFactoryFn)(REFIID, void**);

IDXGIAdapter3* OpenVideoMemory()
{
	HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
	auto create = dxgi ? reinterpret_cast<CreateFactoryFn>(GetProcAddress(dxgi, "CreateDXGIFactory1")) : nullptr;
	IDXGIFactory1* factory = nullptr;
	if (!create || FAILED(create(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))))
		return nullptr;
	// Direct3D 9 does not say which adapter it runs on: take the one with the most video memory.
	IDXGIAdapter3* best = nullptr;
	SIZE_T bestMemory = 0;
	IDXGIAdapter1* a = nullptr;
	for (UINT i = 0; factory->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i)
	{
		DXGI_ADAPTER_DESC1 d;
		IDXGIAdapter3* a3 = nullptr;
		if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && d.DedicatedVideoMemory > bestMemory
			&& SUCCEEDED(a->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&a3))))
		{
			if (best)
				best->Release();
			best = a3;
			bestMemory = d.DedicatedVideoMemory;
		}
		a->Release();
	}
	factory->Release();
	return best;
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
	LONGLONG lastAt = Now();
	if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u))
		lastProcess = FileTime(k) + FileTime(u);
	if (g_renderThread && GetThreadTimes(g_renderThread, &c, &e, &k, &u))
		lastThread = FileTime(k) + FileTime(u);
	std::vector<unsigned char> buffer;
	for (;;)
	{
		Sleep(1000);
		const LONGLONG at = Now();
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

// ---- Benchmark ---------------------------------------------------------------------------------

bool g_recording = false;
std::vector<float> g_benchFrames;
LONGLONG g_benchStart = 0, g_benchLast = 0, g_summaryUntil = 0;
char g_summary[160] = "";

void StopBenchmark(LONGLONG now)
{
	g_recording = false;
	const double seconds = Seconds(now - g_benchStart);
	if (g_benchFrames.empty() || seconds <= 0)
		return;
	std::vector<float> sorted = g_benchFrames;
	std::sort(sorted.begin(), sorted.end());
	const float p99 = sorted[sorted.size() * 99 / 100], p999 = sorted[sorted.size() * 999 / 1000], worst = sorted.back();
	const double avg = g_benchFrames.size() / seconds;
	sprintf_s(g_summary, "Benchmark %.0f s: avg %.0f FPS, 1%% low %.0f, 0.1%% low %.0f, worst frame %.1f ms",
		seconds, avg, 1000.0f / p99, 1000.0f / p999, worst);
	g_summaryUntil = now + g_qpf.QuadPart * 15;
	Log("%s\n", g_summary);

	char dir[MAX_PATH], exe[MAX_PATH], file[MAX_PATH];
	GetModuleFileNameA(g_self, dir, MAX_PATH);
	strcpy_s(strrchr(dir, '\\') + 1, MAX_PATH - (strrchr(dir, '\\') + 1 - dir), "benchmarks");
	CreateDirectoryA(dir, nullptr);
	GetModuleFileNameA(nullptr, exe, MAX_PATH);
	char* name = strrchr(exe, '\\') + 1;
	if (char* dot = strrchr(name, '.'))
		*dot = '\0';
	SYSTEMTIME t;
	GetLocalTime(&t);
	sprintf_s(file, "%s\\%s_%04u-%02u-%02u_%02u-%02u-%02u.csv", dir, name, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
	FILE* f = nullptr;
	if (fopen_s(&f, file, "w") == 0 && f)
	{
		fprintf(f, "# %s\n# fps_limit=%d\nframe,ms\n", g_summary, g_cfg.fpsLimit);
		for (size_t i = 0; i < g_benchFrames.size(); ++i)
			fprintf(f, "%zu,%.3f\n", i + 1, g_benchFrames[i]);
		fclose(f);
		Log("Benchmark saved: %s\n", file);
	}
	g_benchFrames.clear();
}

bool Pressed(int key, bool& wasDown)
{
	if (!key)
		return false;
	const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
	const bool pressed = down && !wasDown;
	wasDown = down;
	return pressed && ProcessInForeground();
}

// ---- Drawing -----------------------------------------------------------------------------------

ID3DXFont* g_font = nullptr;
int g_fontHeight = 0;
bool g_visible = true;

struct Vertex { float x, y, z, w; D3DCOLOR c; };

void Once(const char* why)
{
	static const char* last = nullptr;
	if (last != why)
		Log("Overlay: skipped, %s (frame %ld)\n", why, g_frames);
	last = why;
}

void Rect(std::vector<Vertex>& v, float x, float y, float w, float h, D3DCOLOR c)
{
	const Vertex a = { x, y, 0, 1, c }, b = { x + w, y, 0, 1, c }, d = { x, y + h, 0, 1, c }, e = { x + w, y + h, 0, 1, c };
	v.push_back(a); v.push_back(b); v.push_back(d);
	v.push_back(b); v.push_back(e); v.push_back(d);
}

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

void Draw(IDirect3DDevice9* dev, LONGLONG now)
{
	IDirect3DSurface9* bb = nullptr;
	if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb)
	{
		Once("no back buffer");
		return;
	}
	D3DSURFACE_DESC desc;
	bb->GetDesc(&desc);
	const float scale = desc.Height / 1080.0f * g_cfg.overlaySize / 100.0f;
	const int fontHeight = std::max(10, static_cast<int>(22 * scale));

	// The lines, in the order they are shown.
	char lines[9][160];
	int n = 0;
	if (g_visible)
	{
		if (g_cfg.showFps)
			sprintf_s(lines[n++], "%.0f FPS", g_fps);
		if (g_cfg.showFrameTime)
			sprintf_s(lines[n++], "%.1f ms   1%% low %.0f FPS", g_frameMs, g_low1);
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
	if (g_recording)
		sprintf_s(lines[n++], "REC  %.0f s", Seconds(now - g_benchStart));
	else if (g_summary[0] && now < g_summaryUntil)
		sprintf_s(lines[n++], "%s", g_summary);
	const bool graph = g_visible && g_cfg.showGraph;
	if (!n && !graph)
	{
		Once("nothing to show");
		bb->Release();
		return;
	}
	// The font only once there is text: the shipped ini shows nothing, and the first D3DX font is
	// what makes the system's d3d9.dll write over our method slots (see KeepDeviceRedirects).
	if (n && g_font && g_fontHeight != fontHeight)
	{
		g_font->Release();
		g_font = nullptr;
	}
	if (n && !g_font)
	{
		if (FAILED(D3DXCreateFontA(dev, fontHeight, 0, FW_BOLD, 1, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
			ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI", &g_font)))
		{
			static bool logged = false;
			if (!logged)
				Log("Overlay: font NOT created\n");
			logged = true;
			g_font = nullptr;
			bb->Release();
			return;
		}
		g_fontHeight = fontHeight;
	}

	// Panel size and corner.
	const float pad = 8 * scale, lineH = fontHeight * 1.2f, graphH = 70 * scale;
	float width = 300 * scale;
	for (int i = 0; i < n; ++i)
	{
		RECT r = { 0, 0, 0, 0 };
		g_font->DrawTextA(nullptr, lines[i], -1, &r, DT_CALCRECT, 0);
		width = std::max(width, static_cast<float>(r.right) + 2 * pad);
	}
	const float height = pad * 2 + n * lineH + (graph ? graphH + (n ? pad : 0) : 0);
	const float margin = 12 * scale;
	const bool right = g_cfg.overlayPosition == 2 || g_cfg.overlayPosition == 4;
	const bool bottom = g_cfg.overlayPosition == 3 || g_cfg.overlayPosition == 4;
	const float x0 = right ? desc.Width - width - margin : margin;
	const float y0 = bottom ? desc.Height - height - margin : margin;

	std::vector<Vertex> v;
	Rect(v, x0, y0, width, height, D3DCOLOR_ARGB(150, 6, 6, 17));
	if (graph)
	{
		const float gx = x0 + pad, gy = y0 + pad + n * lineH + (n ? pad : 0), gw = width - 2 * pad;
		float top = 33.4f;   // the graph always shows 30 FPS; it grows for longer frames, up to 100 ms
		for (float ms : g_graph)
			top = std::max(top, std::min(ms, 100.0f));
		Rect(v, gx, gy, gw, graphH, D3DCOLOR_ARGB(90, 0, 0, 0));
		for (float ms : { 1000.0f / 60, 1000.0f / 30 })   // guides at 60 and 30 FPS
			Rect(v, gx, gy + graphH - graphH * ms / top, gw, std::max(1.0f, scale), D3DCOLOR_ARGB(110, 255, 255, 255));
		const float bar = gw / kGraph;
		for (int i = 0; i < kGraph; ++i)
		{
			const float ms = std::min(g_graph[(g_graphHead + i) % kGraph], top);
			if (ms <= 0)
				continue;
			const float h = std::max(1.0f, graphH * ms / top);
			// Gold while the frame is on time for 60 FPS, orange up to 30, red beyond.
			const D3DCOLOR c = ms <= 17.0f ? D3DCOLOR_ARGB(230, 214, 167, 44)
				: ms <= 34.0f ? D3DCOLOR_ARGB(230, 232, 149, 90) : D3DCOLOR_ARGB(240, 220, 60, 50);
			Rect(v, gx + i * bar, gy + graphH - h, std::max(bar, 1.0f), h, c);
		}
	}

	// Our state around the drawing, the game's back afterwards. A state block does not hold the
	// render target nor the depth buffer: the game may leave its own target bound at Present, and
	// drawing there instead of on the back buffer is how the counter once stayed invisible.
	IDirect3DStateBlock9* saved = nullptr;
	if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &saved)) || !saved)
	{
		Once("no state block");
		bb->Release();
		return;
	}
	IDirect3DSurface9 *oldTarget = nullptr, *oldDepth = nullptr;
	dev->GetRenderTarget(0, &oldTarget);
	dev->GetDepthStencilSurface(&oldDepth);
	const bool ownScene = SUCCEEDED(dev->BeginScene());   // fails if the game is still inside its own

	dev->SetRenderTarget(0, bb);
	dev->SetDepthStencilSurface(nullptr);
	D3DVIEWPORT9 vp = { 0, 0, desc.Width, desc.Height, 0.0f, 1.0f };
	dev->SetViewport(&vp);
	const DWORD states[][2] = {
		{ D3DRS_ZENABLE, FALSE }, { D3DRS_ZWRITEENABLE, FALSE }, { D3DRS_STENCILENABLE, FALSE },
		{ D3DRS_SCISSORTESTENABLE, FALSE }, { D3DRS_CLIPPLANEENABLE, 0 }, { D3DRS_FOGENABLE, FALSE },
		{ D3DRS_LIGHTING, FALSE }, { D3DRS_CULLMODE, D3DCULL_NONE }, { D3DRS_ALPHATESTENABLE, FALSE },
		{ D3DRS_ALPHABLENDENABLE, TRUE }, { D3DRS_SRCBLEND, D3DBLEND_SRCALPHA }, { D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA },
		{ D3DRS_BLENDOP, D3DBLENDOP_ADD }, { D3DRS_SEPARATEALPHABLENDENABLE, FALSE }, { D3DRS_COLORWRITEENABLE, 0xF },
		{ D3DRS_SRGBWRITEENABLE, FALSE }, { D3DRS_FILLMODE, D3DFILL_SOLID },
	};
	for (const auto& s : states)
		dev->SetRenderState(static_cast<D3DRENDERSTATETYPE>(s[0]), s[1]);
	dev->SetVertexShader(nullptr);
	dev->SetPixelShader(nullptr);
	dev->SetTexture(0, nullptr);
	dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
	dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(v.size() / 3), v.data(), sizeof(Vertex));

	for (int i = 0; i < n; ++i)
	{
		const LONG x = static_cast<LONG>(x0 + pad), y = static_cast<LONG>(y0 + pad + i * lineH);
		RECT shadow = { x + 1, y + 1, x + 2000, y + 200 }, face = { x, y, x + 2000, y + 200 };
		const bool rec = g_recording && i == n - 1;
		g_font->DrawTextA(nullptr, lines[i], -1, &shadow, DT_NOCLIP, D3DCOLOR_ARGB(200, 0, 0, 0));
		g_font->DrawTextA(nullptr, lines[i], -1, &face, DT_NOCLIP,
			rec ? D3DCOLOR_ARGB(255, 230, 70, 60) : i == 0 && g_cfg.showFps && g_visible ? D3DCOLOR_ARGB(255, 214, 167, 44)
			: D3DCOLOR_ARGB(255, 240, 240, 244));
	}

	if (ownScene)
		dev->EndScene();
	saved->Apply();
	saved->Release();
	dev->SetRenderTarget(0, oldTarget);
	dev->SetDepthStencilSurface(oldDepth);
	if (oldTarget)
		oldTarget->Release();
	if (oldDepth)
		oldDepth->Release();
	bb->Release();

	static bool logged = false;
	if (!logged)
		Log("Overlay: first drawn (%u x %u, own scene %d)\n", desc.Width, desc.Height, ownScene);
	logged = true;
}
}

void NoteKeyPressed()
{
	if (!g_inputAt)
		InterlockedCompareExchange64(&g_inputAt, Now(), 0);
}

bool OverlayWanted()
{
	return g_cfg.showFps || g_cfg.showFrameTime || g_cfg.showGraph || g_cfg.showCpu || g_cfg.showGpu
		|| g_cfg.showVram || g_cfg.showRam || g_cfg.showLatency || g_cfg.benchmarkKey;
}

void DrawOverlay(IDirect3DDevice9* dev)
{
	if (!OverlayWanted())
		return;
	if (!g_qpf.QuadPart)
		QueryPerformanceFrequency(&g_qpf);
	const LONGLONG now = Now();
	CountFrame(now);
	if (g_recording && g_benchLast)
		g_benchFrames.push_back(static_cast<float>(Seconds(now - g_benchLast) * 1000.0));
	g_benchLast = now;

	if (!g_samplerStarted && (g_cfg.showCpu || g_cfg.showGpu || g_cfg.showVram || g_cfg.showRam))
	{
		g_samplerStarted = true;
		g_renderThread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentThreadId());
		if (HANDLE t = CreateThread(nullptr, 0, Sampler, nullptr, 0, nullptr))
			CloseHandle(t);
	}
	static bool overlayDown = false, benchDown = false;
	if (Pressed(g_cfg.overlayKey, overlayDown))
		g_visible = !g_visible;
	if (Pressed(g_cfg.benchmarkKey, benchDown))
	{
		if (g_recording)
			StopBenchmark(now);
		else
		{
			g_recording = true;
			g_benchStart = now;
			g_benchFrames.clear();
			g_benchFrames.reserve(60 * 1000);
			g_summary[0] = '\0';
			Log("Benchmark started\n");
		}
	}
	Draw(dev, now);
}

// Called once the frame has waited for its slot, just before it goes to Direct3D.
void OverlayFrameSent()
{
	const LONGLONG at = g_inputAt;
	if (!at || !g_qpf.QuadPart)
		return;
	const float ms = static_cast<float>(Seconds(Now() - at) * 1000.0);
	g_latencyMs = g_latencyMs < 0 ? ms : g_latencyMs * 0.7f + ms * 0.3f;
	InterlockedExchange64(&g_inputAt, 0);
}

void OverlayDeviceLost()
{
	if (g_font)
		g_font->OnLostDevice();
}

void OverlayDeviceRestored()
{
	if (g_font)
		g_font->OnResetDevice();
}
