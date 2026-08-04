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
# Everything mutable lives under fs_/.crosspoint: settings.json, one progress.bin
# per book, the section caches and the recent-books list. ALL of it is restored
# before every case — settings alone is not enough. The reader saves its position
# on exit, so without the progress files the reader shots drift a page per sweep;
# without the recent-books list the home covers reorder. Both look exactly like a
# regression and are not one.
STATE=fs_/.crosspoint
BASELINE="$OUT/state.baseline"
mkdir -p "$OUT"
# Snapshot on first use; later sweeps reuse it, so a before/after pair starts
# from identical device state.
[ -d "$BASELINE" ] || cp -R "$STATE" "$BASELINE"

run() { # name  input-script  screenshot-script
  rm -rf "$STATE" && cp -R "$BASELINE" "$STATE"
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

# Settings is ONE tab, so no tab cycling: the list is focused on entry and the
# first row is Text Settings. Do not add blind DOWN/ENTER steps here — they walk
# into the Screen Margin popup and CHANGE the margin, which correctly
# invalidates every cached section and fills the log with
# "[SCT] Deserialization failed", looking exactly like a regression.
run settings \
  '3000:DOWN;3400:DOWN;3800:DOWN;4200:DOWN;5000:ENTER;16000:QUIT' \
  "8000:$OUT/05-settings.bmp;12000:$OUT/06-settings-scrolled.bmp"

run text-settings \
  '3000:DOWN;3400:DOWN;3800:DOWN;4200:DOWN;5000:ENTER;7000:ENTER;20000:QUIT' \
  "11000:$OUT/07-text-settings.bmp;15000:$OUT/08-text-settings-2.bmp"

# Back from Home resumes the most recent book; Confirm in the reader opens
# chapter select.
run reader-and-chapters \
  '3000:BACK;14000:ENTER;30000:QUIT' \
  "11000:$OUT/09-reader.bmp;20000:$OUT/10-chapter-select.bmp"

run reader-paging \
  '3000:BACK;14000:RIGHT;18000:RIGHT;30000:QUIT' \
  "24000:$OUT/11-reader-paged.bmp"

rm -rf "$STATE" && cp -R "$BASELINE" "$STATE"
echo "  wrote $(ls "$OUT"/*.png 2>/dev/null | wc -l | tr -d ' ') screenshots to $OUT"
