#!/usr/bin/env bash
#
# Fast-forward the freeink-sdk submodule and prove the bump off-device.
#
#   scripts/update_sdk.sh            # fast-forward to upstream HEAD, verify
#   scripts/update_sdk.sh --dry-run  # report the gap and what it touches, change nothing
#   scripts/update_sdk.sh <ref>      # go to a specific SDK commit instead of HEAD
#
# WHY THIS EXISTS
#
# The SDK compiles into every device binary, so "it builds" is close to no
# evidence at all. The commits in a typical bump include e-paper waveform LUTs,
# GPIO/PWM ordering, deep-sleep pin states and battery voltage curves -- none of
# which a host build or a host test can execute. What CAN be checked off-device
# is that the parts the simulator does render come out unchanged, and this
# script's job is to make that check automatic rather than remembered.
#
# It refuses to leave the submodule anywhere the pin cannot be reproduced: a
# dirty submodule, a non-fast-forward, or a failing gate all abort with the
# pointer put back.
#
# WHAT IT CANNOT DO, AND WHY IT STILL ASKS YOU TO READ A BOOK
#
# Everything below runs on the host. A green run means the bump did not change
# layout, geometry or any tested behaviour -- it says nothing about waveforms,
# refresh artefacts, sleep current or battery reporting. Those need the device.
# The script prints that reminder at the end rather than claiming success.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
REPO="$PWD"
SDK="$REPO/freeink-sdk"
SHOTS="${TMPDIR:-/tmp}/sdk-bump-$$"
DRY=0
TARGET=""

for arg in "$@"; do
  case "$arg" in
    --dry-run) DRY=1 ;;
    -*) echo "unknown option: $arg" >&2; exit 2 ;;
    *) TARGET="$arg" ;;
  esac
done

die() { echo "FAIL: $*" >&2; exit 1; }
step() { printf '\n=== %s\n' "$1"; }

[[ -d "$SDK/.git" || -f "$SDK/.git" ]] || die "freeink-sdk is not initialised. A fresh worktree does NOT inherit
      submodules -- run: git submodule update --init --recursive"

# A dirty submodule means someone is mid-something in there; a bump would bury it.
[[ -z "$(git -C "$SDK" status --porcelain)" ]] || die "freeink-sdk has local changes; commit or stash them first"

step "Where the pin is"
git -C "$SDK" fetch -q origin || die "could not fetch the SDK remote"
OLD="$(git -C "$SDK" rev-parse HEAD)"
NEW="${TARGET:-$(git -C "$SDK" rev-parse origin/HEAD 2>/dev/null || git -C "$SDK" rev-parse origin/main)}"
NEW="$(git -C "$SDK" rev-parse "$NEW")"

if [[ "$OLD" == "$NEW" ]]; then
  echo "already at ${OLD:0:8} — nothing to do"
  exit 0
fi

read -r BEHIND AHEAD <<<"$(git -C "$SDK" rev-list --left-right --count "$OLD...$NEW" | tr '\t' ' ')"
echo "pinned  ${OLD:0:8}"
echo "target  ${NEW:0:8}"
echo "local commits not upstream: $BEHIND   upstream commits not local: $AHEAD"

# A non-fast-forward means the fork carries SDK commits of its own. That is a
# merge, with a decision in it, and it is not this script's to make.
if [[ "$BEHIND" -ne 0 ]]; then
  die "the pin is $BEHIND commits AHEAD of the target — this is not a fast-forward.
      Someone has committed to the SDK locally. Resolve that by hand."
fi

step "What the bump touches"
git -C "$SDK" diff --stat "$OLD..$NEW" | tail -1
git -C "$SDK" diff --name-only "$OLD..$NEW" | awk -F/ '{print $1"/"$2}' | sort | uniq -c | sort -rn | head -8
echo
echo "Commits that name a device, a waveform or a pin — the ones a host cannot test:"
git -C "$SDK" log --oneline "$OLD..$NEW" |
  grep -iE 'waveform|lut|gpio|pwm|pin|sleep|voltage|batter|x3|x4|display|epd' |
  sed 's/^/    /' || echo "    (none)"

if [[ "$DRY" -eq 1 ]]; then
  echo
  echo "--dry-run: the pin was not moved."
  exit 0
fi

