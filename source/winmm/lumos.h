// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// HP2's Lumos: the walls that should fade out near the wand stay solid with the shipped renderer
// (d3d11drv.dll 1.6.2). The decision and the renderer's layout are here, pure: testable without
// the game (tests/winmm_test.cpp). The relay itself is in winmm.cpp.
//
// Read in Render.dll, Engine.dll and d3d11drv.dll on 2026-10-20 (notes/HP2_LUMOS.md):
//   - the studio changed the engine's DrawComplexSurface to FIVE arguments,
//         DrawComplexSurface(Frame, Surface, Facet, DWORD PolyFlags, INT Alpha)
//     and computes the fade itself: for a surface the maps mark Modulated|HighShadowDetail
//     (0x800040), Alpha is the TRANSPARENCY, 0 opaque .. 255 invisible, from the distance of the
//     wand (a wall near the wand goes up to 200, a revealed object down to 100). Any other surface
//     comes with Alpha 255 and its flags untouched;
//   - the renderer never reads the two added arguments. It draws a lit surface as "Translucent"
//     (a screen blend, near opaque on light stone) and skips the revealed geometry when the wand is
//     far. So the player sees a solid wall and, at best, a pale ghost of what is behind.
// The fix: around the renderer's own DrawComplexSurface, when Alpha is 1..254 on a marked surface,
// bind a blend state with a constant factor (the opacity) for the geometry it just buffered, and
// make the renderer flush and set its own state again on the next surface. The renderer keeps the
// last flags it applied in one global (kFlagsCache) and only changes state when they differ: a
// bit the game never passes (0x1000 is cleared at the entry of DrawComplexSurface) is toggled in
// that cache before and after the call, so the state is applied both times.

#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstring>

namespace lumos
{
// The build shipped with HP1 and HP2 (same bytes): "Harry Potter 2 Release", 140 288 bytes.
constexpr DWORD kTimeDateStamp = 0x5FEB7C4A;
constexpr DWORD kSizeOfImage = 0x26000;
// Offsets from the DLL's base (its image base is 0x10000000: a file address minus that).
constexpr size_t kVtable = 0x1B7B0;             // UD3D11RenderDevice's virtual table
constexpr size_t kDrawComplexSurfaceSlot = 30;  // the slot Render.dll calls
constexpr size_t kDrawComplexSurface = 0xB9A0;  // what the slot holds
constexpr size_t kFlagsCache = 0x21724;         // the last PolyFlags the renderer applied
constexpr size_t kContext = 0x2172C;            // ID3D11DeviceContext* the renderer draws with
constexpr DWORD kRefresh = 0x1000;              // toggled in the cache: "flags changed"

// The engine's marks: PF_Modulated | PF_HighShadowDetail, both borrowed by the studio.
constexpr DWORD kMarked = 0x00800040;

// The first bytes of the renderer's DrawComplexSurface, up to "and [Surface.PolyFlags], ~0x1000".
constexpr uint8_t kPrologue[] = { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x81, 0xEC, 0xBC, 0x00, 0x00, 0x00,
	0x53, 0x8B, 0x5D, 0x0C, 0x81, 0x23, 0xFF, 0xEF, 0xFF, 0xFF };

// The engine's FSurfaceInfo: PolyFlags first (the renderer masks [Surface] at its entry).
struct SurfaceInfo
{
	DWORD PolyFlags;
};

// ---- Render.dll: the engine's own Lumos pass ----------------------------------------------
//
// SEEN in the game on 2026-10-20 (a probe in this DLL): the player's actor holds the Lumos
// switch at +0x950 (1 while the wand is lit) and the radius at +0x954 (512.0, the default of
// Engine.u), but the shipped Render.dll (linked 2020-06-15, 299 008 bytes: a rebuild, not the
// file of 2002) reads them at +0x94C and +0x950, one DWORD short, in its four reads: the switch
// is always off and the radius 0, so no wall is ever marked "near the wand" and the hidden
// geometry is never drawn, whatever the renderer. The four displacements are moved by 4.
namespace render
{
constexpr DWORD kTimeDateStamp = 0x5EE7E75C;
constexpr DWORD kSizeOfImage = 0x7C000;

struct Site
{
	size_t rva;         // of the instruction (image base 0x10B00000: a file address minus that)
	uint8_t opcode[2];  // its first two bytes, before the disp32
	uint32_t wrong;     // the displacement it holds
	uint32_t right;     // the one the engine's actor has
};
constexpr Site kSites[] = {
	{ 0x22CA4, { 0x8A, 0x81 }, 0x94C, 0x950 },   // mov al, [ecx+bLumosOn]       pass: the switch
	{ 0x22D47, { 0x8B, 0x91 }, 0x950, 0x954 },   // mov edx, [ecx+fLumosRadius]  pass: the radius
	{ 0x2663F, { 0xF6, 0x81 }, 0x94C, 0x950 },   // test byte [ecx+bLumosOn], 1  draw: hidden geometry
	{ 0x267C1, { 0xD9, 0x81 }, 0x950, 0x954 },   // fld [ecx+fLumosRadius]       draw: the fade
};

// Every site holds the instruction read, with the wrong displacement. Pure: the header is
// checked apart (Recognised).
inline bool SitesMatch(const uint8_t* image)
{
	for (const Site& s : kSites)
	{
		uint32_t disp;
		std::memcpy(&disp, image + s.rva + 2, sizeof(disp));
		if (image[s.rva] != s.opcode[0] || image[s.rva + 1] != s.opcode[1] || disp != s.wrong)
			return false;
	}
	return true;
}

// The loaded Render.dll is the build read.
inline bool Recognised(const uint8_t* image)
{
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		return false;
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
	return nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.TimeDateStamp == kTimeDateStamp
		&& nt->OptionalHeader.SizeOfImage == kSizeOfImage && SitesMatch(image);
}

// The four displacements, moved. Same instruction lengths: nothing else moves.
inline void Patch(uint8_t* image)
{
	for (const Site& s : kSites)
		std::memcpy(image + s.rva + 2, &s.right, sizeof(s.right));
}
}

// A surface the fix acts on: marked by the map, and an Alpha the engine computed (1..254). Alpha
// 0 (a wall far from the wand) and 255 (anything else) are drawn as the renderer does.
inline bool Fades(DWORD polyFlags, int alpha)
{
	return (polyFlags & kMarked) != 0 && alpha > 0 && alpha < 255;
}

// The blend factor: how much of the surface is kept.
inline float Opacity(int alpha)
{
	return 1.0f - static_cast<float>(alpha) / 255.0f;
}

// The loaded d3d11drv.dll is the build read: same header, same DrawComplexSurface in the slot,
// same first bytes. Another build changes nothing (its offsets would be wrong).
inline bool Recognised(const uint8_t* image)
{
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		return false;
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp != kTimeDateStamp
		|| nt->OptionalHeader.SizeOfImage != kSizeOfImage)
		return false;
	const uint8_t* slot = image + kVtable + kDrawComplexSurfaceSlot * sizeof(void*);
	uintptr_t held;
	std::memcpy(&held, slot, sizeof(held));
	return held == reinterpret_cast<uintptr_t>(image + kDrawComplexSurface)
		&& std::memcmp(image + kDrawComplexSurface, kPrologue, sizeof(kPrologue)) == 0;
}
}
