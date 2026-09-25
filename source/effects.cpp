// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// Image effects, drawn over the finished frame: FXAA with sharpening, colour grading, ambient
// occlusion, bloom and light shafts. All optional, all set in [Accio.Graphics].
//
// Two moments: ambient occlusion runs when the game unbinds its scene depth (the 3D scene is
// done, menus and subtitles are not drawn yet), everything else just before Present. Each pass
// saves the whole device state and puts it back, so the game never sees a difference.

#include "hooks.h"
#include "render_state.h"
#include "shaders.h"
#include "d3dx9.h"
#include <cstring>

static IDirect3DTexture9*    s_fxaaTex  = nullptr;
static IDirect3DSurface9*    s_fxaaSurf = nullptr;
static IDirect3DPixelShader9* s_fxaaPS  = nullptr;
static IDirect3DPixelShader9* s_aoPS    = nullptr;
static UINT s_fxaaW = 0, s_fxaaH = 0;
static bool s_fxaaInitFailed = false;

// Light-pass resources (half-res). All created together when Bloom or GodRays is enabled.
static const int PROBE_SIZE = 64;
static IDirect3DTexture9*     s_brightTex = nullptr; static IDirect3DSurface9* s_brightSurf = nullptr;
static IDirect3DTexture9*     s_blur0Tex  = nullptr; static IDirect3DSurface9* s_blur0Surf  = nullptr;
static IDirect3DTexture9*     s_blur1Tex  = nullptr; static IDirect3DSurface9* s_blur1Surf  = nullptr;
static IDirect3DTexture9*     s_rayTex    = nullptr; static IDirect3DSurface9* s_raySurf    = nullptr;
static IDirect3DTexture9*     s_probeTex  = nullptr; static IDirect3DSurface9* s_probeSurf  = nullptr;
static IDirect3DSurface9*     s_probeSysSurf = nullptr;
static IDirect3DPixelShader9* s_brightPS = nullptr;
static IDirect3DPixelShader9* s_blurPS   = nullptr;
static IDirect3DPixelShader9* s_rayPS    = nullptr;
static int  s_lightW = 0, s_lightH = 0;
static bool s_lightingReady = false;
static float s_frameBloomStr = 0.0f; // per-frame composite strengths, set by RenderLighting
static float s_frameRayStr   = 0.0f;

// Small shared helpers for the multi-pass light pipeline.
static void SetLinearClamp(IDirect3DDevice9* dev, DWORD s)
{
    dev->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev->SetSamplerState(s, D3DSAMP_ADDRESSU,  D3DTADDRESS_CLAMP);
    dev->SetSamplerState(s, D3DSAMP_ADDRESSV,  D3DTADDRESS_CLAMP);
}

static void DrawFSQuad(IDirect3DDevice9* dev, float w, float h)
{
    struct V { float x, y, z, rhw, u, v; };
    V q[4] = {
        { -0.5f,   -0.5f,   0, 1, 0, 0 },
        { w-0.5f,  -0.5f,   0, 1, 1, 0 },
        { -0.5f,   h-0.5f,  0, 1, 0, 1 },
        { w-0.5f,  h-0.5f,  0, 1, 1, 1 },
    };
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V));
}

