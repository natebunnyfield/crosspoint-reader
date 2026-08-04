#!/usr/bin/env python3
"""Build SD card fonts from a declarative YAML config.

Reads sd-fonts.yaml, downloads any missing source fonts, runs
fontconvert_sdcard.py in parallel for each family, and optionally
generates the fonts.json manifest.

Usage:
    # Generate fonts (output in ./output/)
    python3 build-sd-fonts.py

    # Generate fonts + manifest
    python3 build-sd-fonts.py --manifest --base-url "http://localhost:8000/"

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
DEFAULT_FALLBACK_FONT = EPDFONTS_DIR / "builtinFonts/source/NotoSans/NotoSans-Regular.ttf"


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
        font = instantiateVariableFont(source_font, axes, updateFontNames=True, optimize=False)
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

    # Resolve all font file paths (downloads as needed).
    # Two passes: styles with real sources first, then `from:` styles, which
    # reuse another style's resolved file (optionally with a `synthetic:`
    # embolden/shear spec applied at conversion time — see
    # docs/synthetic-font-styles.md).
    try:
        resolved_styles = {}
        synth_flags = {}
        for style_name, style_spec in styles.items():
            if "from" in style_spec:
                continue
            resolved_styles[style_name] = resolve_font_path(style_spec, name, style_name)
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
            cmd.extend([f"--fallback-{style_name}", str(DEFAULT_FALLBACK_FONT)])
    else:
        # Single-style mode
        style_name = next(iter(resolved_styles))
        font_path = resolved_styles[style_name]
        cmd.append(str(font_path))
        cmd.extend(["--style", style_name])
        cmd.extend([f"--fallback-{style_name}", str(DEFAULT_FALLBACK_FONT)])

    for style_name, spec in synth_flags.items():
        cmd.extend([f"--synth-{style_name}", spec])

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


def generate_manifest(
    config_path: Path, output_base: Path, base_url: str, manifest_path: Path
):
    """Generate fonts.json manifest from config + built output.

    Uses the standalone generate-font-manifest.py as a subprocess so
    descriptions come from the YAML config via --descriptions-from.
    """
    manifest_script = SCRIPT_DIR.parent.parent.parent / "scripts" / "generate-font-manifest.py"

    if not base_url.endswith("/"):
        base_url += "/"

    cmd = [
        sys.executable, str(manifest_script),
        "--input", str(output_base),
        "--base-url", base_url,
        "--output", str(manifest_path),
    ]

    if config_path.exists():
        cmd.extend(["--descriptions-from", str(config_path)])

    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"ERROR: Manifest generation failed:\n{result.stderr}", file=sys.stderr)
        return
    print(result.stdout, end="")
    print(f"Manifest written: {manifest_path}")


def main():
    parser = argparse.ArgumentParser(description="Build SD card fonts from YAML config")
    parser.add_argument(
        "--config", default=str(DEFAULT_CONFIG), help="Path to font families YAML config"
    )
    parser.add_argument(
        "--output-dir", default=str(DEFAULT_OUTPUT), help="Output directory for .cpfont files"
    )
    parser.add_argument("--only", help="Comma-separated family names to build (default: all)")
    parser.add_argument("--manifest", action="store_true", help="Also generate fonts.json manifest")
    parser.add_argument("--base-url", default="", help="Base URL for manifest (required with --manifest)")
    parser.add_argument(
        "--manifest-output", default=None, help="Manifest output path (default: <output-dir>/fonts.json)"
    )
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

    if args.manifest and not args.base_url:
        parser.error("--base-url is required when using --manifest")

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

    if not DEFAULT_FALLBACK_FONT.exists() or not DEFAULT_FALLBACK_FONT.is_file():
        print(
            "ERROR: Missing default fallback font: "
            f"{DEFAULT_FALLBACK_FONT}\n"
            "This font is required for fallback glyphs in SD font builds.",
            file=sys.stderr,
        )
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

    # Manifest
    if args.manifest:
        manifest_path = Path(args.manifest_output) if args.manifest_output else output_base / "fonts.json"
        generate_manifest(config_path, output_base, args.base_url, manifest_path)

    if failed:
        sys.exit(1)


if __name__ == "__main__":
    main()
