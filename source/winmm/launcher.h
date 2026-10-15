// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// HP2's launcher window under Wine: black, with only "Quitter" showing. Pure byte work: testable
// without the game (tests/winmm_test.cpp).
//
// Reported by Federico Dossena ("Fixing Harry Potter 2 on Linux (Wine)"): the launcher's two
// SetWindowPos calls in Game.exe are the cause, and the window stays centred without them
// (WINDOW.DLL has already centred and shown it). He ships a changed Game.exe; we change nothing on
// disk: this DLL rewrites the two calls in memory, under Wine only (notes/HP2.md, 2026-10-15).
// Read in OUR Game.exe at 0x1090258C and 0x109025A5:
//     push -1 (HWND_TOPMOST)    / mov ecx, [launcher] / push ecx / call [SetWindowPos]
//     push 3 / push 0 x4 / push -2 (HWND_NOTOPMOST) / mov edx, [launcher] / push edx / call [SetWindowPos]
// The same handle and the same import slot in both: a look-alike elsewhere is not taken (Game.exe
// has a second pair, on another window, with the handle read from a frame).
// HP1's HP.exe has the same pair (once, at file offset 0x2813): off by default, switched on by
// HP2's winmm.ini only, since nobody has seen HP1's launcher black under Wine.
//
// Each call [SetWindowPos] (FF 15 + slot, 6 bytes) becomes add esp, 28 + 3 nops (83 C4 1C 90 90 90):
// SetWindowPos is stdcall and would have taken its seven arguments off the stack; eax is not read
// after either call.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace launcher
{
constexpr size_t kLength = 40;           // the bytes Find matches, from the first push -1
constexpr size_t kCalls[2] = {9, 34};    // where the two calls are, in them

inline uint32_t U32(const uint8_t* p)
{
	uint32_t v;
	std::memcpy(&v, p, sizeof(v));
	return v;
}

// The offset of the pair in code, or -1. *slot gets the address of the import slot both calls
// go through, for the caller to check it really is SetWindowPos.
inline ptrdiff_t Find(const uint8_t* code, size_t size, uint32_t* slot)
{
	static const uint8_t pushes[] = {0x6A, 0x03, 0x6A, 0x00, 0x6A, 0x00, 0x6A, 0x00, 0x6A, 0x00,
		0x6A, 0xFE};
	if (size < kLength)
		return -1;
	for (size_t i = 0; i + kLength <= size; ++i)
	{
		const uint8_t* p = code + i;
		if (p[0] != 0x6A || p[1] != 0xFF || p[2] != 0x8B || p[3] != 0x0D || p[8] != 0x51
			|| p[9] != 0xFF || p[10] != 0x15
			|| std::memcmp(p + 15, pushes, sizeof(pushes)) != 0
			|| p[27] != 0x8B || p[28] != 0x15 || p[33] != 0x52 || p[34] != 0xFF || p[35] != 0x15)
			continue;
		if (U32(p + 4) != U32(p + 29) || U32(p + 11) != U32(p + 36))
			continue;
		if (slot)
			*slot = U32(p + 11);
		return static_cast<ptrdiff_t>(i);
	}
	return -1;
}

// at: what Find returned, in code.
inline void Remove(uint8_t* at)
{
	static const uint8_t pop[] = {0x83, 0xC4, 0x1C, 0x90, 0x90, 0x90};
	for (size_t call : kCalls)
		std::memcpy(at + call, pop, sizeof(pop));
}
}
