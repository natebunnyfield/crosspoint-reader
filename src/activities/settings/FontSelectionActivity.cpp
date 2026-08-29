#include "FontSelectionActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "FontActivation.h"
#include "FontDisplayNames.h"
#include "MappedInputManager.h"
#include "ReaderFontSizes.h"
#include "ReadingFontList.h"
#include "SdCardFontSystem.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "notes/EditorFonts.h"

namespace {
constexpr const char* ELLIPSIS_UTF8 = "\xe2\x80\xa6";

// Colophon lines the PREVIEW PANE reserves. Five is the worst case in the
// table, reached two different ways: two lineage stages at a person and a
// year-place each, plus the blank line that separates the originator from
// whoever digitised their work; or Inknut + Junicode, which is deep
// enough that FontDisplayNames::subtitle() collapses it to one bulleted line
// per stage -- four of them -- plus the blank that separates the two
// TYPEFACES (FontDisplayNames::Entry::groupBreakAfter). Nothing exceeds this,
// and nothing has slack: a sixth line is dropped without a word.
constexpr int kColophonLines = 5;

// Families visible at once. The list rows are the family name alone now, so a
// row is one line instead of six, and five families fit in less height than
// three subtitled ones used to take. The credit moved to the pane, which is
// where the face is actually being judged — the list is only the set you step
// through.
constexpr int kVisibleFontRows = 5;

// Air between the pane's label and the colophon beneath it.
constexpr int kColophonGap = 6;

// Resolve the current selection to a POSITION IN `fonts_`.
//
// This must search the built list rather than compute an index: `settingIndex`
// is the persisted value ID (built-ins 0..BUILTIN_FONT_COUNT-1, SD fonts
// BUILTIN_FONT_COUNT + i), which only coincides with the list position while
// the built-in entries are present. They are hidden whenever SD fonts are
// installed, so the two numbering schemes diverge.
template <typename FontList>
int findCurrentFontIndex(const FontList& fonts, const SdCardFontRegistry* registry, const char* sdFontFamilyName,
                         uint8_t fontFamily) {
  // An SD font is selected: match by settingIndex derived from registry order.
  if (sdFontFamilyName[0] != '\0' && registry) {
    const auto& families = registry->getFamilies();
    for (int i = 0; i < static_cast<int>(families.size()); i++) {
      if (families[i].name == sdFontFamilyName) {
        const auto target = static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i);
        for (int p = 0; p < static_cast<int>(fonts.size()); p++) {
          if (!fonts[p].isBuiltin && fonts[p].settingIndex == target) return p;
        }
      }
    }
  }

  // A built-in is selected: only findable when built-ins are being shown.
  for (int p = 0; p < static_cast<int>(fonts.size()); p++) {
    if (fonts[p].isBuiltin && fonts[p].settingIndex == fontFamily) return p;
  }

  // Selection is not in the list (e.g. a built-in was selected before SD fonts
  // were installed, or the stored SD font is no longer present). Fall back to
  // the first entry rather than leaving an out-of-range index.
  return 0;
}
}  // namespace

FontSelectionActivity::FontSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const SdCardFontRegistry* registry)
    : Activity("FontSelect", renderer, mappedInput), registry_(registry) {}

