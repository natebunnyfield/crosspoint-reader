# Webserver Endpoints

This document describes the HTTP, WebSocket, WebDAV, and discovery endpoints
available while CrossPoint Reader is in File Transfer mode.

- HTTP server: port 80
- WebSocket upload server: port 81
- UDP discovery listener: port 8134
- WebDAV: port 80, handled by the same HTTP server

Examples use `crosspoint.local`. If mDNS does not resolve on your network, use
the IP address shown on the device screen.

## HTTP Pages

| Method | Path | Purpose |
|--------|------|---------|
| `GET` | `/` | Home/status page |
| `GET` | `/files` | File manager page |
| `GET` | `/upload` | Plain upload form with no script, for a browser with JavaScript off or a broken page ([upload-from-blink.md](upload-from-blink.md)) |
| `GET` | `/settings` | Web settings page |
| `GET` | `/fonts` | SD-card font manager page |
| `GET` | `/js/jszip.min.js` | JavaScript asset used by the file manager |

## Device Status

### `GET /api/status`

```bash
curl http://crosspoint.local/api/status
```

Response:

```json
{
  "version": "1.0.0",
  "ip": "192.168.1.100",
  "mode": "STA",
  "rssi": -45,
  "freeHeap": 123456,
  "uptime": 3600,
  "device": "X4",
  "deviceId": "ef0001"
}
```

| Field | Type | Description |
|-------|------|-------------|
| `version` | string | Firmware version |
| `ip` | string | Device IP address |
| `mode` | string | `"STA"` for joined Wi-Fi or `"AP"` for hotspot mode |
| `rssi` | number | Wi-Fi RSSI in dBm; `0` in AP mode |
| `freeHeap` | number | Free heap in bytes |
| `uptime` | number | Seconds since boot |
| `device` | string | `"X3"` or `"X4"` hardware detection |
| `deviceId` | string | Per-unit ID: the last 3 bytes of the factory (efuse) MAC as 6 lowercase hex chars (`src/util/DeviceId.cpp`). Unlike `device` it distinguishes two units of the same model. Used to name per-device SD files (`/sleep_<deviceId>.bmp`); also shown on the device's File Transfer screen. The first 3 MAC bytes are the vendor prefix (same across Espressif chips), so the tail carries all the discriminating bits. |

## File Management

### `GET /api/files`

Lists files and folders under a directory.

```bash
curl "http://crosspoint.local/api/files?path=/Books"
```

Query parameters:

| Parameter | Required | Default | Description |
|-----------|----------|---------|-------------|
| `path` | No | `/` | Directory to list |

Response:

```json
[
  {"name":"MyBook.epub","size":1234567,"isDirectory":false,"isEpub":true},
  {"name":"Notes","size":0,"isDirectory":true,"isEpub":false}
]
```

Hidden dotfiles are omitted unless the device setting `showHiddenFiles` is
enabled. `System Volume Information` and `XTCache` are always hidden/protected.

### `GET /download`

Downloads a file from the SD card.

```bash
curl -OJ "http://crosspoint.local/download?path=/Books/MyBook.epub"
```

Query parameters:

| Parameter | Required | Description |
|-----------|----------|-------------|
| `path` | Yes | File path to download |

Protected dotfiles, `System Volume Information`, and `XTCache` cannot be
downloaded. EPUB files are served as `application/epub+zip`; other files use
`application/octet-stream`.

### `POST /upload`

Uploads a file with HTTP multipart form data.

```bash
curl -X POST -F "file=@mybook.epub" "http://crosspoint.local/upload?path=/Books"
```

Parameters:

| Parameter | Required | Default | Description |
|-----------|----------|---------|-------------|
| `path` | No | `/` | Destination directory, in the query string or as a form field placed BEFORE the file part (`-F "path=/Books" -F "file=@mybook.epub"`); on the device a `path` field after the file arrives too late and the file lands in `/` |

Successful response:

```text
File uploaded successfully: mybook.epub
```

