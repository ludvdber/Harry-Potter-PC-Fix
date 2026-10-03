// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// What winmm.dll changes in the joystick answers, without Windows calls: testable without a pad
// (tests/winmm_test.cpp).
//
// HP2 reads its pad through WinMM, and its User.ini binds buttons by NUMBER. The numbers the
// launcher binds are a PlayStation pad's, as WinMM shows a DualShock 4 (read on Ludo's pad,
// 054C:09CC, 2026-09-27): square 1, cross 2, circle 3, triangle 4, L1 5, R1 6, L2 7, R2 8,
// Share 9, Options 10, L3 11, R3 12, PS 13, touch pad 14; right stick on Z (horizontal) and R
// (vertical); L2 on V and R2 on U, 0 at rest.
//
// An Xbox pad (every XInput pad, through Windows' own HID mapping) has another layout: A 1, B 2,
// X 3, Y 4, LB 5, RB 6, Back 7, Start 8, L3 9, R3 10; right stick on U (horizontal) and R
// (vertical); both triggers on Z (left trigger above the middle, right trigger below), no V.
// AsPlayStation turns it into the layout above, so one set of bindings serves both.
// NOT SEEN with an Xbox pad: the layout is Windows' documented one, the trigger directions are
// the usual reading of it. The log names each pad and the layout chosen.

#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

namespace remap
{
enum class Kind { Other, Xbox, PlayStation };

// PlayStation-layout buttons, as bits of JOYINFOEX::dwButtons.
constexpr DWORD kL2 = 1u << 6;
constexpr DWORD kR2 = 1u << 7;
constexpr DWORD kShare = 1u << 8;
constexpr DWORD kOptions = 1u << 9;

// From what WinMM says of the pad: Sony's vendor id, or the layout every XInput pad has
// (10 buttons, axes Z R U without V, a hat).
Kind Identify(WORD manufacturer, UINT buttons, UINT caps);

// The capabilities of an Xbox pad, rewritten as those of a PlayStation pad (14 buttons, six axes).
void AsPlayStation(JOYCAPSA& caps);
void AsPlayStation(JOYCAPSW& caps);

// The state of an Xbox pad in the PlayStation layout. A trigger pressed past a quarter of its
// travel is also its button (L2, R2), as on a PlayStation pad.
void AsPlayStation(JOYINFOEX& info);

// Number of buttons down, which JOYINFOEX carries beside the bits.
DWORD CountButtons(DWORD buttons);
}