static void FreeLighting()
{
    if (s_brightSurf) { s_brightSurf->Release(); s_brightSurf = nullptr; }
    if (s_brightTex)  { s_brightTex->Release();  s_brightTex  = nullptr; }
    if (s_blur0Surf)  { s_blur0Surf->Release();  s_blur0Surf  = nullptr; }
    if (s_blur0Tex)   { s_blur0Tex->Release();   s_blur0Tex   = nullptr; }
    if (s_blur1Surf)  { s_blur1Surf->Release();  s_blur1Surf  = nullptr; }
    if (s_blur1Tex)   { s_blur1Tex->Release();   s_blur1Tex   = nullptr; }
    if (s_raySurf)    { s_raySurf->Release();    s_raySurf    = nullptr; }
    if (s_rayTex)     { s_rayTex->Release();     s_rayTex     = nullptr; }
    if (s_probeSurf)  { s_probeSurf->Release();  s_probeSurf  = nullptr; }
    if (s_probeTex)   { s_probeTex->Release();   s_probeTex   = nullptr; }
    if (s_probeSysSurf) { s_probeSysSurf->Release(); s_probeSysSurf = nullptr; }
    if (s_brightPS)   { s_brightPS->Release();   s_brightPS   = nullptr; }
    if (s_blurPS)     { s_blurPS->Release();     s_blurPS     = nullptr; }
    if (s_rayPS)      { s_rayPS->Release();      s_rayPS      = nullptr; }
    s_lightW = s_lightH = 0;
    s_lightingReady = false;
    s_frameBloomStr = s_frameRayStr = 0.0f;
}

void ReleaseEffects()
{
    if (s_fxaaPS)
        Log("FXAA: resources released (device Reset)\n");
    FreeLighting();
    if (s_fxaaSurf) { s_fxaaSurf->Release(); s_fxaaSurf = nullptr; }
    if (s_fxaaTex)  { s_fxaaTex->Release();  s_fxaaTex  = nullptr; }
    if (s_fxaaPS)   { s_fxaaPS->Release();   s_fxaaPS   = nullptr; }
    if (s_aoPS)     { s_aoPS->Release();     s_aoPS     = nullptr; }
    s_fxaaW = s_fxaaH = 0;
    if (g_depth.texture) { g_depth.texture->Release(); g_depth.texture = nullptr; }
    g_depth.width = g_depth.height = 0;
    // Compare-only diagnostic pointers — would dangle once the texture above is released.
    g_depth.surface = nullptr;
    g_depth.targetWidth = g_depth.targetHeight = 0;
    // A failure can be transient (device mid-Reset); each Reset earns a fresh init attempt,
    // otherwise FXAA/grading/SSAO would stay disabled for the rest of the session.
    s_fxaaInitFailed = false;
}

static IDirect3DPixelShader9* CompilePS(IDirect3DDevice9* dev, const char* src, const char* name)
{
    ID3DXBuffer* pCode = nullptr;
    ID3DXBuffer* pErr  = nullptr;
    IDirect3DPixelShader9* ps = nullptr;
    if (SUCCEEDED(D3DXCompileShader(src, (UINT)strlen(src), nullptr, nullptr,
                                     "main", "ps_3_0", 0, &pCode, &pErr, nullptr)))
    {
        dev->CreatePixelShader((DWORD*)pCode->GetBufferPointer(), &ps);
        pCode->Release();
    }
    else
    {
        Log("Lighting init: %s shader compile failed: %s\n", name,
            pErr ? (char*)pErr->GetBufferPointer() : "?");
    }
    if (pErr) pErr->Release();
    return ps;
}

static IDirect3DTexture9* CreateRT(IDirect3DDevice9* dev, UINT w, UINT h, D3DFORMAT fmt, IDirect3DSurface9** outSurf)
{
    IDirect3DTexture9* tex = nullptr;
    if (FAILED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, fmt, D3DPOOL_DEFAULT, &tex, nullptr)))
        return nullptr;
    tex->GetSurfaceLevel(0, outSurf);
    return tex;
}

