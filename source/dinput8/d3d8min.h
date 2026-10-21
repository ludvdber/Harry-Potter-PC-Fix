// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// The little of Direct3D 8 that dinput8.dll touches, written out by hand: the Windows SDK has no
// d3d8.h any more. Method tables in the order of Microsoft's d3d8.h (checked against the public
// headers of Wine and against D3DDrv.dll, which presents through slot 15 of its device, notes/HP3.md);
// only the methods we call are given a real signature, the others hold their place.
#pragma once
#include <windows.h>
#include <unknwn.h>

typedef DWORD D3DCOLOR;
#define D3DCOLOR_ARGB(a, r, g, b) ((D3DCOLOR)((((a) & 0xff) << 24) | (((r) & 0xff) << 16) | (((g) & 0xff) << 8) | ((b) & 0xff)))

enum D3DFORMAT8 : DWORD { D3DFMT8_UNKNOWN = 0, D3DFMT8_A8R8G8B8 = 21, D3DFMT8_X8R8G8B8 = 22, D3DFMT8_R5G6B5 = 23,
	D3DFMT8_X1R5G5B5 = 24, D3DFMT8_A1R5G5B5 = 25 };
enum D3DPOOL8 : DWORD { D3DPOOL8_DEFAULT = 0, D3DPOOL8_MANAGED = 1, D3DPOOL8_SYSTEMMEM = 2 };

constexpr DWORD kD3D8SdkVersion = 220;          // D3D_SDK_VERSION of the DirectX 8.1 SDK, what D3DDrv passes
constexpr DWORD kD3D8BackBufferMono = 0;        // D3DBACKBUFFER_TYPE_MONO
constexpr DWORD kD3D8LockReadOnly = 0x10;       // D3DLOCK_READONLY
constexpr DWORD kD3D8StateBlockAll = 1;         // D3DSBT_ALL
constexpr DWORD kD3D8TriangleList = 4;          // D3DPT_TRIANGLELIST
constexpr DWORD kD3D8FvfXyzRhw = 0x004, kD3D8FvfDiffuse = 0x040, kD3D8FvfTex1 = 0x100;
constexpr DWORD kD3D8MultisampleNone = 0;

// Render states (D3DRS_*).
enum : DWORD { D3DRS8_ZENABLE = 7, D3DRS8_FILLMODE = 8, D3DRS8_ZWRITEENABLE = 14, D3DRS8_ALPHATESTENABLE = 15,
	D3DRS8_SRCBLEND = 19, D3DRS8_DESTBLEND = 20, D3DRS8_CULLMODE = 22, D3DRS8_ALPHABLENDENABLE = 27,
	D3DRS8_FOGENABLE = 28, D3DRS8_STENCILENABLE = 52, D3DRS8_LIGHTING = 137, D3DRS8_CLIPPLANEENABLE = 152,
	D3DRS8_COLORWRITEENABLE = 168, D3DRS8_BLENDOP = 171 };
// Texture stage states (D3DTSS_*); in Direct3D 8 the sampler lives here too.
enum : DWORD { D3DTSS8_COLOROP = 1, D3DTSS8_COLORARG1 = 2, D3DTSS8_COLORARG2 = 3, D3DTSS8_ALPHAOP = 4,
	D3DTSS8_ALPHAARG1 = 5, D3DTSS8_ALPHAARG2 = 6, D3DTSS8_TEXCOORDINDEX = 11, D3DTSS8_ADDRESSU = 13,
	D3DTSS8_ADDRESSV = 14, D3DTSS8_MAGFILTER = 16, D3DTSS8_MINFILTER = 17, D3DTSS8_MIPFILTER = 18,
	D3DTSS8_TEXTURETRANSFORMFLAGS = 24 };
enum : DWORD { D3DCULL8_NONE = 1, D3DFILL8_SOLID = 3, D3DBLEND8_SRCALPHA = 5, D3DBLEND8_INVSRCALPHA = 6,
	D3DBLENDOP8_ADD = 1, D3DTOP8_DISABLE = 1, D3DTOP8_SELECTARG1 = 2, D3DTOP8_MODULATE = 4, D3DTA8_DIFFUSE = 0,
	D3DTA8_TEXTURE = 2, D3DTEXF8_NONE = 0, D3DTEXF8_POINT = 1, D3DTADDRESS8_CLAMP = 3, D3DTTFF8_DISABLE = 0 };

