#!/usr/bin/env python3
"""Build SD-card fonts and install them into the simulator's emulated card.

    python3 scripts/install-sim-fonts.py                 # the display-name set
    python3 scripts/install-sim-fonts.py --families Junicode,Lora
    python3 scripts/install-sim-fonts.py --fs-dir /path/to/fs_

The default family list is the intersection of the families named in
src/FontDisplayNames.h (the set actually curated for the picker) with the
ACTIVE families in lib/EpdFont/scripts/sd-fonts.yaml — commented blocks
(e.g. commercial fonts whose sources live only in gitignored local_fonts/)
and families this checkout cannot build are skipped with a note, never an
error. That way the same command keeps working as families are added.

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


def active_yaml_families() -> set[str]:
    import yaml

    with open(SCRIPTS / "sd-fonts.yaml") as f:
        config = yaml.safe_load(f)
    return {fam["name"] for fam in config.get("families", [])}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--families",
        help="Comma-separated family names (default: FontDisplayNames set that sd-fonts.yaml can build)",
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
        curated = displayname_families()
        families = [f for f in curated if f in buildable]
        skipped = [f for f in curated if f not in buildable]
        if skipped:
            print(f"Skipping (no buildable source in sd-fonts.yaml): {', '.join(skipped)}")
    if not families:
        print("Nothing to build.", file=sys.stderr)
        return 1

    out_dir = SCRIPTS / "output"
    print(f"Building {len(families)} families: {', '.join(families)}")
    result = subprocess.run(
        [sys.executable, str(SCRIPTS / "build-sd-fonts.py"),
         "--only", ",".join(families), "--output-dir", str(out_dir)],
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
        print(f"  installed {family}: {len(built)} sizes -> {dest}")

    print("\nDone. Launch the simulator; the families appear in Text Settings.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
