// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// HP2's launcher window under Wine: black, with only "Quitter" showing. The decision and the
// import-table reading are here, pure: testable without the game (tests/winmm_test.cpp).
//
// Reported by Federico Dossena ("Fixing Harry Potter 2 on Linux (Wine)"); read on Bazzite with a
// SetWindowPos relay (notes/HP2.md, 2026-10-20). The menu page ("MainMenu", a child dialog inside
// the launcher's frame) is created in place; then Game.exe centres it ON THE SCREEN:
//     SetWindowPos(page, HWND_TOPMOST, 717, 257, 486, 566, SWP_FRAMECHANGED)
// 717,257 is (screen - 486x566) / 2. On a CHILD window those coordinates are parent-relative: the
// page lands outside the launcher's window, and only "Quitter" (a button of the parent) is left.
// Windows shows the launcher fine, so there the call cannot land the page outside (its window or
// coordinates differ; not read yet). Game.exe holds TWO such pairs of calls through its one
// SetWindowPos import slot - 0x1090258C/0x109025A5 on the launcher's own window (a global handle)
// and 0x1091301D/0x10913036 on the page (the pair that runs; a fix of 2026-10-15 removed the
// FIRST pair's bytes and changed nothing). So no more byte patterns: the import slot itself is
// pointed at a relay (winmm.cpp) that drops a screen-centring SetWindowPos on a child window and
// passes every other call through. That covers both pairs, on any edition of the exe.

#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstring>

namespace launcher
{
// Where (sw - cx) / 2, (sh - cy) / 2 would put a cx by cy window: the screen centring the
// launcher computes with GetSystemMetrics(0/1).
inline bool Centred(int x, int y, int cx, int cy, int sw, int sh)
{
	return cx > 0 && cy > 0 && x == (sw - cx) / 2 && y == (sh - cy) / 2;
}

// The one call the relay drops: a SetWindowPos with real coordinates (no SWP_NOMOVE/NOSIZE) that
// centres a CHILD window on the screen - coordinates the window will read as parent-relative.
inline bool Ignored(LONG style, UINT flags, int x, int y, int cx, int cy, int sw, int sh)
{
	return (style & WS_CHILD) && !(flags & (SWP_NOMOVE | SWP_NOSIZE))
		&& Centred(x, y, cx, cy, sw, sh);
}

// The mapped image's import slot for dll!name - the IAT entry its call [slot] instructions read -
// or nullptr. Only imports by name are looked at (Game.exe imports SetWindowPos by name).
inline void** ImportSlot(uint8_t* image, const char* dll, const char* name)
{
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(image + dos->e_lfanew);
	const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir.VirtualAddress)
		return nullptr;
	for (const auto* imp = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(image + dir.VirtualAddress);
		imp->Name; ++imp)
	{
		if (_stricmp(reinterpret_cast<const char*>(image + imp->Name), dll) != 0)
			continue;
		// The names live in the original thunks; the slots the code calls through, in the first.
		const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA*>(
			image + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
		auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(image + imp->FirstThunk);
		for (; names->u1.AddressOfData; ++names, ++slots)
		{
			if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
				continue;
			const auto* entry = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(image + names->u1.AddressOfData);
			if (std::strcmp(reinterpret_cast<const char*>(entry->Name), name) == 0)
				return reinterpret_cast<void**>(&slots->u1.Function);
		}
	}
	return nullptr;
}
}
