#!/usr/bin/env bash
#
# Cut a firmware release: build, verify, tag, publish.
#
# WHY THIS EXISTS. `.github/workflows/release.yml` triggers on tag pushes and
# has NEVER RUN on this fork -- checked 2026-08-28, every workflow reports zero
# runs, so no CI has ever executed here and every release has been assembled by
# hand. Hand-assembly is how a step gets skipped, and the step most worth not
# skipping is the one that checks the binary says what the source says. This
# script is the workflow, run locally, with the checks kept.
#
# It refuses rather than repairs. Every guard below is a state where continuing
# would publish something subtly wrong, and a release is the worst place to
# discover that.
#
#   ./scripts/release.sh            # build, verify, tag, publish
#   ./scripts/release.sh --dry-run  # everything except the tag and the publish
set -euo pipefail

cd "$(dirname "$0")/.."
DRY=0
[[ "${1:-}" == "--dry-run" ]] && DRY=1

step() { printf '\n=== %s ===\n' "$1"; }
die()  { printf '\nREFUSED: %s\n' "$1" >&2; exit 1; }

step "Preflight"
VERSION=$(sed -n 's/^version *= *//p' platformio.ini | head -1 | tr -d ' ')
[[ -n "$VERSION" ]] || die "no [crosspoint] version in platformio.ini"
echo "  version: $VERSION"

# The tag is the FULL version string, suffix included (B-034). A bare 1.5.N tag
# in this repo is upstream's, arriving through the upstream remote, and taking
# one would collide with a release that already means something else.
[[ "$VERSION" == *-* ]] || die "version '$VERSION' has no fork suffix; a bare tag is upstream's (B-034)"

[[ -z "$(git status --porcelain)" ]] || die "working tree is dirty; a release must be reproducible from a commit"
# The next two guard the TAG and the PUBLISH, which a dry run never reaches:
# a dry run of an already-tagged version is exactly how a release path is
# rehearsed (scripts/release-from-repo.sh, cut-release.yml), so they apply to
# a real cut only.
if [[ $DRY -eq 0 ]]; then
  [[ -z "$(git log --oneline '@{u}'..HEAD 2>/dev/null)" ]] || die "unpushed commits; push before tagging or the tag names something nobody else has"
  git rev-parse -q --verify "refs/tags/$VERSION" >/dev/null && die "tag $VERSION already exists; bump [crosspoint] version first"
else
  git rev-parse -q --verify "refs/tags/$VERSION" >/dev/null && echo "  (dry run: tag $VERSION already exists; a real cut would refuse here)"
fi

REMOTE_URL=$(git remote get-url origin)
# The push hazard from CLAUDE.md: two dead clones point origin at UPSTREAM, and
# a release pushed from one would publish to the public project.
[[ "$REMOTE_URL" == *natebunnyfield* ]] || die "origin is '$REMOTE_URL', not the fork -- refusing to publish"
echo "  origin:  $REMOTE_URL"

step "Build"
pio run -e gh_release

BIN=.pio/build/gh_release/firmware.bin
for f in "$BIN" .pio/build/gh_release/bootloader.bin .pio/build/gh_release/partitions.bin; do
  [[ -f "$f" ]] || die "missing build artifact $f"
done

step "Verify the image says what the source says"
# The descriptor is the field that has lied before (B-033): it arrives prebuilt
# and stale from the framework package, and scripts/stamp_app_desc.py rewrites
# it post-link. If that stamper ever silently stops running, THIS is where it
# shows -- which is the whole reason the check is here and not in a comment.
python3 - "$BIN" "$VERSION" <<'PY'
import functools, hashlib, operator, struct, sys
path, want = sys.argv[1], sys.argv[2]
b = open(path, "rb").read()
o = 0x20
if b[0] != 0xE9:
    sys.exit(f"REFUSED: {path} is not an ESP32 image (first byte 0x{b[0]:02X})")
magic, = struct.unpack_from("<I", b, o)
if magic != 0xABCD5432:
    sys.exit(f"REFUSED: no app descriptor at 0x20 (magic 0x{magic:08X})")
got = b[o+16:o+48].split(b"\0")[0].decode(errors="replace")
if got != want:
    sys.exit(f"REFUSED: descriptor says {got!r}, platformio.ini says {want!r} "
             f"-- stamp_app_desc.py did not run, or ran wrong (B-033)")
# The XOR checksum byte is what the stamper left stale in 1.5.17-BD..1.5.21-BD
# (B-046): the device validator, esp_ota_end() and the bootloader all check it,
# and a sha256 recomputed over a stale byte is a valid hash of an invalid image.
pos, acc = 24, 0xEF
for _ in range(b[1]):
    if pos + 8 > len(b):
        sys.exit("REFUSED: segment table overruns the file")
    n = struct.unpack_from("<I", b, pos + 4)[0]
    pos += 8
    if pos + n > len(b):
        sys.exit("REFUSED: a segment overruns the file")
    acc = functools.reduce(operator.xor, b[pos:pos + n], acc)
    pos += n
pad_end = (pos + 16) & ~15
if pad_end + (32 if b[23] == 1 else 0) != len(b):
    sys.exit("REFUSED: segment table does not add up to the file size")
if b[pad_end - 1] != acc:
    sys.exit(f"REFUSED: image checksum byte is 0x{b[pad_end - 1]:02X} but the segment data says 0x{acc:02X}; "
             "the device validator and the bootloader will reject this (B-046)")
if b[23] == 1 and hashlib.sha256(b[:-32]).digest() != b[-32:]:
    sys.exit("REFUSED: appended sha256 does not match; the bootloader will reject this")
print(f"  descriptor: {got}")
print(f"  checksum:   valid")
print(f"  sha256:     valid")
PY

step "Host tests"
cmake -S test -B build/release-test >/dev/null
cmake --build build/release-test -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)" >/dev/null
( cd build/release-test && ctest --output-on-failure ) | tail -3

if [[ $DRY -eq 1 ]]; then
  printf '\n--dry-run: everything verified, nothing tagged or published.\n'
  exit 0
fi

step "Tag and publish"
git tag -a "$VERSION" -m "$VERSION"
git push origin "$VERSION"

# gh release create, not the workflow: release.yml has never fired here.
gh release create "$VERSION" --repo natebunnyfield/crosspoint-reader \
  --title "$VERSION" --generate-notes \
  "$BIN" .pio/build/gh_release/bootloader.bin .pio/build/gh_release/partitions.bin

step "Confirm"
# A release with no firmware.bin is invisible to OtaUpdater, which looks for
# that asset by name -- so the publish is not finished until it is there.
gh release view "$VERSION" --repo natebunnyfield/crosspoint-reader \
  --json assets --jq '.assets[] | "  \(.name)  \(.size) bytes"'
gh release view "$VERSION" --repo natebunnyfield/crosspoint-reader \
  --json assets --jq '[.assets[].name] | index("firmware.bin") // empty' >/dev/null \
  || die "the release has no firmware.bin; OtaUpdater will not see it"
printf '\nReleased %s\n' "$VERSION"
