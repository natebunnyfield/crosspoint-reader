#include "EpubReaderActivity.h"

#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_system.h>

#include <algorithm>
#include <functional>
#include <iterator>
#include <limits>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "EpubReaderFootnotesActivity.h"
#include "EpubReaderUtils.h"
#include "MappedInputManager.h"
#include "ReaderFontSizes.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "ReadingFontList.h"

namespace {
// pagesPerRefresh now comes from SETTINGS.getRefreshFrequency()
// pages per minute, first item is 1 to prevent division by zero if accessed

// SD card folder finished books are moved into. Single source of truth for the path.
// constexpr ⇒ lives in flash .rodata, no DRAM cost.
constexpr char READ_FOLDER[] = "/read";

// True if path is inside READ_FOLDER (starts with "<READ_FOLDER>/"). Non-allocating so
// it is cheap to call from loop(), and avoids reintroducing a separate "/Read/" literal.
bool isInReadFolder(const std::string& path) {
  constexpr size_t n = sizeof(READ_FOLDER) - 1;  // length of "/Read" (excludes NUL)
  return path.size() > n && path.compare(0, n, READ_FOLDER) == 0 && path[n] == '/';
}

struct ProgressRange {
  float start;
  float end;
};

ProgressRange getPageProgressRange(const std::shared_ptr<Epub>& epub, const int spineIndex, const int page,
                                   const int pageCount) {
  if (pageCount <= 1) {
    return {epub->calculateProgress(spineIndex, 0.0f), epub->calculateProgress(spineIndex, 1.0f)};
  }

  const float step = 1.0f / static_cast<float>(pageCount - 1);
  const float anchor = std::clamp(static_cast<float>(page) * step, 0.0f, 1.0f);
  const float start = std::max(0.0f, anchor - (step * 0.5f));
  const float end = std::min(1.0f, anchor + (step * 0.5f));
  return {epub->calculateProgress(spineIndex, start), epub->calculateProgress(spineIndex, end)};
}

// Pick a non-colliding destination path inside /Read/ for a finished book.
// Mirrors the suffixing scheme used elsewhere: "name.epub" -> "name (2).epub", etc.
std::string buildReadFolderDestination(const std::string& srcPath) {
  const size_t lastSlash = srcPath.rfind('/');
  const std::string filename = (lastSlash != std::string::npos) ? srcPath.substr(lastSlash + 1) : srcPath;

  Storage.mkdir(READ_FOLDER);
  std::string dstPath = std::string(READ_FOLDER) + "/" + filename;
  if (!Storage.exists(dstPath.c_str())) {
    return dstPath;
  }

  const size_t dotPos = filename.rfind('.');
  const std::string base = (dotPos != std::string::npos) ? filename.substr(0, dotPos) : filename;
  const std::string ext = (dotPos != std::string::npos) ? filename.substr(dotPos) : "";
  int suffix = 2;
  do {
    dstPath = std::string(READ_FOLDER) + "/" + base + " (" + std::to_string(suffix) + ")" + ext;
    suffix++;
  } while (Storage.exists(dstPath.c_str()) && suffix < 100);
  return dstPath;
}

// Relocate a finished book and its cache dir into /read/, keep it in recents by
// repointing its entry to the new path, and repoint the resume pointer too.
// On rename failure: LOG_ERR and leave everything in place (no UI alert subsystem here).
void moveFinishedBookToReadFolder(const std::string& srcPath, const std::string& dstPath,
                                  const std::string& oldCachePath) {
  LOG_INF("ERS", "Moving finished epub: %s -> %s", srcPath.c_str(), dstPath.c_str());
  if (!Storage.rename(srcPath.c_str(), dstPath.c_str())) {
    LOG_ERR("ERS", "Failed to move finished book to '/Read' folder");
    return;
  }

  // Cache dir is keyed by hash of the epub path (see Epub ctor), so it must be re-keyed.
  const std::string newCachePath = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(dstPath));
  if (!oldCachePath.empty() && Storage.exists(oldCachePath.c_str())) {
    if (!Storage.rename(oldCachePath.c_str(), newCachePath.c_str())) {
      LOG_ERR("ERS", "Failed to rename cache dir %s -> %s (non-fatal)", oldCachePath.c_str(), newCachePath.c_str());
    }
  }

  // Keep the book in recents (crossink behavior): repoint the entry to its new
  // location instead of dropping it. updatePath persists on success.
  RECENT_BOOKS.updatePath(srcPath, dstPath, oldCachePath, newCachePath);
  if (APP_STATE.openEpubPath == srcPath) {
    APP_STATE.openEpubPath = dstPath;
    APP_STATE.saveToFile();
  }
}

}  // namespace

void EpubReaderActivity::onEnter() {
  Activity::onEnter();

  if (!epub) {
    return;
  }

  ImageBlock::clearSessionRenderFailures();
  // Lazy image extraction: section builds only header-probe images, so the first
  // render of an image page pulls the file out of the EPUB through this hook.
  ImageBlock::setExtractor(epub.get(), [](void* ctx, const char* src, const char* dest) {
    return static_cast<Epub*>(ctx)->extractItemToFile(src, dest);
  });

  // Configure screen orientation based on settings
  // NOTE: This affects layout math and must be applied before any render calls.

  epub->setupCacheDir();

  HalFile f;
  if (Storage.openFileForRead("ERS", epub->getCachePath() + "/progress.bin", f)) {
    uint8_t data[12];
    int dataSize = f.read(data, 12);
    if (dataSize == 4 || dataSize == 6 || dataSize == 8 || dataSize == 12) {
      currentSpineIndex = data[0] + (data[1] << 8);
      nextPageNumber = data[2] + (data[3] << 8);
      if (nextPageNumber == UINT16_MAX) {
        // UINT16_MAX is an in-memory navigation sentinel for "open previous
        // chapter on its last page". It should never be treated as persisted
        // resume state after sleep or reopen.
        LOG_DBG("ERS", "Ignoring stale last-page sentinel from progress cache");
        nextPageNumber = 0;
      }
      cachedSpineIndex = currentSpineIndex;
      LOG_DBG("ERS", "Loaded cache: %d, %d", currentSpineIndex, nextPageNumber);
    }
    if (dataSize >= 6) {
      cachedChapterTotalPageCount = data[4] + (data[5] << 8);
    }
    if (dataSize >= 8) {
      // Re-arms the exact reposition across an activity teardown, which is what
      // the Text Settings font and size controls go through.
      pendingParagraphAnchor = static_cast<uint16_t>(data[6] + (data[7] << 8));
    }
    if (dataSize == 12) {
      pendingWordAnchor = static_cast<uint32_t>(data[8]) | (static_cast<uint32_t>(data[9]) << 8) |
                          (static_cast<uint32_t>(data[10]) << 16) | (static_cast<uint32_t>(data[11]) << 24);
    }
  }
  // We may want a better condition to detect if we are opening for the first time.
  // This will trigger if the book is re-opened at Chapter 0.
  if (currentSpineIndex == 0) {
    int textSpineIndex = epub->getSpineIndexForTextReference();
    if (textSpineIndex != 0) {
      currentSpineIndex = textSpineIndex;
      LOG_DBG("ERS", "Opened for first time, navigating to text reference at index %d", textSpineIndex);
    }
  }

  // Save current epub as last opened epub and add to recent books
  APP_STATE.openEpubPath = epub->getPath();
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), epub->getThumbBmpPath());

  // Trigger first update
  requestUpdate();
}

void EpubReaderActivity::onExit() {
  Activity::onExit();

  // "No page": tells a read-aloud consumer to stop speech. Inline no-op on
  // device.
  gpio.publishReadAloudPage(nullptr, 0, nullptr, 0);

  // The extractor holds a raw pointer to this activity's epub; drop it before
  // the activity (and the shared_ptr) goes away.
  ImageBlock::setExtractor(nullptr, nullptr);

  // Reset orientation back to portrait for the rest of the UI

  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();

  // Leaving mid-footnote loses the in-RAM return stack on deep sleep; persist the
  // pre-footnote position so the book reopens at the link origin, not the footnote.
  if (footnoteDepth > 0 && epub) {
    const SavedPosition& origin = savedPositions[0];
    saveProgress(origin.spineIndex, origin.pageNumber, 0);
  }

  section.reset();
  if (pendingReadFolderMove && epub) {
    const std::string srcPath = epub->getPath();
    const std::string oldCachePath = epub->getCachePath();
    const std::string dstPath = buildReadFolderDestination(srcPath);
    epub.reset();  // release the Epub (and any open handles) before renaming on the SD card
    moveFinishedBookToReadFolder(srcPath, dstPath, oldCachePath);
  } else {
    epub.reset();
  }
}

bool EpubReaderActivity::buildTickHeapGate() {
  const size_t freeHeap = ESP.getFreeHeap();
  const size_t maxBlock = ESP.getMaxAllocHeap();
  // Below the floors: just wait. The tick is deferrable — page-turn transients
  // free up between turns and the tick retries every loop pass. Track the
  // paused state so skipLoopDelay() stops pinning the CPU at full speed while
  // no build work is actually happening (the gate can stay closed for a long
  // stretch if the retained build context itself holds the heap down).
  buildHeapPaused = freeHeap < BACKGROUND_BUILD_MIN_FREE_HEAP || maxBlock < BACKGROUND_BUILD_MIN_MAX_ALLOC;
  return !buildHeapPaused;
}

void EpubReaderActivity::showBuildPopup() {
  // Mid-build indexing popup: only during onEnter's blocking build-to-target phase
  // (buildPopupPending), at most once, and only when the framebuffer isn't on loan.
  // If it fires while the loan is active (e.g. the parser's size-based call during
  // startBuild), pending stays set and the deadline check retries after the loan.
  if (!buildPopupPending || !renderer.hasFrameBuffer()) return;
  // Enforced here rather than at the call sites so the parser's image-probe callback
  // obeys it too: that fires early in a build which may still finish inside the delay,
  // one of the ways a fast build used to flash the popup.
  if (buildStartMs == 0 || millis() - buildStartMs < BUILD_POPUP_DELAY_MS) return;
  GUI.drawPopup(renderer, tr(STR_INDEXING));
  // HALF-clear the popup when the page replaces it, else "INDEXING" ghosts.
  pagesUntilFullRefresh = 1;
  buildPopupPending = false;
}

