#!/usr/bin/env python3
"""Build SD card fonts from a declarative YAML config.

Reads sd-fonts.yaml, downloads any missing source fonts, and runs
fontconvert_sdcard.py in parallel for each family.

The "download" here is this script's own, on a developer's machine, fetching
source .ttf files it does not have. It is unrelated to the device-side font
download that used to exist -- that feature and its fonts.json manifest were
removed on 2026-08-10 (it could never install a family completely).

Usage:
    # Generate fonts (output in ./output/)
    python3 build-sd-fonts.py

    # Custom config / output paths
    python3 build-sd-fonts.py --config my-fonts.yaml --output-dir dist/

    # Generate only specific families
    python3 build-sd-fonts.py --only Literata,IBMPlexMono

    # Stream child process output for debugging
    python3 build-sd-fonts.py --verbose

    # Override the per-family timeout (default: 600s)
    python3 build-sd-fonts.py --timeout 1200
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import socket
import urllib.request
import zipfile
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

import yaml

SCRIPT_DIR = Path(__file__).parent
FONTCONVERT = SCRIPT_DIR / "fontconvert_sdcard.py"
EPDFONTS_DIR = SCRIPT_DIR.parent  # lib/EpdFont
DEFAULT_CONFIG = SCRIPT_DIR / "sd-fonts.yaml"
DEFAULT_OUTPUT = SCRIPT_DIR / "output"
DOWNLOAD_DIR = SCRIPT_DIR / "downloaded_fonts"
INSTANCE_DIR = SCRIPT_DIR / "instanced_fonts"
PATCHED_DIR = SCRIPT_DIR / "patched_fonts"
SCALED_DIR = SCRIPT_DIR / "scaled_fonts"
# Every codepoint a family's own cmap misses is filled from a fallback face, so
# the fallback's coverage IS the shipped coverage for whatever `intervals:` asks
# for beyond the face -- see the "so the Noto fallback supplies them" notes
# throughout sd-fonts.yaml. The fallback is therefore chosen PER FAMILY, from
# what that family's intervals request, not one global default:
#
#   Latin-only intervals  -> Libre Franklin, committed, no network needed
#   anything wider        -> Noto Sans, downloaded into the gitignored cache
#
# History. b8023ff8 deleted Noto Sans from the fork (built-in UI face ruling)
# including its source TTFs, but left it wired in here, so every SD font build
# died at startup on the missing file. 77a5901b repointed the ONE global default
# at the committed Libre Franklin because that was "the one face guaranteed to
# be present"; it measured the cost on one latin-ext family (iA Writer Quattro)
# and ruled the loss acceptable there, which it is. What it could not cover with
# that measurement is the families whose intervals ask for Greek, Cyrillic, math
# and symbols: Libre Franklin is Latin-only, so those came back EMPTY, and a
# routine `install-sim-fonts.py` rebuild silently shipped a worse font than the
# cards already carry -- TeXGyreSchola 1683 -> 997 glyphs per style, Coelacanth
# 1642 -> 1415, LibreFranklin 1676 -> 895, with no error at any point.
#
# Its own coverage note pointed here: "A future CJK, Arabic or Hebrew family
# needs its own fallback passed per-family, not this default." Selecting per
# family fixes the coverage loss without paying for it on the Latin-only
# families, where filling Latin Extended-B and exotic punctuation from Noto adds
# ~385 glyphs per style and ~50% file size for glyphs 77a5901b already ruled
# unnecessary (measured on iA Writer Quattro and Libris ADF, 2026-08-12).
#
# Noto is fetched rather than committed: it is the mechanism sd-fonts.yaml
# already uses for nearly every family source, and the one lib/EpdFont/
# builtinFonts/source/.gitignore describes ("Fonts like NotoSansCJK are
# downloaded on demand by build-sd-fonts.py"). This URL's cmap is
# codepoint-identical to the repo copy b8023ff8 deleted over the `reading`,
# `latin-ext,greek,cyrillic` and `latin-ext` interval sets (verified
# 2026-08-12), so a rebuild reproduces the shipped .cpfonts' coverage exactly.
#
# Noto Sans is still Latin+Greek+Cyrillic+symbols only. A future CJK, Arabic or
# Hebrew family needs its own fallback named in its recipe, not either of these.
LATIN_FALLBACK_FONT = EPDFONTS_DIR / "builtinFonts/source/LibreFranklin/LibreFranklin-Regular.ttf"
BROAD_FALLBACK_URL = (
    "https://raw.githubusercontent.com/notofonts/notofonts.github.io/"
    "main/fonts/NotoSans/unhinted/ttf/NotoSans-Regular.ttf"
)
BROAD_FALLBACK_FONT = DOWNLOAD_DIR / "_fallback" / "NotoSans-Regular.ttf"

# The blocks Libre Franklin is qualified to serve: Latin proper, its phonetic
# and combining companions, the Latin Extended Additional/Vietnamese block,
# General Punctuation and the f-ligatures -- exactly the reach 77a5901b measured
# it over. `latin-ext` resolves inside this set; `reading`, `greek`, `cyrillic`,
# `symbols` and the rest do not.
LATIN_FALLBACK_RANGES = [
    (0x0000, 0x024F),  # ASCII, Latin-1, Latin Extended-A/B
    (0x02B0, 0x02FF),  # Spacing modifier letters
    (0x0300, 0x036F),  # Combining diacritics
    (0x1E00, 0x1EFF),  # Latin Extended Additional (Vietnamese)
    (0x2000, 0x206F),  # General punctuation
    (0xFB00, 0xFB06),  # f-ligatures
    (0xFFFD, 0xFFFD),  # Replacement char, appended to every interval set
]


def fallback_font_for(intervals: str) -> Path:
    """Pick the fallback face for one family, from its `intervals:` string.

    Libre Franklin when every requested codepoint is Latin (committed, offline,
    and what 77a5901b measured); Noto Sans otherwise.
    """
    from fontconvert_sdcard import resolve_intervals

    for start, end in resolve_intervals(intervals):
        if not any(lo <= start and end <= hi for lo, hi in LATIN_FALLBACK_RANGES):
            return BROAD_FALLBACK_FONT
    return LATIN_FALLBACK_FONT


_orig_getaddrinfo = socket.getaddrinfo


def _ipv4_only_getaddrinfo(*args, **kwargs):
    """getaddrinfo variant that drops AAAA records (IPv4 only)."""
    return [ai for ai in _orig_getaddrinfo(*args, **kwargs) if ai[0] == socket.AF_INET]


def download_font(url: str, dest: Path, retries: int = 3) -> Path:
    """Download a font file if not already cached. Returns the local path.

    Some sources (e.g. mirrors.ctan.org) are round-robin redirectors that land
    on a different mirror each request; a mirror may advertise an IPv6 address a
    host without an IPv6 route cannot reach ([Errno 101] Network is unreachable).
    Retry on failure, forcing IPv4 resolution after the first attempt.
    """
    if dest.exists():
        return dest
    dest.parent.mkdir(parents=True, exist_ok=True)
    print(f"  Downloading {dest.name}...")
    last_err = None
    for attempt in range(1, retries + 1):
        force_ipv4 = attempt > 1
        if force_ipv4:
            socket.getaddrinfo = _ipv4_only_getaddrinfo
        try:
            urllib.request.urlretrieve(url, dest)
            break
        except Exception as e:  # noqa: BLE001 - reported via RuntimeError below
            last_err = e
            dest.unlink(missing_ok=True)
            if attempt < retries:
                print(f"  Attempt {attempt} failed ({e}); retrying (IPv4-only)...")
        finally:
            if force_ipv4:
                socket.getaddrinfo = _orig_getaddrinfo
    else:
        raise RuntimeError(f"Failed to download {url}: {last_err}") from last_err
    size_kb = dest.stat().st_size / 1024
    print(f"  Downloaded {dest.name} ({size_kb:.0f} KB)")
    return dest


def extract_static_instance(source_path: Path, axes: dict, family_name: str, style_name: str) -> Path:
    """Use fonttools instancer to pin variable font axes, producing a static TTF.

    Caches the result in INSTANCE_DIR/<family>/<style>_<axes>_<mtime>.ttf.
    Returns the path to the static font file.
    """
    from fontTools.varLib.instancer import instantiateVariableFont
    from fontTools.ttLib import TTFont

    mtime = int(source_path.stat().st_mtime)
    axis_key = "_".join(f"{k}{v}" for k, v in sorted(axes.items()))
    cache_name = f"{style_name}_{axis_key}_{mtime}.ttf"
    cached = INSTANCE_DIR / family_name / cache_name

    if cached.exists():
        return cached

    # Clean old cached instances for this style
    cached.parent.mkdir(parents=True, exist_ok=True)
    for old in cached.parent.glob(f"{style_name}_*.ttf"):
        old.unlink()

    print(f"  Extracting static instance: {family_name}/{style_name} ({axis_key})")
    # Atomic write: save to a temp file first, then rename. A crash or save()
    # exception would otherwise leave a corrupt `cached` file that future runs
    # would happily reuse via the `cached.exists()` check above.
    tmp_fd, tmp_name = tempfile.mkstemp(suffix=".ttf", dir=cached.parent)
    os.close(tmp_fd)
    tmp_path = Path(tmp_name)
    # Keep separate handles for the source variable font and the static
    # instance: instantiateVariableFont with default inplace=False returns a
    # *new* TTFont, so rebinding `font` would otherwise strand the source's
    # file handle open until GC runs.
    #
    # updateFontNames=True   — rewrite the name table so the saved font
    #                          reports its weight/style accurately rather
    #                          than retaining the variable-font names.
    # optimize=False         — skip the gvar interpolation optimisation;
    #                          fully pinning every axis drops gvar anyway,
    #                          so the work would be wasted.
    source_font = TTFont(str(source_path))
    try:
        # updateFontNames=True rewrites the name table from the STAT axis-value
        # records, which only exist for NAMED instances -- it raises
        # "Cannot find Axis Values {...}" the moment you pin an axis to a
        # coordinate the foundry did not name. Pinning an unnamed coordinate is
        # the entire point of a variable font (Junicode's ENLA x-height axis is
        # continuous 0-100 with no named stops at all), so fall back to leaving
        # the names alone. Nothing downstream reads them: the family name comes
        # from the yaml and the converter reads outlines.
        try:
            font = instantiateVariableFont(source_font, axes, updateFontNames=True, optimize=False)
        except Exception as name_err:  # noqa: BLE001 - reported, then retried
            print(f"  (unnamed axis position, keeping source names: {name_err})")
            source_font.close()
            source_font = TTFont(str(source_path))
            font = instantiateVariableFont(source_font, axes, updateFontNames=False, optimize=False)
        try:
            font.save(str(tmp_path))
        finally:
            font.close()
    except Exception:
        tmp_path.unlink(missing_ok=True)
        raise
    finally:
        source_font.close()
    tmp_path.replace(cached)

    return cached


def apply_cmap_drops(source_path: Path, codepoints, family_name: str, style_name: str) -> Path:
    """Delete cmap entries so the Noto fallback fills those codepoints instead.

    For sources whose cmap points at the WRONG outline. fontconvert_sdcard.py
    reaches for the fallback face only when `face.get_char_index(cp) == 0`
    (see its `load_glyph`), so a codepoint that maps to a real-but-wrong glyph
    renders that wrong glyph and never falls back. Removing the entry is the
    only lever: the font has no correct outline to point at.

    Not a styling knob — reach for it only with a rendered contact sheet
    showing the wrong letter, and list the exact codepoints. Narrowing the
    family's `intervals:` would NOT work: a codepoint outside every interval is
    dropped from the build entirely rather than fallen back.

    Patched into PATCHED_DIR/<family>/cmap/, a directory of its own so
    apply_metrics_override's stale-file sweep (which globs `<style>_*` in
    PATCHED_DIR/<family>/) cannot delete this function's output — which is its
    input when both are configured.
    """
    from fontTools.ttLib import TTFont

    wanted = sorted({int(c) for c in codepoints})
    if not wanted:
        return source_path

    mtime = int(source_path.stat().st_mtime)
    key = "_".join(f"{c:04X}" for c in wanted)
    cached = PATCHED_DIR / family_name / "cmap" / f"{style_name}_{key}_{mtime}{source_path.suffix}"
    if cached.exists():
        return cached

    cached.parent.mkdir(parents=True, exist_ok=True)
    for old in cached.parent.glob(f"{style_name}_*{source_path.suffix}"):
        old.unlink()

    # recalcBBoxes=False for the same reason apply_metrics_override needs it:
    # re-walking CFF charstrings crashes on seac-style endchar.
    font = TTFont(str(source_path), recalcBBoxes=False, recalcTimestamp=False)
    try:
        dropped = 0
        for table in font["cmap"].tables:
            for cp in wanted:
                if cp in table.cmap:
                    del table.cmap[cp]
                    dropped += 1
        print(f"  Dropping {len(wanted)} cmap codepoints: {family_name}/{style_name} "
              f"({dropped} subtable entries) -> Noto fallback")
        tmp_fd, tmp_name = tempfile.mkstemp(suffix=source_path.suffix, dir=cached.parent)
        os.close(tmp_fd)
        tmp_path = Path(tmp_name)
        try:
            font.save(str(tmp_path))
        except Exception:
            tmp_path.unlink(missing_ok=True)
            raise
    finally:
        font.close()
    tmp_path.replace(cached)
    return cached


def apply_metrics_override(source_path: Path, metrics: dict, family_name: str, style_name: str) -> Path:
    """Rewrite a font's vertical metrics before conversion.

    `metrics` comes from the family's YAML block: {ascent, descent, linegap}
    in font units **per 1000 upem** (scaled for 2048/4096-upem fonts). All
    three metric homes are set consistently — hhea (what FreeType's
    face.size.height, i.e. the .cpfont advanceY, is derived from), OS/2 typo,
    and OS/2 win — the same way the hand-patched S-tier sources were built.

    Exists for faces whose stock metrics are sized for scripts the built
    charset never includes (Inknut Antiqua: hhea 1703/-876 for Devanagari
    stacks = 2.58 em line height on Latin-only text). Cached in
    PATCHED_DIR/<family>/, keyed on the values + source mtime.
    """
    from fontTools.ttLib import TTFont

    ascent = int(metrics["ascent"])
    descent = int(metrics["descent"])  # negative, hhea convention
    linegap = int(metrics.get("linegap", 0))
    if descent > 0:
        raise ValueError(
            f"{family_name}: metrics.descent must be negative (hhea convention), got {descent}")

    mtime = int(source_path.stat().st_mtime)
    cache_name = f"{style_name}_a{ascent}_d{descent}_g{linegap}_{mtime}{source_path.suffix}"
    cached = PATCHED_DIR / family_name / cache_name
    if cached.exists():
        return cached

    cached.parent.mkdir(parents=True, exist_ok=True)
    for old in cached.parent.glob(f"{style_name}_*{source_path.suffix}"):
        old.unlink()

    print(f"  Patching metrics: {family_name}/{style_name} -> {ascent}/{descent} gap {linegap}")
    # recalcBBoxes=False: on save, fontTools would otherwise re-walk every
    # charstring to recompute hhea bounds — clobbering nothing here (we set the
    # values explicitly) and crashing outright on CFF fonts that use the
    # deprecated seac-style two-argument endchar (Coelacanth: "not enough
    # values to unpack (expected 4, got 2)" in psCharStrings.op_endchar).
    font = TTFont(str(source_path), recalcBBoxes=False, recalcTimestamp=False)
    try:
        scale = font["head"].unitsPerEm / 1000.0
        asc = round(ascent * scale)
        desc = round(descent * scale)
        gap = round(linegap * scale)
        hhea = font["hhea"]
        hhea.ascent, hhea.descent, hhea.lineGap = asc, desc, gap
        os2 = font["OS/2"]
        os2.sTypoAscender, os2.sTypoDescender, os2.sTypoLineGap = asc, desc, gap
        os2.usWinAscent, os2.usWinDescent = asc, -desc
        # Atomic write, mirroring extract_static_instance: never leave a
        # truncated file where the cached.exists() check would reuse it.
        tmp_fd, tmp_name = tempfile.mkstemp(suffix=source_path.suffix, dir=cached.parent)
        os.close(tmp_fd)
        tmp_path = Path(tmp_name)
        try:
            font.save(str(tmp_path))
        except Exception:
            tmp_path.unlink(missing_ok=True)
            raise
    finally:
        font.close()
    tmp_path.replace(cached)
    return cached


def apply_upem_scale(source_path: Path, scale: float, family_name: str, style_name: str) -> Path:
    """Render a face larger or smaller relative to the em, without touching outlines.

    `scale` is a plain multiplier on rendered glyph size: 1.27 makes every
    glyph 27% bigger at a given point size. Implemented by shrinking
    `head.unitsPerEm` (new = old / scale) and changing NOTHING else -- outline
    coordinates, advance widths and GPOS kern values are all in font units, so
    they keep their proportions to each other and all grow together relative to
    the em. That is why this is a one-field edit rather than a transform:
    scaling the outlines instead would round every coordinate and every kern
    value, and would need hmtx and GPOS rewritten to match.

    Exists for MIXED-SOURCE families -- one where the roman and the italic come
    from different typefaces, so their x-heights were never drawn to agree.
    A metrics override cannot fix that: it rewrites hhea/OS2 line metrics only,
    which sets line spacing, not glyph size. Junicode's italic measures 0.416 em
    x-height against Inknut's 0.530, so an unscaled pairing renders the italic
    visibly smaller than the roman it sits beside.

    Caveat worth knowing: TrueType hinting instructions are compiled against the
    original upem, so they fire at a different ppem after this. Check the
    rendered x-height ramp for skipped pixel sizes rather than assuming, and
    reach for family-level `force_autohint` if the native hints misbehave.

    Cached in SCALED_DIR/<family>/, keyed on the scale + source mtime.
    """
    from fontTools.ttLib import TTFont

    if scale <= 0:
        raise ValueError(f"{family_name}/{style_name}: scale must be positive, got {scale}")

    mtime = int(source_path.stat().st_mtime)
    cache_name = f"{style_name}_s{scale:g}_{mtime}{source_path.suffix}"
    cached = SCALED_DIR / family_name / cache_name
    if cached.exists():
        return cached

    cached.parent.mkdir(parents=True, exist_ok=True)
    for old in cached.parent.glob(f"{style_name}_*{source_path.suffix}"):
        old.unlink()

    # recalcBBoxes=False for the same reason apply_metrics_override needs it:
    # re-walking charstrings crashes on CFF fonts using the deprecated
    # two-argument endchar, and nothing here invalidates the stored bounds
    # (they are in font units, which do not change).
    font = TTFont(str(source_path), recalcBBoxes=False, recalcTimestamp=False)
    try:
        old_upem = font["head"].unitsPerEm
        new_upem = round(old_upem / scale)
        if new_upem < 16:
            raise ValueError(
                f"{family_name}/{style_name}: scale {scale} would drop unitsPerEm to "
                f"{new_upem}, below any sane floor")
        print(f"  Scaling {family_name}/{style_name} x{scale:g}: upem {old_upem} -> {new_upem}")
        font["head"].unitsPerEm = new_upem
        tmp_fd, tmp_name = tempfile.mkstemp(suffix=source_path.suffix, dir=cached.parent)
        os.close(tmp_fd)
        tmp_path = Path(tmp_name)
        try:
            font.save(str(tmp_path))
        except Exception:
            tmp_path.unlink(missing_ok=True)
            raise
    finally:
        font.close()
    tmp_path.replace(cached)
    return cached


def extract_zip_member(url: str, member: str, family_name: str) -> Path:
    """Fetch a release archive and pull one font file out of it.

    Some foundries (e.g. Junicode) publish only a zip of the whole family, with
    no per-file raw URLs. The archive is cached in DOWNLOAD_DIR/_archives/ and
    shared across styles/families; the extracted member is cached next to the
    plain-url downloads so the rest of the pipeline sees an ordinary file.
    """
    zip_path = DOWNLOAD_DIR / "_archives" / url.rsplit("/", 1)[-1]
    dest = DOWNLOAD_DIR / family_name / Path(member).name
    if dest.exists():
        return dest
    download_font(url, zip_path)
    dest.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path) as zf:
        try:
            with zf.open(member) as src, open(dest, "wb") as out:
                shutil.copyfileobj(src, out)
        except KeyError:
            raise FileNotFoundError(
                f"{family_name}: '{member}' not found in {zip_path.name}"
            ) from None
    return dest


def resolve_font_path(style_spec: dict, family_name: str, style_name: str) -> Path:
    """Resolve a style spec (path, url, or zip+member) to a local font file path.

    If 'variable' key is present, extracts a static instance via fonttools
    instancer after resolving the source file.
    """
    if "path" in style_spec:
        resolved = EPDFONTS_DIR / style_spec["path"]
        if not resolved.exists():
            raise FileNotFoundError(f"{family_name}/{style_name}: {resolved} not found")
    elif "zip" in style_spec:
        if "member" not in style_spec:
            raise ValueError(f"{family_name}/{style_name}: 'zip' requires 'member'")
        resolved = extract_zip_member(style_spec["zip"], style_spec["member"], family_name)
    elif "url" in style_spec:
        url = style_spec["url"]
        # Derive a stable filename from the URL
        filename = url.rsplit("/", 1)[-1]
        dest = DOWNLOAD_DIR / family_name / filename
        resolved = download_font(url, dest)
    else:
        raise ValueError(f"{family_name}/{style_name}: must have 'path', 'url', or 'zip'")

    # If variable font axes are specified, extract a static instance
    if "variable" in style_spec:
        resolved = extract_static_instance(
            resolved, style_spec["variable"], family_name, style_name
        )

    return resolved


def _stream_pipe(pipe, prefix: str, dest: list[str]):
    """Read lines from a pipe, print with prefix, and accumulate into dest."""
    for line in pipe:
        dest.append(line)
        print(f"  [{prefix}] {line}", end="", flush=True)


def build_family(
    family: dict, output_base: Path, verbose: bool = False, timeout: int = 600
) -> tuple[str, bool, str]:
    """Build a single font family. Returns (name, success, message)."""
    name = family["name"]
    output_dir = output_base / name
    output_dir.mkdir(parents=True, exist_ok=True)

    styles = family.get("styles", {})
    intervals = family["intervals"]
    sizes = ",".join(str(s) for s in family["sizes"])
    fallback_font = fallback_font_for(intervals)

    # Resolve all font file paths (downloads as needed).
    # Two passes: styles with real sources first, then `from:` styles, which
    # reuse another style's resolved file (optionally with a `synthetic:`
    # embolden/shear spec applied at conversion time — see
    # docs/synthetic-font-styles.md).
    try:
        resolved_styles = {}
        synth_flags = {}
        space_flags = {}
        # Advance-only spacing, collected over EVERY style. Flat keys rather
        # than a nested map, and deliberately NOT folded into the `from:` pass
        # below the way `synthetic:` is: a synthetic style borrows another
        # style's file, whereas tracking applies to a style that has its own.
        # Reading it in that pass silently skips every real-source style, which
        # is every style that normally wants it.
        for style_name, style_spec in styles.items():
            space = {k: style_spec[k] for k in ("tracking_em", "word_space_em")
                     if k in style_spec}
            if space:
                space_flags[style_name] = ",".join(f"{k}={v}" for k, v in sorted(space.items()))
            # `synthetic:` on a style that has its OWN source, rather than only
            # on a `from:` alias. A synthetic style borrows another style's file
            # because the family ships no such cut; a real cut can still want an
            # embolden on top of it, when the axis it rides runs out before the
            # target does. Collected here so both cases work; the `from:` pass
            # below still handles aliases and would otherwise be the only path.
            if "from" not in style_spec and "synthetic" in style_spec:
                synth_flags[style_name] = ",".join(
                    f"{k}={v}" for k, v in sorted(style_spec["synthetic"].items()))
        for style_name, style_spec in styles.items():
            if "from" in style_spec:
                continue
            resolved_styles[style_name] = resolve_font_path(style_spec, name, style_name)
        # Per-style glyph scale, FIRST in the chain. Before the metrics
        # override because that one computes its font-unit values as
        # `ascent * upem/1000` -- running it second keeps the line metrics
        # em-relative and so identical across scaled and unscaled styles,
        # which is the whole point (one advanceY for the whole family).
        # Before `from:` aliasing too, so a synthetic inherits the scale.
        for style_name, style_spec in styles.items():
            if "from" in style_spec or "scale" not in style_spec:
                continue
            resolved_styles[style_name] = apply_upem_scale(
                resolved_styles[style_name], float(style_spec["scale"]), name, style_name)
        # Family-level cmap drops, before the metrics patch so the two chain
        # (drops -> metrics -> conversion) and before `from:` aliasing so
        # synthetics inherit both.
        drops = family.get("drop_codepoints")
        if drops:
            for style_name in list(resolved_styles):
                resolved_styles[style_name] = apply_cmap_drops(
                    resolved_styles[style_name], drops, name, style_name)
        # Family-level metrics override, applied to the real sources before
        # `from:` styles alias them, so synthetics inherit the patched file.
        metrics = family.get("metrics")
        if metrics:
            for style_name in list(resolved_styles):
                resolved_styles[style_name] = apply_metrics_override(
                    resolved_styles[style_name], metrics, name, style_name)
        for style_name, style_spec in styles.items():
            if "from" not in style_spec:
                continue
            base = style_spec["from"]
            if base not in resolved_styles:
                return name, False, (
                    f"{name}/{style_name}: 'from: {base}' must name another "
                    f"style in this family with a real source (path/url/zip)")
            resolved_styles[style_name] = resolved_styles[base]
            synth = style_spec.get("synthetic")
            if synth:
                synth_flags[style_name] = ",".join(
                    f"{k}={v}" for k, v in sorted(synth.items()))

    except (FileNotFoundError, RuntimeError) as e:
        return name, False, str(e)

    # Build the fontconvert_sdcard.py command
    cmd = [sys.executable, str(FONTCONVERT)]

    multi_style = len(resolved_styles) > 1 or "regular" not in resolved_styles
    has_any_multi = any(k in resolved_styles for k in ("regular", "bold", "italic", "bolditalic"))

    if has_any_multi and len(resolved_styles) > 1:
        # Multi-style mode
        for style_name, font_path in resolved_styles.items():
            cmd.extend([f"--{style_name}", str(font_path)])
            cmd.extend([f"--fallback-{style_name}", str(fallback_font)])
    else:
        # Single-style mode
        style_name = next(iter(resolved_styles))
        font_path = resolved_styles[style_name]
        cmd.append(str(font_path))
        cmd.extend(["--style", style_name])
        cmd.extend([f"--fallback-{style_name}", str(fallback_font)])

    for style_name, spec in synth_flags.items():
        cmd.extend([f"--synth-{style_name}", spec])
    for style_name, spec in space_flags.items():
        cmd.extend([f"--space-{style_name}", spec])

    cmd.extend(["--intervals", intervals])
    cmd.extend(["--sizes", sizes])
    cmd.extend(["--name", name])
    cmd.extend(["--output-dir", str(output_dir) + "/"])

    if family.get("force_autohint", False):
        cmd.append("--force-autohint")

    # Run fontconvert_sdcard.py
    start = time.monotonic()
    try:
        if verbose:
            proc = subprocess.Popen(
                cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
            )
            stdout_lines: list[str] = []
            stderr_lines: list[str] = []
            t_out = threading.Thread(
                target=_stream_pipe, args=(proc.stdout, name, stdout_lines)
            )
            t_err = threading.Thread(
                target=_stream_pipe, args=(proc.stderr, f"{name}/err", stderr_lines)
            )
            t_out.start()
            t_err.start()
            try:
                proc.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
                elapsed = time.monotonic() - start
                return name, False, f"Timed out after {elapsed:.0f}s"
            finally:
                t_out.join()
                t_err.join()

            if proc.returncode != 0:
                err = "".join(stderr_lines).strip()
                return name, False, err or f"Exit code {proc.returncode}"
            return name, True, ""
        else:
            result = subprocess.run(
                cmd, capture_output=True, text=True, timeout=timeout,
            )
            if result.returncode != 0:
                return name, False, result.stderr.strip() or f"Exit code {result.returncode}"
            return name, True, ""
    except subprocess.TimeoutExpired as e:
        elapsed = time.monotonic() - start
        tail = ""
        captured = getattr(e, "stderr", None) or getattr(e, "stdout", None)
        if captured:
            lines = captured.strip().splitlines()
            tail = "\n    Last output:\n" + "\n".join(f"    | {l}" for l in lines[-20:])
        return name, False, f"Timed out after {elapsed:.0f}s{tail}"
    except Exception as e:
        return name, False, str(e)


def main():
    parser = argparse.ArgumentParser(description="Build SD card fonts from YAML config")
    parser.add_argument(
        "--config", default=str(DEFAULT_CONFIG), help="Path to font families YAML config"
    )
    parser.add_argument(
        "--output-dir", default=str(DEFAULT_OUTPUT), help="Output directory for .cpfont files"
    )
    parser.add_argument("--only", help="Comma-separated family names to build (default: all)")
    parser.add_argument(
        "--jobs", "-j", type=int, default=None,
        help="Max parallel jobs (default: number of families)"
    )
    parser.add_argument("--clean", action="store_true", help="Clean output directory before building")
    parser.add_argument(
        "--verbose", "-v", action="store_true",
        help="Stream child process output in real time (useful for debugging timeouts)"
    )
    parser.add_argument(
        "--timeout", type=int, default=600,
        help="Per-family timeout in seconds (default: 600)"
    )
    args = parser.parse_args()

    # Load config
    config_path = Path(args.config)
    if not config_path.exists():
        print(f"ERROR: Config not found: {config_path}", file=sys.stderr)
        sys.exit(1)

    with open(config_path) as f:
        config = yaml.safe_load(f)

    families = config.get("families", [])
    if not families:
        print("ERROR: No families defined in config", file=sys.stderr)
        sys.exit(1)

    # Filter if --only specified
    if args.only:
        only_names = set(args.only.split(","))
        families = [f for f in families if f["name"] in only_names]
        missing = only_names - {f["name"] for f in families}
        if missing:
            print(f"WARNING: families not found in config: {', '.join(missing)}", file=sys.stderr)
        if not families:
            print("ERROR: no matching families after --only filter", file=sys.stderr)
            sys.exit(1)

    output_base = Path(args.output_dir)

    if args.clean and output_base.exists():
        print(f"Cleaning {output_base}...")
        shutil.rmtree(output_base)

    output_base.mkdir(parents=True, exist_ok=True)

    # Resolve the fallback faces this run needs, before the parallel build
    # phase: workers would otherwise race on the same download cache path. Fail
    # loudly rather than build without one — a missing fallback costs hundreds
    # of glyphs per style and raises no other error anywhere.
    chosen_fallbacks: dict[Path, list[str]] = {}
    for f in families:
        chosen_fallbacks.setdefault(fallback_font_for(f["intervals"]), []).append(f["name"])
    needed_fallbacks = set(chosen_fallbacks)
    for path, names in chosen_fallbacks.items():
        print(f"Fallback glyphs from {path.name}: {', '.join(sorted(names))}")
    if LATIN_FALLBACK_FONT in needed_fallbacks and not LATIN_FALLBACK_FONT.is_file():
        print(
            f"ERROR: Missing Latin fallback font: {LATIN_FALLBACK_FONT}\n"
            "It is committed to this repo; a checkout is incomplete.",
            file=sys.stderr,
        )
        sys.exit(1)
    if BROAD_FALLBACK_FONT in needed_fallbacks:
        try:
            download_font(BROAD_FALLBACK_URL, BROAD_FALLBACK_FONT)
        except Exception as e:  # noqa: BLE001 - reported and fatal
            print(
                f"ERROR: could not obtain the broad-coverage fallback font: {e}\n"
                f"Expected at {BROAD_FALLBACK_FONT}, from {BROAD_FALLBACK_URL}.\n"
                "Families asking for Greek, Cyrillic, math or symbols fill those "
                "from it; building without it silently ships reduced coverage.",
                file=sys.stderr,
            )
            sys.exit(1)

    # Download phase (sequential — avoids hammering servers)
    print(f"\n=== Resolving {len(families)} font families ===\n")
    for family in families:
        for style_name, style_spec in family.get("styles", {}).items():
            if "url" in style_spec or "zip" in style_spec:
                try:
                    resolve_font_path(style_spec, family["name"], style_name)
                except Exception as e:
                    print(f"ERROR: {e}", file=sys.stderr)
                    sys.exit(1)

    # Build phase (parallel)
    max_workers = args.jobs or len(families)
    verbose = args.verbose
    timeout = args.timeout
    print(f"\n=== Building {len(families)} families ({max_workers} parallel jobs, timeout {timeout}s) ===\n")

    failed = []
    with ProcessPoolExecutor(max_workers=max_workers) as executor:
        futures = {
            executor.submit(build_family, family, output_base, verbose, timeout): family["name"]
            for family in families
        }
        for future in as_completed(futures):
            name, success, message = future.result()
            if success:
                # Count output files
                family_dir = output_base / name
                count = len(list(family_dir.glob("*.cpfont")))
                size = sum(f.stat().st_size for f in family_dir.glob("*.cpfont"))
                print(f"  OK: {name} ({count} files, {size / 1024 / 1024:.1f} MB)")
            else:
                print(f"  FAILED: {name}: {message}", file=sys.stderr)
                failed.append(name)

    # Summary
    print("\n=== Summary ===\n")
    total_files = len(list(output_base.rglob("*.cpfont")))
    total_size = sum(f.stat().st_size for f in output_base.rglob("*.cpfont"))
    print(f"Total: {total_files} .cpfont files ({total_size / 1024 / 1024:.1f} MB)")

    if failed:
        print(f"\nFailed families: {', '.join(failed)}", file=sys.stderr)

    if failed:
        sys.exit(1)


if __name__ == "__main__":
    main()