// Creates the half-res bloom/god-ray targets + the brightest-pixel probe (RT + system-mem copy).
// Non-fatal: on any failure the lighting pass stays off and FXAA continues normally.
static void InitLighting(IDirect3DDevice9* dev, UINT W, UINT H, D3DFORMAT fmt)
{
    s_lightingReady = false;
    if (!g_cfg.bloom && !g_cfg.godRays) return;

    s_lightW = (int)(W / 2); s_lightH = (int)(H / 2);
    if (s_lightW < 4 || s_lightH < 4) return;

    s_brightTex = CreateRT(dev, s_lightW, s_lightH, fmt, &s_brightSurf);
    s_blur0Tex  = CreateRT(dev, s_lightW, s_lightH, fmt, &s_blur0Surf);
    s_blur1Tex  = CreateRT(dev, s_lightW, s_lightH, fmt, &s_blur1Surf);
    s_rayTex    = CreateRT(dev, s_lightW, s_lightH, fmt, &s_raySurf);
    s_probeTex  = CreateRT(dev, PROBE_SIZE, PROBE_SIZE, fmt, &s_probeSurf);
    dev->CreateOffscreenPlainSurface(PROBE_SIZE, PROBE_SIZE, fmt, D3DPOOL_SYSTEMMEM, &s_probeSysSurf, nullptr);

    // Clear to black so the composite never reads uninitialised GPU memory: when only one of
    // bloom/god rays is active the other buffer is still bound and sampled (then *0). Garbage
    // could be NaN, and NaN*0 = NaN would corrupt the pixel — a black start guarantees 0.
    if (s_brightSurf) dev->ColorFill(s_brightSurf, nullptr, 0);
    if (s_blur0Surf)  dev->ColorFill(s_blur0Surf,  nullptr, 0);
    if (s_blur1Surf)  dev->ColorFill(s_blur1Surf,  nullptr, 0);
    if (s_raySurf)    dev->ColorFill(s_raySurf,    nullptr, 0);

    s_brightPS = CompilePS(dev, kPs_brightPS, "bright");
    s_blurPS   = CompilePS(dev, kPs_blurPS,   "blur");
    s_rayPS    = CompilePS(dev, kPs_rayPS,    "ray");

    bool ok = s_brightTex && s_blur1Tex && s_brightPS && s_blurPS;
    if (g_cfg.godRays) ok = ok && s_rayTex && s_rayPS && s_probeSurf && s_probeSysSurf;
    if (!ok)
    {
        Log("Lighting init: resource/shader creation failed -> lighting pass disabled\n");
        FreeLighting();
        return;
    }
    s_lightingReady = true;
    Log("Lighting init: %dx%d half-res  Bloom=%d (str=%.2f thr=%.2f)  GodRays=%d (str=%.2f decay=%.3f)\n",
        s_lightW, s_lightH, (int)g_cfg.bloom, g_cfg.bloomStrength, g_cfg.bloomThreshold,
        (int)g_cfg.godRays, g_cfg.godRaysStrength, g_cfg.godRaysDecay);
}

static bool InitFXAA(IDirect3DDevice9* dev, UINT W, UINT H, D3DFORMAT fmt)
{
    ReleaseEffects();
    if (FAILED(dev->CreateTexture(W, H, 1, D3DUSAGE_RENDERTARGET, fmt, D3DPOOL_DEFAULT, &s_fxaaTex, nullptr)))
    {
        Log("FXAA init: CreateTexture failed\n");
        return false;
    }
    s_fxaaTex->GetSurfaceLevel(0, &s_fxaaSurf);

    ID3DXBuffer* pCode = nullptr;
    ID3DXBuffer* pErr  = nullptr;
    if (FAILED(D3DXCompileShader(kPs_fxaaPS, (UINT)strlen(kPs_fxaaPS), nullptr, nullptr,
                                  "main", "ps_3_0", 0, &pCode, &pErr, nullptr)))
    {
        Log("FXAA init: shader compile failed: %s\n", pErr ? (char*)pErr->GetBufferPointer() : "?");
        if (pErr) pErr->Release();
        ReleaseEffects();
        return false;
    }
    if (pErr) pErr->Release();
    dev->CreatePixelShader((DWORD*)pCode->GetBufferPointer(), &s_fxaaPS);
    pCode->Release();

    // AO-only shader for the depth-unbind pass (option B). Non-fatal if it fails: the Present
    // pass keeps its own SSAO as a fallback (gated on g_depth.aoDoneThisFrame staying false).
    ID3DXBuffer* pAOCode = nullptr;
    ID3DXBuffer* pAOErr  = nullptr;
    if (SUCCEEDED(D3DXCompileShader(kPs_aoPS, (UINT)strlen(kPs_aoPS), nullptr, nullptr,
                                     "main", "ps_3_0", 0, &pAOCode, &pAOErr, nullptr)))
    {
        dev->CreatePixelShader((DWORD*)pAOCode->GetBufferPointer(), &s_aoPS);
        pAOCode->Release();
    }
    else
    {
        Log("AO-unbind: shader compile failed: %s (falling back to SSAO-at-Present)\n",
            pAOErr ? (char*)pAOErr->GetBufferPointer() : "?");
    }
    if (pAOErr) pAOErr->Release();

    s_fxaaW = W; s_fxaaH = H;
    Log("FXAA init: %dx%d fmt=%d (SSAO kernel: 12-tap spiral, per-pixel rotation)\n", W, H, (int)fmt);

    InitLighting(dev, W, H, fmt);
    return true;
}

