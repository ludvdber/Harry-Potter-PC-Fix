// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// The frame of HP1 and HP2, as d3d9.dll has it for the other games: the performance panel
// ([Accio.Overlay]), the benchmark, the screenshot key and the frame-rate limit ([Accio.Window]),
// on the renderer's Unlock (frame.h). Direct3D 11, the renderer's own device and swap chain.

#pragma once

#include "frame.h"
#include <windows.h>
#include <cstdint>

// winmm.cpp's log, for this module.
void Log(const char* fmt, ...);

namespace overlay11
{
struct Settings
{
	bool showFps = false, showFrameTime = false, showGraph = false, showCpu = false, showGpu = false;
	bool showVram = false, showRam = false, showLatency = false;
	UINT overlayKey = VK_F10, benchmarkKey = VK_F11, screenshotKey = VK_F12;
	int position = 1;   // corner: 1 top left, 2 top right, 3 bottom left, 4 bottom right
	int size = 100;     // percent
	int fpsLimit = 0;   // 0 = none
	wchar_t screenshotFolder[MAX_PATH] = L"";
};
extern Settings g_cfg;

// [Accio.Overlay] and the frame keys of [Accio.Window], the same names as d3d9.ini.
void Load(const wchar_t* ini);
// True when any of it is switched on: the renderer's Unlock is then taken.
bool Wanted();
// Once the renderer is loaded and recognised (frame.h): its Unlock slot is pointed at the relay.
void Hook(uint8_t* renderer, const frame::Build& build);
// A key went down on the game's thread (latency: the time to the frame that follows).
void NoteKeyPressed();
}
