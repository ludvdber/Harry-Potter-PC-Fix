"""Generates data/<game>/d3d9.ini (HP4, HP5, HP6, HP7a, HP7b), data/<game>/winmm.ini (HP1,
HP2) and data/HP3/dinput8.ini: the shipped settings of each game.

    python tools/make_ini.py          writes every file
    python tools/make_ini.py --check  fails if a committed file differs (run by the Build workflow)

The files are written by this one script so that a setting added to the DLL reaches every game
together. Edit here, run it, commit the script and the files together.
ASCII, CRLF: the games' own ini files are in that form, and GetPrivateProfile reads it as is.
"""
import sys
from pathlib import Path

JEUX = {
    "HP4": dict(titre="Harry Potter and the Goblet of Fire", resolution="800x600",
                fps_limit=120, center=1, dpi=0, fov_note="0 = as shipped (114.6 degrees)",
                animations=True, cap=120, unlock=False, haze=True, aspect="16:9", pads=True,
                # Measured in game (2026-09-26, camp at night, F11 while walking): 100 -> 1 % low 94;
                # 120 -> average 119, 1 % low 111-115, same positions at the same instants (not faster).
                fps_note=("; 120: the frame-rate reference below (FrameRateCap) is 120 too; measured",
                          "; at 119 FPS on average, 1 % low 111, game speed unchanged."),
                # Played by Ludo from the start to the Forbidden Forest with all of these on (2026-09-27 and 28,
                # no crash): HP6's soft grade, sharp mipmaps that keep their density. Bloom made the Pensieve
                # (level choice) "too bright": it only takes the brightest spots here, and less of them.
                # Supersampling 1.5 on both of those games: Ludo wants "as little pixelation as possible, first".
                # Grade "E2, readable" (2026-10-01): the forest is the game's own darkness, not our SSAO;
                # a lower gamma and a pivot at 0.30 open the shadows without flattening. Chosen by Ludo on
                # in-game pairs (HP4_3_en_jeu): "on voit vachement mieux Harry".
                graphics=dict(FXAA=1, AnisotropicFiltering=16, SSAAFactor="1.5", SSAO=1, Bloom=1, BloomStrength="0.25",
                              BloomThreshold="0.85", GodRays=1, ColorGrading=1, Vibrance="0.28", Gamma="0.90",
                              Temperature="0.05", Contrast="0.28", ContrastPivot="0.30",
                              SkinProtect="0.70", MipmapFilter=1, MipmapCoverage=1)),
    "HP5": dict(titre="Harry Potter and the Order of the Phoenix", resolution="640x480",
                fps_limit=120, center=1, dpi=1, fov_note="0 = as shipped",
                animations=False, cap=120, unlock=True, haze=False, fog=True, detail=True, xinput=True,
                # Measured in game (2026-09-26, common room, F11): the same bursts as HP6,
                # 1 % low 38 FPS alone; with FPSLimit=120, 1 % low 84.
                fps_note=("; 120: the game's own ceiling alone keeps 120 on average, but in bursts",
                          "; of fast frames and waits of 25 ms (1 % low 38 FPS, against 84)."),
                # The image HP5 has shipped with since the fix existed (validated in game), with the grade
                # of board 53 (2026-09-27): the old one turned stone and grass bright yellow (20 % of the
                # courtyard). No added warmth, yellows held back, every other hue more vivid.
                graphics=dict(FXAA=1, Antialiasing=16, AnisotropicFiltering=16, TextureLODBias=-1.5,
                              VSync=1, ColorGrading=1, Vibrance="0.60", Vignette="0.08", Gain="1.05",
                              Temperature="0.00", Tint="0.00", Contrast="0.30", SplitTone="0.10",
                              SkinProtect="0.70", YellowRestraint="1.00",
                              Bloom=1, GodRays=1, SSAO=1, SSAOStrength="0.55",
                              SSAOMinDelta="0.02", SSAOMaxDelta="0.15")),
    # DPIAware=1 (2026-10-01): with 0, Windows enlarged a WINDOWED HP6 by the display scale (3200x1518
    # on a 2560x1440 screen at 125 %, image cut off); with 1, window and menus right (seen 2026-09-30).
    "HP6": dict(titre="Harry Potter and the Half-Blood Prince", resolution="640x480",
                fps_limit=120, center=0, dpi=1, fov_note="0 = as shipped",
                animations=False, cap=120, unlock=True, haze=False, language=True, fog=True, detail=True, xinput=True,
                # Measured in game (2026-09-26, same walk, F11): the game's own ceiling alone
                # averages 120 in bursts, 1 % low 31 FPS; with FPSLimit=120, 1 % low 102.
                fps_note=("; 120: the game's own ceiling alone keeps 120 on average, but in bursts",
                          "; of fast frames and waits of 25 ms (1 % low 31 FPS, against 102)."),
                # Judged by Ludo on same-frame before/after pictures (CompareKey, 2026-09-27): every
                # effect better on, each one isolated and all together. A softer grading than HP5's
                # (whose tuning made HP6 too teal), with faces spared: they came out orange without it.
                # Distance fog kept: without it the blurry far textures show. Supersampling 1.5: played that
                # way by Ludo, menus clicked where pointed (2026-09-28); "as little pixelation as possible".
                graphics=dict(FXAA=1, AnisotropicFiltering=16, SSAAFactor="1.5", SSAO=1, SSAOStrength="0.55",
                              SSAOMinDelta="0.02", SSAOMaxDelta="0.15", Bloom=1, ColorGrading=1,
                              # Grade "E, nature with the haze held back" (2026-10-01): the mint green of sky,
                              # hills and lamp halos taken out (GreenRestraint), stone grey again. Chosen by
                              # Ludo on in-game pairs (HP6_5_en_jeu a, b, c); the green held back in full
                              # (1, not 0.5), judged on the hills and outdoors (2026-10-01).
                              Vibrance="0.35", Vignette="0.06", Gamma="0.95", Temperature="0.10", Tint="0.75",
                              Contrast="0.25", ContrastPivot="0.40", SkinProtect="0.75",
                              YellowRestraint="0.60", GreenRestraint="1.00",
                              # Shadows x4 judged better outdoors (2026-10-01), then taken back the same
                              # evening: dark patches and a ghost on Harry outdoors when the camera turns.
                              # Left to the player, at his own risk (launcher: ombres_nettes).
                              ShadowMapScale=1)),
    # HP7 parts 1 and 2: no start-up resolution or frame-rate ceiling of their own to change; a 30 fps
    # wait of their own instead. FOV: what the earlier fix gave every player (part 1: its camera
    # set-up converted with 0.03 instead of pi/180; part 2: [FOV] fov=1, pi/180 made 0.025).
    "HP7a": dict(titre="Harry Potter and the Deathly Hallows Part 1", resolution=None,
                 fps_limit=60, center=1, dpi=0, fov="1.7189",
                 fov_note="1.7189 = as with the earlier fix",
                 animations=False, cap=None, unlock="wait", haze=False, aspect=0, xinput=True,
                 # The camera's mouse speed follows the frame rate (measured 2026-10-01): kept as at 30.
                 mouse=30,
                 # Above 60 fps the cut-scenes play too fast (Ludo, 2026-10-01, launcher limit at 144).
                 ceiling=60),
    "HP7b": dict(titre="Harry Potter and the Deathly Hallows Part 2", resolution=None,
                 fps_limit=60, center=1, dpi=0, fov="1.4324",
                 fov_note="1.4324 = as with the earlier fix",
                 animations=False, cap=None, unlock="wait", haze=False, aspect=None, xinput=True,
                 # Off until seen in game: the earlier fix's frame-rate unlock (fps.dll) crashed the
                 # cut-scene at the Thief's Downfall (Ludo, 2026-09-25); that scene not yet played at 60.
                 unlock_off=True,
                 # Same camera as part 1: at 20 fps a gesture turned it 1.67 times more than at 30
                 # (measured 2026-10-01); kept as at 30 whenever the game drops below.
                 mouse=30,
                 # Same engine as part 1: its cut-scenes would play too fast above 60 once unlocked.
                 ceiling=60),
}

