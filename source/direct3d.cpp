// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// Direct3D 9. The game keeps the objects Direct3D creates; the methods below are redirected in
// their method tables (hooks.h). Slot numbers are the declaration order in d3d9.h.

#include "hooks.h"
#include "render_state.h"
#include "d3dx9.h"
#include <unordered_map>

#pragma comment(lib, "d3dx9.lib")
#pragma comment(lib, "dxguid.lib")

SceneDepth g_depth;
int g_backBufferWidth = 0, g_backBufferHeight = 0;

namespace
{
// ---- Presentation parameters ---------------------------------------------------------------

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
HRESULT STDMETHODCALLTYPE SetRenderTarget(IDirect3DDevice9*, DWORD, IDirect3DSurface9*);
HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DDevice9*, IDirect3DSurface9*);
HRESULT STDMETHODCALLTYPE SetViewport(IDirect3DDevice9*, const D3DVIEWPORT9*);
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
REDIRECT(37, SetRenderTarget);
REDIRECT(39, SetDepthStencilSurface);
REDIRECT(47, SetViewport);
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

void HookDevice(IDirect3DDevice9* dev)
{
	g_Reset.Install(dev);
	g_Present.Install(dev);
	g_CreateTexture.Install(dev);
	g_CreateDepthStencilSurface.Install(dev);
	g_SetRenderTarget.Install(dev);
	g_SetDepthStencilSurface.Install(dev);
	g_SetViewport.Install(dev);
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
		&g_SetRenderTarget, &g_SetDepthStencilSurface, &g_SetViewport, &g_SetTexture, &g_SetSamplerState };
	static const char* const names[] = { "Reset", "Present", "CreateTexture", "CreateDepthStencilSurface",
		"SetRenderTarget", "SetDepthStencilSurface", "SetViewport", "SetTexture", "SetSamplerState" };
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
		HookDevice(*out);
		OnDeviceRestored(*out);
	}
	return hr;
}

void BeforeReset()
{
	OnDeviceLost();
	ForgetBigTargets();
	g_boundDepth = nullptr;
}

HRESULT STDMETHODCALLTYPE Reset(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* pp)
{
	const auto original = g_Reset.Original<ResetFn>(self);
	if (g_internal || !pp)
		return original(self, pp);
	BeforeReset();
	PrepareWindow(pp, nullptr);
	const D3DFORMAT gameDepth = pp->AutoDepthStencilFormat;
	AdjustPresentation(pp);
	const HRESULT hr = WithFallbacks(pp, gameDepth, [&] { return original(self, pp); });
	Log("Direct3D: reset, hr=0x%lX\n", static_cast<unsigned long>(hr));
	if (SUCCEEDED(hr))
		OnDeviceRestored(self);
	return hr;
}

HRESULT STDMETHODCALLTYPE ResetEx(IDirect3DDevice9Ex* self, D3DPRESENT_PARAMETERS* pp, D3DDISPLAYMODEEX* mode)
{
	const auto original = g_ResetEx.Original<ResetExFn>(self);
	if (g_internal || !pp)
		return original(self, pp, mode);
	BeforeReset();
	PrepareWindow(pp, nullptr);
	if (g_cfg.windowed)
		mode = nullptr;
	const D3DFORMAT gameDepth = pp->AutoDepthStencilFormat;
	AdjustPresentation(pp);
	const HRESULT hr = WithFallbacks(pp, gameDepth, [&] { return original(self, pp, mode); });
	Log("Direct3D: Ex reset, hr=0x%lX\n", static_cast<unsigned long>(hr));
	if (SUCCEEDED(hr))
		OnDeviceRestored(self);
	return hr;
}

// Present can reach Direct3D three ways (device, Ex device, swap chain), and one implementation
// may call another inside: only the outermost call is a frame.
HRESULT STDMETHODCALLTYPE Present(IDirect3DDevice9* self, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty)
{
	const auto original = g_Present.Original<PresentFn>(self);
	if (g_internal)
		return original(self, src, dst, wnd, dirty);
	BeforePresent(self);
	InternalCalls inside;
	return original(self, src, dst, wnd, dirty);
}

HRESULT STDMETHODCALLTYPE PresentEx(IDirect3DDevice9Ex* self, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty, DWORD flags)
{
	const auto original = g_PresentEx.Original<PresentExFn>(self);
	if (g_internal)
		return original(self, src, dst, wnd, dirty, flags);
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
	g_UnlockRect.Install(tex);
	if (withChain)
	{
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
	}
	return left;
}

HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(IDirect3DDevice9* self, UINT w, UINT h, D3DFORMAT format,
	D3DMULTISAMPLE_TYPE ms, DWORD quality, BOOL discard, IDirect3DSurface9** out, HANDLE* shared)
{
	const auto original = g_CreateDepthStencilSurface.Original<CreateDepthFn>(self);
	if (g_internal)
		return original(self, w, h, format, ms, quality, discard, out, shared);

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
	return g_SetRenderTarget.Original<SetTargetFn>(self)(self, index, target);
}

HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DDevice9* self, IDirect3DSurface9* depth)
{
	const auto original = g_SetDepthStencilSurface.Original<SetDepthFn>(self);
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

LONG MipmapsFilled()
{
	return g_mipsFilled;
}
