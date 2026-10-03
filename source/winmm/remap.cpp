// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.

#include "remap.h"

namespace remap
{
namespace
{
constexpr DWORD kMiddle = 32767;
constexpr DWORD kTriggerButton = 16384;   // a quarter of the full travel (0 to 65535)

// Xbox button bit -> PlayStation button bit: A cross, B circle, X square, Y triangle, LB L1,
// RB R1, Back Share, Start Options, L3 L3, R3 R3.
constexpr int kXboxToPlayStation[10] = { 1, 2, 0, 3, 4, 5, 8, 9, 10, 11 };

template <class Caps> void Rewrite(Caps& caps)
{
	// Right stick horizontal moves from U to Z; U and V become the triggers, 0 at rest.
	caps.wZmin = caps.wUmin;
	caps.wZmax = caps.wUmax;
	caps.wUmin = caps.wVmin = 0;
	caps.wUmax = caps.wVmax = 65535;
	caps.wCaps |= JOYCAPS_HASZ | JOYCAPS_HASR | JOYCAPS_HASU | JOYCAPS_HASV;
	caps.wNumAxes = caps.wMaxAxes = 6;
	caps.wNumButtons = 14;
	if (caps.wMaxButtons < 14)
		caps.wMaxButtons = 14;
}
}

Kind Identify(WORD manufacturer, UINT buttons, UINT caps)
{
	if (manufacturer == 0x054C)
		return Kind::PlayStation;
	const UINT axes = JOYCAPS_HASZ | JOYCAPS_HASR | JOYCAPS_HASU;
	if (buttons == 10 && (caps & axes) == axes && !(caps & JOYCAPS_HASV) && (caps & JOYCAPS_HASPOV))
		return Kind::Xbox;
	return Kind::Other;
}

void AsPlayStation(JOYCAPSA& caps) { Rewrite(caps); }
void AsPlayStation(JOYCAPSW& caps) { Rewrite(caps); }

void AsPlayStation(JOYINFOEX& info)
{
	const DWORD triggers = info.dwZpos;
	const DWORD left = triggers > kMiddle ? (triggers - kMiddle) * 2 : 0;
	const DWORD right = triggers < kMiddle ? (kMiddle - triggers) * 2 : 0;
	info.dwZpos = info.dwUpos;
	info.dwVpos = left > 65535 ? 65535 : left;
	info.dwUpos = right > 65535 ? 65535 : right;

	DWORD buttons = 0;
	for (int i = 0; i < 10; ++i)
		if (info.dwButtons & (1u << i))
			buttons |= 1u << kXboxToPlayStation[i];
	if (left > kTriggerButton)
		buttons |= kL2;
	if (right > kTriggerButton)
		buttons |= kR2;
	info.dwButtons = buttons;
	info.dwButtonNumber = CountButtons(buttons);
}

DWORD CountButtons(DWORD buttons)
{
	DWORD n = 0;
	for (; buttons; buttons &= buttons - 1)
		++n;
	return n;
}
}
