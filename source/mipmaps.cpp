// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// Mipmap levels built by the fix itself. They used to come from D3DXFilterTexture, which tied
// the DLL to d3dx9_43.dll: a file Windows has never shipped, so on a fresh PC the game did not
// start at all. The result is the same kind of image: a triangle filter (or, in the ini, a sharp
// one, and cut-outs that keep their coverage), colours averaged as light
// rather than as sRGB numbers (a plain average darkens contrasted edges in the distance), 16-bit
// formats dithered, compressed textures (DXT1 to DXT5) decoded, filtered and encoded again.

#include "mipmaps.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace mips
{
namespace
{
constexpr D3DFORMAT kDXT1 = static_cast<D3DFORMAT>(MAKEFOURCC('D', 'X', 'T', '1'));
constexpr D3DFORMAT kDXT2 = static_cast<D3DFORMAT>(MAKEFOURCC('D', 'X', 'T', '2'));
constexpr D3DFORMAT kDXT3 = static_cast<D3DFORMAT>(MAKEFOURCC('D', 'X', 'T', '3'));
constexpr D3DFORMAT kDXT4 = static_cast<D3DFORMAT>(MAKEFOURCC('D', 'X', 'T', '4'));
constexpr D3DFORMAT kDXT5 = static_cast<D3DFORMAT>(MAKEFOURCC('D', 'X', 'T', '5'));

bool IsBlock(D3DFORMAT f) { return f == kDXT1 || f == kDXT2 || f == kDXT3 || f == kDXT4 || f == kDXT5; }
int BlockBytes(D3DFORMAT f) { return f == kDXT1 ? 8 : 16; }

int PixelBytes(D3DFORMAT f)
{
	switch (f)
	{
	case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: case D3DFMT_A8B8G8R8: case D3DFMT_X8B8G8R8: return 4;
	case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5: case D3DFMT_A4R4G4B4:
	case D3DFMT_X4R4G4B4: case D3DFMT_A8L8: return 2;
	case D3DFMT_L8: case D3DFMT_A8: return 1;
	default: return 0;
	}
}

// ---- Light and numbers ----------------------------------------------------------------------

float g_toLinear[256];
uint8_t g_toSrgb[4096];
bool g_tables = false;

void Tables()
{
	if (g_tables)
		return;
	for (int i = 0; i < 256; i++)
	{
		const float s = i / 255.0f;
		g_toLinear[i] = s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
	}
	for (int i = 0; i < 4096; i++)
	{
		const float l = i / 4095.0f;
		const float s = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
		g_toSrgb[i] = static_cast<uint8_t>(std::clamp(std::lround(s * 255.0f), 0L, 255L));
	}
	g_tables = true;
}

uint8_t ToSrgb(float linear) { return g_toSrgb[std::clamp(static_cast<int>(linear * 4095.0f + 0.5f), 0, 4095)]; }
uint8_t ToByte(float v) { return static_cast<uint8_t>(std::clamp(static_cast<int>(v * 255.0f + 0.5f), 0, 255)); }

// ---- 16-bit formats ---------------------------------------------------------------------------

uint8_t Expand(unsigned v, int bits) { return static_cast<uint8_t>((v << (8 - bits)) | (v >> (2 * bits - 8))); }

// Ordered dither: a 4x4 Bayer pattern spreads the rounding error of 5- and 4-bit channels, so
// the smooth gradients of far-away levels do not turn into bands.
const int kBayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

unsigned Quantise(uint8_t v, int bits, int x, int y)
{
	const int top = (1 << bits) - 1;
	const float scaled = v * top / 255.0f + (kBayer[y & 3][x & 3] - 7.5f) / 16.0f;
	return static_cast<unsigned>(std::clamp(static_cast<int>(std::floor(scaled + 0.5f)), 0, top));
}

// ---- DXT blocks -----------------------------------------------------------------------------

Rgba From565(uint16_t c) { return { Expand(c >> 11, 5), Expand((c >> 5) & 63, 6), Expand(c & 31, 5), 255 }; }

uint16_t To565(int r, int g, int b)
{
	r = std::clamp(r, 0, 255); g = std::clamp(g, 0, 255); b = std::clamp(b, 0, 255);
	return static_cast<uint16_t>(((r * 31 + 127) / 255) << 11 | ((g * 63 + 127) / 255) << 5 | ((b * 31 + 127) / 255));
}

// The four (or three + transparent) colours a colour block can hold.
void Palette(uint16_t c0, uint16_t c1, bool fourColours, Rgba out[4])
{
	const Rgba a = From565(c0), b = From565(c1);
	out[0] = a;
	out[1] = b;
	if (fourColours)
	{
		out[2] = { uint8_t((2 * a.r + b.r) / 3), uint8_t((2 * a.g + b.g) / 3), uint8_t((2 * a.b + b.b) / 3), 255 };
		out[3] = { uint8_t((a.r + 2 * b.r) / 3), uint8_t((a.g + 2 * b.g) / 3), uint8_t((a.b + 2 * b.b) / 3), 255 };
	}
	else
	{
		out[2] = { uint8_t((a.r + b.r) / 2), uint8_t((a.g + b.g) / 2), uint8_t((a.b + b.b) / 2), 255 };
		out[3] = { 0, 0, 0, 0 };
	}
}

void DecodeColour(const uint8_t* block, bool alwaysFour, Rgba texels[16])
{
	uint16_t c0, c1;
	uint32_t idx;
	memcpy(&c0, block, 2);
	memcpy(&c1, block + 2, 2);
	memcpy(&idx, block + 4, 4);
	Rgba pal[4];
	Palette(c0, c1, alwaysFour || c0 > c1, pal);
	for (int i = 0; i < 16; i++)
		texels[i] = pal[(idx >> (2 * i)) & 3];
}

int Distance(const Rgba& a, const Rgba& b)
{
	const int dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
	return dr * dr + dg * dg + db * db;
}

// Nearest palette entry for each texel (entries [0, count)); total squared error.
int Indices(const Rgba texels[16], const bool use[16], const Rgba pal[4], int count, int idx[16])
{
	int error = 0;
	for (int i = 0; i < 16; i++)
	{
		if (!use[i])
			continue;
		int best = 0, bestD = Distance(texels[i], pal[0]);
		for (int k = 1; k < count; k++)
		{
			const int d = Distance(texels[i], pal[k]);
			if (d < bestD) { best = k; bestD = d; }
		}
		idx[i] = best;
		error += bestD;
	}
	return error;
}

// Endpoints along the block's colour diagonal: the bounding box, oriented by the sign of how the
// channels vary together (a red-to-green gradient runs across the box, not along it), pulled in
// by 1/16 so that the end colours fall on real texels rather than beyond them.
void Endpoints(const Rgba texels[16], const bool use[16], int lo[3], int hi[3])
{
	int mn[3] = { 255, 255, 255 }, mx[3] = { 0, 0, 0 };
	float mean[3] = {}, n = 0;
	for (int i = 0; i < 16; i++)
	{
		if (!use[i])
			continue;
		const int c[3] = { texels[i].r, texels[i].g, texels[i].b };
		for (int k = 0; k < 3; k++)
		{
			mn[k] = std::min(mn[k], c[k]);
			mx[k] = std::max(mx[k], c[k]);
			mean[k] += c[k];
		}
		n++;
	}
	for (float& m : mean)
		m /= n;
	// The channel that varies most leads; the other two follow it or run against it.
	int lead = 0;
	for (int k = 1; k < 3; k++)
		if (mx[k] - mn[k] > mx[lead] - mn[lead])
			lead = k;
	float cov[3] = {};
	for (int i = 0; i < 16; i++)
	{
		if (!use[i])
			continue;
		const int c[3] = { texels[i].r, texels[i].g, texels[i].b };
		for (int k = 0; k < 3; k++)
			cov[k] += (c[lead] - mean[lead]) * (c[k] - mean[k]);
	}
	for (int k = 0; k < 3; k++)
	{
		const int inset = (mx[k] - mn[k]) / 16;
		lo[k] = mn[k] + inset;
		hi[k] = mx[k] - inset;
		if (cov[k] < 0)
			std::swap(lo[k], hi[k]);
	}
}

// One least-squares pass: the endpoints that best reproduce the texels with the indices chosen.
bool Refine(const Rgba texels[16], const bool use[16], const int idx[16], bool fourColours, int lo[3], int hi[3])
{
	static const float kT4[4] = { 0.0f, 1.0f, 1.0f / 3.0f, 2.0f / 3.0f };
	static const float kT3[4] = { 0.0f, 1.0f, 0.5f, 0.0f };
	float a = 0, b = 0, c = 0, r0[3] = {}, r1[3] = {};
	for (int i = 0; i < 16; i++)
	{
		if (!use[i])
			continue;
		const float t = fourColours ? kT4[idx[i]] : kT3[idx[i]];
		const float s = 1.0f - t;
		const float p[3] = { float(texels[i].r), float(texels[i].g), float(texels[i].b) };
		a += s * s; b += s * t; c += t * t;
		for (int k = 0; k < 3; k++) { r0[k] += s * p[k]; r1[k] += t * p[k]; }
	}
	const float det = a * c - b * b;
	if (std::fabs(det) < 1e-4f)
		return false;
	for (int k = 0; k < 3; k++)
	{
		lo[k] = static_cast<int>(std::lround((c * r0[k] - b * r1[k]) / det));
		hi[k] = static_cast<int>(std::lround((a * r1[k] - b * r0[k]) / det));
	}
	return true;
}

// A colour block. With `dxt1`, texels under half alpha become index 3 (transparent), which needs
// the three-colour mode; DXT2 to DXT5 always read their colour block in four-colour mode.
void EncodeColour(const Rgba texels[16], bool dxt1, uint8_t* out)
{
	bool use[16];
	bool transparent = false, any = false;
	for (int i = 0; i < 16; i++)
	{
		use[i] = !dxt1 || texels[i].a >= 128;
		transparent |= !use[i];
		any |= use[i];
	}
	const bool four = !transparent;
	uint16_t c0 = 0, c1 = 0;
	int idx[16] = {};
	if (any)
	{
		int lo[3], hi[3];
		Endpoints(texels, use, lo, hi);
		int bestError = -1;
		for (int pass = 0; pass < 2; pass++)
		{
			uint16_t a = To565(hi[0], hi[1], hi[2]), b = To565(lo[0], lo[1], lo[2]);
			// Four colours need c0 > c1, three need c0 <= c1: the order IS the mode.
			if (four ? a < b : a > b)
				std::swap(a, b);
			Rgba pal[4];
			Palette(a, b, four || a > b, pal);
			int cand[16] = {};
			const int error = Indices(texels, use, pal, four ? 4 : 3, cand);
			if (four && a == b)
				std::fill(cand, cand + 16, 0);   // one colour: equal endpoints read as three-colour mode
			if (bestError < 0 || error < bestError)
			{
				bestError = error;
				c0 = a;
				c1 = b;
				memcpy(idx, cand, sizeof(idx));
			}
			// Indices are chosen again from scratch on the next pass, so which refined endpoint
			// comes out as `lo` or `hi` does not matter: the pass above orders them for the mode.
			if (!Refine(texels, use, idx, four, lo, hi))
				break;
		}
	}
	uint32_t bits = 0;
	for (int i = 0; i < 16; i++)
		bits |= static_cast<uint32_t>(use[i] ? idx[i] : 3) << (2 * i);
	memcpy(out, &c0, 2);
	memcpy(out + 2, &c1, 2);
	memcpy(out + 4, &bits, 4);
}

void DecodeAlpha5(const uint8_t* block, uint8_t alpha[16])
{
	const int a0 = block[0], a1 = block[1];
	int pal[8] = { a0, a1 };
	if (a0 > a1)
		for (int i = 2; i < 8; i++)
			pal[i] = ((8 - i) * a0 + (i - 1) * a1) / 7;
	else
	{
		for (int i = 2; i < 6; i++)
			pal[i] = ((6 - i) * a0 + (i - 1) * a1) / 5;
		pal[6] = 0;
		pal[7] = 255;
	}
	uint64_t bits = 0;
	memcpy(&bits, block + 2, 6);
	for (int i = 0; i < 16; i++)
		alpha[i] = static_cast<uint8_t>(pal[(bits >> (3 * i)) & 7]);
}

void EncodeAlpha5(const Rgba texels[16], uint8_t* out)
{
	int mn = 255, mx = 0;
	for (int i = 0; i < 16; i++)
	{
		mn = std::min<int>(mn, texels[i].a);
		mx = std::max<int>(mx, texels[i].a);
	}
	int pal[8] = { mx, mn };
	for (int i = 2; i < 8; i++)
		pal[i] = ((8 - i) * mx + (i - 1) * mn) / 7;
	uint64_t bits = 0;
	if (mx != mn)
		for (int i = 0; i < 16; i++)
		{
			int best = 0;
			for (int k = 1; k < 8; k++)
				if (std::abs(pal[k] - texels[i].a) < std::abs(pal[best] - texels[i].a))
					best = k;
			bits |= static_cast<uint64_t>(best) << (3 * i);
		}
	out[0] = static_cast<uint8_t>(mx);
	out[1] = static_cast<uint8_t>(mn);
	memcpy(out + 2, &bits, 6);
}

// ---- Filtering --------------------------------------------------------------------------------

// The sharp filter: sinc windowed by Kaiser, three output texels on each side, alpha 4. A
// published recipe for mipmaps (sharper than a tent, far less ringing than a bare sinc).
constexpr float kSharpRadius = 3.0f;
constexpr float kKaiserAlpha = 4.0f;

// Modified Bessel function of the first kind, order 0 (series; converges fast for these values).
double BesselI0(double x)
{
	double sum = 1, term = 1;
	for (int k = 1; k < 32; k++)
	{
		term *= (x / (2 * k)) * (x / (2 * k));
		sum += term;
		if (term < sum * 1e-12)
			break;
	}
	return sum;
}

// Weight at distance t, in output texels.
float SharpWeight(float t)
{
	const float r = t / kSharpRadius;
	if (r <= -1.0f || r >= 1.0f)
		return 0.0f;
	const double window = BesselI0(kKaiserAlpha * std::sqrt(1.0 - double(r) * r)) / BesselI0(kKaiserAlpha);
	const double pt = 3.14159265358979 * t;
	const double sinc = std::fabs(pt) < 1e-6 ? 1.0 : std::sin(pt) / pt;
	return static_cast<float>(sinc * window);
}

// Weights for one output position: the source texels the filter reaches from its centre. The
// sharp filter has negative lobes; the weights still sum to one, so a flat picture stays flat.
void Taps(int x, int srcSize, float scale, Filter filter, std::vector<std::pair<int, float>>& taps)
{
	taps.clear();
	const float centre = (x + 0.5f) * scale;
	const float reach = filter == Filter::Sharp ? kSharpRadius * scale : scale;
	const int first = static_cast<int>(std::floor(centre - reach));
	const int last = static_cast<int>(std::ceil(centre + reach));
	float sum = 0;
	for (int i = first; i <= last; i++)
	{
		const float d = i + 0.5f - centre;
		const float w = filter == Filter::Sharp ? SharpWeight(d / scale) : scale - std::fabs(d);
		if (w == 0 || (filter == Filter::Tent && w < 0))
			continue;
		taps.push_back({ ((i % srcSize) + srcSize) % srcSize, w });
		sum += w;
	}
	for (auto& t : taps)
		t.second /= sum;
}
}

bool Supported(D3DFORMAT format) { return IsBlock(format) || PixelBytes(format) != 0; }

int Pitch(D3DFORMAT format, int w)
{
	return IsBlock(format) ? std::max(1, (w + 3) / 4) * BlockBytes(format) : w * PixelBytes(format);
}

int Rows(D3DFORMAT format, int h) { return IsBlock(format) ? std::max(1, (h + 3) / 4) : h; }

bool Decode(D3DFORMAT f, const void* bits, int pitch, int w, int h, std::vector<Rgba>& out)
{
	if (!Supported(f) || w <= 0 || h <= 0)
		return false;
	out.assign(static_cast<size_t>(w) * h, Rgba{});
	const auto* base = static_cast<const uint8_t*>(bits);
	if (IsBlock(f))
	{
		for (int by = 0; by < Rows(f, h); by++)
			for (int bx = 0; bx < (w + 3) / 4; bx++)
			{
				const uint8_t* block = base + by * pitch + bx * BlockBytes(f);
				Rgba t[16];
				if (f == kDXT1)
					DecodeColour(block, false, t);
				else
				{
					DecodeColour(block + 8, true, t);
					if (f == kDXT2 || f == kDXT3)
					{
						uint64_t a;
						memcpy(&a, block, 8);
						for (int i = 0; i < 16; i++)
							t[i].a = static_cast<uint8_t>(((a >> (4 * i)) & 15) * 17);
					}
					else
					{
						uint8_t a[16];
						DecodeAlpha5(block, a);
						for (int i = 0; i < 16; i++)
							t[i].a = a[i];
					}
				}
				for (int i = 0; i < 16; i++)
				{
					const int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
					if (x < w && y < h)
						out[static_cast<size_t>(y) * w + x] = t[i];
				}
			}
		return true;
	}
	for (int y = 0; y < h; y++)
	{
		const uint8_t* row = base + static_cast<size_t>(y) * pitch;
		for (int x = 0; x < w; x++)
		{
			Rgba& o = out[static_cast<size_t>(y) * w + x];
			uint16_t v16 = 0;
			if (PixelBytes(f) == 2)
				memcpy(&v16, row + x * 2, 2);
			switch (f)
			{
			case D3DFMT_A8R8G8B8: o = { row[x * 4 + 2], row[x * 4 + 1], row[x * 4], row[x * 4 + 3] }; break;
			case D3DFMT_X8R8G8B8: o = { row[x * 4 + 2], row[x * 4 + 1], row[x * 4], 255 }; break;
			case D3DFMT_A8B8G8R8: o = { row[x * 4], row[x * 4 + 1], row[x * 4 + 2], row[x * 4 + 3] }; break;
			case D3DFMT_X8B8G8R8: o = { row[x * 4], row[x * 4 + 1], row[x * 4 + 2], 255 }; break;
			case D3DFMT_R5G6B5: o = From565(v16); break;
			case D3DFMT_X1R5G5B5:
			case D3DFMT_A1R5G5B5:
				o = { Expand((v16 >> 10) & 31, 5), Expand((v16 >> 5) & 31, 5), Expand(v16 & 31, 5),
					uint8_t(f == D3DFMT_X1R5G5B5 || (v16 & 0x8000) ? 255 : 0) };
				break;
			case D3DFMT_A4R4G4B4:
			case D3DFMT_X4R4G4B4:
				o = { uint8_t(((v16 >> 8) & 15) * 17), uint8_t(((v16 >> 4) & 15) * 17), uint8_t((v16 & 15) * 17),
					uint8_t(f == D3DFMT_X4R4G4B4 ? 255 : (v16 >> 12) * 17) };
				break;
			case D3DFMT_A8L8: o = { uint8_t(v16), uint8_t(v16), uint8_t(v16), uint8_t(v16 >> 8) }; break;
			case D3DFMT_L8: o = { row[x], row[x], row[x], 255 }; break;
			case D3DFMT_A8: o = { 0, 0, 0, row[x] }; break;
			default: return false;
			}
		}
	}
	return true;
}

bool Encode(D3DFORMAT f, const std::vector<Rgba>& in, int w, int h, void* bits, int pitch)
{
	if (!Supported(f) || w <= 0 || h <= 0 || in.size() < static_cast<size_t>(w) * h)
		return false;
	auto* base = static_cast<uint8_t*>(bits);
	if (IsBlock(f))
	{
		for (int by = 0; by < Rows(f, h); by++)
			for (int bx = 0; bx < (w + 3) / 4; bx++)
			{
				Rgba t[16];
				for (int i = 0; i < 16; i++)
				{
					// A level smaller than a block repeats its last texels into the rest.
					const int x = std::min(bx * 4 + (i & 3), w - 1), y = std::min(by * 4 + (i >> 2), h - 1);
					t[i] = in[static_cast<size_t>(y) * w + x];
				}
				uint8_t* block = base + by * pitch + bx * BlockBytes(f);
				if (f == kDXT1)
				{
					EncodeColour(t, true, block);
					continue;
				}
				if (f == kDXT2 || f == kDXT3)
				{
					uint64_t a = 0;
					for (int i = 0; i < 16; i++)
						a |= static_cast<uint64_t>((t[i].a * 15 + 127) / 255) << (4 * i);
					memcpy(block, &a, 8);
				}
				else
					EncodeAlpha5(t, block);
				EncodeColour(t, false, block + 8);
			}
		return true;
	}
	for (int y = 0; y < h; y++)
	{
		uint8_t* row = base + static_cast<size_t>(y) * pitch;
		for (int x = 0; x < w; x++)
		{
			const Rgba& p = in[static_cast<size_t>(y) * w + x];
			uint16_t v16 = 0;
			switch (f)
			{
			case D3DFMT_A8R8G8B8: row[x * 4] = p.b; row[x * 4 + 1] = p.g; row[x * 4 + 2] = p.r; row[x * 4 + 3] = p.a; break;
			case D3DFMT_X8R8G8B8: row[x * 4] = p.b; row[x * 4 + 1] = p.g; row[x * 4 + 2] = p.r; row[x * 4 + 3] = 255; break;
			case D3DFMT_A8B8G8R8: row[x * 4] = p.r; row[x * 4 + 1] = p.g; row[x * 4 + 2] = p.b; row[x * 4 + 3] = p.a; break;
			case D3DFMT_X8B8G8R8: row[x * 4] = p.r; row[x * 4 + 1] = p.g; row[x * 4 + 2] = p.b; row[x * 4 + 3] = 255; break;
			case D3DFMT_R5G6B5:
				v16 = static_cast<uint16_t>(Quantise(p.r, 5, x, y) << 11 | Quantise(p.g, 6, x, y) << 5 | Quantise(p.b, 5, x, y));
				break;
			case D3DFMT_X1R5G5B5:
			case D3DFMT_A1R5G5B5:
				v16 = static_cast<uint16_t>((f == D3DFMT_X1R5G5B5 || p.a >= 128 ? 0x8000 : 0)
					| Quantise(p.r, 5, x, y) << 10 | Quantise(p.g, 5, x, y) << 5 | Quantise(p.b, 5, x, y));
				break;
			case D3DFMT_A4R4G4B4:
			case D3DFMT_X4R4G4B4:
				v16 = static_cast<uint16_t>((f == D3DFMT_X4R4G4B4 ? 15u : Quantise(p.a, 4, x, y)) << 12
					| Quantise(p.r, 4, x, y) << 8 | Quantise(p.g, 4, x, y) << 4 | Quantise(p.b, 4, x, y));
				break;
			case D3DFMT_A8L8: v16 = static_cast<uint16_t>(p.a << 8 | p.r); break;
			case D3DFMT_L8: row[x] = p.r; break;
			case D3DFMT_A8: row[x] = p.a; break;
			default: return false;
			}
			if (PixelBytes(f) == 2)
				memcpy(row + x * 2, &v16, 2);
		}
	}
	return true;
}

void Downsample(const std::vector<Rgba>& src, int w, int h, std::vector<Rgba>& dst, int dw, int dh, Filter filter)
{
	Tables();
	// Light, not numbers: rgb decoded from sRGB, alpha kept as it is.
	std::vector<float> lin(static_cast<size_t>(w) * h * 4);
	for (size_t i = 0; i < static_cast<size_t>(w) * h; i++)
	{
		lin[i * 4] = g_toLinear[src[i].r];
		lin[i * 4 + 1] = g_toLinear[src[i].g];
		lin[i * 4 + 2] = g_toLinear[src[i].b];
		lin[i * 4 + 3] = src[i].a / 255.0f;
	}
	std::vector<std::pair<int, float>> taps;
	std::vector<float> across(static_cast<size_t>(dw) * h * 4, 0.0f);
	for (int x = 0; x < dw; x++)
	{
		Taps(x, w, static_cast<float>(w) / dw, filter, taps);
		for (int y = 0; y < h; y++)
			for (const auto& [sx, wt] : taps)
				for (int c = 0; c < 4; c++)
					across[(static_cast<size_t>(y) * dw + x) * 4 + c] += lin[(static_cast<size_t>(y) * w + sx) * 4 + c] * wt;
	}
	dst.assign(static_cast<size_t>(dw) * dh, Rgba{});
	for (int y = 0; y < dh; y++)
	{
		Taps(y, h, static_cast<float>(h) / dh, filter, taps);
		for (int x = 0; x < dw; x++)
		{
			float acc[4] = {};
			for (const auto& [sy, wt] : taps)
				for (int c = 0; c < 4; c++)
					acc[c] += across[(static_cast<size_t>(sy) * dw + x) * 4 + c] * wt;
			dst[static_cast<size_t>(y) * dw + x] = { ToSrgb(acc[0]), ToSrgb(acc[1]), ToSrgb(acc[2]), ToByte(acc[3]) };
		}
	}
}

bool IsCutout(const std::vector<Rgba>& p)
{
	if (p.empty())
		return false;
	size_t in = 0, out = 0;
	for (const Rgba& t : p)
	{
		in += t.a >= 240;
		out += t.a <= 15;
	}
	// Nine texels in ten fully in or out, and a real share of each: a texture merely edged with
	// transparency, or one blended by its alpha (glass, smoke), is not touched.
	const size_t n = p.size();
	return (in + out) * 10 >= n * 9 && out * 100 >= n && in * 100 >= n;
}

float Coverage(const std::vector<Rgba>& p, uint8_t ref)
{
	if (p.empty())
		return 0.0f;
	size_t in = 0;
	for (const Rgba& t : p)
		in += t.a >= ref;
	return static_cast<float>(in) / p.size();
}

void KeepCoverage(std::vector<Rgba>& p, float target, uint8_t ref)
{
	// The alpha a texel gets at scale `s`, rounded as it will be written.
	auto alphaAt = [](uint8_t a, float s) { return std::clamp(static_cast<int>(a * s + 0.5f), 0, 255); };
	auto scaled = [&](float s) {
		size_t in = 0;
		for (const Rgba& t : p)
			in += alphaAt(t.a, s) >= ref;
		return static_cast<float>(in) / p.size();
	};
	if (p.empty() || std::fabs(scaled(1.0f) - target) * p.size() < 1.0f)
		return;
	// The share grows with the scale: halve the interval towards the one that matches.
	float lo = 0.0f, hi = 8.0f;
	for (int i = 0; i < 16; i++)
	{
		const float mid = (lo + hi) / 2;
		(scaled(mid) < target ? lo : hi) = mid;
	}
	// A level can be too even for an exact match (its texels all near one alpha): the closer side.
	const float s = std::fabs(scaled(lo) - target) <= std::fabs(scaled(hi) - target) ? lo : hi;
	for (Rgba& t : p)
		t.a = static_cast<uint8_t>(alphaAt(t.a, s));
}

bool FillChain(IDirect3DTexture9* tex, Filter filter, bool keepCoverage)
{
	const DWORD levels = tex->GetLevelCount();
	if (levels < 2)
		return true;
	D3DSURFACE_DESC d = {};
	if (FAILED(tex->GetLevelDesc(0, &d)) || !Supported(d.Format))
		return false;
	std::vector<Rgba> cur, next;
	D3DLOCKED_RECT r = {};
	if (FAILED(tex->LockRect(0, &r, nullptr, D3DLOCK_READONLY)))
		return false;
	const bool read = Decode(d.Format, r.pBits, r.Pitch, static_cast<int>(d.Width), static_cast<int>(d.Height), cur);
	tex->UnlockRect(0);
	if (!read)
		return false;
	// Half alpha is where the games' alpha tests split in and out, and the edge of DXT1's one bit.
	constexpr uint8_t kRef = 128;
	const bool cutout = keepCoverage && IsCutout(cur);
	const float coverage = cutout ? Coverage(cur, kRef) : 0.0f;
	std::vector<Rgba> kept;
	int w = static_cast<int>(d.Width), h = static_cast<int>(d.Height);
	for (DWORD level = 1; level < levels; level++)
	{
		D3DSURFACE_DESC ld = {};
		if (FAILED(tex->GetLevelDesc(level, &ld)))
			return false;
		const int dw = static_cast<int>(ld.Width), dh = static_cast<int>(ld.Height);
		Downsample(cur, w, h, next, dw, dh, filter);
		// The next level is filtered from this one as it came out of the filter; only what is
		// written gets its coverage back, so the scaling never compounds from level to level.
		if (cutout)
		{
			kept = next;
			KeepCoverage(kept, coverage, kRef);
		}
		if (FAILED(tex->LockRect(level, &r, nullptr, 0)))
			return false;
		const bool written = Encode(ld.Format, cutout ? kept : next, dw, dh, r.pBits, r.Pitch);
		tex->UnlockRect(level);
		if (!written)
			return false;
		cur.swap(next);
		w = dw;
		h = dh;
	}
	return true;
}
}
