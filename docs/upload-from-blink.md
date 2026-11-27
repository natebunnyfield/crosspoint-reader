# Uploading from Blink (or any curl), with no web page

Written 2026-11-27. Revised the same day after the review passes
(`docs/review-passes-2026-11-27.md`, rows R-01 to R-14) against firmware
`ba4ff9c2c` and simulator `6a42ece5`. Owner: *"be sure to make it possible for
blink to upload a file no matter what bugs happen in file transfer screen"*.

Blink Shell on an iPhone or iPad has `curl`, `ssh`, `mosh`, `scp` and `sftp`
clients, and no browser. The device runs no ssh server, so **curl is the
path**. For months the File Manager page's script did not parse (B-080) and
every upload made through the page was silently dead. The uploads below need
no page and no script at all: they talk to the server directly.

## The commands

Run them from the folder in Blink that holds the file. Type the file's name as
it is, inside the quotes: **these forms take any name**, with commas,
semicolons, brackets, braces, `+`, `#`, `%`, accents and apostrophes (the four
characters the shell itself reads are trap 4 below). `crosspoint.local` can be
replaced by the IP address the device shows (see "When `crosspoint.local` does
not resolve").

### The BOOX Picco, over Wi-Fi

Open **File Transfer** on the Picco, pick the network, wait for the screen with
the QR code. Then in Blink:

```sh
# 1. Is it there? Prints a line of JSON with "ip" in it.
curl http://crosspoint.local/api/status

# 2. Upload into /books. Keeps the file's name. Replaces a file of the same name.
#    Keep the -g: without it curl reads [ ] { } in the NAME as a pattern.
curl -g -T "My Book.epub" http://crosspoint.local/books/

# Same, printing [201] (new file), [204] (replaced), or the reason it was refused.
curl -g -w ' [%{http_code}]\n' -T "My Book.epub" http://crosspoint.local/books/

# Several files: one -T and one address for each.
curl -g -T "Tom, Jerry.epub" http://crosspoint.local/books/ -T "Title [Author].epub" http://crosspoint.local/books/

# Under another name on the card: put the new name in the address. %20 is a space.
curl -g -T "Dune: Messiah.epub" "http://crosspoint.local/books/Dune%20-%20Messiah.epub"

# 3. Into a NEW folder: make it first, then upload.
curl -X MKCOL "http://crosspoint.local/New%20Shelf/"
curl -g -T "My Book.epub" "http://crosspoint.local/New%20Shelf/"

# 4. The other way in: a form post, the same request the web page makes.
#    Refuses to replace an existing file. The folder goes FIRST, typed as it is.
#    Keep the \" pair: without it curl splits the name at a comma or semicolon.
curl --form-string "path=/books" -F "file=@\"Ender's Game.epub\"" http://crosspoint.local/upload
curl --form-string "path=/New Shelf" -F "file=@\"Tom, Jerry.epub\"" http://crosspoint.local/upload

# 5. Check what landed, and remove a mistake. No %20 needed in these two.
curl -G --data-urlencode "path=/books" http://crosspoint.local/api/files
curl --data-urlencode "path=/books/My Book.epub" http://crosspoint.local/delete
```

**Use `-g -T` unless you need the form post.** It needs only one plain pair of
quotes, and it replaces a file of the same name.

Shorter forms that work only for plain names (no `[ ] { } , ;`):
`curl -T "My Book.epub" URL`, `curl -T "{a.epub,b.epub}" URL` (curl's own
list, the feature `-g` turns off) and
`curl -F "path=/books" -F "file=@My Book.epub" URL/upload`.

By IP, the same commands with the address the screen shows, for example:

```sh
curl -g -T "My Book.epub" http://192.168.1.42/books/
curl --form-string "path=/books" -F "file=@\"Tom, Jerry.epub\"" http://192.168.1.42/upload
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
curl -g -T "My Book.epub" http://192.168.1.57:8080/books/
curl -X MKCOL "http://192.168.1.57:8080/New%20Shelf/"
curl --form-string "path=/books" -F "file=@\"Tom, Jerry.epub\"" http://192.168.1.57:8080/upload
```

Three limits that are the platform's, not the firmware's:

- **`crosspoint.local` never reaches the app.** iOS lets an app publish a
  Bonjour *service*, not claim a hostname (crosspoint-simulator
  `ios/WIFI.md`, finding 6). Use the IP and `:8080`.
- **The app must stay in front for the whole upload.** iOS suspends a
  background app and its socket stops accepting (`ios/WIFI.md`, "Known
  limits"). So Blink on the SAME iPhone cannot reach it: bringing Blink to the
  front suspends the app. Run Blink on another device on the same Wi-Fi. On an
  iPad, Split View or Stage Manager with both apps on screen may work; that is
  untested.
- **A form post (`-F`) over 256 MB is refused with `413`.** The app holds a
  form upload in memory. `curl -g -T` is streamed to the card and has no size
  limit (measured on the desktop simulator: 300 MB, R-13).

## What a reply means

Every refusal says why in plain text, and nothing is written. Until 2026-11-27
most of these were `500 Write failed - incomplete upload or disk full`.

| Reply | What to do |
|---|---|
| `201`, `204` (from `-T`); `200 File uploaded successfully: NAME` (from `-F`) | Nothing. It landed (`204` = replaced) |
| `405 /books is a folder. To upload into it, end the URL with a slash ...` | Add the `/` after the folder |
| `409 Folder /X does not exist. Create it first (MKCOL), then upload` | Make the folder (step 3) |
| `409 /books/x.epub is a file, not a folder` | The address runs on past a file name |
| `400 File already exists: NAME` | `-F` never replaces. Use `-g -T`, or delete it first |
| `400 Nothing was uploaded: the request had no file in it. ...` | The `@` is missing from `file=@` |
| `400 File name has a character the card cannot store: ':' (not allowed: ...)` | Upload under another name (step 2). Picco only, see below |
| `400 File name is too long: 256 characters, the card allows 255` | Upload under a shorter name |
| `423 Locked: the File Transfer page is uploading "NAME" right now (...)` | The web page is partway through that same name. It lets go when it finishes, its tab closes, or it sends nothing for 30 s. Retry then, or upload under another name |
| `500 Could not create the file on the card ...`, `500 Write to the card failed after N bytes ...` | The card is full or failing |
| `curl: (3) bad range ...`, `curl: (26) Failed to open/read local data`, `curl: cannot open 'NAME'` | curl gave up before sending anything: the `-g` or the `\"` pair is missing |

**Names the Picco's card cannot store.** The card is FAT. It refuses a name
holding any of `" * : < > ? \ |` or a control character, a name over 255
characters, and a name of nothing but spaces and dots
(`FsHelpers::checkFatName`, SdFat's own rules). Sending `Dune: Messiah.epub`
into `/books/` answers the `400` above; send it under a name without the
character. **Read from source, not run on a Picco.** The simulator and the iOS
app store files on a host file system that takes those characters (measured:
that name gives `201` there), so a desktop run passes where the Picco refuses.

## The rules each form follows

Measured on the desktop Picco simulator unless marked otherwise.

| | `curl -g -T` (WebDAV PUT) | `curl -F` (POST /upload) |
|---|---|---|
| File name | curl appends the local name to an address ending in `/` and percent-encodes it. A name in the address renames | the name curl puts in the form. `-F "file=@\"x.epub\";filename=\"Other.epub\""` renames |
| What curl itself reads in the name | `[ ] { }` as a pattern, unless `-g` | `,` as "next file" and `;` as "options follow", unless the name is inside `\"` |
| Folder | in the address, `%20` for a space | a `path` field **before** the file, typed as it is. `--form-string` and not `-F`, which cuts the folder at a `;` |
| Existing file | **replaced**, `204` | **refused**, `400`, old bytes kept |
| Missing folder | **refused**, `409` | **refused on the Picco**, `409` (read from source). The simulator creates it instead, so a desktop test passes where the Picco refuses |
| Success | `201` new, `204` replaced, no body | `200 File uploaded successfully: NAME` |

Four traps:

1. **With `-F`, the folder field must come first.** The device's parser gives
   the upload handler only the fields it has read so far (`WebServer::arg()`
   checks `_postArgs` while the file streams, arduino-esp32 3.3.7
   `Parsing.cpp` `_parseForm`). The file before the folder therefore lands in
   `/` on the Picco. The simulator reads the whole form first and gets it
   right, so only the device shows this.
2. **Never pipe into `-T`.** `cat x | curl -T - URL` sends a chunked body. The
   device's WebServer has no chunked request support (`Parsing.cpp:111-112`
   sets the length to 0 and nothing reads chunks), so it would write an EMPTY
   file and answer `201`. Read from source, not run on a device. The simulator
   refuses with `501 Chunked transfer encoding not supported`. Always give
   curl a file name.
3. **A big file pauses for about a second first.** curl asks for
   `100 Continue` on a large body and the server never sends it, so curl waits
   ~1 s and then sends anyway (measured: 5 MB PUT, 1.05 s on loopback).
   Harmless.
4. **A name with `"`, `\`, `$` or a backquote** is read by the shell before
   curl sees it. The Picco's card refuses the first two anyway. None of the
   owner's 67 epub names has any of the four.

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
same `/upload` as `curl -F`, so the same rules hold: the folder must exist, a
same-named file is not replaced, and Upload with no file picked is answered
`400 Nothing was uploaded`. The File Manager carries a static line under its
buttons, "Upload not working? Basic upload page", which shows whether or not
the page's script runs.

The form lives in `CrossPointWebServer.cpp` as a literal (`UPLOAD_FORM_HTML`),
not in `src/network/html/`, so it never passes through
`scripts/build_html.py`: that minifier is what broke the page in B-080.

Its one cost: a file at the card's root named exactly `upload` (no extension)
can no longer be fetched with a WebDAV `GET /upload`, the same as a root file
named `files`, `settings` or `fonts` before it. It still lists, and
`/download?path=/upload` still serves it.

## Does the File Transfer SCREEN, or its page, have to work?

Audit of 2026-11-27, read against the source and corrected after the review
(R-01, R-06, R-07, R-08). The question: can anything the device's screen or
the web page does stop the server answering curl.

**The page: no longer.** A page upload that stalled with its tab open used to
hold its file name against every curl route until the tab closed. It is now
given up on after 30 s without data (`WS_UPLOAD_STALL_MS`,
`CrossPointWebServer.cpp`), and until then curl is told what holds the name
(the `423` above) and can upload under another name at once. Measured on the
simulator; `crosspoint-simulator/tests/test_blink_upload.sh` check 11 pins it.

**The screen: mostly no, by construction.** On the device, requests are served
on the main loop task: `CrossPointWebServerActivity::loop()` calls
`webServer->handleClient()` in a time-bounded batch
(`CrossPointWebServerActivity.cpp:476`), reached from `ActivityManager::loop()`
(`ActivityManager.cpp:94`) with no render lock held. The screen is drawn on a
separate task, `ActivityManager::renderTaskLoop()`, which takes the render lock
and calls `render()` (`ActivityManager.cpp:56-59`). No request handler touches
the renderer or the activity manager (checked by grep over
`CrossPointWebServer.cpp`, `WebDAVHandler.cpp`, `BookCacheUtils.cpp`). So a
slow draw does not stop requests; the rows below are what can.

What can still pause or stop serving:

| What | Effect | Where |
|---|---|---|
| A short Power press with Power set to Force Refresh | The main loop takes the render lock BLOCKING and then runs the half refresh itself, so serving pauses for that whole refresh on every press, and first for any render already in flight. With a hung panel each BUSY wait in it gives up after 30 s on the Picco, the X4 and the X3. **On the X4 Pro it never ends**: that panel's BUSY wait has no timeout by design, so serving stops until a reset | `main.cpp:1519-1525`; `EpdBus.cpp` `waitBusy` |
| Leaving File Transfer (Back, the Home gesture, 5 minutes of lost Wi-Fi) | The server stops and the device restarts, by design | `CrossPointWebServerActivity.cpp:419-422`, `:492-493` |
| A `.bin` uploaded into `/firmware/` on an X3/X4 | The confirm screen is pushed over File Transfer, whose `loop()` (and so `handleClient()`) does not run until it is answered. **Not on the Picco or the app**: both build with `CROSSPOINT_NO_DEVICE_FLASH`. The desktop simulator does push it | `CrossPointWebServerActivity.cpp:391` |
| The Picco's idle panel power-off | Runs on the main loop 2 s after a refresh, with the render lock free (try-lock). It no longer starts while the panel reads BUSY, so a hung panel does not stall serving after every screen update (R-07). A panel that hangs during the power-off itself still costs one wait of up to 30 s | `main.cpp:1558`; `HalDisplay.cpp` `panelIdlePowerOffDue` |
| A crash in `render()` | Reboots the device; the server goes with it. Nothing seen, nothing to fix without a report | — |
| The iOS app going to the background | iOS suspends it | `ios/WIFI.md` |

The BUSY caps, from `freeink-sdk/libs/display/FreeInkDisplay/src/bus/EpdBus.cpp`
`waitBusy`: 30 s on the X4's SSD1677 (`:225`), the Picco's SSD2677 (`:248`) and
the X3's UC8253 (`:284`). The X4 Pro's UC8179 and UC8279 take the `UcIdleHigh`
branch (`:251-267`), which says "deliberately no fixed millisecond timeout",
and `waitRefreshComplete` uses the same wait (`:301-303`). A hung X4 Pro panel
therefore keeps the render task inside `render()` holding the render lock.
Uploads keep working, because File Transfer's own loop needs no lock, until
something on the main loop asks for the lock: the Force Refresh row. Read from
source; no X4 Pro was tried.

**The simulator and the iOS app do not serve `curl -T` the way the device
does.** Their HTTP shim (`crosspoint-simulator/src/WebServer.cpp`) accepts and
parses on a worker thread, parks the request, and `handleClient()` (`:934`)
runs the route handler on the firmware's main thread, as on the device. A
WebDAV PUT body is the exception (`:338-350`, `:743-792`): the worker itself
calls the firmware's `WebDAVHandler::raw()` as the bytes arrive, so the file
is opened, written, closed and renamed on the worker whether or not the main
loop is running. Only the PUT's status reply waits for `handleClient()`. On
the device the body and the reply both run inside `handleClient()`. A form
post (`-F`) is buffered whole by the worker and its upload handler does run
in `handleClient()`. What follows from that:

- With the main loop stalled, the simulator and the app put a whole `curl -T`
  file on the card and send no reply; the device would write nothing. Measured
  in the review (R-06): a 1 MB PUT got no reply and landed with a matching md5.
- So a matching md5 on a `-T` upload in a desktop test shows the bytes
  arrived, not that the main loop moved them. Only the status code shows the
  loop served the request.
- After a stall the shim still serves a request whose curl already gave up, so
  on the simulator or the app a file can appear after curl reported a failure
  (measured: a `-F` landed about 22 s after its curl timed out). A retry with
  `-F` then answers `400 File already exists`; `-g -T` replaces it.

The one structural fix that would make serving fully independent of the
activity (a server task of its own) is not the smallest change and was not
made.

## How this was proven, and what was not

Desktop Picco simulator, a `simulator_picco` build made after the review fixes
(it answers the `405`, `409` and `400` replies above), a clean scratch card
holding only an empty `/books`, a `{}` settings file beside it,
`CROSSPOINT_SIM_HTTP_PORT=18641`, navigated to File Transfer headlessly
(`200:QTAP:BACK:2500;4000:UP;4900:UP;5800:UP;6800:CONFIRM;8000:CONFIRM;10000:CONFIRM`
with `CROSSPOINT_SIM_WIFI_NETWORKS='Alpha:-40:open'`; on the Picco the rocker's
upper end, `UP`, is next-item). Files with obviously fake contents. Every file
on the card was checked by md5 against what was sent, under the name sent.

- **Every command in "The commands"**, in order, with only host and port
  changed, with curl 8.23.0, each of the three blocks on a fresh card of its
  own. The app's block ran against the desktop simulator, not the app.
- **`curl -g -T` and the `--form-string` / `-F "file=@\"NAME\""` form, each with
  curl 8.23.0 and macOS's 8.7.1**, for `Tom, Jerry.epub`, `Book; A Novel.epub`,
  `Title [Author].epub`, `Series {Vol 1}.epub`, `C++ Primer.epub`,
  `Café Olé.epub`, `Ender's Game.epub` and `Book #1 & 2 = 100%.epub`: all
  landed intact.
- **All 67 epub NAMES in `~/src/crosspoint-books`** (fake contents under the
  real names; 6 hold a comma or semicolon, 4 a straight apostrophe, the longest
  is 197 bytes): 67 of 67 landed intact with `-g -T` on both curls and with
  the form post.
- **The failures the old commands had**: without `-g`, `Title [Author].epub`
  and `Series {Vol 1}.epub` never left curl; without the `\"` pair, the comma
  and semicolon names never left curl; `-F "path=/a;b,c"` landed in `/a`, and
  `--form-string` put it in `/a;b,c`.
- **The replies**: the `405`, both `409`s, `400 File already exists`,
  `400 Nothing was uploaded`, `400 ... too long` (a 256-character name) and
  the `501` for a piped body. The `423` and the 30 s release were measured in
  the R-01 fix and are pinned by the gate's check 11, not run again here.
- Gate: `crosspoint-simulator/tests/test_blink_upload.sh`, in
  `tests/run_all.sh` (`tests/run_all.sh -k blink_upload`), about 30 s. It
  does not yet send a name with `, ; [ ] { }`.

**Unproven:** nothing here ran on a real Picco or a real iPhone. Read from
source or platform knowledge, not observed: the Picco's refusal of reserved
characters and its message, the folder-field order, the `-F` missing-folder
`409`, the chunked-pipe empty file, the X4 Pro's endless BUSY wait, mDNS, the
Local Network prompt. **Blink's own curl and shell were not inspected**: its
curl version, and whether its shell passes `\"` inside quotes through as the
shells here do. If the form post misbehaves there, `-g -T` needs only one
plain pair of quotes. The encoding of the appended file name was measured on
curl 8.7.1 and 8.23.0 only.

**Proposed, not done** (each beyond what was asked; say the word):

- A chunked PUT could be refused (`411`) on the device, as the simulator does,
  instead of writing an empty file. Needs `Transfer-Encoding` added to
  `collectHeaders` in `CrossPointWebServer::begin()`.
- `POST /upload` could take an explicit `overwrite=1`.
- The simulator's `HalStorage::open` creates missing parent folders, its
  multipart parser reads every field before the file, and its file system
  takes the characters FAT refuses; the Picco does none of the three. Each
  makes a desktop pass where the device fails.
- The X4 Pro row. Two options, neither taken, because each changes a device
  this was not run on. (a) The Force Refresh press could TRY the render lock
  while File Transfer is up and skip the refresh when a render holds it. A
  hung render then no longer takes the main loop with it; a panel that hangs
  during the forced refresh itself still would. (b) The SDK's `UcIdleHigh`
  wait could get a timeout. Its comment says the lack of one is deliberate:
  a command sent while BUSY is low can make the controller drop plane or LUT
  writes.
