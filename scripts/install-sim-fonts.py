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
are removed.

All THREE render-scale tiers are built and installed: 1x (what the device
reads) plus the 2x and 3x hi-res companions a host build reads. Until
2026-08-20 this script rebuilt 1x only and merely pruned the hi-res sets by
filename, so a font-config fix landed on the device tier and left every scaled
host build on the old glyphs — see B-035.

A full run additionally removes whole family directories a tier ruling has
CUT from installed_families: (see prune_cut_families below for exactly which
names are eligible). Until 2026-08-12 it only ever pruned sizes WITHIN a
family it was installing, so a demoted family sat on the card forever and had
to be deleted by hand.
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
EDITOR_FONTS = REPO / "src/notes/EditorFonts.h"

# The hi-res tiers every surface carries, alongside the 1x base tier.
#
# 1x is what the DEVICE reads; 2x and 3x are what a host build reads at the
# matching CROSSPOINT_RENDER_SCALE (docs/render-scale.md), and the standing
# ruling of 2026-08-15 is that all three ship. The owner inspects this project
# through the simulator, so a tier left stale is a fix that looks applied and
# is not -- see the hi-res install block in main() for the case that proved it.
HIRES_TIERS = (2, 3)

# Codepoints omitted from ONE hi-res tier used to be TWO TABLES HERE, and that
# is the whole story of docs/inknut-l-slot-2026-08-26.md.
#
# EpdGlyph stores glyph width/height in uint8, so anything rasterising over
# 255 px is unrepresentable and the build aborts rather than truncating. Which
# codepoint offends is a property of the SIZE, not of the family, so the drop
# is tier-local: U+2E3B is still built into the tiers where it fits and only a
# host build at the affected tier falls back for it, per glyph.
#
# The tables now live in sd-fonts.yaml -- `tier_drops:` at the top level and
# `hires_drops:` per family -- because this script is not the only caller.
# `build-sd-fonts.py --only X --scale 2` is the documented way to fill the iOS
# bundle's build/seedfonts tree (docs/ios-app-size.md), it never comes through
# here, and with the knowledge parked in this file it had no way to know that
# InknutJunicode cannot build a 2x tier at all. It aborted, left partial output
# under fontconvert's rasterisation-ppem names, and one of those names collided
# with a real slot.
#
# build-sd-fonts.py resolves both layers PER FAMILY now, so this script passes
# no --drop-codepoints of its own and no longer has to split a tier into one
# invocation per distinct drop set. That split existed only because
# --drop-codepoints is global to a run, and batching families with different
# needs would have stripped a codepoint from a family that carries it fine.


def displayname_families() -> list[str]:
    """Directory names from the FontDisplayNames entry table, in order."""
    text = DISPLAY_NAMES.read_text(encoding="utf-8")
    body = text[text.index("kEntries[]"):]
    body = body[: body.index("};")]
    return re.findall(r'\{"([^"]+)"', body)


def editor_families() -> set[str]:
    """The EDITOR font group — writing faces, a separate list from the S tier.

    Owner ruling 2026-08-05 (src/notes/EditorFonts.h): these are chosen by
    SETTINGS.editorFont and never join the reading tier, so they are legitimately
    on a card while absent from installed_families:. Pruning must skip them or a
    card-only row (builtinFontId == 0) loses its face.
    """
    text = EDITOR_FONTS.read_text(encoding="utf-8")
    body = text[text.index("FAMILIES[] = {"):]
    body = body[: body.index("};")]
    return set(re.findall(r'\{"([^"]+)"', body))


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