GRAPHICS_OFF = dict(FXAA=0, Sharpness="0.40", Antialiasing=0, TransparencyAntialiasing=0, AnisotropicFiltering=0, TextureLODBias=0,
                    SSAAFactor=1, ShadowMapScale=1, VSync=0, ColorGrading=0, Vibrance="0.25",
                    Vignette="0.00", Lift="0.00", Gamma="1.00", Gain="1.00", Temperature="0.00",
                    Tint="0.00", Contrast="0.00", SplitTone="0.00", SkinProtect="0.00", YellowRestraint="0.00", GreenRestraint="0.00", ContrastPivot="0.50", SSAO=0, SSAOStrength="0.50",
                    SSAORadius="6.0", SSAOMinDelta="0.0005", SSAOMaxDelta="0.05", Bloom=0,
                    BloomStrength="0.35", BloomThreshold="0.75", GodRays=0, GodRaysStrength="0.45",
                    GodRaysDecay="0.96", MipmapFilter=0, MipmapCoverage=0)


def ini(jeu, graphismes=None):
    j = JEUX[jeu]
    g = {**GRAPHICS_OFF, **j.get("graphics", {}), **(graphismes or {})}
    L = []
    a = L.append
    a("; ============================================================================")
    a(f";  {j['titre']} - PC fix")
    a(";  Accio Launcher - https://acciolauncher.be/")
    a(";  (c) 2026 Accio Launcher. PolyForm Strict 1.0.0 - see license.")
    a("; ============================================================================")
    a(";")
    a(";  Read once, when the game starts. 1 = on, 0 = off.")
    a(";  A line you delete falls back to its default; a missing file means all")
    a(";  defaults.")
    a("; ============================================================================")
    a("")
    a("")
    a("; ----------------------------------------------------------------------------")
    a(";  Window, focus, mouse and keyboard")
    a("; ----------------------------------------------------------------------------")
    a("[Accio.Window]")
    a("")
    a("; A window instead of exclusive full screen. Needed for everything below:")
    a("; in exclusive full screen, Alt+Tab freezes the game.")
    a("Windowed=1")
    a("")
    a("; 1 = borderless, covering the whole monitor (recommended)")
    a("; 2 = ordinary window, 3 = resizable window, 4 = borderless, image size")
    a("WindowStyle=1")
    a("")
    a("; Keeps the game running while another window is in front (Alt+Tab, a")
    a("; click on another monitor) instead of freezing it. Meanwhile the mouse")
    a("; pointer stays visible over the game and the game cannot move it; keyboard")
    a("; and mouse are taken back as soon as the game is in front again.")
    a("KeepRunningInBackground=1")
    a("")
    a("; Placement of a window (styles 2 to 4).")
    a(f"CenterWindow={j['center']}")
    a("; 1 = always on the main monitor, 0 = the monitor the game opens on.")
    a("UsePrimaryMonitor=0")
    a("AlwaysOnTop=0")
    a("; Styles 2 and 3: a window too big for the screen (an image the size of the")
    a("; screen plus the frame) went under the taskbar. 1 = scaled down to fit,")
    a("; proportions kept; 0 = the window is the image size plus the frame.")
    a("FitToScreen=1")
    a("")
    a("; The game was made before Windows display scaling. 0 keeps its original")
    a("; behaviour; 1 makes it ignore scaling (sharper at 125 % and up, but the")
    a("; window and menus may change size).")
    a(f"DPIAware={j['dpi']}")
    a("")
    a("; Frame-rate limit (0 = none).")
    for ligne in j.get("fps_note", ()):
        a(ligne)
    a(f"FPSLimit={j['fps_limit']}")
    a("")
    a("; Screenshot key, as a Windows virtual-key code (123 = F12, 44 = Print")
    a("; Screen, 0 = none). Pictures go to the \"screenshots\" folder next to the")
    a("; game, or to ScreenshotFolder when it is set: Accio Launcher sets it to")
    a("; Pictures\\Accio Launcher\\<game>, so that uninstalling a game keeps them.")
    a("ScreenshotKey=123")
    a(";ScreenshotFolder=")
    a("")
    a("; Back in front after Alt+Tab: keyboard and mouse taken back at once")
    a("; (without it, the keyboard stayed dead up to 30 seconds).")
    a("RetakeInputOnReturn=1")
    a("; A key you released while in another window is released for the game too")
    a("; (without it, Harry could keep walking on his own).")
    a("ReleaseKeysOnReturn=1")
    a("")
    a("; Writes d3d9_accio.log next to the game: what the fix did, for bug reports.")
    a("Log=1")
    a("")
    a("")
    a("; ----------------------------------------------------------------------------")
    a(";  Performance overlay, like a benchmark tool. Each line has its own switch.")
    a("; ----------------------------------------------------------------------------")
    a("[Accio.Overlay]")
    a("")
    a("; Frames per second.")
    a("ShowFPS=0")
    a("; Time per frame, and the \"1% low\": the frame rate of the slowest 1% of")
    a("; frames, which is what you feel as stutter.")
    a("ShowFrameTime=0")
    a("; Graph of the last 240 frame times. Lines at 60 and 30 FPS; gold bars are on")
    a("; time for 60 FPS, orange for 30, red slower.")
    a("ShowGraph=0")
    a("; Processor used by the game (share of the whole processor), and by its main")
    a("; thread alone (100% = one core full, the real limit of these games).")
    a("ShowCPU=0")
    a("; Graphics card load from this game (read from Windows).")
    a("ShowGPU=0")
    a("; Video memory used by this game.")
    a("ShowVRAM=0")
    a("; Memory used by this game.")
    a("ShowRAM=0")
    a("; Time from the moment the game reads a key you just pressed to the moment the")
    a("; resulting frame goes to the graphics card (the screen itself is not counted).")
    a("ShowLatency=0")
    a("")
    a("; Key that shows or hides the panel, as a Windows virtual-key code (121 = F10,")
    a("; 0 = none). With every line above at 0, it shows the frame rate and the 1% low.")
    a("OverlayKey=121")
    a("; Key that starts a benchmark, and stops it when pressed again (122 = F11, 0 = none). At the end:")
    a("; average FPS, 1% and 0.1% low, worst frame, on screen for 15 seconds, and every")
    a("; frame time in the \"benchmarks\" folder next to the game.")
    a("BenchmarkKey=122")
    a("; Key that switches the image effects of this fix off and on, to compare the same")
    a("; picture with and without them (0 = none; 119 = F8). Anti-aliasing stays as it is.")
    a("CompareKey=0")
    a("; Corner: 1 top left, 2 top right, 3 bottom left, 4 bottom right.")
    a("Position=1")
    a("; Size in percent (50 to 300).")
    a("Size=100")
    a("")
    a("")
    a("; ----------------------------------------------------------------------------")
    a(";  Changes made inside the game")
    a("; ----------------------------------------------------------------------------")
    a("[Accio.Game]")
    a("")
    if j["resolution"]:
        a(f"; The resolution the game starts with (as shipped: {j['resolution']}). You can")
        a("; still change it in the game's own options.")
        a("Width=1920")
        a("Height=1080")
        a("")
    if j.get("aspect", 0) is not None:
        a("; Aspect ratio of the image: 0 = as shipped, or a ratio such as 16:10,")
        a("; 21:9, 32:9, 2.37.")
        a(f"AspectRatio={j.get('aspect', 0)}")
        a("")
    if "fov" in j:
        a(f"; Field of view, as a factor: {j['fov_note']}.")
        a("; 0 = as shipped (narrower); higher widens it further.")
    else:
        a(f"; Field of view, as a factor: {j['fov_note']}; 1.15, 1.25 or 1.40 widen it.")
    a(f"FOV={j.get('fov', 0)}")
    if j["animations"]:
        a("")
        a("; Character animations are played at 20 frames per second. 25 or 30 makes")
        a("; them smoother; 0 = as shipped.")
        a("AnimationRate=0")
    if j["unlock"] == "wait":
        a("")
        a("; The game holds itself to 30 frames per second, spinning a processor core")
        a("; while it waits. 1 skips that wait: FPSLimit above sets the pace (the game")
        a("; keeps its real speed, measured at 30 and 60). The earlier fix only did it")
        a("; while the 9 key was toggled.")
        if j.get("unlock_off"):
            a("; Off for now: with the earlier fix's unlock, the game crashed in the cut-scene")
            a("; at the Thief's Downfall (Gringotts). Not yet played through at 60.")
        a(f"UnlockFrameRate={0 if j.get('unlock_off') else 1}")
    elif j["unlock"]:
        a("")
        a("; The game runs at 30 frames per second as shipped. 1 lifts that limit.")
        a("UnlockFrameRate=1")
        a("")
        a("; Highest frame rate the game allows itself. The original fix notes that the")
        a("; mouse behaves oddly above 59; 0 = the game's own value.")
        a(f"FrameRateCap={j['cap']}")
    elif j["cap"]:
        a("")
        a("; The game's frame-rate reference: 60 as shipped, which holds it back.")
        a("; 120 lets it follow FPSLimit, as the earlier fix did; 0 = as shipped.")
        a(f"FrameRateCap={j['cap']}")
    if j.get("mouse"):
        a("")
        a("; The game turns its camera by the mouse movement of a frame times that")
        a("; frame's length: the faster the game runs, the less the same gesture turns")
        a("; it (measured: 1.7 times less at 60 than at 30), and the camera's speed")
        a("; follows every change of frame rate. 30 = the mouse turns the camera as it")
        a("; did at 30 frames per second, whatever the frame rate; 0 = as shipped.")
        a(f"MouseFrameRate={j['mouse']}")
    if j.get("ceiling"):
        a("")
        a("; Highest frame rate allowed, whatever FPSLimit says (0 there included):")
        a("; above 60 the cut-scenes play too fast. 0 = no ceiling.")
        a(f"FPSCeiling={j['ceiling']}")
    if j["haze"]:
        a("")
        a("; The haze of the Forbidden Forest, the lake, the maze and the graveyard. As")
        a("; shipped, it crashes the game on screens wider than 2048 pixels.")
        a("; 0 = not drawn (the earlier fix's cure), 1 = drawn, with the crash fixed")
        a("; (played through the Forbidden Forest at 2880 pixels wide), 2 = drawn as shipped.")
        a("HazeOverlay=1")
        a("")
        a("; About two seconds after Yes at the autosave prompt, the game sometimes reads")
        a("; a sound stream before its buffer exists and crashes (about 1 start in 10).")
        a("; 1 = that read counts as empty and the game goes on, 0 = as shipped.")
        a("AudioStreamGuard=1")
    if j.get("pads"):
        a("")
        a("; The game only plays the controllers of its own list, which has no")
        a("; PlayStation controller: 1 adds the DualShock 4 and DualSense to that list")
        a("; (nothing is written to the registry), 0 = ignored as shipped.")
        a("PlayStationController=1")
    if j.get("language"):
        a("")
        a("; The language the start menu opens on (and takes by itself after 15 s). The")
        a("; game takes it from Windows but only knows one variant of each language:")
        a("; French from Belgium, Switzerland or Canada, or Spanish as Windows gives it in")
        a("; Spain today, opened on English.")
        a("; auto = your Windows language, brought to the variant the game knows;")
        a("; windows = as the game asks; or a language: en, fr, es, de, it, nl, pt,")
        a("; pt-br, pl, ru, sv, da, fi, no, cs, hu (the game must have it on disk).")
        a("Language=auto")
    if j.get("fog"):
        a("")
        a("; The green haze the game lays over distant scenery (the hills around the")
        a("; grounds melt into it). 1 = as shipped, 0 = removed: far hills sharp and")
        a("; contrasted, the scene a little darker.")
        a("DistanceFog=1")
    if j.get("detail"):
        a("")
        a("; The detail level the game starts on while none is saved (a first start):")
        a("; 0 = Speed, 1 = Balanced (as shipped), 2 = Quality, the sharpest textures.")
        a("; A level chosen in the game's own options is kept. Nothing is written to")
        a("; the registry.")
        a("TextureDetail=2")
    a("")
    a("")
    a("; ----------------------------------------------------------------------------")
    a(";  Your own keys")
    a("; ----------------------------------------------------------------------------")
    a("[Accio.Keys]")
    a("")
    a("; Action=key, or several keys: Action=key1,key2. Keys are named as printed on")
    a("; YOUR keyboard (Z, Q, 1...), or Space, Enter, Tab, LShift, LCtrl, LAlt,")
    a("; Up, Down, Left, Right, F1 to F12, Num0 to Num9, and MouseLeft, MouseRight,")
    a("; MouseMiddle, Mouse4, Mouse5. A key given to an action stops doing what it")
    a("; did before; every other key of the game keeps working.")
    if j["animations"]:
        a(";")
        a("; Actions: MoveUp, MoveDown, MoveLeft, MoveRight, Charm, Jinx, Accio,")
        a("; Extremos, Pause, Confirm, Back.")
        a("; Any game key can also be named Key.<its US-keyboard name>, e.g. Key.C.")
        a(";")
        a("; Example (remove the ; to use it): move with ZQSD on an AZERTY keyboard")
        a("; (WASD on QWERTY), spells on the mouse and around the left hand.")
        a(";MoveUp=Z")
        a(";MoveLeft=Q")
        a(";MoveDown=S")
        a(";MoveRight=D")
        a(";Charm=MouseLeft")
        a(";Jinx=MouseRight")
        a(";Accio=E")
        a(";Extremos=R")
    else:
        a(";")
        a("; Name the game's key as Key.<its US-keyboard name>, e.g. Key.E=F.")
    a("")
    a("")
    if j.get("xinput"):
        a("; ----------------------------------------------------------------------------")
        a(";  Controller (read by xinput1_3.dll, next to the game)")
        a("; ----------------------------------------------------------------------------")
        a("[Accio.Controller]")
        a("")
        a("; A PlayStation 4 or 5 controller plays like an Xbox controller, on the first")
        a("; place no Xbox controller holds. 0 = Xbox controllers only: use it if a tool")
        a("; such as Steam Input or DS4Windows already turns yours into an Xbox")
        a("; controller, or the game would see it twice.")
        a("PlayStation=1")
        a("; Vibration of a PlayStation controller (USB only).")
        a("Rumble=1")
        a("; Light bar of a PlayStation controller (USB only), as red,green,blue from 0")
        a("; to 255, e.g. 255,110,0. Empty = left as it is. Accio Launcher sets it to")
        a("; your house colours.")
        a("LightBar=")
        a("")
        a("")
    a("; ----------------------------------------------------------------------------")
    a(";  Image")
    if "graphics" not in j:
        a(";  All off: none of these effects has been tuned for this game yet.")
    a("; ----------------------------------------------------------------------------")
    a("[Accio.Graphics]")
    a("")
    a("; Edge smoothing (FXAA) with light sharpening. Also required by ColorGrading,")
    a("; SSAO, Bloom and GodRays.")
    a(f"FXAA={g['FXAA']}")
    a(f"Sharpness={g['Sharpness']}")
    a("")
    a("; Multisample anti-aliasing: 0, 2, 4, 8 or 16 (stepped down if the graphics")
    a("; card cannot). Reaches the 3D scene only with SSAO=0: ambient occlusion reads")
    a("; the scene depth, which Direct3D 9 cannot multisample.")
    a(f"Antialiasing={g['Antialiasing']}")
    a("")
    a("; With Antialiasing: hair and leaves smoothed too (their cut-out edges).")
    a("; NVIDIA cards only, elsewhere no change. Costs frames where there are many")
    a("; leaves (HP6, 2560x1440, RTX 2060 SUPER: 1 % low 101 -> 76 FPS).")
    a(f"TransparencyAntialiasing={g['TransparencyAntialiasing']}")
    a("")
    a("; Texture sharpness at an angle: 0, 2, 4, 8 or 16.")
    a(f"AnisotropicFiltering={g['AnisotropicFiltering']}")
    a("")
    a("; Negative = sharper distant textures (-1.0 is a good value), 0 = as shipped.")
    a(f"TextureLODBias={g['TextureLODBias']}")
    a("")
    a("; Render larger, then scale down (supersampling): 1 = off, 1.5, 2 (up to 4). Very demanding:")
    a("; 2 draws four times the pixels, 1.5 a little over twice.")
    a(f"SSAAFactor={g['SSAAFactor']}")
    a("; The enlarged image is never taller than this (the factor is lowered to fit):")
    a("; 2880 keeps 1440p at 2 and 4K at 1.33, the largest image seen running. 0 = no")
    a("; limit but the graphics card's own.")
    a("SSAAMaxHeight=2880")
    a("")
    a("; Sharper shadows (1, 2 or 4). Experimental: also enlarges reflections.")
    a(f"ShadowMapScale={g['ShadowMapScale']}")
    a("")
    a(f"VSync={g['VSync']}")
    a("")
    a("; Colour adjustments. SkinProtect (0 to 1) spares faces part of the contrast and vibrance;")
    a("; YellowRestraint (0 to 1) keeps yellows out of the vibrance and tones them down a little;")
    a("; GreenRestraint (0 to 1) takes the mint green out of skies and distance (HP5, HP6).")
    a("; ContrastPivot: the grey the contrast leaves in place; 0.5 = as before, lower (0.35) adds")
    a("; depth to a dark game without darkening it.")
    for k in ("ColorGrading", "Vibrance", "Vignette", "Lift", "Gamma", "Gain", "Temperature", "Tint",
              "Contrast", "ContrastPivot", "SplitTone", "SkinProtect", "YellowRestraint", "GreenRestraint"):
        a(f"{k}={g[k]}")
    a("")
    a("; Ambient occlusion (contact shadows), bloom and light shafts.")
    for k in ("SSAO", "SSAOStrength", "SSAORadius", "SSAOMinDelta", "SSAOMaxDelta", "Bloom", "BloomStrength",
              "BloomThreshold", "GodRays", "GodRaysStrength", "GodRaysDecay"):
        a(f"{k}={g[k]}")
    a("")
    a("; Size the image is drawn at. 0 = the size chosen in the game's options")
    a("; (recommended: another size can give characters a faint double).")
    a("; -1 = the monitor's own size.")
    a("RenderWidth=0")
    a("RenderHeight=0")
    a("")
    a("; Most textures ship without mipmaps (smaller copies for the distance), which")
    a("; makes them shimmer and blur far away. 1 builds them when the game loads.")
    a("GenerateMipmaps=1")
    a("; How those copies are made: 0 = soft, 1 = sharp (keeps more detail far away).")
    a(f"MipmapFilter={g['MipmapFilter']}")
    a("; 1 = leaves, hair and fences keep their density in the distance instead of")
    a("; thinning out and vanishing.")
    a(f"MipmapCoverage={g['MipmapCoverage']}")
    a("; Blends between those copies on every texture (trilinear filtering).")
    a("ForceTrilinear=1")
    a("")
    a("; Frames the graphics driver may prepare in advance: 1 = least input delay,")
    a("; 0 = the driver's own choice.")
    a("MaxFrameLatency=1")
    a("")
    return "\r\n".join(L)


