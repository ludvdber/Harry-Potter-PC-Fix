// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// Mipmap chains for the textures the games ship without them. Pure pixel work (decode, filter,
// encode) is separate from Direct3D so that tests/mipmaps_test.cpp checks it without a device.

#pragma once
#ifndef NOMINMAX
#define NOMINMAX   // std::min / std::max, not the macros of windows.h
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d9.h>
#include <cstdint>
#include <vector>

namespace mips
{
struct Rgba { uint8_t r, g, b, a; };

// Formats whose levels we can read and write. Anything else gets no chain at all: the game then
// looks exactly as it does without the fix, instead of sampling empty lower levels.
bool Supported(D3DFORMAT format);

// Bytes per row of blocks (DXT) or of pixels, and number of rows, for a w x h level.
int Pitch(D3DFORMAT format, int w);
int Rows(D3DFORMAT format, int h);

// One level <-> RGBA, row after row. `pitch` is the distance between rows (or rows of 4x4
// blocks) in `bits`. Encoding dithers 16-bit formats and picks DXT endpoints per block.
bool Decode(D3DFORMAT format, const void* bits, int pitch, int w, int h, std::vector<Rgba>& out);
bool Encode(D3DFORMAT format, const std::vector<Rgba>& in, int w, int h, void* bits, int pitch);

// Tent: a triangle filter, soft (what the fix has always used). Sharp: a windowed sinc (Kaiser
// window, three levels wide), which keeps the detail a tent blurs away in the distance.
enum class Filter { Tent, Sharp };

// The next level: colours averaged as light (sRGB decoded), alpha averaged as is; texture edges
// wrap, as games tile most of their textures.
void Downsample(const std::vector<Rgba>& src, int w, int h, std::vector<Rgba>& dst, int dw, int dh,
	Filter filter = Filter::Tent);

// Cut-outs (leaves, hair, fences): alpha almost only fully in or fully out. Averaged, their edges
// turn to half alpha, which the alpha test drops: trees thin out and vanish with distance.
// `Coverage` is the share of texels at or above `ref`; `KeepCoverage` scales a level's alpha until
// that share matches the full-size texture's again.
bool IsCutout(const std::vector<Rgba>& p);
float Coverage(const std::vector<Rgba>& p, uint8_t ref);
void KeepCoverage(std::vector<Rgba>& p, float target, uint8_t ref);

// Fills levels 1..n of `tex` from level 0 with `filter`, and cut-outs keep their coverage when
// `keepCoverage`. False (and the lower levels untouched) on failure.
bool FillChain(IDirect3DTexture9* tex, Filter filter = Filter::Tent, bool keepCoverage = false);
}
