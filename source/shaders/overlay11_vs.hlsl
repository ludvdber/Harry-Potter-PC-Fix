// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// Vertex shader (vs_4_0), compiled with winmm.dll by the FxCompile step of build/accio-winmm.vcxproj.
//
// The overlay of HP1 and HP2, drawn in Direct3D 11 over the renderer's back buffer: the vertices
// come in clip space already (the panel is laid out in pixels on the CPU and converted there),
// with a colour and a glyph coordinate that go straight through.
struct Vertex
{
    float2 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 colour : COLOR0;
};

struct Pixel
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float4 colour : COLOR0;
};

Pixel main(Vertex v)
{
    Pixel p;
    p.pos = float4(v.pos, 0.0, 1.0);
    p.uv = v.uv;
    p.colour = v.colour;
    return p;
}
