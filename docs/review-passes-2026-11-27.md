# Review passes over the Picco work, 2026-11-27

Owner, 2026-11-27: *"take multiple passes until 12:55"*, then *"work without me"* and *"be sure to make it possible for blink to upload a file no matter what bugs happen in file transfer screen"*.

One timed workflow (`picco-multipass-blink`, 83 agents, 11:00 to 11:53 on the Mac's clock, 12:00 to 12:53 Eastern). Six read-only finders, one lens each (the simulator's present path, device power, the frontlit page, the web pages and Settings, tests and docs, cross-repo integration), over firmware `2c9136128..4998d84da`, simulator `4c95ebd9..36206135` and freeink-sdk `e05bb17..119f3ed`. Every finding went to three skeptics told to refute it (reproduce, look for a guard, check reach); it stood on two of three. In parallel one agent built the Blink upload path (`docs/upload-from-blink.md`), and three more skeptics then tried to break that.

**Result: 23 findings reported, 22 confirmed, 1 refuted; the Blink skeptics demonstrated 15 failures (some are the same defect found twice). The time box ended before any fixer ran, so all 36 below were OPEN when this file was written.** Fix pass the same day (below): 28 FIXED, R-15 partly fixed, 7 still OPEN (R-16, R-17, R-19, R-20, R-21, R-22, R-33). Each row's status is updated by the commit that closes it. Limits: one round only (the second never started), one card, the desktop simulator and reading for everything device-side; nothing here was tried on the Picco or a real iPhone.

Repo key: **fw** = crosspoint-reader, **sim** = crosspoint-simulator, **sdk** = freeink-sdk. `[blink]` = found by the Blink skeptics.


## visible-bug (15)

### R-01 [blink] A page upload that stalls with its tab open locks that file name against every documented curl route, and the -T reply blames a missing slash or folder

**status: FIXED** (fw 4b02a5804) · fw `src/network/WebDAVHandler.cpp:64`

This is the scenario the owner asked about: a File Transfer page bug stops curl. If the page's script sends a WebSocket START and then dies (or the phone's browser is suspended mid-upload) while the socket stays open, webUploadIsWriting() (CrossPointWebServer.cpp:342-348) stays true for that path. A WebDAV PUT is then refused at RAW_START (WebDAVHandler.cpp:64) and handlePut answers 500 'Write failed - incomplete upload or disk full' (WebDAVHandler.cpp:436-439). POST /upload answers 400 'File already exists' because the page's partial file is on the card (CrossPointWebServer.cpp:868), and DELETE answers 423 (WebDAVHandler.cpp:482-483). Nothing on the device times out a stalled WebSocket upload: there is no enableHeartbeat anywhere in src/network, no stall timer on wsUploadStartTime, and crosspoint-simulator BUGS.md (the 'One 5-second socket timeout' entry, around line 647) says the device's WebSocket library 'has no idle timeout on an established connection'. On the device the lock lasts until the page's socket closes or File Transfer is left. The simulator caps it at 120 s. Doc trap 1 (docs/upload-from-blink.md:104-108) attributes that 500 only to a missing trailing slash or a missing folder. Neither way out is documented: closing the page's tab, or a PUT to an explicit other name such as `curl -T x.epub http://host/books/x-2.epub`.

*Trigger:* With File Transfer up, a page (simulated by a raw WebSocket client on port 18724) sends START:Stuck Book.epub:5242880:/books plus one 4096-byte chunk and holds the socket open. Blink then runs the documented `curl -T "Stuck Book.epub" http://host/books/`.

*Evidence:* Desktop Picco simulator at HEAD 847ecabdd, port 18723. The scratch tool /private/tmp/claude-501/-Users-natebunnyfield-src/2f7ddd19-354b-4b40-85d2-4aff02275054/scratchpad/passes/blink-skeptic/tools/stuck_ws.py got 101, then READY; a 4096-byte 'Stuck Book.epub' appeared in /books. Then: `curl -T` gave 'Write failed - incomplete upload or disk full [500]'; `curl -F path=/books -F file=@...` gave 'File already exists: Stuck Book.epub [400]'; DELETE gave 'Locked: upload in progress [423]'; a different name gave 201. Once the socket closed after 40 s the partial file was removed and the same `curl -T` gave 201 with a matching md5.

### R-02 [blink] POST /upload with no file part answers '200 File uploaded successfully: <previous file>' and writes nothing

**status: FIXED** (fw 99a6b2ad9) · fw `src/network/CrossPointWebServer.cpp:967`

The UploadState `upload` member lives across requests. state.success is reset only at UPLOAD_FILE_START (line 830), and a request with no file part never reaches that point. handleUploadPost (line 964-971) then reports the last successful upload of the session. On the curl -F route this reply is the only success signal (doc table, docs/upload-from-blink.md:100). Several easy Blink mistakes produce a 200 that names a different file while nothing reaches the card: dropping the '@' (`-F "file=My Book.epub"`), trying `--data-binary @x.epub`, or sending the path field alone. The same code runs on the device: arduino-esp32 3.3.7 _parseForm adds a text-only part to _postArgs and never raises UPLOAD_FILE_START. This was there before the change. Only the first such request after boot gets the honest 400 'Unknown error during upload'.

*Trigger:* After any successful upload in the session, run `curl -F "path=/books" -F "file=My Book.epub" http://host/upload` (no @), or `curl --data-binary @a.epub "http://host/upload?path=/books"`.

*Evidence:* Port 18723, after a successful -F upload of 'C++ Primer.epub'. All four of these answered 'File uploaded successfully: C++ Primer.epub [200]': -F without '@', -F path alone, a bare -X POST, and --data-binary @a.epub. A diff of the /books and / listings before and after showed nothing written.

### R-03 [blink] The simulator and iOS app decode WebDAV paths twice, so `curl -T "C++ Primer.epub" .../books/` lands as 'C Primer.epub'

**status: FIXED** (sim 467f2fa4) · sim `src/WebServer.cpp:651`

The shim percent-decodes the request path before storing it as currentUri (src/WebServer.cpp:651). The firmware's WebDAVHandler::getRequestPath then calls WebServer::urlDecode on it again (firmware WebDAVHandler.cpp:839), and the shim's urlDecodeStd turns '+' into a space. curl correctly sends %2B, so the first decode yields '+' and the second yields ' '. A literal '%XX' in a name would also be decoded twice. The device is correct: arduino-esp32 3.3.7 Parsing.cpp:110 stores _currentUri raw, so it is decoded once. The iOS app compiles this shim: cmake/CrossPointSources.cmake:39 lists src/WebServer.cpp and cmake/CrossPointIOSExclusions.cmake does not remove it. docs/upload-from-blink.md:73 documents exactly this route (`curl -T "My Book.epub" http://<phone-ip>:8080/books/`), so names with '+' are silently renamed on the phone app. It also means desktop runs, test_blink_upload included, decode WebDAV paths differently from the device.

*Trigger:* `curl -T "C++ Primer.epub" http://127.0.0.1:<port>/books/` against the desktop Picco simulator, or `http://<phone-ip>:8080/books/` against the iOS app.

*Evidence:* curl -v showed '> PUT /books/C%2B%2B%20Primer.epub' (curl 8.23.0; 8.7.1 sends %2b). The reply was 201, but the card held 'C Primer.epub'. The same file sent with `-F path=/books -F file=@"C++ Primer.epub"` landed as 'C++ Primer.epub', because multipart filenames are not URL-decoded.

### R-04 [blink] The documented `curl -F "file=@NAME"` fails for any name with a comma or semicolon, which covers 6 of the owner's 67 corpus epubs

**status: FIXED** (fw 6ea93b2bf) · fw `docs/upload-from-blink.md:43`

