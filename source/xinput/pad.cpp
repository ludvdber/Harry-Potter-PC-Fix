// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// DualShock 4 and DualSense reports read as an Xbox pad. Layouts of the public HID reports:
//   DS4 USB 0x01 and Bluetooth 0x01 (the short report before the long one is asked for): from
//   byte 1, left X, left Y, right X, right Y, hat + face buttons, shoulder / stick / menu buttons,
//   PS + pad click, then L2 and R2 at bytes 8 and 9. DS4 Bluetooth 0x11: the same, two bytes later.
//   DualSense USB 0x01 and Bluetooth 0x31 (one byte later): sticks, L2, R2, a counter, then the three
//   button bytes. DualSense Bluetooth 0x01 (short): the DS4 layout.
// Sticks go 0 (left, up) to 255; XInput's Y axis points up, hence the sign.

#include "pad.h"
#include <cstring>

namespace pad
{
bool IsDualShock4(uint16_t pid) { return pid == 0x05C4 || pid == 0x09CC || pid == 0x0BA0; }
bool IsDualSense(uint16_t pid) { return pid == 0x0CE6 || pid == 0x0DF2; }

namespace
{
int16_t Axis(uint8_t v, bool flip)
{
	int x = (static_cast<int>(v) - 128) * 257;   // -32896 .. 32639, and 0 at rest
	if (flip)
		x = -x;
	if (x > 32767) x = 32767;
	if (x < -32768) x = -32768;
	return static_cast<int16_t>(x);
}

uint16_t Hat(uint8_t hat)
{
	static const uint16_t dirs[8] = { DpadUp, DpadUp | DpadRight, DpadRight, DpadRight | DpadDown,
		DpadDown, DpadDown | DpadLeft, DpadLeft, DpadLeft | DpadUp };
	return hat < 8 ? dirs[hat] : 0;
}

// The three button bytes, the same bits on both controllers.
uint16_t Buttons(uint8_t b0, uint8_t b1, uint8_t b2)
{
	uint16_t w = Hat(b0 & 0x0F);
	if (b0 & 0x10) w |= X;          // square
	if (b0 & 0x20) w |= A;          // cross
	if (b0 & 0x40) w |= B;          // circle
	if (b0 & 0x80) w |= Y;          // triangle
	if (b1 & 0x01) w |= LeftShoulder;
	if (b1 & 0x02) w |= RightShoulder;
	if (b1 & 0x10) w |= Back;       // Share / Create
	if (b1 & 0x20) w |= Start;      // Options
	if (b1 & 0x40) w |= LeftThumb;
	if (b1 & 0x80) w |= RightThumb;
	if (b2 & 0x01) w |= Guide;      // PS
	return w;
}

// Layout A (DS4, short reports): sticks at +0..+3, buttons at +4..+6, triggers at +7, +8.
bool LayoutA(const uint8_t* d, Gamepad& out)
{
	out.thumbLX = Axis(d[0], false);
	out.thumbLY = Axis(d[1], true);
	out.thumbRX = Axis(d[2], false);
	out.thumbRY = Axis(d[3], true);
	out.buttons = Buttons(d[4], d[5], d[6]);
	out.leftTrigger = d[7];
	out.rightTrigger = d[8];
	return true;
}

// Layout B (DualSense long reports): sticks at +0..+3, triggers at +4, +5, buttons at +7..+9.
bool LayoutB(const uint8_t* d, Gamepad& out)
{
	out.thumbLX = Axis(d[0], false);
	out.thumbLY = Axis(d[1], true);
	out.thumbRX = Axis(d[2], false);
	out.thumbRY = Axis(d[3], true);
	out.leftTrigger = d[4];
	out.rightTrigger = d[5];
	out.buttons = Buttons(d[7], d[8], d[9]);
	return true;
}
}

bool Parse(uint16_t pid, const uint8_t* r, size_t n, Gamepad& out)
{
	if (!r || n < 10)
		return false;
	if (IsDualShock4(pid))
	{
		if (r[0] == 0x01)
			return LayoutA(r + 1, out);
		if (r[0] == 0x11 && n >= 12)
			return LayoutA(r + 3, out);
		return false;
	}
	if (IsDualSense(pid))
	{
		if (r[0] == 0x01 && n >= 64)   // USB: the long report
			return LayoutB(r + 1, out);
		if (r[0] == 0x01)              // Bluetooth, short report
			return LayoutA(r + 1, out);
		if (r[0] == 0x31 && n >= 12)
			return LayoutB(r + 2, out);
	}
	return false;
}

size_t Output(uint16_t pid, size_t outputSize, uint8_t big, uint8_t small, const uint8_t rgb[3], bool setLight,
	uint8_t* out, size_t capacity)
{
	if (IsDualShock4(pid) && outputSize == 32 && capacity >= 32)
	{
		memset(out, 0, 32);
		out[0] = 0x05;
		out[1] = static_cast<uint8_t>(0x01 | (setLight ? 0x02 | 0x04 : 0));   // rumble, light bar, flash (off)
		out[4] = small;
		out[5] = big;
		if (setLight)
		{
			out[6] = rgb[0];
			out[7] = rgb[1];
			out[8] = rgb[2];
		}
		return 32;
	}
	if (IsDualSense(pid) && outputSize == 48 && capacity >= 48)
	{
		memset(out, 0, 48);
		out[0] = 0x02;
		out[1] = 0x01 | 0x02;          // valid_flag0: compatible rumble, haptics select
		out[2] = setLight ? 0x04 : 0;  // valid_flag1: light bar
		out[3] = small;                // right (small) motor
		out[4] = big;                  // left (big) motor
		if (setLight)
		{
			out[45] = rgb[0];
			out[46] = rgb[1];
			out[47] = rgb[2];
		}
		return 48;
	}
	return 0;
}
}