void EpubReaderActivity::loop() {
  if (!epub) {
    // Should never happen
    finish();
    return;
  }

  // Idle glyph prewarm for the likely next page (currentPage + 1). The scan
  // pass draws nothing (FCM scan mode suppresses pixels), so the displayed
  // framebuffer is untouched; endScanAndPrewarm loads only glyphs not already
  // cached. Debounced past rapid page-flipping, one attempt per position, and
  // deferred while a render/build owns the CPU or the heap is at the render
  // floor. Cross-chapter prewarm is deliberately out of scope (next spine's
  // section isn't loaded).
  constexpr unsigned long IDLE_PREWARM_DEBOUNCE_MS = 400;
  if (section && !section->isBuilding() && !RenderLock::peek() && renderer.hasFrameBuffer() &&
      lastRenderCompleteMs != 0 && millis() - lastRenderCompleteMs > IDLE_PREWARM_DEBOUNCE_MS &&
      ESP.getFreeHeap() > RENDER_MIN_FREE_HEAP && ESP.getMaxAllocHeap() > BACKGROUND_BUILD_MIN_MAX_ALLOC &&
      (idlePrewarmSpine != currentSpineIndex || idlePrewarmPage != section->currentPage)) {
    RenderLock lock;  // the page table must not change under the scan
    // Re-check under the lock: peek() and acquisition are not atomic, so the render
    // task may have reset/replaced the section or moved the page in between.
    if (section && !section->isBuilding() &&
        (idlePrewarmSpine != currentSpineIndex || idlePrewarmPage != section->currentPage)) {
      idlePrewarmSpine = currentSpineIndex;
      idlePrewarmPage = section->currentPage;
      const int nextPage = section->currentPage + 1;
      if (nextPage < static_cast<int>(section->pageCount)) {
        if (const auto p = section->loadPage(nextPage)) {
          if (auto* fcm = renderer.getFontCacheManager()) {
            const auto t0 = millis();
            auto scope = fcm->createPrewarmScope();
            p->render(renderer, SETTINGS.getReaderFontId(), 0, 0);  // scan only, no pixels
            scope.endScanAndPrewarm();
            LOG_DBG("ERS", "Idle prewarm: page %d in %lums", nextPage, millis() - t0);
          }
        }
      }
    }
  }

  // Lazily resume a partial's extension build once the reader nears its watermark. Far from
  // it the rebuild is all cost (whole-chapter re-layout from page 0) and no benefit this
  // session, so reopening a partial deliberately does NOT start it (see the deferral in
  // render()); crossing this margin is the signal that the reader will actually need pages
  // past the watermark soon. Uses the last render's viewport so pagination matches the
  // partial being extended.
  if (section && !section->isBuilding() && section->isPartial() && !RenderLock::peek() && buildViewportWidth > 0 &&
      !partialRebuildStartFailed &&
      section->currentPage + PARTIAL_REBUILD_START_MARGIN >= static_cast<int>(section->pageCount)) {
    RenderLock lock;
    // Reuse the last render's viewport so the extension paginates identically to the partial.
    const ReaderRenderSpec buildSpec = SETTINGS.readerRenderSpec(buildViewportWidth, buildViewportHeight);
    if (!section->startBuild(buildSpec)) {
      // Not fatal: the partial keeps serving its pages; crossing the watermark falls back to
      // the blocking extension in render(). Don't retry every tick.
      partialRebuildStartFailed = true;
      LOG_ERR("ERS", "Failed to start deferred partial extension build");
    } else {
      LOG_DBG("ERS", "Reader near partial watermark (%d/%d), resuming extension build", section->currentPage,
              section->pageCount);
    }
  }

  // Drive any in-progress incremental section build forward, off the page-turn critical path,
  // but only within a small window ahead of the reader: an unbounded build monopolized the
  // RenderLock and locked out page turns. The build follows the reader instead, and instant
  // reopen comes from suspendBuild() persisting the laid-out pages as a partial on exit.
  // Skip while the render mutex is busy so we never delay a pending render; re-check
  // isBuilding() under the lock since render() may have just finished it.
  // While extending a partial (rebuild from a previous session), pageCount is pinned at the
  // partial's watermark until the build catches up, so the window check would wrongly read
  // "far enough ahead" and stall the build at 0 pages -- then the first turn past the
  // watermark re-parses the whole chapter synchronously. Keep ticking until it finalizes.
  if (section && section->isBuilding() && !RenderLock::peek() &&
      (section->isPartial() || static_cast<int>(section->pageCount) < section->currentPage + BUILD_WINDOW_AHEAD) &&
      buildTickHeapGate()) {
    RenderLock lock;
    // Re-check under the lock: render() (which also holds the RenderLock) may have finalized the
    // build between the outer isBuilding() check and acquiring the lock here, in which case
    // buildSomeMore() would fail and wrongly reset the section. The heap gate must be re-read
    // too: a render that won the lock race can expand retained glyph buffers, invalidating the
    // pre-lock heap reading. cppcheck can't see the cross-task mutation, so it flags this as
    // always true.
    // cppcheck-suppress knownConditionTrueFalse
    if (section->isBuilding() && buildTickHeapGate()) {
      if (!section->buildSomeMore(BACKGROUND_BUILD_PAGES_PER_TICK)) {
        LOG_ERR("ERS", "Background section build failed");
        section.reset();
        requestUpdate();
      } else if (section->isBuildComplete() && applyDeferredReposition()) {
        // The chapter re-paginated since the saved progress (settings changed): we now know the
        // real page count, so re-render at the remapped page. No-op for an unchanged resume.
        requestUpdate();
      }
    }
  }

  // Second chance at the deferred remap.
  //
  // The only other call is inside the background-build tick above, which is
  // gated on `isPartial() || pageCount < currentPage + BUILD_WINDOW_AHEAD`. The
  // initial synchronous build in render() runs in BUILD_PAGES_PER_CHUNK (8)
  // steps while BUILD_WINDOW_AHEAD is 5, so it routinely overshoots the window:
  // the tick never runs, isBuildComplete() is never observed there, and the
  // remap never fires at all. The user then lands on the old page NUMBER after a
  // font/size change instead of the proportionally equivalent page.
  //
  // applyDeferredReposition() self-disarms (sets cachedChapterTotalPageCount = 0)
  // and no-ops when pagination is unchanged, so this cannot double-apply or
  // disturb a plain resume. Takes the lock because it mutates
  // section->currentPage, which the render task reads.
  if (section && !section->isBuilding() && cachedChapterTotalPageCount != 0 && !RenderLock::peek()) {
    RenderLock lock;
    if (applyDeferredReposition()) {
      requestUpdate();
    }
  }

  // End-of-Book screen reached (currentSpineIndex == spine count) means the book is
  // finished. Two independent finished-book features key off this same condition.
  const bool atEndOfBook = currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount();

  // Drop this book from the Recent Books list; if the reader then pages back into the book,
  // re-add it. So removal only sticks if the reader leaves while still on the End-of-Book
  // screen. Acts only on the transition (guarded by recentsEntryRemoved) — no per-frame writes.
  if (SETTINGS.removeReadBooksFromRecents) {
    if (atEndOfBook && !recentsEntryRemoved) {
      // Only treat the book as "removed by us" if it was actually in the list, so the
      // re-add branch below doesn't insert a book the feature never removed.
      recentsEntryRemoved = RECENT_BOOKS.removeByPath(epub->getPath());
    } else if (!atEndOfBook && recentsEntryRemoved) {
      // Re-add (goes to front of the list via addBook — accepted ordering side effect).
      RECENT_BOOKS.addBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), epub->getThumbBmpPath());
      recentsEntryRemoved = false;
    }
  }

  // Arm the move here so ANY exit path (Back, Home, file browser) relocates the book into
  // /Read/ in onExit(); paging back off the end screen disarms it (book not actually
  // finished). If removeReadBooksFromRecents also fired, RecentBooksStore::updatePath in the
  // move path becomes a safe no-op since the entry was already removed.
  if (atEndOfBook) {
    pendingReadFolderMove = SETTINGS.moveFinishedToReadFolder && !isInReadFolder(epub->getPath());
  } else {
    pendingReadFolderMove = false;
  }

  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);

  // ---- CONFIRM AS A MODIFIER KEY (Confirm held + side button tap = line spacing) ----
  //
  // Read here, ahead of every Confirm and side-button handler below, because a chord must
  // produce exactly ONE action. Three of the reader's existing gestures overlap it and are
  // gated off in three different places:
  //   1. chapter select on Confirm RELEASE — suppressed through the existing
  //      ignoreNextConfirmRelease latch, set where the chord acts;
  //   3. the side button's own gestures — font SIZE on tap, font FAMILY on hold, and the page
  //      turn from detectPageTurn() — all skipped by the chord block further down, for the
  //      press AND for the release that ends it.
  //
  // Cross-group chord, so the hardware really can see it: freeink-sdk
  // InputManager::getState() reads the FRONT group (BACK/CONFIRM/LEFT/RIGHT) off ADC pin 1 as
  // `state |= (1 << button1)` and UP/DOWN off a SECOND ADC as `state |= (1 << (button2 + 4))`
  // (InputManager.cpp:133-145). Two buttons in the SAME resistor ladder collapse to one
  // voltage and one index, which is why the modifier must be a FRONT button paired with a
  // SIDE button and not, say, Confirm + front Left.
  //
  // Nothing in the chord may be timed with getHeldTime(): applyStateChange() stamps
  // buttonPressStart only when `pressedEvents > 0 && currentState == 0`
  // (InputManager.cpp:242-248), i.e. once per whole chord, and never restamps per button. So
  // during a chord getHeldTime() reports the time since CONFIRM went down. Measured in the
  // simulator before this gate existed: Confirm down at t=12.0s, a 300ms side tap at t=13.0s
  // reported held=1000 on the very frame the side button went down — already past
  // SKIP_HOLD_MS, so the family-hold branch fired and changed the font family (observed:
  // sdFontFamilyName InknutAntiqua62 -> LibreCaslonText) while the Confirm release then also
  // opened the menu. The modifier is therefore read as isPressed() STATE and the trigger as a
  // press EDGE.
  const bool confirmModifierDown = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  const auto sideHeldNow = ReaderUtils::detectHeldSideDirection(mappedInput);
  if (!confirmModifierDown) {
    confirmModifierUsed = false;  // hold is over: re-arm for the next chord
  }

  // Chapter select on short-press Confirm or a downward swipe from the top edge. There is no
  // reader menu — jumping to a chapter is the one thing Confirm does.
  // On the end-of-book screen Confirm goes home instead.
  //
  // A long-press that fired a bound function (KOReader sync) sets ignoreNextConfirmRelease so
  // the release ending the hold does not also open chapter select; so does a line-spacing chord.
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || ReaderUtils::isTouchMenuGesture(mappedInput)) {
    if (ignoreNextConfirmRelease) {
      ignoreNextConfirmRelease = false;
    } else if (atEndOfBook) {
      onGoHome();
      return;
    } else {
      openChapterSelection();
    }
  }

  // Note sideHeldNow follows ReaderUtils::sideFontButtons(), so it is live even when
  // SETTINGS.sideButtonLayout is SIDE_BUTTONS_DISABLED — that setting withdraws side PAGING,
  // not the side buttons themselves, and the chord has to be reachable wherever the font
  // gestures are.
  // On the end-of-book screen a short Back goes home (same as Confirm and PageForward).
  if (atEndOfBook && mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_BACK_OR_HOME_MS) {
    onGoHome();
    return;
  }
  // Short press Back restores position when viewing a footnote (takes priority over navigation)
  if (footnoteDepth > 0 && mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_BACK_OR_HOME_MS) {
    restoreSavedPosition();
    return;
  }

  if (ReaderUtils::handleBackNavigation(mappedInput, activityManager, epub ? epub->getPath().c_str() : "",
                                        {this, [](void* ctx) { static_cast<EpubReaderActivity*>(ctx)->onGoHome(); }})) {
    return;
  }

  // auto [prevTriggered, nextTriggered] = ReaderUtils::detectPageTurn(mappedInput);

  // Handle short power button press for footnotes
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FOOTNOTES &&
      mappedInput.wasReleased(MappedInputManager::Button::Power) &&
      !mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (footnoteDepth > 0) {
      restoreSavedPosition();
    } else {
      if (currentPageFootnotes.size() == 1) {
        navigateToHref(currentPageFootnotes[0].href, true);
      } else if (currentPageFootnotes.size() > 1) {
        startActivityForResult(
            std::make_unique<EpubReaderFootnotesActivity>(renderer, mappedInput, currentPageFootnotes),
            [this](const ActivityResult& result) {
              if (!result.isCancelled) {
                const auto& footnoteResult = std::get<FootnoteResult>(result.data);
                navigateToHref(footnoteResult.href, true);
              }
              requestUpdate();
            });
      }
    }
    return;
  }

  // Side buttons whose press a chord already consumed: swallow the remainder of the gesture.
  // This has to sit ahead of BOTH the font block and detectPageTurn(), because both of them
  // are RELEASE-triggered whenever longPressButtonBehavior != OFF (the font tap branch reads
  // detectSideRelease; detectPageTurn's `usePress` is false unless the setting is OFF — see
  // ReaderUtils.h). An unswallowed release is therefore a second action from one press, which
  // is the exact "one edge consumed by two handlers" failure this feature had to avoid.
  if (sideChordConsumed) {
    if (sideHeldNow.any()) {
      return;  // still down: this press belongs to the chord and to nobody else
    }
    sideChordConsumed = false;  // both side buttons up: gesture over, re-arm
    // Clear the FONT block's latch too. It is set when a hold cycles the family
    // (below) and is only ever cleared by a side RELEASE reaching the font-tap
    // branch — but the return below eats exactly that release. Ordering that
    // reaches here: hold a side button past the threshold (family cycles, latch
    // set), press Confirm while still holding, then release the side button. The
    // latch would survive and silently swallow the user's NEXT plain font-size
    // tap. Both latches describe "this gesture is spent", so they must retire
    // together.
    suppressNextSideRelease = false;
    if (ReaderUtils::detectSideRelease(mappedInput).any()) {
      return;  // consume the release edge that ended it
    }
  }

  // THE CHORD: Confirm held + a side button PRESS steps SETTINGS.lineSpacing.
  //
  // Acting on the press rather than the release is both the modifier-key convention and what
  // makes the reflow start while the button is still down — the same reasoning that moved the
  // font-family hold off the release edge below.
  if (confirmModifierDown) {
    const auto sidePress = ReaderUtils::detectSidePress(mappedInput);
    if (sidePress.any()) {
      confirmModifierUsed = true;       // no long-press Confirm function for the rest of this hold
      sideChordConsumed = true;         // no font tap and no page turn from this press or release
      ignoreNextConfirmRelease = true;  // and no reader menu when Confirm is let go
      // PageForward widens, PageBack tightens: `next` = "more", matching the font-size ramp
      // where a `next` tap goes larger. Routed through PageBack/PageForward, so the user's
      // sideButtonLayout swap applies here too.
      stepReaderLineSpacing(sidePress.next ? +1 : -1);
      return;
    }
    if (sideHeldNow.any()) {
      // A side button was already down when Confirm arrived. There is no press edge left to
      // act on, but while Confirm is down the side buttons belong to the modifier, so this
      // must not become a font-family hold or a page turn either. Mark the press spent: the
      // gesture ends with no action rather than with the wrong one.
      sideChordConsumed = true;
      return;
    }
  }

  // SIDE BUTTONS ARE THE FONT CONTROLS while this setting is on:
  //   hold  = next / previous font FAMILY, cycling at both ends
  //   tap   = font SIZE step
  // Page turning therefore moves entirely to the FRONT Left/Right buttons (and
  // the power button when configured for it) — detectPageTurn still provides those below.
  // This is a deliberate trade the device owner asked for, not an oversight: the side
  // buttons no longer turn pages while longPressButtonBehavior == FONT_SIZE_STEP.
  //
  // The hold branch reads the HELD state rather than an edge, which is why it sits ahead of
  // detectPageTurn: it must not be gated behind "an edge fired this frame". Acting at the
  // threshold instead of on release also means the reflow starts while the button is still
  // down, and it uses the isPressed()+getHeldTime() shape, which reports a real duration on
  // both device and host (the wasReleased() shape does not — see
  // tools/patches/0001-crosspoint-simulator-halgpio-completed-hold.patch).
  if (SETTINGS.longPressButtonBehavior == SETTINGS.FONT_SIZE_STEP) {
    const auto held = ReaderUtils::detectHeldSideDirection(mappedInput);
    if (!held.any()) {
      // Gesture finished (or never started): re-arm for the next hold.
      sideHoldFired = false;
    } else if (!sideHoldFired && mappedInput.getHeldTime() >= ReaderUtils::SKIP_HOLD_MS) {
      // One family step per hold. Holding longer does not auto-repeat — lift and hold again
      // — so a lean on the button cannot race through the whole font list, each step of
      // which costs an SD load and a full re-paginate.
      sideHoldFired = true;
      // Mark the release this hold will end with, so the tap branch below does not then
      // read it as a size step.
      suppressNextSideRelease = true;
      // `next` wins if both are somehow down: getHeldTime() is a single global chord timer
      // (freeink-sdk InputManager stamps it only when everything was up), so a two-button
      // hold has no per-button duration to disambiguate with.
      cycleReaderFontFamily(held.next ? +1 : -1);
      return;
    }

    // Tap: the release of a side button that never crossed the hold threshold.
    const auto sideTap = ReaderUtils::detectSideRelease(mappedInput);
    if (sideTap.any()) {
      if (suppressNextSideRelease) {
        suppressNextSideRelease = false;  // this release belonged to a hold we already acted on
        return;
      }
      stepReaderFontSize(sideTap.next ? +1 : -1);
      return;
    }
  }

  auto [prevTriggered, nextTriggered] = ReaderUtils::detectPageTurn(mappedInput);
  prevTriggered = prevTriggered || touch.prev;
  nextTriggered = nextTriggered || touch.next;
  if (!prevTriggered && !nextTriggered) {
    return;
  }

  // At end of the book: forward goes home, back returns to last page
  if (currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount()) {
    if (nextTriggered) {
      onGoHome();
    } else {
      currentSpineIndex = epub->getSpineItemsCount() - 1;
      nextPageNumber = 0;
      pendingPageJump = std::numeric_limits<uint16_t>::max();
      requestUpdate();
    }
    return;
  }

  const unsigned long heldMs = (touch.prev || touch.next) ? touch.heldMs : mappedInput.getHeldTime();
  const bool longPress = heldMs > ReaderUtils::SKIP_HOLD_MS;

  if (longPress && SETTINGS.longPressButtonBehavior == SETTINGS.CHAPTER_SKIP) {
    if (!nextTriggered && section && section->currentPage > 0) {
      section->currentPage = 0;
      requestUpdate();
      return;
    }

    // We don't want to delete the section mid-render, so grab the semaphore
    {
      RenderLock lock(*this);
      nextPageNumber = 0;
      if (nextTriggered) {
        currentSpineIndex++;
      } else if (currentSpineIndex > 0) {
        currentSpineIndex--;
      }
      section.reset();
    }
    requestUpdate();
    return;
  }

  // NOTE: the font-size step is handled ABOVE, on the held state, before detectPageTurn.
  // It used to live here keyed off the release edge; that is what made it feel laggy.

  // No current section, attempt to rerender the book
  if (!section) {
    requestUpdate();
    return;
  }

  if (prevTriggered) {
    pageTurn(false);
  } else {
    pageTurn(true);
  }
}

