#!/usr/bin/env bash
#
# Cut a firmware release with nobody at the keyboard. scripts/release.applescript
# hands this to Terminal.app; a phone (Shortcuts "Run Script Over SSH", Blink,
# Terminus) fires the AppleScript over SSH, and the result lands back on the
# phone as an ntfy notification. Same shape as crosspoint-simulator's
# ios/deploy-from-repo.sh, which ships the TestFlight build the same way.
#
# What it does, in order, refusing loudly at each step rather than repairing:
#   1. cd to the fork checkout, switch to main, `git pull --ff-only`.
#   2. Find pio (Terminal's login shell has it; a bare SSH shell may not).
#   3. Disk: if the volume has under CROSSPOINT_MIN_FREE_GB (6) free, delete
#      the two regenerable build trees (.pio/build and the shared
#      ~/.platformio/build_cache) first -- the 2026-09-05 release died on
#      "No space left on device" in exactly that state.
#   4. Version: if the tag for platformio.ini's version already exists, bump
#      the patch number (1.5.23-BD -> 1.5.24-BD), append the usual history
#      note, commit "chore(release): old -> new" with the commits since the
#      old tag in its body, and push main. CROSSPOINT_AUTO_BUMP=0 disables
#      this and lets release.sh refuse instead.
#   5. ./scripts/release.sh, which builds, verifies the image against the
#      source (B-033, B-046), tags, and publishes with gh. CROSSPOINT_DRY_RUN=1
#      passes --dry-run: everything except the tag and the publish, and step 4
#      is skipped too, so a rehearsal never pushes anything.
#
# The same script is what .github/workflows/cut-release.yml runs on a hosted
# runner, with the licensed faces from the private mirror instead of the Mac's
# folder; nothing here is Mac-only except the ~/.platformio path it prunes.
#
# Environment (KEY=VALUE arguments to the AppleScript arrive here through env):
#   CROSSPOINT_FIRMWARE_DIR   checkout to release from   (~/src/crosspoint-reader)
#   CROSSPOINT_AUTO_BUMP      1 = bump when the tag exists (default), 0 = refuse
#   CROSSPOINT_DRY_RUN        1 = release.sh --dry-run
#   CROSSPOINT_MIN_FREE_GB    free space below which build trees are pruned (6)
#   CROSSPOINT_NTFY_TOPIC     ntfy.sh topic for the result (the iOS deploy's)

set -euo pipefail

FIRMWARE_DIR="${CROSSPOINT_FIRMWARE_DIR:-$HOME/src/crosspoint-reader}"
AUTO_BUMP="${CROSSPOINT_AUTO_BUMP:-1}"
DRY_RUN="${CROSSPOINT_DRY_RUN:-0}"
MIN_FREE_GB="${CROSSPOINT_MIN_FREE_GB:-6}"
NTFY_TOPIC="${CROSSPOINT_NTFY_TOPIC:-crds-ios-natebunnyfield-9k3m2p7v}"

say() { printf '\n=== %s ===\n' "$1"; }
notify() { # notify <priority> <tag> <title> <body>
  curl -s -m 10 -H "Title: $3" -H "Priority: $1" -H "Tags: $2" -d "$4" \
    "https://ntfy.sh/$NTFY_TOPIC" >/dev/null 2>&1 || true
}
die() {
  printf '\nREFUSED: %s\n' "$1" >&2
  notify 4 warning "CrossPoint firmware release refused" "$1"
  exit 1
}
# Anything release.sh refuses, or any other failure, reaches the phone too.
trap 'rc=$?; if [[ $rc -ne 0 ]]; then notify 4 warning "CrossPoint firmware release failed" "exit $rc at: ${BASH_COMMAND:-?} (see the Terminal tab on the Mac)"; fi' EXIT

say "Checkout"
cd "$FIRMWARE_DIR" || die "no checkout at $FIRMWARE_DIR (set CROSSPOINT_FIRMWARE_DIR)"
[[ "$(git remote get-url origin)" == *natebunnyfield/crosspoint-reader* ]] \
  || die "origin is $(git remote get-url origin), not the fork"
[[ -z "$(git status --porcelain)" ]] || die "working tree is dirty; stash or commit on the Mac first"
git checkout -q main
git pull --ff-only origin main || die "git pull --ff-only failed; main has diverged, resolve on the Mac"
echo "  main at $(git rev-parse --short HEAD): $(git log -1 --format=%s)"

say "Toolchain"
if ! command -v pio >/dev/null; then
  for candidate in "$HOME/.platformio/penv/bin" "$HOME/.local/bin" /opt/homebrew/bin /usr/local/bin; do
    if [[ -x "$candidate/pio" ]]; then export PATH="$candidate:$PATH"; break; fi
  done
