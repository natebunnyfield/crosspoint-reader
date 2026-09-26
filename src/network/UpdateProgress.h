#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

// What Update Fonts and Update Library show while they work, and WHEN they
// repaint it. Pure, so the decisions are host-tested (test/update_progress)
// rather than judged by watching a panel.
//
// Owner bug 2026-09-26: "not appearing frozen when i select Update Library and
// Update Fonts". Trace and measurements: docs/update-progress-2026-09-26.md.
// Two things were wrong, and this header answers the second:
//
//   1. On a HOST build (the iOS app, the Mac apps) nothing reached the glass
//      while a family or a book was being synced, because the host presents
//      only between loop() calls and the whole family ran inside one. That is
//      UpdateWorker.h's job.
//   2. On EVERY build the screen had nothing to say while it hashed the files
//      already on the card (no progress callback at all), and it repainted only
//      when the WHOLE RUN's percentage moved -- so a family whose files were
//      being checked, or a slow download, sat on one frame.
//
// The answer to 2 is a detail line that names the phase, the file and the
// bytes, an elapsed clock, and a repaint rule that follows them without
// flashing an e-ink panel on every callback.
namespace updprogress {

// What the updater is doing inside ONE item (a font family, or a book).
enum class Phase : uint8_t {
  IDLE = 0,
  PREPARING,    // recovery, root lookup, the deletion list -- card metadata only
  CHECKING,     // hashing a file already on the card against the manifest
  DOWNLOADING,  // streaming a file from GitHub, hashing as it arrives
  INSTALLING,   // renames into place, ledger entries
  FINISHING,    // end of run: removals, the ledger write
};

// Everything the progress frame shows that can change. Two snapshots that
// compare equal would paint the same pixels.
struct Snapshot {
  uint8_t state = 0;  // the activity's own State, as a number
  uint8_t phase = 0;  // Phase while syncing, the check step while checking
  uint32_t item = 0;  // family or book index
  uint32_t file = 0;  // file within the family
  uint64_t bytes = 0;
  uint32_t elapsedSec = 0;
  bool stopping = false;  // Back was pressed; the run ends after this item
};

// A phase, item or file change: the frame says something different, not just a
// bigger number. Painted sooner than a byte count, but still not more than four
// times a second -- a run over thirteen unchanged families walks their phases in
// milliseconds, and an e-ink refresh is ~500 ms of panel time each.
constexpr uint32_t MIN_STRUCTURAL_REPAINT_MS = 250;
// Bytes and the clock: at most once a second. FAST refresh, never a flash.
constexpr uint32_t MIN_REPAINT_INTERVAL_MS = 1000;

inline bool structuralChange(const Snapshot& a, const Snapshot& b) {
  return a.state != b.state || a.phase != b.phase || a.item != b.item || a.file != b.file || a.stopping != b.stopping;
}

inline bool visibleChange(const Snapshot& a, const Snapshot& b) {
  return structuralChange(a, b) || a.bytes != b.bytes || a.elapsedSec != b.elapsedSec;
}

// Should a frame be requested now? `shown` is the snapshot the last requested
// frame carried; `hasShown` is false before the first. A change that is held
// back by the interval is NOT lost: `shown` still differs from the truth, so the
// next caller -- a progress callback, or the loop's own heartbeat -- paints it.
inline bool shouldRepaint(bool hasShown, const Snapshot& shown, const Snapshot& now, uint32_t msSinceLastRequest) {
  if (!hasShown) return true;
  if (!visibleChange(shown, now)) return false;
  if (structuralChange(shown, now)) return msSinceLastRequest >= MIN_STRUCTURAL_REPAINT_MS;
  return msSinceLastRequest >= MIN_REPAINT_INTERVAL_MS;
}

// Between items, what happens next. A stop requested at ANY point -- including
// during the last item -- ends the run without its end-of-run removals: a reader
// who stopped did not ask for a mirror (owner ruling 2026-09-07, fonts). An
// item is never interrupted by this, which is what keeps a family whole.
enum class Next : uint8_t { RUN_ITEM, FINISH, STOP };

inline Next nextStep(bool stopRequested, size_t nextIndex, size_t total) {
  if (stopRequested) return Next::STOP;
  return nextIndex < total ? Next::RUN_ITEM : Next::FINISH;
}

// Bytes done across an item made of several files: every file before
// `fileIndex` counts whole, the current one counts `inFile`, clamped to its own
// declared size so a server that sends more than it promised cannot run the
// line past its total. `Files` is anything indexable whose elements have a
// `bytes` member (FontUpdater::FontFile).
template <typename Files>
uint64_t bytesDone(const Files& files, size_t fileIndex, uint64_t inFile) {
  uint64_t done = 0;
  const size_t count = files.size();
  for (size_t i = 0; i < count && i < fileIndex; ++i) done += files[i].bytes;
  if (fileIndex < count) {
    const uint64_t cap = files[fileIndex].bytes;
    done += inFile < cap ? inFile : cap;
  }
  return done;
}

template <typename Files>
uint64_t bytesTotal(const Files& files) {
  uint64_t total = 0;
  for (size_t i = 0; i < files.size(); ++i) total += files[i].bytes;
  return total;
}

// Decimal megabytes, the unit GitHub and Finder print, so the figures agree
// with what the owner sees when he publishes: one decimal below 10 MB ("1.2",
// "0.4"), whole above it ("80"). Truncated, never rounded up, so a file cannot
// read "6.8 of 6.8" while bytes are still arriving.
inline void formatMb(uint64_t bytes, char* out, size_t n) {
  if (n == 0) return;
  const uint64_t tenths = bytes / 100000u;  // 0.1 MB units
  if (tenths >= 100) {
    snprintf(out, n, "%llu", static_cast<unsigned long long>(tenths / 10));
  } else {
    snprintf(out, n, "%u.%u", static_cast<unsigned>(tenths / 10), static_cast<unsigned>(tenths % 10));
  }
}

// "0:07", "12:03", "1:02:03".
inline void formatElapsed(uint32_t sec, char* out, size_t n) {
  if (n == 0) return;
  const unsigned h = sec / 3600u;
  const unsigned m = (sec / 60u) % 60u;
  const unsigned s = sec % 60u;
  if (h > 0) {
    snprintf(out, n, "%u:%02u:%02u", h, m, s);
  } else {
    snprintf(out, n, "%u:%02u", m, s);
  }
}

// A font file's name without its ".cpfont" -- the line has room for "Edgar_12",
// and the extension says nothing the screen's title has not.
inline void stemOf(const char* file, char* out, size_t n) {
  if (n == 0) return;
  size_t len = 0;
  while (file[len] != '\0') ++len;
  const char* dot = nullptr;
  for (size_t i = len; i > 0; --i) {
    if (file[i - 1] == '.') {
      dot = file + i - 1;
      break;
    }
  }
  const size_t stem = dot != nullptr && dot != file ? static_cast<size_t>(dot - file) : len;
  const size_t copy = stem < n - 1 ? stem : n - 1;
  for (size_t i = 0; i < copy; ++i) out[i] = file[i];
  out[copy] = '\0';
}

}  // namespace updprogress