// Translate an absolute percent into a spine index plus a normalized position
// within that spine so we can jump after the section is loaded.
void EpubReaderActivity::stepReaderFontSize(const int delta) {
  // The selectable sizes are the point sizes the active family actually ships
  // (the same list buildFontSizeSetting() and FontSelectionActivity offer), so a
  // step walks that list. Clamped at the ends rather than wrapped, for the same
  // "the end of the range must be perceptible" reason
  // ReaderUtils::steppedLineSpacing clamps.
  // Steps the SLOT. Mirrors FontSelectionActivity::changeFontSize so a side-button
  // hold and the Text Settings control walk the same ramp.
  const std::vector<uint8_t> sizes = readerFontPointSizes(&sdFontSystem.registry(), SETTINGS.sdFontFamilyName);
  if (sizes.empty()) return;
  int nextSlot = static_cast<int>(SETTINGS.fontSizeSlot) + delta;
  if (nextSlot < 0) nextSlot = 0;
  if (nextSlot >= static_cast<int>(sizes.size())) nextSlot = static_cast<int>(sizes.size()) - 1;
  const uint8_t next = sizes[nextSlot];

  if (nextSlot == static_cast<int>(SETTINGS.fontSizeSlot)) {
    // At an end of the ramp: nothing to persist (Resource Protocol 8 — no settings write
    // without a value change) and nothing to re-paginate. Return without requesting an
    // update: there is no longer a popup to draw, so a refresh here would spend a full
    // e-ink pass redrawing a page that has not changed.
    return;
  }

  {
    // Everything below has to be atomic with respect to the render task, which is why this
    // is one lock and not three. ActivityManager::loop() calls us holding NO lock by design
    // ("the loop() method must be responsible for acquire one if needed",
    // ActivityManager.cpp), so the render task can be inside render() right now.
    RenderLock lock(*this);

    // Preserve the reading position across the reflow, exactly as the retired
    // the retired applyOrientation() did before its own section.reset(). Without these three lines the position is
    // simply lost: nextPageNumber is never written by an intra-chapter page turn, so the
    // rebuild lands on whatever page the CHAPTER was entered at and render() then persists
    // that wrong page. They also re-arm applyDeferredReposition(), which bails on
    // cachedChapterTotalPageCount == 0 — i.e. the proportional remap onto the new
    // pagination happens only because the count is captured here.
    if (section) {
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      // Capture a paragraph anchor while the OLD section is still alive. A page
      // index cannot survive a reflow; a paragraph index can, and the LUT that
      // resolves it is already written to every section file. The proportional
      // remap in applyDeferredReposition() stays as the fallback for chapters
      // whose LUT is degenerate (no <p> elements -> paragraph 0 on every page).
      pendingParagraphAnchor = section->getParagraphIndexForPage(static_cast<uint16_t>(section->currentPage));
      pendingWordAnchor = section->getFirstWordAnchorForPage(static_cast<uint16_t>(section->currentPage));
      LOG_DBG("ERS", "Capture: page %d para %d word %ld", section->currentPage,
              pendingParagraphAnchor ? static_cast<int>(*pendingParagraphAnchor) : -1,
              pendingWordAnchor ? static_cast<long>(*pendingWordAnchor) : -1L);
      nextPageNumber = section->currentPage;
    }

    // Drop the section BEFORE ensureLoaded(), never after: Section caches the font ID it
    // was built with and its destructor still runs suspendBuild(), while ensureLoaded ->
    // SdCardFontManager::unloadAll() does removeFont(id) / delete lf.font and frees exactly
    // those glyph tables. Same ordering, same reason, as the TEXT_SETTINGS case.
    section.reset();

    SETTINGS.fontSizeSlot = static_cast<uint8_t>(nextSlot);
    SETTINGS.fontPointSize = next;
    // Guarded by the no-change early-return above, so this only ever runs on a real
    // change — the SPIFFS/SD write is not repeated by leaning on the button at the end of
    // the ramp.
    SETTINGS.saveToFile();

    // An SD card family keeps only ONE point size resident, so the new size has to be
    // loaded before the chapter can be re-paginated or drawn at it; a built-in face just
    // resolves to a different font ID. Called under the lock because unloadAll() frees
    // glyph tables the render task may be mid-draw with.
    sdFontSystem.ensureLoaded(renderer);
  }
  // Outside the lock: requestUpdate() only sets a deferred flag / notifies the render task.
  // requestUpdateAndWait() would deadlock (ActivityManager asserts on a held lock).
  requestUpdate();
}

