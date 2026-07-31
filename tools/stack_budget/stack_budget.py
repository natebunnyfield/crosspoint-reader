#!/usr/bin/env python3
"""Stack frame budget gate for CrossPoint Reader (-fstack-usage).

Builds the firmware with -fstack-usage into a PRIVATE build directory, parses
every generated .su file, and fails when any first-party frame is larger than
the budget.

Why this exists: the Resource Protocol in CLAUDE.md caps function locals at
256 bytes, but nothing enforced it. Oversized frames were found the expensive
way -- a device panic with an empty reason, then a manual -fstack-usage run.
lib/EpdFont/SdCardFont.cpp:265-284 documents exactly that history for
buildMiniKernMatrix (1648 bytes). This turns that into a build check.

Usage:
    tools/stack_budget/stack_budget.py                  # build + check
    tools/stack_budget/stack_budget.py --threshold 512  # tighter ratchet
    tools/stack_budget/stack_budget.py --no-build       # reuse existing .su
    tools/stack_budget/stack_budget.py --suggest        # emit allowlist lines

Exit codes: 0 = pass, 1 = frame over budget, 2 = usage/environment error.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

# Default budget. Deliberately NOT the 256-byte Resource Protocol limit yet --
# see README.md ("Ratchet path") for the measured distribution and the plan to
# walk this down. A gate that red-lights on day one gets disabled on day two.
#
# 512 was chosen by measurement, not taste: at seed time the worst first-party
# frame was 1008 bytes and exactly 20 frames sat above 512, few enough to name
# and justify individually in allowlist.txt. It is also 1/16th of the 8192-byte
# render task stack, so a single frame at the limit still leaves room for the
# ~10 frames deep that the font path actually nests.
DEFAULT_THRESHOLD = 512

# Only frames whose location starts with one of these are first-party code.
# Everything else (.pio/libdeps/**, freeink-sdk/**, toolchain headers,
# ~/.platformio/packages/framework-arduinoespressif32/**) is out of our control.
INCLUDE_PREFIXES = ("src/", "lib/")

# Vendored sources that live *inside* the included prefixes, plus defensive
# entries for third-party code in case INCLUDE_PREFIXES is ever widened.
# miniz's inflate/deflate frames measure 8400-8480 bytes on this target; without
# this list the gate would red-light forever on code we do not maintain.
EXEMPT_SUBSTRINGS = (
    "lib/miniz/",
    "lib/expat/",
    "lib/uzlib/",
    # ONLY minibidi.c/.h are vendored here (mintty, MIT — see lib/MiniBidi/minibidi.h).
    # BidiUtils.cpp/.h in the same directory are project-authored, so exempting the
    # whole directory hid a first-party 608-byte frame
    # (BidiUtils::computeVisualWordOrder) that sits on the render path.
    "lib/MiniBidi/minibidi.",
    # Not under src/ or lib/ today, listed so the filter stays correct if that changes.
    "freeink-sdk/",
    "PNGdec",
    "JPEGDEC",
    "Arduino-wolfSSL",
    ".pio/",
    "/packages/framework-",
    "/packages/toolchain-",
)

# location:line:col:demangled signature <TAB> size <TAB> qualifiers
# The location is matched non-greedily so the first ":<digits>:<digits>:" wins;
# a demangled C++ signature can itself contain ':' (Foo::bar) and ',' and ' '.
SU_LINE_RE = re.compile(r"^(?P<loc>.+?):(?P<line>\d+):(?P<col>\d+):(?P<sig>.*)$")

PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ALLOWLIST = PROJECT_ROOT / "tools" / "stack_budget" / "allowlist.txt"
DEFAULT_BUILD_DIR = Path("/tmp/pio_stack_budget/build")
DEFAULT_CACHE_DIR = Path("/tmp/pio_stack_budget/cache")


class Frame:
    """One function's stack frame as reported by -fstack-usage."""

    __slots__ = ("location", "line", "signature", "size", "qualifiers")

    def __init__(self, location: str, line: int, signature: str, size: int, qualifiers: str):
        self.location = location
        self.line = line
        self.signature = signature
        self.size = size
        self.qualifiers = qualifiers

    @property
    def key(self) -> tuple[str, str]:
        # Keyed on file + signature, NOT line number: line numbers move on every
        # edit above the function, which would rot the allowlist immediately.
        return (self.location, self.signature)

    @property
    def is_dynamic(self) -> bool:
        # GCC prints "dynamic" for alloca/VLA frames; the number it reports then
        # covers only the static part, so the real frame is larger than shown.
        return "dynamic" in self.qualifiers


