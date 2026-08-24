#include "BookNotesActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

// The note id to string mapping. It lives here, not in lib/Epub, because
// nothing under lib/ calls tr() and these are the only strings in the feature
// a person ever reads.
//
// The order of this table is the order the notes are PRESENTED in, and it is
// deliberately not the enum's order: what a reader most needs to know first is
// the thing that makes a book unreadable (a lock, a missing contents list),
// then the things that change how the words are set, then the things that
// change how the page is arranged.
struct NoteText {
  booknotes::Note note;
  StrId headline;
  StrId body;
  // Which Details figure fills the body's single %u, if any.
  // Encoding fills a %s; every other filled body takes a %u.
  enum class Fill : uint8_t { None, CharsPerLine, Images, CssRules, MissingGlyphs, Encoding, CssUnit } fill;
};

constexpr NoteText kNotes[] = {
    {booknotes::Note::Drm, StrId::STR_BOOK_NOTE_DRM_H, StrId::STR_BOOK_NOTE_DRM_B, NoteText::Fill::None},
    {booknotes::Note::TextEncodingUnsupported, StrId::STR_BOOK_NOTE_ENCODING_H, StrId::STR_BOOK_NOTE_ENCODING_B,
     NoteText::Fill::Encoding},
    {booknotes::Note::NoTableOfContents, StrId::STR_BOOK_NOTE_NO_TOC_H, StrId::STR_BOOK_NOTE_NO_TOC_B,
     NoteText::Fill::None},
    {booknotes::Note::SpineEntriesMissing, StrId::STR_BOOK_NOTE_SPINE_MISSING_H, StrId::STR_BOOK_NOTE_SPINE_MISSING_B,
     NoteText::Fill::None},
    {booknotes::Note::TocEntriesUnresolved, StrId::STR_BOOK_NOTE_TOC_UNRESOLVED_H,
     StrId::STR_BOOK_NOTE_TOC_UNRESOLVED_B, NoteText::Fill::None},
    {booknotes::Note::VerticalWritingIgnored, StrId::STR_BOOK_NOTE_VERTICAL_H, StrId::STR_BOOK_NOTE_VERTICAL_B,
     NoteText::Fill::None},
    {booknotes::Note::AlignmentOverridden, StrId::STR_BOOK_NOTE_ALIGNMENT_H, StrId::STR_BOOK_NOTE_ALIGNMENT_B,
     NoteText::Fill::None},
    {booknotes::Note::JustificationDemoted, StrId::STR_BOOK_NOTE_RAGGED_H, StrId::STR_BOOK_NOTE_RAGGED_B,
     NoteText::Fill::CharsPerLine},
    {booknotes::Note::MissingGlyphs, StrId::STR_BOOK_NOTE_MISSING_GLYPHS_H, StrId::STR_BOOK_NOTE_MISSING_GLYPHS_B,
     NoteText::Fill::MissingGlyphs},
    {booknotes::Note::NoHyphenationForLanguage, StrId::STR_BOOK_NOTE_NO_HYPHENATION_H,
     StrId::STR_BOOK_NOTE_NO_HYPHENATION_B, NoteText::Fill::None},
    {booknotes::Note::EmbeddedFontsIgnored, StrId::STR_BOOK_NOTE_FONTS_H, StrId::STR_BOOK_NOTE_FONTS_B,
     NoteText::Fill::None},
    {booknotes::Note::StylesheetPartlyUnderstood, StrId::STR_BOOK_NOTE_CSS_PARTIAL_H,
     StrId::STR_BOOK_NOTE_CSS_PARTIAL_B, NoteText::Fill::CssRules},
    {booknotes::Note::StylesheetSkipped, StrId::STR_BOOK_NOTE_CSS_SKIPPED_H, StrId::STR_BOOK_NOTE_CSS_SKIPPED_B,
     NoteText::Fill::None},
    {booknotes::Note::CssUnitsUnsupported, StrId::STR_BOOK_NOTE_CSS_UNITS_H, StrId::STR_BOOK_NOTE_CSS_UNITS_B,
     NoteText::Fill::CssUnit},
    {booknotes::Note::ImagesDropped, StrId::STR_BOOK_NOTE_IMAGES_H, StrId::STR_BOOK_NOTE_IMAGES_B,
     NoteText::Fill::Images},
    {booknotes::Note::TablesFlattened, StrId::STR_BOOK_NOTE_TABLES_H, StrId::STR_BOOK_NOTE_TABLES_B,
     NoteText::Fill::None},
    {booknotes::Note::PreformattedCollapsed, StrId::STR_BOOK_NOTE_PRE_H, StrId::STR_BOOK_NOTE_PRE_B,
     NoteText::Fill::None},
};

// Every note the model can raise must have text, or a book would report N notes
// and show fewer. The count is the only thing a compiler can check here.
static_assert(sizeof(kNotes) / sizeof(kNotes[0]) == static_cast<size_t>(booknotes::Note::_COUNT),
              "every booknotes::Note needs a headline and a body");

}  // namespace

int BookNotesActivity::visibleLineCount() const {
  auto metrics = UITheme::getInstance().getMetrics();
  Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  return lineHeight > 0 ? std::max(1, contentHeight / lineHeight) : 1;
}

