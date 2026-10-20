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

	printf("%d failure(s)\n", failures);
	return failures ? 1 : 0;
}
