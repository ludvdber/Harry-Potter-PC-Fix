// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
// Checks pad.cpp without a controller: DualShock 4 and DualSense input reports read as an Xbox
// pad (USB and Bluetooth layouts), and the output report that carries rumble and light bar.
// The DS4 USB layout was also checked on a real pad (054C:09CC) on 2026-09-27.
#include "../source/xinput/pad.h"
#include <cstdio>
#include <cstring>

static int failures = 0;
static void Expect(bool ok, const char* what)
{
	if (!ok)
		printf("FAIL %s\n", what);
	failures += !ok;
}

// The seven bytes both layouts share, at rest: sticks centred, hat released (8), nothing pressed.
static void Rest(uint8_t* sticks, uint8_t* buttons)
{
	sticks[0] = sticks[1] = sticks[2] = sticks[3] = 128;
	buttons[0] = 0x08;
	buttons[1] = buttons[2] = 0;
}

static void Ds4Usb()
{
	uint8_t r[64] = { 0x01 };
	Rest(r + 1, r + 5);
	pad::Gamepad g{};
	Expect(pad::Parse(0x09CC, r, sizeof(r), g), "DS4 USB parsed");
	Expect(g.buttons == 0 && g.thumbLX == 0 && g.thumbLY == 0 && g.thumbRX == 0 && g.thumbRY == 0,
		"DS4 at rest reads as a pad at rest");

	r[1] = 0;     // left stick fully left
	r[2] = 0;     // and fully up
	r[3] = 255;   // right stick fully right
	r[4] = 255;   // and fully down
	r[5] = 0x20 | 0x02;   // cross, hat right
	r[6] = 0x01 | 0x20;   // L1, Options
	r[7] = 0x01;          // PS
	r[8] = 255;           // L2
	r[9] = 40;            // R2
	Expect(pad::Parse(0x09CC, r, sizeof(r), g), "DS4 USB parsed again");
	Expect(g.thumbLX == -32768 && g.thumbLY == 32767, "left stick: left and up (XInput's Y points up)");
	Expect(g.thumbRX == 32639 && g.thumbRY == -32639, "right stick: right and down");
	Expect(g.buttons == (pad::A | pad::DpadRight | pad::LeftShoulder | pad::Start | pad::Guide),
		"cross = A, hat right, L1 = left shoulder, Options = Start, PS = Guide");
	Expect(g.leftTrigger == 255 && g.rightTrigger == 40, "analogue triggers");
}

static void Ds4Buttons()
{
	struct { uint8_t b0, b1; uint16_t want; const char* what; } cases[] = {
		{ 0x18, 0, pad::X, "square = X" },
		{ 0x48, 0, pad::B, "circle = B" },
		{ 0x88, 0, pad::Y, "triangle = Y" },
		{ 0x08, 0x02, pad::RightShoulder, "R1" },
		{ 0x08, 0x10, pad::Back, "Share = Back" },
		{ 0x08, 0x40, pad::LeftThumb, "L3" },
		{ 0x08, 0x80, pad::RightThumb, "R3" },
		{ 0x00, 0, pad::DpadUp, "hat 0 = up" },
		{ 0x01, 0, pad::DpadUp | pad::DpadRight, "hat 1 = up right" },
		{ 0x05, 0, pad::DpadDown | pad::DpadLeft, "hat 5 = down left" },
		{ 0x07, 0, pad::DpadLeft | pad::DpadUp, "hat 7 = up left" },
		{ 0x0F, 0, 0, "hat 15 = released" },
	};
	for (const auto& c : cases)
	{
		uint8_t r[64] = { 0x01 };
		Rest(r + 1, r + 5);
		r[5] = c.b0;
		r[6] = c.b1;
		pad::Gamepad g{};
		Expect(pad::Parse(0x05C4, r, sizeof(r), g) && g.buttons == c.want, c.what);
	}
}

static void Ds4Bluetooth()
{
	uint8_t shortReport[10] = { 0x01 };
	Rest(shortReport + 1, shortReport + 5);
	shortReport[5] = 0x28;   // cross
	pad::Gamepad g{};
	Expect(pad::Parse(0x09CC, shortReport, sizeof(shortReport), g) && g.buttons == pad::A,
		"DS4 Bluetooth short report");

	uint8_t full[78] = { 0x11, 0xC0, 0x00 };
	Rest(full + 3, full + 7);
	full[7] = 0x48;   // circle
	full[10] = 99;    // L2
	Expect(pad::Parse(0x09CC, full, sizeof(full), g) && g.buttons == pad::B && g.leftTrigger == 99,
		"DS4 Bluetooth full report, two bytes later");
}

