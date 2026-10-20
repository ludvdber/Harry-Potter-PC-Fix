// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// Pixel shader (ps_3_0), compiled with the DLL by the FxCompile step of build/accio-fix.vcxproj.
//
// Overlay text with Pipeline=1: the glyph texture (white, coverage in alpha) times the vertex
// colour. The same thing Direct3D's fixed pipeline does with MODULATE on stage 0, without the
// fixed pipeline: under Wine (DXVK) that pipeline is a generated shader nothing else in the
// frame uses, and the first panel drawn hung an Intel GPU (Bazzite, 2026-10-20).
sampler2D glyphs : register(s0);
float4 main(float4 colour : COLOR0, float2 uv : TEXCOORD0) : COLOR0
{
    return tex2D(glyphs, uv) * colour;
}