Refusals. Each names its cause, and none of them writes anything (the
messages and codes below date from 2026-11-27; before that every refusal was a
`400`, and a request with no file part was answered `200` with the PREVIOUS
upload's name, review R-02):

| Reply | Cause |
|-------|-------|
| `400 Nothing was uploaded: the request had no file in it. ...` | No file part: `-F "file=NAME"` without the `@`, `--data-binary`, the `path` field alone, a bare `POST` |
| `400 Nothing was uploaded: no file was chosen` | A file part with an empty name (the plain form submitted with no file picked; device only, the simulator answers the line above) |
| `400 Invalid file name` | The name is `.`, `..` or contains `/` or `\` |
| `400 File already exists: <name>` | Same-named file on the card. The old file is kept (refused since `35a45f9c1`, 2026-07-15). WebDAV `PUT` replaces; see [upload-from-blink.md](upload-from-blink.md) |
| `423 Locked: the File Transfer page is uploading "<name>" right now (<n> of <m> bytes, last data <s> s ago). ...` | A WebSocket upload from the page holds that name. It lets go when it finishes, its tab closes, or it gets no data for 30 s (see "WebSocket Upload") |
| `400 Cannot upload into a protected folder` | A dot-folder, `System Volume Information` or `XTCache` in `path` |
| `400 File name has a character the card cannot store: '<c>' (not allowed: " * : < > ? \ \|)` | The card refused the name and it holds one of FAT's reserved characters. Device only: the simulator and the iOS app store on a host file system that takes them |
| `400 File name is too long: <n> characters, the card allows 255` | Over 255 UTF-16 units |
| `400 File name is not valid UTF-8, which the card cannot store` | Raw bytes that are not UTF-8 |
| `409 Folder <path> does not exist. Create it first, then upload` | The destination folder is missing (device; the simulator creates it instead) |
| `409 <path> is a file, not a folder` | `path` names a file |
| `500 Could not create the file on the card (the card may be full or failing)` | The card refused a valid name in a folder that exists |
| `400 Failed to write to SD card - disk may be full`, `400 Failed to write final data to SD card` | A write came back short |

The name, folder and card rows are worked out AFTER the card refuses the open
(`explainCreateFailure` in `CrossPointWebServer.cpp`, rules in
`FsHelpers::checkFatName`), so nothing the card would accept is turned away.

Notes:

- EPUB cache data for the uploaded path is cleared after a successful upload.
- HTTP upload uses a 4 KB write buffer before flushing to the SD card.

### `POST /mkdir`

Creates a folder.

```bash
curl -X POST -d "name=NewFolder&path=/" http://crosspoint.local/mkdir
```

Form parameters:

| Parameter | Required | Default | Description |
|-----------|----------|---------|-------------|
| `name` | Yes | - | New folder name |
| `path` | No | `/` | Parent folder |

### `POST /rename`

Renames a file.

```bash
curl -X POST -d "path=/Books/old.epub&name=new.epub" http://crosspoint.local/rename
```

Form parameters:

| Parameter | Required | Description |
|-----------|----------|-------------|
| `path` | Yes | Existing file path |
| `name` | Yes | New file name, not a path |

Only files can be renamed through this endpoint. The old EPUB cache path is
cleared before the rename.

### `POST /move`

Moves a file into an existing folder.

```bash
curl -X POST -d "path=/Books/mybook.epub&dest=/Read" http://crosspoint.local/move
```

Form parameters:

| Parameter | Required | Description |
|-----------|----------|-------------|
| `path` | Yes | Existing file path |
| `dest` | Yes | Existing destination folder |

Only files can be moved through this endpoint. The old EPUB cache path is
cleared before the move.

### `POST /delete`

Deletes one or more files or empty folders.

```bash
curl -X POST -d "path=/Books/mybook.epub" http://crosspoint.local/delete
curl -X POST -d 'paths=["/Books/old.epub","/OldFolder"]' http://crosspoint.local/delete
```

Form parameters:

| Parameter | Required | Description |
|-----------|----------|-------------|
| `path` | Yes, unless `paths` is provided | Single path to delete |
| `paths` | Yes, unless `path` is provided | JSON array of paths to delete |

Protected items cannot be deleted. Non-empty folders are rejected. EPUB cache
data for deleted files is cleared.

## Settings API

### `GET /api/settings`

Returns a streamed JSON array of editable settings. Each item contains common
fields plus type-specific fields.

```bash
curl http://crosspoint.local/api/settings
```

Example item:

```json
{
  "key": "fontSize",
  "name": "Reader Font Size",
  "category": "Reader",
  "type": "enum",
  "value": 3,
  "options": ["XXS (8pt)", "XS (10pt)", "S (12pt)", "M (14pt)", "L (16pt)", "XL (18pt)"]
}
```

`value` is always an index into `options`, never the option's text. `fontSize`
is one of the settings whose `options` are built at request time — they are the
point sizes the selected font family actually ships, so a family installed at
10/12/14 offers three options. (`fontFamily` varies the same way, from the SD
card contents.)

Types:

| Type | Extra fields |
|------|--------------|
| `toggle` | `value` (`0` or `1`) |
| `enum` | `value`, `options` |
| `value` | `value`, `min`, `max`, `step` |
| `string` | `value` |

The font-family setting includes SD-card font families when they are installed.

### `POST /api/settings`

Applies a partial settings update from a JSON object.

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"fontSize":2,"showHiddenFiles":1}' \
  http://crosspoint.local/api/settings
```

Successful response:

```text
Applied 2 setting(s)
```

## Font Management API

### `GET /api/fonts`

Lists installed SD-card font families.

```bash
curl http://crosspoint.local/api/fonts
```

Response:

```json
{
  "maxFamilies": 128,
  "families": [
    {
      "name": "Literata",
      "sizes": [12, 14, 16, 18],
      "files": [
        {"name": "Literata_12.cpfont", "size": 123456}
      ]
    }
  ]
}
```

### `POST /api/fonts/upload`

Uploads one `.cpfont` file into a family folder.

```bash
curl -X POST \
  -F "family=Literata" \
  -F "file=@Literata_12.cpfont" \
  http://crosspoint.local/api/fonts/upload
```

The handler validates the family name, `.cpfont` filename, and `CPFONT` magic
bytes before accepting the file.

Successful response:

```json
{"ok":true}
```

### `POST /api/fonts/delete`

Deletes an installed font family.

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"family":"Literata"}' \
  http://crosspoint.local/api/fonts/delete
```

Successful response:

```json
{"ok":true}
```

## Wi-Fi Credential API

### `GET /api/wifi`

Lists saved Wi-Fi networks. Passwords are never returned.

```bash
curl http://crosspoint.local/api/wifi
```

Response:

```json
[
  {
    "index": 0,
    "ssid": "HomeWiFi",
    "hasPassword": true,
    "isLastConnected": true
  }
]
```

### `POST /api/wifi`

Adds or updates a saved Wi-Fi network. Include `index` to update an existing
entry. If `password` is omitted during an update, the existing password is
preserved.

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"ssid":"HomeWiFi","password":"secret"}' \
  http://crosspoint.local/api/wifi