fi
command -v pio >/dev/null || die "pio not found on PATH or in the usual install locations"
command -v gh  >/dev/null || die "gh not found"
gh auth status >/dev/null 2>&1 || die "gh is not logged in"
echo "  pio: $(command -v pio)"

say "Disk"
free_kb=$(df -k . | awk 'NR==2 {print $4}')
free_gb=$(( free_kb / 1024 / 1024 ))
echo "  ${free_gb} GB free on $(df -k . | awk 'NR==2 {print $NF}')"
if (( free_gb < MIN_FREE_GB )); then
  echo "  under ${MIN_FREE_GB} GB: pruning the regenerable build trees"
  du -sh .pio/build "$HOME/.platformio/build_cache" 2>/dev/null | sed 's/^/    /' || true
  rm -rf .pio/build "$HOME/.platformio/build_cache"
  free_kb=$(df -k . | awk 'NR==2 {print $4}')
  echo "  now $(( free_kb / 1024 / 1024 )) GB free"
fi

say "Version"
VERSION=$(sed -n 's/^version *= *//p' platformio.ini | head -1 | tr -d ' ')
[[ -n "$VERSION" ]] || die "no [crosspoint] version in platformio.ini"
git fetch -q --tags origin
if [[ "$DRY_RUN" == "1" ]]; then
  echo "  dry run: no bump, no tag, no publish (release.sh --dry-run tolerates an existing tag)"
elif git rev-parse -q --verify "refs/tags/$VERSION" >/dev/null; then
  [[ "$AUTO_BUMP" == "1" ]] || die "tag $VERSION already exists and CROSSPOINT_AUTO_BUMP=0"
  NEW=$(python3 - "$VERSION" <<'PY'
import re, sys
m = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)-([A-Za-z0-9]+)", sys.argv[1])
if not m: sys.exit(f"cannot bump '{sys.argv[1]}': expected X.Y.Z-SUFFIX")
print(f"{m[1]}.{m[2]}.{int(m[3]) + 1}-{m[4]}")
PY
)
  COUNT=$(git rev-list --count "$VERSION"..HEAD)
  (( COUNT > 0 )) || die "tag $VERSION already exists and main has no commits since it; nothing to release"
  echo "  $VERSION is tagged; bumping to $NEW ($COUNT commits since)"
  python3 - "$VERSION" "$NEW" "$COUNT" <<'PY'
import re, subprocess, sys, datetime
old, new, count = sys.argv[1], sys.argv[2], sys.argv[3]
p = "platformio.ini"
s = open(p, encoding="utf-8").read()
anchor = "; THE TAG IS THE FULL STRING"
assert s.count(anchor) == 1, "history-note anchor missing from platformio.ini"
assert s.count(f"version = {old}") == 1, "version line not found"
subjects = subprocess.check_output(["git", "log", "--format=%s", f"{old}..HEAD"], text=True).splitlines()
lead = "; ".join(x[:60] for x in subjects[:3])
short_old, short_new = old.rsplit("-", 1)[0], new.rsplit("-", 1)[0]
today = datetime.date.today().isoformat()
note = (f"; {short_old} -> {short_new} on {today}, cut by scripts/release-from-repo.sh (phone-fired):\n"
        f"; {count} commit(s) since {old} -- {lead}.\n;\n")
s = s.replace(anchor, note + anchor, 1).replace(f"version = {old}", f"version = {new}", 1)
open(p, "w", encoding="utf-8").write(s)
PY
  {
    printf 'chore(release): %s -> %s\n\n' "$VERSION" "$NEW"
    printf 'Cut by scripts/release-from-repo.sh on %s, %s. Commits since %s:\n\n' \
      "$(hostname -s)" "$(date -u +%Y-%m-%dT%H:%MZ)" "$VERSION"
    git log --format='- %s' "$VERSION"..HEAD
    printf '\nThe tag is the full string, %s; release.sh creates it after the\nbuild and its gates pass.\n' "$NEW"
  } > .git/RELEASE_BUMP_MSG
  git add platformio.ini
  git commit -q -F .git/RELEASE_BUMP_MSG
  rm -f .git/RELEASE_BUMP_MSG
  git push origin main || die "push of the version bump failed"
  VERSION="$NEW"
fi
echo "  releasing $VERSION"

say "Release"
notify 3 hammer "CrossPoint firmware $VERSION: building" "release.sh started on $(hostname -s)"
if [[ "$DRY_RUN" == "1" ]]; then
  ./scripts/release.sh --dry-run
  notify 4 white_check_mark "CrossPoint firmware $VERSION: dry run OK" "Built and verified; nothing tagged or published."
else
  ./scripts/release.sh
  notify 4 rocket "CrossPoint firmware $VERSION published" \
    "https://github.com/natebunnyfield/crosspoint-reader/releases/tag/$VERSION"
fi