class AllowEntry:
    __slots__ = ("budget", "location", "signature", "reason", "source_line", "matched_size")

    def __init__(self, budget: int, location: str, signature: str, reason: str, source_line: int):
        self.budget = budget
        self.location = location
        self.signature = signature
        self.reason = reason
        self.source_line = source_line
        self.matched_size: int | None = None  # None => matched no frame at all


def normalize_location(loc: str) -> str:
    """Make a .su location repo-relative when it points inside the project."""
    if loc.startswith("/"):
        try:
            return str(Path(loc).relative_to(PROJECT_ROOT))
        except ValueError:
            return loc
    # PlatformIO emits "src/foo.cpp"; guard against "./src/foo.cpp" too.
    if loc.startswith("./"):
        return loc[2:]
    return loc


def is_first_party(loc: str) -> bool:
    if not loc.startswith(INCLUDE_PREFIXES):
        return False
    return not any(pat in loc for pat in EXEMPT_SUBSTRINGS)


def parse_su_file(path: Path, warnings: list[str]) -> list[Frame]:
    frames: list[Frame] = []
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        warnings.append(f"could not read {path}: {exc}")
        return frames

    for lineno, raw in enumerate(text.splitlines(), start=1):
        if not raw.strip():
            continue
        # Tab-separated by construction, so split on tab -- never on whitespace.
        # GOTCHA: riscv32-esp-elf-gcc writes DEMANGLED signatures, which contain
        # spaces ("virtual int Print::availableForWrite()"). Splitting on spaces
        # (or `awk '$2 > N'`) reads a fragment of the signature as the size.
        parts = raw.split("\t")
        if len(parts) < 2:
            warnings.append(f"{path}:{lineno}: malformed line (no tab)")
            continue
        head, size_str = parts[0], parts[1]
        qualifiers = parts[2] if len(parts) > 2 else ""
        match = SU_LINE_RE.match(head)
        if not match:
            warnings.append(f"{path}:{lineno}: unparseable location {head!r}")
            continue
        try:
            size = int(size_str)
        except ValueError:
            warnings.append(f"{path}:{lineno}: non-integer size {size_str!r}")
            continue
        frames.append(
            Frame(
                location=normalize_location(match.group("loc")),
                line=int(match.group("line")),
                signature=match.group("sig"),
                size=size,
                qualifiers=qualifiers,
            )
        )
    return frames


def collect_frames(build_dir: Path, warnings: list[str]) -> tuple[list[Frame], int, int]:
    """Return (first-party frames deduped worst-wins, total frames, su file count)."""
    su_files = sorted(build_dir.rglob("*.su"))
    total = 0
    best: dict[tuple[str, str], Frame] = {}
    for su in su_files:
        for frame in parse_su_file(su, warnings):
            total += 1
            if not is_first_party(frame.location):
                continue
            # A header-defined or template function appears in every TU that
            # instantiates it, sometimes with different sizes after inlining.
            # Keep the worst -- that is the one that can blow the stack.
            existing = best.get(frame.key)
            if existing is None or frame.size > existing.size:
                best[frame.key] = frame
    return list(best.values()), total, len(su_files)


def load_allowlist(path: Path) -> tuple[list[AllowEntry], list[str]]:
    """Parse the allowlist.

    Format, one frame per line:

        <budget> <file> <demangled signature>  # reason

    The reason is mandatory. Field split: first whitespace token is the budget,
    the second is the repo-relative file (no spaces -- true for every path in
    this repo), and the rest up to the ' #' is the signature exactly as
    -fstack-usage prints it.
    """
    entries: list[AllowEntry] = []
    problems: list[str] = []
    if not path.exists():
        return entries, problems

    for lineno, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        stripped = raw.strip()
        if not stripped or stripped.startswith("#"):
            continue
        body, sep, reason = stripped.partition("#")
        body = body.strip()
        reason = reason.strip()
        if not sep or not reason:
            problems.append(f"{path}:{lineno}: entry has no '# reason' -- refusing to allow it")
            continue
        fields = body.split(None, 2)
        if len(fields) < 3:
            problems.append(f"{path}:{lineno}: expected '<budget> <file> <signature>', got {body!r}")
            continue
        try:
            budget = int(fields[0])
        except ValueError:
            problems.append(f"{path}:{lineno}: budget {fields[0]!r} is not an integer")
            continue
        entries.append(AllowEntry(budget, fields[1], fields[2].strip(), reason, lineno))

    seen: dict[tuple[str, str], int] = {}
    for entry in entries:
        key = (entry.location, entry.signature)
        if key in seen:
            problems.append(
                f"{path}:{entry.source_line}: duplicate entry, already listed on "
                f"line {seen[key]}"
            )
        else:
            seen[key] = entry.source_line
    return entries, problems


