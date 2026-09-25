# Harry Potter PC Fix

[Français](README.md) · **English**

[![Build](https://github.com/ludvdber/Harry-Potter-PC-Fix/actions/workflows/build.yml/badge.svg)](https://github.com/ludvdber/Harry-Potter-PC-Fix/actions/workflows/build.yml)
[![License](https://img.shields.io/badge/license-PolyForm%20Strict%20%C2%B7%20no%20redistribution-8b1a1a)](license)
[![Accio Launcher](https://img.shields.io/badge/Accio%20Launcher-acciolauncher.be-d6a72c)](https://acciolauncher.be/)

<!-- Images to come: banner, before / after screenshots. -->

A PC fix for the three Harry Potter games Electronic Arts built on the same engine family, made for [Accio Launcher](https://acciolauncher.be/). It is a single `d3d9.dll` placed next to the game's executable: the game loads it instead of the system Direct3D 9, and it hands everything on to the real one. The same DLL serves the three games and recognises the one it is loaded into; each game has its own `d3d9.ini`.

| Game | Year | Executable | Settings | Release file |
|---|---|---|---|---|
| *Harry Potter and the Goblet of Fire* | 2005 | `gof_f.exe` | [`data/HP4/d3d9.ini`](data/HP4/d3d9.ini) | `HP4-Goblet-of-Fire.zip` |
| *Harry Potter and the Order of the Phoenix* | 2007 | `hp.exe` | [`data/HP5/d3d9.ini`](data/HP5/d3d9.ini) | `HP5-Order-of-the-Phoenix.zip` |
| *Harry Potter and the Half-Blood Prince* | 2009 | `hp6.exe` | [`data/HP6/d3d9.ini`](data/HP6/d3d9.ini) | `HP6-Half-Blood-Prince.zip` |

It turns the game's exclusive full screen into a borderless window that survives Alt+Tab, gives keyboard and mouse back the moment you return, lets you choose your own keys, and patches the resolution, aspect ratio, field of view and frame rate the engine starts with. Optional post-processing sharpens and grades the image. Every setting lives in `d3d9.ini`, read once when the game starts.

**One common base.** The shipped settings are the same for everyone, made for the most common screen (1920×1080). A settings screen in Accio Launcher will then let each player adjust their own.

---

## What it fixes

### Window, focus and input (all three games)

| | Setting | |
|---|---|---|
| Borderless window | `Windowed`, `WindowStyle` | Instead of exclusive full screen, where Alt+Tab freezes the game. Style 1 covers the monitor; 2 to 4 are windows. |
| Keeps running in the background | `KeepRunningInBackground` | The engine stops its clock at any sign that another program is in front. Those signs are kept from it, so it goes on running while you are elsewhere. |
| Keyboard and mouse back at once | `RetakeInputOnReturn` | The games read their devices in DirectInput exclusive mode and are never told they are back in front: the keyboard used to stay dead for up to 30 seconds. The return is detected and every device taken back at its next read. |
| No key stuck after Alt+Tab | `ReleaseKeysOnReturn` | A key released in another window never reached the game, which believed it still held (Harry walking on his own). It is released for the game too. |
| Your own keys | `[Accio.Keys]` | Any key or mouse button for any game key, named as printed on **your** keyboard. *Goblet of Fire* also has named actions (`Charm`, `Jinx`, `Accio`…) and a ready-made preset in its ini. |

### Inside *Harry Potter and the Goblet of Fire* (`gof_f.exe`)

| | Setting | |
|---|---|---|
| Start-up resolution | `Width` / `Height` | The engine starts at 800×600 until its options are read; here it starts at the size you choose. |
| Aspect ratio | `AspectRatio` | The game renders 4:3. 16:9 by default here; any ratio works (16:10, 21:9, 32:9, 2.37…). |
| Field of view | `FOV` | A factor on the game's 114.6 degrees: 1.15, 1.25 or 1.40 widen the view. |
| Animation rate | `AnimationRate` | Characters are animated at 20 frames per second; 25 or 30 makes them smoother. |
| Frame-rate reference | `FrameRateCap` | 60 as shipped, which holds the game back; 120 by default, so that it can follow `FPSLimit` (100). |
| Crash above 2048 pixels | `HazeOverlay` | The haze of the Forbidden Forest, the lake, the maze and the graveyard is built in a fixed array of 129 columns 16 pixels wide: a wider screen overruns it and the game crashes. `1` draws it with the column count capped (wider columns, same haze); `0`, the default for now, skips it as the earlier fix did. |

### Inside *Harry Potter and the Order of the Phoenix* (`hp.exe`)

| | Setting | |
|---|---|---|
| Start-up resolution | `Width` / `Height` | The engine starts at 640×480 until its options are read; here it starts at the size you choose. |
| Aspect ratio | `AspectRatio` | The game's 16:9, replaced by any ratio (16:10, 21:9, 32:9, 2.37…). |
| Field of view | `FOV` | A factor on the game's own view: 1.15, 1.25 or 1.40 widen it. |
| 30 fps limit lifted | `UnlockFrameRate` | The game starts with a presentation interval of 2 (30 frames per second); it becomes 1. |
| Frame-rate ceiling | `FrameRateCap` | The ceiling the engine keeps in memory and sometimes resets, held at 120. |

### Inside *Harry Potter and the Half-Blood Prince* (`hp6.exe`)

| | Setting | |
|---|---|---|
| Start-up resolution | `Width` / `Height` | The engine starts at 640×480 until its options are read; here it starts at the size you choose. |
| Aspect ratio | `AspectRatio` | The game's 16:9, replaced by any ratio (16:10, 21:9, 32:9, 2.37…). |
| Field of view | `FOV` | A factor on the game's own view: 1.15, 1.25 or 1.40 widen it. |
| 30 fps limit lifted | `UnlockFrameRate` | The game starts with a presentation interval of 2 (30 frames per second); it becomes 1. |
| Frame-rate ceiling | `FrameRateCap` | The ceiling the engine keeps in memory and sometimes resets, held at 120 (left alone while the engine sets it to 0). |

Each change is made in the executable once it is loaded, never on disk, and only where the expected bytes are found: another build of a game simply runs unchanged, and the log says so.

### Image

- *Goblet of Fire*: every image effect is off by default: none has been tuned for this game yet.
- *Order of the Phoenix*: the image effects are on by default, with the values this fix has shipped with since its first release.
- *Half-Blood Prince*: every image effect is off by default: none has been tuned for this game yet.

| | Setting | |
|---|---|---|
| Mipmaps built for every texture | `GenerateMipmaps`, `ForceTrilinear` | Most textures ship without smaller copies for the distance, which makes them shimmer and blur far away. The copies are built as each texture loads. |
| MSAA | `Antialiasing` | Stepped down (16, 8, 4, 2, off) until the graphics card accepts it, instead of switching off. |
| Anisotropic filtering, texture sharpness | `AnisotropicFiltering`, `TextureLODBias` | Forced on every texture. |
| FXAA with sharpening | `FXAA`, `Sharpness` | One pass over the finished frame; it also carries the effects below. |
| Colour grading | `ColorGrading` and the values under it | Lift, gain, gamma, white balance, contrast, vibrance, split toning, vignette. |
| Ambient occlusion | `SSAO` and its values | Drawn the moment the 3D scene is done, before menus and subtitles. |
| Bloom and light shafts | `Bloom`, `GodRays` | Half resolution, faded out on menus and white screens. |
| Supersampling | `SSAAFactor` | Renders 2 to 4 times larger. Very demanding. |
| Render size | `RenderWidth`, `RenderHeight` | 0 = the size chosen in the game; -1 = the monitor's own. |

### Performance, like a benchmark tool (`[Accio.Overlay]`)

Each line has its own switch, all off by default: frames per second (`ShowFPS`), frame time and "1% low" (`ShowFrameTime`), a graph of the last 240 frames (`ShowGraph`), processor use of the game and of its main thread (`ShowCPU`), graphics card load (`ShowGPU`), video memory (`ShowVRAM`) and memory (`ShowRAM`), latency from reading a key to sending the frame (`ShowLatency`). F10 shows or hides the panel (`OverlayKey`); F11 starts and stops a benchmark (`BenchmarkKey`): average, 1% and 0.1% low and worst frame on screen, and every frame in the `benchmarks` folder.

Also: a screenshot key (`ScreenshotKey`, F12 by default, PNG files in `screenshots`), a frame-rate limit (`FPSLimit`), one frame of driver queue instead of three (`MaxFrameLatency`), and `DPIAware` for high-DPI screens.

---

## Installation

**With [Accio Launcher](https://acciolauncher.be/)**: nothing to do, each game comes with its fix.

**By hand**: take your game's zip from a [release](https://github.com/ludvdber/Harry-Potter-PC-Fix/releases) and copy `d3d9.dll` and `d3d9.ini` next to the game's executable. Files left by earlier fixes (`d3d9_original.dll`, `fps.dll`) can be deleted: nothing loads them any more.

**On Linux** (Wine or Proton), Wine uses its own `d3d9` unless told otherwise: `WINEDLLOVERRIDES="d3d9=n,b"`. Accio Launcher does it for you.

Settings are read when the game starts: change `d3d9.ini`, then restart the game. Every line of the file is commented. A `d3d9.ini` written for an earlier fix still works: keys missing from the `[Accio.*]` sections are read where earlier versions kept them.

---

## When something looks wrong

Every start writes `d3d9_accio.log` next to the game's executable (`Log=0` turns it off): which game was recognised, which change was made or skipped, what Direct3D accepted (window, image size, MSAA, readable depth), when the game left and came back to the front, and when keyboard and mouse were taken back. Attach it to any [bug report](https://github.com/ludvdber/Harry-Potter-PC-Fix/issues/new/choose).

---

## Building

Visual Studio 2022 (C++ desktop workload), Win32 only: the games are 32-bit.

```powershell
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" `
    build\accio-fix.sln /p:Configuration=Release /p:Platform=Win32 /v:minimal
```

Output: `data\d3d9.dll`. Without a game, `tests\run_keys_test.bat` checks the key remapping and `tests\run_settings_test.bat` reads the three `d3d9.ini` files (and one in the earlier format) with the DLL's own reader. The three ini files are generated by `python tools\make_ini.py`; the build fails when they drift.

**From GitHub, nothing to install.** Every push runs **Build** (compile, tests, PE32 check, ini files, version). **Actions** → **Release** → **Run workflow** builds the release files; with **Créer la release** ticked it creates a draft release, published by hand. Every release file carries a signed build provenance attestation: `gh attestation verify d3d9.dll --repo ludvdber/Harry-Potter-PC-Fix`.

---

## License

© 2026 Accio Launcher. **This fix is not open source**: [PolyForm Strict 1.0.0 with an additional permission](license).

- ✅ You may read the code, fork it on GitHub, change it and build it **for your own personal use**.
- ❌ You may not redistribute it, changed or not, compiled or as source, in whole or in part: not on a mod site (Nexus Mods, ModDB…), a file host, a forum, a Discord server, a Patreon, a repack or another launcher, free or paid.
- ❌ You may not sell it, put it behind a payment, a subscription or an advertisement, nor use the names "Accio Launcher" or "Harry Potter PC Fix" for a copy.

The only official sources are [Accio Launcher](https://acciolauncher.be/) and this repository's releases. A copy found anywhere else is not ours: it may have been altered, and it will be reported for removal.

Microsoft's DirectX SDK headers in `source/dxsdk` keep their own license ([notices](THIRD_PARTY_NOTICES.md)). The games and their files belong to their owners (Electronic Arts, Warner Bros.); this project claims no rights over them and ships none of them.

Found it useful? [A coffee on Ko-fi](https://ko-fi.com/ludovic01) keeps it going.