# The Unreal Engine 1 games: no Direct3D 9 (no d3d9.dll, no d3d9.ini), winmm.dll instead.
# HP1's Alt+Enter was SEEN fine (2026-10-01): its Alt+Enter block is off. Its pad bindings (ACT-060,
# 2026-10-08, notes/HP1.md) use PlayStation numbers like HP2's, so an Xbox pad is renumbered too, and
# Options presses Escape as the keyboard does (SEEN in both games, 2026-10-08). Share opens no map:
# HP1 has none, and HP2's opened and closed at once (SEEN, 2026-10-08), so Ludo had it taken off.
UE1 = {
    "HP1": dict(titre="Harry Potter and the Philosopher's Stone", ini_jeu="HP.ini", pads=True, map=False,
                alt_enter=False, setup=True),
    "HP2": dict(titre="Harry Potter and the Chamber of Secrets", ini_jeu="Game.ini", pads=True, map=False,
                alt_enter=True, setup=False),
}


def winmm_ini(jeu):
    """data/<game>/winmm.ini, read by winmm.dll."""
    c = UE1[jeu]
    L = []
    a = L.append
    a("; ============================================================================")
    a(f";  {c['titre']} - PC fix")
    a(";  Accio Launcher - https://acciolauncher.be/")
    a(";  (c) 2026 Accio Launcher. PolyForm Strict 1.0.0 - see license.")
    a("; ============================================================================")
    a(";")
    a(";  Read by winmm.dll, next to the game's exe, once when the game starts.")
    a(";  1 = on, 0 = off. A line you delete falls back to its default; a missing")
    a(";  file means all defaults.")
    a("; ============================================================================")
    a("")
    a("")
    a("; ----------------------------------------------------------------------------")
    a(";  Window")
    a("; ----------------------------------------------------------------------------")
    a("[Accio.Window]")
    a("")
    a("; Writes winmm_accio.log next to the game: the controllers found, and what")
    a("; was done with them.")
    a("Log=1")
    a("")
    a("; The game's window has a title bar and a border: with a picture as large as")
    a("; the screen, the bottom went under the taskbar. 1 = a picture as large as")
    a("; the screen fills it, with no frame (taskbar hidden), without switching the")
    a("; display mode. A smaller picture stays a normal window. 0 = as shipped.")
    a("FillScreen=1")
    a("")
    a("; Alt+Enter switches the game between window and full screen: its menu")
    a("; stays at the old size (too large, cut off) and the new size is written")
    a(f"; into {c['ini_jeu']}. 1 = Alt+Enter does nothing.")
    a(f"BlockAltEnter={1 if c['alt_enter'] else 0}")
    a("")
    if c["setup"]:
        a("; At every start the game opens its first-run setup (an empty list of 3D")
        a("; cards) or, without Running.ini, tests a renderer it does not have and")
        a("; writes it into HP.ini. 1 = both skipped: the game starts with the")
        a("; renderer HP.ini names. 0 = as shipped.")
        a("SkipSetup=1")
        a("")
    a("")
    a("; ----------------------------------------------------------------------------")
    a(";  Controller")
    a("; ----------------------------------------------------------------------------")
    a("[Accio.Controller]")
    a("")
    a("; The game knows buttons by their number, and the numbers Accio Launcher binds")
    a("; are a PlayStation controller's. 1 = an Xbox controller is shown to the game")
    a("; with those numbers (A = cross, B = circle, X = square, Y = triangle, Back =")
    a("; Share, Start = Options, triggers = L2 and R2). 0 = as it is.")
    a(f"XboxLayout={1 if c['pads'] else 0}")
    a("")
    a("; The key pressed by Share (Back on Xbox) and by Options (Start), held as long")
    a("; as the button: the game opens its map and its menu only from the keyboard.")
    a("; Windows key codes: 9 = Tab (map), 27 = Escape (menu). 0 = the button stays")
    a("; a button, for a binding in User.ini.")
    a(f"ShareKey={9 if c['pads'] and c['map'] else 0}")
    a(f"OptionsKey={27 if c['pads'] else 0}")
    a("")
    return "\r\n".join(L)


