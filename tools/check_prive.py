# Accio Launcher - PC fix for the EA Harry Potter games.
# Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
#
"""Fails when something private or forbidden is about to ship in the public repository.

    python tools/check_prive.py

Two gates, both over the files GIT TRACKS (what a push would publish), nothing else:

  * no private path: CLAUDE.md and the notes/ sheets live in the workshop, not here.
    They are gitignored, but one edit to .gitignore would be enough to leak them;
  * no forbidden word: three author names (their zlib/MIT code was rewritten, not
    reused, so their names must not appear) and the name of HP3's D3D8 translator
    (a rule of the project: it is only ever called "graphics settings").

The words are stored as hex so this file never contains them, and a hit is reported
masked ("word #2"): the list is in the project memory, not on screen. Run by the
Build workflow before anything is compiled.
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Paths that must never be tracked (private project memory).
PRIVATE = ("CLAUDE.md", "notes/")

# Forbidden words, lowercase, hex-encoded on purpose (see the docstring).
WORDS = [bytes.fromhex(h) for h in (
    "636869702d62697363756974",  # word #1: author 1
    "746869727465656e6167",      # word #2: author 2
    "726965646c696e676572",      # word #3: author 3
    "6467766f6f646f6f",          # word #4: HP3's D3D8 translator
)]


def main() -> int:
    out = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-z"],
                         capture_output=True, check=True).stdout
    files = [f.decode("utf-8") for f in out.split(b"\0") if f]
    errors = 0

    for name in files:
        if name in PRIVATE or any(name.startswith(p) for p in PRIVATE if p.endswith("/")):
            print(f"{name}: private file tracked by git, must stay in the workshop.")
            errors += 1

    for name in files:
        data = (ROOT / name).read_bytes().lower()
        for i, word in enumerate(WORDS, 1):
            pos = data.find(word)
            if pos != -1:
                line = data.count(b"\n", 0, pos) + 1
                print(f"{name}:{line}: forbidden word #{i} (masked; list in the project memory).")
                errors += 1

    if errors:
        print(f"{errors} problem(s).")
        return 1
    print(f"OK: {len(files)} tracked files, nothing private, no forbidden word.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
