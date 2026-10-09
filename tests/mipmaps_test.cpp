// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
// Checks the mipmap filter and the pixel formats of mipmaps.cpp without a game or a GPU: each
// format round trip, the DXT encoders against their decoders, and the filter's light averaging
// (a black and white checker must fade to the grey of half the light, 188, not to 128).
#include "../source/mipmaps.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static int failures = 0;
static void Expect(bool ok, const char* what, const char* detail = "")
{
	if (!ok)
		printf("FAIL %s %s\n", what, detail);
	failures += !ok;
}

using mips::Rgba;

static const D3DFORMAT kDXT1 = static_cast<D3DFORMAT>(MAKEFOURCC('D', 'X', 'T', '1'));
static const D3DFORMAT kDXT3 = static_cast<D3DFORMAT>(MAKEFOURCC('D', 'X', 'T', '3'));
static const D3DFORMAT kDXT5 = static_cast<D3DFORMAT>(MAKEFOURCC('D', 'X', 'T', '5'));

// A smooth picture with detail: what real textures look like to a block encoder.
static std::vector<Rgba> Picture(int w, int h, bool alpha)
{
	std::vector<Rgba> p(static_cast<size_t>(w) * h);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			const double fx = x / double(w), fy = y / double(h);
			p[static_cast<size_t>(y) * w + x] = {
				uint8_t(128 + 100 * std::sin(fx * 6.3)), uint8_t(40 + 180 * fy), uint8_t(200 - 150 * fx * fy),
				uint8_t(alpha ? 255 * fx : 255) };
		}
	return p;
}

static double Rmse(const std::vector<Rgba>& a, const std::vector<Rgba>& b, bool alpha)
{
	double sum = 0;
	for (size_t i = 0; i < a.size(); i++)
	{
		const int d[4] = { a[i].r - b[i].r, a[i].g - b[i].g, a[i].b - b[i].b, alpha ? a[i].a - b[i].a : 0 };
		for (int c : d)
			sum += c * c;
	}
	return std::sqrt(sum / (a.size() * (alpha ? 4 : 3)));
}

static double RoundTrip(D3DFORMAT f, const std::vector<Rgba>& in, int w, int h, std::vector<Rgba>& out)
{
	std::vector<uint8_t> bits(static_cast<size_t>(mips::Pitch(f, w)) * mips::Rows(f, h) + 16, 0xCD);
	if (!mips::Encode(f, in, w, h, bits.data(), mips::Pitch(f, w)) || !mips::Decode(f, bits.data(), mips::Pitch(f, w), w, h, out))
		return 1e9;
	return Rmse(in, out, true);
}

