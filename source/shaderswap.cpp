// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// See shaderswap.h. The token stream is walked, never trusted: a stream that leaves the given
// room or ends without an end token is not touched at all.

#include "shaderswap.h"
#include <cstring>
#include <cwchar>

size_t ShaderBytecodeBytes(const DWORD* code, size_t maxBytes)
{
	if (!code || maxBytes < 8)
		return 0;
	const DWORD version = code[0];
	const DWORD kind = version >> 16;
	if (kind != 0xFFFE && kind != 0xFFFF) // vs / ps version token
		return 0;
	// Shader model 2 and up carries each instruction's length; model 1 (HP4) does not, and is
	// walked one token at a time. Comments carry theirs in both.
	const bool instructionLengths = (version & 0xFFFF) >= 0x0200;
	const size_t maxTokens = maxBytes / 4;
	size_t i = 1;
	while (i < maxTokens)
	{
		const DWORD t = code[i];
		if (t == 0x0000FFFF) // end token
			return (i + 1) * 4;
		if ((t & 0xFFFF) == 0xFFFE) // comment, length in tokens above it
			i += 1 + ((t >> 16) & 0x7FFF);
		else
			i += instructionLengths ? 1 + ((t >> 24) & 0xF) : 1;
	}
	return 0;
}

unsigned long long ShaderFingerprint(const void* data, size_t bytes)
{
	const unsigned char* p = static_cast<const unsigned char*>(data);
	unsigned long long h = 14695981039346656037ULL;
	for (size_t i = 0; i < bytes; i++)
	{
		h ^= p[i];
		h *= 1099511628211ULL;
	}
	return h;
}

void ShaderFileName(bool pixel, unsigned long long fingerprint, wchar_t (&name)[24])
{
	swprintf_s(name, L"%ls_%016llX.bin", pixel ? L"ps" : L"vs", fingerprint);
}

namespace
{
bool PathOf(const wchar_t* folder, bool pixel, unsigned long long fingerprint, wchar_t (&path)[MAX_PATH])
{
	wchar_t name[24];
	ShaderFileName(pixel, fingerprint, name);
	return swprintf_s(path, L"%ls\\%ls", folder, name) > 0;
}
}

bool LoadShaderReplacement(const wchar_t* folder, bool pixel, unsigned long long fingerprint,
	std::vector<DWORD>& out)
{
	wchar_t path[MAX_PATH];
	if (!folder || !PathOf(folder, pixel, fingerprint, path))
		return false;
	const HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return false;
	LARGE_INTEGER size = {};
	bool ok = GetFileSizeEx(file, &size) && size.QuadPart >= 8 && size.QuadPart <= (1 << 22)
		&& size.QuadPart % 4 == 0;
	if (ok)
	{
		out.resize(static_cast<size_t>(size.QuadPart) / 4);
		DWORD read = 0;
		ok = ReadFile(file, out.data(), static_cast<DWORD>(size.QuadPart), &read, nullptr)
			&& read == size.QuadPart;
	}
	CloseHandle(file);
	// The right kind of shader, whole, with its end token where the stream says.
	ok = ok && (out[0] >> 16) == (pixel ? 0xFFFFu : 0xFFFEu)
		&& ShaderBytecodeBytes(out.data(), out.size() * 4) == out.size() * 4;
	if (!ok)
		out.clear();
	return ok;
}

bool DumpShaderFile(const wchar_t* folder, bool pixel, unsigned long long fingerprint,
	const void* data, size_t bytes)
{
	wchar_t path[MAX_PATH];
	if (!folder || !data || !bytes || !PathOf(folder, pixel, fingerprint, path))
		return false;
	CreateDirectoryW(folder, nullptr); // the game folder exists; one level is enough
	// CREATE_NEW: the same shader comes back at every Reset, the first write is kept.
	const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return false;
	DWORD written = 0;
	const bool ok = WriteFile(file, data, static_cast<DWORD>(bytes), &written, nullptr) && written == bytes;
	CloseHandle(file);
	if (!ok)
		DeleteFileW(path); // a half-written file must not pass for a shader
	return ok;
}

// ---- Soft shadows -------------------------------------------------------------------------
namespace
{
// Register type of an operand token: bits 28..30 are the low three bits, bits 11..12 the high two.
DWORD RegisterType(DWORD token)
{
	return ((token >> 28) & 7) | (((token >> 11) & 3) << 3);
}

// The constant table of a D3DX-compiled shader (the comment that opens with 'CTAB'): true when
// its sampler 4 is named LightTextureMap. `ctab` points right after the fourcc.
bool LightMapAtSampler4(const DWORD* ctab, size_t tokens)
{
	const unsigned char* base = reinterpret_cast<const unsigned char*>(ctab);
	const size_t bytes = tokens * 4;
	if (bytes < 28 || ctab[0] != 28)
		return false;
	const DWORD count = ctab[3], info = ctab[4];
	for (DWORD i = 0; i < count; i++)
	{
		const size_t at = static_cast<size_t>(info) + 20 * i;
		if (at + 20 > bytes)
			return false;
		DWORD name = 0;
		WORD set = 0, index = 0;
		memcpy(&name, base + at, 4);
		memcpy(&set, base + at + 4, 2);
		memcpy(&index, base + at + 6, 2);
		if (set == 3 && index == 4) // D3DXRS_SAMPLER, s4
		{
			static const char wanted[] = "LightTextureMap";
			return name + sizeof(wanted) <= bytes && memcmp(base + name, wanted, sizeof(wanted)) == 0;
		}
	}
	return false;
}
}