void EpubReaderActivity::stepReaderLineSpacing(const int delta) {
  const uint8_t current = SETTINGS.lineSpacing;
  const uint8_t next = ReaderUtils::steppedLineSpacing(current, delta);

  if (next == current) {
    // At an end of the three-slot ramp: nothing to persist (Resource Protocol 8 — no settings
    // write without a value change) and nothing to re-paginate. Deliberately no
    // requestUpdate() either: a full e-ink pass to redraw an identical page is exactly the
    // flash the owner asked to be removed from the font-resize path.
    return;
  }

  {
    // One lock for the whole mutation, for the same reason as stepReaderFontSize:
    // ActivityManager::loop() calls activities with NO lock held by design, so the render task
    // may be inside render() right now — and render() reads SETTINGS.lineSpacing on every
    // build/load path via SETTINGS.getReaderLineCompression() (lines 1188 / 1232 / 1293 /
    // 1372 here). Mutating the setting and dropping the section have to be atomic against it.
    RenderLock lock(*this);

    // Position preservation, copied from stepReaderFontSize above (see the long comment
    // there): capture BEFORE section.reset(). Without these three lines the place is simply
    // lost — nextPageNumber is never written by an intra-chapter page turn, so the rebuild
    // lands on whatever page the CHAPTER was entered at and render() then persists that wrong
    // page. They are also what re-arms applyDeferredReposition(), which bails on
    // cachedChapterTotalPageCount == 0, so the proportional remap onto the new (re-leaded)
    // pagination happens only because the count is captured here.
    if (section) {
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      // Capture a paragraph anchor while the OLD section is still alive. A page
      // index cannot survive a reflow; a paragraph index can, and the LUT that
      // resolves it is already written to every section file. The proportional
      // remap in applyDeferredReposition() stays as the fallback for chapters
      // whose LUT is degenerate (no <p> elements -> paragraph 0 on every page).
      pendingParagraphAnchor = section->getParagraphIndexForPage(static_cast<uint16_t>(section->currentPage));
      pendingWordAnchor = section->getFirstWordAnchorForPage(static_cast<uint16_t>(section->currentPage));
      LOG_DBG("ERS", "Capture: page %d para %d word %ld", section->currentPage,
              pendingParagraphAnchor ? static_cast<int>(*pendingParagraphAnchor) : -1,
              pendingWordAnchor ? static_cast<long>(*pendingWordAnchor) : -1L);
      nextPageNumber = section->currentPage;
    }
    section.reset();

    SETTINGS.lineSpacing = next;
    // Guarded by the next == current early return above, so repeated presses at either end of
    // the ramp do not repeat the write (Resource Protocol 8).
    SETTINGS.saveToFile();

    // No sdFontSystem.ensureLoaded() here, unlike stepReaderFontSize and
    // cycleReaderFontFamily: line spacing changes neither the font family nor its point size,
    // so no glyph table is loaded or freed and there is nothing for the render task to be
    // mid-draw with. The re-pagination instead comes from Section::loadSectionFile rejecting
    // the cached .bin on the lineCompression mismatch (lib/Epub/Epub/Section.cpp:156).
  }
  LOG_DBG("ERS", "Line spacing slot %u -> %u (chord)", current, next);
  // Outside the lock: requestUpdate() only sets a deferred flag / notifies the render task.
  // requestUpdateAndWait() would deadlock (ActivityManager asserts on a held lock).
  requestUpdate();
}

void EpubReaderActivity::cycleReaderFontFamily(const int delta) {
  // Mirror FontSelectionActivity's list EXACTLY, or holding a side button would walk a
  // different set of fonts than the picker shows. That list is: the installed SD families
  // if there are any, and ONLY otherwise the two built-in Noto faces
  // (FontSelectionActivity.cpp, onEnter).
  //
  // THE WRITING-ONLY FILTER IS PART OF THAT LIST and was missing here. The picker skips
  // editor faces that are not also reading faces (FontSelectionActivity.cpp, the
  // isWritingOnlyFamily continue); this cycle walked the raw registry, so holding a side
  // button stepped onto Space Mono, IBM Plex Mono and the iA monos -- families Text
  // Settings will not list, and therefore cannot show you as current or let you leave by
  // the same route you arrived. Owner bug report 2026-08-11: "only fonts in Text Settings
  // list should be selected."
  //
  // Indices into the registry, not a copy of the names: SETTINGS.sdFontFamilyName resolves
  // against registry position elsewhere, so renumbering here would re-point it.
  const auto& families = sdFontSystem.registry().getFamilies();
  std::vector<int> readable;
  readable.reserve(families.size());
  for (int i = 0; i < static_cast<int>(families.size()); i++) {
    if (!readingfonts::offeredForReading(families[i].name.c_str())) continue;
    readable.push_back(i);
  }
  const int sdCount = static_cast<int>(readable.size());

  // Cycles at both ends, as asked: the modulo is written to work for delta = -1 too, since
  // (-1 % n) is negative in C++.
  const auto wrap = [](const int index, const int count) { return ((index % count) + count) % count; };

  {
    RenderLock lock(*this);

    // Same position-preservation and same ordering as stepReaderFontSize: capture before the
    // reset, reset before ensureLoaded. See the comments there — the reasons are identical
    // and the failure modes (lost place, freed glyph tables mid-draw) are the same.
    if (section) {
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      // Capture a paragraph anchor while the OLD section is still alive. A page
      // index cannot survive a reflow; a paragraph index can, and the LUT that
      // resolves it is already written to every section file. The proportional
      // remap in applyDeferredReposition() stays as the fallback for chapters
      // whose LUT is degenerate (no <p> elements -> paragraph 0 on every page).
      pendingParagraphAnchor = section->getParagraphIndexForPage(static_cast<uint16_t>(section->currentPage));
      pendingWordAnchor = section->getFirstWordAnchorForPage(static_cast<uint16_t>(section->currentPage));
      LOG_DBG("ERS", "Capture: page %d para %d word %ld", section->currentPage,
              pendingParagraphAnchor ? static_cast<int>(*pendingParagraphAnchor) : -1,
              pendingWordAnchor ? static_cast<long>(*pendingWordAnchor) : -1L);
      nextPageNumber = section->currentPage;
    }
    section.reset();

    if (sdCount == 0) {
      const int count = static_cast<int>(CrossPointSettings::BUILTIN_FONT_COUNT);
      const int current = SETTINGS.fontFamily < count ? SETTINGS.fontFamily : 0;
      SETTINGS.fontFamily = static_cast<uint8_t>(wrap(current + delta, count));
      SETTINGS.sdFontFamilyName[0] = '\0';
    } else {
      // An empty sdFontFamilyName means a built-in is selected while SD fonts exist (the
      // picker does not list built-ins then), so start from the first SD family.
      // A family that is no longer offered -- a writing-only face the reader was already
      // sitting on from before this filter existed -- matches nothing here, so `current`
      // stays 0 and the step lands on a listed family. That is the wanted behaviour: the
      // first move off an unlisted font brings you back into the list.
      int current = 0;
      if (SETTINGS.sdFontFamilyName[0] != '\0') {
        for (int i = 0; i < sdCount; i++) {
          if (families[readable[i]].name == SETTINGS.sdFontFamilyName) {
            current = i;
            break;
          }
        }
      }
      const std::string& picked = families[readable[wrap(current + delta, sdCount)]].name;
      strncpy(SETTINGS.sdFontFamilyName, picked.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
      SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
    }

    SETTINGS.saveToFile();
    sdFontSystem.ensureLoaded(renderer);
  }
  requestUpdate();
}

void EpubReaderActivity::openChapterSelection() {
  const int spineIdx = currentSpineIndex;
  const std::string path = epub->getPath();
  startActivityForResult(
      std::make_unique<EpubReaderChapterSelectionActivity>(renderer, mappedInput, epub, path, spineIdx),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          const auto& chapterResult = std::get<ChapterResult>(result.data);
          RenderLock lock(*this);

          currentSpineIndex = chapterResult.spineIndex;

          // If anchor is not empty, it will be used later to calculate the page number.
          pendingAnchor = chapterResult.anchor;

          // Otherwise page 0 will be used.
          nextPageNumber = 0;

          section.reset();
        }
      });
}

void EpubReaderActivity::pageTurn(bool isForwardTurn) {
  if (isForwardTurn) {
    // Advance within the section while there are (or may still be) more pages: either a built
    // page ahead, or the section is still building (windowed), in which case more pages exist
    // beyond the current watermark and render()'s ensure-built pump will lay them out. Only when
    // the section is fully built AND we're on its last page do we move to the next spine -- using
    // the live pageCount alone would mistake the build watermark for the end of a giant spine.
    if (section->currentPage < section->pageCount - 1 || section->isBuilding()) {
      section->currentPage++;
    } else {
      // We don't want to delete the section mid-render, so grab the semaphore
      {
        RenderLock lock(*this);
        nextPageNumber = 0;
        currentSpineIndex++;
        section.reset();
      }
    }
  } else {
    if (section->currentPage > 0) {
      section->currentPage--;
    } else if (currentSpineIndex > 0) {
      // We don't want to delete the section mid-render, so grab the semaphore
      {
        RenderLock lock(*this);
        nextPageNumber = 0;
        pendingPageJump = std::numeric_limits<uint16_t>::max();
        currentSpineIndex--;
        section.reset();
      }
    }
  }
  requestUpdate();
}