int main()
{
	char detail[128];

	// Every supported format keeps a picture within its own precision.
	struct Case { D3DFORMAT f; const char* name; double tolerance; bool alpha; };
	const Case cases[] = {
		{ D3DFMT_A8R8G8B8, "A8R8G8B8", 0.01, true }, { D3DFMT_X8R8G8B8, "X8R8G8B8", 0.01, false },
		{ D3DFMT_A8B8G8R8, "A8B8G8R8", 0.01, true }, { D3DFMT_R5G6B5, "R5G6B5", 3.5, false },
		{ D3DFMT_A1R5G5B5, "A1R5G5B5", 3.5, false }, { D3DFMT_A4R4G4B4, "A4R4G4B4", 9.0, true },
		{ kDXT1, "DXT1", 9.0, false }, { kDXT3, "DXT3", 10.0, true }, { kDXT5, "DXT5", 9.0, true },
	};
	for (const Case& c : cases)
	{
		const int w = 64, h = 32;
		const auto in = Picture(w, h, c.alpha);
		std::vector<Rgba> out;
		const double e = RoundTrip(c.f, in, w, h, out);
		sprintf_s(detail, "RMSE %.2f (limit %.1f)", e, c.tolerance);
		Expect(e <= c.tolerance, c.name, detail);
		printf("%-9s round trip RMSE %.2f\n", c.name, e);
	}

	// L8, A8 and A8L8 carry one or two channels.
	{
		std::vector<Rgba> in(16), out;
		for (int i = 0; i < 16; i++)
			in[i] = { uint8_t(i * 16), uint8_t(i * 16), uint8_t(i * 16), uint8_t(255 - i * 16) };
		std::vector<uint8_t> bits(64);
		Expect(mips::Encode(D3DFMT_A8L8, in, 4, 4, bits.data(), 8) && mips::Decode(D3DFMT_A8L8, bits.data(), 8, 4, 4, out)
			&& Rmse(in, out, true) == 0, "A8L8 exact");
		Expect(mips::Encode(D3DFMT_L8, in, 4, 4, bits.data(), 4) && mips::Decode(D3DFMT_L8, bits.data(), 4, 4, 4, out)
			&& out[5].r == in[5].r && out[5].a == 255, "L8 exact");
		Expect(mips::Encode(D3DFMT_A8, in, 4, 4, bits.data(), 4) && mips::Decode(D3DFMT_A8, bits.data(), 4, 4, 4, out)
			&& out[5].a == in[5].a, "A8 exact");
	}

	// DXT1 cut-outs: what is transparent stays transparent, what is opaque stays opaque.
	{
		std::vector<Rgba> in(64);
		for (int i = 0; i < 64; i++)
			in[i] = { 200, uint8_t(i * 4), 30, uint8_t((i % 8) < 3 ? 0 : 255) };
		std::vector<Rgba> out;
		RoundTrip(kDXT1, in, 8, 8, out);
		int wrong = 0;
		for (int i = 0; i < 64; i++)
			wrong += (in[i].a < 128) != (out[i].a < 128);
		sprintf_s(detail, "%d texels changed side", wrong);
		Expect(wrong == 0, "DXT1 transparency", detail);
	}

	// One flat colour per block, the case a careless encoder breaks (equal endpoints).
	for (D3DFORMAT f : { kDXT1, kDXT3, kDXT5 })
	{
		std::vector<Rgba> in(16, Rgba{ 90, 160, 220, 255 }), out;
		sprintf_s(detail, "format %d", static_cast<int>(f));
		Expect(RoundTrip(f, in, 4, 4, out) < 4.0 && out[7].a == 255, "flat block", detail);
	}

	// Levels smaller than a block (2x2, 1x1) are written and read back.
	{
		// Four greys lie on one line of colour: a block holds them all.
		std::vector<Rgba> in = { { 0, 0, 0, 255 }, { 85, 85, 85, 200 }, { 170, 170, 170, 100 }, { 255, 255, 255, 0 } }, out;
		Expect(RoundTrip(kDXT5, in, 2, 2, out) < 6.0 && out.size() == 4, "DXT5 2x2 level");
		std::vector<Rgba> one = { { 10, 20, 30, 40 } };
		Expect(RoundTrip(kDXT5, one, 1, 1, out) < 4.0, "DXT5 1x1 level");
	}

	// The filter averages light: a fine black and white checker becomes the grey of half the light.
	{
		std::vector<Rgba> checker(16);
		for (int i = 0; i < 16; i++)
		{
			const uint8_t v = ((i & 1) ^ ((i >> 2) & 1)) ? 255 : 0;
			checker[i] = { v, v, v, 255 };
		}
		std::vector<Rgba> half;
		mips::Downsample(checker, 4, 4, half, 2, 2);
		sprintf_s(detail, "got %d", half[0].r);
		Expect(std::abs(half[0].r - 188) <= 1, "light-averaged grey", detail);
		Expect(half[0].a == 255, "alpha kept");
	}

	// A flat picture stays exactly flat through every level, odd sizes included.
	{
		int w = 37, h = 12;
		std::vector<Rgba> cur(static_cast<size_t>(w) * h, Rgba{ 77, 140, 201, 99 }), next;
		bool flat = true;
		while (w > 1 || h > 1)
		{
			const int dw = w > 1 ? w / 2 : 1, dh = h > 1 ? h / 2 : 1;
			mips::Downsample(cur, w, h, next, dw, dh);
			for (const Rgba& p : next)
				flat &= p.r == 77 && p.g == 140 && p.b == 201 && p.a == 99;
			cur.swap(next);
			w = dw;
			h = dh;
		}
		Expect(flat, "flat stays flat down to 1x1");
	}

	// Edges wrap: a stripe on the last column bleeds into the first texel, not nowhere.
	{
		std::vector<Rgba> in(8 * 1, Rgba{ 0, 0, 0, 255 });
		in[7] = { 255, 255, 255, 255 };
		std::vector<Rgba> out;
		mips::Downsample(in, 8, 1, out, 4, 1);
		Expect(out[0].r > 0 && out[3].r > out[0].r, "wrap at the edges");
	}

	// The sharp filter: flat stays flat (its weights sum to one despite the negative lobes), and
	// the light average holds.
	{
		int w = 37, h = 12;
		std::vector<Rgba> cur(static_cast<size_t>(w) * h, Rgba{ 77, 140, 201, 99 }), next;
		bool flat = true;
		while (w > 1 || h > 1)
		{
			const int dw = w > 1 ? w / 2 : 1, dh = h > 1 ? h / 2 : 1;
			mips::Downsample(cur, w, h, next, dw, dh, mips::Filter::Sharp);
			for (const Rgba& p : next)
				flat &= p.r == 77 && p.g == 140 && p.b == 201 && p.a == 99;
			cur.swap(next);
			w = dw;
			h = dh;
		}
		Expect(flat, "sharp: flat stays flat down to 1x1");

		std::vector<Rgba> checker(64 * 64), half;
		for (int i = 0; i < 64 * 64; i++)
		{
			const uint8_t v = ((i & 1) ^ ((i >> 6) & 1)) ? 255 : 0;
			checker[i] = { v, v, v, 255 };
		}
		mips::Downsample(checker, 64, 64, half, 32, 32, mips::Filter::Sharp);
		sprintf_s(detail, "got %d", half[33].r);
		Expect(std::abs(half[33].r - 188) <= 2, "sharp: light-averaged grey", detail);
	}

	// ... and keeps more of a detail the tent smooths away: stripes four texels wide, halved
	// twice, keep more of their contrast.
	{
		const int w = 64, h = 4;
		std::vector<Rgba> in(static_cast<size_t>(w) * h);
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
			{
				const uint8_t v = (x / 4) % 2 ? 200 : 40;
				in[static_cast<size_t>(y) * w + x] = { v, v, v, 255 };
			}
		auto contrast = [&](mips::Filter f) {
			std::vector<Rgba> a, b;
			mips::Downsample(in, w, h, a, w / 2, h / 2, f);
			mips::Downsample(a, w / 2, h / 2, b, w / 4, h / 4, f);
			int mn = 255, mx = 0;
			for (const Rgba& p : b)
			{
				mn = std::min<int>(mn, p.r);
				mx = std::max<int>(mx, p.r);
			}
			return mx - mn;
		};
		const int tent = contrast(mips::Filter::Tent), sharp = contrast(mips::Filter::Sharp);
		sprintf_s(detail, "tent %d, sharp %d", tent, sharp);
		Expect(sharp > tent + 20, "sharp keeps detail", detail);
		printf("stripes after two levels: contrast %s\n", detail);
	}

	// Cut-outs: a branch of thin leaves loses its leaves as it shrinks; with the coverage kept, the
	// share of texels the alpha test lets through stays that of the full-size texture.
	{
		const int w = 128, h = 128;
		// Small leaves of every size scattered over the texture, as a branch is drawn.
		std::vector<Rgba> leaves(static_cast<size_t>(w) * h, Rgba{ 40, 120, 30, 0 });
		unsigned seed = 12345;
		auto rnd = [&] { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7FFF; };
		for (int n = 0; n < 260; n++)
		{
			const int cx = rnd() % w, cy = rnd() % h, r = 1 + rnd() % 3;
			for (int y = -r; y <= r; y++)
				for (int x = -r; x <= r; x++)
					if (x * x + y * y <= r * r)
						leaves[static_cast<size_t>((cy + y + h) % h) * w + (cx + x + w) % w].a = 255;
		}
		Expect(mips::IsCutout(leaves), "leaves recognised as a cut-out");
		const float full = mips::Coverage(leaves, 128);
		std::vector<Rgba> cur = leaves, next;
		int cw = w, ch = h;
		float plain = 0, kept = 0;
		for (int level = 1; level <= 3; level++)
		{
			mips::Downsample(cur, cw, ch, next, cw / 2, ch / 2);
			cw /= 2;
			ch /= 2;
			std::vector<Rgba> k = next;
			mips::KeepCoverage(k, full, 128);
			plain = mips::Coverage(next, 128);
			kept = mips::Coverage(k, 128);
			cur.swap(next);
		}
		sprintf_s(detail, "full %.3f, 16x16 plain %.3f, kept %.3f", full, plain, kept);
		Expect(std::fabs(kept - full) < 0.03f && std::fabs(plain - full) > 0.05f, "coverage kept", detail);
		printf("leaf coverage: %s\n", detail);

		// Glass or smoke (alpha that blends, not cut) and an opaque texture are left alone.
		std::vector<Rgba> glass(64);
		for (int i = 0; i < 64; i++)
			glass[i] = { 200, 200, 255, uint8_t(60 + i) };
		std::vector<Rgba> opaque(64, Rgba{ 1, 2, 3, 255 });
		Expect(!mips::IsCutout(glass) && !mips::IsCutout(opaque), "only cut-outs");
	}

	Expect(!mips::Supported(D3DFMT_P8) && !mips::Supported(D3DFMT_A2R10G10B10), "unsupported formats refused");
	Expect(mips::Pitch(kDXT1, 2) == 8 && mips::Rows(kDXT5, 1) == 1 && mips::Pitch(D3DFMT_R5G6B5, 10) == 20, "pitch");

	printf(failures ? "%d FAILED\n" : "All mipmap checks passed\n", failures);
	return failures ? 1 : 0;
}
