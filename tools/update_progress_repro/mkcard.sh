#!/bin/bash
# Fresh scratch card: Edgar installed (no ledger -> hashed), one book on the card, a saved open network.
S="${UPD_WORK:?set UPD_WORK to a scratch dir}"; HERE=$(cd "$(dirname "$0")" && pwd)
rm -rf "$S/card"; mkdir -p "$S/card/fs_/fonts/Edgar" "$S/card/fs_/books" "$S/card/fs_/.crosspoint"
cp ~/src/crosspoint-reader/fs_/fonts/Edgar/*.cpfont "$S/card/fs_/fonts/Edgar/"
cp ~/src/crosspoint-reader/fs_/ai-engineering-from-zero.epub "$S/card/fs_/books/"
echo '{"lastConnectedSsid":"Simulator WiFi (fake)","credentials":[{"ssid":"Simulator WiFi (fake)","password_obf":"","password_len":0,"password_crc32":0}]}' > "$S/card/fs_/.crosspoint/wifi.json"
python3 - "$S" <<PY
import json,sys
p=sys.argv[1]+"/card/fs_/.crosspoint/state.json"
json.dump({"readerActivityLoadCount":1},open(p,"w"))
PY