// TODO: Failure handling
void EpubReaderActivity::render(RenderLock&& lock) {
  if (!epub) {
    return;
  }

  // A section build failure (e.g. an invalid/corrupt EPUB that fails XML parsing) leaves the
  // "Indexing" popup on screen with no way forward. Surface an explicit error instead of hanging.
  // clearScreen first so the error popup doesn't overlay the stale "Indexing" popup.
  const auto showBuildError = [this]() {
    renderer.clearScreen();
    GUI.drawPopup(renderer, tr(STR_INDEX_FAILED));
  };

  // edge case handling for sub-zero spine index
  if (currentSpineIndex < 0) {
    currentSpineIndex = 0;
  }
  // based bounds of book, show end of book screen
  if (currentSpineIndex > epub->getSpineItemsCount()) {
    currentSpineIndex = epub->getSpineItemsCount();
  }

  // Show end of book screen
  if (currentSpineIndex == epub->getSpineItemsCount()) {
    renderer.clearScreen();
    // 3/8 of the screen height matches the previous fixed position on the 480x800 panel
    // and scales to other resolutions.
    renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() * 3 / 8, tr(STR_END_OF_BOOK), true,
                              EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  // Apply screen viewable areas and additional padding
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += SETTINGS.screenMargin;
  orientedMarginLeft += SETTINGS.screenMargin;
  orientedMarginRight += SETTINGS.screenMargin;

  orientedMarginBottom += SETTINGS.screenMargin;

  const uint16_t viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  const uint16_t viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;
  // Capture for loop()'s lazy partial-extension start (must match this render's layout params).
  buildViewportWidth = viewportWidth;
  buildViewportHeight = viewportHeight;

  const ReaderRenderSpec renderSpec = SETTINGS.readerRenderSpec(viewportWidth, viewportHeight);

  if (!section) {
    const auto filepath = epub->getSpineItem(currentSpineIndex).href;
    LOG_DBG("ERS", "Loading file: %s, index: %d", filepath.c_str(), currentSpineIndex);
    section = std::unique_ptr<Section>(new Section(epub, currentSpineIndex, renderer));
    // Fresh section, fresh chance: a failed lazy extension start in a previous
    // section must not suppress watermark-triggered rebuilds for this one.
    partialRebuildStartFailed = false;

    // A finalized cache serves every page as-is. A partial cache (suspended build from a
    // previous session) serves its pages instantly too, but a build must still run to lay
    // out the rest -- it re-parses from the top in the background (HTML already cached,
    // pages are deterministic) and finalizes, so the partial machinery retires itself.
    const bool cacheLoaded = section->loadSectionFile(renderSpec);
    if (cacheLoaded) {
      // Matching render params means identical pagination, so the saved page number is valid
      // as-is: consume any pending settings-change reposition. Without this, a chapter total
      // saved while the section was still building (i.e. a watermark, not the real count)
      // would remap the resume page against the finalized count and teleport the reader.
      cachedChapterTotalPageCount = 0;
    }
    const bool cacheComplete = cacheLoaded && !section->isPartial();
    if (!cacheComplete) {
      if (section->isPartial()) {
        LOG_DBG("ERS", "Partial cache found (%d pages), resuming build...", section->pageCount);
      } else {
        LOG_DBG("ERS", "Cache not found, building...");
      }

      // Jumps that need the final pagination or the anchor map -- explicit page jumps,
      // fragment anchors, percent jumps, and cross-setting progress repositioning -- can't
      // resolve their landing page until the whole chapter is laid out, so they take the full
      // (blocking) build with the indexing popup. Everything else -- plain forward reads, resume,
      // and explicit page jumps -- only needs a specific page, so it builds incrementally to that
      // page and finishes the rest in loop(). The settings-change reposition (cachedChapterTotal*)
      // is NOT a full-build trigger: it's deferred to applyDeferredReposition() once the real page
      // count is known, so it never blocks the first page.
      // Only a percent jump truly needs the whole chapter up front (percent -> page needs the final
      // page count). Anchor jumps (TOC / chapter select / footnotes) resolve incrementally below --
      // the anchor is recorded as its page is laid out, so a chapter-top anchor lands on page 0
      // without indexing the whole chapter.
      const bool needsFullBuild = pendingPercentJump;
      if (needsFullBuild) {
        GUI.drawPopup(renderer, tr(STR_INDEXING));
        // The popup's own refresh is a plain FAST, so force the page that replaces it onto the HALF
        // ghost-cleanup path -- otherwise the "INDEXING" text ghosts under the rendered page.
        pagesUntilFullRefresh = 1;
        // No popup redraws while the framebuffer is lent to the build below;
        // the panel holds the popup displayed above (e-ink is persistent).
        const auto popupFn = [this]() {
          if (renderer.hasFrameBuffer()) GUI.drawPopup(renderer, tr(STR_INDEXING));
        };
        // Lend the framebuffer's 48 KB to the blocking full build; restored
        // (white) at scope exit, and the page render below redraws everything.
        GfxRenderer::FrameBufferLoan loan(renderer);
        if (!section->createSectionFile(renderSpec, popupFn)) {
          LOG_ERR("ERS", "Failed to persist page data to SD");
          section.reset();
          loan.end();  // restore before anything draws
          showBuildError();
          return;
        }
        loan.end();
      } else {
        // Lay out just enough to show the landing page; loop() builds the rest behind it. Show the
        // indexing popup up front only when the build will actually be slow: a large spine (its
        // whole HTML must be inflated before page 1 can lay out -- the giant single-spine case), or
        // a deep resume/jump that must lay out many pages to reach the landing page. Tiny sections
        // build in a blink and stay popup-free.
        const int target = pendingPageJump.has_value() ? *pendingPageJump : (nextPageNumber < 0 ? 0 : nextPageNumber);
        const bool anchorJump = !pendingAnchor.empty();

        // Landing well inside a partial: the page (or anchor, via the on-disk map) is already
        // servable, so don't restart the extension build now -- it re-lays out the WHOLE chapter
        // from page 0 (minutes of background CPU + SD writes on a giant spine), pure waste when
        // the reader never nears the watermark this session. loop() starts it lazily once the
        // reader is within PARTIAL_REBUILD_START_MARGIN pages of the watermark.
        if (section->isPartial() &&
            (anchorJump ? section->getPageForAnchor(pendingAnchor).has_value()
                        : target + PARTIAL_REBUILD_START_MARGIN < static_cast<int>(section->pageCount))) {
          LOG_DBG("ERS", "Partial covers target %d of %d; deferring extension build", target, section->pageCount);
        } else {
          const size_t spineBytes =
              epub->getCumulativeSpineItemSize(currentSpineIndex) -
              (currentSpineIndex > 0 ? epub->getCumulativeSpineItemSize(currentSpineIndex - 1) : 0);
          // Decided by elapsed time alone (showBuildPopup), so nothing is drawn up front
          // and nothing has to predict how slow this build will be.
          buildPopupPending = true;
          buildStartMs = millis();
          bool started;
          {
            // Lend the framebuffer's 48 KB to startBuild only (the spine HTML
            // inflation peak). The chunk loop below runs without it so the popup
            // can draw mid-build; background chunks never had the loan either.
            GfxRenderer::FrameBufferLoan loan(renderer);
            started = section->startBuild(renderSpec, [this] { showBuildPopup(); });
          }
          if (!started) {
            LOG_ERR("ERS", "Failed to start section build");
            section.reset();
            buildPopupPending = false;
            buildStartMs = 0;
            showBuildError();
            return;
          }
          while (!section->isBuildComplete() &&
                 (anchorJump
                      ? !section->findAnchor(pendingAnchor)
                      : (static_cast<int>(section->pageCount) <= target ||
                         (pendingParagraphAnchor && !section->getPageForParagraphIndex(*pendingParagraphAnchor))))) {
            // Anchor jump: build until the anchor's page is laid out (usually page 0), checking a
            // partial's on-disk anchor map too so an already-indexed anchor resolves immediately.
            // Otherwise: build until the target page exists. loop() builds the rest behind it.
            if (buildPopupPending) {
              // The predictive gates guessed fast but the build blew the silent budget.
              showBuildPopup();
            }
            if (!section->buildSomeMore(BUILD_PAGES_PER_CHUNK)) {
              LOG_ERR("ERS", "Failed during incremental section build");
              section.reset();
              buildPopupPending = false;
              showBuildError();
              return;
            }
          }
          buildPopupPending = false;
          buildStartMs = 0;
        }
      }
    } else {
      LOG_DBG("ERS", "Cache found, skipping build...");
    }

    if (pendingPageJump.has_value()) {
      section->currentPage = *pendingPageJump;
      pendingPageJump.reset();
    } else {
      section->currentPage = nextPageNumber;
      if (section->currentPage < 0) {
        section->currentPage = 0;
      }
    }

    // Resolve pending anchors here, on the same footing as a named anchor,
    // rather than waiting for applyDeferredReposition(). That runs only once
    // the section has finished building, but the build-to-target loop above
    // stops as soon as the target page exists, so on a font or size change the
    // reader would otherwise be shown — and would then PERSIST — the old page
    // index on the new pagination, losing the position for good.
    //
    // Word anchor first, and the paragraph anchor stays GATED while it is
    // armed: in a chapter without <p> elements the paragraph LUT resolves
    // every page to paragraph 0, so letting the paragraph path run first
    // would "successfully" consume the reposition to page 0 — the exact
    // rewind this anchor exists to fix. While the build has not yet laid out
    // past the word anchor, getPageForWordAnchor refuses, the anchor stays
    // armed, and the completion-time applyDeferredReposition() finishes the
    // job (cachedChapterTotalPageCount is deliberately left non-zero).
    if (pendingWordAnchor) {
      if (const auto page = section->getPageForWordAnchor(*pendingWordAnchor)) {
        LOG_DBG("ERS", "Reposition by word anchor %u: page %d -> %u", *pendingWordAnchor, section->currentPage, *page);
        section->currentPage = static_cast<int>(*page);
        pendingWordAnchor.reset();
        pendingParagraphAnchor.reset();
        cachedChapterTotalPageCount = 0;  // consumed: don't also apply the proportional remap
      } else if (!section->isBuilding()) {
        // Fully laid out and still unresolved (pre-v35 cache without a word
        // LUT) — drop to the paragraph anchor below.
        LOG_DBG("ERS", "Word anchor %u did not resolve; trying paragraph", *pendingWordAnchor);
        pendingWordAnchor.reset();
      }
    }
    if (!pendingWordAnchor && pendingParagraphAnchor) {
      if (const auto page = section->getPageForParagraphIndex(*pendingParagraphAnchor)) {
        LOG_DBG("ERS", "Reposition by paragraph %u: page %d -> %u", *pendingParagraphAnchor, section->currentPage,
                *page);
        section->currentPage = *page;
        pendingParagraphAnchor.reset();
        cachedChapterTotalPageCount = 0;  // consumed: don't also apply the proportional remap
      } else if (!section->isBuilding()) {
        // The whole chapter is laid out and the paragraph still does not resolve
        // (no <p> elements, or a degenerate LUT) — leave the proportional remap armed.
        LOG_DBG("ERS", "Paragraph %u did not resolve; leaving proportional remap armed", *pendingParagraphAnchor);
        pendingParagraphAnchor.reset();
      }
    }

    if (!pendingAnchor.empty()) {
      // Resolve from the pages laid out so far and/or the on-disk map (finalized or partial).
      const auto page = section->findAnchor(pendingAnchor);
      if (page) {
        section->currentPage = *page;
        LOG_DBG("ERS", "Resolved anchor '%s' to page %d", pendingAnchor.c_str(), *page);
      } else {
        LOG_DBG("ERS", "Anchor '%s' not found in section %d", pendingAnchor.c_str(), currentSpineIndex);
      }
      pendingAnchor.clear();
    }

    if (pendingPercentJump && section->pageCount > 0) {
      // Apply the pending percent jump now that we know the new section's page count.
      int newPage = static_cast<int>(pendingSpineProgress * static_cast<float>(section->pageCount));
      if (newPage >= section->pageCount) {
        newPage = section->pageCount - 1;
      }
      section->currentPage = newPage;
      pendingPercentJump = false;
    }
  }

  // Extend the build to the requested page if needed (for partials and in-progress builds).
  // This runs every render, so it covers both the first page and any forward turn that gets
  // ahead of the background builder; pages already built do no work here.
  //
  // Crossing a partial's watermark before the extension rebuild has caught up means a
  // synchronous wait spanning the remaining prefix re-layout -- potentially tens of
  // seconds on a giant spine. Show the indexing popup so it isn't a silent freeze
  // (the page that replaces it takes the HALF ghost-cleanup path). Ordinary window
  // catch-ups on a non-partial build are a page or two and stay popup-free.
  if (section->isPartial() && section->currentPage >= static_cast<int>(section->pageCount)) {
    GUI.drawPopup(renderer, tr(STR_INDEXING));
    pagesUntilFullRefresh = 1;
  }
  while (section->isPartial() && section->currentPage >= static_cast<int>(section->pageCount)) {
    // Start a build to extend a partial toward the requested page.
    if (!section->isBuilding() && !section->startBuild(renderSpec)) {
      LOG_ERR("ERS", "Failed to start partial extension build");
      section.reset();
      showBuildError();
      return;
    }
    // Extend until either the target page exists or the build completes.
    while (!section->isBuildComplete() && section->currentPage >= static_cast<int>(section->pageCount)) {
      if (!section->buildSomeMore(BUILD_PAGES_PER_CHUNK)) {
        LOG_ERR("ERS", "Failed during incremental section build");
        section.reset();
        showBuildError();
        return;
      }
    }
  }
  // For an in-progress incremental build, make sure the page we're about to show has been laid out.
  if (section->isBuilding()) {
    while (!section->isBuildComplete() && section->currentPage >= static_cast<int>(section->pageCount)) {
      if (!section->buildSomeMore(BUILD_PAGES_PER_CHUNK)) {
        LOG_ERR("ERS", "Failed during incremental section build");
        section.reset();
        showBuildError();
        return;
      }
    }
  }

  // The requested page is now as built as it will get. If it still lands past the end,
  // clamp to the last real page: the UINT16_MAX "last page" sentinel from backward chapter
  // navigation, an explicit jump beyond a finished chapter, or a stale saved position.
  // Guarded on !isBuilding() because a still-building section's pageCount is only the current
  // watermark (not the final count) and has already been driven far enough by the loops above.
  // Apply a deferred settings-change reposition FIRST, before the clamp below.
  //
  // Order matters and getting it backwards loses the reader's place when pagination
  // SHRINKS (tightening the line spacing, or a smaller font). The remap divides the
  // target page by the OLD page count, so it must see the pre-clamp target. Clamping
  // first fed it an already-truncated value and compounded the error: page 20 of 21
  // tightened to 18 pages clamped to 17, then 17/21 = 0.81 -> page 14 of 18 (~78%)
  // instead of the chapter end (~95%) — a silent 3-page jump backwards.
  //
  // Safe in this order: applyDeferredReposition() no-ops unless the spine matches and
  // the page count actually changed, it clamps its own result to pageCount - 1, and it
  // is disarmed (cachedChapterTotalPageCount == 0) on a plain resume — so the
  // UINT16_MAX "last page" sentinel still falls through to the clamp below.
  applyDeferredReposition();

  // clamp to the last real page: the UINT16_MAX sentinel from backward chapter navigation,
  // an explicit jump beyond a finished chapter, or a stale saved position.
  if (!section->isBuilding() && section->pageCount > 0 &&
      section->currentPage >= static_cast<int>(section->pageCount)) {
    section->currentPage = section->pageCount - 1;
  }

  renderer.clearScreen();

  if (section->pageCount == 0) {
    LOG_DBG("ERS", "No pages to render");
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_CHAPTER), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    LOG_DBG("ERS", "Page out of bounds: %d (max %d)", section->currentPage, section->pageCount);
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_OUT_OF_BOUNDS), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  {
    // Unified page read: the in-progress build's in-RAM table if it has reached the page,
    // otherwise the on-disk file (finalized section, or a partial from a previous session).
    auto p = section->loadPage(section->currentPage);
    if (!p) {
      LOG_ERR("ERS", "Failed to load page from SD - clearing section cache");
      // Retrying rebuilds a transiently corrupt section and usually recovers, but a page that keeps
      // failing would loop forever on a blank screen, so bound the retries before giving up.
      const bool giveUp = ++pageLoadRetryCount > MAX_PAGE_LOAD_RETRIES;
      // Abandon (not suspend) any active build BEFORE clearing: clearCache deletes the files,
      // and the destructor's suspend would otherwise commit tables into a deleted handle.
      section->abandonBuild();
      section->clearCache();
      section.reset();
      if (giveUp) {
        LOG_ERR("ERS", "Page load retry limit reached, aborting");
        pageLoadRetryCount = 0;  // Reset so a later user-initiated navigation can try afresh
        renderer.clearScreen();
        renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_PAGE_LOAD_ERROR), true, EpdFontFamily::BOLD);
        renderer.displayBuffer();
        return;
      }
      requestUpdate();  // Try again after clearing cache
      return;
    }
    pageLoadRetryCount = 0;  // Reset the retry counter once a page loads cleanly

    // Collect footnotes from the loaded page
    currentPageFootnotes = std::move(p->footnotes);

    const auto start = millis();
    renderContents(std::move(p), orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    LOG_DBG("ERS", "Rendered page in %dms", millis() - start);
    lastRenderCompleteMs = millis();
  }
  // Only persist when the position actually changed. render() also runs on re-renders that
  // do not move the position, and writeAtomic is several FAT ops for 6 bytes.
  // Every real page turn changes currentPage, so progress durability is unaffected.
  if (currentSpineIndex != lastSavedSpineIndex || section->currentPage != lastSavedPage ||
      section->pageCount != lastSavedPageCount) {
    if (saveProgress(currentSpineIndex, section->currentPage, section->estimatedTotalPages())) {
      lastSavedSpineIndex = currentSpineIndex;
      lastSavedPage = section->currentPage;
      lastSavedPageCount = section->estimatedTotalPages();
    }
  }
}

