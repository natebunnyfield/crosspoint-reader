#!/usr/bin/env bash
# Mirror a curated SD-card tree onto a CrossPoint reader over Wi-Fi (WebDAV).
#
# The device must be in File Transfer mode (Home -> File Transfer). This makes
# the card match SOURCE_DIR exactly — files missing from SOURCE_DIR are DELETED
# from the card, including dot-paths like /.fonts and /.crosspoint.
#
# Synced like everything else: /.crosspoint/wifi.json (saved Wi-Fi networks)
# and /.crosspoint/recent.json (recents list — the firmware auto-prunes entries
# whose book is gone, so a curated list that drops books is safe). If the
# source tree is missing either file, that file is left alone on the device
# instead of being deleted, so a fresh curation can't wipe Wi-Fi creds.
#
# What is never touched:
#   - System Volume Information, XTCache (server hides them from WebDAV)
#   - /.crosspoint/state.json, sleep_frame.bin (device runtime state)
#   - /.crosspoint/epub_*|txt_*|xtc_* layout caches (hashes are host-specific;
#     the device rebuilds its own, and prunes them itself when a book is deleted)
#
# Mac litter (.DS_Store, ._* AppleDouble stubs) is never pushed AND is deleted
# from the card in a cleanup pass before the mirror.
#
# settings.json is force-pushed in a second pass: the routine pass compares by
# size only (the server reports no real modtimes), and a settings edit can keep
# the byte count identical.
#
# Usage:
#   scripts/sync-card.sh <source-dir> [device-host[:port]] [--dry-run] [--full]
#
#   <source-dir>   curated card root (e.g. fs_ or a copy of the iOS app's Documents)
#   device-host    default: crosspoint.local (device announces itself via mDNS)
#   --dry-run      show what would change without touching the card
#   --full         recopy everything regardless of size match (catches same-size
#                  content changes, e.g. a corrupted or re-hinted .cpfont)
#
# After the sync finishes, press Back on the device: leaving File Transfer
# reboots it, and the reboot picks up the synced settings and fonts.

set -euo pipefail

SOURCE_DIR=""
HOST="crosspoint.local"
EXTRA_FLAGS=()

for arg in "$@"; do
  case "$arg" in
    --dry-run) EXTRA_FLAGS+=(--dry-run) ;;
    --full) EXTRA_FLAGS+=(--ignore-times --ignore-size) ;;
    -h|--help)
      sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      if [[ -z "$SOURCE_DIR" ]]; then
        SOURCE_DIR="$arg"
      else
        HOST="$arg"
      fi
      ;;
  esac
done

if [[ -z "$SOURCE_DIR" || ! -d "$SOURCE_DIR" ]]; then
  echo "error: source dir missing or not a directory: '$SOURCE_DIR'" >&2
  echo "usage: $0 <source-dir> [device-host[:port]] [--dry-run] [--full]" >&2
  exit 1
fi

if ! command -v rclone >/dev/null; then
  echo "error: rclone not installed (brew install rclone)" >&2
  exit 1
fi

URL="http://$HOST"

# Protect wifi.json / recent.json only when the source doesn't carry them —
# mirroring their absence would wipe saved Wi-Fi networks / the recents list.
GUARD_FILTERS=()
for f in wifi.json recent.json; do
  if [[ ! -f "$SOURCE_DIR/.crosspoint/$f" ]]; then
    echo "note: source has no .crosspoint/$f — leaving the device's copy alone"
    GUARD_FILTERS+=(--filter "- /.crosspoint/$f")
  fi
done

echo "Cleaning Mac litter (.DS_Store, ._*) off the card"
rclone delete :webdav: \
  --webdav-url "$URL" \
  --transfers 1 --checkers 1 \
  --include '.DS_Store' \
  --include '._*' \
  ${EXTRA_FLAGS[@]+"${EXTRA_FLAGS[@]}"} || true

echo "Mirroring '$SOURCE_DIR' -> $URL (deletes files not in source)"

# One connection only: the device's HTTP server is single-threaded.
# --size-only: the server reports a fixed fake modtime for every file.
rclone sync "$SOURCE_DIR" :webdav: \
  --webdav-url "$URL" \
  --size-only \
  --transfers 1 --checkers 1 \
  ${GUARD_FILTERS[@]+"${GUARD_FILTERS[@]}"} \
  --filter '- /.crosspoint/state.json' \
  --filter '- /.crosspoint/sleep_frame.bin' \
  --filter '- /.crosspoint/epub_*/**' \
  --filter '- /.crosspoint/txt_*/**' \
  --filter '- /.crosspoint/xtc_*/**' \
  --filter '- .DS_Store' \
  --filter '- ._*' \
  -P ${EXTRA_FLAGS[@]+"${EXTRA_FLAGS[@]}"}

if [[ -f "$SOURCE_DIR/.crosspoint/settings.json" ]]; then
  echo "Force-pushing settings.json (size compare is not safe for it)"
  rclone copyto "$SOURCE_DIR/.crosspoint/settings.json" \
    :webdav:/.crosspoint/settings.json \
    --webdav-url "$URL" \
    --ignore-times --ignore-size ${EXTRA_FLAGS[@]+"${EXTRA_FLAGS[@]}"}
fi

echo "Done. Press Back on the device to leave File Transfer — it reboots and"
echo "loads the synced settings and fonts."
