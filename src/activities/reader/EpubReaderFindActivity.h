#pragma once

#include <Epub.h>

#include <memory>
#include <string>

#include "BookFinder.h"
#include "activities/Activity.h"

// Runs one whole-book Find (BookFinder) under a progress popup, "Searching...
// chapter N of M", and hands the landing back to the reader as a FindResult.
// Back cancels between pages and chapters; a cancel and a miss both return a
// cancelled result, so the reader stays exactly where it was. A miss first shows
// "Not found" briefly.
//
// The reader releases its own Section before pushing this, so the layout done
// here never holds a second build working set beside the reader's
// (docs/find-in-book-options-2026-09-28.md section 2.5).
class EpubReaderFindActivity final : public Activity {
  BookFinder finder;
  std::string query;
  int startSpine;
  uint16_t startPage;
  int32_t startOffset;

  enum class State : uint8_t { Searching, NotFound };
  State state = State::Searching;
  bool armed = false;
  // The chapter number the popup last showed, and when; the popup is redrawn
  // when the chapter moves on, at most once per POPUP_MIN_INTERVAL_MS, because
  // every redraw is a panel refresh.
  int shownChapter = 0;
  unsigned long lastPopupMs = 0;
  unsigned long notFoundSinceMs = 0;
  unsigned long startedMs = 0;

  // Work per loop() pass. Short enough that a Back press is seen promptly and
  // the render task gets the lock between passes.
  static constexpr unsigned long STEP_BUDGET_MS = 40;
  static constexpr unsigned long POPUP_MIN_INTERVAL_MS = 1000;
  static constexpr unsigned long NOT_FOUND_HOLD_MS = 1500;

  void finishCancelled();

 public:
  EpubReaderFindActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::shared_ptr<Epub>& epub,
                         const ReaderRenderSpec& spec, int fontId, std::string query, int startSpine,
                         uint16_t startPage, int32_t startOffset)
      : Activity("EpubReaderFind", renderer, mappedInput),
        finder(epub, renderer, spec, fontId),
        query(std::move(query)),
        startSpine(startSpine),
        startPage(startPage),
        startOffset(startOffset) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return state == State::Searching; }
  bool preventAutoSleep() override { return state == State::Searching; }
};
