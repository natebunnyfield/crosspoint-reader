#!/bin/bash
# run.sh <binary> <fonts|library> <tag> [extra script]
S="${UPD_WORK:?set UPD_WORK to a scratch dir}"; HERE=$(cd "$(dirname "$0")" && pwd); BIN=$1; MODE=$2; TAG=$3
"$HERE/mkcard.sh"
R=""; t=3000; for i in 1 2 3 4 5 6 7 8; do R="$R$t:RIGHT;"; t=$((t+900)); done
R="${R}10500:CONFIRM;"; t=12000; for i in 1 2 3 4 5 6 7; do R="$R$t:RIGHT;"; t=$((t+900)); done
R="${R}18500:CONFIRM;23000:CONFIRM;"
if [ "$MODE" = fonts ]; then t=25000; for i in 1 2 3 4 5; do R="$R$t:RIGHT;"; t=$((t+900)); done; R="${R}30000:CONFIRM"
else R="${R}25000:BACK;27500:LEFT;30000:CONFIRM"; fi
R="$R$4"
SH=""; for t in $(seq 30500 1000 75500); do SH="$SH$t:$S/shots/$TAG-$t.bmp;"; done
mkdir -p "$S/shots"; rm -f "$S/shots/$TAG-"*
cd "$S/card" && SDL_VIDEODRIVER=dummy CROSSPOINT_SIM_LOG_PRESENTS=1 CROSSPOINT_SIM_HTTP_MOCK_ROOT="$S/mock" \
  CROSSPOINT_SIM_GITHUB_TOKEN=mock-not-a-token CROSSPOINT_SIM_INPUT_SCRIPT="$R" CROSSPOINT_SIM_SCREENSHOTS="$SH" \
  timeout 80 "$BIN" > "$S/$TAG.log" 2>&1
echo "exit $?"