In -F, curl reads ',' after '@' as a separator between several files and ';' as the start of type=/filename=. An unquoted name with either character fails locally with curl (26) and nothing is sent. Commas are common in book file names ('Lee, Harper'-style author sort, Anna's Archive names). The doc's 'Four traps' do not mention it. The fix is to quote inside the field, `-F 'file=@"Lee, Harper.epub"'`, which works. curl -T is not affected by commas.

*Trigger:* `curl -F "path=/books" -F "file=@Go Set a Watchman ... -- Lee, Harper -- ... Anna’s Archive.epub" http://host/upload`, or any name with ',' or ';'.

*Evidence:* curl 8.23.0 and /usr/bin/curl 8.7.1 both failed `-F "file=@Tom, Jerry.epub"` with 'curl: (26) Failed to open/read local data', reply [000]. The 198-byte corpus-shaped name (CJK and ’ included) failed the same way. `-F 'file=@"semi;colon.epub"'` gave 200, and the quoted corpus name gave 200 with a matching md5. The same corpus name via `curl -T` gave 201 with a matching md5. `ls ~/src/crosspoint-books`: 6 of 67 .epub names contain ',' or ';'.

### R-05 [blink] The documented `curl -T "NAME"` fails for names with [ ] or { }, because curl globs the -T argument

**status: FIXED** (fw 6ea93b2bf) · fw `docs/upload-from-blink.md:29`

curl expands {..} and [..] in the -T file name. The doc relies on that for `-T "{a.epub,b.epub}"` (line 34-35) but never warns that a single file whose name has brackets or braces breaks. 'Title [Author].epub' is a range error, and 'Series {1}.epub' turns into a request for 'Series 1.epub'. `-g` (--globoff) fixes both, but it also turns off the doc's own multi-file form, so it cannot be blanket advice. None of the owner's 67 corpus epubs has these characters, so this is a doc gap for other people's files, not for his library today.

*Trigger:* `curl -T "Title [Author].epub" http://host/books/` or `curl -T "Series {1}.epub" http://host/books/`

*Evidence:* curl 8.23.0 gave 'curl: (3) bad range in position 8' for brackets and 'curl: cannot open 'Series 1.epub'' (26) for braces. /usr/bin/curl 8.7.1 gave the same bad-range error. `curl -g -T` gave 201 for both.

### R-06 [blink] Simulator and iOS app write curl -T bodies on the accept thread, so the doc's 'the shape is the same' is wrong and the md5 checks cannot show the main loop moved the bytes

**status: FIXED** (fw 6ea93b2bf (doc), sim 25eacbfc (comment); behavior kept by ruling) · sim `src/WebServer.cpp:694`

In the shim, a WebDAV PUT body never goes through handleClient(). The accept worker calls WebDAVHandler::raw() itself for RAW_START, RAW_WRITE and RAW_END (WebServer.cpp:694, :699/:719, :743). That opens the .davtmp file, writes it, closes it and renames it to the final name (firmware WebDAVHandler.cpp:60-120). Only the status reply (handlePut) waits for the firmware's main loop. On the device all of it runs inside handleClient() on the loop task. So when the main loop is not pumping, the simulator and the iOS app put the whole file on the card and send no reply, while the device writes nothing. Three things follow. (1) firmware docs/upload-from-blink.md:186 says 'In the simulator and the iOS app the shape is the same... handleClient() runs it on the main thread'. That is false for curl -T, the doc's main Blink route. The shim's own comment at WebServer.cpp:328-331 ('nothing in a route handler runs on the worker thread') is also false for raw handlers. (2) test_blink_upload's md5 checks on the -T cases pass whether or not the main loop moved the bytes. Only the HTTP code checks catch a stalled loop, and nothing tests the device's real body path (5 s HTTP_MAX_DATA_WAIT on the loop task; the shim waits 60 s). (3) After a stall, the shim still serves requests whose curl already gave up: the socket buffer held the whole body, so the file lands later. A Blink user sees a failure, the file shows up anyway, and a retry with -F answers 400 'File already exists'.

*Trigger:* Any stall of the simulator's or the iOS app's main loop during a Blink upload. Shown with a screen pushed over File Transfer: PUT a .bin into /firmware/ on the desktop simulator_picco. That build does not define CROSSPOINT_NO_DEVICE_FLASH (platformio.ini [env:simulator_picco] extends [env:simulator]), so SdFirmwareUpdate is pushed and File Transfer's loop() stops calling handleClient().

*Evidence:* Script: /private/tmp/claude-501/-Users-natebunnyfield-src/2f7ddd19-354b-4b40-85d2-4aff02275054/scratchpad/passes/screen-skeptic/popup_probe.sh (simulator_picco, port 18762, scratch card). Log: /private/tmp/claude-501/-Users-natebunnyfield-src/2f7ddd19-354b-4b40-85d2-4aff02275054/scratchpad/passes/screen-skeptic/run_popup/sim.log. Output: 'MKCOL /firmware/ -> 201', 'PUT /firmware/fw.bin -> 201', log '[10462] Entering activity: SdFirmwareUpdate'; then 'PUT during-put.epub (-m 8) -> 000 ; card md5 03d2ee52d7cad3f7268e0aa4e17dea40 src 03d2ee52d7cad3f7268e0aa4e17dea40' (no reply, but the 1 MB file is complete on the card), log '[12529] PUT END: 1000000 bytes, ok=1' with no 'PUT complete' until '[48013]'; 'GET /api/status (-m 5) -> 000'; 'POST during-post.epub (-m 8) -> 000 ; card MISSING'. After BACK at 48 s: '[48013] WARNING: 37552 ms gap since last handleClient', then '[48014] POST /upload ... [48020] Complete: during-post.epub (400000 bytes)': the POST curl gave up on landed 22 s later, md5 8bea0517e6d36b5b642b3088d404f19f matches the source. Recovery confirmed: 'GET /api/status -> 200', 'PUT after-put.epub -> 201 ; match yes'. Simulator was killed at the end; nothing listens on 18762.

### R-07 [blink] A hung panel stalls Picco serving for up to 30 s per screen update, because the idle power-off waits on BUSY on the main loop

**status: FIXED** (fw e1eb42916 (UNCONFIRMED on device)) · fw `src/main.cpp:1557`

The Picco's idle panel power-off runs on the main loop, which is the task that calls handleClient(). Two seconds after any refresh, main.cpp:1557-1559 takes a try-lock and calls display.powerOffIdlePanel(). That reaches FreeInkDisplay::powerOffBetweenFrames (FreeInkDisplay.cpp:577-584), then Ssd2677Driver::powerOffBetweenFrames (Ssd2677Driver.cpp:129-134), which sends POWER_OFF and calls bus.waitBusy(). The Picco's BUSY is ActiveLow (test_picco.cpp:175). With BUSY stuck at its active level, that wait loops until the 30 s cap (EpdBus.cpp:248). The slice hook declines with Wi-Fi on (HalPowerManager.cpp:321), so the wait is plain delays on the loop task, and handleClient() is not called the whole time. A timed-out refresh still calls noteRefresh() (HalDisplay.cpp:82 and the other call sites, all unconditional), so every screen update arms it again: a Wi-Fi bar change, a Wi-Fi drop or recovery, a USB edge, the slide switch, or the first SERVER_RUNNING paint. The try-lock does not help here: by then the render task has given up and released the lock. This contradicts the doc's 'a slow or stuck draw does not stop requests' (docs/upload-from-blink.md:180-181) and its table row 'costs one power-off command' (:198). Uploads are paused, not lost: curl has no default timeout, and lwIP holds the connection, so the request is served once the wait ends.

*Trigger:* Picco in File Transfer with the panel's BUSY line stuck low (a hung controller, a loose FPC, or a brown-out on the panel). Any screen update, then 2 s later the main-loop power-off.

*Evidence:* Read from source, not run: the simulator mirrors the idle power-off as a no-op (simulator commit ee0690d3), so no host run can show it. Chain: main.cpp:1557-1559 (main loop, after activityManager.loop()) -> HalDisplay.cpp:259-265 (clears panelMayBePowered, so it runs once per refresh, then calls einkDisplay.powerOffBetweenFrames) -> Ssd2677Driver.cpp:129-134 (POWER_OFF; waitBusy). _needsPowerOn is false after any refresh drawn with turnOff=false (Ssd2677Driver.cpp:139-148), so the wait is not skipped -> EpdBus.cpp:227-249 (ActiveLow: 'if (millis() - start > 30000) break;'). The serving pump is the same task: CrossPointWebServerActivity.cpp:476.

### R-08 [blink] The doc's 30 s BUSY cap does not hold on the X4 Pro, so Force Refresh over a hung panel stops serving for good

**status: FIXED** (fw 6ea93b2bf) · fw `docs/upload-from-blink.md:181`

The doc says 'A panel BUSY wait gives up after 30 s (EpdBus.cpp:225), so even a hung panel ends its render' (:181-182). Its Force Refresh row says serving pauses 'at most the 30 s BUSY cap' (:195). Line 225 is only the ActiveHigh path. The X4 Pro's UC8179 and UC8279 drivers use BusyPolarity::UcIdleHigh (Uc8179Driver.h:65, Uc8279X4Driver.h:70). That wait has 'deliberately no fixed millisecond timeout' (EpdBus.cpp:251-267), and waitRefreshComplete takes the same path (EpdBus.cpp:301-303). On an X4 Pro with a hung panel, the render task never leaves render() and keeps the render lock. File Transfer's own pump needs no lock, so uploads keep working until the main loop asks for the lock. A short Power press with Force Refresh takes RenderLock blocking (main.cpp:1520-1526) and then never returns, so serving stops until a hard reset. The Force Refresh row also leaves out that, even with no render running, the forced HALF refresh itself runs on the main loop. Serving pauses for that whole waveform on every press, not only when a render is in flight.

*Trigger:* X4 Pro, File Transfer running, panel BUSY_N stuck LOW, Power set to Force Refresh, and a short Power press.

*Evidence:* Read from source. EpdBus.cpp:251-267: 'There is deliberately no fixed millisecond timeout on this controller path', then 'while (digitalRead(_pins.busy) == LOW) { busyIdle(...); ... }' with no break. Compare the ActiveHigh path's 'if (millis() - start > 30000) break;' at :225. main.cpp:1520-1526: 'RenderLock lock; renderer.displayBuffer(HalDisplay::HALF_REFRESH);' on the loop task.

### R-09 [blink] test_blink_upload SKIPs, and the suite stays green, when File Transfer's server never starts or the sim crashes

**status: FIXED** (sim 6a42ece5) · sim `tests/test_blink_upload.sh:72`

The test treats two cases as a missing precondition (exit 2, SKIP): the port never answering (line 72) and the simulator exiting before it answers (line 67). run_all.sh's run_shell_skip counts a SKIP as not failed and exits 0. Those two cases are exactly the regression the owner asked to guard against: File Transfer is entered but nothing can upload. The log already tells this apart from a drifted menu walk, since it contains 'Entering activity: CrossPointWebServer'. So the test could FAIL when the activity was entered but the port never answered, or when the process died after entering it.

*Trigger:* Mutate a scratch copy of the firmware so CrossPointWebServer::begin() never calls server->begin() (CrossPointWebServer.cpp:308). Build simulator_picco and run tests/test_blink_upload.sh against it, then run tests/run_all.sh -k blink_upload.

*Evidence:* Mutation M4 in a scratch copy (pio run -e simulator_picco SUCCESS). The test printed 'Entering activity: CrossPointWebServer' / 'Entering activity: WifiSelection' / 'SKIP: web server never came up (menu walk may have drifted)', exit=2, after 32 s. CROSSPOINT_FIRMWARE_DIR=<mutant> tests/run_all.sh -k blink_upload printed 'test_blink_upload SKIP' and '0 passed, 1 skipped', exit 0 (run_all.sh tail: fail==0 -> exit 0). For contrast, removing GET /upload (M1) gave FAIL on check 6 (404), and removing POST /upload (M3) gave FAIL on checks 4 and 5 (404). The test does fail when one upload path goes, but passes silently when all of them go.

### R-10 [blink] The doc's curl commands fail for names with [ ] { } (-T globbing) or , ; (-F separators), and the doc does not say so

**status: FIXED** (fw 6ea93b2bf) · fw `docs/upload-from-blink.md:29`

The doc gives curl -T "My Book.epub" and curl -F "file=@My Book.epub" as the universal commands. curl globs the -T argument, the same feature the doc uses for -T "{a.epub,b.epub}". So a name with brackets fails before any request, and a name with braces is expanded into a file that does not exist. In -F, a comma separates several files and a semicolon starts a parameter. So 'Smith, John - Novel.epub' (Calibre-style author ordering) and 'Book; A Novel.epub' fail with error 26 even though the file exists. Ebook names with [Retail] or (Series #1) [2020] are common. Workarounds, measured: curl -g -T for brackets and braces (this disables the {a,b} multi-file form), and -F 'file=@"Smith, John - Novel.epub"' for commas and semicolons.

*Trigger:* In a folder holding 'Book [2020].epub', 'Series {Vol 1}.epub', 'Smith, John - Novel.epub' and 'Book; A Novel.epub': curl -T "<name>" http://HOST/books/ ; curl -F "path=/books" -F "file=@<name>" http://HOST/upload

*Evidence:* curl 8.23.0 against simulator_picco port 18613. curl -T 'Book [2020].epub' gave 'curl: (3) bad range in position 11'. curl -T 'Series {Vol 1}.epub' gave 'curl: cannot open 'Series Vol 1.epub'' (rc 26). curl -F with 'Smith, John - Novel.epub' and with 'Book; A Novel.epub' gave 'curl: (26) Failed to open/read local data from file/application'. In the other direction, -T worked for the comma and semicolon names (201), and -F worked for the bracket and brace names (200). curl -g -T gave 204 for both bracket and brace names, and the quoted -F form reached the server (400 'File already exists', so the name came through intact).

### R-11 [blink] A file name of 249-255 characters fails curl -T with a misleading '500 Write failed', while curl -F accepts it

**status: FIXED** (fw ba40f8cb7) · fw `src/network/WebDAVHandler.cpp:93`

WebDAV PUT writes to _putPath + ".davtmp" first (WebDAVHandler.cpp:93). That temp name is 7 bytes longer than the real one, so it goes over the 255-unit long-name limit (SdFat FAT_MAX_LFN_LENGTH = 255, FsStructs.h:354, checked in FatFileLFN.cpp parsePathName; on the simulator, the macOS 255-byte limit) for names the card could hold. openFileForWrite fails, and handlePut answers 500 'Write failed - incomplete upload or disk full' (:439). The doc's trap 1 says that 500 means a missing trailing slash or a missing folder, so the user is sent the wrong way. POST /upload writes directly to the final name and takes the same file.

*Trigger:* curl -T with a 250-character name into /books/, then curl -F with the same-length name

*Evidence:* Measured on simulator_picco port 18613. Name lengths 245, 247 and 248 gave -T 201. Lengths 250 and 255 gave -T 500. -F with names of 246-251 characters gave 200, and only 256 gave 400. On the device the same arithmetic holds from source: '.davtmp' plus 249 or more units exceeds FAT_MAX_LFN_LENGTH 255. Not run on a Picco.

### R-12 [blink] On the Picco, names with : ? " * < > | are refused with misleading errors; the simulator accepts them, so neither the doc nor the gate shows it

**status: FIXED** (fw ee54dea76, 5373b207d, 6ea93b2bf) · fw `docs/upload-from-blink.md:104`

FsHelpers::isSafePathComponent (lib/FsHelpers/FsHelpers.cpp:333) rejects only '/', '\', '.' and '..'. SdFat rejects the FAT long-name reserved set (FsStructs.h:108 lfnReservedChar, enforced in FatFileLFN.cpp:476 parsePathName with USE_UTF8_LONG_NAMES=1). So on the Picco, curl -T of 'Dune: Messiah.epub' fails at openFileForWrite (WebDAVHandler.cpp:94) and answers '500 Write failed - incomplete upload or disk full'. The doc attributes that error only to a missing trailing slash or folder. curl -F of the same name answers '400 Failed to create file on SD card'. The simulator stores the name on macOS and answers 201 or 200, so a desktop run passes where the device fails. The doc's rules table and trap list do not mention reserved characters.

*Trigger:* On a Picco: curl -T "Dune: Messiah.epub" http://crosspoint.local/books/ (likewise for names containing ?, ", *, <, > or |)

*Evidence:* Simulator, measured: 'Dune: Messiah.epub', 'Why? Not.epub' and 'Say "Hi".epub' all gave 201 and landed on the card. Device, read from source only: lfnReservedChar includes '"' '*' ':' '<' '>' '?' '\\' '|'. parsePathName jumps to fail on them, so vol().open(path, O_RDWR|O_CREAT|O_TRUNC) in SDCardManager.cpp:377 returns false, then _putOk=false and handlePut sends 500. Not run on a Picco.

### R-13 [blink] iOS app and simulator: uploads over 256 MB get 413, and a curl -F upload peaks at about 4.4x its size in RAM

**status: FIXED** (sim b0f5660f, 892f0fe7) · sim `src/WebServer.cpp:675`

The shim refuses any body over MAX_BODY_SIZE = 256 MB (WebServer.cpp:30, :675) with 413. The Picco has no such cap. The doc presents the iOS app route as 'the same commands' and never mentions the cap. A multipart POST is buffered whole: currentBody (:766), then each part is copied again (part.data, :292). The measured peak was about 4.4x the file size. At the 256 MB cap that is roughly 1.1 GB, which risks an iOS memory kill of the app mid-upload. That risk is inferred and was not run on a phone. curl -T streams, so it is the safer route for large files on the app.

*Trigger:* curl -T of a 300 MB file to the simulator or app; curl -F of a 100 MB file while sampling the process RSS

*Evidence:* Measured on simulator_picco port 18613. The 300 MB curl -T answered 'Payload Too Large' with 413 in 0.014 s. The 100 MB curl -F answered 200 with md5 match, and the sampled RSS went from 30,448 KB to a 469,152 KB peak.

### R-14 [blink] test_blink_upload's FAIL lines still say 'bytes match'

**status: FIXED** (sim 6a42ece5) · sim `tests/test_blink_upload.sh:117`

The check messages are fixed strings ending in 'bytes match' (lines 99, 106, 117, 122), so a failing check reports success-sounding text.

*Trigger:* Any failing PUT or POST check, for example with POST /upload removed

*Evidence:* Mutation M3 printed 'FAIL: curl -F path then file -> 404 Not Found ... [404], bytes match'.

### R-15 iOS: the sleep screen keeps the lit page's tones for the whole sleep, though the light is off

**status: PARTLY FIXED** (sim e4982508, light page only; the dark-mode arm is still open (F-10 below)) · sim `ios/CrossPointIOSShim.cpp:4858`

The firmware draws the sleep screen while the light is still on, then switches the light off. On iOS the page only learns about light changes from the per-frame polls (pollPanelPalette, pollPanelGlow, pollScanlines), and those do not run while the firmware sleeps. So the sleep screen is presented in the lit tones and stays that way for the whole sleep. That breaks the model's own rule that with the light off the page is the frozen page (docs/frontlit-page-2026-11-27.md:28-31, src/FrontlitPage.h:15-17). The desktop as-shipped build goes the other way: its change hook repaints the frozen tones before sleep. So the two platforms disagree on the sleep screen. Fix directions, for whoever acts on this: have sleepTick run pollPanelPalette, pollPanelGlow and pollScanlines, or install simfrontlight's change hook on iOS too.

*Trigger:* Picco iOS app. Light on at any setting other than 60/50 (for example Warmth 100, Brightness 100). Light page, or dark page with the power-off collapse off. Press POWER or let the sleep timeout fire. The sleep screen is drawn and held on the lit sheet (paper FFF1C2 at warmth 100, per the doc's table) instead of the frozen F9F3E9, until the wake. If the light is restored on wake, nothing visibly changes after the wake. If it is not restored, the palette corrects only once CrossPointHarness_begin runs applyTheme after setup().

*Evidence:* Verified against source; not run (the doc says iOS glass captures were stale). 1) Firmware src/main.cpp:790 activityManager.goToSleep() draws the sleep screen, and main.cpp:812 Frontlight.setOn(false) comes after it. 2) Simulator src/HalFrontlight.cpp:82-85 setOn publishes off (generation++, hook only if installed; iOS installs none). 3) iOS reads the light only in perFrame: CrossPointIOSShim.cpp:5045-5046 (pollPanelPalette, pollPanelGlow) and :5339 (pollScanlines). 4) perFrame is called only from the main while-loop (src/simulator_main.cpp:475). The sleep loop src/HalGPIO.cpp:2268-2365 never returns. Its host tick sleepTick (CrossPointIOSShim.cpp:4858-4862) runs only CrossPointOrientation_poll, repaintAfterForeground and presentIfNeeded, with no palette, glow or bloom poll. The shim's own comment at :4842-4845 says perFrame stops for the whole sleep. repaintAfterForeground contains no applyTheme, applyPanel or setPanel call (grep). 5) Desktop contrast, by reading: hook HalDisplay.cpp:2446 -> applyFrontlitPage -> setPanelPalette sets pendingReconvert and requestPresent (HalDisplay.cpp:2102-2117), then HalDisplay::deepSleep runs presentIfNeeded (HalDisplay.cpp:5275-5276), so the desktop's sleep frame is the frozen one.


