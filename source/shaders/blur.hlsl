// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// Pixel shader (ps_3_0), compiled with the DLL by the FxCompile step of build/accio-fix.vcxproj.
//
// Separable 9-tap Gaussian. c0.xy = texel step along the blur axis (one axis non-zero per pass).
sampler2D src : register(s0);
float4 dir : register(c0);
float4 main(float2 uv:TEXCOORD0):COLOR0{
    float w0=0.227027, w1=0.1945946, w2=0.1216216, w3=0.054054, w4=0.016216;
    float3 c = tex2D(src, uv).rgb * w0;
    c += (tex2D(src, uv + dir.xy*1.0).rgb + tex2D(src, uv - dir.xy*1.0).rgb) * w1;
    c += (tex2D(src, uv + dir.xy*2.0).rgb + tex2D(src, uv - dir.xy*2.0).rgb) * w2;
    c += (tex2D(src, uv + dir.xy*3.0).rgb + tex2D(src, uv - dir.xy*3.0).rgb) * w3;
    c += (tex2D(src, uv + dir.xy*4.0).rgb + tex2D(src, uv - dir.xy*4.0).rgb) * w4;
    return float4(c, 1);
}