# ---- reference render, BEFORE the bump ------------------------------------
#
# Taken from the build that is already on disk. If there is no simulator binary
# the comparison is skipped rather than faked -- a bump verified without it is
# still worth more than one verified by nothing, and saying so beats a green
# tick that means less than it looks.
mkdir -p "$SHOTS"
shots() { # $1 = label
  CROSSPOINT_SIM_INPUT_SCRIPT='2000:HOME;2600:ENTER;9600:QUIT' \
  CROSSPOINT_SIM_SCREENSHOTS="2400:$SHOTS/$1-home.bmp;6600:$SHOTS/$1-page.bmp" \
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  timeout 60 .pio/build/simulator_x3/program >/dev/null 2>&1 || true
}
HAVE_REF=0
if [[ -x .pio/build/simulator_x3/program ]]; then
  step "Reference render (pre-bump)"
  shots before
  [[ -f "$SHOTS/before-home.bmp" ]] && HAVE_REF=1
  echo "captured: $(ls "$SHOTS" | tr '\n' ' ')"
else
  echo "NOTE: no simulator_x3 binary on disk — skipping the render comparison."
fi

step "Moving the pin"
git -C "$SDK" checkout -q "$NEW" || die "could not check out ${NEW:0:8}"
restore() { git -C "$SDK" checkout -q "$OLD"; echo "pin restored to ${OLD:0:8}" >&2; }

step "Device build (gh_release)"
pio run -e gh_release >/dev/null 2>&1 || { restore; die "gh_release did not build"; }
echo "ok"

step "Desktop build (simulator_x3)"
pio run -e simulator_x3 >/dev/null 2>&1 || { restore; die "simulator_x3 did not build"; }
echo "ok"

step "Host tests"
BUILD="${TMPDIR:-/tmp}/sdk-bump-test-$$"
cmake -S test -B "$BUILD" >/dev/null 2>&1 || { restore; die "test configure failed"; }
cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)" >/dev/null 2>&1 ||
  { restore; die "tests did not build"; }
CTEST_OUT="$( cd "$BUILD" && ctest --output-on-failure 2>&1 )" || { restore; die "host tests failed"; }
echo "ok — $(echo "$CTEST_OUT" | grep -oE 'out of [0-9]+' | tail -1 | sed 's/out of //') tests"

if [[ "$HAVE_REF" -eq 1 ]]; then
  step "Render comparison"
  shots after
  python3 - "$SHOTS" <<'PYEOF' || { restore; die "the render changed — inspect $SHOTS before bumping"; }
import sys, struct
d = sys.argv[1]
def pixels(p):
    raw = open(p, "rb").read()
    off = struct.unpack_from("<I", raw, 10)[0]
    w, h = struct.unpack_from("<ii", raw, 18)
    bpp = struct.unpack_from("<H", raw, 28)[0]
    stride = ((w * bpp // 8) + 3) & ~3
    step = bpp // 8
    return w, abs(h), bytes(raw[off + r*stride + c*step] for r in range(abs(h)) for c in range(w))
bad = False
for name in ("home", "page"):
    try:
        aw, ah, a = pixels(f"{d}/before-{name}.bmp")
        bw, bh, b = pixels(f"{d}/after-{name}.bmp")
    except OSError:
        print(f"  {name}: missing capture, skipped"); continue
    if (aw, ah) != (bw, bh):
        print(f"  {name}: SIZE CHANGED {aw}x{ah} -> {bw}x{bh}"); bad = True; continue
    diff = sum(1 for x, y in zip(a, b) if abs(x - y) > 8)
    print(f"  {name}: {diff} px differ of {len(a)}")
    if diff: bad = True
sys.exit(1 if bad else 0)
PYEOF
  echo "identical"
fi

step "Result"
git -C "$SDK" log --oneline -1
cat <<EOF

The pin is moved but NOT committed — 'git add freeink-sdk' when you are happy.

WHAT THIS RUN DID NOT PROVE. Every gate above is a host gate. It shows the bump
changes nothing the simulator renders and breaks no tested behaviour. It cannot
execute a waveform LUT, a GPIO ordering change, a deep-sleep pin state or a
battery curve — and those are most of what an SDK bump actually carries.

Before trusting this on hardware: flash it, read a book for a few pages, watch
for ghosting or a changed refresh, sleep and wake it, and check the battery
reading. Screenshots: $SHOTS
EOF
