#!/bin/zsh
# Lag gate: fails when a scripted reading session regresses one of the
# findings in docs/lag-reduction-plan-2026-11-07.md. Every one of them was
# invisible until it was counted, and each would come back just as silently.
#
# Needs the simulator built with the [lat] trace (../crosspoint-simulator
# carries it since 586f03db), in its own build dir:
#   PLATFORMIO_BUILD_DIR=.pio/build-sim PLATFORMIO_BUILD_FLAGS="-O2 -g" pio run -e simulator
# then: tools/latency/gate.sh
#
# The card is generated (tools/make_perf_book.py, seeded) so the gate needs
# nothing outside the repo, and the host panel is pinned to LIGHT: the
# simulator's desktop default is its dark page, which the firmware sees as an
# inverted panel and answers with its dark-mode paths.
set -e
here=${0:A:h}; repo=${here:h:h}
work=$(mktemp -d -t lat-gate)
trap 'rm -rf $work' EXIT
mkdir -p $work/card/books $work/card/.crosspoint
python3 $repo/tools/make_perf_book.py $work/card/books/gate.epub 3 60 >/dev/null
print -r -- '{"openEpubPath":"/books/gate.epub","showBootScreen":false}' > $work/card/.crosspoint/state.json
print -r -- '{"books":[{"path":"/books/gate.epub","title":"Gate","author":"","coverBmpPath":""}]}' > $work/card/.crosspoint/recent.json
print -r -- '{ "darkMode": 0 }' > $work/settings.json

# 6 slow turns (each rests long enough for the deferred gray pass), 8 fast
# turns (no gray pass may run between them), Back to Home, two Home moves,
# Confirm into Settings, two row moves, Back, Back.
s=""; t=3000
for i in 1 2 3 4 5 6; do s="${s}${t}:RIGHT:150;"; t=$((t+1400)); done
for i in 1 2 3 4 5 6 7 8; do s="${s}${t}:RIGHT:120;"; t=$((t+330)); done
t=$((t+1500)); s="${s}${t}:BACK:150;"
t=$((t+1500)); s="${s}${t}:DOWN:150;"; t=$((t+1000)); s="${s}${t}:DOWN:150;"
t=$((t+1000)); s="${s}${t}:CONFIRM:150;"
t=$((t+1500)); s="${s}${t}:RIGHT:150;"; t=$((t+1000)); s="${s}${t}:RIGHT:150;"
t=$((t+1000)); s="${s}${t}:BACK:150;"; t=$((t+1500)); s="${s}${t}:BACK:150;"
t=$((t+2000)); s="${s}${t}:QUIT"

PROGRAM=${PROGRAM:-$repo/.pio/build-sim/simulator/program} \
  $here/run_scenario.sh $work/card $work/run.log "$s" > $work/summary.txt
python3 $here/gate_check.py $work/run.log
