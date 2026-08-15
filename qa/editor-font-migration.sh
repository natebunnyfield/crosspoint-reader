#!/usr/bin/env bash
# Prove where each OLD stored editorFont value lands after the 2026-08-15 cut to
# three faces. This is the migration's only real test: the host suites do not
# link the real CrossPointSettings.cpp (they stub it in HostHarness), so
# fromJson() -- where the legacy path lives and where the name-key gate is --
# only ever runs inside the firmware itself. The simulator runs that firmware.
#
# For each legacy value: write a settings.json carrying ONLY the old byte and no
# "editorFontFamily" key, boot the simulator headless, then read the file back.
# The firmware sets needsResave on the legacy branch, so a correct migration
# rewrites the file with the family NAME -- which is both the observable proof
# and the thing that makes the migration one-shot.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SETTINGS="$ROOT/fs_/.crosspoint/settings.json"
SIM="$ROOT/.pio/build/simulator/program"
BACKUP="$(mktemp)"

[ -x "$SIM" ] || { echo "no simulator binary at $SIM"; exit 1; }
cp "$SETTINGS" "$BACKUP"
restore() { cp "$BACKUP" "$SETTINGS"; rm -f "$BACKUP"; }
trap restore EXIT

# legacy stored byte -> family the ruling says it must land on.
# 0..5 are positions in the SIX-row table that shipped between the two
# 2026-08-15 rulings; 6 is out of range for it (it was Nitti under the SEVEN-row
# table that preceded, and cannot be told apart from a six-row 6 -- see
# EditorFonts.h).
EXPECT_0=iAWriterQuattro   # iA Writer Quattro, exact
EXPECT_1=iAWriterQuattro   # iA Writer Duo     -> same superfamily
EXPECT_2=iAWriterQuattro   # iA Writer Mono    -> same superfamily
EXPECT_3=PragmataPro       # IBM Plex Mono     -> nearest surviving mono
EXPECT_4=PragmataPro       # PragmataPro, exact
EXPECT_5=NittiTypewriter   # Nitti Typewriter, exact
EXPECT_6=iAWriterQuattro   # out of range -> default

fail=0
for n in 0 1 2 3 4 5 6; do
  eval "want=\$EXPECT_$n"
  # Legacy shape: the byte, and deliberately NO editorFontFamily key.
  python3 - "$SETTINGS" "$n" <<'PY'
import json, sys
p, n = sys.argv[1], int(sys.argv[2])
d = json.load(open(p))
d["editorFont"] = n
d.pop("editorFontFamily", None)
json.dump(d, open(p, "w"), indent=1)
PY

  CROSSPOINT_SIM_INPUT_SCRIPT='2500:QUIT' SDL_VIDEODRIVER=dummy \
    timeout 60 "$SIM" >/dev/null 2>&1

  got="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("editorFontFamily","<ABSENT>"))' "$SETTINGS")"
  if [ "$got" = "$want" ]; then
    echo "PASS  stored editorFont=$n  ->  $got"
  else
    echo "FAIL  stored editorFont=$n  ->  $got   (expected $want)"
    fail=1
  fi
done

# And the whole point of the rewrite: once the NAME is present it is believed,
# and the legacy byte beside it is ignored rather than re-migrated. This is the
# case the predecessor migration got wrong -- it re-applied on every load and
# walked a saved choice down the list one row per boot.
echo
python3 - "$SETTINGS" <<'PY'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
d["editorFontFamily"] = "NittiTypewriter"
d["editorFont"] = 0          # disagreeing stale byte: the name must win
json.dump(d, open(p, "w"), indent=1)
PY
for boot in 1 2 3; do
  CROSSPOINT_SIM_INPUT_SCRIPT='2500:QUIT' SDL_VIDEODRIVER=dummy \
    timeout 60 "$SIM" >/dev/null 2>&1
  got="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("editorFontFamily","<ABSENT>"))' "$SETTINGS")"
  if [ "$got" = "NittiTypewriter" ]; then
    echo "PASS  boot $boot: name still NittiTypewriter (no drift)"
  else
    echo "FAIL  boot $boot: name drifted to $got"
    fail=1
  fi
done

exit $fail
