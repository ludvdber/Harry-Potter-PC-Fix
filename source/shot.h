// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// A screenshot on its way to the disk, shared by d3d9.dll (present.cpp) and winmm.dll
// (winmm/overlay11.cpp): where the picture goes and how it is written. The games take the
// keyboard exclusively, and Windows' own capture keys work badly over them. The picture is copied
// off the GPU at once, then written as PNG by Windows' own image codecs (WIC, which Wine has too)
// on a thread of its own: the game does not stall while it compresses.

#pragma once

#include <windows.h>
#include <wincodec.h>
#include <cwchar>

namespace shot
{
struct Shot
{
	wchar_t file[MAX_PATH];
	UINT width = 0, height = 0;
	BYTE* bgr = nullptr;   // 24-bit rows, top to bottom, no padding
};

// Each missing folder of `path`, parent first.
inline void MakeFolders(const wchar_t* path)
{
	wchar_t part[MAX_PATH];
	for (size_t i = 0; path[i] && i < MAX_PATH - 1; i++)
	{
		part[i] = path[i];
		part[i + 1] = L'\0';
		if ((path[i + 1] == L'\\' || path[i + 1] == L'/' || !path[i + 1]) && !(i == 1 && path[1] == L':'))
			CreateDirectoryW(part, nullptr);
	}
}

inline bool WritePng(const Shot& s)
{
	IWICImagingFactory* factory = nullptr;
	IWICStream* stream = nullptr;
	IWICBitmapEncoder* encoder = nullptr;
	IWICBitmapFrameEncode* frame = nullptr;
	WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
	bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory1, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
		&& SUCCEEDED(factory->CreateStream(&stream))
		&& SUCCEEDED(stream->InitializeFromFilename(s.file, GENERIC_WRITE))
		&& SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))
		&& SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache))
		&& SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr))
		&& SUCCEEDED(frame->Initialize(nullptr))
		&& SUCCEEDED(frame->SetSize(s.width, s.height))
		&& SUCCEEDED(frame->SetPixelFormat(&format))
		&& IsEqualGUID(format, GUID_WICPixelFormat24bppBGR)
		&& SUCCEEDED(frame->WritePixels(s.height, s.width * 3, s.width * 3 * s.height, s.bgr))
		&& SUCCEEDED(frame->Commit())
		&& SUCCEEDED(encoder->Commit());
	if (frame) frame->Release();
	if (encoder) encoder->Release();
	if (stream) stream->Release();
	if (factory) factory->Release();
	if (!ok)
		DeleteFileW(s.file);   // no half-written picture left behind
	return ok;
}

// Where the next picture goes: `folder` when the ini names one, else "screenshots" next to
// `self` (this DLL). The name is the local time; a second picture in the same second gets _2.
inline bool FileName(HMODULE self, const wchar_t* folder, wchar_t (&file)[MAX_PATH])
{
	wchar_t dir[MAX_PATH];
	if (folder && folder[0])
		wcscpy_s(dir, folder);
	else
	{
		GetModuleFileNameW(self, dir, MAX_PATH);
		wchar_t* slash = wcsrchr(dir, L'\\');
		if (!slash)
			return false;
		wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - dir), L"screenshots");
	}
	size_t n = wcslen(dir);
	while (n && (dir[n - 1] == L'\\' || dir[n - 1] == L'/'))
		dir[--n] = L'\0';
	MakeFolders(dir);
	SYSTEMTIME t;
	GetLocalTime(&t);
	for (int k = 1; k < 100; k++)
	{
		wchar_t suffix[8] = L"";
		if (k > 1)
			swprintf_s(suffix, L"_%d", k);
		swprintf_s(file, L"%s\\%04u-%02u-%02u_%02u-%02u-%02u%s.png", dir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
			t.wSecond, suffix);
		if (GetFileAttributesW(file) == INVALID_FILE_ATTRIBUTES)
			return true;
	}
	return false;
}

// 32-bit rows (B, G, R, x in memory, as BGRA and as RGBA swapped by `rgba`) to the 24-bit rows of
// a Shot. `pitch` is the row stride of the source in bytes.
inline void Pack(const BYTE* src, UINT pitch, bool rgba, Shot& s)
{
	for (UINT y = 0; y < s.height; y++)
	{
		const BYTE* row = src + static_cast<size_t>(y) * pitch;
		BYTE* dst = s.bgr + static_cast<size_t>(y) * s.width * 3;
		for (UINT x = 0; x < s.width; x++)
		{
			dst[x * 3] = row[x * 4 + (rgba ? 2 : 0)];
			dst[x * 3 + 1] = row[x * 4 + 1];
			dst[x * 3 + 2] = row[x * 4 + (rgba ? 0 : 2)];
		}
	}
}
}
