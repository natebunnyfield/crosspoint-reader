#!/bin/zsh
# Scripted simulator run with the [lat] trace on, summarized per press.
# Needs a simulator built with docs/data/sim-latency-trace-2026-11-07.patch
# applied to ../crosspoint-simulator, at -O2:
#   PLATFORMIO_BUILD_FLAGS="-O2 -g" pio run -e simulator
# usage: tools/latency/run_scenario.sh <card-dir> <log-out> '<input script>'
#   e.g. '3000:RIGHT:150;4000:RIGHT:150;5000:QUIT'   (hold >=150 ms)
# The card is MUTATED (progress, caches): pass a fresh copy every run.
set -e
card=$1; log=$2; script=$3
here=${0:A:h}; repo=${here:h:h}
env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy CROSSPOINT_SIM_TUBE_OFF=1 \
  CROSSPOINT_SIM_SD=$card CROSSPOINT_SIM_LOG_LATENCY=1 CROSSPOINT_SIM_INPUT_SCRIPT="$script" \
  timeout 300 $repo/.pio/build/simulator/program > $log 2>&1 || true
python3 $here/summarize.py $log