def find_pio(explicit: str | None) -> str:
    if explicit:
        return explicit
    candidate = Path.home() / ".platformio" / "penv" / "bin" / "pio"
    if candidate.exists():
        return str(candidate)
    found = shutil.which("pio")
    if found:
        return found
    return "pio"


def run_build(args: argparse.Namespace) -> int:
    build_dir = args.build_dir.resolve()
    cache_dir = args.cache_dir.resolve()

    # Build isolation: .pio/ and .cache/ belong to whoever else is working in
    # this tree. Never write into them.
    for name, path in (("--build-dir", build_dir), ("--cache-dir", cache_dir)):
        if path == PROJECT_ROOT or PROJECT_ROOT in path.parents:
            print(
                f"error: {name}={path} is inside the project tree. Use a path "
                f"outside {PROJECT_ROOT} so this never clobbers .pio/ or .cache/.",
                file=sys.stderr,
            )
            return 2

    if args.fresh:
        for path in (build_dir, cache_dir):
            shutil.rmtree(path, ignore_errors=True)

    build_dir.mkdir(parents=True, exist_ok=True)
    cache_dir.mkdir(parents=True, exist_ok=True)

    env = os.environ.copy()
    env["PLATFORMIO_BUILD_DIR"] = str(build_dir)
    # ESSENTIAL, not cosmetic: platformio.ini sets `build_cache_dir = .cache`,
    # and a cache hit restores the .o WITHOUT running the compiler -- so no .su
    # is written and the gate silently checks 0 frames. A private cache dir
    # keeps the shared cache intact and still guarantees .su output.
    env["PLATFORMIO_BUILD_CACHE_DIR"] = str(cache_dir)
    extra = env.get("PLATFORMIO_BUILD_FLAGS", "").strip()
    env["PLATFORMIO_BUILD_FLAGS"] = (extra + " -fstack-usage").strip()

    cmd = [find_pio(args.pio), "run", "-e", args.env]
    print(f"[stack_budget] {' '.join(cmd)}")
    print(f"[stack_budget]   PLATFORMIO_BUILD_DIR={build_dir}")
    print(f"[stack_budget]   PLATFORMIO_BUILD_CACHE_DIR={cache_dir}")
    print(f"[stack_budget]   PLATFORMIO_BUILD_FLAGS={env['PLATFORMIO_BUILD_FLAGS']}")
    sys.stdout.flush()

    # Swallow build chatter unless asked for it, but KEEP it so a failed build can
    # print it. A plain DEVNULL on stdout is not enough: the third-party deps emit
    # ~150 KB of "macro redefined" warnings on stderr, which would bury the one
    # line of this tool's output that CI actually needs to show.
    try:
        if args.verbose_build:
            proc = subprocess.run(cmd, cwd=str(PROJECT_ROOT), env=env)
            output = ""
        else:
            proc = subprocess.run(
                cmd, cwd=str(PROJECT_ROOT), env=env,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
            )
            output = proc.stdout or ""
    except OSError as exc:
        print(f"error: could not run {cmd[0]}: {exc}", file=sys.stderr)
        return 2
    if proc.returncode != 0:
        if output:
            # Only the tail: the useful error is at the end, the header is noise.
            print("--- build output (last 40 lines) ---", file=sys.stderr)
            for line in output.splitlines()[-40:]:
                print(line, file=sys.stderr)
        print(f"error: build failed (exit {proc.returncode})", file=sys.stderr)
        return 2
    return 0


