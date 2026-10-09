// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
// Checks the byte search of hooks.cpp on this program's own image, without a game: a sequence held
// once is found, a sequence held twice is refused by FindUniquePattern (another build of a game
// could hold one of ours twice, and the first copy may not be the code we read), and FindPattern
// still takes the first copy (the aspect ratio relies on it).
#include "../source/hooks.h"
#include <cstdarg>
#include <cstdio>

void Log(const char* fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); }

static int failures = 0;
static void Expect(bool ok, const char* what)
{
	if (!ok)
		printf("FAIL %s\n", what);
	failures += !ok;
}

// Sixteen bytes no compiler writes by itself. `kOnce` is in the image once; the twin twice, once
// read-only and once writable, so the linker cannot fold the two copies into one.
static const BYTE kOnce[] = { 0xA7, 0x10, 0xC1, 0x0D, 0x5E, 0xED, 0x01, 0x33, 0x7A, 0x9C, 0x42, 0xE1, 0x6B, 0x0F, 0xD4, 0x88 };
static const BYTE kTwinA[] = { 0x3C, 0x91, 0xE4, 0x07, 0xB2, 0x5D, 0xF0, 0x6E, 0x19, 0xCB, 0x84, 0x2F, 0xA0, 0x73, 0xD8, 0x56 };
static BYTE g_twinB[] = { 0x3C, 0x91, 0xE4, 0x07, 0xB2, 0x5D, 0xF0, 0x6E, 0x19, 0xCB, 0x84, 0x2F, 0xA0, 0x73, 0xD8, 0x56 };

int main()
{
	HMODULE self = GetModuleHandleA(nullptr);
	bool twice = true;

	Expect(FindUniquePattern(self, kOnce, sizeof(kOnce), &twice) == kOnce, "a sequence held once is found where it is");
	Expect(!twice, "a sequence held once is not called twice");

	twice = false;
	Expect(FindUniquePattern(self, kTwinA, sizeof(kTwinA), &twice) == nullptr, "a sequence held twice is refused");
	Expect(twice, "and the refusal says why");
	Expect(FindUniquePattern(self, kTwinA, sizeof(kTwinA)) == nullptr, "refused without asking why too");

	const BYTE* a = kTwinA;
	const BYTE* b = g_twinB;
	const BYTE* first = a < b ? a : b;
	Expect(FindPattern(self, kTwinA, sizeof(kTwinA)) == first, "FindPattern still takes the first copy");

	// Built on the stack, outside the image: nowhere.
	BYTE absent[sizeof(kOnce)];
	for (size_t i = 0; i < sizeof(kOnce); i++)
		absent[i] = static_cast<BYTE>(kOnce[i] ^ 0x5A);
	twice = true;
	Expect(FindUniquePattern(self, absent, sizeof(absent), &twice) == nullptr, "an absent sequence is not found");
	Expect(!twice, "an absent sequence is not called twice");

	Expect(g_twinB[0] == 0x3C, "the writable twin is kept in the image");
	if (failures)
		printf("%d FAILED\n", failures);
	else
		printf("hooks: all checks passed\n");
	return failures ? 1 : 0;
}