## latent (3)

### R-16 minify_js silently rewrites multi-line template literals; the syntax-only gate cannot see it

**status: OPEN** · fw `scripts/build_html.py:38`

minify_js drops every blank line and every line whose stripped text starts with `//`, and it does not know when it is inside a template literal or a multi-line string. A blank line, or a line such as `//cdn...` or a JS snippet, inside a backtick string is deleted. The script still parses, so check_inline_scripts (:93-117) passes, and the page then produces different strings than its source. FilesPage has 55 multi-line template chunks (147 lines), which it uses to generate EPUB XML/CSS, so the exposure is real. None of them is affected today.

*Trigger:* A future edit puts a blank line or a `//`-leading line inside one of FilesPage's multi-line template literals.

*Evidence:* Fixture run through the real minify_html + check_inline_scripts: the template ``body {\n margin: 0;\n\n // not a comment, part of the string\n}`` came out as ``body {\n margin: 0;\n}``, and the gate printed 'passed'. The current pages are clean: an acorn token-stream comparison (template cooked values included) of source vs minified found 0 differences on all four pages.

### R-17 The gate's `node --check` on a .js temp file accepts top-level await, import and return, which a browser <script> rejects

**status: OPEN** · fw `scripts/build_html.py:112`

check_inline_scripts writes each inline script to a `.js` temp file and runs `node --check`. Node v25 detects module syntax and retries as ESM, and it wraps CommonJS in a function. As a result it accepts top-level `await`, `import`, and a top-level `return`. All three are SyntaxErrors in a classic browser script, where the whole script then fails to run (B-080's symptom). Minification cannot introduce these constructs, so the gate still does its stated job. But the build reports 'parses' for a page the browser will not run. Parsing with `new vm.Script(src)` matches the browser's classic-script goal: it rejects all three. A `.cjs` suffix rejects await but still accepts return. No current page uses any of these.

*Trigger:* A page script with e.g. `const r = await fetch('/api/files');` at top level builds green and does not run on the phone.

*Evidence:* node v25.9.0: `node --check` returns rc=0 for `await fetch("/x");`, `import x from "/y.js";` and `return 1;` in .js files. `new (require('vm').Script)(src)` returns rc=1 for all three. `node --check x.cjs` returns rc=1 for await and rc=0 for return.

### R-18 glass.py cannot follow a press whose frame is replaced by a gray compose, and can credit it with a later press's frame

**status: FIXED** (fw d9d11eddc (documented: glass.py is for 1-bit screens only)) · fw `tools/latency/glass.py:43`

glass.py links presses to frames only through the simulator's `[lat] written <seq>` lines. The simulator emits `written` only from refreshDisplay, the 1-bit path (crosspoint-simulator src/HalDisplay.cpp:2588). composeGrayscalePreview (HalDisplay.cpp:1161) bumps pixelBufSeq under a new seq and emits no `written` line; displayGrayBuffer notes only `gray-display` with arg -1 (HalDisplay.cpp:5344). On a screen whose gray pass runs inside render(), the 1-bit frame is held and replaced, so its seq never gets a `glass` line, and the composed seq that does reach the screen was never `written`. glass.py:43-45 then takes the first LATER written seq that appears in `glass`, which may belong to a later press. glass.py:54 lists the press's own frame as 'never on glass', and the docstring at line 14 calls that 'coalesced away (the selection skipped it)'.

*Trigger:* Run tools/latency/glass.py on a CROSSPOINT_SIM_LOG_LATENCY=1 log that steps through a screen with an in-render gray pass, such as the Colophon or the font picker specimen (the doc's O2 row says the Colophon showed 5 composed frames and no 1-bit one). Each step shows as '-' and is counted as 'never on glass'. If the run then returns to a 1-bit screen, the last gray-screen press gets the next screen's frame and latency.

*Evidence:* Read only; I did not run it. glass.py:43 `seqs = sorted(s for s, wt in written.items() if wt >= t)` and :45 `shown = next((x for x in seqs if x in glass), None)`. The only `simlat::note("written"` call is at simulator HalDisplay.cpp:2588, inside refreshDisplay. composeGrayscalePreview has `const uint64_t seq = ++pixelBufSeq;` and no simlat note. The Settings measurements in the doc are not affected, because those menu steps are 1-bit.


## test-gap (4)

### R-19 No test anywhere shows the Picco's 2 s idle power-off ever fires

**status: OPEN** · fw `src/main.cpp:1557`

O5 left the SSD2677 powered after every B/W refresh (freeink-sdk Ssd2677Driver.cpp:106 now passes turnOff through). The only thing that turns it off during normal use is the firmware chain: noteRefresh() at the six HalDisplay entry points (HalDisplay.cpp:82,96,115,173,209,225), then panelIdlePowerOffDue() (HalDisplay.cpp:250-257), then the loop block at main.cpp:1556-1560, then powerOffIdlePanel(). No test runs any link of that chain. The firmware host tests link a stub HalDisplay (test/cmake/HostThemeStack.cmake:19), so lib/hal/HalDisplay.cpp is never compiled there. The simulator's mirror is `bool panelIdlePowerOffDue(uint32_t) const { return false; }` and `void powerOffIdlePanel() {}` (crosspoint-simulator src/HalDisplay.h:166-167), so simulator_picco compiles the main.cpp branch but never takes it. The SDK's test_picco covers only the driver's PON/POF order. If an entry point lost its noteRefresh(), or the block moved below one of loop()'s early returns, every test would stay green and the device would keep the panel powered with no limit. Nobody would see that, only feel it in battery life. The device is UNCONFIRMED by the lag doc's own account. The global rule for a feature that must 'fire' asks for a proof that it runs. The decision (due after N ms since the last refresh, cleared by the power-off, re-armed by the next refresh) is pure and could be pinned by a host test of HalDisplay's three functions with a fake millis().

*Trigger:* Any future edit to HalDisplay's refresh entry points or to the order of main.cpp loop(). Nothing off the device would catch the panel staying powered.

*Evidence:* `grep -rln 'panelIdlePowerOffDue|powerOffIdlePanel|noteRefresh|lib/hal/HalDisplay.cpp' test` returns nothing. test/cmake/HostThemeStack.cmake:19 provides `HalDisplay display;` as a host stub. crosspoint-simulator src/HalDisplay.h:166-167 holds the no-op mirror from commit ee0690d3 ('A host has no panel rails, so nothing is ever due'). freeink-sdk test_picco.cpp, as changed in 666ec93/119f3ed, asserts only the driver-level command order.

### R-20 No test covers the Picco device Settings list (Font Size presence or position)

**status: OPEN** · fw `src/activities/settings/SettingsActivity.cpp:250`

The Reader Font Size row is admitted on the Picco by name and moved to index 1 with std::rotate. Nothing asserts the Picco device list's contents or order. A future rename of the row's nameId, or a filter change, would silently remove the only way the Picco can change the reading size (the reason given in the commit).

*Trigger:* Any change to STR_FONT_SIZE, to isPiccoDeviceRow, or to the list construction.

*Evidence:* A grep of test/ for SettingsActivity|isPiccoDeviceRow|STR_FONT_SIZE finds only include-chain comments (test/reader_typography_controls/CMakeLists.txt:4,50 and test/sleep_screen/SleepScreenPolicyTest.cpp:18,119). The repo records no headless verification of the row (grep of docs/TODO/BUGS).

### R-21 scanlines_dirty never checks the upload box on the edits that matter, so a wrong box passes

**status: OPEN** · sim `tests/scanlines_dirty_test.cpp:159`

The shipping path uploads only the Rect that scandirty::refill returns (src/SurfaceTube.cpp:420-425, SDL_UpdateTexture(scanTexture, &r, ...) out of scanFieldPx). The test checks that Rect only for a single pixel (line 112) and for a whole-field edit (line 141). The menu-step case (line 131) and the 600 chained random edits (line 159) compare only the in-memory kept field, which refill writes pixel by pixel whatever box it reports. So a box that misses changed pixels leaves the texture stale on screen while every check passes. letterpress_dirty does not have this gap, because its patched() copies through the returned box.

*Trigger:* Any refill bug that computes the box wrong for an edit spanning several rows, for example minX tracked only on the first changed row. The kept field stays correct, the uploaded box is too narrow, and stale scanline pixels stay on the dark page.

*Evidence:* Mutated a scratch copy of src/ScanlinesDirty.h so that line 112 `if (x < minX) minX = x;` became `if (minY == h || y == minY) { if (x < minX) minX = x; }`. scanlines_dirty_test still printed 'all checks passed (600 chained edits)', exit 0. A probe on that mutant with changed pixels at (7,2) and (1,5) returned rect x=7 w=1, which misses (1,5). Scratch: /private/tmp/claude-501/-Users-natebunnyfield-src/2f7ddd19-354b-4b40-85d2-4aff02275054/scratchpad/passes/tests-docs-p1/mut/sl_rect/. The same harness caught the letterpress no-growth mutant (313 failures) and both glass_capture mutants.

### R-22 frontlit_page floor and direction gates skip half the device's reachable settings

**status: OPEN** · sim `tests/frontlit_page_test.cpp:86`

The test comment and run_all.sh say every setting clears 7:1 'over the whole grid'. The grid steps by 2 on both axes (lines 86-87). The firmware rows step by 5 (SettingsList.h:790-793, {0,100,5}), so 5, 15, 25 ... 95 on each axis are never checked: 121 of the 441 reachable settings are covered. The warmth-direction loop also checks only Brightness 0/25/50/75/100. A future retune that dips under the floor, or reverses warmth, only at an odd multiple of 5 would pass the gate. Today the model is clean. A probe found 0 floor failures over all 10,201 integer settings (worst 7.000 light/dark) and 0 direction failures over the full 5-step device grid, so this is latent.

*Trigger:* A tuning change (kInkDepthMax, kBeamMax, kGunWarmthRatio, the paper ends) whose failure lands only on Brightness or Warmth values 5, 15, ... 95.

*Evidence:* tests/frontlit_page_test.cpp:86-87 `b += 2`, `w += 2`; :109,:123 outer loops `+= 25`. Probes in /private/tmp/claude-501/-Users-natebunnyfield-src/2f7ddd19-354b-4b40-85d2-4aff02275054/scratchpad/passes/tests-docs-p1/floor_probe.cpp and probe4.cpp.


## doc-wrong (10)

### R-23 Two comments still say the host idle slice is 50 ms after O3 made it 20 ms

**status: FIXED** (sim e3f72238) · sim `src/simulator_main.cpp:439`

acf338aa set the host light-sleep slice to 20 ms (HalPowerManager.h:48, kHostIdleSliceMs). 36206135's 'Stale idle-slice comment' fix updated only SimulatorIdle.h. Two other comments still describe a 50 ms slice: simulator_main.cpp:439-440 ('the firmware's 10 ms polls and the 50 ms idle slices') and HalGPIO.cpp:251 ('a 50 ms idle slice is a plain delay that pumps nothing'). The HalGPIO one is part of the reasoning for the Picco touch-lift deferral, so a reader sizing that window from it gets the wrong number.

*Trigger:* Reading either comment while reasoning about idle input or present timing on the host.

*Evidence:* grep -n -i 'slice\|50 ms' src/HalGPIO.cpp src/simulator_main.cpp returns HalGPIO.cpp:251 and simulator_main.cpp:439. HalPowerManager.h:48-51 is `static constexpr unsigned long kHostIdleSliceMs = 20; ... delay(kHostIdleSliceMs);`. git show 36206135 --stat lists SimulatorIdle.h and does not list these two files.

### R-24 The comments and the O2 write-up say every gray pass runs inside render(); the sleep screen's gray passes do not

**status: FIXED** (fw 0359754b3, sim 75fbabe7) · fw `src/activities/ActivityManager.cpp:61`

ActivityManager.cpp:61 ("Every gray pass runs inside render(), so the simulator's present hold can stop waiting for one here"), lib/hal/HalDisplay.h:191 ("every gray pass runs inside render(), so once render() returns with none, the hold can let the frame out") and docs/menu-nav-lag-2026-11-27.md:125 (O2, "since every gray pass runs inside render()") all state an invariant the firmware breaks. SleepActivity has no render() override (the base Activity::render at Activity.h:34 is an empty no-op). It draws its covers from onEnter() on the loop task: displayGrayscaleBase, then copyGrayscaleLsb/MsbBuffers, then displayGrayBuffer at SleepActivity.cpp:368-392, 767-794 and 841-862. processPendingTransitions releases the render lock before onEnter (ActivityManager.cpp:186, "onEnter may acquire its own lock"), so those passes also run outside the lock. On the device renderPassComplete() is an inline no-op, so nothing misbehaves on the Picco. The cost is what a reader takes away. The second review's finding 4 had to add the compare-and-swap in presentheld::releaseAtPassEnd precisely because this invariant is false. A comment that states it as true invites someone to simplify that guard away, and the guard is the only thing that keeps the sleep cover's 1-bit base from flashing in the simulator and apps.

*Trigger:* Any sleep screen drawn with gray (cover, bitmap or transparent overlay). Its displayGrayBuffer() runs from SleepActivity::onEnter on the main task, never inside render().

*Evidence:* grep 'every gray pass|Every gray pass' gives lib/hal/HalDisplay.h:191, src/activities/ActivityManager.cpp:61 and docs/menu-nav-lag-2026-11-27.md:125. grep Gray in src/activities/boot_sleep/SleepActivity.cpp shows renderer.displayGrayBuffer() at :392, :794 and :862. SleepActivity.h declares no render(), and Activity.h:34 is `virtual void render(RenderLock&&) {}`. ActivityManager.cpp:186 calls lock.unlock() before currentActivity->onEnter(). The lag doc's own review, finding 4, says 'SleepActivity draws its gray covers on the main thread outside the render lock'.

### R-25 Doc: 'with the switch off, the page is the frozen page' is false when the switch is bound to Dark Mode

**status: FIXED** (sim 598db51c) · sim `docs/frontlit-page-2026-11-27.md:34`

The doc says the light follows the pad's slide switch and that with the switch off the page is frozen whatever the rows say. That holds only for the default binding (SLIDE_SWITCH_FRONTLIGHT). If Slide Switch is set to Dark Mode, the switch flips darkMode, and the light follows the Frontlight row and Restore Light on Wake instead. With the switch off, the page then shows the light page lit whenever the Frontlight row is on. The page code is right here; only the doc is wrong. It matters because the owner is judging the look from this doc.

*Trigger:* Picco: Settings > Slide Switch = Dark Mode, Frontlight row On, Brightness or Warmth away from 60/50, pad switch off. The light page renders lit, not frozen.

*Evidence:* Firmware src/main.cpp:707-723 handleSlideSwitch: the NIGHT_MODE case sets only darkMode and setInverted, and only the default case calls Frontlight.setOn. src/activities/settings/SettingsActivity.cpp:87-91: the Frontlight and Restore rows are shown exactly when the switch does not own the light, and :403-405 toggles Frontlight.setOn. src/util/SlideSwitch.h:18 slideSwitchOwnsLight checks SLIDE_SWITCH_FRONTLIGHT only. Simulator publish (src/HalFrontlight.cpp:23-32) follows the HAL whatever drove it.

### R-26 Picco list-order comments omit the new Reader Font Size row

**status: FIXED** (fw b770b51d0) · fw `src/activities/settings/SettingsActivity.cpp:133`

At :132-136 the comment says the isPiccoDeviceRow rows 'arrive in getSettingsList() order after Clock UTC Offset: Brightness, Warmth, Frontlight, Restore Light on Wake, Slide Switch'. isPiccoDeviceRow (:74) now also admits Font Size, which arrives FIRST: getSettingsList pushes FONT_FAMILY and then FONT_SIZE before everything else (SettingsList.h:361-365). The 'THE HEAD OF THE LIST' block (:199-212) calls itself 'the only place it is written down' and lists Reader Font, Typography Settings, Editor Font... On the Picco the rotate at :250 puts Reader Font Size at index 1. The same comment records that an earlier stale version 'sent a reader to the owner with a wrong question about ordering'.

*Trigger:* Someone reads the comment to learn the Picco Settings order.

*Evidence:* cf1bba014 changed only :63-74 and added :242-252. The comments at :132-136 and :199-212 were not touched (git show cf1bba014).

### R-27 'Scanlines full from Brightness 10' holds only at mid Warmth; warm settings stay thinned

**status: FIXED** (sim 598db51c) · sim `docs/frontlit-page-2026-11-27.md:92`

The doc (lines 90-93) and the src/FrontlitPage.h header (lines 48-53) say the dark page's scanlines fall to about 6% of their depth at Brightness 0, about 80% at 5, and are full from 10, with no qualifier. That is true at Warmth 50 only. Warmth moves the trace and the glass, so the contrast headroom that caps scanlines::effectiveDepth also moves. At warm settings the cap reaches higher up the Brightness row, on values the device's 5-step rows can reach.

*Trigger:* Picco with Warmth 90-100 and Brightness 0-15, dark page.

*Evidence:* Probe using the same chain as SurfaceTube.cpp:340-343 and Scanlines.h:297-302: budget = kRasterBudgetShare * darkeningBudget(relLum(ink), relLum(paper)), then effectiveDepth at intensity 50, compared against the frozen pair (0.2000, uncapped). Results as a percent of frozen depth. w=50: b0 6%, b5 80%, b10 100% (matches the doc). w=100: b0 0%, b5 35%, b10 67%, b15 95%, b20 100%. w=90: b10 79%. w=0: b0 10%, b5 100%. Source: /private/tmp/claude-501/-Users-natebunnyfield-src/2f7ddd19-354b-4b40-85d2-4aff02275054/scratchpad/passes/tests-docs-p1/depth.cpp

### R-28 menu-nav-lag doc's opening still says no firmware changed and nothing was built

**status: FIXED** (fw 28db20f6d) · fw `docs/menu-nav-lag-2026-11-27.md:11`

The doc's own later rows record built firmware changes, but its opening sections were never updated. Line 11-12 says '**No firmware source changed.**', yet fd49312eb (ButtonNavigator), 5bc238009 (ActivityManager renderPassComplete), 91e4c987c (HalDisplay + main.cpp) and 4998d84da (main.cpp) all changed it. Line 120's heading reads 'Decisions for the owner (nothing built)', but five of its six rows say 'built'. The one-paragraph answer (lines 31-35) still says a held rocker 'jumps a whole screen every 500 ms and wraps' and the device does 'a panel power-on and power-off per step'. Both are false on the Picco since O1 and O5. The platform table (line 42) still gives the host idle poll as delay(50); it is 20 ms since acf338aa. A reader who stops at the summary gets the pre-ruling state.

*Trigger:* Anyone reading the top of the doc, which is the part summaries and later sessions quote.

*Evidence:* git log 2c9136128..HEAD shows the four firmware source commits. Line 120 heading is against row text 'RULED ... built' at lines 124-129. src/util/ButtonNavigator.h kHoldPagingWraps=false under FREEINK_DEVICE_PICCO. freeink-sdk Ssd2677Driver.cpp display() now passes turnOff. crosspoint-simulator src/HalPowerManager.h kHostIdleSliceMs = 20.

### R-29 Stale line citation SurfaceTube.cpp:310-313 in the frontlit doc

**status: FIXED** (sim 598db51c) · sim `docs/frontlit-page-2026-11-27.md:91`

The doc cites `src/SurfaceTube.cpp:310-313` for the scanline depth budget. b59b6455 (O6) later inserted the resident-buffer statics and the partial predicate above it, so the budget lines are now 340-343. Lines 310-313 now hold the bloom/defocus key loads and the start of the cache-hit check.

*Trigger:* Following the citation at HEAD.

*Evidence:* `git show 9e2f2645:src/SurfaceTube.cpp | sed -n 305,316p` shows the params/budget block. At HEAD `grep -n 'budgetMeanDarkening =' src/SurfaceTube.cpp` gives 340, and sed -n 308,314p shows pitchKey/bloom/cd/seq/cache check.

### R-30 ButtonNavigator.h still says nextPageIndex/previousPageIndex wrap

**status: FIXED** (fw 693f95a39) · fw `src/util/ButtonNavigator.h:74`

The side-button paging comment says pageDown/pageUp are 'Distinct from nextPageIndex/previousPageIndex above, which wrap and snap to the page boundary'. fd49312eb made them not wrap on the Picco (kHoldPagingWraps=false, lines 58-72 of the same header). The comment now contradicts the one directly above it.

*Trigger:* Reading the header to decide which paging helper a Picco list should use.

*Evidence:* src/util/ButtonNavigator.h:63-67 (#if FREEINK_DEVICE_PICCO kHoldPagingWraps = false) against :74-75. ButtonNavigator.cpp:132-133 `return wrap ? 0 : totalItems - 1;`.

### R-31 Simulator CLAUDE.md still prices a menu keypress at 51-53 ms of panel field

**status: FIXED** (sim ea7ce32f) · sim `CLAUDE.md:875`

CLAUDE.md:875 says navigation within a screen costs '51–53 ms of panel field per keypress'. 95c2e568 now rebuilds the letterpress field only inside the changed box. The menu-nav doc measures 5.5 ms per step for a 194x866 box (9.7 ms with the inkness pass). The guide future sessions read now overstates the per-step cost about tenfold.

*Trigger:* A later session budgeting menu latency from CLAUDE.md.

*Evidence:* CLAUDE.md:872-876 against src/SurfaceSheet.cpp:452-466 (partial/changedRect) and firmware docs/menu-nav-lag-2026-11-27.md:72.

### R-32 Frontlit write-up cites SurfaceTube.cpp:310-313 for the scanline depth cap; those lines are now the cache-key check

**status: FIXED** (sim 598db51c) · sim `docs/frontlit-page-2026-11-27.md:91`

The doc says the dark page's scanline depth is capped by the contrast headroom at `src/SurfaceTube.cpp:310-313`. That was right when the doc was written (9e2f2645, where params.budgetMeanDarkening sat at line 312). The later O6 commit b59b6455 added the partial-refill block above it. At HEAD, lines 310-313 are the pitchKey/bloom/defocus/seq lines of the full-match check, and the budget is set at SurfaceTube.cpp:340-343. The other half of the citation, Scanlines.h:298-302 (effectiveDepth), is still correct.

*Trigger:* A reader follows the citation to check why the scanlines go flat at Brightness 0.

*Evidence:* `git show 9e2f2645:src/SurfaceTube.cpp | sed -n 308,314p` shows the budget assignment. At HEAD, `grep -n budgetMeanDarkening src/SurfaceTube.cpp` gives 340, and `sed -n 306,316p` shows the key check.


## cosmetic (4)

### R-33 Deferred glass readback (rule 3) loses the afterglow of a fully swept frame when the next frame arrives within one present

**status: OPEN** · sim `src/HalDisplay.cpp:4553`

Rule 3 (734d887a) moved the glass readback from the present where frame N's sweep finishes (P, beamFinishedHere at HalDisplay.cpp:4155, so deferToNext is passed at :4659) to the next present (P+1, requested at :5192). If the render task writes frame N+1 between P's pixel-lock section and P+1's, the capture still holds N-1. At P+1, captureIsCurrent (:3682) is false, so N+1 lands whole (:3698-3700). That is the half the earlier review accepted. The trail deposit is also skipped: freshGlass (:4553) compares depositedGlassSeq, which was set to N-1 when N arrived (:4593), with glassSeq, still N-1. P+1 defers its capture again because it landed whole, and P+2 captures N+1 (:4663). So N, which was wholly on the glass after its sweep reached the foot, is never captured and never deposited. On a dark page it leaves no afterglow and goes out at once while the frames before and after it fade. Before 734d887a the old shouldCapture had no deferToNext, so P captured N before its flip; N+1 then swept over N and deposited it. The firmware doc's review (docs/menu-nav-lag-2026-11-27.md:172-176) accepts only 'a frame can land whole where it used to sweep', and :182 lists 'no double or missing phosphor deposit' as checked clean, which is wrong for this window. GlassCapture.h:57-58 says 'The next present composes the same glass from cache, so it reads back the same picture', which is also false here: the next present carries N+1. Owner decision (nothing proposed is built): the four options and their costs are under "R-33: options awaiting the owner" at the foot of this file. Both false statements above were corrected on 2026-11-27 (fw 8cb0d7be3, sim 3e35db04); the defect itself is not fixed.

*Trigger:* Dark (tube) page with the phosphor trail on (every CRT palette) and the beam on (the Picco app's 55 ms). A menu step's 1-bit write lands after the present where the previous step's sweep reached the foot of the glass but before the next present runs. This is the same one-present window the lag doc already counts as 'landed whole': the hold armed by the new write blocks P+1 until the render pass ends, and P+1 then sees the new frame.

*Evidence:* Traced in code; since seen once on the iOS Simulator (see "The owner's fade recording" below). glassSeq is written in only one place, the capture at HalDisplay.cpp:4663; destroyGlassTextures (:2848) only resets it. The capture is suppressed at :4656-4659 whenever landedWhole || beamFinishedHere (GlassCapture.h:62). freshGlass at :4553 is depositedGlassSeq != glassSeq, and depositedGlassSeq is assigned only at :4593. The sequence N-1 captured -> N arrives (deposit N-1, sweep) -> sweep ends at P (capture deferred) -> N+1 written before P+1 -> P+1: captureIsCurrent=false, landedWhole=true, freshGlass=false -> P+2 captures N+1 never deposits N. The firmware doc (docs/menu-nav-lag-2026-11-27.md:172-176) states and measures the window itself. The four related host tests pass (present_hold, glass_capture, letterpress_dirty, scanlines_dirty); none covers this wiring, and the doc's line 167 already records that gap as accepted.

### R-34 British spelling 'neighbour' in two of today's files

**status: FIXED** (sim 1c28fcd1) · sim `src/ScanlinesDirty.h:16`

'Nothing reads a neighbour's bucket' in src/ScanlinesDirty.h:16, and 'no neighbour' in tests/scanlines_dirty_test.cpp:105. Both were written today (b59b6455). The rest of today's added lines in all three repos scanned clean.

*Trigger:* n/a

*Evidence:* grep -n neighbour src/ScanlinesDirty.h tests/scanlines_dirty_test.cpp. A British-spelling regex over every added line of 2c9136128..HEAD, 4c95ebd9..HEAD and e05bb17..HEAD hit only these two.

### R-35 Two simulator comments still describe a 50 ms host idle slice after O3 made it 20 ms

**status: FIXED** (sim e3f72238) · sim `src/simulator_main.cpp:439`

acf338aa set HalPowerManager::kHostIdleSliceMs to 20 and fixed the SimulatorIdle.h comment. Two other comments still say 50 ms: src/simulator_main.cpp:439 ('the firmware's 10 ms polls and the 50 ms idle slices') and src/HalGPIO.cpp:251 ('a 50 ms idle slice is a plain delay that pumps nothing'). The reasoning in both still holds at 20 ms; only the number is stale.

*Trigger:* Reading the idle/pump comments when debugging input timing on the host.

*Evidence:* src/HalPowerManager.h:48 `static constexpr unsigned long kHostIdleSliceMs = 20;` against the grep hits at simulator_main.cpp:439 and HalGPIO.cpp:251.

### R-36 ButtonNavigator.h still says the hold page jumps 'wrap', which is no longer true on the Picco

**status: FIXED** (fw 693f95a39) · fw `src/util/ButtonNavigator.h:75`

The side-button paging comment says nextPageIndex/previousPageIndex 'wrap and snap to the page boundary'. Since fd49312eb they take `wrap = kHoldPagingWraps`, which is false on the Picco (O1), and they then stop on the last or first row. The comment a few lines above the declarations states the Picco exception, so the stale clause conflicts with it.

*Trigger:* A reader compares pageDown/pageUp with the hold paging on the Picco.

*Evidence:* ButtonNavigator.h:58-72 (kHoldPagingWraps and its comment) against :74-76. ButtonNavigator.cpp:132-133 now returns `wrap ? 0 : totalItems - 1`.


## Refuted (1)

- iOS CI builds the pinned firmware 1b7fc3b26, which still ships the broken File Manager and has no script gate (`cmake/CrossPointSources.cmake`). The facts check out, but nothing here is broken code. It is the designed, documented behavior of a workflow nobody uses. What is true: - With a blank firmware_ref, testflight-ios.yml:136-146 does resolve CROSSPOINT_FIRMWARE_PIN, which is 1b7fc3b26 (CrossPointSources.cmake:14). - `git merge-base --is-ancestor 954d63187 1b7fc3b26` returns 1, so the pin lacks the fix. 589fddb9d, the commit that caused it, is an ancestor of the pin. - The cmake files run no build_html.py. Neither CMakeLists.txt nor cmake/*.cmake calls it or gen_i18n. Why it is not a defect: 1. Pinning is the stated design. The workflow input says a blank ref means "the commit pinned in cmake/CrossPointSources.cmake, which is the only tree the generated source set is guaranteed to match". Building newer firmware means passing firmware_ref, so a pin build lacks every firmware change since 2026-11-08. B-080 is not special, and the review's commits caused none of it. 2. The CI path is dormant. `gh run list -R natebunnyfield/crosspoint-simulator --workflow testflight-ios.yml` shows the last run on 2026-09-12, and the last three runs failed. Every build since then shipped from ios/testflight.sh; tag build-380 points at 4c95ebd9 (2026-11-26). The trigger means dispatching that workflow, which this task forbids anyway. 3. The path that actually ships regenerates the headers through the fixed script. ios/testflight.sh runs `pio run -e $CANARY_ENV` in FIRMWARE_DIR before the archive. The simulator env lists pre:scripts/build_html.py (platformio.ini:513). build_html.py has no mtime or up-to-date skip; it rebuilds every time and writes in-tree to src/network/html/*.generated.h (gitignored). The iOS cmake build compiles those files. So once the firmware checkout holds 954d63187, the next TestFlight run builds the fixed script with its node gate. 4. The manual cmake/Xcode route in ios/README.md is a local developer build that ships nothing. It reads generated outputs from the firmware tree, the same way it reads I18nStrings.cpp, and the next pio run refreshes them. 5. The primary checkout at 2730016ae is checkout state, as the finding itself says. It is fixed by pulling, which this task forbids doing to primary checkouts. At most this is a release-hygiene note: bump the pin or pass firmware_ref the next time CI is used. It is not a bug. The mechanism is real, but it does not reach any shipped build, and the severity is overstated. What holds up: - `git merge-base --is-ancestor 954d63187 1b7fc3b26` returns 1, so the pin lacks the fix. - 589fddb9d is an ancestor of the pin, so the pin carries the bug. - testflight-ios.yml:136-146 resolves a blank firmware_ref to CROSSPOINT_FIRMWARE_PIN, then :164-169 runs that tree's build_html.py. Why it does not reach a shipped build: 1. **The iOS Picco app cannot come from this workflow.** testflight-ios.yml has no Picco device at all: grep for PICCO, Picco and picco finds nothing. The only Picco path is `CROSSPOINT_IOS_DEVICE=PICCO ios/testflight.sh` (testflight.sh:55-80). The shipped Picco build, picco-build-15 (simulator 36206135, 2026-11-27 10:55), was built with `CROSSPOINT_FIRMWARE_DIR` set to the key-paste worktree. That is line 301 of crosspoint-simulator/build/ios-picco/CMakeCache.txt, and that worktree contains 954d63187. testflight.sh:~149 also runs the desktop canary `pio run -e $CANARY_ENV` in that firmware tree first. Through platformio.ini:510-513 (`pre:scripts/build_html.py`), that regenerates the html headers with the fixed, gated script. build/picco-deploy.log shows "desktop OK", then "source set is current (151 ... TUs)", then ARCHIVE SUCCEEDED and UPLOAD SUCCEEDED. The worktree's src/network/html/*.generated.h are dated Nov 27 11:23, after the fix. 2. **The real Picco, X4 and X3 devices are unaffected.** Every device env builds with pio, which runs the tree's own build_html.py, so they get the fix and the gate. 3. **The CI path is dormant and works as designed.** - `gh run list -R natebunnyfield/crosspoint-simulator --workflow testflight-ios.yml` shows the last dispatch on 2026-09-12, and the last three runs failed. - Every iOS build since then has been local: build-377 to build-380 sit on 4c95ebd9, and picco-build-1 to picco-build-15 were tagged by testflight.sh. - The workflow input says outright that blank means the pinned tree, "the only tree the generated source set is guaranteed to match". So every one of the 147 firmware commits since the pin is equally missing from CI. That is the documented pin contract, not a B-080 regression. - The task forbids running TestFlight, so the trigger is out of reach for this workflow. - I did not check whether simulator HEAD would even compile against the pin. The firmware HAL and settings headers moved 196 lines since then. 4. **The manual cmake/Xcode path (ios/README.md:96-102) is a developer path, not the shipping path.** On a fresh checkout the html headers are missing and the build fails to compile rather than shipping stale bytes. On a used checkout they come from a pio run of the same tree. The stale case needs a firmware pull followed by a cmake-only rebuild with no pio. 5. **The rest is checkout state, not code.** ~/src/crosspoint-reader is at 2730016ae, which lacks 954d63187 (checked). The current X3 app, build-380 at 4c95ebd9 (2026-11-26 16:04), predates the fix whichever path built it. It gets the fix the next time an X3 build runs from an up-to-date firmware tree, as any fix does. What is fair to keep, as a nit or docs item: - B-080 lists the iOS app's server as in scope and is marked FIXED. The X3 iOS app has not been rebuilt since the fix. - The CI pin and source set need a bump before anyone dispatches testflight-ios.yml again. That bump should also bring in check_inline_scripts. Neither one makes a shipped build serve a broken File Manager.


## Fix pass, 2026-11-27 (workflow wf_b1fe41f9-0af)

Fixers closed the rows marked FIXED above. Three reviewers then reviewed the fixes, and skeptics checked each of their findings: 12 reported, 11 confirmed. The six most severe were repaired and pinned by test_blink_upload checks that failed before each fix. Nothing in this section was run on a Picco, an X4 or a real iPhone. Device-side behavior comes from reading arduino-esp32 3.3.7 and SdFat.

| id | finding (pre-existing unless noted) | fix |
|----|---------|-----|
| F-01 | A POST /upload that is not multipart (bare POST, `-d`, `--data-binary`) dereferenced a null upload and rebooted the device | fw da3da10d4 (`requestIsMultipart` guard), sim 927ed8a1 (the shim makes the device's callback calls) |
| F-02 | A curl -F upload that the card stopped taking left a truncated file under its final name, and the retry was refused as "File already exists" | fw c592d0d74, sim 86b2af96 |
| F-03 | A form with two file parts was answered for the last part only | fw 4df9eac7d, sim 31e2b44b |
| F-04 | A chunked PUT (piped `curl -T`) on the device answered 201/204 with an empty file, and replaced an existing book with 0 bytes | fw 2b213e97b (refused), sim 3d08c4d2 |
| F-05 | New Folder, Rename and Move from the page answered 400 on the simulator and the iOS app, because multipart fields were parsed only on upload routes | sim 80f343b5 |
| F-06 | A '+' in a WebDAV path was read as a space, so a file named with '+' could not be fetched by its own listed href | fw 9c56e23a5, sim 93062304 |

Confirmed, not repaired (over the cap of six or low severity):

- **F-07 latent.** The protected-path check and the upload-lock check compare text without SdFat's trimming of leading spaces and trailing dots and spaces. `path=/ .crosspoint` reaches /.crosspoint (the same dot paths WebDAV already serves on purpose). On the device, `PUT /books/x.epub.` during a page upload of x.epub is not answered 423. Fix option: one FsHelpers function that trims each component the way SdFat does, used by both checks (`src/network/CrossPointWebServer.cpp:164`).
- **F-08 test-gap.** test_blink_upload FAILs instead of SKIPping when its port is held by something that does not speak HTTP, or when port+1 is taken. Use a TCP probe like test_web_server_hardening.sh:32-36.
- **F-09 test-gap.** The firmware's "no file was chosen" 400 cannot be reached on the simulator, because the shim turns a `filename=""` part into a text field (`src/WebServer.cpp:868`). Both paths answer 400 and write nothing.
- **F-10 visible-bug (the R-15 dark arm).** In an iOS dark-mode session that was in light mode with the lamp on, the sleep screen still sits on the lit paper. Only the active polarity's pair is published (`ios/CrossPointIOSShim.cpp:1894`). Every option changes the look, so this is the owner's call: (a) publish both pairs whenever the pair syncs, (b) publish both from the sleep tick only, (c) leave it.
- **F-11 cosmetic.** `docs/menu-nav-lag-2026-11-27.md:60,77` cites main.cpp:1556-1559. The block is now 1557-1560.
- **F-12, outside the ask.** POST /api/fonts/upload has the R-02 shape. A POST with no file part, after a good font upload, answers `{"ok":true}` and writes nothing. Fix: claim fontUpload.valid once, as R-02 did.

Gate on fw 9c56e23a5 / sim 93062304 / SDK 119f3ed (all green):
- Firmware ctest: 976 of 976 pass.
- `pio run` for picco (psram 6), default, simulator_picco and simulator_x3: all build.
- Simulator `tests/run_all.sh`: 106 pass, 7 skip.
- SDK run_picco.py: passes.
- iOS Simulator xcodebuild (Release): succeeds.
- build_html: clean.

The skips: three tests need the desktop `simulator` env, which the gate does not build. Four need `fs_/.crosspoint/settings.json`. Those four were run off-gate on a scratch card:
- test_web_server_hardening, test_manage_files_and_wifi_nav and test_note_editor_repaint pass.
- **test_text_entry fails, and that is pre-existing.** Its "LEFT x2 is Device Owner" walk predates commit 669167dde, which put the GitHub Token and Claude API Key rows in between, so it now types into a secret field. The fix is to retarget the walk or assert on the row's title. It is not fixed yet.


## The owner's fade recording, 2026-11-27 (after the passes)

Owner report the same day, with a screen recording from the Picco app: *"fading out is not smooth as it should be"*. Read frame by frame from list crops. In frames 451-455 the cursor is on TeX Gyre Schola and the previous page's ghost is fading. In 456-457 the same frame shows with no afterglow at all. In 458 the new frame (cursor on Libre Franklin) arrives with the old ghost back. The same cut is in the recording at frames 375, 456, 466, 474 and 499. It is not R-33: under R-33 the earlier afterglow keeps fading and only one frame's own afterglow is missing, so the screen dims less; here everything goes dark.

### R-37 The trail was clipped to the swept band, so every live afterglow below the beam vanished on a sweep's first present

**status: FIXED** (sim 2974e0d2) · sim `src/HalDisplay.cpp:4821-4828`, `src/GlassCapture.h:115`

When a new frame arms the beam, everything below the beam line shows `glassPrevTexture`, the capture of the frame being replaced. That capture is read back before the accumulator is drawn (`captureGlass`, `HalDisplay.cpp:2855`, called at :4662; the comment above it explains why), so it holds no afterglow. The trail was clipped to the swept band whenever the beam ran. On the first sweep present, then, every afterglow still fading below the beam went to the bare page, and it came back only as the beam passed. The owner's list sits about 56-71% down the screen, so it went dark for two frames. An isolated press is unaffected, because nothing is fading when it lands.

Fix: under MAXIMUM (Metal, the phone) the trail is drawn over the whole glass during a sweep, the same draw a non-sweep present makes, with no readback. It is correct because the present that arms the sweep has just deposited that same old frame, and a deposit (the frame's excitation scaled to the ink, `captureGlass`) is never brighter than the frame in any channel, so max(old frame, trail) is the old frame with the afterglow it was already showing. The ADD fallback (SDL's software renderer: the desktop and the Mac apps) keeps the clip, because there the fresh deposit would light the old frame twice (measured earlier as the un-swept band scaled by 1.175). The decision is `glasscapture::clipTrailToSweptBand(beamSweeping, trailComposedMaximum)`.

*Evidence:*
- `tests/glass_capture_test.cpp` gained four checks. Against the old decision (`return beamSweeping;`) it fails: `FAIL tests/glass_capture_test.cpp:97: !clipTrailToSweptBand(true, true)`, exit 1. With the fix: `glass_capture_test: all checks passed`, exit 0.
- `tests/run_all.sh -k` glass_capture, present_hold, letterpress_dirty, scanlines_dirty and frontlit_page (plus frontlit_page_sleep): all PASS, `CROSSPOINT_FIRMWARE_DIR` = this worktree.
- `pio run -e simulator_picco`: SUCCESS (HalDisplay.o recompiled; the one warning is the pre-existing `%u` at :5120). iOS Simulator Release xcodebuild (CrossPointX3 scheme, Picco build dir): BUILD SUCCEEDED.
- clang-format 21.1.8 `--dry-run`: no new warning under the simulator's own (LLVM) style; under the firmware `.clang-format` the nested worktree inherits, +1 in each of `GlassCapture.h` and `HalDisplay.cpp`, the same 80-column wrapping as the rest of both files.
- Frame dumps, iOS Simulator, iPhone 17 Pro, Release Picco app, dark page, Home list, four rocker presses (`CROSSPOINT_SIM_INPUT_SCRIPT='4000:UP:40;4130:UP:40;4260:UP:40;4390:UP:40'`, `CROSSPOINT_SIM_SCREEN_DUMP` from 3650 ms, 22 frames, `CROSSPOINT_SIM_LOG_SCREEN/TIMING/PRESENTS=1`). The unfixed arm was the same build with `clipTrailToSweptBand` returning `beamSweeping`. Mean gray of a strip of the old cover ghost (x 150-400, y 465-540 of 1206x2622), where the bare page measures 27.0:
  - Unfixed: 47.5 (#5) -> **26.7 (#6, the first sweep present: no afterglow left)** -> 49.6 (#7). List-area luma 35.78 -> 33.96 -> 38.40.
  - Fixed: 43.3 (#5) -> 29.6 (#6) -> 48.0 (#7, the first sweep present). List-area luma 35.50 -> 34.28 -> 35.93 -> 36.63. Nothing drops to the bare page while a ghost is live.
  - The two runs' press timing differs (the dumps slow presents to 60-130 ms apart), so compare shapes, not single values.
  - Proof strips were kept outside the repo (session scratchpad, `fade-cut-unfixed.png`, `fade-cut-fixed.png`).
- Per-present cost: **not measured.** The trail draw has no timing bucket of its own, and in these runs each present's total is dominated by the 13 MB BMP write (60-140 ms), so the dump runs cannot resolve one full-glass textured quad against a band-clipped one.

*Unconfirmed:* the look on the owner's iPhone. Nothing here ran on a device.

### R-38 Each new deposit turns every older ghost back from the tail tint to the ink color

**status: OPEN** (owner decision) · sim `src/HalDisplay.cpp:4775-4802`

Seen in the same dump runs, both arms. The cascade tail's color ramp is one color multiply over the whole accumulator, driven by the age of the NEWEST deposit (`age = now - accumLastAddMs`, :4781). A deposit resets that age to 0, so every older ghost still in the buffer jumps from the dim tail tint back to the full ink color. In the fixed run the cover ghost strip went 29.6 (#6, dim red) -> 48.0 (#7, gray) at the present where the next frame was deposited; the deposited frame does not contain the cover. Preset 0 ships with a tail (`[glow] preset 0 -> 1095 ms trail, tail yes`). The code's own comment at :4755-4762 marks this as an approximation and names the exact fix: two accumulators running at the two layers' decay rates. This is probably part of the recording's "ghost back" at frame 458, but that was not checked against the recording. Not built: it is an architecture choice (a second output-sized accumulator, its memory and its draw), which goes to the owner first.

### R-33: seen on the iOS Simulator

In the fixed arm above, presents #4 and #5 each carried a new frame (`changed=1`, upload BUILD) with no deposit (`lastAdd` stayed 3690), and no glass capture ran between #2 and #6. So the row-1 frame was never captured or deposited, and the Browse Files band left no afterglow. That is the R-33 sequence, seen once. Not reproduced deliberately and not counted.

### R-33: options awaiting the owner

R-33 needs a press to land in a window of about one present (roughly 60-80 ms after the previous step's sweep reached the foot of the glass). No fix without added latency was found, so nothing is built.

| option | what it does | cost |
|----|----|----|
| (a) | Read the glass back before the flip on every present that completes a frame (undo rule 3's deferral) | about +35 ms per dark menu step, every step: the readback figure in `docs/menu-nav-lag-2026-11-27.md` (iOS Simulator, iPhone 17) |
| (b) | Pay that readback only when a new frame is already waiting at the completing present | the same ~35 ms plus about one present, but only on presses that land in the window |
| (c) | Deposit an approximation built from the page texture before the next frame is uploaded | no readback; approximate (it misses the pad and the surround, which only the composed glass holds), and not a small change |
| (d) | Accept it | that frame leaves no afterglow |

The scanline field and its readback are keyed on the page sequence (`src/SurfaceTube.cpp:313-319`), so a fade present serves them from cache; what a fade present itself costs was not measured.
