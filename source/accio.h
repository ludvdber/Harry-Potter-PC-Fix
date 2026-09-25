// Accio Launcher - PC fix for the EA Harry Potter games (HP4, HP5, HP6).
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// Shared declarations. One DLL, named d3d9.dll, sits next to the game executable: Windows
// loads it instead of the system Direct3D 9, and it forwards everything to the real one.
// Nothing is wrapped: the few Direct3D and DirectInput methods the fix needs are redirected
// in the objects' method tables (see hooks.h), and the game keeps the real objects.

#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>

// ---------------------------------------------------------------------------------------------
// Settings (settings.cpp). Read once from d3d9.ini next to this DLL, at load time.
// ---------------------------------------------------------------------------------------------
struct Settings
{
	// [Accio.Window]
	bool  windowed = true;          // borderless window instead of exclusive full screen
	int   windowStyle = 1;          // 1 borderless full screen, 2 window, 3 resizable, 4 borderless window
	bool  centerWindow = false;
	bool  primaryMonitor = false;
	bool  alwaysOnTop = false;
	bool  keepRunning = true;       // keeps the game running when it loses focus
	bool  dpiAware = false;
	int   fpsLimit = 0;
	bool  showFps = false;
	int   screenshotKey = VK_F12;
	bool  retakeInput = true;       // keyboard and mouse taken back when the game returns to the front
	bool  releaseStaleKeys = true;  // keys released while away are released for the game too

	// [Accio.Game] - patches in the game executable, see game.cpp
	int   width = 0, height = 0;    // the game's own default resolution
	float aspectRatio = 0.0f;       // 0 = the game's own
	float fovScale = 0.0f;          // 0 = the game's own
	int   animationRate = 0;        // HP4: 0 = the game's own (20)
	int   frameRateCap = -1;        // -1 = this game's default, 0 = the game's own
	int   unlockFrameRate = -1;     // HP5 / HP6: -1 = this game's default
	int   hazeOverlay = -1;         // HP4: 0 = skipped (default), 1 = drawn with the column cap, 2 = as shipped

	// The same choices in the file format used before 2026-09-26, kept so that a player's old
	// d3d9.ini goes on working. Their meaning depends on the game; game.cpp translates them.
	int   legacyAspectIndex = 0;
	int   legacyFov = 0;

	// [Accio.Graphics]
	bool  fxaa = false;
	float sharpness = 0.40f;
	int   msaa = 0;
	int   anisotropy = 0;
	float lodBias = 0.0f;
	int   ssaa = 1;
	int   shadowScale = 1;
	bool  vsync = false;
	bool  grading = false;
	float vibrance = 0.15f, vignette = 0.0f, lift = 0.0f, gamma = 1.0f, gain = 1.0f;
	float temperature = 0.0f, tint = 0.0f, contrast = 0.0f, splitTone = 0.0f;
	bool  ssao = false;
	float ssaoStrength = 0.5f, ssaoRadius = 6.0f, ssaoMinDelta = 0.0005f, ssaoMaxDelta = 0.05f;
	bool  bloom = false;
	float bloomStrength = 0.35f, bloomThreshold = 0.75f;
	bool  godRays = false;
	float godRaysStrength = 0.45f, godRaysDecay = 0.96f;
	int   renderWidth = 0, renderHeight = 0;
	bool  generateMipmaps = true;   // full mipmap chains for textures shipped without
	bool  forceTrilinear = true;    // mipmaps used on every texture
	int   maxFrameLatency = 1;      // frames the driver may queue; 0 = its own choice
};

extern Settings g_cfg;
void LoadSettings(const char* iniPath);

// ---------------------------------------------------------------------------------------------
// Log (log.cpp). d3d9_accio.log next to the DLL, rewritten at each start. Every line carries
// the time in ms since boot: without it a line written at exit once passed for one written at
// an Alt+Tab, and a whole evening went into explaining a silence that never happened.
// ---------------------------------------------------------------------------------------------
void OpenLog(HMODULE self);
void Log(const char* fmt, ...);

// ---------------------------------------------------------------------------------------------
// Module-wide state
// ---------------------------------------------------------------------------------------------
extern HMODULE g_self;
extern HWND    g_gameWindow;              // the window the game gave Direct3D
extern volatile LONG g_frames;            // frames presented so far

// Our own Direct3D calls go through the same method tables the game uses. While this counter is
// non-zero, every redirected method passes straight to Direct3D (see InternalCalls in hooks.h).
extern thread_local int g_internal;

// ---------------------------------------------------------------------------------------------
// Entry points between modules
// ---------------------------------------------------------------------------------------------
// direct3d.cpp
IDirect3D9* HookDirect3D9(IDirect3D9* d3d);

// window.cpp
void PrepareWindow(D3DPRESENT_PARAMETERS* pp, HWND focusWindow);
void InstallFocusHooks();

// input.cpp
void InstallInputHooks();
bool ProcessInForeground();

// keys.cpp
void LoadKeyMap(const char* iniPath);
void RemapKeyboardState(BYTE* keys);

// game.cpp
void ApplyGamePatches();

// present.cpp
void BeforePresent(IDirect3DDevice9* dev);
void OnDeviceLost();
void OnDeviceRestored(IDirect3DDevice9* dev);

// effects.cpp
void RunPostEffects(IDirect3DDevice9* dev);
void RunAmbientOcclusionPass(IDirect3DDevice9* dev);
void ReleaseEffects();