struct D3DPRESENT_PARAMETERS8
{
	UINT BackBufferWidth, BackBufferHeight;
	D3DFORMAT8 BackBufferFormat;
	UINT BackBufferCount;
	DWORD MultiSampleType;
	DWORD SwapEffect;
	HWND hDeviceWindow;
	BOOL Windowed;
	BOOL EnableAutoDepthStencil;
	D3DFORMAT8 AutoDepthStencilFormat;
	DWORD Flags;
	UINT FullScreen_RefreshRateInHz;
	UINT FullScreen_PresentationInterval;
};

struct D3DSURFACE_DESC8
{
	D3DFORMAT8 Format;
	DWORD Type;
	DWORD Usage;
	D3DPOOL8 Pool;
	UINT Size;
	DWORD MultiSampleType;
	UINT Width, Height;
};

struct D3DLOCKED_RECT8
{
	INT Pitch;
	void* pBits;
};

struct D3DVIEWPORT8
{
	DWORD X, Y, Width, Height;
	float MinZ, MaxZ;
};

struct IDirect3DDevice8;
struct IDirect3DTexture8;

struct IDirect3DSurface8 : IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice8**) = 0;        // 3
	virtual HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void*, DWORD, DWORD) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void*, DWORD*) = 0;
	virtual HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetContainer(REFIID, void**) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC8*) = 0;           // 8
	virtual HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT8*, const RECT*, DWORD) = 0; // 9
	virtual HRESULT STDMETHODCALLTYPE UnlockRect() = 0;                          // 10
};

struct IDirect3DBaseTexture8 : IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice8**) = 0;        // 3
	virtual HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void*, DWORD, DWORD) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void*, DWORD*) = 0;
	virtual HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) = 0;
	virtual DWORD STDMETHODCALLTYPE SetPriority(DWORD) = 0;                      // 7
	virtual DWORD STDMETHODCALLTYPE GetPriority() = 0;
	virtual void STDMETHODCALLTYPE PreLoad() = 0;
	virtual DWORD STDMETHODCALLTYPE GetType() = 0;                               // 10
	virtual DWORD STDMETHODCALLTYPE SetLOD(DWORD) = 0;
	virtual DWORD STDMETHODCALLTYPE GetLOD() = 0;
	virtual DWORD STDMETHODCALLTYPE GetLevelCount() = 0;                         // 13
};

struct IDirect3DTexture8 : IDirect3DBaseTexture8
{
	virtual HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT, D3DSURFACE_DESC8*) = 0; // 14
	virtual HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT, IDirect3DSurface8**) = 0;
	virtual HRESULT STDMETHODCALLTYPE LockRect(UINT, D3DLOCKED_RECT8*, const RECT*, DWORD) = 0; // 16
	virtual HRESULT STDMETHODCALLTYPE UnlockRect(UINT) = 0;                      // 17
	virtual HRESULT STDMETHODCALLTYPE AddDirtyRect(const RECT*) = 0;
};

// Slots we redirect in the device's table.
constexpr int kD3D8SlotReset = 14;
constexpr int kD3D8SlotPresent = 15;
// Slot we redirect in IDirect3D8's table.
constexpr int kD3D8SlotCreateDevice = 15;