void FontSelectionActivity::onEnter() {
  Activity::onEnter();

  // Re-assert the reader's SD family before anything reads it. The resident font
  // is NOT guaranteed to be SETTINGS.sdFontFamilyName on entry: the Lyra Six theme
  // loads GTAlpinaCond for Home's titles, and loadForDisplay() evicts the reader
  // family to do it (SdCardFontSystem.h documents the eviction). Home always
  // renders between the reader and here, so without this the first preview draws
  // with getReaderFontId()'s BUILT-IN fallback — Libre Franklin — while the
  // label above it still names the real family.
  // The list build below also reads the registry, and ensureLoaded() may clear a
  // missing family, so this has to come first.
  {
    // ActivityManager releases the render lock before calling onEnter, so take
    // our own — same reason changeFontSize() and applySelectedFont() do.
    RenderLock lock(*this);
    sdFontSystem.ensureLoaded(renderer);
  }

  // Get metrics and calculate layout dimensions
  metrics_ = UITheme::getInstance().getMetrics();
  afterHeader = metrics_.topPadding + metrics_.headerHeight + metrics_.verticalSpacing;
  bottomReserved = metrics_.buttonHintsHeight + metrics_.verticalSpacing;
  usableHeight = renderer.getScreenHeight() - afterHeader - bottomReserved;

  // The LIST is sized first here, opposite to previewHeightPercent's usual
  // ordering, because the row count is the fixed quantity now. Whole rows only,
  // so the last one is never drawn clipped, and clamped to the space that
  // exists in case a theme's row grows past a third of the screen.
  const int listBudget = std::max(0, usableHeight - metrics_.verticalSpacing);
  // Name-only rows now, so the step is a single line and more families fit in
  // less height than three subtitled rows used to take.
  const int rowStep = GUI.getListRowStep(false, 1);
  listHeight = rowStep > 0 ? std::min(rowStep * kVisibleFontRows, (listBudget / rowStep) * rowStep)
                           : usableHeight * (100 - metrics_.previewHeightPercent) / 100;
  previewHeight = usableHeight - metrics_.verticalSpacing - listHeight;

  const int sdFontCount = registry_ ? static_cast<int>(registry_->getFamilyCount()) : 0;

  fonts_.clear();
  fonts_.reserve(sdFontCount > 0 ? sdFontCount : CrossPointSettings::BUILTIN_FONT_COUNT);

  if (registry_) {
    const auto& families = registry_->getFamilies();
    for (int i = 0; i < static_cast<int>(families.size()); i++) {
      // ONE predicate, shared with the in-book cycle (src/ReadingFontList.h).
      // It withholds writing-only editor faces -- they live on the card exactly
      // as a reading family does, so without this a card carrying them grew
      // extra reading families; iA Writer Quattro is the deliberate exception
      // (owner ruling 2026-08-09) and carries alsoReading -- and retired
      // families a card may still carry from before their ruling.
      //
      // settingIndex still counts EVERY registry family, skipped ones included:
      // it addresses the registry, and renumbering it here would re-point
      // SETTINGS.sdFontFamilyName's resolution at a different family.
      if (!readingfonts::offeredForReading(families[i].name.c_str())) continue;
      fonts_.push_back({families[i].name, false, static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i)});
    }
    // Reverse chronological by lineage: newest EARLIEST (creation) year first,
    // per FontDisplayNames. Undated (unlisted/user-installed) families sort
    // last; ties fall back to the display name so the order is stable run to
    // run. settingIndex travels with each row, so reordering never changes
    // which registry family a row selects.
    // The rule itself lives in readingfonts:: because the in-book cycle has to
    // walk this same order, and a comparator spelled out in two places drifts
    // -- which is exactly what happened here (the sets matched, the orders did
    // not).
    std::sort(fonts_.begin(), fonts_.end(), [](const FontEntry& a, const FontEntry& b) {
      return readingfonts::sortsBefore(a.name.c_str(), b.name.c_str());
    });
  }

  // The built-in Libre Franklin is hidden once the user has installed their own
  // READING fonts, so the list shows only their set. It is listed whenever
  // nothing else is — the picker must never be empty, and Libre Franklin is the
  // fallback CrossPointSettings::getReaderFontId() resolves to when a selected
  // SD font cannot be loaded.
  //
  // Keyed on the built list, not on sdFontCount: a card can now carry families
  // this picker skips, so "the registry found something" stopped meaning "this
  // picker has a row". A card holding ONLY the editor faces left it empty --
  // no rows, no selection, and no way back to a working reading font.
  if (fonts_.empty()) {
    fonts_.push_back(
        {I18N.get(StrId::STR_LIBRE_FRANKLIN), true, static_cast<uint8_t>(CrossPointSettings::BUILTIN_LIBRE_FRANKLIN)});
  }

  selectedIndex_ = findCurrentFontIndex(fonts_, registry_, SETTINGS.sdFontFamilyName, SETTINGS.fontFamily);
  previewFontIndex_ = selectedIndex_;

  requestUpdate();
}

void FontSelectionActivity::onExit() { Activity::onExit(); }

