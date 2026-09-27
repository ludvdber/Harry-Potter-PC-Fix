// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// Pixel shader (ps_3_0), compiled with the DLL by the FxCompile step of build/accio-fix.vcxproj.
//
// Standalone SSAO shader for the "AO at depth-unbind" pass (option B). Identical occlusion math
// to the combined shader's ssaoFactor, but it runs on the pure 3D scene the instant the game
// unbinds scene depth (before any UI is drawn), so it needs NO luma-variance UI-bleed guard —
// there is no UI in the buffer yet. Output is sceneColor * ao; FXAA + grading still run at Present.
// Constants/samplers match the combined shader: s0 scene, s1 depth, c0 rcp+dims, c3 ssao, c4 uvscale.
sampler2D scene    : register(s0);
sampler2D depthTex : register(s1);
float4 rcpFrame    : register(c0);
float4 ssaoP       : register(c3);
float4 dScale      : register(c4);
float4 main(float2 uv:TEXCOORD0):COLOR0{
    float3 col = tex2D(scene, uv).rgb;
    if (ssaoP.x < 0.001) return float4(col, 1);
    float zC = tex2D(depthTex, uv * dScale.xy).r;
    if (zC > 0.9995) return float4(col, 1);
    float depthScale = max(1.0 - zC, 0.001);
    float minD = ssaoP.z * depthScale;
    float maxD = ssaoP.w * depthScale;
    float2 off[12] = {
        float2( 0.2887,  0.0000), float2(-0.3010,  0.2758), float2( 0.0437, -0.4981),
        float2( 0.3514,  0.4582), float2(-0.6356, -0.1124), float2( 0.5967, -0.3795),
        float2(-0.1986,  0.7375), float2(-0.3750, -0.7253), float2( 0.8134,  0.2974),
        float2(-0.8438,  0.3485), float2( 0.4045, -0.8677), float2( 0.2997,  0.9540)
    };
    float2 px = uv * rcpFrame.zw;
    float ang = 6.2831853 * frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715))));
    float sn, cs;
    sincos(ang, sn, cs);
    float occ = 0.0;
    // Unrolled, as the run-time D3DX compiler already did: the depth reads need gradients.
    [unroll] for (int i=0; i<12; i++) {
        float2 r = float2(off[i].x*cs - off[i].y*sn, off[i].x*sn + off[i].y*cs);
        float2 sUV = (uv + r * rcpFrame.xy * ssaoP.y) * dScale.xy;
        float zS = tex2D(depthTex, sUV).r;
        float d = zC - zS;
        if (d > minD && d < maxD) occ += 1.0;
    }
    float ao = saturate(1.0 - (occ / 12.0) * ssaoP.x);
    return float4(col * ao, 1);
}
