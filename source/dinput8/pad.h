// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// What dinput8.dll changes in a pad's state, without DirectInput calls: testable without a pad
// (tests/dinput8_test.cpp).
//
// HP3 reads its pad through DirectInput 8 (WinDrv.dll, UpdateInput): DIJOYSTATE2, the six axes lX
// lY lZ lRx lRy lRz as JoyX JoyY JoyZ JoyR JoyU JoyV, buttons 1 to 12 as Joy1 to Joy12. Its
// User.ini binds them by NUMBER. The numbers the launcher binds are a PlayStation pad's, as
// DirectInput shows a DualShock 4 (read on Ludo's pad, 054C:09CC, 2026-10-08): square 1, cross 2,
// circle 3, triangle 4, L1 5, R1 6, L2 7, R2 8, Share 9, Options 10, L3 11, R3 12; left stick
// lX lY, right stick lZ (horizontal) and lRz (vertical), L2 on lRx and R2 on lRy (lowest at rest).
//
// An Xbox pad (every XInput pad, through Windows' own HID mapping) has another layout: A 1, B 2,
// X 3, Y 4, LB 5, RB 6, Back 7, Start 8, L3 9, R3 10; right stick lRx (horizontal) and lRy
// (vertical); both triggers on lZ (left trigger above the middle, right trigger below).
// AsPlayStation turns it into the layout above, so one set of bindings serves both.
// NOT SEEN with an Xbox pad: the layout is Windows' documented one, the trigger directions are
// the ones winmm.dll uses for the same pads (remap.h). The log names each pad and the layout chosen.
//
// No dead zone anywhere else: the game sets no DIPROP_DEADZONE, and its binding parser applies
// DeadZone= only to SpeedBase= bindings, which ignore the axis value (Engine.dll, 0x1042ad7e).
// A stick at rest is never exactly in the middle (Ludo's: up to +0.8 %): the character crept.

#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

namespace dpad
{
enum class Kind { Other, Xbox, PlayStation };

// PlayStation-layout buttons, as indexes of rgbButtons.
constexpr int kL2 = 6;
constexpr int kR2 = 7;
constexpr int kShare = 8;
constexpr int kOptions = 9;

struct Range
{
	LONG min = 0;
	LONG max = 0xFFFF;   // DirectInput's default
};

// The range of each axis, as the game set it (asked once per pad).
struct Ranges
{
	Range x, y, z, rx, ry, rz;
};

// From what DirectInput says of the pad: Sony's vendor id (guidProduct.Data1 is
// MAKELONG(vendor, product)), or the layout every XInput pad has (5 axes, 10 buttons, a hat).
Kind Identify(DWORD productData1, DWORD axes, DWORD buttons, DWORD povs);

// The state of an Xbox pad in the PlayStation layout. A trigger pressed past a quarter of its
// travel is also its button (L2, R2), as on a PlayStation pad.
void AsPlayStation(DIJOYSTATE& state, const Ranges& ranges);

// Both sticks, where `kind` has them (Xbox: lX lY, lRx lRy; otherwise lX lY, lZ lRz), BEFORE
// AsPlayStation moves them, each against its own range: within `percent` of the middle, the
// middle; beyond, scaled so the edge is still the edge. 0 = left as it is.
void DeadZone(DIJOYSTATE& state, const Ranges& ranges, int percent, Kind kind);
}
