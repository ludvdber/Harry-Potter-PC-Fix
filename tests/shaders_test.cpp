// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
// The shader extraction and replacement logic (shaderswap.cpp), without Direct3D or a game:
// the token walk that bounds a shader, the fingerprint, and the dump / load round trip.
#include "../source/shaderswap.h"
#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;
static void Expect(bool ok, const char* what)
{
	if (!ok)
		printf("FAIL %s\n", what);
	failures += !ok;
}
#define EXPECT(cond) Expect((cond), #cond)

int main(int argc, char** argv)
{
	if (argc < 2)
		return 2;
	std::wstring scratch(argv[1], argv[1] + strlen(argv[1]));
	scratch += L"\\shaders_scratch";

	// A shader-model-3 stream: version, a comment of 2 tokens, an instruction of 2 operands
	// (length bits 24..27), the end token. 8 tokens, 32 bytes.
	const DWORD vs3[] = { 0xFFFE0300, 0x0002FFFE, 0x42414354, 0x0000001C,
	                      0x02000005, 0x800F0000, 0xA0E40000, 0x0000FFFF };
	EXPECT(ShaderBytecodeBytes(vs3, sizeof(vs3)) == sizeof(vs3));
	// Trailing bytes after the end token are not the shader's.
	const DWORD vs3Trail[] = { 0xFFFE0300, 0x02000005, 0x800F0000, 0xA0E40000, 0x0000FFFF, 0xDEADBEEF };
	EXPECT(ShaderBytecodeBytes(vs3Trail, sizeof(vs3Trail)) == sizeof(vs3Trail) - 4);
	// Shader model 1 (HP4): no instruction lengths, walked one token at a time.
	const DWORD ps1[] = { 0xFFFF0101, 0x00000042, 0xB00F0000, 0x0000FFFF };
	EXPECT(ShaderBytecodeBytes(ps1, sizeof(ps1)) == sizeof(ps1));
	// No end token within the given room, or no version token at all: not a shader, 0.
	const DWORD cut[] = { 0xFFFE0300, 0x02000005, 0x800F0000, 0xA0E40000 };
	EXPECT(ShaderBytecodeBytes(cut, sizeof(cut)) == 0);
	const DWORD junk[] = { 0x12345678, 0x0000FFFF };
	EXPECT(ShaderBytecodeBytes(junk, sizeof(junk)) == 0);
	EXPECT(ShaderBytecodeBytes(nullptr, 64) == 0);

	// The fingerprint holds still, and moves with any byte.
	const unsigned long long print = ShaderFingerprint(vs3, sizeof(vs3));
	EXPECT(print == ShaderFingerprint(vs3, sizeof(vs3)));
	DWORD other[8];
	memcpy(other, vs3, sizeof(vs3));
	other[5] ^= 1;
	EXPECT(print != ShaderFingerprint(other, sizeof(other)));
	// The file name both sides agree on.
	wchar_t name[24];
	ShaderFileName(false, 0x0123456789ABCDEFULL, name);
	EXPECT(wcscmp(name, L"vs_0123456789ABCDEF.bin") == 0);

	// Dump, then load as a replacement: the same bytes come back. The second dump of the same
	// shader (every Reset creates them again) is refused, the file is kept.
	EXPECT(DumpShaderFile(scratch.c_str(), false, print, vs3, sizeof(vs3)));
	EXPECT(!DumpShaderFile(scratch.c_str(), false, print, vs3, sizeof(vs3)));
	std::vector<DWORD> back;
	EXPECT(LoadShaderReplacement(scratch.c_str(), false, print, back));
	EXPECT(back.size() * 4 == sizeof(vs3) && memcmp(back.data(), vs3, sizeof(vs3)) == 0);
	// The wrong kind (a vertex shader file for a pixel shader), or no file: refused.
	EXPECT(!LoadShaderReplacement(scratch.c_str(), true, print, back));
	EXPECT(!LoadShaderReplacement(scratch.c_str(), false, print ^ 1, back));
	EXPECT(!LoadShaderReplacement(nullptr, false, print, back));
	// A truncated file (no end token where the stream says) must not reach the game.
	const unsigned long long cutPrint = ShaderFingerprint(cut, sizeof(cut));
	EXPECT(DumpShaderFile(scratch.c_str(), true, cutPrint, cut, sizeof(cut)));
	EXPECT(!LoadShaderReplacement(scratch.c_str(), true, cutPrint, back));

	// The soft-shadow rewrite. A ps_3_0 with a constant table naming LightTextureMap at s4
	// (header of 7 DWORDs, one constant of 5, the name), dcl_texcoord v1.xy, dcl_2d s4,
	// texld_pp r0, v1, s4, mov oC0, r0, end.
	const DWORD ctab[] = { 0x42415443, 0x0000001C, 0, 0xFFFF0300, 1, 0x0000001C, 0, 0, // 'CTAB', header
	                       0x00000030, 0x00040003, 0x00000001, 0, 0,                      // name at 0x30, sampler 4, count 1
	                       0x6867694C, 0x78655474, 0x65727574, 0x0070614D };             // "LightTextureMap\0"
	std::vector<DWORD> soft = { 0xFFFF0300, 0x0011FFFE };
	soft.insert(soft.end(), ctab, ctab + 17);
	const DWORD body[] = { 0x0200001F, 0x80000005, 0x90030001, 0x0200001F, 0x90000000, 0xA00F0804,
	                       0x03000042, 0x802F0000, 0x90E40001, 0xA0E40804, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF };
	soft.insert(soft.end(), body, body + 14);
	std::vector<DWORD> soften;
	EXPECT(SoftenShadowSampling(soft.data(), soft.size() * 4, soften));
	// Expected: the same up to the last dcl, def c220, then four taps (r2 the coordinate, r1 the
	// extra tap, added into r0), the mean, the rest.
	const DWORD taps[] = { 0x05000051, 0xA00F00DC, 0x3BBFFCDB, 0xBBBFFCDB, 0x3E800000, 0,
	                       0x03000002, 0x80030002, 0x90E40001, 0xA00000DC, 0x03000042, 0x802F0000, 0x80E40002, 0xA0E40804,
	                       0x03000002, 0x80030002, 0x90E40001, 0xA05400DC, 0x03000042, 0x802F0001, 0x80E40002, 0xA0E40804,
	                       0x03000002, 0x802F0000, 0x80E40000, 0x80E40001,
	                       0x03000002, 0x80030002, 0x90E40001, 0xA00100DC, 0x03000042, 0x802F0001, 0x80E40002, 0xA0E40804,
	                       0x03000002, 0x802F0000, 0x80E40000, 0x80E40001,
	                       0x03000002, 0x80030002, 0x90E40001, 0xA05500DC, 0x03000042, 0x802F0001, 0x80E40002, 0xA0E40804,
	                       0x03000002, 0x802F0000, 0x80E40000, 0x80E40001,
	                       0x03000005, 0x802F0000, 0x80E40000, 0xA0AA00DC,
	                       0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF };
	std::vector<DWORD> expected(soft.begin(), soft.begin() + 2 + 17 + 6);
	expected.insert(expected.end(), taps, taps + sizeof(taps) / 4);
	EXPECT(soften == expected);
	EXPECT(ShaderBytecodeBytes(soften.data(), soften.size() * 4) == soften.size() * 4);
	// Refused: another sampler name, a second sample of s4, c220 in use, not shader model 3.
	std::vector<DWORD> other = soft;
	other[2 + 13] = 0x6867694D; // "Might..."
	EXPECT(!SoftenShadowSampling(other.data(), other.size() * 4, soften) && soften.empty());
	std::vector<DWORD> twice = soft;
	twice.insert(twice.end() - 4, body + 6, body + 10);
	EXPECT(!SoftenShadowSampling(twice.data(), twice.size() * 4, soften));
	std::vector<DWORD> taken = soft;
	const DWORD movC220[] = { 0x02000001, 0x800F0001, 0xA0E400DC };
	taken.insert(taken.end() - 4, movC220, movC220 + 3);
	EXPECT(!SoftenShadowSampling(taken.data(), taken.size() * 4, soften));
	std::vector<DWORD> ps2 = soft;
	ps2[0] = 0xFFFF0200;
	EXPECT(!SoftenShadowSampling(ps2.data(), ps2.size() * 4, soften));
	EXPECT(!SoftenShadowSampling(nullptr, 64, soften));

	printf("%d failure(s)\n", failures);
	return failures ? 1 : 0;
}
