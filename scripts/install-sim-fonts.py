#!/usr/bin/env python3
"""Build SD-card fonts and install them into the simulator's emulated card.

    python3 scripts/install-sim-fonts.py                 # the S-tier four
    python3 scripts/install-sim-fonts.py --families Junicode,Lora
    python3 scripts/install-sim-fonts.py --all-curated   # every buildable family
    python3 scripts/install-sim-fonts.py --fs-dir /path/to/fs_

The default family list is `installed_families:` from
lib/EpdFont/scripts/sd-fonts.yaml — the S-tier ruling in docs/sd-card-fonts.md,
which is the set installed on every surface (both device SD cards, fs_/fonts/,
and the iOS seed bundle). It defaulted to "every curated family sd-fonts.yaml
can build" until 2026-08-03; once all 15 became buildable that meant a routine
re-run silently reinstalled the eleven the ruling excludes, so the default now
follows the ruling and --all-curated opts back into the old behavior.

Either way the list is intersected with the ACTIVE families in sd-fonts.yaml —
commented blocks (e.g. commercial fonts whose sources live only in gitignored
local_fonts/) and families this checkout cannot build are skipped with a note,
never an error. That way the same command keeps working as families are added.

Installation mirrors the iOS bundle-seeding rules (crosspoint-simulator
ios/CrossPointFsPrep.cpp): a family directory under fs_/fonts/ is made to
contain exactly the freshly built size set — stale sizes from an older ramp
are removed, families not being installed are never touched.
"""

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SCRIPTS = REPO / "lib/EpdFont/scripts"
DISPLAY_NAMES = REPO / "src/FontDisplayNames.h"


def displayname_families() -> list[str]:
    """Directory names from the FontDisplayNames entry table, in order."""
    text = DISPLAY_NAMES.read_text(encoding="utf-8")
    body = text[text.index("kEntries[]"):]
    body = body[: body.index("};")]
    return re.findall(r'\{"([^"]+)"', body)


def _yaml_config() -> dict:
    import yaml

    with open(SCRIPTS / "sd-fonts.yaml") as f:
        return yaml.safe_load(f)


def active_yaml_families() -> set[str]:
    return {fam["name"] for fam in _yaml_config().get("families", [])}


def installed_families() -> list[str]:
    """The S-tier set: what actually ships on every surface.

    Falls back to the curated FontDisplayNames set if the key is ever removed,
    so this script degrades to its pre-2026-08-03 behavior rather than
    installing nothing.
    """
    declared = _yaml_config().get("installed_families")
    return list(declared) if declared else displayname_families()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--families",
        help="Comma-separated family names (default: installed_families from sd-fonts.yaml)",
    )
    parser.add_argument(
        "--all-curated",
        action="store_true",
        help="Install every curated family sd-fonts.yaml can build, not just the S-tier set. "
             "This breaks four-family parity with the device cards and the iOS seed bundle.",
    )
    parser.add_argument(
        "--fs-dir",
        default=str(REPO / "fs_"),
        help="Simulated SD card root (default: <repo>/fs_)",
    )
    args = parser.parse_args()

    buildable = active_yaml_families()
    if args.families:
        wanted = [f.strip() for f in args.families.split(",") if f.strip()]
        missing = [f for f in wanted if f not in buildable]
        if missing:
            print(f"ERROR: not active in sd-fonts.yaml: {', '.join(missing)}", file=sys.stderr)
            return 1
        families = wanted
    else:
        if args.all_curated:
            curated = displayname_families()
            print("--all-curated: installing beyond the S-tier set; this breaks parity "
                  "with the device cards and the iOS seed bundle.")
        else:
            curated = installed_families()
        families = [f for f in curated if f in buildable]
        skipped = [f for f in curated if f not in buildable]
        if skipped:
            print(f"Skipping (no buildable source in sd-fonts.yaml): {', '.join(skipped)}")
    if not families:
        print("Nothing to build.", file=sys.stderr)
        return 1

    out_dir = SCRIPTS / "output"
    print(f"Building {len(families)} families: {', '.join(families)}")
    # --clean: a stale output/ from an earlier run with a different size ramp
    # would otherwise be globbed below and installed alongside the fresh
    # sizes (this shipped a 5-size Junicode once).
    result = subprocess.run(
        [sys.executable, str(SCRIPTS / "build-sd-fonts.py"),
         "--only", ",".join(families), "--output-dir", str(out_dir), "--clean"],
        cwd=SCRIPTS,
    )
    if result.returncode != 0:
        return result.returncode

    fonts_root = Path(args.fs_dir) / "fonts"
    fonts_root.mkdir(parents=True, exist_ok=True)
    for family in families:
        built = sorted((out_dir / family).glob("*.cpfont"))
        if not built:
            print(f"  WARNING: no output for {family}, leaving it untouched", file=sys.stderr)
            continue
        dest = fonts_root / family
        dest.mkdir(exist_ok=True)
        # Exactly the built size set: install the new files, then drop sizes
        # the ramp no longer carries so the family cannot become a mixed-ramp
        # hybrid of old and new builds.
        keep = {p.name for p in built}
        for f in built:
            shutil.copy2(f, dest / f.name)
        for old in dest.glob("*.cpfont"):
            if old.name not in keep:
                old.unlink()
                print(f"  pruned stale {old.relative_to(fonts_root.parent)}")
        # Hi-res companions (dest/2x/<same 1x filename>, see
        # SdCardFontManager::hiResCompanionPath) are built out-of-band at
        # doubled point sizes; this script cannot regenerate them, but a ramp
        # change makes the old set unreachable (lookup is by 1x filename) and
        # leaves it lying as dead weight. Prune the orphans and say so.
        hires = dest / "2x"
        if hires.is_dir():
            for old in hires.glob("*.cpfont"):
                if old.name not in keep:
                    old.unlink()
                    print(f"  pruned orphaned hi-res {old.relative_to(fonts_root.parent)}")
            if not any(hires.glob("*.cpfont")):
                print(f"  NOTE: {family} hi-res set now empty — rebuild at 2x sizes "
                      f"and install under {hires.relative_to(fonts_root.parent)}")
        print(f"  installed {family}: {len(built)} sizes -> {dest}")

        # The firmware scans /.fonts BEFORE /fonts and dedups by family name
        # (SdCardFontRegistry::discover), so a stale copy of this family in
        # the hidden root would silently shadow everything just installed.
        hidden = Path(args.fs_dir) / ".fonts" / family
        if hidden.is_dir():
            # Carry a hi-res set over before deleting: the hidden copy may hold
            # the only 2x build (this script cannot regenerate those), and the
            # visible-root family it now shadows should inherit it. Only sizes
            # matching the fresh ramp survive; the rest are orphans anyway.
            hidden_hires = hidden / "2x"
            if hidden_hires.is_dir() and not (dest / "2x").is_dir():
                salvaged = [f for f in hidden_hires.glob("*.cpfont") if f.name in keep]
                if salvaged:
                    (dest / "2x").mkdir()
                    for f in salvaged:
                        shutil.copy2(f, dest / "2x" / f.name)
                    print(f"  salvaged {len(salvaged)} hi-res files from {hidden_hires.relative_to(fonts_root.parent)}")
            shutil.rmtree(hidden)
            print(f"  removed shadowing stale copy {hidden.relative_to(fonts_root.parent)}")

    print("\nDone. Launch the simulator; the families appear in Text Settings.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
