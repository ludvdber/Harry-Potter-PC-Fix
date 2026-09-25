// Accio Launcher - PC fix for the EA Harry Potter games.
// Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
//
// d3d9.ini. Our keys live in [Accio.Window], [Accio.Game] and [Accio.Graphics]. A key missing
// there is looked up where the file format used before 2026-09-26 kept it: players customised
// those files (colour grading, SSAO, their resolution), and a new DLL must not quietly put
// their game back to defaults.

#include "accio.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

Settings g_cfg;

namespace
{
const char* g_ini = nullptr;

bool Has(const char* section, const char* key)
{
	char probe[4];
	// A value no ini holds: if it comes back, the key is absent.
	GetPrivateProfileStringA(section, key, "\x7f", probe, sizeof(probe), g_ini);
	return !(probe[0] == '\x7f' && probe[1] == '\0');
}

// The first section in `sections` (nullptr-terminated) that holds the key.
const char* Find(const char* const* sections, const char* key)
{
	for (; *sections; sections++)
		if (Has(*sections, key))
			return *sections;
	return nullptr;
}

bool Text(const char* const* sections, const char* key, char* out, DWORD size)
{
	const char* section = Find(sections, key);
	if (!section)
		return false;
	GetPrivateProfileStringA(section, key, "", out, size, g_ini);
	// Comments on the same line are not ours to keep, but old files had them.
	for (char* p = out; *p; p++)
		if (*p == ';' || (p[0] == '/' && p[1] == '/')) { *p = '\0'; break; }
	return true;
}

int Int(const char* const* sections, const char* key, int fallback)
{
	char buf[64];
	return Text(sections, key, buf, sizeof(buf)) ? atoi(buf) : fallback;
}

bool Bool(const char* const* sections, const char* key, bool fallback)
{
	return Int(sections, key, fallback ? 1 : 0) != 0;
}

float Float(const char* const* sections, const char* key, float fallback)
{
	char buf[64];
	return Text(sections, key, buf, sizeof(buf)) ? static_cast<float>(atof(buf)) : fallback;
}

// "16:9", "21:9", "2.37" or 0.
float Ratio(const char* const* sections, const char* key)
{
	char buf[64];
	if (!Text(sections, key, buf, sizeof(buf)))
		return 0.0f;
	float w = 0, h = 0;
	if (sscanf_s(buf, "%f:%f", &w, &h) == 2 && w > 0 && h > 0)
		return w / h;
	const float r = static_cast<float>(atof(buf));
	return r > 0.5f && r < 10.0f ? r : 0.0f;
}

template <class T>
T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
}

