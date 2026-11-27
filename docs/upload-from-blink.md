# Uploading from Blink (or any curl), with no web page

Written 2026-11-27, against firmware `4998d84da` plus the commits that add this
file. Owner, the same day: *"be sure to make it possible for blink to upload a
file no matter what bugs happen in file transfer screen"*.

Blink Shell on an iPhone or iPad has `curl`, `ssh`, `mosh`, `scp` and `sftp`
clients, and no browser. The device runs no ssh server, so **curl is the
path**. For months the File Manager page's script did not parse (B-080) and
every upload made through the page was silently dead. The two uploads below
need no page and no script at all: they talk to the server directly.

## The commands

Run them from the folder in Blink that holds the file. In every command,
`crosspoint.local` can be replaced by the IP address the device shows (see
"Where the address is").

### The BOOX Picco, over Wi-Fi

Open **File Transfer** on the Picco, pick the network, wait for the screen with
the QR code. Then in Blink:

```sh
# 1. Is it there? Prints a line of JSON with "ip" in it.
curl http://crosspoint.local/api/status

# 2. Upload into /books. Keeps the file's name. Replaces a file of the same name.
curl -T "My Book.epub" http://crosspoint.local/books/

# Same, printing 201 (new file) or 204 (replaced) so you can see it worked.
curl -w '%{http_code}\n' -T "My Book.epub" http://crosspoint.local/books/

# Several files in one command (curl's own list, no shell loop needed).
curl -T "{a.epub,b.epub}" http://crosspoint.local/books/

# 3. Into a NEW folder: make it first, then upload. %20 is a space.
curl -X MKCOL "http://crosspoint.local/New%20Shelf/"
curl -T "My Book.epub" "http://crosspoint.local/New%20Shelf/"

# 4. The other way in: a form post, the same request the web page makes.
#    Refuses to replace an existing file. The folder goes FIRST.
curl -F "path=/books" -F "file=@My Book.epub" http://crosspoint.local/upload
curl -F "file=@My Book.epub" "http://crosspoint.local/upload?path=/New%20Shelf"

# 5. Check what landed, and remove a mistake.
curl "http://crosspoint.local/api/files?path=/books"
curl -X DELETE "http://crosspoint.local/books/My%20Book.epub"
```

By IP, the same commands with the address the screen shows, for example:

```sh
curl -T "My Book.epub" http://192.168.1.42/books/
curl -F "path=/books" -F "file=@My Book.epub" http://192.168.1.42/upload
```

In **Hotspot** mode the Picco makes its own open network, `CrossPoint-Reader`.
Join it from the phone, then use the address on the screen, usually
`192.168.4.1`. mDNS is started in Hotspot mode as well
(`CrossPointWebServerActivity.cpp` `startAccessPoint`), so `crosspoint.local`
should resolve there too; not tried on a device.

### The CrossPoint Picco iOS app

The app's File Transfer runs the same server on **port 8080** (8081 is its
WebSocket, which the page uses and curl does not), listening on every interface
of the phone. The screen paints the address with the port, for example
`http://192.168.1.57:8080/`.

```sh
curl http://192.168.1.57:8080/api/status
curl -T "My Book.epub" http://192.168.1.57:8080/books/
curl -X MKCOL "http://192.168.1.57:8080/New%20Shelf/"
curl -F "path=/books" -F "file=@My Book.epub" http://192.168.1.57:8080/upload
```

Two limits that are the platform's, not the firmware's:

- **`crosspoint.local` never reaches the app.** iOS lets an app publish a
  Bonjour *service*, not claim a hostname (crosspoint-simulator
  `ios/WIFI.md`, finding 6). Use the IP and `:8080`.