// Option B: apply SSAO the instant the game unbinds scene depth (3D done, UI not yet drawn).
// Darkens the back buffer in place; the game then composites UI on top untouched, and the
// Present pass runs FXAA + grading on the result (its own SSAO suppressed via g_depth.aoDoneThisFrame).
// Reuses s_fxaaTex/s_fxaaSurf as the scene-copy scratch (the two passes never overlap in time).
void RunAmbientOcclusionPass(IDirect3DDevice9* dev)
{
    InternalCalls inside;
    if (!g_cfg.fxaa || !g_cfg.ssao || !g_depth.supported || !g_depth.texture) return;
    if (!s_aoPS || !s_fxaaTex || !s_fxaaSurf) return; // resources not ready yet (first frame)

    IDirect3DSurface9* pBB = nullptr;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBB)) || !pBB) return;
    D3DSURFACE_DESC desc;
    pBB->GetDesc(&desc);
    if (desc.Width != s_fxaaW || desc.Height != s_fxaaH) { pBB->Release(); return; }

    if (FAILED(dev->StretchRect(pBB, nullptr, s_fxaaSurf, nullptr, D3DTEXF_NONE))) { pBB->Release(); return; }

    IDirect3DStateBlock9* pSB = nullptr;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &pSB)) || !pSB) { pBB->Release(); return; }

    // Render target is already the back buffer (verified by the caller); leave depth-stencil bound
    // (Z disabled below makes it irrelevant) so we don't disturb the game's about-to-unbind state.
    dev->SetRenderTarget(0, pBB);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    D3DVIEWPORT9 vp = { 0, 0, s_fxaaW, s_fxaaH, 0.0f, 1.0f };
    dev->SetViewport(&vp);

    dev->SetVertexShader(nullptr);
    dev->SetPixelShader(s_aoPS);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    dev->SetTexture(0, s_fxaaTex);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU,  D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV,  D3DTADDRESS_CLAMP);
    dev->SetTexture(1, g_depth.texture);
    dev->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(1, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev->SetSamplerState(1, D3DSAMP_ADDRESSU,  D3DTADDRESS_CLAMP);
    dev->SetSamplerState(1, D3DSAMP_ADDRESSV,  D3DTADDRESS_CLAMP);

    float rcpF[4] = { 1.0f / s_fxaaW, 1.0f / s_fxaaH, (float)s_fxaaW, (float)s_fxaaH };
    dev->SetPixelShaderConstantF(0, rcpF, 1);
    float ssaoP[4] = { g_cfg.ssaoStrength, g_cfg.ssaoRadius, g_cfg.ssaoMinDelta, g_cfg.ssaoMaxDelta };
    dev->SetPixelShaderConstantF(3, ssaoP, 1);
    float dsc[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
    if (g_depth.width && g_depth.height && g_depth.targetWidth && g_depth.targetHeight)
    {
        float sx = (float)g_depth.targetWidth / (float)g_depth.width;
        float sy = (float)g_depth.targetHeight / (float)g_depth.height;
        if (sx <= 1.0f && sy <= 1.0f) { dsc[0] = sx; dsc[1] = sy; }
    }
    dev->SetPixelShaderConstantF(4, dsc, 1);

    struct V { float x, y, z, w, u, v; };
    float W = (float)s_fxaaW, H = (float)s_fxaaH;
    V q[4] = {
        { -0.5f,    -0.5f,    0, 1,  0, 0 },
        { W-0.5f,   -0.5f,    0, 1,  1, 0 },
        { -0.5f,    H-0.5f,   0, 1,  0, 1 },
        { W-0.5f,   H-0.5f,   0, 1,  1, 1 },
    };
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V));

    pSB->Apply();
    pSB->Release();
    pBB->Release();

    g_depth.aoDoneThisFrame = true;
    static bool s_logged = false;
    if (!s_logged) { s_logged = true; Log("SSAO: running at scene depth-unbind, pre-UI (option B)\n"); }
}

