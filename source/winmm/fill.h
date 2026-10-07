// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// When winmm.dll takes the frame off the game's window, without Windows calls: testable without a
// game (tests/winmm_test.cpp).
//
// HP1 and HP2 (Unreal Engine 1) open a window WITH a title bar and border around a picture of
// [WinDrv.WindowsClient] WindowedViewportX/Y. Its renderer (d3d11drv.dll) has no borderless mode:
// "Borderless" is not a word it contains. With a picture as large as the screen, the window was
// 2578x1487 on a 2560x1440 screen at 125% (SEEN on HP1, 2026-10-07): 47 px below the bottom edge,
// under the taskbar. Taking the frame off and putting the window on the screen's rectangle gives
// the full screen picture, without the change of display mode that crashes HP1 on Alt+Tab (SEEN in
// the same test: menu drawn at 2560x1440, nothing cut).

#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace fill
{
// The frame Windows puts around a window with a title bar.
constexpr LONG kFrame = WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;

// A top-level window with a title bar whose picture covers the whole screen: its frame can only
// hang over the edges. A smaller picture stays a normal window (the player chose that size).
inline bool Wanted(LONG style, bool topLevel, int clientWidth, int clientHeight, const RECT& screen)
{
	return topLevel && (style & WS_VISIBLE) && (style & WS_CAPTION) == WS_CAPTION
		&& clientWidth >= screen.right - screen.left && clientHeight >= screen.bottom - screen.top;
}

// The style without its frame: a pop-up window, everything else kept (visible, clip flags).
inline LONG Borderless(LONG style)
{
	return static_cast<LONG>(static_cast<DWORD>(style & ~kFrame) | WS_POPUP);
}
}
