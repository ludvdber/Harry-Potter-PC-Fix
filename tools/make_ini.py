"""Generates data/HP4, data/HP5 and data/HP6/d3d9.ini: the shipped settings of the three games.

    python tools/make_ini.py          writes the three files
    python tools/make_ini.py --check  fails if a committed file differs (run by the Build workflow)

The three files are written by this one script so that a setting added to the DLL reaches the
three games together. Edit here, run it, commit the script and the files together.
ASCII, CRLF: the games' own ini files are in that form, and GetPrivateProfile reads it as is.
"""
import sys
from pathlib import Path

JEUX = {
    "HP4": dict(titre="Harry Potter and the Goblet of Fire", resolution="800x600",
                fps_limit=100, center=1, dpi=0, fov_note="0 = as shipped (114.6 degrees)",
                animations=True, cap=120, unlock=False, haze=True, aspect="16:9"),
    "HP5": dict(titre="Harry Potter and the Order of the Phoenix", resolution="640x480",
                fps_limit=0, center=1, dpi=1, fov_note="0 = as shipped",
                animations=False, cap=120, unlock=True, haze=False,
                # The image HP5 has shipped with since the fix existed (validated in game).
                graphics=dict(FXAA=1, Antialiasing=16, AnisotropicFiltering=16, TextureLODBias=-1.5,
                              VSync=1, ColorGrading=1, Vibrance="0.45", Vignette="0.08", Gain="1.08",
                              Temperature="0.04", Tint="0.04", Contrast="0.30", SplitTone="0.18",
                              Bloom=1, GodRays=1, SSAO=1, SSAOStrength="0.55",
                              SSAOMinDelta="0.02", SSAOMaxDelta="0.15")),
    "HP6": dict(titre="Harry Potter and the Half-Blood Prince", resolution="640x480",
                fps_limit=0, center=0, dpi=0, fov_note="0 = as shipped",
                animations=False, cap=120, unlock=True, haze=False),
}

GRAPHICS_OFF = dict(FXAA=0, Sharpness="0.40", Antialiasing=0, AnisotropicFiltering=0, TextureLODBias=0,
                    SSAAFactor=1, ShadowMapScale=1, VSync=0, ColorGrading=0, Vibrance="0.25",
                    Vignette="0.00", Lift="0.00", Gamma="1.00", Gain="1.00", Temperature="0.00",
                    Tint="0.00", Contrast="0.00", SplitTone="0.00", SSAO=0, SSAOStrength="0.50",
                    SSAORadius="6.0", SSAOMinDelta="0.0005", SSAOMaxDelta="0.05", Bloom=0,
                    BloomStrength="0.35", BloomThreshold="0.75", GodRays=0, GodRaysStrength="0.45",
                    GodRaysDecay="0.96")


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
    a("; click on another monitor) instead of freezing it. Keyboard and mouse are")
    a("; taken back as soon as the game is in front again.")
    a("KeepRunningInBackground=1")
    a("")
    a("; Placement of a window (styles 2 to 4).")
    a(f"CenterWindow={j['center']}")
    a("; 1 = always on the main monitor, 0 = the monitor the game opens on.")
    a("UsePrimaryMonitor=0")
    a("AlwaysOnTop=0")
    a("")
    a("; The game was made before Windows display scaling. 0 keeps its original")
    a("; behaviour; 1 makes it ignore scaling (sharper at 125 % and up, but the")
    a("; window and menus may change size).")
    a(f"DPIAware={j['dpi']}")
    a("")
    a("; Frame-rate limit (0 = none).")
    a(f"FPSLimit={j['fps_limit']}")
    a("")
    a("; Frame rate and frame time in the corner of the screen.")
    a("ShowFPS=0")
    a("")
    a("; Screenshot key, as a Windows virtual-key code (123 = F12, 44 = Print")
    a("; Screen, 0 = none). Pictures go to the \"screenshots\" folder next to the")
    a("; game.")
    a("ScreenshotKey=123")
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
    a(";  Changes made inside the game")
    a("; ----------------------------------------------------------------------------")
    a("[Accio.Game]")
    a("")
    a(f"; The resolution the game starts with (as shipped: {j['resolution']}). You can")
    a("; still change it in the game's own options.")
    a("Width=1920")
    a("Height=1080")
    a("")
    a("; Aspect ratio of the image: 0 = as shipped, or a ratio such as 16:10,")
    a("; 21:9, 32:9, 2.37.")
    a(f"AspectRatio={j.get('aspect', 0)}")
    a("")
    a(f"; Field of view, as a factor: {j['fov_note']}; 1.15, 1.25 or 1.40 widen it.")
    a("FOV=0")
    if j["animations"]:
        a("")
        a("; Character animations are played at 20 frames per second. 25 or 30 makes")
        a("; them smoother; 0 = as shipped.")
        a("AnimationRate=0")
    if j["unlock"]:
        a("")
        a("; The game runs at 30 frames per second as shipped. 1 lifts that limit.")
        a("UnlockFrameRate=1")
        a("")
        a("; Highest frame rate the game allows itself. The original fix notes that the")
        a("; mouse behaves oddly above 59; 0 = the game's own value.")
        a(f"FrameRateCap={j['cap']}")
    else:
        a("")
        a("; The game's frame-rate reference: 60 as shipped, which holds it back.")
        a("; 120 lets it follow FPSLimit, as the earlier fix did; 0 = as shipped.")
        a(f"FrameRateCap={j['cap']}")
    if j["haze"]:
        a("")
        a("; The haze of the Forbidden Forest, the lake, the maze and the graveyard. As")
        a("; shipped, it crashes the game on screens wider than 2048 pixels.")
        a("; 0 = not drawn (the earlier fix's cure), 1 = drawn, with the crash fixed,")
        a("; 2 = drawn as shipped.")
        a("HazeOverlay=0")
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
    a("; card cannot).")
    a(f"Antialiasing={g['Antialiasing']}")
    a("")
    a("; Texture sharpness at an angle: 0, 2, 4, 8 or 16.")
    a(f"AnisotropicFiltering={g['AnisotropicFiltering']}")
    a("")
    a("; Negative = sharper distant textures (-1.0 is a good value), 0 = as shipped.")
    a(f"TextureLODBias={g['TextureLODBias']}")
    a("")
    a("; Render at 2x, 3x or 4x the resolution, then scale down. Very demanding.")
    a(f"SSAAFactor={g['SSAAFactor']}")
    a("")
    a("; Sharper shadows (1, 2 or 4). Experimental: also enlarges reflections.")
    a(f"ShadowMapScale={g['ShadowMapScale']}")
    a("")
    a(f"VSync={g['VSync']}")
    a("")
    a("; Colour adjustments.")
    for k in ("ColorGrading", "Vibrance", "Vignette", "Lift", "Gamma", "Gain", "Temperature", "Tint",
              "Contrast", "SplitTone"):
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
    a("; Blends between those copies on every texture (trilinear filtering).")
    a("ForceTrilinear=1")
    a("")
    a("; Frames the graphics driver may prepare in advance: 1 = least input delay,")
    a("; 0 = the driver's own choice.")
    a("MaxFrameLatency=1")
    a("")
    return "\r\n".join(L)



if __name__ == "__main__":
    root = Path(__file__).resolve().parent.parent
    check = "--check" in sys.argv[1:]
    stale = []
    for game in JEUX:
        path = root / "data" / game / "d3d9.ini"
        wanted = ini(game).encode("ascii")
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
        print("data/*/d3d9.ini up to date")
