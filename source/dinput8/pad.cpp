// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.

#include "pad.h"

namespace dpad
{
namespace
{
constexpr WORD kSony = 0x054C;

double Middle(const Range& r) { return (static_cast<double>(r.min) + r.max) / 2.0; }
double Half(const Range& r) { return (static_cast<double>(r.max) - r.min) / 2.0; }

LONG Clamp(double v, const Range& r)
{
	if (v < r.min)
		return r.min;
	if (v > r.max)
		return r.max;
	return static_cast<LONG>(v + (v >= 0 ? 0.5 : -0.5));
}

// How far a trigger is pressed, 0 to 1, from the shared axis: one side of the middle each.
double Trigger(LONG z, const Range& r, bool left)
{
	const double half = Half(r);
	if (half <= 0)
		return 0;
	const double d = (z - Middle(r)) / half;
	const double t = left ? d : -d;
	// The middle of 0..65535 falls between two values: a trigger at rest reads one of them.
	if (t < 0.01)
		return 0;
	return t > 1 ? 1 : t;
}

LONG FromTravel(double t, const Range& r) { return Clamp(r.min + t * (static_cast<double>(r.max) - r.min), r); }

void Zone(LONG& value, const Range& r, double zone)
{
	const double half = Half(r);
	if (half <= 0)
		return;
	const double mid = Middle(r);
	const double d = (value - mid) / half;
	const double a = d < 0 ? -d : d;
	if (a <= zone)
	{
		value = Clamp(mid, r);
		return;
	}
	const double scaled = (a - zone) / (1 - zone);
	value = Clamp(mid + (d < 0 ? -scaled : scaled) * half, r);
}
}

Kind Identify(DWORD productData1, DWORD axes, DWORD buttons, DWORD povs)
{
	if (LOWORD(productData1) == kSony)
		return Kind::PlayStation;
	if (axes == 5 && buttons == 10 && povs == 1)
		return Kind::Xbox;
	return Kind::Other;
}

void AsPlayStation(DIJOYSTATE& s, const Ranges& r)
{
	const double left = Trigger(s.lZ, r.z, true);
	const double right = Trigger(s.lZ, r.z, false);
	const LONG rightX = s.lRx;
	const LONG rightY = s.lRy;
	s.lZ = rightX;
	s.lRz = rightY;
	s.lRx = FromTravel(left, r.rx);
	s.lRy = FromTravel(right, r.ry);

	BYTE in[10];
	for (int i = 0; i < 10; ++i)
		in[i] = s.rgbButtons[i];
	BYTE out[14]{};
	out[0] = in[2];   // X -> square
	out[1] = in[0];   // A -> cross
	out[2] = in[1];   // B -> circle
	out[3] = in[3];   // Y -> triangle
	out[4] = in[4];   // LB -> L1
	out[5] = in[5];   // RB -> R1
	out[kL2] = left > 0.25 ? 0x80 : 0;
	out[kR2] = right > 0.25 ? 0x80 : 0;
	out[kShare] = in[6];     // Back
	out[kOptions] = in[7];   // Start
	out[10] = in[8];  // L3
	out[11] = in[9];  // R3
	for (int i = 0; i < 14; ++i)
		s.rgbButtons[i] = out[i];
}

void DeadZone(DIJOYSTATE& s, const Ranges& r, int percent, Kind kind)
{
	if (percent <= 0)
		return;
	const double zone = (percent >= 100 ? 99 : percent) / 100.0;
	Zone(s.lX, r.x, zone);
	Zone(s.lY, r.y, zone);
	if (kind == Kind::Xbox)
	{
		Zone(s.lRx, r.rx, zone);
		Zone(s.lRy, r.ry, zone);
	}
	else
	{
		Zone(s.lZ, r.z, zone);
		Zone(s.lRz, r.rz, zone);
	}
}
}
