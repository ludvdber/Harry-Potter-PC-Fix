# Accio Launcher - PC fix for the EA Harry Potter games.
# Copyright (c) 2026 Accio Launcher. PolyForm Strict 1.0.0, see license.
#
"""Fails when README.md (French) and README.en.md (English) no longer say the same thing.

    python tools/check_readme_parity.py

Both are kept by hand, together. They drifted anyway: the English one lost the table of
sources and the release steps, and both kept "three games" after HP7a and HP7b shipped
(audit 2026-10-07, P2-005 and P3-004). Prose cannot be compared across languages, but
what does not get translated can:

  * the headings, level by level;
  * every `code` token, in order (file names, settings, keys, commands);
  * every link target, badges and the language switch aside;
  * the number of rows of each table.

A token that IS translated (a Windows folder name, a colour order) goes in EXCEPTIONS,
as a French/English pair, on purpose. Run by the Build workflow.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# French token -> its English translation: legitimate differences, nothing else.
EXCEPTIONS = {
    "Images\\Accio Launcher\\<jeu>": "Pictures\\Accio Launcher\\<game>",
    "rouge,vert,bleu": "red,green,blue",
}
LANGUAGE_SWITCH = {"README.md", "README.en.md"}

CODE = re.compile(r"`([^`\n]+)`")
LINK = re.compile(r"\]\(([^)\s]+)\)")
HEADING = re.compile(r"^(#{1,6}) ")


def read(name: str) -> list[str]:
    return (ROOT / name).read_text(encoding="utf-8").splitlines()


def headings(lines: list[str]) -> list[int]:
    return [len(m.group(1)) for line in lines if (m := HEADING.match(line))]


def codes(lines: list[str], translate: dict[str, str]) -> list[str]:
    fenced = False
    out = []
    for line in lines:
        if line.startswith("```"):
            fenced = not fenced
            continue
        if fenced:
            out.append(line.strip())
            continue
        out.extend(translate.get(c, c) for c in CODE.findall(line))
    return out


def links(lines: list[str]) -> list[str]:
    found = []
    for line in lines:
        if line.startswith("[![") or "img.shields.io" in line:
            continue
        # An anchor is a translated heading: only where it stands is compared.
        found.extend("#anchor" if u.startswith("#") else u
                     for u in LINK.findall(line) if u not in LANGUAGE_SWITCH)
    return found


def table_rows(lines: list[str]) -> list[int]:
    rows, count = [], 0
    for line in lines:
        if line.startswith("|"):
            count += 1
        elif count:
            rows.append(count)
            count = 0
    if count:
        rows.append(count)
    return rows


def first_difference(a: list, b: list) -> str:
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return f"item {i + 1}: French {x!r}, English {y!r}"
    return f"French has {len(a)}, English {len(b)}"


def main() -> int:
    fr, en = read("README.md"), read("README.en.md")
    checks = (
        ("headings (levels in order)", headings(fr), headings(en)),
        ("code tokens", codes(fr, EXCEPTIONS), codes(en, {})),
        ("link targets", links(fr), links(en)),
        ("rows of each table", table_rows(fr), table_rows(en)),
    )
    failed = False
    for what, a, b in checks:
        if a != b:
            failed = True
            print(f"README.md and README.en.md differ in their {what}: {first_difference(a, b)}")
    if not failed:
        print("README.md and README.en.md are at parity.")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