- **The app must stay in front for the whole upload.** iOS suspends a
  background app and its socket stops accepting (`ios/WIFI.md`, "Known
  limits"). So Blink on the SAME iPhone cannot reach it: bringing Blink to the
  front suspends the app. Run Blink on another device on the same Wi-Fi. On an
  iPad, Split View or Stage Manager with both apps on screen may work; that is
  untested.

## The rules each form follows

Measured on the desktop Picco simulator unless marked otherwise.

| | `curl -T` (WebDAV PUT) | `curl -F` (POST /upload) |
|---|---|---|
| File name | curl appends the local name to a URL ending in `/`, percent-encoding spaces and accents (curl 8.7.1 and 8.23.0 both did) | the name curl puts in the form; `-F "file=@x.epub;filename=Other.epub"` renames |
| Existing file | **replaced**, answers `204` | **refused**, `400 File already exists: NAME`, old bytes kept |
| Missing folder | **refused** (`500`, see below). Make it with `MKCOL` first | **refused on the Picco** (`400 Failed to create file on SD card`, read from source): SdFat opens each parent read-only and fails on a missing one (SdFat `FatFile.cpp:477`). The simulator creates it instead, so a desktop test passes where the Picco fails |
| Folder | in the URL path, `%20` for a space | `-F "path=..."` **before** `-F "file=@..."`, or `?path=` in the URL |
| Success | `201` new, `204` replaced, no body | `200 File uploaded successfully: NAME` |

Four traps, each with a misleading symptom:

1. **Leave the trailing slash on.** `curl -T x.epub http://crosspoint.local/books`
   PUTs onto the folder itself and the device answers
   `500 Write failed - incomplete upload or disk full`. Nothing is wrong with
   the card; add the `/`. The same `500` comes back for a folder that does not
   exist yet.
2. **With `-F`, the folder field must come first.** The device's parser gives
   the upload handler only the fields it has read so far (`WebServer::arg()`
   checks `_postArgs` while the file streams, arduino-esp32 3.3.7
   `Parsing.cpp` `_parseForm`). `-F file=@x -F path=/books` therefore lands in
   `/` on the Picco. The simulator reads the whole form first and gets it right,
   so only the device shows this.
3. **Never pipe into `-T`.** `cat x | curl -T - URL` sends a chunked body. The
   device's WebServer has no chunked request support (`Parsing.cpp:111-112`
   sets the length to 0 and nothing reads chunks), so it would write an EMPTY
   file and answer `201`. Read from source, not run on a device. The simulator
   refuses chunked bodies with `501`. Always give curl a file name.
4. **A big file pauses for about a second first.** curl asks for
   `100 Continue` on a large body and the server never sends it, so curl waits
   ~1 s and then sends anyway (measured: 5 MB PUT, `Expect: 100-continue`,
   1.05 s on loopback). Harmless.

The WebSocket upload on port 81 (8081 in the app) is the page's fast path. It
needs a WebSocket client speaking `START:<name>:<size>:<path>` and then binary
frames (`docs/webserver-endpoints.md`), which Blink does not have. It is not a
Blink route.

## When `crosspoint.local` does not resolve

1. **Use the IP the device shows.** On the Picco's File Transfer screen (joined
   network) it is the large `http://<ip>/` line under the QR code, with
   `or http://crosspoint.local/` in small type beneath it, and the QR code
   there encodes that same IP URL. In Hotspot mode it is the `or http://<ip>/`
   line beside the second QR code (that QR code encodes `crosspoint.local`).
2. **Give Blink the Local Network permission.** iOS blocks an app from reaching
   devices on the local network until it is allowed (Settings > Privacy &
   Security > Local Network > Blink). Without it a LAN address times out or
   fails as unreachable even though Safari reaches it. Platform behavior, not
   tested on a phone here.
3. **Same network, no isolation.** A guest network or "AP isolation" on the
   router stops one device reaching another. Hotspot mode on the Picco sidesteps
   the router entirely.
4. **Check with one request** before uploading:
   `curl -m 5 http://<ip>/api/status`. A timeout means the network path; a
   reply means the upload commands will work.

## For a browser with no working script

`http://crosspoint.local/upload` (or `http://<ip>/upload`, or
`http://<phone-ip>:8080/upload` for the app) serves a plain HTML form: a folder
field, a file picker and an Upload button. No script on it, so it works with
JavaScript off and when the File Manager's script is broken. It posts to the
same `/upload` as `curl -F`, so the same rules hold (the folder must exist, a
same-named file is not replaced). The File Manager carries a static line under
its buttons, "Upload not working? Basic upload page", which shows whether or not
the page's script runs.

The form lives in `CrossPointWebServer.cpp` as a literal (`UPLOAD_FORM_HTML`),
not in `src/network/html/`, so it never passes through
`scripts/build_html.py`: that minifier is what broke the page in B-080.

Its one cost: a file at the card's root named exactly `upload` (no extension)
can no longer be fetched with a WebDAV `GET /upload`, the same as a root file
named `files`, `settings` or `fonts` before it. It still lists, and
`/download?path=/upload` still serves it.

## Does the File Transfer SCREEN have to work?

Audit of 2026-11-27, read against the source; the question was whether anything
the device's screen does can stop the server answering.

