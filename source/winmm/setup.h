// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// HP1's first-run setup, skipped. Pure byte work: testable without the game (tests/winmm_test.cpp).
//
// HP.exe goes through its setup on EVERY start, not only the first (read in HP.exe on 2026-10-07,
// notes/HP1.md):
//   - Running.ini in Documents\Harry Potter (the launcher's guard, never taken away): the "premiere
//     configuration" wizard opens, with an empty list of 3D cards and "Commencer !".
//   - no Running.ini: a second HP.exe is started to test D3DDrv (not shipped), and GameRenderDevice
//     is written D3DDrv or SoftDrv into HP.ini: Alt+Enter then crashes the game.
// Both live in one block, entered when the engine is neither the editor nor without a client:
//     mov ecx, [GIsEditor] / cmp dword [ecx], 0 / jne <after the block>
//     mov edx, [GIsClient] / cmp dword [edx], 0 / je  <after the block>
//     lea ecx, [ebp - x]  / call [WWizardDialog::WWizardDialog]
// Making the first jump unconditional skips the whole block, wizard AND test: the renderer stays
// the one HP.ini names (D3D11, written by the launcher), and Running.ini keeps its other job (the
// game writes it at start and deletes it on a clean exit). After the block the engine goes on as
// usual (FirstRun written, CD path, and so on).

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace setup
{
constexpr size_t kLength = 38;   // the bytes Find matches, from the first mov
constexpr size_t kJump = 9;      // where the jne is, in them

inline int32_t Rel32(const uint8_t* p)
{
	int32_t v;
	std::memcpy(&v, p, sizeof(v));
	return v;
}

// The offset of the block's first jne in code, or -1. Both jumps must land at the same place (the
// end of the block): a look-alike elsewhere is not taken.
inline ptrdiff_t Find(const uint8_t* code, size_t size)
{
	if (size < kLength)
		return -1;
	for (size_t i = 0; i + kLength <= size; ++i)
	{
		const uint8_t* p = code + i;
		if (p[0] != 0x8B || p[1] != 0x0D || p[6] != 0x83 || p[7] != 0x39 || p[8] != 0x00
			|| p[9] != 0x0F || p[10] != 0x85
			|| p[15] != 0x8B || p[16] != 0x15 || p[21] != 0x83 || p[22] != 0x3A || p[23] != 0x00
			|| p[24] != 0x0F || p[25] != 0x84
			|| p[30] != 0x8D || p[31] != 0x8D || p[36] != 0xFF || p[37] != 0x15)
			continue;
		const int64_t first = static_cast<int64_t>(i) + 15 + Rel32(p + 11);
		const int64_t second = static_cast<int64_t>(i) + 30 + Rel32(p + 26);
		if (first == second && first > static_cast<int64_t>(i))
			return static_cast<ptrdiff_t>(i + kJump);
	}
	return -1;
}

// jne rel32 (0F 85 xx xx xx xx) becomes nop + jmp rel32 (90 E9 xx xx xx xx): same length, same end,
// so the same rel32 lands at the same place.
inline void Skip(uint8_t* jne)
{
	jne[0] = 0x90;
	jne[1] = 0xE9;
}
}
