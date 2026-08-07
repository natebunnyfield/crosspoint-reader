#!/usr/bin/env bash
# SPIKE (branch spike/ble-editor) — build/upload helper.
#
# The NimBLE host is already compiled into the stock arduino-esp32 libbt.a for
# the C3 (CONFIG_BT_NIMBLE_ENABLED=y, ROLE_CENTRAL=y, GATT_CLIENT=y in the
# framework sdkconfig), but PlatformIO does not put the bt/ include dirs on the
# compile line for *project* sources — 325 -I flags reach a project TU and none
# of them is nimble. Rather than edit platformio.ini (which wipes EVERY env's
# build dir, per CLAUDE.md), the paths are injected through
# PLATFORMIO_BUILD_FLAGS. Keep this value constant across the session: it feeds
# project.checksum, so changing it triggers a full rebuild.
#
#   ./spike-build.sh              # build
#   ./spike-build.sh upload       # build + flash the USB-connected X4
set -euo pipefail

PKG="$HOME/.platformio/packages/framework-arduinoespressif32-libs/esp32c3/include"
NIMBLE_INCLUDES=(
  "$PKG/bt/host/nimble/nimble/nimble/host/include"
  "$PKG/bt/host/nimble/nimble/nimble/include"
  "$PKG/bt/host/nimble/nimble/nimble/transport/include"
  "$PKG/bt/host/nimble/nimble/porting/nimble/include"
  "$PKG/bt/host/nimble/nimble/porting/npl/freertos/include"
  "$PKG/bt/host/nimble/nimble/nimble/host/util/include"
  "$PKG/bt/host/nimble/port/include"
  "$PKG/bt/host/nimble/esp-hci/include"
  "$PKG/bt/include/esp32c3/include"
  "$PKG/bt/common/osi/include"
)

FLAGS=""
for d in "${NIMBLE_INCLUDES[@]}"; do FLAGS="$FLAGS -I$d"; done
export PLATFORMIO_BUILD_FLAGS="$FLAGS"

# `|| true`: with set -e, a missing device would otherwise abort a plain build.
PORT="${SPIKE_PORT:-$(ls /dev/cu.usbmodem* 2>/dev/null | head -1 || true)}"

if [ "${1:-}" = "upload" ]; then
  if [ -z "$PORT" ]; then
    echo "No /dev/cu.usbmodem* found — is the X4 plugged in and awake?" >&2
    exit 1
  fi
  # Auto-detect picks /dev/cu.BeatsStudioPro on this machine; always be explicit.
  exec pio run -e default -t upload --upload-port "$PORT"
fi

exec pio run -e default
