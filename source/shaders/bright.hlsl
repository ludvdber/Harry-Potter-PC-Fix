// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// Pixel shader (ps_3_0), compiled with the DLL by the FxCompile step of build/accio-fix.vcxproj.
//
// ---- Light pass shaders (bloom + god rays), all run at half resolution ----------------------
// Bright-pass: isolate pixels brighter than the threshold, keeping their colour. Rendered into a
// half-res target (bilinear downsample from the full-res scene happens for free). c0.x = threshold.
sampler2D scene : register(s0);
float4 p : register(c0);
float4 main(float2 uv:TEXCOORD0):COLOR0{
    float3 c = tex2D(scene, uv).rgb;
    float l = dot(c, float3(0.299,0.587,0.114));
    float k = max(l - p.x, 0.0) / max(l, 1e-4);
    return float4(c * k, 1);
}
