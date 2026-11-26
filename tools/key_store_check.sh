#!/bin/bash
# The key store, end to end on the desktop Picco simulator (owner 2026-11-26;
# docs/keys-and-paste-2026-11-26.md). Fake values only: never a real key.
#
#   0. a gate: host-keys.txt (the desktop store, written beside the card) is
#      git-ignored in both repos. A run from a repo root writes it there, and
#      both forks are public (pass 3, finding 1).
#   1. a card that still carries the files and the settings.json field older
#      builds read: nothing is read from them (no migration, by the owner's
#      no-backwards-compatibility ruling), the row reads "Not set" and opens
#      the entry, a 41-character value typed into the Secret field lands in
#      the store, and no fake value is on the card or in the log.
#   2. with a key saved, the row's popup -> Enter or paste, and a
#      255-character value: stored whole, nothing on the card or in the log.
# Screenshots of the row, both fields and the row after are kept for a look:
# the field must be ONE line, a mask and the length (owner 2026-11-26: "do not
# wrap long secret inputs or outputs").
#
# Usage: tools/key_store_check.sh   (needs: pio run -e simulator_picco)
set -u
cd "$(dirname "$0")/.."
BIN="$PWD/.pio/build/simulator_picco/program"
[ -x "$BIN" ] || { echo "build first: pio run -e simulator_picco"; exit 2; }
WORK="$(mktemp -d)"
FAKE_OLD_GH="FAKE-OLD-CARD-TOKEN-NOT-REAL-000000000-WXYZ"
FAKE_OLD_CL="FAKE-OLD-CARD-KEY-NOT-REAL-111111111-ABCD"
FAKE_OLD_SET="FAKE-OLD-SETTINGS-TOKEN-NOT-REAL-444444444"
FAKE_NEW="FAKE-TYPED-TOKEN-NOT-REAL-2222222222-QRST"                   # 41 characters
FAKE_LONG="FAKE-LONG-NOT-REAL-$(printf '5%.0s' $(seq 1 231))-LONG"      # 255 characters
pass=0; fail=0
check() { if eval "$2"; then echo "PASS  $1"; pass=$((pass + 1)); else echo "FAIL  $1"; fail=$((fail + 1)); fi; }
# No new fake (or a 16-char prefix of one) anywhere under a path.
clean_of() { ! grep -r -a -q -e "${FAKE_NEW:0:16}" -e "${FAKE_LONG:0:16}" "$@" 2>/dev/null; }
# Picco buttons: UP = next, CONFIRM = select. Home -> Settings is 8 rows down;
# GitHub Token is Settings row 21 (after Device owner). TYPE types like a host
# keyboard; \n commits. Prints the script up to the row, and sets $t.
to_row() {
  t=3500; s="200:QTAP:BACK:2500"
  for i in 1 2 3 4 5 6 7 8; do s="$s;$t:UP"; t=$((t + 900)); done
  s="$s;$t:CONFIRM"; t=$((t + 1500))
  for i in $(seq 1 21); do s="$s;$t:UP"; t=$((t + 600)); done
}

# --- 0. the desktop key file is git-ignored in both repos --------------------
check "gitignore: host-keys.txt is ignored in the firmware repo" 'git check-ignore -q host-keys.txt'
check "gitignore: host-keys.txt is ignored in the simulator repo" \
  '[ ! -d ../crosspoint-simulator ] || git -C ../crosspoint-simulator check-ignore -q host-keys.txt'
check "the fakes are the lengths the cases name" '[ ${#FAKE_NEW} -eq 41 ] && [ ${#FAKE_LONG} -eq 255 ]'

# --- 1. old copies ignored; a 41-character value typed ------------------------
D="$WORK/entry"; mkdir -p "$D/fs_/.crosspoint"
printf '%s\n' "$FAKE_OLD_CL" >"$D/fs_/claude-key.txt"
printf '%s\n' "$FAKE_OLD_GH" >"$D/fs_/github-token.txt"
printf '{"githubToken":"%s"}\n' "$FAKE_OLD_SET" >"$D/fs_/.crosspoint/settings.json"
to_row
rowShot=$((t + 800))
s="$s;$t:CONFIRM"; t=$((t + 1500))            # nothing set: straight to the entry
s="$s;$t:TYPE:$FAKE_NEW"; t=$((t + 1500))
fieldShot=$t; t=$((t + 500))
s="$s;$t:TYPE:\\n"; t=$((t + 2500))
afterShot=$t
s="$s;$((t + 1500)):QUIT"
(cd "$D" && CROSSPOINT_SIM_INPUT_SCRIPT="$s" \
  CROSSPOINT_SIM_SCREENSHOTS="$rowShot:$D/1-row-not-set.bmp;$fieldShot:$D/2-field-41.bmp;$afterShot:$D/3-row-after.bmp" \
  SDL_VIDEODRIVER=dummy timeout 120 "$BIN" >log 2>&1)
check "old copies: nothing read from the card files or settings.json" \
  '! grep -a -q -e FAKE-OLD "$D/host-keys.txt" 2>/dev/null'
check "old copies: no old value in the log" '! grep -a -q FAKE-OLD "$D/log"'
check "old copies: /github-token.txt deleted at boot" '[ ! -e "$D/fs_/github-token.txt" ]'
check "old copies: /claude-key.txt deleted at boot" '[ ! -e "$D/fs_/claude-key.txt" ]'
check "entry: the typed 41-character token is in the store" 'grep -qx "githubToken=$FAKE_NEW" "$D/host-keys.txt"'
check "entry: the store file is owner-only" '[ "$(stat -f %Lp "$D/host-keys.txt")" = 600 ]'
check "entry: no Claude key was stored" '! grep -q "^claudeApiKey=" "$D/host-keys.txt"'
check "entry: no fake value on the card" 'clean_of "$D/fs_"'
check "entry: no fake value in the log" 'clean_of "$D/log"'

# --- 2. a 255-character value, over a saved one ------------------------------
D2="$WORK/long"; mkdir -p "$D2"; cp -R "$D/fs_" "$D2/fs_"; cp "$D/host-keys.txt" "$D2/"
to_row
s="$s;$t:CONFIRM"; t=$((t + 1200))            # a key is set: the popup
s="$s;$t:CONFIRM"; t=$((t + 1500))            # Enter or paste
s="$s;$t:TYPE:$FAKE_LONG"; t=$((t + 2500))
longShot=$t; t=$((t + 500))
s="$s;$t:TYPE:\\n"; t=$((t + 2500))
longAfter=$t
s="$s;$((t + 1500)):QUIT"
(cd "$D2" && CROSSPOINT_SIM_INPUT_SCRIPT="$s" \
  CROSSPOINT_SIM_SCREENSHOTS="$longShot:$D2/4-field-255.bmp;$longAfter:$D2/5-row-after-255.bmp" \
  SDL_VIDEODRIVER=dummy timeout 120 "$BIN" >log 2>&1)
check "long: the 255-character value is stored whole" 'grep -qx "githubToken=$FAKE_LONG" "$D2/host-keys.txt"'
check "long: no fake value on the card" 'clean_of "$D2/fs_"'
check "long: no fake value in the log" 'clean_of "$D2/log"'

echo "$pass passed, $fail failed  (logs and screenshots: $WORK)"
[ "$fail" -eq 0 ]
