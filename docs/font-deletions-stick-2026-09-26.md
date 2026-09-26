# Deleted fonts come back — trace and fix (2026-09-26)

Owner report, taken at face value: *"warblertext and lutetianova and other fonts
keep getting recreated after i delete them."*

Surveyed at firmware commit `6c5fc7260` (main) and crosspoint-simulator
`feedd4a`. Each finding says how it is known: **verified** means read in source
or measured, **inferred** means reasoned and not observed.

## 1. What re-creates them

### Not the iOS seed pass (verified)

`crosspoint-simulator/ios/CrossPointFsPrep.cpp:273-304` already has a deletion
ledger (`.crosspoint/seeded-fonts.txt`, added after the 2026-09-15 report of the
same symptom). A bundled family that is absent and in the ledger is not
re-seeded (`:371-375`). WarblerText, LutetiaNova and DanteMT are not in build
221's `SeedFonts`, so the seed pass could not re-create them in any case.

### Update Fonts (verified)

Settings → Update Fonts (`SettingsActivity.cpp:77`, reachable on every build)
runs `FontUpdateActivity` → `FontUpdater`. `FontUpdater` fetches the private
release `natebunnyfield/claude-tools` tag `fonts-latest`
(`FontUpdater.cpp:34`) and MIRRORS its `manifest.json`:

- `syncFamily` (`FontUpdater.cpp:797`) found a manifest family with no
  directory in either root (`HEAD:src/network/FontUpdater.cpp:806`,
  `existed = false`) and installed it whole, reporting ADDED. Nothing
  distinguished "never installed here" from "the owner deleted it".
- `removeUnlistedFamilies` (`FontUpdater.cpp:973`) then deleted every family
  directory the manifest does not list, over both roots.

The release as it stood this morning (fetched with `gh release download`,
2026-09-26): published 2026-09-08T01:06:01Z, manifest `generated`
2026-09-08T01:05:49Z, 13 families:

`Edgar Coelacanth TeXGyreSchola LibreFranklin LibrisADF InknutJunicode
TeXGyreHeros Almendra DanteMT LutetiaNova Doves WarblerText VandenKeere`

against `installed_families:` (`lib/EpdFont/scripts/sd-fonts.yaml:270-283`),
13 families:

`Edgar Coelacanth TeXGyreSchola LibreFranklin LibrisADF InknutJunicode
TeXGyreHeros Almendra Doves VandenKeere AtkinsonHyperlegibleNext Albo
HerosTextCut`