# HP3 reads its pad through DirectInput 8: dinput8.dll, not winmm.dll (notes/HP3.md).
def dinput8_ini():
    """data/HP3/dinput8.ini, read by dinput8.dll."""
    L = []
    a = L.append
    a("; ============================================================================")
    a(";  Harry Potter and the Prisoner of Azkaban - PC fix")
    a(";  Accio Launcher - https://acciolauncher.be/")
    a(";  (c) 2026 Accio Launcher. PolyForm Strict 1.0.0 - see license.")
    a("; ============================================================================")
    a(";")
    a(";  Read by dinput8.dll, next to the game's exe, once when the game starts.")
    a(";  1 = on, 0 = off. A line you delete falls back to its default; a missing")
    a(";  file means all defaults.")
    a("; ============================================================================")
    a("")
    a("")
    a("; ----------------------------------------------------------------------------")
    a(";  Controller")
    a("; ----------------------------------------------------------------------------")
    a("[Accio.Controller]")
    a("")
    a("; Writes dinput8_accio.log next to the game: the controllers found, and what")
    a("; was done with them.")
    a("Log=1")
    a("")
    a("; The game has no dead zone: a stick at rest is never exactly in the middle,")
    a("; and the character crept. Percent of the travel, each side of the middle,")
    a("; read as the middle; beyond it, the stick still reaches its edge. Both")
    a("; sticks, not the triggers. 0 = as shipped.")
    a("DeadZone=15")
    a("")
    a("; The game knows buttons by their number, and the numbers Accio Launcher binds")
    a("; are a PlayStation controller's. 1 = an Xbox controller is shown to the game")
    a("; with that layout (A = cross, B = circle, X = square, Y = triangle, Back =")
    a("; Share, Start = Options, right stick and triggers where a PlayStation")
    a("; controller has them). 0 = as it is.")
    a("XboxLayout=1")
    a("")
    a("; The key pressed by Share (Back on Xbox) and by Options (Start), held as long")
    a("; as the button. The game's menu opens on Escape and listens to no controller")
    a("; button: 27 = Escape opens it and closes it. Windows key codes; 0 = the")
    a("; button stays a button, for a binding in User.ini.")
    a("ShareKey=0")
    a("OptionsKey=27")
    a("")
    return "\r\n".join(L)


# Every generated file: data/<path> -> its text.
def fichiers():
    out = {f"{game}/d3d9.ini": ini(game) for game in JEUX}
    out.update({f"{jeu}/winmm.ini": winmm_ini(jeu) for jeu in UE1})
    out["HP3/dinput8.ini"] = dinput8_ini()
    return out


if __name__ == "__main__":
    root = Path(__file__).resolve().parent.parent
    check = "--check" in sys.argv[1:]
    stale = []
    for name, text in fichiers().items():
        path = root / "data" / name
        wanted = text.encode("ascii")
        if check:
            if not path.exists() or path.read_bytes() != wanted:
                stale.append(str(path.relative_to(root)))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(wanted)
            print(path.relative_to(root), len(wanted), "bytes")
    if stale:
        print("Out of date (run python tools/make_ini.py and commit):", ", ".join(stale))
        sys.exit(1)
    if check:
        print("data/*/*.ini up to date")