**Mostly no, by construction.** Requests are served on the main loop task:
`CrossPointWebServerActivity::loop()` calls `webServer->handleClient()` in a
time-bounded batch (`CrossPointWebServerActivity.cpp:476`), reached from
`ActivityManager::loop()` (`ActivityManager.cpp:87`) with no render lock held.
The screen is drawn on a separate task, `ActivityManager::renderTaskLoop()`,
which takes the render lock and calls `render()` (`ActivityManager.cpp:56-59`).
The main loop never waits for a render in this state, so a slow or stuck draw
does not stop requests. A panel BUSY wait gives up after 30 s
(`freeink-sdk/libs/display/FreeInkDisplay/src/bus/EpdBus.cpp:225`), so even a
hung panel ends its render. No request handler
touches the renderer or the activity manager (checked by grep over
`CrossPointWebServer.cpp`, `WebDAVHandler.cpp`, `BookCacheUtils.cpp`).
In the simulator and the iOS app the shape is the same: the shim's accept thread
parks each request and `WebServer::handleClient()` runs it on the main thread
(`crosspoint-simulator/src/WebServer.cpp:884-916`), and the firmware's render
task is its own thread.

What CAN still pause or stop serving, none of it changed here:

| What | Effect | Where |
|---|---|---|
| A short Power press with Power set to Force Refresh | the main loop takes the render lock BLOCKING; if a render is mid-refresh, serving pauses until it ends (at most the 30 s BUSY cap) | `main.cpp:1519-1525` |
| Leaving File Transfer (Back, the Home gesture, 5 minutes of lost Wi-Fi) | the server stops and the device restarts, by design | `CrossPointWebServerActivity.cpp:419-422`, `:492-493` |
| A `.bin` uploaded into `/firmware/` on an X3/X4 | the confirm screen is pushed over File Transfer, whose `loop()` (and so `handleClient()`) does not run until it is answered. **Not on the Picco or the app**: both build with `CROSSPOINT_NO_DEVICE_FLASH` | `CrossPointWebServerActivity.cpp:391` |
| The Picco's idle panel power-off | runs on the main loop, but only with the render lock already free (try-lock), costs one power-off command | `main.cpp:1558` |
| A crash in `render()` | reboots the device; the server goes with it. Nothing seen, nothing to fix without a report | — |
| The iOS app going to the background | iOS suspends it | `ios/WIFI.md` |

No code change was needed for the screen question. The one structural fix that
would make serving fully independent of the activity (a server task of its own)
is not the smallest change and was not made.

## How this was proven, and what was not

Desktop Picco simulator (`pio run -e simulator_picco`), a scratch card per run,
`CROSSPOINT_SIM_HTTP_PORT=18437`, navigated to File Transfer headlessly
(`200:QTAP:BACK:2500;4000:UP;4900:UP;5800:UP;6800:CONFIRM;8000:CONFIRM;10000:CONFIRM`
with `CROSSPOINT_SIM_WIFI_NETWORKS='Alpha:-40:open'`; on the Picco the rocker's
upper end, `UP`, is next-item). Every byte on the card was checked by md5.

- `curl -T`: a 5 MB `Café Book.epub` into `/books/` (201), `MKCOL /New Shelf/`
  then a file into it (201), a replacement over an existing file (204, new
  bytes), two files in one `-T "{a,b}"`.
- `curl -F`: 5 MB into `?path=/New%20Shelf`, the folder as a form field
  first, no folder (lands in `/`), a second upload of the same name (400, old
  bytes kept).
- Every command in "The commands" above, with only host and port changed.
- The form: Chrome headless with scripts disabled, driven over DevTools (file
  chosen, folder typed, button clicked): `Ñandú Form Book.epub` landed in
  `/books/Shelf` with matching md5, and the reply page read correctly once the
  `/upload` reply declared UTF-8. The File Manager at 390 px wide with the new
  line: no horizontal scroll, with and without its script.
- Gate: `crosspoint-simulator/tests/test_blink_upload.sh`, in `tests/run_all.sh`
  (`tests/run_all.sh -k blink_upload`), about 12 s.
- `pio run -e picco` and `pio run -e default` build.

**Unproven:** nothing here ran on a real Picco or a real iPhone. The device-only
behaviors above (the folder-field order, the missing-folder refusal, the
chunked-pipe empty file, mDNS, the Local Network prompt, Blink's own curl
version and its handling of `{a,b}`) are read from source or platform
knowledge, not observed. Blink's curl was not inspected; the encoding of the
appended file name was measured on curl 8.7.1 and 8.23.0 only.

**Proposed, not done** (each beyond what was asked; say the word):

- A PUT to a folder, or into a missing folder, could answer `409` with a plain
  reason instead of the misleading `500 Write failed`.
- A chunked PUT could be refused (`411`) on the device, as the simulator does,
  instead of writing an empty file. Needs `Transfer-Encoding` added to
  `collectHeaders` in `CrossPointWebServer::begin()`.
- `POST /upload` could take an explicit `overwrite=1`.
- The simulator's `HalStorage::open` creates missing parent folders and its
  multipart parser reads every field before the file; the Picco does neither.
  Both make a desktop pass where the device fails.