static void DualSense()
{
	uint8_t usb[64] = { 0x01 };
	usb[1] = usb[2] = usb[3] = usb[4] = 128;
	usb[5] = 12;     // L2
	usb[6] = 200;    // R2
	usb[7] = 0x55;   // a counter, not a button
	usb[8] = 0x88;   // triangle, hat released
	usb[9] = 0x02;   // R1
	usb[10] = 0x01;  // PS
	pad::Gamepad g{};
	Expect(pad::Parse(0x0CE6, usb, sizeof(usb), g), "DualSense USB parsed");
	Expect(g.buttons == (pad::Y | pad::RightShoulder | pad::Guide), "DualSense buttons");
	Expect(g.leftTrigger == 12 && g.rightTrigger == 200, "DualSense triggers come before the buttons");

	uint8_t bt[78] = { 0x31, 0x00 };
	memcpy(bt + 2, usb + 1, 10);
	Expect(pad::Parse(0x0DF2, bt, sizeof(bt), g) && g.buttons == (pad::Y | pad::RightShoulder | pad::Guide) &&
		g.rightTrigger == 200, "DualSense Bluetooth 0x31, one byte later");

	uint8_t shortBt[10] = { 0x01 };
	Rest(shortBt + 1, shortBt + 5);
	shortBt[5] = 0x28;
	shortBt[8] = 77;
	Expect(pad::Parse(0x0CE6, shortBt, sizeof(shortBt), g) && g.buttons == pad::A && g.leftTrigger == 77,
		"DualSense Bluetooth short report has the DS4 layout");
}

static void Refused()
{
	uint8_t r[64] = { 0x05 };
	pad::Gamepad g{};
	Expect(!pad::Parse(0x09CC, r, sizeof(r), g), "an unknown report id is not read");
	r[0] = 0x01;
	Expect(!pad::Parse(0x09CC, r, 5, g), "a truncated report is not read");
	Expect(!pad::Parse(0x0268, r, sizeof(r), g), "a DualShock 3 is not ours");
	Expect(!pad::Parse(0x09CC, nullptr, 64, g), "no report");
}

static void Output()
{
	const uint8_t gold[3] = { 255, 110, 0 };
	uint8_t out[64];
	memset(out, 0xEE, sizeof(out));
	Expect(pad::Output(0x09CC, 32, 200, 50, gold, true, out, sizeof(out)) == 32, "DS4 USB output is 32 bytes");
	Expect(out[0] == 0x05 && out[1] == 0x07, "report 5, rumble + light bar + flash flags");
	Expect(out[4] == 50 && out[5] == 200, "small motor then big motor");
	Expect(out[6] == 255 && out[7] == 110 && out[8] == 0, "light bar colour");
	Expect(out[9] == 0 && out[10] == 0 && out[31] == 0, "no flashing, rest cleared");

	Expect(pad::Output(0x09CC, 32, 1, 2, gold, false, out, sizeof(out)) == 32 && out[1] == 0x01 && out[6] == 0,
		"rumble alone leaves the light bar untouched");

	Expect(pad::Output(0x0CE6, 48, 200, 50, gold, true, out, sizeof(out)) == 48, "DualSense USB output is 48 bytes");
	Expect(out[0] == 0x02 && out[1] == 0x03 && out[2] == 0x04, "report 2, rumble and light bar flags");
	Expect(out[3] == 50 && out[4] == 200, "DualSense motors");
	Expect(out[45] == 255 && out[46] == 110 && out[47] == 0, "DualSense light bar");

	Expect(pad::Output(0x09CC, 78, 1, 1, gold, true, out, sizeof(out)) == 0, "Bluetooth output: not written");
	Expect(pad::Output(0x09CC, 32, 1, 1, gold, true, out, 16) == 0, "too small a buffer: not written");
}

int main()
{
	Expect(pad::IsDualShock4(0x05C4) && pad::IsDualShock4(0x09CC) && pad::IsDualShock4(0x0BA0), "DS4 ids");
	Expect(pad::IsDualSense(0x0CE6) && pad::IsDualSense(0x0DF2) && !pad::IsDualSense(0x09CC), "DualSense ids");
	Ds4Usb();
	Ds4Buttons();
	Ds4Bluetooth();
	DualSense();
	Refused();
	Output();
	printf(failures ? "%d failure(s)\n" : "xinput: all tests passed\n", failures);
	return failures ? 1 : 0;
}
