// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// Direct3D 9. The game keeps the objects Direct3D creates; the methods below are redirected in
// their method tables (hooks.h). Slot numbers are the declaration order in d3d9.h.

#include "hooks.h"
#include "render_state.h"
#include "d3dx9.h"
#include <set>
#include <tuple>
#include <unordered_map>

#pragma comment(lib, "d3dx9.lib")
#pragma comment(lib, "dxguid.lib")

SceneDepth g_depth;
int g_backBufferWidth = 0, g_backBufferHeight = 0;

namespace
{
// ---- Presentation parameters ---------------------------------------------------------------

// Hair and leaves are cut out by the alpha test, which multisampling leaves in steps: a pixel is
// in or out. NVIDIA's drivers run that test once per sample while D3DRS_ADAPTIVETESS_Y holds the
// code 'SSAA' (transparency supersampling). Asked for while the bound target is multisampled
// (SetRenderTarget), D3DFMT_UNKNOWN otherwise. Other cards: nothing changes. Their 'ATOC' (alpha
// to coverage) was tried first: no visible change in HP6 (2026-09-26).
constexpr DWORD kSsaa = MAKEFOURCC('S', 'S', 'A', 'A');
bool g_ssaaSupported = false;

void CheckDepthTexture(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type)
{
	static bool checked = false;
	if (checked)
		return;
	checked = true;
	// The format check wants the display format (X8R8G8B8 as a rule), not the back buffer's:
	// A8R8G8B8 is no display format and made the check fail.
	D3DDISPLAYMODE mode = {};
	const D3DFORMAT display = SUCCEEDED(d3d->GetAdapterDisplayMode(adapter, &mode)) ? mode.Format : D3DFMT_X8R8G8B8;
	g_depth.supported = SUCCEEDED(d3d->CheckDeviceFormat(adapter, type, display, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, D3DFMT_INTZ));
	Log("Direct3D: depth as texture (INTZ) %s\n", g_depth.supported ? "available" : "not available");
	if (g_cfg.transparencyAa && g_cfg.msaa > 0)
	{
		g_ssaaSupported = SUCCEEDED(d3d->CheckDeviceFormat(adapter, type, display, 0, D3DRTYPE_SURFACE,
			static_cast<D3DFORMAT>(kSsaa)));
		Log("Direct3D: transparency supersampling %s\n", g_ssaaSupported ? "available (NVIDIA)" : "not available");
	}
}

// Size of the monitor in physical pixels, whatever the display scaling.
bool NativeSize(HWND hwnd, int& w, int& h)
{
	HMONITOR m = (!g_cfg.primaryMonitor && hwnd) ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)
	                                              : MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
	MONITORINFOEXA mi = {};
	mi.cbSize = sizeof(mi);
	if (!GetMonitorInfoA(m, &mi))
		return false;
	DEVMODEA dm = {};
	dm.dmSize = sizeof(dm);
	if (EnumDisplaySettingsA(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmPelsWidth && dm.dmPelsHeight)
	{
		w = static_cast<int>(dm.dmPelsWidth);
		h = static_cast<int>(dm.dmPelsHeight);
	}
	else
	{
		w = mi.rcMonitor.right - mi.rcMonitor.left;
		h = mi.rcMonitor.bottom - mi.rcMonitor.top;
	}
	return true;
}

void AdjustPresentation(D3DPRESENT_PARAMETERS* pp)
{
	Log("Direct3D: the game asks for %ux%u, windowed=%d\n", pp->BackBufferWidth, pp->BackBufferHeight, pp->Windowed);

	if (g_cfg.ssao && g_depth.supported && pp->EnableAutoDepthStencil)
		pp->AutoDepthStencilFormat = D3DFMT_INTZ;

	int w = g_cfg.renderWidth, h = g_cfg.renderHeight;
	if ((w == -1 || h == -1) && !NativeSize(pp->hDeviceWindow ? pp->hDeviceWindow : g_gameWindow, w, h))
		w = h = 0;
	if (w > 0 && h > 0)
	{
		pp->BackBufferWidth = static_cast<UINT>(w);
		pp->BackBufferHeight = static_cast<UINT>(h);
	}

	int msaa = g_cfg.msaa;
	if (g_cfg.ssaa > 1)
	{
		// Rendered larger, scaled down by Present. Multisampling on top would be both redundant
		// and far too heavy.
		pp->BackBufferWidth *= g_cfg.ssaa;
		pp->BackBufferHeight *= g_cfg.ssaa;
		pp->SwapEffect = D3DSWAPEFFECT_DISCARD;
		msaa = 0;
	}
	if (msaa > 1)
	{
		pp->MultiSampleType = static_cast<D3DMULTISAMPLE_TYPE>(msaa);
		pp->MultiSampleQuality = 0;
		pp->SwapEffect = D3DSWAPEFFECT_DISCARD; // required by multisampling
	}
	if (g_cfg.vsync)
		pp->PresentationInterval = D3DPRESENT_INTERVAL_ONE;

	g_backBufferWidth = static_cast<int>(pp->BackBufferWidth);
	g_backBufferHeight = static_cast<int>(pp->BackBufferHeight);
	Log("Direct3D: image %ux%u, MSAA %d, depth format 0x%X\n", pp->BackBufferWidth, pp->BackBufferHeight,
		static_cast<int>(pp->MultiSampleType), static_cast<unsigned>(pp->AutoDepthStencilFormat));
}

// Steps multisampling down (16, 8, 4, 2, none) until the GPU takes it for both the image and its
// depth: asking for 16 on a card that stops at 8 used to switch it off altogether.
void FitMultisampling(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, D3DPRESENT_PARAMETERS* pp)
{
	const D3DMULTISAMPLE_TYPE asked = pp->MultiSampleType;
	if (asked == D3DMULTISAMPLE_NONE)
		return;
	const D3DFORMAT color = pp->BackBufferFormat == D3DFMT_UNKNOWN ? D3DFMT_X8R8G8B8 : pp->BackBufferFormat;
	for (int level = asked; level >= 2; level = level > 2 ? level / 2 : 0)
	{
		const auto t = static_cast<D3DMULTISAMPLE_TYPE>(level);
		if (FAILED(d3d->CheckDeviceMultiSampleType(adapter, type, color, pp->Windowed, t, nullptr)))
			continue;
		if (pp->EnableAutoDepthStencil
			&& FAILED(d3d->CheckDeviceMultiSampleType(adapter, type, pp->AutoDepthStencilFormat, pp->Windowed, t, nullptr)))
			continue;
		pp->MultiSampleType = t;
		if (level != asked)
			Log("Direct3D: MSAA %d not supported, %d instead\n", static_cast<int>(asked), level);
		return;
	}
	Log("Direct3D: MSAA %d not supported at all, off\n", static_cast<int>(asked));
	pp->MultiSampleType = D3DMULTISAMPLE_NONE;
	pp->MultiSampleQuality = 0;
}

// Creation or reset, with two fallbacks: the game's own depth format if INTZ is refused, then
// no multisampling.
template <class Call>
HRESULT WithFallbacks(D3DPRESENT_PARAMETERS* pp, D3DFORMAT gameDepth, Call call)
{
	HRESULT hr = call();
	if (FAILED(hr) && pp && pp->AutoDepthStencilFormat == D3DFMT_INTZ)
	{
		Log("Direct3D: refused with an INTZ depth (0x%lX), again with the game's\n", static_cast<unsigned long>(hr));
		pp->AutoDepthStencilFormat = gameDepth;
		hr = call();
	}
	if (FAILED(hr) && pp && pp->MultiSampleType != D3DMULTISAMPLE_NONE)
	{
		Log("Direct3D: refused with MSAA (0x%lX), again without\n", static_cast<unsigned long>(hr));
		pp->MultiSampleType = D3DMULTISAMPLE_NONE;
		pp->MultiSampleQuality = 0;
		hr = call();
	}
	return hr;
}

// ---- Shadow maps drawn larger (ShadowMapScale, experimental) -----------------------------
// The games draw their projected shadows into small square render targets (128 to 512). They
// get targets N times larger; since they still believe the old size, every viewport set on those
// targets is scaled by the same factor. The same size is also used for reflections, which are
// scaled too: this is why the option is off in every shipped ini.

std::unordered_map<IDirect3DSurface9*, std::pair<UINT, UINT>> g_bigSurfaces; // surface -> size asked
std::unordered_map<IDirect3DTexture9*, IDirect3DSurface9*> g_bigTextures;
bool g_targetIsBig = false;
IDirect3DSurface9* g_boundDepth = nullptr; // the depth the game has bound; compared, not owned
UINT g_targetW = 0, g_targetH = 0;

bool ShadowSized(UINT w, UINT h) { return w == h && w >= 32 && w <= 512; }

void ForgetBigTargets()
{
	g_bigSurfaces.clear();
	g_bigTextures.clear();
	g_targetIsBig = false;
}

// ---- Redirected methods ------------------------------------------------------------------

using CreateDeviceFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
using CreateDeviceExFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9Ex*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*, IDirect3DDevice9Ex**);
using ResetFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using ResetExFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*);
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using PresentExFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
using ChainPresentFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
using CreateTextureFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*);
using CreateDepthFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL, IDirect3DSurface9**, HANDLE*);
using SetTargetFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, DWORD, IDirect3DSurface9*);
using SetDepthFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, IDirect3DSurface9*);
using SetViewportFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, const D3DVIEWPORT9*);
using SetTextureFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9*);
using SetSamplerFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, DWORD, D3DSAMPLERSTATETYPE, DWORD);
using SetStateFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DRENDERSTATETYPE, DWORD);
using TargetDataFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, IDirect3DSurface9*, IDirect3DSurface9*);
using StretchRectFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const RECT*, D3DTEXTUREFILTERTYPE);
using UnlockFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DTexture9*, UINT);
using ReleaseFn = ULONG(STDMETHODCALLTYPE*)(IDirect3DTexture9*);

