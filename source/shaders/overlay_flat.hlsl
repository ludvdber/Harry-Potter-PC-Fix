// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// Pixel shader (ps_3_0), compiled with the DLL by the FxCompile step of build/accio-fix.vcxproj.
//
// Overlay panel and graph with Pipeline=1: the vertex colour as it is (see overlay_text.hlsl).
float4 main(float4 colour : COLOR0) : COLOR0
{
    return colour;
}