```

### `POST /api/wifi/delete`

Deletes a saved Wi-Fi network by index.

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"index":0}' \
  http://crosspoint.local/api/wifi/delete
```

## WebSocket Upload

### Port 81

The WebSocket path is used for fast binary uploads from the file manager.

Connection:

```text
ws://crosspoint.local:81/
```

Protocol:

1. Client sends text: `START:<filename>:<size>:<path>`
2. Server replies `READY`
3. Client sends binary chunks
4. Server sends `PROGRESS:<received>:<total>` every 64 KB or at completion
5. Server sends `DONE` when complete or `ERROR:<message>` on failure

Example session:

```text
Client -> START:mybook.epub:1234567:/Books
Server -> READY
Client -> [binary chunk]
Server -> PROGRESS:65536:1234567
...
Server -> DONE
```

Error messages include:

| Message | Cause |
|---------|-------|
| `ERROR:Upload already in progress` | A second upload was started before the first completed |
| `ERROR:Invalid START format` | Malformed START message or invalid size token |
| `ERROR:File name has a character the card cannot store: ...`, `ERROR:File name is too long: ...`, `ERROR:Folder <path> does not exist. ...`, `ERROR:Could not create the file on the card (the card may be full or failing)` | Destination file could not be opened; the same reasons as `POST /upload` (this was `ERROR:Failed to create file` until 2026-11-27) |
| `ERROR:No upload in progress` | Binary data arrived without a matching START |
| `ERROR:Upload overflow` | Client sent more bytes than declared |
| `ERROR:Write failed - disk full?` | SD write failed |
| `ERROR:Upload stalled: no data for 30 s, partial file removed` | No binary frame for 30 s with the socket still open |