bool SoftenShadowSampling(const DWORD* code, size_t bytes, std::vector<DWORD>& out)
{
	out.clear();
	const size_t n = bytes / 4;
	if (!code || n < 2 || code[0] != 0xFFFF0300) // ps_3_0 only
		return false;
	bool lightMap = false;
	size_t texld = 0, lastHeader = 0, found = 0, i = 1;
	DWORD maxTemp = 0;
	for (; i < n; )
	{
		const DWORD t = code[i];
		if (t == 0x0000FFFF)
			break;
		if ((t & 0xFFFF) == 0xFFFE) // comment
		{
			const size_t len = (t >> 16) & 0x7FFF;
			if (i + 1 + len > n)
				return false;
			if (len >= 8 && code[i + 1] == 0x42415443) // 'CTAB'
				lightMap = LightMapAtSampler4(code + i + 2, len - 1);
			i += 1 + len;
			continue;
		}
		const DWORD op = t & 0xFFFF;
		const size_t len = (t >> 24) & 0xF;
		if (i + 1 + len > n)
			return false;
		if (op == 0x1F || op == 0x51) // dcl, def: no register operands to weigh, floats after def
			lastHeader = i + 1 + len;
		else
		{
			for (size_t k = 1; k <= len; k++)
			{
				const DWORD o = code[i + k];
				const DWORD type = RegisterType(o), number = o & 0x7FF;
				if (type == 0 && number > maxTemp)
					maxTemp = number;
				if (type == 2 && number == 220)
					return false; // c220 is taken
				if (k > 1 && (o & 0x2000)) // relative addressing: one more token follows
					k++;
			}
			if (op == 0x42 && len == 3 && RegisterType(code[i + 3]) == 10 && (code[i + 3] & 0x7FF) == 4) // texld ..., s4
			{
				if (RegisterType(code[i + 1]) != 0 || RegisterType(code[i + 2]) != 1)
					return false; // not the pattern: the map read into or from something else
				texld = i;
				found++;
			}
		}
		i += 1 + len;
	}
	if (i >= n || !lightMap || found != 1 || maxTemp + 2 > 31 || !lastHeader || lastHeader > texld)
		return false;
	const DWORD a = maxTemp + 1, b = maxTemp + 2; // two free temporaries
	const DWORD x = code[texld + 1] & 0x7FF, src = code[texld + 2], sampler = code[texld + 3];
	out.reserve(n + 54);
	out.insert(out.end(), code, code + lastHeader);
	// def c220, d, -d, 0.25, 0 (d = 0.75 / 128, as the files seen in game carry it)
	const float d = 0.005859f, minus = -d, quarter = 0.25f;
	DWORD dBits = 0, minusBits = 0, quarterBits = 0;
	memcpy(&dBits, &d, 4);
	memcpy(&minusBits, &minus, 4);
	memcpy(&quarterBits, &quarter, 4);
	const DWORD def[6] = { 0x05000051, 0xA00F00DC, dBits, minusBits, quarterBits, 0 };
	out.insert(out.end(), def, def + 6);
	out.insert(out.end(), code + lastHeader, code + texld);
	// add rB.xy, vN, c220.xx ; texld_pp rX, rB, s4 ; then three more taps into rA, each added to rX.
	static const DWORD swizzle[4] = { 0x00, 0x54, 0x01, 0x55 }; // xx, xy, yx, yy
	for (int k = 0; k < 4; k++)
	{
		const DWORD tap[8] = { 0x03000002, 0x80030000 | b, src, 0xA00000DC | (swizzle[k] << 16),
			0x03000042, 0x802F0000 | (k ? a : x), 0x80E40000 | b, sampler };
		out.insert(out.end(), tap, tap + 8);
		if (k)
		{
			const DWORD sum[4] = { 0x03000002, 0x802F0000 | x, 0x80E40000 | x, 0x80E40000 | a };
			out.insert(out.end(), sum, sum + 4);
		}
	}
	const DWORD mean[4] = { 0x03000005, 0x802F0000 | x, 0x80E40000 | x, 0xA0AA00DC }; // mul_pp rX, rX, c220.z
	out.insert(out.end(), mean, mean + 4);
	out.insert(out.end(), code + texld + 4, code + i + 1); // the rest, end token included
	return true;
}
