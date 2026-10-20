// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// See shaderswap.h. The token stream is walked, never trusted: a stream that leaves the given
// room or ends without an end token is not touched at all.

#include "shaderswap.h"
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