HRESULT STDMETHODCALLTYPE CreateDevice(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
HRESULT STDMETHODCALLTYPE CreateDeviceEx(IDirect3D9Ex*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*, IDirect3DDevice9Ex**);
HRESULT STDMETHODCALLTYPE Reset(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
HRESULT STDMETHODCALLTYPE ResetEx(IDirect3DDevice9Ex*, D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*);
HRESULT STDMETHODCALLTYPE Present(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
HRESULT STDMETHODCALLTYPE PresentEx(IDirect3DDevice9Ex*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
HRESULT STDMETHODCALLTYPE ChainPresent(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
HRESULT STDMETHODCALLTYPE CreateTexture(IDirect3DDevice9*, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*);
HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(IDirect3DDevice9*, UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL, IDirect3DSurface9**, HANDLE*);
HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DDevice9*, IDirect3DSurface9*, IDirect3DSurface9*);
HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DDevice9*, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const RECT*, D3DTEXTUREFILTERTYPE);
HRESULT STDMETHODCALLTYPE SetRenderTarget(IDirect3DDevice9*, DWORD, IDirect3DSurface9*);
HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DDevice9*, IDirect3DSurface9*);
HRESULT STDMETHODCALLTYPE SetViewport(IDirect3DDevice9*, const D3DVIEWPORT9*);
HRESULT STDMETHODCALLTYPE SetRenderState(IDirect3DDevice9*, D3DRENDERSTATETYPE, DWORD);
HRESULT STDMETHODCALLTYPE SetTexture(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9*);
HRESULT STDMETHODCALLTYPE SetSamplerState(IDirect3DDevice9*, DWORD, D3DSAMPLERSTATETYPE, DWORD);
HRESULT STDMETHODCALLTYPE UnlockRect(IDirect3DTexture9*, UINT);
ULONG STDMETHODCALLTYPE ReleaseTexture(IDirect3DTexture9*);

#define REDIRECT(slot, fn) MethodRedirect g_##fn(slot, reinterpret_cast<void*>(fn))
REDIRECT(16, CreateDevice);            // IDirect3D9
REDIRECT(20, CreateDeviceEx);          // IDirect3D9Ex
REDIRECT(16, Reset);                   // IDirect3DDevice9
REDIRECT(17, Present);
REDIRECT(23, CreateTexture);
REDIRECT(29, CreateDepthStencilSurface);
REDIRECT(32, GetRenderTargetData);
REDIRECT(34, StretchRect);
REDIRECT(37, SetRenderTarget);
REDIRECT(39, SetDepthStencilSurface);
REDIRECT(47, SetViewport);
REDIRECT(57, SetRenderState);
REDIRECT(65, SetTexture);
REDIRECT(69, SetSamplerState);
REDIRECT(121, PresentEx);              // IDirect3DDevice9Ex
REDIRECT(132, ResetEx);
REDIRECT(3, ChainPresent);             // IDirect3DSwapChain9
REDIRECT(20, UnlockRect);              // IDirect3DTexture9
MethodRedirect g_ReleaseTexture(2, reinterpret_cast<void*>(ReleaseTexture));
#undef REDIRECT

// Marks a texture whose mipmaps we generate: the game creates most of its textures without any,
// which blurs them in the distance. They are created with a full chain instead, filled when the
// game has written the top level.
// {5D3E4A10-7C0B-4E8B-9A51-6B3C2F0D8E21}
const GUID kFillMips = { 0x5d3e4a10, 0x7c0b, 0x4e8b, { 0x9a, 0x51, 0x6b, 0x3c, 0x2f, 0x0d, 0x8e, 0x21 } };
LONG g_mipsFilled = 0;
// Chains made, by pool: the game fills a texture of the default pool without locking it (a copy
// from memory), so its chain may never be filled here. Counted to see whether that happens.
LONG g_mipChains = 0, g_mipChainsDefault = 0;


// ---- Multisampled scene (Antialiasing) ---------------------------------------------------
// HP4 and HP6 draw their 3D scene into a render-target TEXTURE the size of the image, with a
// depth of their own, then copy it to the back buffer (measured 2026-09-25): multisampling the
// back buffer alone never reached the scene. That texture gets a multisampled twin, bound in its
// place while the game draws into it and resolved into it when the game moves on. A depth used
// with a multisampled target gets a multisampled twin too, since Direct3D 9 wants both to match.
// Off with ambient occlusion: it reads the scene depth as an INTZ texture, which cannot be
// multisampled.

struct SceneTwin
{
	IDirect3DSurface9* surface = nullptr; // the game's level 0, compared against, not owned
	IDirect3DSurface9* msaa = nullptr;    // ours
	bool dirty = false;                   // drawn into since the last resolve
};
std::unordered_map<IDirect3DTexture9*, SceneTwin> g_sceneTwins;
std::unordered_map<IDirect3DSurface9*, IDirect3DTexture9*> g_twinOfSurface;  // game's level 0 -> texture
std::unordered_map<IDirect3DSurface9*, IDirect3DSurface9*> g_surfaceOfTwin;  // our twin -> game's level 0
std::unordered_map<IDirect3DSurface9*, IDirect3DSurface9*> g_depthTwins;     // game's depth -> ours
std::unordered_map<IDirect3DSurface9*, IDirect3DSurface9*> g_depthOfTwin;    // ours -> game's depth
D3DMULTISAMPLE_TYPE g_sceneMsaa = D3DMULTISAMPLE_NONE;
D3DMULTISAMPLE_TYPE g_targetMsaa = D3DMULTISAMPLE_NONE; // of the target really bound at index 0
SceneTwin* g_twinBound = nullptr;

void DropTwin(std::unordered_map<IDirect3DTexture9*, SceneTwin>::iterator it)
{
	if (g_twinBound == &it->second)
		g_twinBound = nullptr; // still bound: the device keeps it alive until the next target
	g_twinOfSurface.erase(it->second.surface);
	g_surfaceOfTwin.erase(it->second.msaa);
	it->second.msaa->Release();
	g_sceneTwins.erase(it);
}

// The twin of a target the game binds, if it still fits it. An address can come back for another
// texture if its release went unseen (Direct3D has been seen rewriting method tables).
SceneTwin* TwinFor(IDirect3DSurface9* target)
{
	auto key = target ? g_twinOfSurface.find(target) : g_twinOfSurface.end();
	if (key == g_twinOfSurface.end())
		return nullptr;
	auto it = g_sceneTwins.find(key->second);
	if (it == g_sceneTwins.end())
	{
		g_twinOfSurface.erase(key);
		return nullptr;
	}
	D3DSURFACE_DESC a, b;
	if (SUCCEEDED(target->GetDesc(&a)) && SUCCEEDED(it->second.msaa->GetDesc(&b)) && a.Width == b.Width
		&& a.Height == b.Height && a.Format == b.Format)
		return &it->second;
	DropTwin(it);
	return nullptr;
}

void DecideSceneMsaa(const D3DPRESENT_PARAMETERS* pp)
{
	g_sceneMsaa = D3DMULTISAMPLE_NONE;
	if (!pp || pp->MultiSampleType == D3DMULTISAMPLE_NONE)
		return;
	if (g_cfg.ssao && g_depth.supported)
	{
		static bool said = false;
		if (!said)
		{
			said = true;
			Log("Direct3D: Antialiasing reaches the back buffer only: ambient occlusion reads the scene depth "
			    "as a texture, which Direct3D 9 cannot multisample\n");
		}
		return;
	}
	g_sceneMsaa = pp->MultiSampleType;
}

void AddSceneTwin(IDirect3DDevice9* dev, IDirect3DTexture9* tex, UINT w, UINT h, D3DFORMAT format)
{
	IDirect3DSurface9* top = nullptr;
	if (FAILED(tex->GetSurfaceLevel(0, &top)) || !top)
		return;
	top->Release(); // the texture keeps its own level alive
	IDirect3DSurface9* msaa = nullptr;
	HRESULT hr;
	{
		InternalCalls inside;
		hr = dev->CreateRenderTarget(w, h, format, g_sceneMsaa, 0, FALSE, &msaa, nullptr);
	}
	if (FAILED(hr) || !msaa)
	{
		Log("Direct3D: scene %ux%u (format %d) not multisampled, hr=0x%lX\n", w, h, static_cast<int>(format),
			static_cast<unsigned long>(hr));
		return;
	}
	g_ReleaseTexture.Install(tex);
	if (auto old = g_sceneTwins.find(tex); old != g_sceneTwins.end())
		DropTwin(old); // left over from a texture whose release went unseen
	g_sceneTwins[tex] = { top, msaa, false };
	g_twinOfSurface[top] = tex;
	g_surfaceOfTwin[msaa] = top;
	Log("Direct3D: scene %ux%u (format %d) drawn with MSAA %d\n", w, h, static_cast<int>(format), static_cast<int>(g_sceneMsaa));
}

void Resolve(IDirect3DDevice9* dev, SceneTwin& t)
{
	if (!t.dirty)
		return;
	t.dirty = false;
	InternalCalls inside;
	const HRESULT hr = dev->StretchRect(t.msaa, nullptr, t.surface, nullptr, D3DTEXF_NONE);
	static bool logged = false;
	if (FAILED(hr) && !logged)
	{
		logged = true;
		Log("Direct3D: multisampled scene not resolved, hr=0x%lX\n", static_cast<unsigned long>(hr));
	}
}

// The depth to bind with the target really bound: the game's, or its multisampled twin when the
// target is multisampled and the game's depth is not. The game's depth is HELD while it has a
// twin: the device holds the twin in its place, and a game may release its own reference counting
// on the device's, as Direct3D allows. Without that hold, the next change of target read a freed
// surface.
IDirect3DSurface9* DepthFor(IDirect3DDevice9* dev, IDirect3DSurface9* depth)
{
	if (!depth || g_targetMsaa == D3DMULTISAMPLE_NONE)
		return depth;
	D3DSURFACE_DESC d;
	if (FAILED(depth->GetDesc(&d)) || d.MultiSampleType != D3DMULTISAMPLE_NONE)
		return depth;
	bool held = false;
	auto it = g_depthTwins.find(depth);
	if (it != g_depthTwins.end())
	{
		D3DSURFACE_DESC t;
		if (SUCCEEDED(it->second->GetDesc(&t)) && t.MultiSampleType == g_targetMsaa)
			return it->second;
		// Another multisampling since: a new twin, the game's depth still held.
		g_depthOfTwin.erase(it->second);
		it->second->Release();
		g_depthTwins.erase(it);
		held = true;
	}
	IDirect3DSurface9* twin = nullptr;
	HRESULT hr;
	{
		InternalCalls inside;
		hr = dev->CreateDepthStencilSurface(d.Width, d.Height, d.Format, g_targetMsaa, 0, FALSE, &twin, nullptr);
	}
	if (FAILED(hr) || !twin)
	{
		static bool logged = false;
		if (!logged)
		{
			logged = true;
			Log("Direct3D: multisampled depth %ux%u refused, hr=0x%lX\n", d.Width, d.Height, static_cast<unsigned long>(hr));
		}
		// A hold left over stays: the device is about to bind this depth, it must not die first.
		return depth;
	}
	if (!held)
		depth->AddRef();
	g_depthTwins[depth] = twin;
	g_depthOfTwin[twin] = depth;
	return twin;
}

// Once a frame: a twin goes with its game depth once nobody else holds that depth and neither is
// bound. Kept while bound: the game may have released a depth it still draws with.
void PruneDepthTwins(IDirect3DDevice9* dev)
{
	if (g_depthTwins.empty())
		return;
	IDirect3DSurface9* bound = nullptr;
	if (FAILED(dev->GetDepthStencilSurface(&bound)))
		bound = nullptr;
	for (auto it = g_depthTwins.begin(); it != g_depthTwins.end();)
	{
		IDirect3DSurface9* game = it->first;
		game->AddRef();
		const ULONG refs = game->Release();
		if (refs == 1 && game != bound && it->second != bound)
		{
			g_depthOfTwin.erase(it->second);
			it->second->Release();
			game->Release();
			it = g_depthTwins.erase(it);
		}
		else
			++it;
	}
	if (bound)
		bound->Release();
}

// A copy out of the scene texture reads what is drawn so far. Our twin, which GetRenderTarget
// hands out while it is bound, stands for the game's texture: multisampled, it could not be read
// back to memory.
IDirect3DSurface9* SceneSource(IDirect3DDevice9* dev, IDirect3DSurface9* src)
{
	if (g_internal || !src || g_sceneTwins.empty())
		return src;
	if (auto back = g_surfaceOfTwin.find(src); back != g_surfaceOfTwin.end())
		src = back->second;
	if (auto key = g_twinOfSurface.find(src); key != g_twinOfSurface.end())
		if (auto it = g_sceneTwins.find(key->second); it != g_sceneTwins.end())
		{
			Resolve(dev, it->second);
			if (g_twinBound == &it->second)
				it->second.dirty = true; // still the target: what is drawn next counts too
		}
	return src;
}

// After a change of target: the depth in place must match it. Read from Direct3D rather than
// remembered, since the game may never have set one (the automatic depth stays bound).
void RebindDepth(IDirect3DDevice9* dev)
{
	IDirect3DSurface9* bound = nullptr;
	if (FAILED(dev->GetDepthStencilSurface(&bound)) || !bound)
		return; // no depth: nothing to match
	IDirect3DSurface9* game = bound;
	if (auto it = g_depthOfTwin.find(bound); it != g_depthOfTwin.end())
		game = it->second;
	IDirect3DSurface9* wanted = DepthFor(dev, game);
	if (wanted != bound)
		g_SetDepthStencilSurface.Original<SetDepthFn>(dev)(dev, wanted);
	bound->Release();
}

// Before a Reset: nothing of ours bound, all of it released (a Reset refuses to run while any
// resource of the default pool is alive). The back buffer and no depth rather than the game's
// surfaces: the game may already have released those, and Reset binds its own anyway. A twin
// whose texture went while bound is still bound, hence the check on the multisampling seen.
void ForgetSceneTwins(IDirect3DDevice9* dev)
{
	if (dev && (!g_sceneTwins.empty() || !g_depthTwins.empty() || g_targetMsaa != D3DMULTISAMPLE_NONE))
	{
		InternalCalls inside;
		IDirect3DSurface9* bb = nullptr;
		if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb)
		{
			dev->SetRenderTarget(0, bb);
			bb->Release();
		}
		IDirect3DSurface9* bound = nullptr;
		if (SUCCEEDED(dev->GetDepthStencilSurface(&bound)) && bound)
		{
			if (g_depthOfTwin.count(bound))
				dev->SetDepthStencilSurface(nullptr);
			bound->Release();
		}
	}
	for (auto& [tex, twin] : g_sceneTwins)
		twin.msaa->Release();
	for (auto& [depth, twin] : g_depthTwins)
	{
		twin->Release();
		depth->Release(); // our hold (see DepthFor)
	}
	g_sceneTwins.clear();
	g_twinOfSurface.clear();
	g_surfaceOfTwin.clear();
	g_depthTwins.clear();
	g_depthOfTwin.clear();
	g_twinBound = nullptr;
	g_targetMsaa = D3DMULTISAMPLE_NONE;
}

void HookDevice(IDirect3DDevice9* dev)
{
	g_Reset.Install(dev);
	g_Present.Install(dev);
	g_CreateTexture.Install(dev);
	g_CreateDepthStencilSurface.Install(dev);
	g_GetRenderTargetData.Install(dev);
	g_StretchRect.Install(dev);
	g_SetRenderTarget.Install(dev);
	g_SetDepthStencilSurface.Install(dev);
	g_SetViewport.Install(dev);
	g_SetRenderState.Install(dev);
	g_SetTexture.Install(dev);
	g_SetSamplerState.Install(dev);

	IDirect3DDevice9Ex* ex = nullptr;
	if (SUCCEEDED(dev->QueryInterface(IID_IDirect3DDevice9Ex, reinterpret_cast<void**>(&ex))) && ex)
	{
		// Slots 121 and up exist only in an Ex device's table.
		g_PresentEx.Install(ex);
		g_ResetEx.Install(ex);
		if (g_cfg.maxFrameLatency > 0)
			ex->SetMaximumFrameLatency(static_cast<UINT>(g_cfg.maxFrameLatency));
		ex->Release();
	}
	IDirect3DSwapChain9* chain = nullptr;
	if (SUCCEEDED(dev->GetSwapChain(0, &chain)) && chain)
	{
		g_ChainPresent.Install(chain);
		chain->Release();
	}
	Log("Direct3D: device %p followed\n", dev);
}

void TakeBackRedirects(IDirect3DDevice9* dev)
{
	MethodRedirect* const all[] = { &g_Reset, &g_Present, &g_CreateTexture, &g_CreateDepthStencilSurface,
		&g_GetRenderTargetData, &g_StretchRect, &g_SetRenderTarget, &g_SetDepthStencilSurface, &g_SetViewport, &g_SetRenderState,
		&g_SetTexture, &g_SetSamplerState };
	static const char* const names[] = { "Reset", "Present", "CreateTexture", "CreateDepthStencilSurface",
		"GetRenderTargetData", "StretchRect", "SetRenderTarget", "SetDepthStencilSurface", "SetViewport", "SetRenderState",
		"SetTexture", "SetSamplerState" };
	static_assert(_countof(all) == _countof(names));
	static bool logged[_countof(all)] = {};
	for (size_t i = 0; i < _countof(all); ++i)
		if (all[i]->Reclaim(dev) && !logged[i])
		{
			logged[i] = true;
			Log("Direct3D: %s had been written over, taken back (frame %ld)\n", names[i], g_frames);
		}
}

HRESULT STDMETHODCALLTYPE CreateDevice(IDirect3D9* self, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
	D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out)
{
	const auto original = g_CreateDevice.Original<CreateDeviceFn>(self);
	if (!pp)
		return original(self, adapter, type, focus, flags, pp, out);
	PrepareWindow(pp, focus);
	CheckDepthTexture(self, adapter, type);
	const D3DFORMAT gameDepth = pp->AutoDepthStencilFormat;
	AdjustPresentation(pp);
	FitMultisampling(self, adapter, type, pp);
	const HRESULT hr = WithFallbacks(pp, gameDepth, [&] { return original(self, adapter, type, focus, flags, pp, out); });
	Log("Direct3D: device created, hr=0x%lX\n", static_cast<unsigned long>(hr));
	if (SUCCEEDED(hr) && out && *out)
	{
		DecideSceneMsaa(pp);
		HookDevice(*out);
		OnDeviceRestored(*out);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE CreateDeviceEx(IDirect3D9Ex* self, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
	D3DPRESENT_PARAMETERS* pp, D3DDISPLAYMODEEX* mode, IDirect3DDevice9Ex** out)
{
	const auto original = g_CreateDeviceEx.Original<CreateDeviceExFn>(self);
	if (!pp)
		return original(self, adapter, type, focus, flags, pp, mode, out);
	PrepareWindow(pp, focus);
	if (g_cfg.windowed)
		mode = nullptr; // a display mode is for exclusive full screen only
	CheckDepthTexture(self, adapter, type);
	const D3DFORMAT gameDepth = pp->AutoDepthStencilFormat;
	AdjustPresentation(pp);
	FitMultisampling(self, adapter, type, pp);
	const HRESULT hr = WithFallbacks(pp, gameDepth, [&] { return original(self, adapter, type, focus, flags, pp, mode, out); });
	Log("Direct3D: Ex device created, hr=0x%lX\n", static_cast<unsigned long>(hr));
	if (SUCCEEDED(hr) && out && *out)
	{
		DecideSceneMsaa(pp);
		HookDevice(*out);
		OnDeviceRestored(*out);
	}
	return hr;
}

void BeforeReset(IDirect3DDevice9* dev)
{
	OnDeviceLost();
	ForgetBigTargets();
	ForgetSceneTwins(dev);
	g_boundDepth = nullptr;
}

// Reset takes the ini's multisampling as it stands: 16 on a card that stops at 8 switched it off.
void FitMultisamplingOnReset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp)
{
	IDirect3D9* d3d = nullptr;
	D3DDEVICE_CREATION_PARAMETERS cp = {};
	if (SUCCEEDED(dev->GetDirect3D(&d3d)) && d3d)
	{
		if (SUCCEEDED(dev->GetCreationParameters(&cp)))
			FitMultisampling(d3d, cp.AdapterOrdinal, cp.DeviceType, pp);
		d3d->Release();
	}
}

HRESULT STDMETHODCALLTYPE Reset(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* pp)
{
	const auto original = g_Reset.Original<ResetFn>(self);
	if (g_internal || !pp)
		return original(self, pp);
	BeforeReset(self);
	PrepareWindow(pp, nullptr);
	const D3DFORMAT gameDepth = pp->AutoDepthStencilFormat;
	AdjustPresentation(pp);
	FitMultisamplingOnReset(self, pp);
	const HRESULT hr = WithFallbacks(pp, gameDepth, [&] { return original(self, pp); });
	Log("Direct3D: reset, hr=0x%lX\n", static_cast<unsigned long>(hr));
	if (SUCCEEDED(hr))
	{
		DecideSceneMsaa(pp);
		OnDeviceRestored(self);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE ResetEx(IDirect3DDevice9Ex* self, D3DPRESENT_PARAMETERS* pp, D3DDISPLAYMODEEX* mode)
{
	const auto original = g_ResetEx.Original<ResetExFn>(self);
	if (g_internal || !pp)
		return original(self, pp, mode);
	BeforeReset(self);
	PrepareWindow(pp, nullptr);
	if (g_cfg.windowed)
		mode = nullptr;
	const D3DFORMAT gameDepth = pp->AutoDepthStencilFormat;
	AdjustPresentation(pp);
	FitMultisamplingOnReset(self, pp);
	const HRESULT hr = WithFallbacks(pp, gameDepth, [&] { return original(self, pp, mode); });
	Log("Direct3D: Ex reset, hr=0x%lX\n", static_cast<unsigned long>(hr));
	if (SUCCEEDED(hr))
	{
		DecideSceneMsaa(pp);
		OnDeviceRestored(self);
	}
	return hr;
}

// Present can reach Direct3D three ways (device, Ex device, swap chain), and one implementation
// may call another inside: only the outermost call is a frame.
HRESULT STDMETHODCALLTYPE Present(IDirect3DDevice9* self, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty)
{
	const auto original = g_Present.Original<PresentFn>(self);
	if (g_internal)
		return original(self, src, dst, wnd, dirty);
	PruneDepthTwins(self);
	BeforePresent(self);
	InternalCalls inside;
	return original(self, src, dst, wnd, dirty);
}

HRESULT STDMETHODCALLTYPE PresentEx(IDirect3DDevice9Ex* self, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty, DWORD flags)
{
	const auto original = g_PresentEx.Original<PresentExFn>(self);
	if (g_internal)
		return original(self, src, dst, wnd, dirty, flags);
	PruneDepthTwins(self);
	BeforePresent(self);
	InternalCalls inside;
	return original(self, src, dst, wnd, dirty, flags);
}

HRESULT STDMETHODCALLTYPE ChainPresent(IDirect3DSwapChain9* self, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty, DWORD flags)
{
	const auto original = g_ChainPresent.Original<ChainPresentFn>(self);
	if (g_internal)
		return original(self, src, dst, wnd, dirty, flags);
	IDirect3DDevice9* dev = nullptr;
	if (SUCCEEDED(self->GetDevice(&dev)) && dev)
	{
		PruneDepthTwins(dev);
		BeforePresent(dev);
		dev->Release();
	}
	InternalCalls inside;
	return original(self, src, dst, wnd, dirty, flags);
}

HRESULT STDMETHODCALLTYPE CreateTexture(IDirect3DDevice9* self, UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT format,
	D3DPOOL pool, IDirect3DTexture9** out, HANDLE* shared)
{
	const auto original = g_CreateTexture.Original<CreateTextureFn>(self);
	if (g_internal)
		return original(self, w, h, levels, usage, format, pool, out, shared);

	const UINT askedW = w, askedH = h;
	bool big = false;
	if (g_cfg.shadowScale > 1 && (usage & D3DUSAGE_RENDERTARGET) && pool == D3DPOOL_DEFAULT && ShadowSized(w, h))
	{
		w *= g_cfg.shadowScale;
		h *= g_cfg.shadowScale;
		big = true;
	}

	const bool fillMips = g_cfg.generateMipmaps && levels == 1 && pool != D3DPOOL_SYSTEMMEM
		&& !(usage & (D3DUSAGE_DYNAMIC | D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL));
	HRESULT hr = fillMips ? original(self, w, h, 0, usage, format, pool, out, shared) : E_FAIL;
	const bool withChain = SUCCEEDED(hr);
	if (!withChain)
		hr = original(self, w, h, levels, usage, format, pool, out, shared);
	if (FAILED(hr) && big)
	{
		Log("Direct3D: %ux%u shadow target refused, back to %ux%u\n", w, h, askedW, askedH);
		w = askedW;
		h = askedH;
		big = false;
		hr = original(self, w, h, levels, usage, format, pool, out, shared);
	}
	if (FAILED(hr) || !out || !*out)
		return hr;

	IDirect3DTexture9* tex = *out;
	if (usage & D3DUSAGE_RENDERTARGET)
	{
		// Where the game draws: a scene-sized target of its own is drawn without the back
		// buffer's multisampling. Once per size and format.
		static std::set<std::tuple<UINT, UINT, int>> seen;
		if (seen.insert({ askedW, askedH, static_cast<int>(format) }).second)
			Log("Direct3D: game render target %ux%u (format %d, pool %d)\n", askedW, askedH, static_cast<int>(format),
				static_cast<int>(pool));
		if (g_sceneMsaa != D3DMULTISAMPLE_NONE && pool == D3DPOOL_DEFAULT && !big
			&& static_cast<int>(w) == g_backBufferWidth && static_cast<int>(h) == g_backBufferHeight)
			AddSceneTwin(self, tex, w, h, format);
	}
	g_UnlockRect.Install(tex);
	if (withChain)
	{
		InterlockedIncrement(&g_mipChains);
		if (pool == D3DPOOL_DEFAULT)
			InterlockedIncrement(&g_mipChainsDefault);
		const BYTE one = 1;
		tex->SetPrivateData(kFillMips, &one, sizeof(one), 0);
	}
	if (big)
	{
		g_ReleaseTexture.Install(tex);
		IDirect3DSurface9* top = nullptr;
		if (SUCCEEDED(tex->GetSurfaceLevel(0, &top)) && top)
		{
			g_bigSurfaces[top] = { askedW, askedH };
			g_bigTextures[tex] = top;
			top->Release(); // the texture keeps its own level alive
		}
		static bool logged = false;
		if (!logged)
		{
			logged = true;
			Log("Direct3D: shadow targets drawn %dx larger (first %ux%u)\n", g_cfg.shadowScale, askedW, askedH);
		}
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE UnlockRect(IDirect3DTexture9* self, UINT level)
{
	const HRESULT hr = g_UnlockRect.Original<UnlockFn>(self)(self, level);
	if (g_internal || FAILED(hr) || level != 0)
		return hr;
	BYTE mark = 0;
	DWORD size = sizeof(mark);
	if (FAILED(self->GetPrivateData(kFillMips, &mark, &size)))
		return hr;
	self->FreePrivateData(kFillMips); // once: later unlocks are the game updating a live texture
	InternalCalls inside;
	// Triangle filter with dithering, averaged in sRGB: visibly crisper than box or bilinear.
	const HRESULT filled = D3DXFilterTexture(self, nullptr, 0, D3DX_FILTER_TRIANGLE | D3DX_FILTER_DITHER | D3DX_FILTER_SRGB);
	if (SUCCEEDED(filled))
		InterlockedIncrement(&g_mipsFilled);
	else
	{
		D3DSURFACE_DESC d = {};
		self->GetLevelDesc(0, &d);
		Log("Direct3D: mipmaps of a %ux%u texture (format %d) not filled, hr=0x%lX\n", d.Width, d.Height,
			static_cast<int>(d.Format), static_cast<unsigned long>(filled));
	}
	return hr;
}

ULONG STDMETHODCALLTYPE ReleaseTexture(IDirect3DTexture9* self)
{
	const ULONG left = g_ReleaseTexture.Original<ReleaseFn>(self)(self);
	if (!left)
	{
		// The address can come back for another texture, which must not inherit the scaling.
		auto it = g_bigTextures.find(self);
		if (it != g_bigTextures.end())
		{
			g_bigSurfaces.erase(it->second);
			g_bigTextures.erase(it);
		}
		if (auto twin = g_sceneTwins.find(self); twin != g_sceneTwins.end())
			DropTwin(twin);
	}
	return left;
}

HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(IDirect3DDevice9* self, UINT w, UINT h, D3DFORMAT format,
	D3DMULTISAMPLE_TYPE ms, DWORD quality, BOOL discard, IDirect3DSurface9** out, HANDLE* shared)
{
	const auto original = g_CreateDepthStencilSurface.Original<CreateDepthFn>(self);
	if (g_internal)
		return original(self, w, h, format, ms, quality, discard, out, shared);

	{
		// A depth the game creates itself keeps its own multisampling, whatever the back buffer's.
		static std::set<std::tuple<UINT, UINT, int>> seen;
		if (seen.insert({ w, h, static_cast<int>(ms) }).second)
			Log("Direct3D: game depth %ux%u (format 0x%X, MSAA %d)\n", w, h, static_cast<unsigned>(format), static_cast<int>(ms));
	}
	// A shadow target's depth grows with it: Direct3D wants depth at least as large as the target.
	bool big = false;
	if (g_cfg.shadowScale > 1 && ms == D3DMULTISAMPLE_NONE && ShadowSized(w, h))
	{
		w *= g_cfg.shadowScale;
		h *= g_cfg.shadowScale;
		big = true;
	}

	// Ambient occlusion reads the scene depth. Only the scene depth (as large as the image or
	// larger) becomes a readable INTZ texture: the small ones the games use for reflections keep
	// their format, INTZ there changed their own rendering. "Larger" and not "equal": after a
	// Reset the games size their depth to the window while the image keeps the ini size.
	const bool sceneSized = !big && static_cast<int>(w) >= g_backBufferWidth && static_cast<int>(h) >= g_backBufferHeight;
	if (g_cfg.ssao && g_depth.supported && sceneSized && out && ms == D3DMULTISAMPLE_NONE && !shared)
	{
		IDirect3DTexture9* tex = nullptr;
		IDirect3DSurface9* surf = nullptr;
		HRESULT hr;
		{
			InternalCalls inside;
			hr = self->CreateTexture(w, h, 1, D3DUSAGE_DEPTHSTENCIL, D3DFMT_INTZ, D3DPOOL_DEFAULT, &tex, nullptr);
		}
		if (SUCCEEDED(hr) && tex && SUCCEEDED(tex->GetSurfaceLevel(0, &surf)) && surf)
		{
			if (g_depth.texture)
				g_depth.texture->Release();
			g_depth.texture = tex; // our reference, from CreateTexture
			g_depth.surface = surf;
			g_depth.width = w;
			g_depth.height = h;
			g_depth.targetWidth = g_depth.targetHeight = 0;
			Log("Direct3D: scene depth %ux%u readable (game format 0x%X)\n", w, h, static_cast<unsigned>(format));
			*out = surf; // the game's reference
			return D3D_OK;
		}
		if (tex)
			tex->Release();
		Log("Direct3D: readable depth refused (0x%lX), game format kept\n", static_cast<unsigned long>(hr));
	}
	return original(self, w, h, format, ms, quality, discard, out, shared);
}

HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DDevice9* self, IDirect3DSurface9* src, IDirect3DSurface9* dst)
{
	return g_GetRenderTargetData.Original<TargetDataFn>(self)(self, SceneSource(self, src), dst);
}

HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DDevice9* self, IDirect3DSurface9* src, const RECT* srcRect,
	IDirect3DSurface9* dst, const RECT* dstRect, D3DTEXTUREFILTERTYPE filter)
{
	return g_StretchRect.Original<StretchRectFn>(self)(self, SceneSource(self, src), srcRect, dst, dstRect, filter);
}

HRESULT STDMETHODCALLTYPE SetRenderTarget(IDirect3DDevice9* self, DWORD index, IDirect3DSurface9* target)
{
	if (!g_internal && index == 0 && g_cfg.shadowScale > 1)
	{
		auto it = target ? g_bigSurfaces.find(target) : g_bigSurfaces.end();
		g_targetIsBig = it != g_bigSurfaces.end();
		if (g_targetIsBig)
		{
			g_targetW = it->second.first;
			g_targetH = it->second.second;
		}
	}
	const auto original = g_SetRenderTarget.Original<SetTargetFn>(self);
	if (g_internal || index != 0 || g_sceneMsaa == D3DMULTISAMPLE_NONE)
		return original(self, index, target);

	// The game may hand back what GetRenderTarget gave it: our twin stands for its texture.
	if (auto back = target ? g_surfaceOfTwin.find(target) : g_surfaceOfTwin.end(); back != g_surfaceOfTwin.end())
		target = back->second;
	SceneTwin* twin = TwinFor(target);
	const HRESULT hr = original(self, 0, twin ? twin->msaa : target);
	if (FAILED(hr))
		return hr;
	if (g_twinBound && g_twinBound != twin)
		Resolve(self, *g_twinBound); // the scene is finished: into the game's texture
	g_twinBound = twin;
	if (twin)
	{
		twin->dirty = true;
		g_targetMsaa = g_sceneMsaa;
	}
	else
	{
		D3DSURFACE_DESC d;
		g_targetMsaa = (target && SUCCEEDED(target->GetDesc(&d))) ? d.MultiSampleType : D3DMULTISAMPLE_NONE;
	}
	RebindDepth(self);
	if (g_ssaaSupported)
	{
		static bool said = false;
		if (!said && g_targetMsaa != D3DMULTISAMPLE_NONE)
		{
			said = true;
			Log("Direct3D: alpha-tested edges supersampled (frame %ld)\n", g_frames);
		}
		InternalCalls inside;
		self->SetRenderState(D3DRS_ADAPTIVETESS_Y, g_targetMsaa != D3DMULTISAMPLE_NONE ? kSsaa : D3DFMT_UNKNOWN);
	}
	return hr;
}

HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DDevice9* self, IDirect3DSurface9* depth)
{
	const auto original = g_SetDepthStencilSurface.Original<SetDepthFn>(self);
	if (!g_internal && g_sceneMsaa != D3DMULTISAMPLE_NONE)
	{
		if (auto it = depth ? g_depthOfTwin.find(depth) : g_depthOfTwin.end(); it != g_depthOfTwin.end())
			depth = it->second;
		g_boundDepth = depth;
		return original(self, DepthFor(self, depth));
	}
	if (g_internal || !g_depth.surface)
		return original(self, depth);

	if (depth == g_depth.surface)
	{
		// The largest target drawn with the scene depth is the scene itself.
		IDirect3DSurface9* rt = nullptr;
		if (SUCCEEDED(self->GetRenderTarget(0, &rt)) && rt)
		{
			D3DSURFACE_DESC d;
			if (SUCCEEDED(rt->GetDesc(&d)) && (d.Width > g_depth.targetWidth || d.Height > g_depth.targetHeight))
			{
				g_depth.targetWidth = d.Width;
				g_depth.targetHeight = d.Height;
				Log("Direct3D: scene drawn at %ux%u into a %ux%u depth\n", d.Width, d.Height, g_depth.width, g_depth.height);
			}
			rt->Release();
		}
	}
	else if (!depth && g_boundDepth == g_depth.surface && !g_depth.aoDoneThisFrame)
	{
		// The scene is finished and the interface not drawn yet: the moment for ambient occlusion,
		// before menus and subtitles are laid on top.
		IDirect3DSurface9* rt = nullptr;
		IDirect3DSurface9* bb = nullptr;
		self->GetRenderTarget(0, &rt);
		self->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
		if (rt && rt == bb)
			RunAmbientOcclusionPass(self);
		if (rt)
			rt->Release();
		if (bb)
			bb->Release();
	}
	g_boundDepth = depth;
	return original(self, depth);
}

HRESULT STDMETHODCALLTYPE SetViewport(IDirect3DDevice9* self, const D3DVIEWPORT9* vp)
{
	const auto original = g_SetViewport.Original<SetViewportFn>(self);
	// Only a viewport that fits the size the game believes in: one already in the larger
	// coordinates passes unchanged.
	if (!g_internal && g_targetIsBig && vp && vp->X + vp->Width <= g_targetW && vp->Y + vp->Height <= g_targetH)
	{
		D3DVIEWPORT9 big = *vp;
		const DWORD k = static_cast<DWORD>(g_cfg.shadowScale);
		big.X *= k;
		big.Y *= k;
		big.Width *= k;
		big.Height *= k;
		return original(self, &big);
	}
	return original(self, vp);
}

// A multisampled target is only antialiased while this state is on: a game written without
// multisampling may switch it off, which leaves the samples all alike.
HRESULT STDMETHODCALLTYPE SetRenderState(IDirect3DDevice9* self, D3DRENDERSTATETYPE state, DWORD value)
{
	const auto original = g_SetRenderState.Original<SetStateFn>(self);
	if (!g_internal && state == D3DRS_MULTISAMPLEANTIALIAS && !value && g_sceneMsaa != D3DMULTISAMPLE_NONE)
	{
		static bool logged = false;
		if (!logged)
		{
			logged = true;
			Log("Direct3D: the game switches multisample antialiasing off, kept on\n");
		}
		value = TRUE;
	}
	return original(self, state, value);
}

// The games ask for bilinear filtering without mipmaps, point filtering in places, and a
// sharpness bias of their own; these are overridden per the ini.
DWORD SamplerValue(D3DSAMPLERSTATETYPE type, DWORD value)
{
	if (g_cfg.anisotropy > 0)
	{
		if (type == D3DSAMP_MINFILTER)
			value = D3DTEXF_ANISOTROPIC;
		else if (type == D3DSAMP_MAGFILTER && value == D3DTEXF_POINT)
			value = D3DTEXF_LINEAR;
		else if (type == D3DSAMP_MIPFILTER)
			value = D3DTEXF_LINEAR;
		else if (type == D3DSAMP_MAXANISOTROPY && value < static_cast<DWORD>(g_cfg.anisotropy))
			value = static_cast<DWORD>(g_cfg.anisotropy);
	}
	else if (type == D3DSAMP_MIPFILTER && value == D3DTEXF_NONE && g_cfg.forceTrilinear)
		value = D3DTEXF_LINEAR; // the mipmaps generated above are used
	if (type == D3DSAMP_MIPMAPLODBIAS && g_cfg.lodBias != 0.0f)
		memcpy(&value, &g_cfg.lodBias, sizeof(value));
	if (type == D3DSAMP_MAXMIPLEVEL)
		value = 0;
	return value;
}

HRESULT STDMETHODCALLTYPE SetSamplerState(IDirect3DDevice9* self, DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value)
{
	const auto original = g_SetSamplerState.Original<SetSamplerFn>(self);
	return original(self, sampler, type, g_internal ? value : SamplerValue(type, value));
}

HRESULT STDMETHODCALLTYPE SetTexture(IDirect3DDevice9* self, DWORD stage, IDirect3DBaseTexture9* tex)
{
	if (!g_internal && tex && !g_sceneTwins.empty())
	{
		// Read before the game has moved to another target: the scene drawn so far first.
		// A key only: a cube or volume texture simply is not found.
		auto it = g_sceneTwins.find(reinterpret_cast<IDirect3DTexture9*>(tex));
		if (it != g_sceneTwins.end())
		{
			Resolve(self, it->second);
			if (g_twinBound == &it->second)
				it->second.dirty = true; // still the target: what is drawn next counts too
		}
	}
	const HRESULT hr = g_SetTexture.Original<SetTextureFn>(self)(self, stage, tex);
	if (g_internal || FAILED(hr) || !tex)
		return hr;
	// Textures bound without the game touching the sampler still get the filtering above.
	const auto sampler = g_SetSamplerState.Original<SetSamplerFn>(self);
	if (g_cfg.forceTrilinear)
		sampler(self, stage, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
	if (g_cfg.lodBias != 0.0f)
	{
		DWORD bias;
		memcpy(&bias, &g_cfg.lodBias, sizeof(bias));
		sampler(self, stage, D3DSAMP_MIPMAPLODBIAS, bias);
	}
	if (g_cfg.anisotropy > 0)
	{
		sampler(self, stage, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
		sampler(self, stage, D3DSAMP_MAXANISOTROPY, static_cast<DWORD>(g_cfg.anisotropy));
	}
	return hr;
}
}

void KeepDeviceRedirects(IDirect3DDevice9* dev)
{
	TakeBackRedirects(dev);
}

IDirect3D9* HookDirect3D9(IDirect3D9* d3d)
{
	g_CreateDevice.Install(d3d);
	IDirect3D9Ex* ex = nullptr;
	if (SUCCEEDED(d3d->QueryInterface(IID_IDirect3D9Ex, reinterpret_cast<void**>(&ex))) && ex)
	{
		g_CreateDevice.Install(ex); // an Ex object has its own table
		g_CreateDeviceEx.Install(ex);
		ex->Release();
	}
	Log("Direct3D: %p followed\n", d3d);
	return d3d;
}

void ReportMipmaps(LONG frame)
{
	static LONG said[3] = { -1, -1, -1 };
	const LONG now[3] = { g_mipChains, g_mipChainsDefault, g_mipsFilled };
	if (frame % 1200 || !memcmp(now, said, sizeof(now)))
		return;
	memcpy(said, now, sizeof(now));
	Log("Mipmaps: %ld chains made (%ld in the default pool), %ld filled (frame %ld)\n", now[0], now[1], now[2], frame);
}