So the release was STALE in both directions: it carried the three families
dropped from the card on 2026-09-20 (`sd-fonts.yaml:765`, "DanteMT, LutetiaNova,
WarblerText should have dropped"), and it lacked the three added after it was
published (AtkinsonHyperlegibleNext 2026-09-12, Albo 2026-09-14,
HerosTextCut).

Every sync therefore re-downloaded DanteMT, LutetiaNova and WarblerText — and
would have re-downloaded ANY listed family the owner deleted, stale release or
not.

### Where the full mirror runs (verified)

- **X3 device:** `platformio.ini:152` sets
  `-DCROSSPOINT_FONTS_ONLY_FAMILY='"Edgar"'`, so the device looks at Edgar
  only (`FontUpdater.cpp` `acceptManifestFamily`, the `ONLY_FAMILY` filter)
  and its removal pass is switched off (`removeUnlistedFamilies`, the
  `ONLY_FAMILY != nullptr` gate). The device could re-create only Edgar.
- **iOS / desktop simulator:** that flag is not set — 0 matches in
  `crosspoint-simulator/cmake/CrossPointSources.cmake` and
  `ios/CMakeLists.txt`, and in `platformio.ini` it sits in `[base]` (section
  opens at `:74`), which `[env:simulator]` (`:353`) does not extend — that env
  says so itself, repeating `-Werror=switch` for the same reason. So on the
  PHONE and the desktop the sync is the full mirror.

### The removal risk on the phone — CONFIRMED (verified in a test, inferred on the device)

With the stale manifest, a phone sync's removal pass deleted every family
directory not in it. On the phone the seed pass puts bundled families in
`fonts/`, so Albo, AtkinsonHyperlegibleNext and HerosTextCut — all bundled —
were deleted by `removeUnlistedFamilies`. Reproduced headlessly: the new test
`FontCommit.RemovalSparesAFamilyTheHostSeeded` failed against the pre-fix tree
with Albo removed.

Worse, it does not heal: on the next launch the seed pass finds the family
absent and in `seeded-fonts.txt`, reads that as the owner's deletion
(`CrossPointFsPrep.cpp:371-375`), and never re-seeds it. **Whether this has
already happened on the owner's phone is not known** — it requires that Update
Fonts was run there after Albo was bundled. If it has, the recovery is to
delete the `Albo` (and the other two) lines from
`.crosspoint/seeded-fonts.txt` via Files and relaunch. The fix below prevents
it happening again; it cannot undo a removal that already happened.

## 2. The fix

### Half one — the release (done by the coordinator, verified)

`fonts-latest` was republished 2026-09-26T13:57:11Z with
`scripts/publish_fonts.py --skip-build --output-dir
~/src/crosspoint-reader/fs_/fonts` (the validated seed set shipped in build
221). Its manifest now lists exactly the 13 `installed_families:` — re-fetched
and checked here: `generated` 2026-09-26T13:57:11Z, 79 assets, no DanteMT /
LutetiaNova / WarblerText, and AtkinsonHyperlegibleNext / Albo / HerosTextCut
present. A phone sync against it now REMOVES the three dropped families rather
than restoring them.

That alone does not make a deletion stick: a deleted family that IS in the
manifest still came back. Hence half two.

### Half two — firmware (this commit)

`src/network/FontDeletionList.h`, header-only (so the simulator's generated
`cmake/CrossPointSources.cmake` does not need regenerating):

1. **Deletions stick.** `/.crosspoint/deleted-fonts.txt`, one family per line,
   `#` comments — the same shape as `seeded-fonts.txt`, so one parser reads
   both and the owner can edit it over File Transfer. Same semantics as the
   seed ledger:
   - listed and absent → `syncFamily` returns the new
     `FamilyResult::SKIPPED_DELETED`: nothing downloaded, nothing written,
     logged by name (`FontUpdater.cpp:838-864`), and the activity logs
     `not downloaded, deleted by the owner: …` at the end of the run
     (`FontUpdateActivity.cpp:300`). The on-screen summary is unchanged.
   - listed and present → someone put it back; it leaves the list and syncs as
     usual (`FontUpdater.cpp:844`).

   It is written two ways:
   - `FontInstaller::deleteFamily` records it (`FontInstaller.cpp:150,158`) —
     the web Fonts page's delete (`CrossPointWebServer.cpp:1978`). There is no
     other caller of `deleteFamily` in the tree (grep, 2026-09-26); the
     on-device Manage Files screen deletes through `util/FsOps.cpp`, not through
     `FontInstaller`.
   - **Inferred** by the sync (`FontUpdater.cpp:848-853`): the family is
     absent, but `font_sync.json` still holds records for it. The sync verified
     it on this card before, and the sync never removes a family its manifest
     lists (removal's own records are pruned at `flushSyncRecords`), so
     something else deleted it — the iOS Files app, Manage Files, WebDAV, a card
     reader. This is what makes a delete that never passes through
     `FontInstaller` stick, and it needs no delete hook in any of those paths —
     the same reason the seed ledger needs none. Limit: a family deleted before
     any sync ever verified it has no records, so the first sync after that
     still installs it; the next deletion then sticks.

   On a skip, the family's `font_sync.json` records are dropped, so that
   deleting its line from `deleted-fonts.txt` brings it back on the next sync
   rather than the inference putting the line straight back.

   It is cleared by `FontInstaller::ensureFamilyDir` (`FontInstaller.cpp:66`) —
   the web installer's first step for each uploaded cut — and by a sync finding
   the family present (a copy into `fonts/`, WebDAV, the iOS seed pass after the
   owner removes a `seeded-fonts.txt` line).

   A missing, oversized (> 4 KB) or unparseable list is EMPTY, and a line that
   is not a safe family name (`fontsync::isSafeFamilyName`) is ignored: the
   failure direction is a font reappearing, never one silently withheld.

2. **A stale manifest cannot remove what the host bundles.**
   `removeUnlistedFamilies` now spares any family named in
   `/.crosspoint/seeded-fonts.txt` (`FontUpdater.cpp:1001,1038`), with a log
   line naming it. Chosen over a manifest-age rule because:
   - it is a file read, no HAL channel: the iOS harness writes that file at the
     card root (`CrossPointFsPrep.cpp:304`, relative to the card after
     `chdir`), which is exactly `/.crosspoint/seeded-fonts.txt` to the firmware;
   - the app bundle ships with every app build, so it is a newer statement of
     what belongs on the card than a release published whenever
     `publish_fonts.py` last ran — the exact failure here;
   - an age threshold would be arbitrary, would also block legitimate removals
     (the three dropped families today), and would need a trusted clock;
   - on the device and the desktop nothing writes that file, so the mirror there
     is byte-for-byte the 2026-09-07 ruling. The ruling's "no exemption for
     sideloaded families" is untouched: only host-BUNDLED families are spared.

   Skipped families are never removal candidates: they are in the manifest by
   definition, and absent.

## 3. Tests

In `test/font_commit/` (the real `FontUpdater` and, new, the real
`FontInstaller` over a real temp filesystem). `FontInstaller.cpp` is compiled
from a copy in the build directory, because in place its quoted includes would
find the real `CrossPointSettings.h` beside it rather than the stub.

Fail-first: seven failed against the pre-fix tree and pass after it —

| Test | Pre-fix |
|---|---|
| AFamilyDeletedThroughTheInstallerIsNotDownloadedAgain | FAIL (re-downloaded) |
| AFamilyDeletedOutsideTheFirmwareIsNotDownloadedAgain | FAIL |
| ADeletionHoldsAcrossReloadsAndRepeatedRuns | FAIL ×3 runs |
| AFamilyReinstalledThroughTheWebInstallerSyncsAgain | FAIL |
| AFamilyCopiedBackByHandLeavesTheListAndIsKeptCurrent | FAIL |
| DeletingTheLineFromTheListBringsTheFamilyBack | FAIL |
| RemovalSparesAFamilyTheHostSeeded | FAIL (Albo removed) |

plus `AnUnreadableDeletionListIsTreatedAsEmpty`, `AMissingOrOversizedListLoadsEmpty`
and two pure parser tests (these pass either way — they pin the safe
direction). Suite: 32/32. Whole host suite (`ctest --test-dir build`): 100% of
778, 9 disabled/skipped as before.

Builds: `pio run -e default` SUCCESS (Flash 81.9%), `pio run -e simulator_x3`
SUCCESS.

## 4. Checked and found clean

- iOS seed pass: already has its own ledger; not the re-creator (above).
- The X3 device: `ONLY_FAMILY` limits both download and removal to Edgar;
  unaffected by the stale manifest's extra families.
- Cancel path: `cancelSync` never calls removal (`FontUpdateActivity.cpp`,
  unchanged); the new skip does not change that.
- Crash recovery: the deletion check runs AFTER `recoverStaleStaging`, so a
  family left absent by our own interrupted commit is restored before its
  absence could be read as a deletion.
- `font_sync.json` prune (`flushSyncRecords`) keeps records only for manifest
  files, so the sync's own removals cannot feed the inference.

## 5. Not done (suggestions, not in the diff)

- The FontUpdate summary screen does not show the skipped count; it is in the
  log only. A screen line would need a new string in every language file.
- The iOS seed pass cannot tell a sync removal from an owner deletion; with the
  removal protection above that can no longer be caused by the sync, but a
  phone that already lost Albo stays without it until its `seeded-fonts.txt`
  line is deleted.