void LoadSettings(const char* iniPath)
{
	g_ini = iniPath;
	Settings& c = g_cfg;

	static const char* const window[] = { "Accio.Window", nullptr };
	static const char* const windowOld[] = { "Accio.Window", "FORCEWINDOWED", "MAIN", nullptr };
	static const char* const game[] = { "Accio.Game", nullptr };
	static const char* const graphics[] = { "Accio.Graphics", "GRAPHICS", nullptr };
	static const char* const renderSize[] = { "Accio.Graphics", nullptr };

	// Window. Old names first where they differ from ours.
	static const char* const mainOld[] = { "MAIN", nullptr };
	static const char* const fwOld[] = { "FORCEWINDOWED", nullptr };
	c.windowed = Bool(window, "Windowed", Bool(mainOld, "ForceWindowedMode", true));
	c.windowStyle = Clamp(Int(window, "WindowStyle", Int(fwOld, "ForceWindowStyle", 1)), 0, 4);
	c.keepRunning = Bool(window, "KeepRunningInBackground", Bool(fwOld, "DoNotNotifyOnTaskSwitch", true));
	c.centerWindow = Bool(windowOld, "CenterWindow", false);
	c.primaryMonitor = Bool(windowOld, "UsePrimaryMonitor", false);
	c.alwaysOnTop = Bool(windowOld, "AlwaysOnTop", false);
	c.dpiAware = Bool(windowOld, "DPIAware", false);
	c.fpsLimit = Clamp(Int(windowOld, "FPSLimit", 0), 0, 1000);
	// Overlay. ShowFPS lived in [Accio.Window] before the overlay had its own section.
	static const char* const overlay[] = { "Accio.Overlay", nullptr };
	c.showFps = Bool(overlay, "ShowFPS", Bool(window, "ShowFPS", Bool(windowOld, "DisplayFPSCounter", false)));
	c.showFrameTime = Bool(overlay, "ShowFrameTime", false);
	c.showGraph = Bool(overlay, "ShowGraph", false);
	c.showCpu = Bool(overlay, "ShowCPU", false);
	c.showGpu = Bool(overlay, "ShowGPU", false);
	c.showVram = Bool(overlay, "ShowVRAM", false);
	c.showRam = Bool(overlay, "ShowRAM", false);
	c.showLatency = Bool(overlay, "ShowLatency", false);
	c.overlayKey = Clamp(Int(overlay, "OverlayKey", VK_F10), 0, 255);
	c.benchmarkKey = Clamp(Int(overlay, "BenchmarkKey", VK_F11), 0, 255);
	c.overlayPosition = Clamp(Int(overlay, "Position", 1), 1, 4);
	c.overlaySize = Clamp(Int(overlay, "Size", 100), 50, 300);
	c.screenshotKey = Clamp(Int(windowOld, "ScreenshotKey", VK_F12), 0, 255);
	c.retakeInput = Bool(window, "RetakeInputOnReturn", true);
	c.releaseStaleKeys = Bool(window, "ReleaseKeysOnReturn", true);

	// Game patches.
	static const char* const resOld[] = { "RESOLUTION", nullptr };
	c.width = Int(game, "Width", Int(resOld, "width", 0));
	c.height = Int(game, "Height", Int(resOld, "height", 0));
	c.aspectRatio = Ratio(game, "AspectRatio");
	c.fovScale = Clamp(Float(game, "FOV", 0.0f), 0.0f, 3.0f);
	c.animationRate = Int(game, "AnimationRate", 0);
	c.frameRateCap = Int(game, "FrameRateCap", -1);
	c.unlockFrameRate = Int(game, "UnlockFrameRate", -1);
	c.hazeOverlay = Int(game, "HazeOverlay", -1);
	static const char* const aspectOld[] = { "fullscreenaspectratio", nullptr };
	static const char* const fovOld[] = { "FOV", nullptr };
	static const char* const animOld[] = { "FPSANIMATIONS", nullptr };
	c.legacyAspectIndex = Int(aspectOld, "fullscreenaspectratio", 0);
	c.legacyFov = Int(fovOld, "fov", 0);
	if (!c.animationRate)
	{
		const int old = Int(animOld, "fpsanimations", 0);
		c.animationRate = old == 1 ? 25 : old == 2 ? 30 : 0;
	}

	// Image.
	c.fxaa = Bool(graphics, "FXAA", false);
	c.sharpness = Clamp(Float(graphics, "Sharpness", 0.40f), 0.0f, 1.0f);
	c.msaa = Clamp(Int(graphics, "Antialiasing", 0), 0, 16);
	c.anisotropy = Clamp(Int(graphics, "AnisotropicFiltering", 0), 0, 16);
	c.lodBias = Clamp(Float(graphics, "TextureLODBias", 0.0f), -3.0f, 3.0f);
	c.ssaa = Clamp(Int(graphics, "SSAAFactor", 1), 1, 4);
	c.shadowScale = Clamp(Int(graphics, "ShadowMapScale", 1), 1, 8);
	c.vsync = Bool(graphics, "VSync", false);
	c.grading = Bool(graphics, "ColorGrading", false);
	c.vibrance = Float(graphics, "Vibrance", 0.15f);
	c.vignette = Float(graphics, "Vignette", 0.0f);
	c.lift = Float(graphics, "Lift", 0.0f);
	c.gamma = Float(graphics, "Gamma", 1.0f);
	c.gain = Float(graphics, "Gain", 1.0f);
	c.temperature = Clamp(Float(graphics, "Temperature", 0.0f), -1.0f, 1.0f);
	c.tint = Clamp(Float(graphics, "Tint", 0.0f), -1.0f, 1.0f);
	c.contrast = Clamp(Float(graphics, "Contrast", 0.0f), 0.0f, 1.0f);
	c.splitTone = Clamp(Float(graphics, "SplitTone", 0.0f), 0.0f, 1.0f);
	c.ssao = Bool(graphics, "SSAO", false);
	c.ssaoStrength = Float(graphics, "SSAOStrength", 0.50f);
	c.ssaoRadius = Float(graphics, "SSAORadius", 6.0f);
	c.ssaoMinDelta = Float(graphics, "SSAOMinDelta", 0.0005f);
	c.ssaoMaxDelta = Float(graphics, "SSAOMaxDelta", 0.05f);
	c.bloom = Bool(graphics, "Bloom", false);
	c.bloomStrength = Float(graphics, "BloomStrength", 0.35f);
	c.bloomThreshold = Clamp(Float(graphics, "BloomThreshold", 0.75f), 0.0f, 1.0f);
	c.godRays = Bool(graphics, "GodRays", false);
	c.godRaysStrength = Float(graphics, "GodRaysStrength", 0.45f);
	c.godRaysDecay = Clamp(Float(graphics, "GodRaysDecay", 0.96f), 0.80f, 0.999f);
	// Only our own section: the old [RESOLUTION] Width/Height once forced the back buffer to
	// 1920x1080 behind the game's back, which is what gave characters a faint double.
	c.renderWidth = Int(renderSize, "RenderWidth", 0);
	c.renderHeight = Int(renderSize, "RenderHeight", 0);
	c.generateMipmaps = Bool(renderSize, "GenerateMipmaps", true);
	c.forceTrilinear = Bool(renderSize, "ForceTrilinear", true);
	c.maxFrameLatency = Clamp(Int(renderSize, "MaxFrameLatency", 1), 0, 16);

	Log("Settings from %s\n", iniPath);
	Log("  window: windowed=%d style=%d keepRunning=%d center=%d primary=%d onTop=%d dpiAware=%d fpsLimit=%d\n",
		c.windowed, c.windowStyle, c.keepRunning, c.centerWindow, c.primaryMonitor, c.alwaysOnTop, c.dpiAware, c.fpsLimit);
	Log("  game: %dx%d aspect=%.4f (old index %d) fov=%.3f (old %d) animations=%d frameRateCap=%d unlock=%d haze=%d\n",
		c.width, c.height, c.aspectRatio, c.legacyAspectIndex, c.fovScale, c.legacyFov, c.animationRate,
		c.frameRateCap, c.unlockFrameRate, c.hazeOverlay);
	Log("  image: FXAA=%d sharp=%.2f MSAA=%d AF=%d LOD=%.2f SSAA=%d shadows=%d vsync=%d grading=%d SSAO=%d bloom=%d rays=%d render=%dx%d\n",
		c.fxaa, c.sharpness, c.msaa, c.anisotropy, c.lodBias, c.ssaa, c.shadowScale, c.vsync, c.grading,
		c.ssao, c.bloom, c.godRays, c.renderWidth, c.renderHeight);
	Log("  mipmaps=%d trilinear=%d latency=%d retakeInput=%d releaseKeys=%d\n", c.generateMipmaps,
		c.forceTrilinear, c.maxFrameLatency, c.retakeInput, c.releaseStaleKeys);
}