Incomplete WebSocket uploads are deleted on disconnect or error.

**An upload that stalls is given up on after 30 s** (`WS_UPLOAD_STALL_MS` in
`CrossPointWebServer.cpp`, since 2026-11-27, review R-01). A page whose script
dies mid-upload, or a phone browser suspended with the tab open, keeps its
socket, and the device's WebSocket library never times out an established
connection. Before this the half-written file stayed open for as long as that
tab did: its name was refused on `PUT`, `POST /upload` and `DELETE`, and no
other page could `START`. The clock restarts after each frame is WRITTEN, so a
slow card is not counted as a silent sender. While the upload holds a name,
the other routes answer `423` with the byte count and the 30 s rule. The
simulator reads `CROSSPOINT_SIM_WS_STALL_MS` to shorten the wait for tests.

## WebDAV

The same HTTP server registers a WebDAV-compatible handler for file manager clients.

Supported methods:

```text
OPTIONS, GET, HEAD, PUT, DELETE, PROPFIND, MKCOL, MOVE, COPY, LOCK, UNLOCK
```

Notes:

- `PUT` writes to a temporary `.davtmp` file first, then renames it into place.
  The temp file is `<name>.davtmp`, or `<8 hex digits>.davtmp` in the same
  folder when the name is too long to take the suffix (249 characters and up;
  such names failed outright until 2026-11-27, review R-11).
- `PUT` answers `201` for a new file and `204` for a replaced one. Its
  refusals, each with a plain-text reason (until 2026-11-27 every one of them
  was `500 Write failed - incomplete upload or disk full`):

  | Status | Reason |
  |--------|--------|
  | `405` | `<path> is a folder. To upload into it, end the URL with a slash ...` (`curl -T x.epub http://host/books`, no trailing slash) |
  | `409` | `Folder <path> does not exist. Create it first (MKCOL), then upload`, or `<path> is a file, not a folder` (a URL that runs on past a file name) |
  | `423` | `Locked: the File Transfer page is uploading "<name>" right now ...` (see "WebSocket Upload") |
  | `400` | `File name is too long: <n> characters, the card allows 255`, refused before anything is written |
  | `400` | `File name has a character the card cannot store: ...` or `File name is not valid UTF-8 ...`, when the card refuses the name (the reserved-character case is device only) |
  | `400` | `Nothing was uploaded: the PUT had no plain body. ...` (a multipart `PUT`, which the device's parser never hands to the WebDAV handler) |
  | `403` | `Forbidden` (a protected path) |
  | `500` | `Could not create the file on the card (the card may be full or failing)`, or `Write to the card failed after <n> bytes (the card may be full). The upload was discarded` |
- Dot-paths (`/.crosspoint`, `/.fonts`, …) are fully listed and writable over
  WebDAV — unlike the HTML file browser — so sync tools such as `rclone` can
  mirror an entire card, settings and fonts included. See
  [webserver.md](webserver.md) for the rclone recipe and
  `scripts/sync-card.sh` for a ready-made wrapper.
- Only `System Volume Information` and `XTCache` are protected. PROPFIND also
  hides them, so sync clients neither see nor try to prune them.
- Every file's `getlastmodified` is a fixed fake date (no RTC guarantee), so
  rclone must compare by size (`--size-only`) or content (`--ignore-times`).
- `LOCK` and `UNLOCK` are accepted for client compatibility only. The server
  does not implement full WebDAV Class 2 locking semantics such as persistent
  locks or lock discovery.

## UDP Discovery

The server listens on UDP port `8134`. When it receives the text payload
`hello`, it replies to the sender with:

```text
crosspoint (on <hostname>);81
```

The final field is the WebSocket upload port.

## Network Modes

### Station Mode (STA)

- Device joins an existing 2.4 GHz Wi-Fi network.
- `crosspoint.local` is advertised with mDNS when available.
- `/api/status` returns `"mode": "STA"` and RSSI in dBm.

### Access Point Mode (AP)

- Device creates an open hotspot named `CrossPoint-Reader`.
- The device shows a Wi-Fi QR code and URL QR code.
- The fallback IP is typically `192.168.4.1`.
- `/api/status` returns `"mode": "AP"` and `"rssi": 0`.
