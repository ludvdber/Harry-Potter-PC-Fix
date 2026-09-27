// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// PlayStation controllers seen as XInput pads. Pure part: what an input report of a DualShock 4
// or a DualSense says, in the words of an Xbox pad, and the output report that sets rumble and
// light bar. tests/xinput_test.cpp checks it without a controller.

#pragma once
#include <cstddef>
#include <cstdint>

namespace pad
{
// The XInput structures, same layout as <Xinput.h> (which is not included: our exports carry the
// same names as the functions it declares).
struct Gamepad
{
	uint16_t buttons;
	uint8_t leftTrigger, rightTrigger;
	int16_t thumbLX, thumbLY, thumbRX, thumbRY;
};

enum : uint16_t
{
	DpadUp = 0x0001, DpadDown = 0x0002, DpadLeft = 0x0004, DpadRight = 0x0008,
	Start = 0x0010, Back = 0x0020, LeftThumb = 0x0040, RightThumb = 0x0080,
	LeftShoulder = 0x0100, RightShoulder = 0x0200, Guide = 0x0400,
	A = 0x1000, B = 0x2000, X = 0x4000, Y = 0x8000,
};

constexpr uint16_t kSony = 0x054C;
bool IsDualShock4(uint16_t pid);   // 05C4, 09CC, 0BA0 (wireless adapter)
bool IsDualSense(uint16_t pid);    // 0CE6, 0DF2 (Edge)

// One input report (report id first) -> gamepad. False if the report is not one we read.
bool Parse(uint16_t pid, const uint8_t* report, size_t size, Gamepad& out);

// The output report for rumble (0-255 each, big and small motor) and light bar. `outputSize` is
// the device's output report length: 32 (DS4 USB) or 48 (DualSense USB). 0 if not handled
// (Bluetooth output needs a checksum: not written yet).
size_t Output(uint16_t pid, size_t outputSize, uint8_t bigMotor, uint8_t smallMotor, const uint8_t rgb[3],
	bool setLight, uint8_t* out, size_t capacity);
}
