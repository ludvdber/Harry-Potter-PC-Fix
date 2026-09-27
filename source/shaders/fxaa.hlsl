// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// Pixel shader (ps_3_0), compiled with the DLL by the FxCompile step of build/accio-fix.vcxproj.
//
// FXAA + adaptive sharpening + color grading + screen-space AO pixel shader.
// Runs on the final frame, single combined pass for performance.
// Constants: c0 = (rcpFrameX, rcpFrameY, frameW, frameH)
//            c1 = (lift, gamma, gain, vibrance)
//            c2 = (vignetteStrength, sharpness, temperature, tint)
//            c3 = (ssaoStrength, ssaoRadiusPx, ssaoMinDelta, ssaoMaxDelta)
//            c4 = (depthUVScaleX, depthUVScaleY, _, _) — maps screen UVs onto the depth
//                 texture when it is larger than the back buffer (post-Reset window-sized depth)
//            c5 = (bloomStrength, godRayStrength, _, _) — 0 disables that effect's composite
//            c6 = (contrast, splitToneStrength, skinProtect, yellowRestraint) — S-curve contrast; teal/orange split
//                 toning; how much of both contrast and vibrance skin tones are spared; yellows held back
// Samplers: s0 = scene color, s1 = depth (INTZ when available), s2 = bloom, s3 = god rays
sampler2D scene    : register(s0);
sampler2D depthTex : register(s1);
sampler2D bloomTex : register(s2);
sampler2D rayTex   : register(s3);
float4 rcpFrame    : register(c0);
float4 grade1      : register(c1);
float4 grade2      : register(c2);
float4 ssaoP       : register(c3);
float4 dScale      : register(c4);
float4 lightP      : register(c5);
float4 gradeC      : register(c6);
float luma(float3 c){return dot(c,float3(0.299,0.587,0.114));}
float ssaoFactor(float2 uv, float lumaRange){
    if (ssaoP.x < 0.001) return 1.0;
    // UI-bleed protection: the game draws transparent UI without writing depth, so the depth
    // buffer under a dialog still holds 3D scene depth. Without this guard, AO would bleed
    // background object silhouettes through the UI as gray outlines. Real 3D surfaces have
    // texture / lighting variance at 1px radius; flat UI overlays don't.
    if (lumaRange < 0.01) return 1.0;
    float zC = tex2D(depthTex, uv * dScale.xy).r;
    if (zC > 0.9995) return 1.0;
    // Perspective Z is non-linear: a 50 cm contact distance produces a delta of ~0.003 near
    // the camera and ~0.00002 at far distance. Scaling thresholds by (1 - zC) approximates the
    // perspective compression, so the same MinDelta/MaxDelta ini values stay meaningful at
    // every distance (no halos at near silhouettes, no missed AO at far walls).
    float depthScale = max(1.0 - zC, 0.001);
    float minD = ssaoP.z * depthScale;
    float maxD = ssaoP.w * depthScale;
    // 12-tap golden-angle spiral over the whole sampling disk (radii sqrt-spaced for even
    // area coverage, outermost tap = ssaoRadiusPx), rotated per pixel by interleaved
    // gradient noise. A static kernel repeats the same pattern on every pixel, which shows
    // up as banding/ringing around objects; per-pixel rotation converts that error into
    // high-frequency noise the eye ignores (and the FXAA pass in this same shader smooths).
    // Disk coverage (vs the old single-radius ring) gives the AO volume in corners instead
    // of a thin contact line.
    static const float2 off[12] = {
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
    return saturate(1.0 - (occ / 12.0) * ssaoP.x);
}
float3 grade(float3 col,float2 uv){
    col = saturate(col * grade1.z + grade1.x);
    col = pow(max(col, 1e-5), grade1.y);
    // White balance: temperature (grade2.z, + = warmer: more red / less blue) and tint
    // (grade2.w, + = magenta: less green). Cheap channel scaling, then re-normalise luma so the
    // shift only re-tints rather than brightening/darkening the image.
    float lumPre = luma(col);
    col *= float3(1.0 + 0.15*grade2.z, 1.0 - 0.10*grade2.w, 1.0 - 0.15*grade2.z);
    float lumPost = max(luma(col), 1e-4);
    col *= lumPre / lumPost;
    col = saturate(col);
    // Skin tones (gradeC.z = SkinProtect): faces are warm and only moderately saturated, exactly
    // what vibrance pushes hardest — Harry's face came out orange on HP6 (Ludo, 2026-09-27).
    // Weight: red highest, hue about 10 to 45 degrees, mid brightness (dark warm wood stays out).
    float mx0 = max(col.r, max(col.g, col.b));
    float sat0 = mx0 - min(col.r, min(col.g, col.b));
    float hue = (col.g - col.b) / max(sat0, 1e-4);   // 0..1 = 0..60 degrees when red is highest
    float skin = step(max(col.g, col.b), col.r) * saturate(sat0 * 10.0)
               * smoothstep(0.10, 0.25, hue) * (1.0 - smoothstep(0.70, 0.90, hue))
               * smoothstep(0.15, 0.30, luma(col));
    float spare = 1.0 - gradeC.z * skin;
    // Contrast S-curve (gradeC.x): blend toward smoothstep, which deepens shadows AND lifts
    // highlights around the 0.5 pivot — adds depth/pop to HP5's flat midtones without crushing.
    col = lerp(col, col*col*(3.0 - 2.0*col), gradeC.x * spare);
    float lum = luma(col);
    float mx = max(col.r, max(col.g, col.b));
    float mn = min(col.r, min(col.g, col.b));
    float sat = mx - mn;
    // Yellow restraint (gradeC.w): vibrance swelled HP5's already yellow stone and grass into bright
    // yellow (Ludo, 2026-09-27). Weight 1 on pure yellow (red and green close, blue lowest), 0.25 on
    // orange: those hues lose the vibrance and a fifth of their saturation, every other hue keeps it.
    float yel = saturate(1.0 - abs(col.r - col.g) / max(sat, 1e-4) * 1.5) * step(col.b, min(col.r, col.g))
              * saturate(sat * 8.0);
    float vib = grade1.w * spare * (1.0 - sat);
    col = lerp(lum.xxx, col, 1.0 + vib - gradeC.w * yel * (vib + 0.2));
    // Split toning (gradeC.y = strength): push shadows toward cool teal and highlights toward warm
    // orange — the classic cinematic "teal & orange". Tints are roughly luma-neutral (a positive
    // channel paired with a negative one) so they re-colour rather than brighten. Midtones (0.45..
    // 0.55) get neither weight, so faces/mid-greys stay natural.
    float lSplit = luma(col);
    float3 shTint = float3(-0.10, 0.04, 0.16);
    float3 hiTint = float3( 0.16, 0.05, -0.10);
    float shW = saturate(1.0 - lSplit * 1.8);
    float hiW = saturate((lSplit - 0.45) * 1.8);
    col = saturate(col + (shTint*shW + hiTint*hiW) * gradeC.y);
    float2 d = uv - 0.5;
    float vig = 1.0 - saturate(dot(d, d) * 4.0 * grade2.x);
    col *= vig;
    return col;
}
float4 main(float2 uv:TEXCOORD0):COLOR0{
    float2 o=rcpFrame.xy;
    float3 nw=tex2D(scene,uv+float2(-o.x,-o.y)).rgb;
    float3 ne=tex2D(scene,uv+float2( o.x,-o.y)).rgb;
    float3 sw=tex2D(scene,uv+float2(-o.x, o.y)).rgb;
    float3 se=tex2D(scene,uv+float2( o.x, o.y)).rgb;
    float3 m =tex2D(scene,uv).rgb;
    float3 avg=(nw+ne+sw+se)*0.25;
    float lNW=luma(nw),lNE=luma(ne),lSW=luma(sw),lSE=luma(se),lM=luma(m);
    float lMin=min(lM,min(min(lNW,lNE),min(lSW,lSE)));
    float lMax=max(lM,max(max(lNW,lNE),max(lSW,lSE)));
    float range = lMax - lMin;
    float thresh = max(0.0625, lMax*0.125);
    // --- sharpen candidate (flat-detail enhancement, strength = grade2.y from ini Sharpness) ---
    // Overshoot-limited unsharp mask. Plain (m-avg)*amount paints a bright/dark 1px fringe at
    // medium-contrast edges (the "extra pixel on the width" the user reported); clamping the
    // result to the local neighbourhood +/- a small slack keeps real sharpening but forbids the
    // fringe from exceeding what the surrounding pixels already span.
    float3 nbHi = max(max(nw,ne),max(sw,se));
    float3 nbLo = min(min(nw,ne),min(sw,se));
    float3 sharp = clamp(m + (m-avg)*grade2.y, nbLo - 0.05, nbHi + 0.05);
    // --- FXAA candidate ---
    float2 dir=float2(-((lNW+lNE)-(lSW+lSE)),(lNW+lSW)-(lNE+lSE));
    float rcp=1.0/(min(abs(dir.x),abs(dir.y))+max((lNW+lNE+lSW+lSE)*0.03125,0.0078125));
    dir=clamp(dir*rcp,-12,12)*o;
    float3 A=0.5*(tex2D(scene,uv+dir*(1.0/3.0-0.5)).rgb+tex2D(scene,uv+dir*(2.0/3.0-0.5)).rgb);
    float3 B=A*0.5+0.25*(tex2D(scene,uv-dir*0.5).rgb+tex2D(scene,uv+dir*0.5).rgb);
    float lB=luma(B);
    float3 fxaa = (lB<lMin||lB>lMax)?A:B;
    // --- smooth blend instead of a hard flat/edge branch ---
    // The old binary `if(range<thresh)` made pixels sitting on the threshold flip between
    // sharpened and FXAA'd from frame to frame as scene luma changed (e.g. a flickering torch),
    // so the edge fringe appeared to crawl even with a still camera. A smoothstep transition
    // removes the toggle: which path a pixel takes no longer flips on a tiny luma change.
    float edge = smoothstep(thresh*0.6, thresh*1.4, range);
    float3 result = lerp(sharp, fxaa, edge);
    result *= ssaoFactor(uv, range);
    // Additive composite of the light passes (bloom + god rays). Both buffers are pre-blurred
    // half-res, sampled bilinear; the branch is skipped (no texture fetch) when both strengths
    // are 0, so a build with the lighting pass off pays nothing and never reads s2/s3.
    if (lightP.x + lightP.y > 0.0001) {
        result += tex2D(bloomTex, uv).rgb * lightP.x + tex2D(rayTex, uv).rgb * lightP.y;
    }
    return float4(grade(result, uv), 1);
}
