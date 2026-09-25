# Harry Potter PC Fix

A PC fix for the three Harry Potter games Electronic Arts built on the same engine family, made for [Accio Launcher](https://acciolauncher.be/). It is a single `d3d9.dll` placed next to the game's executable: the game loads it instead of the system Direct3D 9, and it hands everything on to the real one. The same DLL serves the three games and recognises the one it is loaded into; each game has its own `d3d9.ini`.

| Game | Year | Executable | Settings | Release file |
|---|---|---|---|---|
| *Harry Potter and the Goblet of Fire* | 2005 | `gof_f.exe` | [`data/HP4/d3d9.ini`](data/HP4/d3d9.ini) | `HP4-Goblet-of-Fire.zip` |
| *Harry Potter and the Order of the Phoenix* | 2007 | `hp.exe` | [`data/HP5/d3d9.ini`](data/HP5/d3d9.ini) | `HP5-Order-of-the-Phoenix.zip` |
| *Harry Potter and the Half-Blood Prince* | 2009 | `hp6.exe` | [`data/HP6/d3d9.ini`](data/HP6/d3d9.ini) | `HP6-Half-Blood-Prince.zip` |

It turns the game's exclusive full screen into a borderless window that survives Alt+Tab, gives keyboard and mouse back the moment you return, lets you choose your own keys, and patches the resolution, aspect ratio, field of view and frame rate the engine starts with. Optional post-processing sharpens and grades the image. Every setting lives in `d3d9.ini`, read once when the game starts.

---

## What it does

### Window, focus and input (all three games)

| | Setting | |
|---|---|---|
| Borderless window | `Windowed`, `WindowStyle` | Instead of exclusive full screen, where Alt+Tab freezes the game. Style 1 covers the monitor; 2 to 4 are windows. |
| Keeps running in the background | `KeepRunningInBackground` | The engine stops its clock at any sign that another program is in front. Those signs are kept from it, so it goes on running while you are in another window. |
| Keyboard and mouse back at once | `RetakeInputOnReturn` | The games read their devices in DirectInput exclusive mode and are never told they are back in front: the keyboard used to stay dead for up to 30 seconds. The return is detected and every device taken back at its next read. |
| No key stuck after Alt+Tab | `ReleaseKeysOnReturn` | A key released while you were in another window never reaches the game, which then believes it is still held (Harry walking on his own). Such keys are released for the game too. |
| Your own keys | `[Accio.Keys]` | Any key or mouse button for any game key. Keys are named as printed on your keyboard, so ZQSD on AZERTY works as written. *Goblet of Fire* also has named actions (`Charm`, `Jinx`, `Accio`...) and a ready-made preset in its ini. |

### Inside *Goblet of Fire* (`gof_f.exe`)

| | Setting | |
|---|---|---|
| Start-up resolution | `Width` / `Height` | The engine starts at 800x600 until its options are read; this sets the size it starts with. |
| Aspect ratio | `AspectRatio` | The game renders 4:3. 16:9 by default here; any ratio works (16:10, 21:9, 32:9, 2.37...). |
| Field of view | `FOV` | A factor on the game's 114.6 degrees: 1.15, 1.25 or 1.40 widen the view. |
| Animation rate | `AnimationRate` | Characters are animated at 20 frames per second; 25 or 30 makes them smoother. |
| Frame-rate reference | `FrameRateCap` | 60 as shipped, which holds the game back; 120 by default, so that it can follow `FPSLimit` (100). |
| High-resolution crashes | `HazeOverlay` | A translucent overlay some levels draw crashed the Forbidden Forest, the lake, the maze and the graveyard above 1080p. It is skipped by default. |

### Inside *Order of the Phoenix* (`hp.exe`)

| | Setting | |
|---|---|---|
| Start-up resolution | `Width` / `Height` | The engine starts at 640x480 until its options are read; this sets the size it starts with. |
| Aspect ratio | `AspectRatio` | The 16:9 ratio the game renders with, replaced by any ratio (16:10, 21:9, 32:9, 2.37...). |
| Field of view | `FOV` | A factor on the game's own view: 1.15, 1.25 or 1.40 widen it. |
| 30 fps limit lifted | `UnlockFrameRate` | The game starts with a presentation interval of 2 (30 frames per second); this sets it to 1. |
| Frame-rate ceiling | `FrameRateCap` | The ceiling the engine keeps in memory and sometimes resets, held at 120. |

### Inside *Half-Blood Prince* (`hp6.exe`)

| | Setting | |
|---|---|---|
| Start-up resolution | `Width` / `Height` | The engine starts at 640x480 until its options are read; this sets the size it starts with. |
| Aspect ratio | `AspectRatio` | The 16:9 ratio the game renders with, replaced by any ratio (16:10, 21:9, 32:9, 2.37...). |
| Field of view | `FOV` | A factor on the game's own view: 1.15, 1.25 or 1.40 widen it. |
| 30 fps limit lifted | `UnlockFrameRate` | The game starts with a presentation interval of 2 (30 frames per second); this sets it to 1. |
| Frame-rate ceiling | `FrameRateCap` | The ceiling the engine keeps in memory and sometimes resets, held at 120 (left alone while the engine sets it to 0). |

Each change is made in the executable once it is loaded, never on disk, and only where the expected bytes are found: another build of a game simply runs unchanged, and the log says so.

### Image

- *Goblet of Fire*: every image effect is off by default: none has been tuned for this game yet.
- *Order of the Phoenix*: the image effects are on by default, with the values this fix has shipped with since its first release.
- *Half-Blood Prince*: every image effect is off by default: none has been tuned for this game yet.

| | Setting | |
|---|---|---|
| Mipmaps built for every texture | `GenerateMipmaps`, `ForceTrilinear` | Most textures ship without smaller copies for the distance, which makes them shimmer and blur far away. The copies are built when the game loads each texture. |
| MSAA | `Antialiasing` | Stepped down (16, 8, 4, 2, off) until the graphics card accepts it, instead of switching off. |
| Anisotropic filtering, texture sharpness | `AnisotropicFiltering`, `TextureLODBias` | Forced on every texture. |
| FXAA with sharpening | `FXAA`, `Sharpness` | One pass over the finished frame; it also carries the effects below. |
| Colour grading | `ColorGrading` and the values under it | Lift, gain, gamma, white balance, contrast, vibrance, split toning, vignette. |
| Ambient occlusion | `SSAO` and its values | Drawn the moment the 3D scene is done, before menus and subtitles are laid on top. |
| Bloom and light shafts | `Bloom`, `GodRays` | Half-resolution, faded out on menus and white screens. |
| Supersampling | `SSAAFactor` | Renders 2 to 4 times larger. Very demanding. |
| Render size | `RenderWidth`, `RenderHeight` | 0 = the size chosen in the game; -1 = the monitor's own. |

Also: a screenshot key (`ScreenshotKey`, F12 by default, PNG files in `screenshots`), a frame counter (`ShowFPS`), a frame-rate limit (`FPSLimit`), one frame of driver queue instead of three (`MaxFrameLatency`), and `DPIAware` for high-DPI screens.

---

## Installation

**With [Accio Launcher](https://acciolauncher.be/)**: nothing to do, each game comes with the fix.

**By hand**: take the zip of your game from a release and copy its `d3d9.dll` and `d3d9.ini` next to the game's executable. Files left by earlier fixes (`d3d9_original.dll`, `fps.dll`) can be deleted: nothing loads them any more.

**On Linux** (Wine or Proton), Wine uses its own `d3d9` unless told otherwise: set `WINEDLLOVERRIDES="d3d9=n,b"`. Accio Launcher does it for you.

Settings are read when the game starts: change `d3d9.ini`, then restart the game. Every line of the file is commented. A `d3d9.ini` written for an earlier fix still works: keys missing from the `[Accio.*]` sections are read where earlier versions kept them.

---

## When something looks wrong

Every start writes `d3d9_accio.log` next to the game's executable (`Log=0` turns it off). It says which game was recognised, which change was made or skipped, what Direct3D accepted (window, image size, MSAA, readable depth), when the game left and came back to the front, and when keyboard and mouse were taken back. Attach it to any bug report.

---

## Building

Visual Studio 2022 (C++ desktop workload), Win32 only: the games are 32-bit.

```powershell
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" `
    build\accio-fix.sln /p:Configuration=Release /p:Platform=Win32 /v:minimal
```

Output: `data\d3d9.dll`. `tests\run_keys_test.bat` checks the key remapping without a game.

| Path | Role |
|---|---|
| `source/main.cpp` | Entry point, exports, loads the system Direct3D 9 |
| `source/settings.cpp` | `d3d9.ini` |
| `source/hooks.cpp` | Method tables, import tables, executable bytes |
| `source/game.cpp` | What is changed inside each game, and where |
| `source/window.cpp` | Window style and position, focus |
| `source/input.cpp` | DirectInput: return to the front, stuck keys |
| `source/keys.cpp` | `[Accio.Keys]` |
| `source/direct3d.cpp` | Device creation and reset, textures, depth, samplers |
| `source/present.cpp` | Each frame: effects, frame counter, screenshots, frame-rate limit |
| `source/effects.cpp`, `shaders.h` | Post-processing |
| `source/version.h` | Version written into the DLL (must match `VERSION`) |
| `data/HP4`, `data/HP5`, `data/HP6` | Each game's `d3d9.ini` |

---

## Compiler sans Windows

Tout se fait depuis l'onglet **Actions** du dépôt sur GitHub : la compilation tourne sur une machine Windows fournie par GitHub, rien à installer.

**Build automatique.** Chaque push et chaque pull request lance le workflow **Build** : compilation, tests des touches, vérification que la DLL est bien une DLL 32 bits et que le numéro de version est le même dans `VERSION` et `source/version.h`. Pour récupérer la DLL : **Actions** → **Build** → cliquer sur le run → section **Artifacts** → `d3d9-win32`.

**Build d'essai (rien n'est publié).** **Actions** → **Release** → **Run workflow** → *Use workflow from* : `main`, case **Créer la release** décochée → **Run workflow**. Une fois le run vert : **Artifacts** → `release-v<VERSION>` (la DLL et un zip par jeu).

**Créer une release.**

1. Changer le numéro dans `VERSION` **et** dans `source/version.h` (`ACCIO_VERSION_NUM` et `ACCIO_VERSION_STR`), dans le même commit : le build refuse deux numéros différents.
2. **Actions** → **Release** → **Run workflow** → cocher **Créer la release** → **Run workflow**.
3. Une fois le run vert : **Releases** → le brouillon `v<VERSION>` → relire → **Edit** → **Publish release**. Rien n'est public avant ce clic, et le tag n'est créé qu'à ce moment-là.

Chaque fichier de la release reçoit une attestation de provenance signée : `gh attestation verify d3d9.dll --repo ludvdber/Harry-Potter-PC-Fix`.

Les actions sont épinglées par empreinte de commit ; Dependabot propose chaque semaine leur mise à jour, à fusionner si le build est vert.

---

## License

© 2026 Accio Launcher. [PolyForm Strict 1.0.0 with an additional permission](license): you may read the code, fork it, change it and build it for yourself, but you may not redistribute it, changed or not, in any form. The only official source of this fix is Accio Launcher. Microsoft's DirectX SDK headers in `source/dxsdk` keep their own license ([notices](THIRD_PARTY_NOTICES.md)).

The games and their files belong to their owners (Electronic Arts, Warner Bros.). This project claims no rights over them and ships none of them.
