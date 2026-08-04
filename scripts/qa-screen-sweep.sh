#!/usr/bin/env bash
# Screenshot sweep across the reader's main screens, for before/after UI QA.
#
#   pio run -e simulator                       # build the BEFORE state
#   scripts/qa-screen-sweep.sh qa/before
#   ...make the change, rebuild...
#   scripts/qa-screen-sweep.sh qa/after
#   for f in qa/after/*.png; do cmp -s "$f" "qa/before/$(basename "$f")" \
#     && echo "same $(basename "$f")" || echo "DIFF $(basename "$f")"; done
#
# Every case is its own simulator process started from a restored settings.json
# AND a restored progress.bin, so runs never inherit each other's navigation or
# reading position. Without the progress restore the two reader shots drift one
# page per sweep and every comparison reports a false regression.
#
# Output is deterministic: two sweeps of the same binary produce byte-identical
# PNGs, which is what makes "identical" mean something.
set -u
OUT="${1:?usage: sweep.sh <output-dir>}"
BIN=.pio/build/simulator/program
SETTINGS=fs_/.crosspoint/settings.json
BASELINE="$OUT/settings.baseline.json"
# Reading position lives in progress.bin, not settings.json, and the reader
# saves it on exit — so without restoring this too the reader screenshots drift
# a page per sweep and compare false-negative against an earlier run.
PROGRESS="$(find fs_/.crosspoint -name progress.bin | head -1)"
PROGRESS_BASELINE="$OUT/progress.baseline.bin"
mkdir -p "$OUT"
# Snapshot the current state on first use; later sweeps reuse it so a
# before/after pair starts from identical device state.
[ -f "$BASELINE" ] || cp "$SETTINGS" "$BASELINE"
[ -n "${PROGRESS:-}" ] && [ ! -f "$PROGRESS_BASELINE" ] && cp "$PROGRESS" "$PROGRESS_BASELINE"

run() { # name  input-script  screenshot-script
  cp "$BASELINE" "$SETTINGS"
  [ -n "${PROGRESS:-}" ] && cp "$PROGRESS_BASELINE" "$PROGRESS"
  CROSSPOINT_SIM_INPUT_SCRIPT="$2" \
  CROSSPOINT_SIM_SCREENSHOTS="$3" \
  SDL_VIDEODRIVER=dummy "$BIN" >"$OUT/$1.log" 2>&1
  for f in "$OUT"/*.bmp; do
    [ -e "$f" ] || continue
    sips -s format png "$f" --out "${f%.bmp}.png" >/dev/null 2>&1 && rm -f "$f"
  done
  printf '  %-22s errors=%s\n' "$1" "$(grep -cE '\[ERR\]' "$OUT/$1.log")"
}

# Home menu order with one recent book: 0 cover, 1 Browse Files, 2 Recent Books,
# 3 File Transfer, 4 Settings.
run home-and-menu \
  '3000:DOWN;3400:DOWN;3800:DOWN;4200:DOWN;12000:QUIT' \
  "2000:$OUT/01-home-cover.bmp;5500:$OUT/02-home-menu.bmp"

run file-browser \
  '3000:DOWN;4000:ENTER;12000:QUIT' \
  "8000:$OUT/03-file-browser.bmp"

run recent-books \
  '3000:DOWN;3400:DOWN;4200:ENTER;12000:QUIT' \
  "8000:$OUT/04-recent-books.bmp"

run settings-tabs \
  '3000:DOWN;3400:DOWN;3800:DOWN;4200:DOWN;5000:ENTER;8000:ENTER;16000:QUIT' \
  "7000:$OUT/05-settings-reader.bmp;11000:$OUT/06-settings-system.bmp"

run text-settings \
  '3000:DOWN;3400:DOWN;3800:DOWN;4200:DOWN;5000:ENTER;7000:DOWN;7600:ENTER;20000:QUIT' \
  "11000:$OUT/07-text-settings.bmp;15000:$OUT/08-text-settings-2.bmp"

# Back from Home resumes the most recent book; Confirm in the reader opens
# chapter select.
run reader-and-chapters \
  '3000:BACK;14000:ENTER;30000:QUIT' \
  "11000:$OUT/09-reader.bmp;20000:$OUT/10-chapter-select.bmp"

run reader-paging \
  '3000:BACK;14000:RIGHT;18000:RIGHT;30000:QUIT' \
  "24000:$OUT/11-reader-paged.bmp"

cp "$BASELINE" "$SETTINGS"
[ -n "${PROGRESS:-}" ] && cp "$PROGRESS_BASELINE" "$PROGRESS"
echo "  wrote $(ls "$OUT"/*.png 2>/dev/null | wc -l | tr -d ' ') screenshots to $OUT"