def prune_cut_families(fs_dir: Path, keep: set[str], known: set[str]) -> None:
    """Delete family directories a tier ruling has cut from the installed set.

    Only a FULL run calls this — a `--families <subset>` run installs a subset
    deliberately and must not read as "everything else is cut."

    Three fences, because this deletes recursively:
      * `known` is sd-fonts.yaml's `families:`. A directory the yaml has no
        recipe for is the owner's own font, or a family whose block is commented
        out, and this script has no opinion about it — never touched.
      * `keep` is the reference set for this run PLUS the editor group. It is the
        DECLARED list, not the buildable intersection, so a family this checkout
        cannot rebuild (commercial sources in gitignored local_fonts/) keeps the
        copy already on the card instead of losing an unregenerable build.
      * Both scan roots, since /.fonts and /fonts are equally live
        (SdCardFontRegistry::discover) and a cut family in either still shows up
        in the picker.
    A family's `2x/` companions live inside its directory and go with it.
    """
    for root_name in ("fonts", ".fonts"):
        root = fs_dir / root_name
        if not root.is_dir():
            continue
        for entry in sorted(root.iterdir()):
            if not entry.is_dir() or entry.name in keep or entry.name not in known:
                continue
            shutil.rmtree(entry)
            print(f"  pruned cut family {entry.relative_to(fs_dir)}")


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
    # The set this run claims to be the whole installed set — None for a
    # deliberate subset, which is what gates stale-family pruning below.
    reference: set[str] | None = None
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
        reference = set(curated)
        families = [f for f in curated if f in buildable]
        skipped = [f for f in curated if f not in buildable]
        if skipped:
            print(f"Skipping (no buildable source in sd-fonts.yaml): {', '.join(skipped)}")
    if not families:
        print("Nothing to build.", file=sys.stderr)
        return 1

    out_dir = SCRIPTS / "output"
    print(f"Building {len(families)} families: {', '.join(families)}")
    # Build the base tier and every hi-res tier in one run, so all three reach
    # the card together. build-sd-fonts.py --scale N writes into
    # <output>/<Family>/<N>x/ and renames each cut back to its 1x basename
    # (build-sd-fonts.py:587,600), which is exactly the layout installed below.
    #
    # --clean goes on the FIRST tier only. It rmtree's the whole output dir
    # (build-sd-fonts.py:865), so repeating it would delete the tier just
    # built; its job is dropping a stale ramp from an earlier run, and one
    # clean at the start does that. (A stale output/ globbed alongside fresh
    # sizes once shipped a 5-size Junicode.)
    first_invocation = True
    for tier in (1, *HIRES_TIERS):
        cmd = [sys.executable, str(SCRIPTS / "build-sd-fonts.py"),
               "--only", ",".join(families), "--output-dir", str(out_dir)]
        if first_invocation:
            # --clean rmtree's the whole output dir; once, up front.
            cmd.append("--clean")
            first_invocation = False
        if tier != 1:
            cmd.extend(["--scale", str(tier)])
        result = subprocess.run(cmd, cwd=SCRIPTS)
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
        # Hi-res companions (dest/<N>x/<same 1x filename>, see
        # SdCardFontManager::hiResCompanionPath, which splices "<N>x/" in front
        # of the 1x basename). These are now built in the same run as 1x above.
        #
        # WHY THAT MATTERS, since this script used to only prune them: a fix
        # made in sd-fonts.yaml reaches a tier only when that tier is rebuilt.
        # The 2026-08-17 arrow fix was correct in the config and was installed
        # into 1x by this script, while every 2x and 3x file kept the old
        # glyphless cut -- so arrows came back on the device and stayed missing
        # on every scaled host build, which is where they were being looked at.
        # Rebuilding one tier and pruning the others by filename cannot detect
        # that: the names match, only the pixels are stale.
        for tier in HIRES_TIERS:
            hires_src = out_dir / family / f"{tier}x"
            hires = dest / f"{tier}x"
            built_hires = sorted(hires_src.glob("*.cpfont")) if hires_src.is_dir() else []
            if built_hires:
                hires.mkdir(exist_ok=True)
                for f in built_hires:
                    shutil.copy2(f, hires / f.name)
                print(f"  installed {family} {tier}x: {len(built_hires)} sizes")
            if not hires.is_dir():
                continue
            # A ramp change makes an old cut unreachable (lookup is by 1x
            # filename) and leaves it lying as dead weight.
            for old in hires.glob("*.cpfont"):
                if old.name not in keep:
                    old.unlink()
                    print(f"  pruned orphaned hi-res {old.relative_to(fonts_root.parent)}")
            if not any(hires.glob("*.cpfont")):
                print(f"  NOTE: {family} {tier}x set now empty — no hi-res cut was built")
        print(f"  installed {family}: {len(built)} sizes -> {dest}")

        # The firmware scans /.fonts BEFORE /fonts and dedups by family name
        # (SdCardFontRegistry::discover), so a stale copy of this family in
        # the hidden root would silently shadow everything just installed.
        hidden = Path(args.fs_dir) / ".fonts" / family
        if hidden.is_dir():
            # Carry a hi-res set over before deleting, for the case where the
            # build produced no 2x cut of its own (the guard below): the hidden
            # copy may then hold the only one, and the visible-root family it
            # shadows should inherit it. Only sizes matching the fresh ramp
            # survive; the rest are orphans anyway.
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

    if reference is not None:
        prune_cut_families(Path(args.fs_dir), reference | editor_families(), buildable)

    print("\nDone. Launch the simulator; the families appear in Reader Font.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