struct IDirect3DDevice8 : IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE TestCooperativeLevel() = 0;                // 3
	virtual UINT STDMETHODCALLTYPE GetAvailableTextureMem() = 0;
	virtual HRESULT STDMETHODCALLTYPE ResourceManagerDiscardBytes(DWORD) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetDirect3D(void**) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetDeviceCaps(void*) = 0;                  // 7
	virtual HRESULT STDMETHODCALLTYPE GetDisplayMode(void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetCreationParameters(void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT, UINT, IDirect3DSurface8*) = 0; // 10
	virtual void STDMETHODCALLTYPE SetCursorPosition(int, int, DWORD) = 0;
	virtual BOOL STDMETHODCALLTYPE ShowCursor(BOOL) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS8*, void**) = 0; // 13
	virtual HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS8*) = 0;        // 14
	virtual HRESULT STDMETHODCALLTYPE Present(const RECT*, const RECT*, HWND, const RGNDATA*) = 0; // 15
	virtual HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT, DWORD, IDirect3DSurface8**) = 0; // 16
	virtual HRESULT STDMETHODCALLTYPE GetRasterStatus(void*) = 0;
	virtual void STDMETHODCALLTYPE SetGammaRamp(DWORD, const void*) = 0;
	virtual void STDMETHODCALLTYPE GetGammaRamp(void*) = 0;                      // 19
	virtual HRESULT STDMETHODCALLTYPE CreateTexture(UINT, UINT, UINT, DWORD, D3DFORMAT8, D3DPOOL8, IDirect3DTexture8**) = 0; // 20
	virtual HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT, UINT, UINT, UINT, DWORD, D3DFORMAT8, D3DPOOL8, void**) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT, UINT, DWORD, D3DFORMAT8, D3DPOOL8, void**) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT, DWORD, DWORD, D3DPOOL8, void**) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT, DWORD, D3DFORMAT8, D3DPOOL8, void**) = 0; // 24
	virtual HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT, UINT, D3DFORMAT8, DWORD, BOOL, IDirect3DSurface8**) = 0; // 25
	virtual HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT, UINT, D3DFORMAT8, DWORD, IDirect3DSurface8**) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateImageSurface(UINT, UINT, D3DFORMAT8, IDirect3DSurface8**) = 0; // 27
	virtual HRESULT STDMETHODCALLTYPE CopyRects(IDirect3DSurface8*, const RECT*, UINT, IDirect3DSurface8*, const POINT*) = 0; // 28
	virtual HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture8*, IDirect3DBaseTexture8*) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetFrontBuffer(IDirect3DSurface8*) = 0;    // 30
	virtual HRESULT STDMETHODCALLTYPE SetRenderTarget(IDirect3DSurface8*, IDirect3DSurface8*) = 0; // 31
	virtual HRESULT STDMETHODCALLTYPE GetRenderTarget(IDirect3DSurface8**) = 0;  // 32
	virtual HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface8**) = 0; // 33
	virtual HRESULT STDMETHODCALLTYPE BeginScene() = 0;                          // 34
	virtual HRESULT STDMETHODCALLTYPE EndScene() = 0;                            // 35
	virtual HRESULT STDMETHODCALLTYPE Clear(DWORD, const void*, DWORD, D3DCOLOR, float, DWORD) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetTransform(DWORD, const void*) = 0;      // 37
	virtual HRESULT STDMETHODCALLTYPE GetTransform(DWORD, void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE MultiplyTransform(DWORD, const void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT8*) = 0;      // 40
	virtual HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT8*) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetMaterial(const void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetMaterial(void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetLight(DWORD, const void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetLight(DWORD, void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE LightEnable(DWORD, BOOL) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD, BOOL*) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD, const float*) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD, float*) = 0;           // 49
	virtual HRESULT STDMETHODCALLTYPE SetRenderState(DWORD, DWORD) = 0;          // 50
	virtual HRESULT STDMETHODCALLTYPE GetRenderState(DWORD, DWORD*) = 0;
	virtual HRESULT STDMETHODCALLTYPE BeginStateBlock() = 0;                     // 52
	virtual HRESULT STDMETHODCALLTYPE EndStateBlock(DWORD*) = 0;
	virtual HRESULT STDMETHODCALLTYPE ApplyStateBlock(DWORD) = 0;                // 54
	virtual HRESULT STDMETHODCALLTYPE CaptureStateBlock(DWORD) = 0;
	virtual HRESULT STDMETHODCALLTYPE DeleteStateBlock(DWORD) = 0;               // 56
	virtual HRESULT STDMETHODCALLTYPE CreateStateBlock(DWORD, DWORD*) = 0;       // 57
	virtual HRESULT STDMETHODCALLTYPE SetClipStatus(const void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetClipStatus(void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetTexture(DWORD, IDirect3DBaseTexture8**) = 0; // 60
	virtual HRESULT STDMETHODCALLTYPE SetTexture(DWORD, IDirect3DBaseTexture8*) = 0;  // 61
	virtual HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD, DWORD, DWORD*) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD, DWORD, DWORD) = 0; // 63
	virtual HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD*) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetInfo(DWORD, void*, DWORD) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT, const PALETTEENTRY*) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT, PALETTEENTRY*) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT*) = 0;        // 69
	virtual HRESULT STDMETHODCALLTYPE DrawPrimitive(DWORD, UINT, UINT) = 0;      // 70
	virtual HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(DWORD, UINT, UINT, UINT, UINT) = 0;
	virtual HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(DWORD, UINT, const void*, UINT) = 0; // 72
	virtual HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(DWORD, UINT, UINT, UINT, const void*, D3DFORMAT8, const void*, UINT) = 0;
	virtual HRESULT STDMETHODCALLTYPE ProcessVertices(UINT, UINT, UINT, void*, DWORD) = 0; // 74
	virtual HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD*, const DWORD*, DWORD*, DWORD) = 0; // 75
	virtual HRESULT STDMETHODCALLTYPE SetVertexShader(DWORD) = 0;                // 76 (an FVF code is a handle)
	virtual HRESULT STDMETHODCALLTYPE GetVertexShader(DWORD*) = 0;               // 77
	virtual HRESULT STDMETHODCALLTYPE DeleteVertexShader(DWORD) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetVertexShaderConstant(DWORD, const void*, DWORD) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetVertexShaderConstant(DWORD, void*, DWORD) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetVertexShaderDeclaration(DWORD, void*, DWORD*) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetVertexShaderFunction(DWORD, void*, DWORD*) = 0; // 82
	virtual HRESULT STDMETHODCALLTYPE SetStreamSource(UINT, void*, UINT) = 0;    // 83
	virtual HRESULT STDMETHODCALLTYPE GetStreamSource(UINT, void**, UINT*) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetIndices(void*, UINT) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetIndices(void**, UINT*) = 0;             // 86
	virtual HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD*, DWORD*) = 0; // 87
	virtual HRESULT STDMETHODCALLTYPE SetPixelShader(DWORD) = 0;                 // 88
	virtual HRESULT STDMETHODCALLTYPE GetPixelShader(DWORD*) = 0;                // 89
	virtual HRESULT STDMETHODCALLTYPE DeletePixelShader(DWORD) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetPixelShaderConstant(DWORD, const void*, DWORD) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetPixelShaderConstant(DWORD, void*, DWORD) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetPixelShaderFunction(DWORD, void*, DWORD*) = 0; // 93
	virtual HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT, const float*, const void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT, const float*, const void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE DeletePatch(UINT) = 0;                     // 96
};

struct IDirect3D8 : IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void*) = 0;        // 3
	virtual UINT STDMETHODCALLTYPE GetAdapterCount() = 0;
	virtual HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT, DWORD, void*) = 0;
	virtual UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT) = 0;
	virtual HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT, UINT, void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT, void*) = 0;    // 8
	virtual HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT, DWORD, D3DFORMAT8, D3DFORMAT8, BOOL) = 0;
	virtual HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT, DWORD, D3DFORMAT8, DWORD, DWORD, D3DFORMAT8) = 0;
	virtual HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT, DWORD, D3DFORMAT8, BOOL, DWORD) = 0;
	virtual HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT, DWORD, D3DFORMAT8, D3DFORMAT8, D3DFORMAT8) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT, DWORD, void*) = 0;     // 13
	virtual HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateDevice(UINT, DWORD, HWND, DWORD, D3DPRESENT_PARAMETERS8*, IDirect3DDevice8**) = 0; // 15
};

typedef IDirect3D8* (WINAPI* Direct3DCreate8Fn)(UINT sdkVersion);
