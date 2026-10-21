// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// Pixel shader (ps_4_0), compiled with winmm.dll by the FxCompile step of build/accio-winmm.vcxproj.
//
// The overlay's panel and graph in HP1 and HP2: the vertex colour as it is (overlay11_vs.hlsl).
struct Pixel
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float4 colour : COLOR0;
};

float4 main(Pixel p) : SV_Target
{
    return p.colour;
}
