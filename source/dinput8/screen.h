// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// The frame counter, the benchmark and the screenshot key of the d3d9.dll's overlay, for HP3,
// which has no d3d9.dll of ours: the game draws with Direct3D 8 through its D3DDrv.dll, and this
// DLL is already in the process (WinDrv.dll imports it). screen.cpp says how it reaches the frames.
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace screen
{
struct Settings
{
	// [Accio.Overlay]
	bool showFps = false, showFrameTime = false, showGraph = false;
	int overlayKey = VK_F10;      // shows or hides the panel
	int benchmarkKey = VK_F11;    // starts and stops a recording
	int position = 1;             // 1 top left, 2 top right, 3 bottom left, 4 bottom right
	int size = 100;               // percent
	// [Accio.Window]
	int screenshotKey = VK_F12;   // 0 = none
	wchar_t screenshotFolder[MAX_PATH] = L"";   // empty = screenshots\ next to the DLL
};

// Read from dinput8.ini (same keys as d3d9.ini).
void LoadSettings(const wchar_t* ini, Settings& s);

// Anything to do at all (a line, a key)?
bool Wanted(const Settings& s);

// Called once from DirectInput8Create, on the game's thread: Direct3D 8 is already loaded (the
// game's exe imports it); its IDirect3D8 class gets CreateDevice watched, and every device made
// from then on gets Present and Reset watched. Returns false when nothing could be watched.
bool Install(const Settings& s);
}