def format_table(frames: list[Frame], budget_of) -> list[str]:
    rows = []
    for frame in frames:
        loc = f"{frame.location}:{frame.line}"
        budget = budget_of(frame)
        note = ""
        if budget is not None:
            note = f" [allowlisted {budget} B]"
        if frame.is_dynamic:
            note += " [dynamic: real frame is larger]"
        rows.append(f"  {frame.size:>6} B  {loc}{note}\n           {frame.signature}")
    return rows


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Fail the build when a first-party stack frame exceeds the budget.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--threshold", type=int, default=DEFAULT_THRESHOLD,
                        help=f"max bytes per frame (default {DEFAULT_THRESHOLD})")
    parser.add_argument("--allowlist", type=Path, default=DEFAULT_ALLOWLIST,
                        help="allowlist file (default tools/stack_budget/allowlist.txt)")
    parser.add_argument("--no-allowlist", action="store_true",
                        help="ignore the allowlist entirely (shows the true debt)")
    parser.add_argument("--env", default="default", help="PlatformIO environment (default: default)")
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR,
                        help=f"private PLATFORMIO_BUILD_DIR (default {DEFAULT_BUILD_DIR})")
    parser.add_argument("--cache-dir", type=Path, default=DEFAULT_CACHE_DIR,
                        help=f"private PLATFORMIO_BUILD_CACHE_DIR (default {DEFAULT_CACHE_DIR})")
    parser.add_argument("--no-build", action="store_true",
                        help="skip the build, parse the .su files already in --build-dir")
    parser.add_argument("--fresh", action="store_true",
                        help="wipe the private build and cache dirs first (full rebuild)")
    parser.add_argument("--pio", help="path to the pio executable")
    parser.add_argument("--verbose-build", action="store_true", help="show compiler output")
    parser.add_argument("--top", type=int, default=10,
                        help="how many of the largest frames to always list (0 to disable)")
    parser.add_argument("--suggest", action="store_true",
                        help="print allowlist lines for every current violation")
    parser.add_argument("--fail-on-stale", action="store_true",
                        help="also fail on stale/obsolete allowlist entries (keeps the "
                             "allowlist honest; recommended once the list is small)")
    parser.add_argument("--json", type=Path, help="write the full frame list to this JSON file")
    args = parser.parse_args(argv)

    if args.threshold < 1:
        print("error: --threshold must be >= 1", file=sys.stderr)
        return 2

    if not args.no_build:
        rc = run_build(args)
        if rc != 0:
            return rc

    build_dir = args.build_dir.resolve()
    if not build_dir.exists():
        print(f"error: build dir {build_dir} does not exist (drop --no-build?)", file=sys.stderr)
        return 2

    warnings: list[str] = []
    frames, total_frames, su_count = collect_frames(build_dir, warnings)

    if su_count == 0:
        print(
            f"error: no .su files under {build_dir}. The compiler did not run -- "
            f"almost always a build-cache hit. Re-run with --fresh, and check that "
            f"--cache-dir is private (platformio.ini's build_cache_dir = .cache "
            f"restores .o files without recompiling, so no .su is produced).",
            file=sys.stderr,
        )
        return 2

    entries: list[AllowEntry] = []
    allow_problems: list[str] = []
    if not args.no_allowlist:
        entries, allow_problems = load_allowlist(args.allowlist)
    by_key: dict[tuple[str, str], AllowEntry] = {(e.location, e.signature): e for e in entries}

    def budget_of(frame: Frame) -> int | None:
        entry = by_key.get(frame.key)
        return entry.budget if entry else None

    frames.sort(key=lambda f: (-f.size, f.location, f.signature))

    violations: list[Frame] = []
    allowed_over: list[Frame] = []
    regressions: list[Frame] = []
    for frame in frames:
        entry = by_key.get(frame.key)
        if entry is not None:
            entry.matched_size = frame.size
        if frame.size <= args.threshold:
            continue
        if entry is None:
            violations.append(frame)
        elif frame.size > entry.budget:
            # Allowlisted, but it grew past the size it was allowed at.
            regressions.append(frame)
        else:
            allowed_over.append(frame)

    # Ratchet bookkeeping. Three distinct ways an entry stops carrying its weight:
    #   stale    - matched no frame (fixed, renamed, or the signature changed)
    #   obsolete - the frame is now under the threshold, so no exemption is needed
    #   slack    - still over threshold but smaller than its recorded budget, so
    #              the budget can be tightened to lock the improvement in
    stale = [e for e in entries if e.matched_size is None]
    obsolete = [e for e in entries if e.matched_size is not None and e.matched_size <= args.threshold]
    slack = [
        e
        for e in entries
        if e.matched_size is not None and args.threshold < e.matched_size < e.budget
    ]
    dynamic = [f for f in frames if f.is_dynamic]

    print()
    print(f"Stack frame budget: threshold {args.threshold} B, {su_count} .su files, "
          f"{total_frames} frames total, {len(frames)} first-party")
    if not args.no_allowlist:
        print(f"Allowlist: {args.allowlist} ({len(entries)} entries, "
              f"{len(allowed_over)} in use)")

    if args.top > 0:
        print()
        print(f"Largest first-party frames (top {args.top}):")
        for row in format_table(frames[: args.top], budget_of):
            print(row)

    if dynamic:
        print()
        print(f"note: {len(dynamic)} frame(s) marked 'dynamic' (alloca/VLA). The size "
              f"shown is the static part only; actual use is unbounded:")
        for frame in dynamic[:5]:
            print(f"  {frame.size:>6} B  {frame.location}:{frame.line}  {frame.signature}")

    if allow_problems:
        print()
        print("Allowlist problems (these entries do NOT grant an exemption):")
        for problem in allow_problems:
            print(f"  {problem}")

    if stale:
        print()
        print("Stale allowlist entries (matched no frame -- fixed, renamed, or "
              "signature changed; delete or update them):")
        for entry in stale:
            print(f"  {args.allowlist}:{entry.source_line}: {entry.location}  {entry.signature}")

    if obsolete:
        print()
        print(f"Obsolete allowlist entries (frame is now <= {args.threshold} B; "
              f"delete the entry to lock the win in):")
        for entry in obsolete:
            print(f"  {args.allowlist}:{entry.source_line}: now {entry.matched_size} B  "
                  f"{entry.location}  {entry.signature}")

    if slack:
        print()
        print("Allowlist entries with slack (frame shrank; tighten the budget so it "
              "cannot grow back):")
        for entry in slack:
            print(f"  {args.allowlist}:{entry.source_line}: {entry.budget} -> "
                  f"{entry.matched_size} B  {entry.location}  {entry.signature}")

    if regressions:
        print()
        print("REGRESSIONS -- allowlisted frames that grew past their recorded budget:")
        for row in format_table(regressions, budget_of):
            print(row)

    if violations:
        print()
        print(f"OVER BUDGET -- {len(violations)} frame(s) above {args.threshold} B "
              f"and not allowlisted, worst first:")
        for row in format_table(violations, budget_of):
            print(row)
        print()
        print("Fix by moving the buffer off the stack (makeUniqueNoThrow<uint8_t[]>(n) "
              "from lib/Memory/Memory.h), per the Resource Protocol in CLAUDE.md.")
        print("If it genuinely cannot move, add it to the allowlist WITH a reason "
              "(run with --suggest for ready-made lines).")

    if args.suggest and (violations or regressions):
        print()
        print(f"# Suggested {args.allowlist.name} lines -- replace TODO with the real reason:")
        for frame in sorted(violations + regressions, key=lambda f: -f.size):
            print(f"{frame.size} {frame.location} {frame.signature}  "
                  f"# TODO: why this frame cannot move to the heap")

    if warnings:
        print()
        print(f"note: {len(warnings)} parse warning(s); first 5:")
        for warning in warnings[:5]:
            print(f"  {warning}")

    if args.json:
        payload = {
            "threshold": args.threshold,
            "su_files": su_count,
            "frames_total": total_frames,
            "frames_first_party": len(frames),
            "violations": len(violations),
            "regressions": len(regressions),
            "allowlisted_in_use": len(allowed_over),
            "stale_allowlist_entries": len(stale),
            "obsolete_allowlist_entries": len(obsolete),
            "allowlist_entries_with_slack": len(slack),
            "frames": [
                {
                    "size": f.size,
                    "location": f.location,
                    "line": f.line,
                    "signature": f.signature,
                    "qualifiers": f.qualifiers,
                    "allowlisted_budget": budget_of(f),
                }
                for f in frames
            ],
        }
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        print(f"\nWrote {args.json}")

    failed = bool(violations or regressions or allow_problems)
    if args.fail_on_stale and (stale or obsolete):
        failed = True

    worst = frames[0] if frames else None
    worst_txt = f"{worst.size}B({worst.location})" if worst else "none"
    print()
    print(
        f"STACK_BUDGET: {'FAIL' if failed else 'PASS'} threshold={args.threshold}B "
        f"env={args.env} frames={len(frames)} over={len(violations)} "
        f"regressions={len(regressions)} allowlisted={len(allowed_over)} "
        f"stale={len(stale)} obsolete={len(obsolete)} slack={len(slack)} "
        f"worst={worst_txt}"
    )
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