// Analyses the bright-pass buffer: downsamples the half-res bright target to a 64x64 RT, reads it
// back to system memory and scans it on the CPU (4096 texels — negligible). Returns the screen-UV
// of the brightest texel (god-ray origin), its luma, and the FRACTION of the frame that is bright.
// That fraction is the menu/over-bright guard: a 2D menu or a near-white close-up has a huge bright
// area (would bloom into a white blob), while a normal scene only has small light sources.
static bool AnalyzeBright(IDirect3DDevice9* dev, float& outU, float& outV, float& outMax, float& outFrac)
{
    if (!s_probeSurf || !s_probeSysSurf || !s_brightSurf) return false;

    // GetRenderTargetData forces a CPU/GPU sync, so we only re-probe every 3rd frame and reuse the
    // cached result in between — neither the light position nor the bright fraction moves fast.
    static int   s_tick = 0;
    static bool  s_haveCache = false;
    static float s_cU = 0.5f, s_cV = 0.5f, s_cMax = 0.0f, s_cFrac = 0.0f;
    if (s_haveCache && (s_tick++ % 3) != 0)
    {
        outU = s_cU; outV = s_cV; outMax = s_cMax; outFrac = s_cFrac;
        return true;
    }

    if (FAILED(dev->StretchRect(s_brightSurf, nullptr, s_probeSurf, nullptr, D3DTEXF_LINEAR))) return false;
    if (FAILED(dev->GetRenderTargetData(s_probeSurf, s_probeSysSurf))) return false;

    D3DLOCKED_RECT lr;
    if (FAILED(s_probeSysSurf->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return false;
    float best = -1.0f; int bx = PROBE_SIZE / 2, by = PROBE_SIZE / 2; int brightCount = 0;
    BYTE* base = (BYTE*)lr.pBits;
    for (int y = 0; y < PROBE_SIZE; y++)
    {
        DWORD* row = (DWORD*)(base + y * lr.Pitch);
        for (int x = 0; x < PROBE_SIZE; x++)
        {
            DWORD px = row[x];
            float r = (float)((px >> 16) & 0xFF);
            float g = (float)((px >> 8)  & 0xFF);
            float b = (float)( px        & 0xFF);
            float l = 0.299f * r + 0.587f * g + 0.114f * b;
            if (l > best) { best = l; bx = x; by = y; }
            if (l > 15.0f) brightCount++; // ~0.06 of full scale = a meaningfully bright texel
        }
    }
    s_probeSysSurf->UnlockRect();
    outU = (bx + 0.5f) / (float)PROBE_SIZE;
    outV = (by + 0.5f) / (float)PROBE_SIZE;
    outMax = best / 255.0f;
    outFrac = (float)brightCount / (float)(PROBE_SIZE * PROBE_SIZE);
    s_cU = outU; s_cV = outV; s_cMax = outMax; s_cFrac = outFrac; s_haveCache = true;
    return true;
}

// Runs the bright-pass, the bloom blur and the god-ray radial blur into the half-res targets.
// Leaves bloom in s_blur1Tex and god rays in s_rayTex; sets the per-frame composite strengths
// (s_frameBloomStr / s_frameRayStr) the FXAA pass then reads. Must run inside ApplyFXAA's state
// block (the block restores the game's render target / shaders afterwards).
static void RenderLighting(IDirect3DDevice9* dev)
{
    s_frameBloomStr = 0.0f;
    s_frameRayStr   = 0.0f;
    if (!s_lightingReady || !s_fxaaTex) return;

    float lw = (float)s_lightW, lh = (float)s_lightH;
    float texX = 1.0f / lw, texY = 1.0f / lh;
    D3DVIEWPORT9 vp = { 0, 0, (DWORD)s_lightW, (DWORD)s_lightH, 0.0f, 1.0f };

    dev->SetDepthStencilSurface(nullptr);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetVertexShader(nullptr);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);

    // --- bright-pass: full-res scene -> half-res bright target ---
    dev->SetRenderTarget(0, s_brightSurf);
    dev->SetViewport(&vp);
    dev->SetPixelShader(s_brightPS);
    dev->SetTexture(0, s_fxaaTex);
    SetLinearClamp(dev, 0);
    float bp[4] = { g_cfg.bloomThreshold, 0, 0, 0 };
    dev->SetPixelShaderConstantF(0, bp, 1);
    DrawFSQuad(dev, lw, lh);

    // Analyse the bright-pass once (light position + bright fraction). The fraction drives the
    // over-bright guard below; the position is the god-ray origin.
    float lu = 0.5f, lv = 0.5f, lmax = 0.0f, lfrac = 0.0f;
    bool haveProbe = AnalyzeBright(dev, lu, lv, lmax, lfrac);
    // Menu / over-bright guard: a 2D menu or a near-white close-up has a huge bright area that
    // would bloom into a white blob. Fade the whole light pass out as the bright fraction climbs:
    // full strength up to 30% of the frame bright, down to 0 by 60%. Normal scenes (small light
    // sources) stay well under 30% and keep full effect.
    float guard = 1.0f;
    if (haveProbe)
    {
        guard = 1.0f - (lfrac - 0.30f) / 0.30f;
        if (guard < 0.0f) guard = 0.0f;
        if (guard > 1.0f) guard = 1.0f;
    }

    // --- bloom: two separable-Gaussian iterations with wide tap spacing ---
    // A single tight 9-tap blur only reaches ~8px and reads as a 1px halo, not a glow. Two
    // iterations (the 2nd twice as wide) spread the light far enough onto surrounding pixels to
    // actually look like bloom. blur0/blur1 ping-pong; the final glow ends up in s_blur1Tex.
    if (g_cfg.bloom && guard > 0.01f)
    {
        dev->SetPixelShader(s_blurPS);
        const float spread = 2.5f;
        struct Pass { IDirect3DSurface9* dst; IDirect3DTexture9* src; float dx, dy; };
        Pass passes[4] = {
            { s_blur0Surf, s_brightTex, texX * spread,        0.0f },
            { s_blur1Surf, s_blur0Tex,  0.0f,                 texY * spread },
            { s_blur0Surf, s_blur1Tex,  texX * spread * 2.0f, 0.0f },
            { s_blur1Surf, s_blur0Tex,  0.0f,                 texY * spread * 2.0f },
        };
        for (int p = 0; p < 4; p++)
        {
            dev->SetRenderTarget(0, passes[p].dst);
            dev->SetViewport(&vp);
            dev->SetTexture(0, passes[p].src);
            SetLinearClamp(dev, 0);
            float d[4] = { passes[p].dx, passes[p].dy, 0, 0 };
            dev->SetPixelShaderConstantF(0, d, 1);
            DrawFSQuad(dev, lw, lh);
        }
        s_frameBloomStr = g_cfg.bloomStrength * guard;
    }

    // --- god rays: radial-blur from the detected brightest on-screen source ---
    if (g_cfg.godRays && haveProbe && lmax > 0.04f && guard > 0.01f)
    {
        dev->SetRenderTarget(0, s_raySurf);
        dev->SetViewport(&vp);
        dev->SetPixelShader(s_rayPS);
        dev->SetTexture(0, s_brightTex);
        SetLinearClamp(dev, 0);
        float c0[4] = { lu, lv, g_cfg.godRaysDecay, 0.95f };
        float c1[4] = { 1.0f, 1.0f, 0, 0 }; // exposure=1 (normalized), density=1; strength applied at composite
        dev->SetPixelShaderConstantF(0, c0, 1);
        dev->SetPixelShaderConstantF(1, c1, 1);
        DrawFSQuad(dev, lw, lh);
        s_frameRayStr = g_cfg.godRaysStrength * guard;
    }
}

static void ApplyFXAA(IDirect3DDevice9* dev, IDirect3DSurface9* pBB)
{
    // Copy the finished scene into our texture
    if (FAILED(dev->StretchRect(pBB, nullptr, s_fxaaSurf, nullptr, D3DTEXF_NONE)))
        return;

    // Save full device state, apply FXAA pass, restore. Without a state block we can't
    // restore the game's state afterwards, so skip the pass entirely rather than corrupt it.
    IDirect3DStateBlock9* pSB = nullptr;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &pSB)) || !pSB)
        return;

    // Light passes (bloom + god rays) render into their own half-res targets first; the FXAA
    // pass below composites the results. No-op (and leaves strengths at 0) when lighting is off.
    RenderLighting(dev);

    dev->SetRenderTarget(0, pBB);
    dev->SetDepthStencilSurface(nullptr);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    D3DVIEWPORT9 vp = { 0, 0, s_fxaaW, s_fxaaH, 0.0f, 1.0f };
    dev->SetViewport(&vp);

    dev->SetVertexShader(nullptr);
    dev->SetPixelShader(s_fxaaPS);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    dev->SetTexture(0, s_fxaaTex);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER,  D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER,  D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER,  D3DTEXF_NONE);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU,   D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV,   D3DTADDRESS_CLAMP);
    float rcpF[4] = { 1.0f / s_fxaaW, 1.0f / s_fxaaH, (float)s_fxaaW, (float)s_fxaaH };
    dev->SetPixelShaderConstantF(0, rcpF, 1);
    // c1 = (lift, gamma, gain, vibrance), c2 = (vignette, sharpness, temperature, tint). Neutral
    // when ColorGrading=0, but sharpness rides in c2.y independently — it is part of the FXAA
    // pass, not grading, so it applies whenever FXAA is on.
    float grade1[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
    float grade2[4] = { 0.0f, g_cfg.sharpness, 0.0f, 0.0f };
    if (g_cfg.grading)
    {
        grade1[0] = g_cfg.lift;
        grade1[1] = (g_cfg.gamma > 0.01f) ? g_cfg.gamma : 1.0f;
        grade1[2] = g_cfg.gain;
        grade1[3] = g_cfg.vibrance;
        grade2[0] = g_cfg.vignette;
        grade2[2] = g_cfg.temperature;
        grade2[3] = g_cfg.tint;
    }
    dev->SetPixelShaderConstantF(1, grade1, 1);
    dev->SetPixelShaderConstantF(2, grade2, 1);
    float gradeC[4] = { g_cfg.grading ? g_cfg.contrast : 0.0f, g_cfg.grading ? g_cfg.splitTone : 0.0f, 0.0f, 0.0f };
    dev->SetPixelShaderConstantF(6, gradeC, 1);

    // SSAO: use the cached scene-depth INTZ texture (populated at CreateDepthStencilSurface time).
    // We do NOT use GetDepthStencilSurface here because the game often unbinds depth before Present
    // (UI rendering); the cached texture still holds the last-rendered scene depth contents.
    // Skip SSAO here when the depth-unbind pass (option B) already darkened this frame's scene.
    // If that pass didn't run (g_depth.aoDoneThisFrame still false — e.g. a scene with no clean
    // scene-depth unbind), fall back to doing SSAO in this combined pass, UI-bleed guard included.
    bool ssaoActive = false;
    if (g_cfg.ssao && g_depth.supported && g_depth.texture && !g_depth.aoDoneThisFrame)
    {
        ssaoActive = true;
    }
    float ssaoP[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (ssaoActive)
    {
        ssaoP[0] = g_cfg.ssaoStrength;
        ssaoP[1] = g_cfg.ssaoRadius;
        ssaoP[2] = g_cfg.ssaoMinDelta;
        ssaoP[3] = g_cfg.ssaoMaxDelta;
        dev->SetTexture(1, g_depth.texture);
        dev->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        dev->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        dev->SetSamplerState(1, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetSamplerState(1, D3DSAMP_ADDRESSU,  D3DTADDRESS_CLAMP);
        dev->SetSamplerState(1, D3DSAMP_ADDRESSV,  D3DTADDRESS_CLAMP);
        static bool s_loggedSSAOActive = false;
        if (!s_loggedSSAOActive)
        {
            s_loggedSSAOActive = true;
            Log("SSAO: first-frame active, depthTex=%p (%ux%u)\n",
                (void*)g_depth.texture, g_depth.width, g_depth.height);
        }
    }
    dev->SetPixelShaderConstantF(3, ssaoP, 1);
    // c4: screen-UV -> depth-UV scale. Identity until the game has bound the cached depth at
    // least once (g_depth.targetWidth measured in SetDepthStencilSurface); identity also covers
    // the common case where depth and back buffer have the same size.
    float dsc[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
    if (ssaoActive && g_depth.width && g_depth.height && g_depth.targetWidth && g_depth.targetHeight)
    {
        float sx = (float)g_depth.targetWidth / (float)g_depth.width;
        float sy = (float)g_depth.targetHeight / (float)g_depth.height;
        if (sx <= 1.0f && sy <= 1.0f)
        {
            dsc[0] = sx;
            dsc[1] = sy;
        }
    }
    dev->SetPixelShaderConstantF(4, dsc, 1);

    // c5: light-pass composite strengths. RenderLighting() set s_frameBloomStr / s_frameRayStr
    // (0 when that effect is off or, for god rays, when no bright source was on screen this frame).
    float lightP[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (s_lightingReady && (s_frameBloomStr > 0.0f || s_frameRayStr > 0.0f))
    {
        lightP[0] = s_frameBloomStr;
        lightP[1] = s_frameRayStr;
        dev->SetTexture(2, s_blur1Tex);
        SetLinearClamp(dev, 2);
        dev->SetTexture(3, s_rayTex);
        SetLinearClamp(dev, 3);
    }
    dev->SetPixelShaderConstantF(5, lightP, 1);

    // Full-screen quad with D3D9 half-pixel offset
    struct V { float x, y, z, w, u, v; };
    float W = (float)s_fxaaW, H = (float)s_fxaaH;
    V q[4] = {
        { -0.5f,    -0.5f,    0, 1,  0, 0 },
        { W-0.5f,   -0.5f,    0, 1,  1, 0 },
        { -0.5f,    H-0.5f,   0, 1,  0, 1 },
        { W-0.5f,   H-0.5f,   0, 1,  1, 1 },
    };
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V));

    pSB->Apply();
    pSB->Release();
}

void RunPostEffects(IDirect3DDevice9* dev)
{
    if (!g_cfg.fxaa) return;
    if (s_fxaaInitFailed) return;
    IDirect3DSurface9* pBB = nullptr;
    if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBB)))
    {
        D3DSURFACE_DESC desc;
        pBB->GetDesc(&desc);
        if (s_fxaaW != desc.Width || s_fxaaH != desc.Height || !s_fxaaPS)
        {
            if (!InitFXAA(dev, desc.Width, desc.Height, desc.Format))
            {
                s_fxaaInitFailed = true;
                pBB->Release();
                return;
            }
        }
        if (s_fxaaPS)
            ApplyFXAA(dev, pBB);
        pBB->Release();
    }
}
