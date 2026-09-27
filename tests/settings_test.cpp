// Reads each game's shipped d3d9.ini with the DLL's own reader (settings.cpp) and checks the
// values that matter, so a misspelt key (read as its default, silently) fails the build. Then
// reads a file in the format used before 2026-09-26, the one players kept their tuning in.
#include "../source/accio.h"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

void Log(const char*, ...) {}

static int failures = 0;
static void Expect(bool ok, const char* game, const char* what)
{
	if (!ok)
		printf("FAIL %s: %s\n", game, what);
	failures += !ok;
}
static bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

#define EXPECT(cond) Expect((cond), game, #cond)

static void Load(const std::string& path)
{
	g_cfg = Settings{};
	LoadSettings(path.c_str());
}

int main(int argc, char** argv)
{
	if (argc < 3)
		return 2;
	const std::string data = argv[1]; // folder holding HP4, HP5, HP6, HP7a and HP7b
	const std::string scratch = argv[2];
	const Settings& c = g_cfg;

	// Common to the three shipped files.
	for (const char* game : { "HP4", "HP5", "HP6" })
	{
		Load(data + "\\" + game + "\\d3d9.ini");
		EXPECT(c.windowed && c.windowStyle == 1 && c.keepRunning);
		EXPECT(c.retakeInput && c.releaseStaleKeys);
		EXPECT(c.screenshotKey == VK_F12 && !c.showFps && c.compareKey == 0);
		EXPECT(c.width == 1920 && c.height == 1080);
		EXPECT(Near(c.fovScale, 0.0f));
		EXPECT(c.renderWidth == 0 && c.renderHeight == 0);
		EXPECT(c.generateMipmaps && c.forceTrilinear && c.maxFrameLatency == 1);
		EXPECT(c.mipmapFilter == 0 && !c.mipmapCoverage);
		EXPECT(c.legacyAspectIndex == 0 && c.legacyFov == 0);
	}

	{
		const char* game = "HP4";
		Load(data + "\\HP4\\d3d9.ini");
		EXPECT(c.fpsLimit == 120 && c.centerWindow && !c.dpiAware);
		EXPECT(Near(c.aspectRatio, 16.0f / 9));
		EXPECT(c.animationRate == 0 && c.frameRateCap == 120 && c.hazeOverlay == 0);
		EXPECT(!c.fxaa && c.msaa == 0 && !c.transparencyAa && c.anisotropy == 0 && !c.grading && !c.ssao && !c.bloom && !c.godRays);
	}
	{
		// The image HP5 has shipped with: Ludo's tuning, value for value.
		const char* game = "HP5";
		Load(data + "\\HP5\\d3d9.ini");
		EXPECT(c.fpsLimit == 120 && c.centerWindow && c.dpiAware);
		EXPECT(Near(c.aspectRatio, 0.0f) && c.unlockFrameRate == 1 && c.frameRateCap == 120 && c.hazeOverlay == -1);
		EXPECT(c.distanceFog == 1); // the switch is wired, the fog stays as shipped
		EXPECT(c.fxaa && Near(c.sharpness, 0.40f) && c.msaa == 16 && c.anisotropy == 16);
		EXPECT(Near(c.lodBias, -1.5f) && c.vsync && c.ssaa == 1 && c.shadowScale == 1);
		EXPECT(c.grading && Near(c.vibrance, 0.60f) && Near(c.vignette, 0.08f) && Near(c.lift, 0.0f));
		EXPECT(Near(c.gamma, 1.0f) && Near(c.gain, 1.05f) && Near(c.temperature, 0.0f) && Near(c.tint, 0.0f));
		EXPECT(Near(c.contrast, 0.30f) && Near(c.splitTone, 0.10f));
		EXPECT(Near(c.skinProtect, 0.70f) && Near(c.yellowRestraint, 1.0f));
		EXPECT(c.bloom && Near(c.bloomStrength, 0.35f) && Near(c.bloomThreshold, 0.75f));
		EXPECT(c.godRays && Near(c.godRaysStrength, 0.45f) && Near(c.godRaysDecay, 0.96f));
		EXPECT(c.ssao && Near(c.ssaoStrength, 0.55f) && Near(c.ssaoRadius, 6.0f));
		EXPECT(Near(c.ssaoMinDelta, 0.02f) && Near(c.ssaoMaxDelta, 0.15f));
	}
	{
		const char* game = "HP6";
		Load(data + "\\HP6\\d3d9.ini");
		EXPECT(c.fpsLimit == 120 && !c.centerWindow && !c.dpiAware);
		EXPECT(c.unlockFrameRate == 1 && c.frameRateCap == 120);
		EXPECT(strcmp(c.language, "auto") == 0 && c.distanceFog == 1);
		// Judged on same-frame before/after pictures (2026-09-27): every effect on, a soft grading
		// with faces spared; no MSAA, no light shafts.
		EXPECT(c.fxaa && c.anisotropy == 16 && c.msaa == 0 && !c.transparencyAa && !c.godRays);
		EXPECT(c.ssao && Near(c.ssaoStrength, 0.55f) && Near(c.ssaoMinDelta, 0.02f) && Near(c.ssaoMaxDelta, 0.15f));
		EXPECT(c.bloom && c.grading && Near(c.vibrance, 0.30f) && Near(c.contrast, 0.20f) && Near(c.skinProtect, 0.70f));
		EXPECT(Near(c.gain, 1.0f) && Near(c.splitTone, 0.0f) && Near(c.temperature, 0.0f));
	}
	// HP7 parts 1 and 2: the window and focus of the others, 60 fps without the game's own
	// 30 fps wait, and the field of view the earlier fix gave every player.
	for (const char* game : { "HP7a", "HP7b" })
	{
		Load(data + "\\" + game + "\\d3d9.ini");
		EXPECT(c.windowed && c.windowStyle == 1 && c.keepRunning);
		EXPECT(c.retakeInput && c.releaseStaleKeys);
		EXPECT(c.screenshotKey == VK_F12 && !c.showFps && c.compareKey == 0);
		EXPECT(c.fpsLimit == 60 && c.centerWindow && !c.dpiAware);
		EXPECT(c.renderWidth == 0 && c.renderHeight == 0);
		EXPECT(c.legacyAspectIndex == 0 && c.legacyFov == 0);
		EXPECT(!c.fxaa && c.msaa == 0 && !c.transparencyAa && c.anisotropy == 0 && !c.grading && !c.ssao && !c.bloom && !c.godRays);
	}
	{
		const char* game = "HP7a";
		Load(data + "\\HP7a\\d3d9.ini");
		EXPECT(Near(c.fovScale, 1.7189f) && Near(c.aspectRatio, 0.0f));
		EXPECT(c.unlockFrameRate == 1);
	}
	{
		const char* game = "HP7b";
		Load(data + "\\HP7b\\d3d9.ini");
		EXPECT(Near(c.fovScale, 1.4324f));
		// Kept at 30 until the Thief's Downfall cut-scene has been played at 60 (see make_ini.py).
		EXPECT(c.unlockFrameRate == 0);
	}
	{
		// A file in the earlier format, comments on the lines as players had them.
		const char* game = "old format";
		const std::string path = scratch + "\\old_format.ini";
		FILE* f = nullptr;
		fopen_s(&f, path.c_str(), "w");
		if (!f)
			return 3;
		fputs("[MAIN]\nFPSLimit = 100 // max fps\nForceWindowedMode = 1\nDisplayFPSCounter = 1\n"
		      "[RESOLUTION]\nwidth = 2560\nheight = 1440\n"
		      "[fullscreenaspectratio]\nfullscreenaspectratio = 3 // 21:9\n"
		      "[FOV]\nfov = 2\n[FPSANIMATIONS]\nfpsanimations = 2\n"
		      "[FORCEWINDOWED]\nCenterWindow = 1\nDoNotNotifyOnTaskSwitch = 1\nForceWindowStyle = 2\n"
		      "[GRAPHICS]\nFXAA = 1\nVibrance = 0.45   // comment\nLift     = 0.02\nSSAO = 1\n"
		      "SSAOStrength = 0.55\nRenderWidth = 3840\n", f);
		fclose(f);
		Load(path);
		EXPECT(c.fpsLimit == 100 && c.windowed && c.showFps && c.centerWindow && c.keepRunning);
		EXPECT(c.windowStyle == 2);
		EXPECT(c.width == 2560 && c.height == 1440);
		EXPECT(c.legacyAspectIndex == 3 && c.legacyFov == 2 && c.animationRate == 30);
		EXPECT(c.fxaa && Near(c.vibrance, 0.45f) && Near(c.lift, 0.02f) && c.ssao && Near(c.ssaoStrength, 0.55f));
		// The render size is only ours: the old [GRAPHICS] never sized the back buffer.
		EXPECT(c.renderWidth == 0);
	}

	{
		// The screenshot folder as Accio Launcher writes it: UTF-8, a user name with an accent.
		const char* game = "screenshot folder";
		const std::string path = scratch + "\\folder.ini";
		FILE* f = nullptr;
		fopen_s(&f, path.c_str(), "wb");
		if (!f)
			return 3;
		fputs("[Accio.Window]\r\nScreenshotFolder=C:\\Users\\Fr\xC3\xA9" "d\xC3\xA9ric\\Pictures\\Accio Launcher\\HP4  \r\n", f);
		fclose(f);
		Load(path);
		EXPECT(wcscmp(c.screenshotFolder, L"C:\\Users\\Fr\u00e9d\u00e9ric\\Pictures\\Accio Launcher\\HP4") == 0);
		// Not set: the pictures stay next to the game, as before.
		fopen_s(&f, path.c_str(), "wb");
		if (!f)
			return 3;
		fputs("[Accio.Window]\r\n;ScreenshotFolder=\r\n", f);
		fclose(f);
		Load(path);
		EXPECT(c.screenshotFolder[0] == L'\0');
	}

	{
		// The before/after key: read where the other overlay keys are, a key code or nothing.
		const char* game = "compare key";
		const std::string path = scratch + "\\compare.ini";
		FILE* f = nullptr;
		fopen_s(&f, path.c_str(), "wb");
		if (!f)
			return 3;
		fputs("[Accio.Overlay]\r\nCompareKey=119\r\n", f);
		fclose(f);
		Load(path);
		EXPECT(c.compareKey == VK_F8);
		fopen_s(&f, path.c_str(), "wb");
		if (!f)
			return 3;
		fputs("[Accio.Overlay]\r\nCompareKey=4000\r\n", f);
		fclose(f);
		Load(path);
		EXPECT(c.compareKey == 255);
	}

	{
		// How mipmaps are made: the sharp filter and kept coverage, a filter out of range capped.
		const char* game = "mipmap filter";
		const std::string path = scratch + "\\mipmaps.ini";
		FILE* f = nullptr;
		fopen_s(&f, path.c_str(), "wb");
		if (!f)
			return 3;
		fputs("[Accio.Graphics]\r\nMipmapFilter=1\r\nMipmapCoverage=1\r\n", f);
		fclose(f);
		Load(path);
		EXPECT(c.mipmapFilter == 1 && c.mipmapCoverage && c.generateMipmaps);
		fopen_s(&f, path.c_str(), "wb");
		if (!f)
			return 3;
		fputs("[Accio.Graphics]\r\nMipmapFilter=7\r\n", f);
		fclose(f);
		Load(path);
		EXPECT(c.mipmapFilter == 1 && !c.mipmapCoverage);
	}
	{
		// Supersampling takes any factor (1.5 costs about half of 2), bounded to 1..4.
		const char* game = "supersampling";
		const std::string path = scratch + "\\ssaa.ini";
		FILE* f = nullptr;
		fopen_s(&f, path.c_str(), "wb");
		if (!f)
			return 3;
		fputs("[Accio.Graphics]\r\nSSAAFactor=1.5\r\n", f);
		fclose(f);
		Load(path);
		EXPECT(Near(c.ssaa, 1.5f));
		fopen_s(&f, path.c_str(), "wb");
		if (!f)
			return 3;
		fputs("[Accio.Graphics]\r\nSSAAFactor=9\r\n", f);
		fclose(f);
		Load(path);
		EXPECT(Near(c.ssaa, 4.0f));
	}

	printf("%d failure(s)\n", failures);
	return failures ? 1 : 0;
}