bool EpubReaderActivity::applyDeferredReposition() {
  if (cachedChapterTotalPageCount == 0 || !section || section->isBuilding()) {
    return false;
  }
  bool changed = false;
  // Most exact path first: the source position of the first word the owner was
  // looking at, resolved onto the new pagination via the word-anchor LUT. This
  // is what keeps the place in chapters where the paragraph LUT is degenerate
  // (poems: no <p> elements, so every page maps to paragraph 0 and the
  // paragraph path "successfully" rewinds to page 0).
  if (pendingWordAnchor && currentSpineIndex == cachedSpineIndex) {
    if (const auto page = section->getPageForWordAnchor(*pendingWordAnchor)) {
      LOG_DBG("ERS", "Reposition by word anchor %u: page %d -> %u", *pendingWordAnchor, section->currentPage, *page);
      if (static_cast<int>(*page) != section->currentPage) {
        section->currentPage = static_cast<int>(*page);
        changed = true;
      }
      pendingWordAnchor.reset();
      pendingParagraphAnchor.reset();
      cachedChapterTotalPageCount = 0;  // consumed
      return changed;
    }
    // No word LUT (pre-v35 file) or build not yet past the anchor while the
    // callers only invoke this on a complete build — fall back to paragraph.
    LOG_DBG("ERS", "Word anchor %u did not resolve; trying paragraph", *pendingWordAnchor);
    pendingWordAnchor.reset();
  }
  // The paragraph the owner was reading, resolved onto the new pagination. Only
  // the proportional remap below needs a page COUNT, so this works even when
  // the count is an estimate.
  if (pendingParagraphAnchor && currentSpineIndex == cachedSpineIndex) {
    if (const auto page = section->getPageForParagraphIndex(*pendingParagraphAnchor)) {
      LOG_DBG("ERS", "Reposition by paragraph %u: page %d -> %u", *pendingParagraphAnchor, section->currentPage, *page);
      if (static_cast<int>(*page) != section->currentPage) {
        section->currentPage = static_cast<int>(*page);
        changed = true;
      }
      pendingParagraphAnchor.reset();
      cachedChapterTotalPageCount = 0;  // consumed
      return changed;
    }
    // LUT absent or degenerate for this chapter — fall through to the estimate.
    LOG_DBG("ERS", "Paragraph %u did not resolve; using proportional remap", *pendingParagraphAnchor);
    pendingParagraphAnchor.reset();
  }
  // Only remap when the chapter actually re-paginated (e.g. after a settings change). A plain
  // resume has identical pagination, so section->pageCount == cachedChapterTotalPageCount and
  // nothing moves.
  if (currentSpineIndex == cachedSpineIndex && section->pageCount != cachedChapterTotalPageCount) {
    const float progress = static_cast<float>(section->currentPage) / static_cast<float>(cachedChapterTotalPageCount);
    int newPage = static_cast<int>(progress * static_cast<float>(section->pageCount));
    if (newPage < 0) newPage = 0;
    if (section->pageCount > 0 && newPage >= static_cast<int>(section->pageCount)) {
      newPage = section->pageCount - 1;
    }
    if (newPage != section->currentPage) {
      section->currentPage = newPage;
      changed = true;
    }
  }
  cachedChapterTotalPageCount = 0;  // consumed; don't read cached progress again
  return changed;
}

bool EpubReaderActivity::saveProgress(int spineIndex, int currentPage, int pageCount) {
  // Record the paragraph alongside the page so a later font/size change can land
  // on the same text rather than the same page number. Unavailable mid-build or
  // for a chapter with no <p> elements, in which case this writes the 6-byte form.
  int paragraphIndex = -1;
  int64_t wordAnchor = -1;
  if (section) {
    // Asked unconditionally, including mid-build: getParagraphIndexForPage()
    // consults the in-RAM build LUT first, and progress is routinely saved while
    // the section is still building.
    if (const auto p = section->getParagraphIndexForPage(static_cast<uint16_t>(currentPage))) {
      paragraphIndex = static_cast<int>(*p);
    }
    if (const auto a = section->getFirstWordAnchorForPage(static_cast<uint16_t>(currentPage))) {
      wordAnchor = static_cast<int64_t>(*a);
    }
  }
  return EpubReaderUtils::saveProgress(*epub, spineIndex, currentPage, pageCount, paragraphIndex, wordAnchor);
}