void FontSelectionActivity::loop() {
  // Back on PRESS. ActivityManager::swallowUntilIdle() is called at every
  // activity swap, so the outgoing activity's press edge is never visible to
  // the incoming one. FontSelectionActivity is launched only from Settings
  // (not from the reader), so the old concern about EpubReaderActivity seeing
  // a leaked release no longer applies. Exiting on press gives immediate
  // feedback and matches every other settings sub-screen.
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    // Choices apply as they are made, so Back just leaves. There is no
    // separate confirm step and nothing to roll back.
    finish();
    return;
  }

  // Confirm splits on context, and the button hint says which meaning applies:
  //
  //   cursor on a row that is NOT applied -> apply that font ("Select")
  //   cursor on the applied row           -> swap the specimen ("Sample")
  //
  // Applying is never lost — every other row still does it, and the applied row
  // has nothing left to apply. The specimen swap lives here because that is
  // exactly where a user sits while judging the face they just chose.
  // CONFIRM IS A TAP, AND A HOLD IS A DIFFERENT GESTURE.
  //
  // The tap moved from the PRESS edge to the RELEASE edge when the hold was
  // added, and it had to: a press-edge action fires before any hold can be
  // recognised, so the font would already have been applied by the time the
  // button had been held long enough to mean something else. This is the same
  // shape the reader's side button already uses -- the hold fires AT the
  // threshold while the button is still down (immediate feedback, no waiting
  // for a lift), and marks the release it will end with as spent so the tap
  // branch does not also run.
  //
  // Moving Confirm to release is safe here for the reason the Back comment
  // above gives: ActivityManager::swallowUntilIdle() runs at every activity
  // swap, so the press that launched this screen cannot leak its release in.
  if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
    if (!confirmHoldFired_ && mappedInput.getHeldTime() >= ReaderUtils::SKIP_HOLD_MS) {
      confirmHoldFired_ = true;
      toggleSelectedFontActive();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const bool spent = confirmHoldFired_;
    confirmHoldFired_ = false;  // the gesture is over: re-arm for the next one
    if (spent) return;
    if (selectedIndex_ == previewFontIndex_) {
      proseSpecimen_ = !proseSpecimen_;
      requestUpdate();
    } else {
      applySelectedFont();
    }
    return;
  }

  // Side buttons step the reader font size, so a font can be judged at the
  // size it will actually be read at.
  //
  // PageBack/PageForward (rather than Up/Down) so the user's side-button swap
  // preference is honored, consistent with page turns in the reader. Note
  // that this means the size control is unavailable when side buttons are set
  // to Disabled — the same tradeoff page turning already makes.
  if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
    changeFontSize(+1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
    changeFontSize(-1);
    return;
  }

  const int listSize = static_cast<int>(fonts_.size());
  // hasSubtitle=true: the list rows carry a designer/years subtitle, so the
  // page stride must use the taller subtitle row height or continuous paging
  // would jump past entries the screen never showed.
  //
  // No cap: the stride is whatever the theme says fits below the preview pane,
  // which is the same question render()'s list rect answers, so the page stride
  // and the drawn page still agree.
  const int pageItems =
      UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false, previewHeight + metrics_.verticalSpacing, 1);

  // List navigation is bound to the FRONT buttons only. NavNext/NavPrevious are
  // the front pair now (2026-08-15, MappedInputManager.cpp) so getNextButtons()
  // would work, but spell it out: this screen must NOT pick up the side pair's
  // page handlers either, because the side buttons here step the reader font
  // size. This is one of the two screens the "side buttons page" ruling
  // deliberately exempts — see the size handler above and
  // docs/ui-conventions.md.
  // ButtonNavigator::Buttons is a private alias, so spell out the vector type.
  static const std::vector<MappedInputManager::Button> kNextButtons = {MappedInputManager::Button::Right};
  static const std::vector<MappedInputManager::Button> kPreviousButtons = {MappedInputManager::Button::Left};

  buttonNavigator_.onRelease(kNextButtons, [this, listSize] {
    notice_.clear();  // a one-shot line: it must not outlive the row it was about
    selectedIndex_ = ButtonNavigator::nextIndex(selectedIndex_, listSize);
    requestUpdate();
  });

  buttonNavigator_.onRelease(kPreviousButtons, [this, listSize] {
    notice_.clear();
    selectedIndex_ = ButtonNavigator::previousIndex(selectedIndex_, listSize);
    requestUpdate();
  });

  buttonNavigator_.onContinuous(kNextButtons, [this, listSize, pageItems] {
    notice_.clear();
    selectedIndex_ = ButtonNavigator::nextPageIndex(selectedIndex_, listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator_.onContinuous(kPreviousButtons, [this, listSize, pageItems] {
    notice_.clear();
    selectedIndex_ = ButtonNavigator::previousPageIndex(selectedIndex_, listSize, pageItems);
    requestUpdate();
  });
}

void FontSelectionActivity::changeFontSize(int delta) {
  // Steps the SLOT, not a point size: the slot is what persists and what
  // survives a family switch. Clamp rather than wrap — wrapping from the largest
  // size back to the smallest reads as a glitch, and clamping makes the ends of
  // the range perceptible. readerFontPointSizes never returns empty (it falls
  // back to the built-in ramp), so its size bounds the slot.
  const std::vector<uint8_t> sizes = readerFontPointSizes(registry_, SETTINGS.sdFontFamilyName);
  if (sizes.empty()) return;
  const int maxSlot = static_cast<int>(sizes.size()) - 1;
  const int nextSlot = std::clamp(static_cast<int>(SETTINGS.fontSizeSlot) + delta, 0, maxSlot);
  if (nextSlot == static_cast<int>(SETTINGS.fontSizeSlot)) return;

  {
    // RenderLock is REQUIRED here, not defensive. ActivityManager::loop() calls
    // us with no lock held by design ("the loop() method must be responsible
    // for acquire one if needed", ActivityManager.cpp:64), while the render
    // task is concurrently inside render() holding a reference into the font
    // map (`const auto& font = fontIt->second`, GfxRenderer.cpp). ensureLoaded
    // -> SdCardFontManager::unloadAll does
    // clearSdCardFonts() / removeFont(id) / delete lf.font — it erases that
    // map node and frees the glyph tables under the renderer's feet. On device
    // that is a LoadProhibited panic or garbled glyphs; the host simulator
    // renders in 5-8ms so the window never opens there.
    //
    // Also serialises the SETTINGS.fontSizeSlot / fontPointSize writes, which
    // the render task reads every frame (resolvedPointSize / getReaderFontId).
    RenderLock lock(*this);
    SETTINGS.fontSizeSlot = static_cast<uint8_t>(nextSlot);
    SETTINGS.fontPointSize = sizes[nextSlot];
    // An SD family keeps only one point size resident, so the new size has to be
    // loaded before the preview can draw at it. Built-in faces just resolve to a
    // different font ID.
    sdFontSystem.ensureLoaded(renderer);
  }
  // Outside the lock: requestUpdate only sets a deferred flag / notifies the
  // render task. requestUpdateAndWait would deadlock (ActivityManager.cpp:303).
  requestUpdate();
}

void FontSelectionActivity::toggleSelectedFontActive() {
  if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(fonts_.size())) return;
  const auto& font = fonts_[selectedIndex_];

  // The built-in is not on the card and cannot be switched off. It is listed
  // only when nothing else is (see rebuild()), which is exactly the state the
  // last-active rule protects -- so refusing here is the same rule, one layer
  // out.
  if (font.isBuiltin) {
    notice_ = tr(STR_FONT_LAST_ACTIVE);
    requestUpdate();
    return;
  }

  // The count is taken over the families THIS PICKER offers, not the raw
  // registry: a count including writing-only editor faces would let the last
  // READING family be switched off, and the symptom would surface later as a
  // font cycle that does nothing.
  std::vector<const char*> offered;
  offered.reserve(fonts_.size());
  for (const auto& f : fonts_) {
    if (!f.isBuiltin) offered.push_back(f.name.c_str());
  }

  const std::string family = font.name;
  const fontactivation::Result result = fontactivation::toggle(SETTINGS.fontsOff, sizeof(SETTINGS.fontsOff),
                                                               family.c_str(), offered.data(), offered.size());

  switch (result) {
    case fontactivation::Result::RefusedLast:
      notice_ = tr(STR_FONT_LAST_ACTIVE);
      requestUpdate();
      return;
    case fontactivation::Result::RefusedName:
    case fontactivation::Result::NoRoom:
      // Both are storage refusals rather than policy ones, and both are rare
      // enough that a dedicated string would be a string nobody ever reads.
      // Saying nothing, though, is the one thing that must not happen: the
      // hold would look unrecognised.
      notice_ = tr(STR_FONTS_OFF);
      requestUpdate();
      return;
    case fontactivation::Result::Reactivated:
      notice_.clear();
      break;
    case fontactivation::Result::Deactivated:
      notice_.clear();
      // DEACTIVATING THE FAMILY BEING READ HAS TO MOVE THE READER. Leaving it
      // applied would put the reader on a font its own cycle can no longer
      // reach, so the next ACTIVE family in this list's order takes over --
      // the same order the in-book cycle walks, so "next" means the same thing
      // in both places. The last-active rule above guarantees one exists.
      if (selectedIndex_ == previewFontIndex_) {
        const int count = static_cast<int>(fonts_.size());
        for (int step = 1; step < count; step++) {
          const int candidate = (selectedIndex_ + step) % count;
          if (fonts_[candidate].isBuiltin) continue;
          if (fontactivation::isDeactivated(SETTINGS.fontsOff, fonts_[candidate].name.c_str())) continue;
          const int keepCursor = selectedIndex_;
          selectedIndex_ = candidate;
          applySelectedFont();          // moves previewFontIndex_ too
          selectedIndex_ = keepCursor;  // the cursor stays on the row just toggled
          break;
        }
      }
      break;
  }

  SETTINGS.saveToFile();
  requestUpdate();
}

void FontSelectionActivity::applySelectedFont() {
  previewFontIndex_ = selectedIndex_;
  const auto& font = fonts_[selectedIndex_];
  {
    // Same reason as changeFontSize: the strncpy into SETTINGS.sdFontFamilyName
    // and SdCardFontManager::loadedFamilyName_ are both read by the render task
    // every frame, and ensureLoaded frees the resident SdCardFont. The write and
    // the unload have to be atomic with respect to render().
    RenderLock lock(*this);
    if (font.isBuiltin) {
      SETTINGS.fontFamily = font.settingIndex;
      SETTINGS.sdFontFamilyName[0] = '\0';
    } else if (registry_) {
      const int sdIdx = font.settingIndex - CrossPointSettings::BUILTIN_FONT_COUNT;
      const auto& families = registry_->getFamilies();
      if (sdIdx < static_cast<int>(families.size())) {
        strncpy(SETTINGS.sdFontFamilyName, families[sdIdx].name.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
        SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
      }
    }

    // Unconditional, and this is the fix for a real leak: the old code only
    // reloaded inside the SD branch, so choosing a built-in after an SD family
    // cleared sdFontFamilyName without ever calling unloadAll(). The previous
    // .cpfont stayed resident, and the waste compounded with every switch —
    // which on a 380KB device is enough to exhaust the heap.
    sdFontSystem.ensureLoaded(renderer);
  }
  requestUpdate();
}

uint8_t FontSelectionActivity::resolvedPointSize() const {
  // Mirrors CrossPointSettings::getReaderFontId()'s resolution order, so the
  // number shown always describes the specimen actually on screen: an SD
  // family wins if it resolves, otherwise it falls through to the built-in ramp.
  if (SETTINGS.sdFontFamilyName[0] != '\0' && registry_ != nullptr) {
    if (const auto* family = registry_->findFamily(SETTINGS.sdFontFamilyName)) {
      // An SD family only ships the sizes it ships; the slot indexes into that
      // family's own ramp, which is why the same slot is a different point size
      // here than on the built-in ramp.
      if (const auto* file = family->findClosestReaderSize(SETTINGS.fontSizeSlot)) return file->pointSize;
    }
  }

  // Built-in Libre Franklin is compiled at a fixed ramp; the slot indexes
  // straight into it.
  return pointSizeForSlot(BUILTIN_READER_POINT_SIZES, std::size(BUILTIN_READER_POINT_SIZES), SETTINGS.fontSizeSlot);
}

void FontSelectionActivity::renderPreviewPane(int top, int height, int fontId, const char* fontName) const {
  const int left = metrics_.previewPadding;
  const int width = renderer.getScreenWidth() - (metrics_.previewPadding * 2);
  if (width <= 0 || height <= 0) return;

  const int labelFontId = UI_10_FONT_ID;
  const int labelH = renderer.getTextHeight(labelFontId);

  // Include the current size: the side buttons change it, but they have no
  // on-screen hint (the hint row covers the front buttons only), so this is
  // what makes the control discoverable and its effect legible.
  //
  // Read live from SETTINGS rather than cached at entry, so this stays correct
  // whether the size was changed here or on the Font Size settings screen.
  // The slot name matches the wording used there; the point size is appended
  // because the slot is ordinal and resolves per-family.
  // ONE scratch buffer, deliberately shared with the prewarm string below.
  //
  // This runs on the render task, whose stack is tight — freeink-ui.md records
  // the failure mode as a "Stack canary watchpoint triggered (loopTask)" panic.
  // This frame previously held labelBuf[160] + sizeBuf[48] + prewarmBuf[256] =
  // 464 bytes at once, on top of the std::strings that truncatedText() and
  // wrappedText() return. The label and the prewarm string are never live at
  // the same time, so they share storage and the frame roughly halves.
  char scratch[256];
  const uint8_t pointSize = resolvedPointSize();
  if (pointSize > 0) {
    // Slot first, then what it resolves to on this family: the slot is the thing
    // the owner selected and the only part that means the same across families.
    // ONE definition of the slot name, shared with the Font Size settings row
    // (readerSlotLabel, src/ReaderFontSizes.h). This site used to carry its own
    // copy of the names AND its own out-of-range rule -- it clamped a too-large
    // slot to index 0, so a family with more installed sizes than there are
    // names showed the LARGEST size as "XXS". Owner report 2026-08-27.
    //
    // The label already carries the point size, so nothing is appended here.
    const std::vector<uint8_t> sizes = readerFontPointSizes(registry_, SETTINGS.sdFontFamilyName);
    snprintf(scratch, sizeof(scratch), "%s \"%s\" — %s", tr(STR_PREVIEW), fontName ? fontName : "",
             readerSlotLabel(sizes, SETTINGS.fontSizeSlot).c_str());
  } else {
    snprintf(scratch, sizeof(scratch), "%s \"%s\"", tr(STR_PREVIEW), fontName ? fontName : "");
  }

  // A REFUSAL SPEAKS HERE, replacing the caption for one render. The caption is
  // where the eye already is (it names the family being judged), and a refusal
  // that drew nothing at all would be indistinguishable from the hold never
  // having been recognised -- which is the failure mode this whole line exists
  // to prevent.
  if (!notice_.empty()) {
    snprintf(scratch, sizeof(scratch), "%s", notice_.c_str());
  }

  // Name, then credit — the order every crediting convention uses, from a
  // library catalogue's statement of responsibility to a foundry's specimen.
  // The label names the face; the colophon under it says who is responsible
  // for it and when. Both sit at the pane's foot so the specimen keeps the top.
  const auto colophon = previewColophonLines(width);
  const int colophonBlockH = previewColophonHeight(width);
  const int labelY = top + height - metrics_.previewPadding - colophonBlockH - labelH;
  // Guard against a negative origin: a short pane, or a large label font, can
  // drive this above the top edge, and the renderer logs
  // "!! Outside range ... -> (x, -1)" and writes outside the framebuffer.
  if (labelY >= 0) {
    const std::string safeLabel = renderer.truncatedText(labelFontId, scratch, width);
    renderer.drawText(labelFontId, left, labelY, safeLabel.c_str());

    const int colophonStep = renderer.getLineHeight(SMALL_FONT_ID);
    int colophonY = labelY + labelH + kColophonGap;
    for (const auto& line : colophon) {
      if (!line.empty()) renderer.drawText(SMALL_FONT_ID, left, colophonY, line.c_str(), true);
      colophonY += colophonStep;
    }
  }

  if (fontId == 0) return;

  if (auto* fcm = renderer.getFontCacheManager()) {
    if (proseSpecimen_) {
      // The prose passage is far longer than `scratch`, so warm each string
      // directly and only in the style it is drawn in: body regular (0x01),
      // closing line italic (0x04). The ellipsis comes from truncation in
      // either of them.
      //
      // ONE call per style, with the ellipsis CONCATENATED rather than warmed
      // separately afterwards. A second prewarm of the same style rebuilds that
      // style's mini working set from scratch whenever the new request is not
      // already covered by it (SdCardFont::prewarmStyle) — so a trailing
      // prewarm(ELLIPSIS, 0x05) replaced style 0's whole prose working set with
      // the single ellipsis glyph. Every body character then fell through to
      // the 16-slot on-demand overflow ring, which for a working set that size
      // is LRU's worst case: 100% misses, one .cpfont open+seek+read PER
      // CHARACTER, per redraw. Measured in the simulator before this fix,
      // ~110,000 on-demand loads across two renders; on device those are real
      // SD reads and the screen simply stops responding. Reported as "freezing
      // on change font size while long text sample is previewing" — the size
      // change makes it worse because ensureLoaded() clears the cache first.
      //
      // The non-prose branch below has always done it this way.
      const std::string proseWarm = std::string(I18N.get(StrId::STR_FONT_PREVIEW_PROSE)) + ELLIPSIS_UTF8;
      const std::string italicWarm = std::string(I18N.get(StrId::STR_FONT_PREVIEW_PROSE_ITALIC)) + ELLIPSIS_UTF8;
      fcm->prewarmCache(fontId, proseWarm.c_str(), 0x01);
      fcm->prewarmCache(fontId, italicWarm.c_str(), 0x04);
    } else {
      // Reuses `scratch` — the label was drawn above and is no longer needed.
      snprintf(scratch, sizeof(scratch), "%s %s", I18N.get(StrId::STR_FONT_PREVIEW_TEXT), ELLIPSIS_UTF8);
      // 0x0F warms all four style variants. The specimen below renders each one,
      // and an SD card font that misses the cache re-reads glyph data from the
      // card per character.
      fcm->prewarmCache(fontId, scratch, 0x0F);
    }
  }

  renderPreviewSpecimen(top, height, fontId);
}

// Guards and geometry are re-derived from the same renderer + SETTINGS inputs
// as renderPreviewPane above, so a second call within the same frame draws the
// identical glyph set — which is what the grayscale AA passes require (they
// flag pixels the BW base pass painted black). Keep the two derivations in
// lockstep. The prewarm stays in renderPreviewPane: by the time the AA passes
// re-render, the glyphs are already cached.
// Segmenting mirrors LyraTheme::drawList's subtitle walk: a '\n' is an EXPLICIT
// break (one lineage stage per pair of lines), and an EMPTY segment is the
// deliberate blank line between an originator and their digitiser, which has to
// be emitted directly because wrappedText() has no words to lay out for it.
std::vector<std::string> FontSelectionActivity::previewColophonLines(int width) const {
  std::vector<std::string> out;
  if (width <= 0) return out;
  if (previewFontIndex_ < 0 || previewFontIndex_ >= static_cast<int>(fonts_.size())) return out;
  if (fonts_[previewFontIndex_].isBuiltin) return out;

  const std::string text = FontDisplayNames::subtitle(fonts_[previewFontIndex_].name);
  if (text.empty()) return out;

  out.reserve(kColophonLines);
  size_t segStart = 0;
  while (segStart <= text.size() && static_cast<int>(out.size()) < kColophonLines) {
    const size_t nl = text.find('\n', segStart);
    const std::string segment = text.substr(segStart, nl == std::string::npos ? std::string::npos : nl - segStart);
    if (segment.empty()) {
      out.push_back(std::string());
    } else {
      for (auto& line :
           renderer.wrappedText(SMALL_FONT_ID, segment.c_str(), width, kColophonLines - static_cast<int>(out.size()))) {
        if (static_cast<int>(out.size()) >= kColophonLines) break;
        out.push_back(std::move(line));
      }
    }
    if (nl == std::string::npos) break;
    segStart = nl + 1;
  }
  return out;
}

int FontSelectionActivity::previewColophonHeight(int width) const {
  const auto lines = previewColophonLines(width);
  if (lines.empty()) return 0;
  return kColophonGap + static_cast<int>(lines.size()) * renderer.getLineHeight(SMALL_FONT_ID);
}

void FontSelectionActivity::renderPreviewSpecimen(int top, int height, int fontId) const {
  const int left = metrics_.previewPadding;
  const int width = renderer.getScreenWidth() - (metrics_.previewPadding * 2);
  if (width <= 0 || height <= 0) return;

  const int labelH = renderer.getTextHeight(UI_10_FONT_ID);
  const int labelGap = 4;
  // Same reserve renderPreviewPane draws into — label, then the colophon under
  // it. Derived from the same call so the AA pass lays the specimen out exactly
  // where the BW pass did.
  const int labelReserved = labelH + labelGap + metrics_.previewPadding + previewColophonHeight(width);

  if (fontId == 0) return;

  const int lineH = renderer.getTextHeight(fontId);
  if (lineH <= 0) return;

  const int innerHeight = height - metrics_.previewPadding - labelReserved;
  const int lineStep = lineH + 2;
  const int maxLines = std::max(1, innerHeight / lineStep);

  // Lines are COLLECTED first and drawn second, so the block can be centered in
  // the pane. The pane is now around half the screen (the list below it is
  // capped at kVisibleFontRows), and a four-line style grid pinned to the top
  // left a band of white between the specimen and its label.
  struct SpecimenLine {
    std::string text;
    EpdFontFamily::Style style;
    int gapBefore;
  };
  std::vector<SpecimenLine> lines;
  lines.reserve(static_cast<size_t>(maxLines));

  const char* previewText = I18N.get(StrId::STR_FONT_PREVIEW_TEXT);

  if (proseSpecimen_) {
    // Prose specimen: continuous text, word-wrapped to the pane, closing with
    // an italic sentence. The style grid shows what the four cuts look like;
    // this shows what the face READS like — color on the page, word-space
    // rhythm, how the eye moves line to line — which a pangram cannot tell you.
    // Wrapped (not one string per line) so it stays honest at every size slot:
    // at XL the same passage simply shows fewer lines of itself.
    //
    // The closing line(s) are reserved BEFORE the body is wrapped, so the
    // italic is never the thing squeezed out; it is half of what this specimen
    // exists to show.
    const int italicLines = maxLines >= 4 ? 2 : (maxLines >= 2 ? 1 : 0);
    const int bodyLines = std::max(1, maxLines - italicLines);

    for (auto& line : renderer.wrappedText(fontId, I18N.get(StrId::STR_FONT_PREVIEW_PROSE), width, bodyLines)) {
      lines.push_back({std::move(line), EpdFontFamily::REGULAR, 0});
    }
    if (italicLines > 0) {
      bool first = true;
      for (auto& line : renderer.wrappedText(fontId, I18N.get(StrId::STR_FONT_PREVIEW_PROSE_ITALIC), width, italicLines,
                                             EpdFontFamily::ITALIC)) {
        // Half a line of air between the passage and its closing sentence.
        lines.push_back({std::move(line), EpdFontFamily::ITALIC, first ? lineStep / 2 : 0});
        first = false;
      }
    }
  } else if (std::strchr(previewText, '\n') != nullptr) {
    // A specimen containing newlines is a per-style sample: one line each in
    // regular, bold, italic and bold-italic, so all four can be judged
    // together. The styles are left unlabelled deliberately — they are
    // self-evident, and a label sitting next to a specimen competes with the
    // thing being evaluated.
    static constexpr EpdFontFamily::Style kStyles[] = {EpdFontFamily::REGULAR, EpdFontFamily::BOLD,
                                                       EpdFontFamily::ITALIC, EpdFontFamily::BOLD_ITALIC};
    constexpr int kStyleCount = static_cast<int>(sizeof(kStyles) / sizeof(kStyles[0]));
    const char* cursor = previewText;
    for (int i = 0; i < kStyleCount && cursor != nullptr; ++i) {
      const char* newline = std::strchr(cursor, '\n');
      const std::string segment =
          newline ? std::string(cursor, static_cast<size_t>(newline - cursor)) : std::string(cursor);
      cursor = newline ? newline + 1 : nullptr;
      if (segment.empty()) continue;
      if (static_cast<int>(lines.size()) >= maxLines) break;  // fewer lines fit at large sizes
      // A font lacking a style falls back to regular inside EpdFontFamily, so
      // an incomplete family degrades rather than failing.
      lines.push_back({renderer.truncatedText(fontId, segment.c_str(), width, kStyles[i]), kStyles[i], 0});
    }
  } else {
    // Translations that have not been given a multi-line specimen are a single
    // string and keep the original wrap-in-regular behavior, so nothing breaks
    // for the other 28 languages.
    for (auto& line : renderer.wrappedText(fontId, previewText, width, maxLines)) {
      lines.push_back({std::move(line), EpdFontFamily::REGULAR, 0});
    }
  }

  int blockHeight = 0;
  for (const auto& line : lines) blockHeight += line.gapBefore + lineStep;
  blockHeight = std::max(0, blockHeight - 2);

  const int textBottomLimit = top + height - labelReserved;
  int y = top + metrics_.previewPadding + std::max(0, (innerHeight - blockHeight) / 2);
  for (const auto& line : lines) {
    y += line.gapBefore;
    if (y + lineH > textBottomLimit) break;
    renderer.drawText(fontId, left, y, line.text.c_str(), /*black=*/true, line.style);
    y += lineStep;
  }
}

void FontSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();

  // Titled for what the screen now covers — family and size, applied live —
  // rather than just the family list it started as.
  GUI.drawHeader(renderer, Rect{0, metrics_.topPadding, pageWidth, metrics_.headerHeight}, tr(STR_TEXT_SETTINGS));

  const int previewTop = afterHeader;
  const int listTop = previewTop + previewHeight + metrics_.verticalSpacing;

  const int previewFontId = SETTINGS.getReaderFontId();
  // Typeface name WITHOUT the designer credit: the label already carries the
  // resolved point size, and the list row directly below shows the attribution.
  // Held in a local because renderPreviewPane takes a const char* — a temporary
  // from displayName() would dangle.
  std::string previewName;
  if (previewFontIndex_ >= 0 && previewFontIndex_ < static_cast<int>(fonts_.size())) {
    const auto& picked = fonts_[previewFontIndex_];
    previewName = picked.isBuiltin ? picked.name : FontDisplayNames::displayName(picked.name);
  }
  const char* previewFontName = previewName.empty() ? nullptr : previewName.c_str();
  renderPreviewPane(previewTop, previewHeight, previewFontId, previewFontName);

  // drawLine's horizontal fast path is inclusive of x2 (`for (x = x1; x <= x2)`),
  // so the last valid column is pageWidth - 1. Passing pageWidth drew one pixel
  // past the right edge; in Portrait that maps to phyY = panelHeight - 1 - 528
  // = -1 and logged "!! Outside range (528, 302) -> (302, -1)". drawRect gets
  // this right (`x + width - 1`); this call did not.
  const int separatorY = listTop - metrics_.verticalSpacing / 2;
  renderer.drawLine(0, separatorY, pageWidth - 1, separatorY);

  // previewFontIndex_ is now simply "what is applied": choices take effect on
  // Confirm and are never rolled back, so there is no longer a tentative
  // preview distinct from the real selection, and no need to remember what the
  // user arrived with. One badge, on the applied font.
  GUI.drawList(
      renderer, Rect{0, listTop, pageWidth, listHeight}, static_cast<int>(fonts_.size()), selectedIndex_,
      // Title: the typeface name alone (FontDisplayNames.h). Built-in entries
      // already carry a translated display name, so only SD families are
      // mapped; an unlisted family falls through to its own directory name.
      [this](int index) {
        return fonts_[index].isBuiltin ? fonts_[index].name : FontDisplayNames::displayName(fonts_[index].name);
      },
      // NO subtitle. The credit does not belong in the list: it is about the
      // face being judged, not about navigating to it, and one row per family
      // is what lets the list BE a comparison set — five families on screen
      // instead of three (owner ruling 2026-08-14). The colophon is now a
      // permanent part of the preview pane, where it sits next to the specimen
      // it describes.
      nullptr, nullptr,
      [this](int index) -> std::string {
        // A DEACTIVATED FAMILY STAYS LISTED, and says so here. The picker is
        // the only place to turn it back on, so filtering it out would make a
        // long hold a one-way door. "Off" beside the row is the state; the
        // row itself is drawn exactly as any other, because a deactivated font
        // is switched off, not broken.
        if (!fonts_[index].isBuiltin && fontactivation::isDeactivated(SETTINGS.fontsOff, fonts_[index].name.c_str())) {
          return tr(STR_FONT_OFF);
        }
        if (index == previewFontIndex_) return tr(STR_SELECTED);
        return "";
      },
      true, nullptr, 1);

  // Confirm carries two meanings and the label says which one is live: on any
  // row but the applied one it applies that font, on the applied row it swaps
  // the specimen. Without the alternating label the second meaning would be a
  // hidden gesture.
  const char* confirmLabel = (selectedIndex_ == previewFontIndex_) ? tr(STR_SAMPLE) : tr(STR_SELECT);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();

  // Grayscale AA pass over the preview specimen, mirroring the reader's
  // pipeline (TxtReaderActivity::renderPage): the BW base frame is on the
  // panel from displayBuffer() above; re-render just the specimen text into
  // the LSB/MSB planes and push them via the gray LUT path. Everything outside
  // the specimen stays unflagged ("leave alone"), so no extra full flash is
  // introduced. renderAntiAliased() picks the strength mapping (On/Crisp/Dark)
  // from SETTINGS.textAntiAliasing itself; at Off this whole block is skipped
  // and the render is byte-identical to the pure-BW path. This is what makes
  // the specimen an honest sample of the reader with AA enabled — the same
  // glyph edges the reader softens are softened here.
  if (SETTINGS.textAntiAliasing != CrossPointSettings::TEXT_AA_OFF) {
    ReaderUtils::renderAntiAliased(renderer, [&] { renderPreviewSpecimen(previewTop, previewHeight, previewFontId); });
  }
}
