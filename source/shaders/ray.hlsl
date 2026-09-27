// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// Pixel shader (ps_3_0), compiled with the DLL by the FxCompile step of build/accio-fix.vcxproj.
//
// God rays / crepuscular light shafts (GPU Gems 3 radial light scattering). Marches 32 samples
// from the pixel toward the detected light screen position, accumulating the bright-pass with a
// per-step decay. c0 = (lightU, lightV, decay, weight), c1 = (exposure, density, _, _).
sampler2D src : register(s0);
float4 lp : register(c0);
float4 ex : register(c1);
float4 main(float2 uv:TEXCOORD0):COLOR0{
    float2 delta = (uv - lp.xy) * (ex.y / 32.0);
    float2 c = uv;
    float3 col = 0.0;
    float illum = 1.0;
    for (int i=0;i<32;i++){
        c -= delta;
        col += tex2D(src, c).rgb * illum * lp.w;
        illum *= lp.z;
    }
    return float4(col * (ex.x / 32.0), 1);
}