void BookNotesActivity::buildLines() {
  lines.clear();
  const auto& notes = booknotes::current();
  const auto& detail = notes.details();

  Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  auto metrics = UITheme::getInstance().getMetrics();
  const int textWidth = screen.width - metrics.contentSidePadding * 2;

  // No maxLines ceiling on the wrap: this screen scrolls, so a paragraph that
  // runs past the panel is paged, never ellipsized. wrappedText takes a limit,
  // so hand it one no paragraph can reach rather than one that quietly cuts.
  constexpr int kNoLineLimit = 64;

  auto appendWrapped = [&](const char* text, const bool headline, const bool gapBefore) {
    const auto wrapped = renderer.wrappedText(headline ? UI_12_FONT_ID : UI_10_FONT_ID, text, textWidth, kNoLineLimit);
    bool first = true;
    for (const auto& line : wrapped) {
      lines.push_back(Line{line, headline, gapBefore && first});
      first = false;
    }
  };

  appendWrapped(tr(STR_BOOK_NOTES_INTRO), false, false);

  char filled[640];
  for (const auto& entry : kNotes) {
    if (!notes.has(entry.note)) continue;
    appendWrapped(I18n::getInstance().get(entry.headline), true, true);
    const char* body = I18n::getInstance().get(entry.body);
    switch (entry.fill) {
      case NoteText::Fill::CharsPerLine:
        // Three arguments, and the last two are the SAME live threshold: the
        // note names the limit twice (what it is, and what to get back above),
        // and naming a constant there would make it a lie at four of the five
        // settings the Justified Text row offers. Read live rather than stored
        // alongside the count -- moving the threshold repaginates the book, so
        // the note is re-raised against the value it is about to print.
        snprintf(filled, sizeof(filled), body, static_cast<unsigned>(detail.narrowestCharsPerLine),
                 static_cast<unsigned>(autojustify::clampThreshold(SETTINGS.justifyThresholdChars)),
                 static_cast<unsigned>(autojustify::clampThreshold(SETTINGS.justifyThresholdChars)));
        body = filled;
        break;
      case NoteText::Fill::Images:
        snprintf(filled, sizeof(filled), body, static_cast<unsigned>(detail.imagesDropped));
        body = filled;
        break;
      case NoteText::Fill::CssRules:
        snprintf(filled, sizeof(filled), body, static_cast<unsigned>(detail.cssRulesDropped));
        body = filled;
        break;
      case NoteText::Fill::MissingGlyphs:
        snprintf(filled, sizeof(filled), body, static_cast<unsigned>(detail.missingCodepoints));
        body = filled;
        break;
      case NoteText::Fill::CssUnit:
        // Same provenance as the encoding name, and the same care: it came off
        // the card through notes.bin, and openBook is what terminated it.
        snprintf(filled, sizeof(filled), body, detail.unsupportedCssUnit);
        body = filled;
        break;
      case NoteText::Fill::Encoding:
        // The name came off the card via notes.bin, so it is not trusted to be
        // terminated by whoever wrote it; openBook terminates it on load and
        // this reads no further than that.
        snprintf(filled, sizeof(filled), body, detail.unsupportedEncoding);
        body = filled;
        break;
      case NoteText::Fill::None:
        break;
    }
    appendWrapped(body, false, false);
  }
}

void BookNotesActivity::onEnter() {
  Activity::onEnter();
  scrollLine = 0;
  buildLines();
  requestUpdate();
}

void BookNotesActivity::onExit() { Activity::onExit(); }

void BookNotesActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  const int perPage = visibleLineCount();
  const int maxScroll = std::max(0, static_cast<int>(lines.size()) - perPage);

  auto scrollBy = [this, maxScroll](const int delta) {
    const int target = std::clamp(scrollLine + delta, 0, maxScroll);
    if (target == scrollLine) return;
    scrollLine = target;
    requestUpdate();
  };

  // A wall of prose has no rows to step through, so BOTH pairs page: the front
  // pair by three lines (a nudge, for the last line half off the bottom) and
  // the side pair by a screenful, which is what the side pair means everywhere
  // else in this firmware.
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    scrollBy(perPage);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    scrollBy(-perPage);
    return;
  }

  buttonNavigator.onNextRelease([&scrollBy] { scrollBy(3); });
  buttonNavigator.onPreviousRelease([&scrollBy] { scrollBy(-3); });
  buttonNavigator.onNextContinuous([&scrollBy, perPage] { scrollBy(perPage); });
  buttonNavigator.onPreviousContinuous([&scrollBy, perPage] { scrollBy(-perPage); });
  buttonNavigator.onPageNext([&scrollBy, perPage] { scrollBy(perPage); });
  buttonNavigator.onPagePrevious([&scrollBy, perPage] { scrollBy(-perPage); });
}

void BookNotesActivity::render(RenderLock&&) {
  renderer.clearScreen();

  auto metrics = UITheme::getInstance().getMetrics();
  Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_BOOK_NOTES));

  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int x = screen.x + metrics.contentSidePadding;

  int y = contentTop;
  for (size_t i = static_cast<size_t>(scrollLine); i < lines.size(); i++) {
    const auto& line = lines[i];
    if (line.gapBefore && y > contentTop) {
      y += lineHeight / 2;
    }
    if (y + lineHeight > contentTop + contentHeight) break;
    renderer.drawText(line.headline ? UI_12_FONT_ID : UI_10_FONT_ID, x, y, line.text.c_str(), true,
                      line.headline ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    y += lineHeight;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
