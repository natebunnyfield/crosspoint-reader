#!/bin/bash
# The Picco header back button, screen by screen (owner 2026-11-26: "on screen
# back button not working for me. check claude and all screens with tests.
# prove it is broken then prove it works").
#
# For each case: boot the desktop Picco simulator on an EMPTY card (a fresh
# directory per case, so no case inherits another's state), reach the screen
# with the Picco's own buttons (UP = rocker upper = next, CONFIRM = the GPIO5
# key), then put a finger on the header's back arrow with a scripted touch
# (TAP, the same contact model the iOS app feeds) and read the firmware's own
# log: the screen must EXIT, and the tap must have been read as the header back
# button. Touches go through the Picco gesture decoder exactly as on the device
# (MappedInputManager::synthesizeGesture).
#
# Usage: tools/picco_back_sweep.sh [-k <name-substring>]
#   needs: pio run -e simulator_picco (builds .pio/build/simulator_picco/program)
# Exit 0 only if every case passes.
set -u
cd "$(dirname "$0")/.."
BIN="$PWD/.pio/build/simulator_picco/program"
[ -x "$BIN" ] || { echo "build first: pio run -e simulator_picco"; exit 2; }
FILTER="${2:-}"
[ "${1:-}" = "-k" ] || FILTER=""
WORK="$(mktemp -d)"
# The arrow: LyraTheme::drawHeader, 32 px icon at (contentSidePadding,
# topPadding + batteryBarHeight) in logical portrait px. Its center:
ARROW="36,62"

# name | button presses after Home is up (space-separated) | activity that must exit
CASES=(
  "Recent Books|CONFIRM|RecentBooks"
  "Browse Files|UP CONFIRM|FileBrowser"
  "Manage Files|UP UP CONFIRM|FileManager"
  "File Transfer|UP UP UP CONFIRM|CrossPointWebServer"
  # Create Note has no header; it draws the header's arrow alone in the same
  # corner (owner 2026-11-26, B1 "Arrow only, + Note's").
  "Create Note|UP UP UP UP CONFIRM|NoteEditor"
  "Claude|UP UP UP UP UP CONFIRM|ClaudeChat"
  "Update Firmware|UP UP UP UP UP UP CONFIRM|OnlineFirmwareUpdate"
  "Update Library|UP UP UP UP UP UP UP CONFIRM|LibraryUpdate"
  "Settings|UP UP UP UP UP UP UP UP CONFIRM|Settings"
  # Settings rows that open a screen of their own (row index = UP presses).
  "Settings > Reader Font|UP UP UP UP UP UP UP UP CONFIRM CONFIRM|FontSelect"
  "Settings > Typography Settings|UP UP UP UP UP UP UP UP CONFIRM UP CONFIRM|Typography"
  "Settings > Editor Font|UP UP UP UP UP UP UP UP CONFIRM UP UP CONFIRM|EditorFontSelect"
  "Settings > Clock UTC Offset|UP UP UP UP UP UP UP UP CONFIRM UP UP UP UP UP UP UP UP UP CONFIRM|ClockOffset"
  "Settings > Wi-Fi Networks|UP UP UP UP UP UP UP UP CONFIRM UP UP UP UP UP UP UP UP UP UP UP UP UP CONFIRM|WifiSelection"
  "Settings > Clear Cache|UP UP UP UP UP UP UP UP CONFIRM UP UP UP UP UP UP UP UP UP UP UP UP UP UP UP UP CONFIRM|ClearCache"
  "Settings > Firmware from SD card|UP UP UP UP UP UP UP UP CONFIRM UP UP UP UP UP UP UP UP UP UP UP UP UP UP UP UP UP CONFIRM|FileBrowser"
  "Settings > Font Update|UP UP UP UP UP UP UP UP CONFIRM UP UP UP UP UP UP UP UP UP UP UP UP UP UP UP UP UP UP CONFIRM|FontUpdate"
  "Settings > Text entry (keyboard)|UP UP UP UP UP UP UP UP CONFIRM UP UP UP UP UP UP UP UP UP UP UP UP UP UP UP UP UP UP UP UP CONFIRM|KeyboardEntry"
  # About is row 24: GitHub Token, Claude API Key (669167dde) and Edge Ruler
  # (a1cd8b3cd) were added after Device owner (row 20), so its old 21 now
  # opens the GitHub Token entry.
  "Settings > About|UP UP UP UP UP UP UP UP CONFIRM UP UP UP UP UP UP UP UP UP UP UP UP UP UP UP UP UP UP UP UP UP UP UP UP CONFIRM|Colophon"
)
# Extra cases from the caller's environment, same format, one per line.
if [ -n "${PICCO_SWEEP_EXTRA:-}" ]; then
  while IFS= read -r line; do [ -n "$line" ] && CASES+=("$line"); done <<<"$PICCO_SWEEP_EXTRA"