namespace {
// Read-aloud capture: walk the display list of the page being shown and
// publish its logical text plus per-word rects over the HAL channel. Called
// only from renderContents — the one site that renders the DISPLAYED page —
// never from Page::render itself, which the idle prewarm also runs against
// the NEXT page ("scan only, no pixels") and which would capture the wrong
// text. On device readAloudCaptureWanted() is inline false and this whole
// function folds away. Contract: the simulator's src/ReadAloudChannel.h.

bool tokenIsBlank(const char* t) {
  for (; *t; ++t)
    if (*t != ' ') return false;
  return true;
}

// The stored words may carry soft hyphens (U+00AD); the renderer never draws
// them, and spoken text must not contain them.
void appendStrippingSoftHyphens(std::string& out, const char* t) {
  for (const char* p = t; *p;) {
    if (static_cast<unsigned char>(p[0]) == 0xC2 && static_cast<unsigned char>(p[1]) == 0xAD) {
      p += 2;
      continue;
    }
    out.push_back(*p++);
  }
}

void captureReadAloudPage(const Page& page, const GfxRenderer& renderer, const int fontId, const int xOffset,
                          const int yOffset) {
  if (!gpio.readAloudCaptureWanted()) return;
  std::string text;
  std::vector<ReadAloudWordRect> rects;
  const int lineH = renderer.getLineHeight(fontId);
  const int spaceW = renderer.getSpaceWidth(fontId);
  const auto clampU16 = [](const int v) { return static_cast<uint16_t>(v < 0 ? 0 : (v > 0xFFFF ? 0xFFFF : v)); };
  // A word the layout hyphen-split across lines arrives as two tokens whose
  // prefix ends in '-' at the end of its line (ParsedText appends the visible
  // hyphen to the stored prefix). The TextBlock arena carries no source
  // anchors, so reuniting them is a display-list heuristic: a line-final '-'
  // joins to the next line's first word, dropping the hyphen; the joined
  // word's fragments publish one rect each, sharing its byte range. Known
  // false positive: a paragraph-final real hyphen. Rare, and the capture
  // audit (gate G0) watches for it.
  bool pendingHyphenJoin = false;
  for (const auto& el : page.elements) {
    if (el->getTag() != TAG_PageLine) {
      pendingHyphenJoin = false;
      continue;
    }
    const auto& line = static_cast<const PageLine&>(*el);
    const auto& block = line.getBlock();
    if (!block || !block->valid() || block->isEmpty()) continue;
    const int lineX = line.xPos + xOffset;
    // yPos is the baseline handed to drawText; the rect wants the line's top.
    // PageLine::yPos is the line's TOP, not its baseline -- it is handed to
    // block->render() as the y origin (Page.cpp:24), and measuring the rendered
    // panel confirms it: with yOffset=9 and lineH=36, lines at yPos 0/54/90/126
    // put their ink at y 15/68/104/137, i.e. yPos + yOffset plus a few px of
    // internal leading above cap height. Subtracting the ascender (26 px) here
    // lifted every rect a full line, so the highlight sat one line above the
    // word being spoken -- and on the first line it clamped to 0 and hid the
    // error. Checked across two fonts (the bigger heading face agrees).
    const int lineTop = line.yPos + yOffset;
    const uint16_t n = block->wordCount();
    bool firstRunOnLine = true;
    uint16_t i = 0;
    while (i < n) {
      if (tokenIsBlank(block->wordText(i))) {
        i++;
        continue;
      }
      // A glue run: consecutive tokens the layout placed with no visible gap
      // (punctuation slices, focus splits, CJK segments) captured as one
      // word, judged by pixel gap because the layout's attach flags are not
      // in the arena either.
      const int runX = lineX + block->wordXpos(i);
      int runEndX = runX;
      std::string runText;
      while (true) {
        appendStrippingSoftHyphens(runText, block->wordText(i));
        runEndX =
            lineX + block->wordXpos(i) + renderer.getTextAdvanceX(fontId, block->wordText(i), block->wordStyle(i));
        i++;
        if (i >= n || tokenIsBlank(block->wordText(i))) break;
        if (lineX + block->wordXpos(i) - runEndX > spaceW / 2) break;
      }
      if (runText.empty()) continue;  // tokens that were nothing but soft hyphens
      const uint16_t rx = clampU16(runX);
      const uint16_t ry = clampU16(lineTop);
      const uint16_t rw = clampU16(runEndX - runX);
      const uint16_t rh = clampU16(lineH);
      if (pendingHyphenJoin && firstRunOnLine && !rects.empty() && !text.empty() && text.back() == '-') {
        text.pop_back();
        const uint32_t off = rects.back().byteOffset;
        text += runText;
        const uint16_t newLen = static_cast<uint16_t>(std::min<size_t>(text.size() - off, 0xFFFF));
        for (auto it = rects.rbegin(); it != rects.rend() && it->byteOffset == off; ++it) it->byteLen = newLen;
        rects.push_back({rx, ry, rw, rh, off, newLen});
      } else {
        if (!text.empty()) text.push_back(' ');
        const uint32_t off = static_cast<uint32_t>(text.size());
        text += runText;
        rects.push_back({rx, ry, rw, rh, off, static_cast<uint16_t>(std::min<size_t>(runText.size(), 0xFFFF))});
      }
      firstRunOnLine = false;
    }
    pendingHyphenJoin = !text.empty() && text.back() == '-';
  }
  gpio.publishReadAloudPage(text.c_str(), text.size(), rects.data(), rects.size());
}
}  // namespace

