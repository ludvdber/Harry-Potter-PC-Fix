// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// HP3 holds itself at 60 frames per second: UGameEngine::GetMaxTickRate (Engine.dll) answers the
// constant 60.0 for the game, and the main loop of hppoa.exe sleeps the rest of each frame
// (notes/HP3_EXPLORATION.md, "Cadence dans le moteur"). That constant is shared with other code,
// so the one `fld dword ptr [60.0]` of that function is pointed at a float of ours instead: the
// cap becomes what dinput8.ini asks (FrameRateCap). Nothing on disk, and nothing at all unless the
// loaded Engine.dll is the build read. Above 60 the game has two known bugs (a frozen imp, portraits
// that no longer open): an option to try, off by default. The layout and the decision are here,
// pure: testable without the game (tests/dinput8_test.cpp). The write itself is in dinput8.cpp.
//
// Read in Engine.dll (linked 2004-05-03, 4 980 736 bytes, image base 0x10300000) on 2026-10-21:
//   - the export is a jump thunk at rva 0x5916 (E9 rel32) to the body at rva 0x118AD0;
//   - the body ends at rva 0x118CB8; its game branch is the last: `fld dword ptr [0x10675554]`
//     (D9 05 disp32) at rva 0x118CA1, then the epilogue;
//   - 0x10675554 (.rdata, rva 0x375554) holds 60.0f and has other readers (two fmul elsewhere).
// The disp32 is an absolute address: on a relocated load it reads base + 0x375554.

#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstring>

namespace tick
{
constexpr char kExport[] = "?GetMaxTickRate@UGameEngine@@UAEMXZ";
constexpr DWORD kTimeDateStamp = 0x40968BF3;
constexpr DWORD kSizeOfImage = 0x6B4000;
constexpr size_t kThunk = 0x5916;    // the export: jmp rel32 to the body
constexpr size_t kBody = 0x118AD0;   // UGameEngine::GetMaxTickRate
constexpr size_t kLoad = 0x118CA1;   // fld dword ptr [60.0]: D9 05 disp32
constexpr size_t kSixty = 0x375554;  // 60.0f, shared with other code: never written
constexpr size_t kLoadSize = 6;

// The jmp rel32 at `thunk` (rva `thunkRva`) lands on the body.
inline bool ThunkToBody(const uint8_t* thunk, size_t thunkRva)
{
	if (thunk[0] != 0xE9)
		return false;
	int32_t rel;
	std::memcpy(&rel, thunk + 1, sizeof(rel));
	return static_cast<int64_t>(thunkRva) + 5 + rel == static_cast<int64_t>(kBody);
}

// The load instruction, reading the constant where the module is loaded (base + kSixty).
inline bool LoadMatches(const uint8_t* at, uintptr_t base)
{
	uint32_t disp;
	std::memcpy(&disp, at + 2, sizeof(disp));
	return at[0] == 0xD9 && at[1] == 0x05 && disp == static_cast<uint32_t>(base + kSixty);
}

// The loaded Engine.dll is the build read: header, thunk, the load and the constant it reads.
inline bool Recognised(const uint8_t* image)
{
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		return false;
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp != kTimeDateStamp
		|| nt->OptionalHeader.SizeOfImage != kSizeOfImage)
		return false;
	float sixty;
	std::memcpy(&sixty, image + kSixty, sizeof(sixty));
	return ThunkToBody(image + kThunk, kThunk) && LoadMatches(image + kLoad, reinterpret_cast<uintptr_t>(image))
		&& sixty == 60.0f;
}

// The load pointed at `ours`. Same instruction length: nothing else moves.
inline void Point(uint8_t* image, const float* ours)
{
	const uint32_t address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ours));
	std::memcpy(image + kLoad + 2, &address, sizeof(address));
}

// The cap asked in the ini: 0 = as shipped, else 30..1000.
inline int Bounded(int cap)
{
	return cap <= 0 ? 0 : (cap < 30 ? 30 : (cap > 1000 ? 1000 : cap));
}
}