fi

pass=0; fail=0
for c in "${CASES[@]}"; do
  IFS='|' read -r name presses act <<<"$c"
  [ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]] && continue
  dir="$WORK/$(echo "$name" | tr ' /' '__')"; mkdir -p "$dir"
  # Hold Back across the boot's routing check so it lands on Home
  # (docs/headless-qa.md), then one press every 900 ms.
  t=3500; script="200:QTAP:BACK:2500"
  for p in $presses; do script="$script;$t:$p"; t=$((t + 900)); done
  t=$((t + 2500)); tapAt=$t
  script="$script;$tapAt:TAP:$ARROW;$((t + 2500)):QUIT"
  ( cd "$dir" && CROSSPOINT_SIM_INPUT_SCRIPT="$script" \
      CROSSPOINT_SIM_SCREENSHOTS="$((tapAt - 300)):$dir/before.bmp;$((tapAt + 2000)):$dir/after.bmp" \
      SDL_VIDEODRIVER=dummy timeout 120 "$BIN" >"$dir/log" 2>&1 )
  entered=$(grep -c "Entering activity: $act\$" "$dir/log")
  # Log lines carry millis() in brackets; take what happened at or after the tap.
  after=$(awk -v t="$tapAt" '{ if (match($0, /^\[[0-9]+\]/)) { ms = substr($0, 2, RLENGTH - 2) + 0; if (ms >= t - 50) print } }' "$dir/log")
  read_as_back=$(grep -c "header back" <<<"$after")
  exited=$(grep -c "Exiting activity: $act\$" <<<"$after")
  if [ "$entered" -ge 1 ] && [ "$exited" -ge 1 ] && [ "$read_as_back" -ge 1 ]; then
    echo "PASS  $name  (tap read as header back: $read_as_back)"; pass=$((pass + 1))
  else
    why="screen never opened"
    [ "$entered" -ge 1 ] && why="arrow tap did not leave $act; the tap was read as: $(grep -o 'Picco tap at.*' <<<"$after" | head -1)"
    [ "$entered" -ge 1 ] && [ -z "$(grep -o 'Picco tap at.*' <<<"$after")" ] && why="arrow tap did not leave $act; the decoder fired NOTHING for it"
    echo "FAIL  $name  -- $why  [$dir]"; fail=$((fail + 1))
  fi
done
# HOME'S ROWS BY TOUCH, on the same empty card (no open book). Found by this
# sweep's first run (2026-11-26): a tap on Settings opened Claude, because
# with no book the theme squeezes the menu's pitch and the touch grid did not.
# Row centers as drawn on that card (480x800 logical, Lyra Six): 404 + 46 i.
ROWS=(RecentBooks FileBrowser FileManager CrossPointWebServer NoteEditor ClaudeChat OnlineFirmwareUpdate LibraryUpdate Settings)
for i in "${!ROWS[@]}"; do
  act=${ROWS[$i]}; name="Home row $i by touch"
  [ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]] && continue
  dir="$WORK/home_row_$i"; mkdir -p "$dir"
  y=$((404 + 46 * i))
  ( cd "$dir" && CROSSPOINT_SIM_INPUT_SCRIPT="200:QTAP:BACK:2500;4000:TAP:200,$y;6500:QUIT" \
      SDL_VIDEODRIVER=dummy timeout 60 "$BIN" >"$dir/log" 2>&1 )
  got=$(grep -o 'Entering activity: .*' "$dir/log" | sed -n 3p | sed 's/Entering activity: //')
  if [ "$got" = "$act" ]; then
    echo "PASS  $name -> $act"; pass=$((pass + 1))
  else
    echo "FAIL  $name -> expected $act, opened ${got:-nothing}  [$dir]"; fail=$((fail + 1))
  fi
done

echo "$pass passed, $fail failed  (logs and screenshots: $WORK)"
[ "$fail" -eq 0 ]