void EpubReaderActivity::renderContents(std::unique_ptr<Page> page, const int orientedMarginTop,
                                        const int orientedMarginRight, const int orientedMarginBottom,
                                        const int orientedMarginLeft) {
  const auto t0 = millis();
  const int fontId = SETTINGS.getReaderFontId();

  // Read-aloud capture, before any pixel work: the page's element list is
  // already final here, and this function is the commitment that this page is
  // the one being displayed.
  captureReadAloudPage(*page, renderer, fontId, orientedMarginLeft, orientedMarginTop);

  // The image pixel-cache RAM slot lives for exactly one page render (it feeds
  // the BW double-refresh and every grayscale band pass); release it on every
  // exit so nothing stays resident across page turns.
  struct PxcSlotGuard {
    ~PxcSlotGuard() { ImageBlock::releaseRenderCache(); }
  } pxcSlotGuard;

  // Font prewarm: scan pass accumulates text, then prewarm, then real render
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);  // scan pass
  scope.endScanAndPrewarm();
  const auto tPrewarm = millis();

  const bool pageHasImages = page->hasImages();
  const bool pageHasImagesNeedingDecode = pageHasImages && page->hasImagesNeedingDecode();
  const bool manualRefreshPending = forcedRefreshPending;
  forcedRefreshPending = false;
  // The reader starts with zero here, which means the normal refresh cycle
  // would use a HALF refresh for its first page. Keep that same clean base for
  // image pages: their double-FAST path otherwise runs directly over the
  // retained frame after a silent restart (for example, when returning from
  // KOReader sync), leaving the old UI mixed with the image.
  const bool cleanImageBasePending = manualRefreshPending || pagesUntilFullRefresh <= 1;
  const bool needsTextGrayscale = SETTINGS.textAntiAliasing != CrossPointSettings::TEXT_AA_OFF;
  const bool needsAnyGrayscale = needsTextGrayscale || pageHasImages;
  // Pick the glyph-gray -> plane mapping for every grayscale pass below (text
  // only; drawBitmap's image mapping is fixed).
  renderer.setGrayscaleAaStrength(ReaderUtils::textAaStrength());
  const bool tiledGrayscale = needsAnyGrayscale && renderer.supportsStripGrayscale();
  // Whole-plane buffering only pays when the BW refresh genuinely runs async
  // underneath it; on blocking panels (X3) it would just spend ~50 KB for the
  // identical serial timing. Image pages take the blocking double-FAST path
  // below (no async refresh is ever started), so they'd spend the buffers with
  // nothing in flight to overlap.
  const bool overlapRefresh = tiledGrayscale && renderer.supportsAsyncRefresh() && !pageHasImages;
  auto renderGrayscalePass = [&]() {
    if (needsTextGrayscale) {
      page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);
    } else {
      page->renderImages(renderer, fontId, orientedMarginLeft, orientedMarginTop);
    }
  };

  if (pageHasImagesNeedingDecode) {
    page->renderWithImagePlaceholders(renderer, fontId, orientedMarginLeft, orientedMarginTop);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    renderer.clearScreen();
  }

  page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);
  const auto tBwRender = millis();

  if (pageHasImages) {
    // Double FAST_REFRESH with selective image blanking (pablohc's technique):
    // HALF_REFRESH sets particles too firmly for the grayscale LUT to adjust.
    // Instead, blank only the image area and do two fast refreshes.
    // Step 1: Display page with image area blanked (text appears, image area white)
    // Step 2: Re-render with images and display again (images appear clean)
    int16_t imgX, imgY, imgW, imgH;
    if (page->getImageBoundingBox(imgX, imgY, imgW, imgH)) {
      // Image pages intentionally bypass the regular refresh cadence. Preserve
      // a pending clean base before their double-FAST grayscale pipeline.
      if (cleanImageBasePending) {
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      }
      renderer.fillRect(imgX + orientedMarginLeft, imgY + orientedMarginTop, imgW, imgH, false);
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);

      // Re-render page content to restore images into the blanked area
      // Status bar is not re-rendered here to avoid reading stale dynamic values (e.g. battery %)
      page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    } else {
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
    // The image's own page is handled above and doesn't count toward the full
    // refresh cadence. But the grayscale pass below leaves gray charge in the
    // image region that a plain fast diff on the *next* page can't clear, so
    // text there ghosts gray (#2190). Force the next ordinary page onto the
    // HALF ghost-cleanup path, which drives every pixel to its target
    // regardless of residue.
    pagesUntilFullRefresh = 1;
  } else {
    // Async form: start the waveform and return so the grayscale plane rendering
    // below overlaps the panel's refresh time instead of following it.
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh, overlapRefresh);
  }
  const auto tDisplay = millis();

  // Tiled grayscale: render each plane band-by-band, leaving the BW
  // framebuffer intact so no full-frame storeBwBuffer is needed; controller
  // RAM is re-synced from the live framebuffer afterward. The page is
  // re-rendered ceil(H/STRIP_ROWS) times per plane, but renderCharImpl culls
  // out-of-band glyphs before decode so the cost stays close to one render.
  // Both text (drawPixel) and images (DirectPixelWriter) honor the active
  // strip target. When the BW refresh above went out async, the plane
  // rendering below overlaps the panel's refresh time; only the controller
  // RAM writes wait for BUSY.
  if (tiledGrayscale) {
    constexpr int STRIP_ROWS = 80;
    const int gh = renderer.getDisplayHeight();
    const int gwBytes = renderer.getDisplayWidthBytes();
    const size_t planeBytes = static_cast<size_t>(gwBytes) * gh;

    // Render one plane band-by-band into a whole-plane buffer without touching
    // the controller, so it can run while the refresh is still in flight.
    auto renderPlaneToBuffer = [&](const bool lsbPlane, uint8_t* buf) {
      renderer.setRenderMode(lsbPlane ? GfxRenderer::GRAYSCALE_LSB : GfxRenderer::GRAYSCALE_MSB);
      for (int y = 0; y < gh; y += STRIP_ROWS) {
        const int rows = (gh - y < STRIP_ROWS) ? (gh - y) : STRIP_ROWS;
        renderer.beginStripTarget(buf + static_cast<size_t>(y) * gwBytes, y, rows);
        renderer.clearScreen(0x00);
        renderGrayscalePass();
        renderer.endStripTarget();
      }
    };

    // Tiered on heap pressure: two plane buffers hide both plane renders
    // inside the refresh wait; one hides the LSB render (its buffer is reused
    // for MSB after streaming); none falls back to the strip-scratch flow with
    // no overlap. Each buffer is only attempted when it leaves ~60 KB free so
    // the pass never starves concurrent allocations: the next page re-render
    // allocates through throwing std::string paths that abort() on OOM under
    // -fno-exceptions, so a plane buffer that "fits" but eats the render
    // headroom is worse than the strip fallback. Blocking panels skip the
    // buffers entirely (nothing to overlap).
    constexpr size_t PLANE_BUF_HEADROOM = 60000;
    // Free-heap alone ignores fragmentation: taking the largest block for a
    // plane can leave only slivers behind even when total headroom looks fine.
    // Require the block to fit the plane with 16 KB contiguous to spare, which
    // also keeps the advance-table batch scratch viable mid-render (same
    // rationale as BACKGROUND_BUILD_MIN_MAX_ALLOC).
    constexpr size_t PLANE_BUF_MAX_ALLOC_RESERVE = 16 * 1024;
    const auto planeBufFits = [planeBytes] {
      return ESP.getFreeHeap() >= planeBytes + PLANE_BUF_HEADROOM &&
             ESP.getMaxAllocHeap() >= planeBytes + PLANE_BUF_MAX_ALLOC_RESERVE;
    };
    auto lsbPlaneBuf = (overlapRefresh && planeBufFits()) ? makeUniqueNoThrow<uint8_t[]>(planeBytes) : nullptr;
    auto msbPlaneBuf = (lsbPlaneBuf && planeBufFits()) ? makeUniqueNoThrow<uint8_t[]>(planeBytes) : nullptr;

    if (lsbPlaneBuf) {
      renderPlaneToBuffer(true, lsbPlaneBuf.get());
      if (msbPlaneBuf) renderPlaneToBuffer(false, msbPlaneBuf.get());
      const auto tGrayRender = millis();

      renderer.waitRefreshComplete();
      const auto tWait = millis();

      renderer.writeGrayscalePlaneStrip(true, lsbPlaneBuf.get(), 0, gh);
      if (msbPlaneBuf) {
        renderer.writeGrayscalePlaneStrip(false, msbPlaneBuf.get(), 0, gh);
      } else {
        renderPlaneToBuffer(false, lsbPlaneBuf.get());
        renderer.writeGrayscalePlaneStrip(false, lsbPlaneBuf.get(), 0, gh);
      }
      const auto tGrayWrite = millis();

      renderer.setRenderMode(GfxRenderer::BW);
      renderer.displayGrayBuffer();
      const auto tGrayDisplay = millis();

      // BW framebuffer is intact; re-sync controller RAM for the next
      // differential page turn directly from it.
      renderer.cleanupGrayscaleWithFrameBuffer();
      const auto tEnd = millis();

      LOG_DBG("ERS",
              "Page render (tiled async): prewarm=%lums bw_render=%lums display=%lums gray_render=%lums "
              "wait=%lums gray_write=%lums gray_display=%lums cleanup=%lums total=%lums (planes buffered: %d)",
              tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tGrayRender - tDisplay, tWait - tGrayRender,
              tGrayWrite - tWait, tGrayDisplay - tGrayWrite, tEnd - tGrayDisplay, tEnd - t0, msbPlaneBuf ? 2 : 1);
    } else {
      // Per-strip scratch tier: blocking panels (X3) and the OOM fallback.
      // The strip writes below need the panel idle, so wait out any pending
      // async refresh first (no-op on blocking panels).
      auto scratch = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(gwBytes) * STRIP_ROWS);
      renderer.waitRefreshComplete();
      if (!scratch) {
        LOG_ERR("ERS", "OOM: grayscale strip scratch (%d bytes); skipping AA this page", gwBytes * STRIP_ROWS);
        if (overlapRefresh) {
          // The BW refresh ran the shadow-free async path, so controller RAM's
          // differential baseline was never rebuilt. Even with AA skipped it must
          // be re-synced from the intact BW framebuffer, or the next differential
          // update diffs against stale contents.
          renderer.cleanupGrayscaleWithFrameBuffer();
        }
      } else {
        // Bands may be streamed in any order: X4 windows each via setRamArea,
        // X3 via PTL.
        renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
        for (int y = 0; y < gh; y += STRIP_ROWS) {
          const int rows = (gh - y < STRIP_ROWS) ? (gh - y) : STRIP_ROWS;
          renderer.beginStripTarget(scratch.get(), y, rows);
          renderer.clearScreen(0x00);
          renderGrayscalePass();
          renderer.endStripTarget();
          renderer.writeGrayscalePlaneStrip(true, scratch.get(), y, rows);
        }
        const auto tGrayLsb = millis();

        // MSB plane.
        renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
        for (int y = 0; y < gh; y += STRIP_ROWS) {
          const int rows = (gh - y < STRIP_ROWS) ? (gh - y) : STRIP_ROWS;
          renderer.beginStripTarget(scratch.get(), y, rows);
          renderer.clearScreen(0x00);
          renderGrayscalePass();
          renderer.endStripTarget();
          renderer.writeGrayscalePlaneStrip(false, scratch.get(), y, rows);
        }
        const auto tGrayMsb = millis();

        renderer.setRenderMode(GfxRenderer::BW);
        renderer.displayGrayBuffer();
        const auto tGrayDisplay = millis();

        // BW framebuffer is intact; re-sync controller RAM for the next
        // differential page turn directly from it.
        renderer.cleanupGrayscaleWithFrameBuffer();
        const auto tCleanup = millis();

        const auto tEnd = millis();
        LOG_DBG("ERS",
                "Page render (tiled): prewarm=%lums bw_render=%lums display=%lums gray_lsb=%lums "
                "gray_msb=%lums gray_display=%lums cleanup=%lums total=%lums",
                tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tGrayLsb - tDisplay, tGrayMsb - tGrayLsb,
                tGrayDisplay - tGrayMsb, tCleanup - tGrayDisplay, tEnd - t0);
      }
    }
  } else {
    // Fallback path for a controller without strip support. grayscale rendering
    // TODO: Only do this if font supports it
    if (needsAnyGrayscale) {
      // Save the BW frame before the grayscale passes overwrite it, restore
      // after. Only needed when grayscale actually renders.
      if (!renderer.storeBwBuffer()) {
        LOG_ERR("ERS", "Failed to store BW buffer for grayscale render; skipping grayscale this page");
        const auto tEnd = millis();
        LOG_DBG("ERS", "Page render: prewarm=%lums bw_render=%lums display=%lums total=%lums", tPrewarm - t0,
                tBwRender - tPrewarm, tDisplay - tBwRender, tEnd - t0);
        return;
      }
      const auto tBwStore = millis();

      renderer.clearScreen(0x00);
      renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
      renderGrayscalePass();
      renderer.copyGrayscaleLsbBuffers();
      const auto tGrayLsb = millis();

      // Render and copy to MSB buffer
      renderer.clearScreen(0x00);
      renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
      renderGrayscalePass();
      renderer.copyGrayscaleMsbBuffers();
      const auto tGrayMsb = millis();

      // display grayscale part
      renderer.displayGrayBuffer();
      const auto tGrayDisplay = millis();
      renderer.setRenderMode(GfxRenderer::BW);
      renderer.restoreBwBuffer();
      const auto tBwRestore = millis();

      const auto tEnd = millis();
      LOG_DBG("ERS",
              "Page render: prewarm=%lums bw_render=%lums display=%lums bw_store=%lums "
              "gray_lsb=%lums gray_msb=%lums gray_display=%lums bw_restore=%lums total=%lums",
              tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tBwStore - tDisplay, tGrayLsb - tBwStore,
              tGrayMsb - tGrayLsb, tGrayDisplay - tGrayMsb, tBwRestore - tGrayDisplay, tEnd - t0);
    } else {
      // No text AA and no images: BW frame already displayed above, no grayscale
      // to render, so no save/restore.
      const auto tEnd = millis();
      LOG_DBG("ERS", "Page render: prewarm=%lums bw_render=%lums display=%lums total=%lums", tPrewarm - t0,
              tBwRender - tPrewarm, tDisplay - tBwRender, tEnd - t0);
    }
  }
}

void EpubReaderActivity::navigateToHref(const std::string& hrefStr, const bool savePosition) {
  if (!epub) return;

  // Push current position onto saved stack
  if (savePosition && section && footnoteDepth < MAX_FOOTNOTE_DEPTH) {
    savedPositions[footnoteDepth] = {currentSpineIndex, section->currentPage};
    footnoteDepth++;
    LOG_DBG("ERS", "Saved position [%d]: spine %d, page %d", footnoteDepth, currentSpineIndex, section->currentPage);
  }

  // Extract fragment anchor (e.g. "#note1" or "chapter2.xhtml#note1")
  std::string anchor;
  const auto hashPos = hrefStr.find('#');
  if (hashPos != std::string::npos && hashPos + 1 < hrefStr.size()) {
    anchor = hrefStr.substr(hashPos + 1);
  }

  // Check for same-file anchor reference (#anchor only)
  bool sameFile = !hrefStr.empty() && hrefStr[0] == '#';

  int targetSpineIndex;
  if (sameFile) {
    targetSpineIndex = currentSpineIndex;
  } else {
    targetSpineIndex = epub->resolveHrefToSpineIndex(hrefStr);
  }

  if (targetSpineIndex < 0) {
    LOG_DBG("ERS", "Could not resolve href: %s", hrefStr.c_str());
    if (savePosition && footnoteDepth > 0) footnoteDepth--;  // undo push
    return;
  }

  {
    RenderLock lock(*this);
    pendingAnchor = std::move(anchor);
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    section.reset();
  }
  requestUpdate();
  LOG_DBG("ERS", "Navigated to spine %d for href: %s", targetSpineIndex, hrefStr.c_str());
}

void EpubReaderActivity::restoreSavedPosition() {
  if (footnoteDepth <= 0) return;
  footnoteDepth--;
  const auto& pos = savedPositions[footnoteDepth];
  LOG_DBG("ERS", "Restoring position [%d]: spine %d, page %d", footnoteDepth, pos.spineIndex, pos.pageNumber);

  {
    RenderLock lock(*this);
    currentSpineIndex = pos.spineIndex;
    nextPageNumber = pos.pageNumber;
    section.reset();
  }
  requestUpdate();
}
