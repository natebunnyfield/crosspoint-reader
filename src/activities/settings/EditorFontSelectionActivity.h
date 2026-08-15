#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"

// The Editor Font picker, with a specimen pane like Text Settings.
//
// It replaces a five-name OptionPopup. These faces are a monospace, a duospace,
// a quattrospace and two true monos, and the whole difference between them is
// glyph-width behavior -- which is precisely what a name cannot tell you.
//
// Two things make it different from FontSelectionActivity:
//
//   - The editor has ONE size. editorfonts::resolve asks for 12 pt and nothing
//     else, so there is no size axis and the specimen is drawn at 12 pt rather
//     than at whatever the reader is set to.
//   - The specimen is MARKDOWN, drawn through mdrender::drawLine -- the same
//     function NoteEditorActivity draws with. The editor styles headings, bold,
//     italic, bullets, quotes and numbered lists, so a reading specimen would
//     show none of what these faces are actually chosen for.
class EditorFontSelectionActivity final : public Activity {
 public:
  explicit EditorFontSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Applies the highlighted face immediately and stays on screen, same contract
  // as the reading picker: no commit step, Back just leaves.
  void applySelectedFont();

  // Font id the pane should draw in, resolved the SAME way NoteEditorActivity
  // resolves it -- so the specimen cannot show a face the editor would not use.
  // Loads the card family when needed, so it MUST be called under a RenderLock.
  int resolveAppliedFontId();

  // Is this row's family actually reachable: compiled in, or present on the
  // card? A cheap registry lookup, no file IO -- safe to call from render().
  bool isAvailable(uint8_t storedIndex) const;

  void renderPreviewPane(int top, int height, int fontId, const char* faceName, bool available) const;
  // Specimen text only (no label, no pane furniture), so the grayscale
  // anti-aliasing passes can re-render exactly the glyphs the BW pass drew.
  // Same split, and same reason, as FontSelectionActivity.
  void renderPreviewSpecimen(int top, int height, int fontId) const;
  // Colophon for the face the pane shows, wrapped and capped at kColophonLines;
  // both pane passes size their bottom reserve from it so the BW pass and the
  // grayscale AA pass cannot disagree about where the specimen ends.
  std::vector<std::string> previewColophonLines(int width) const;
  int previewColophonHeight(int width) const;

  ButtonNavigator buttonNavigator_;
  // Display positions -> editorfonts::FAMILIES indices, from
  // editorfonts::displayOrder(): compiled-in faces first.
  std::vector<uint8_t> rows_;
  int selectedIndex_ = 0;
  int appliedIndex_ = 0;
  // Resolved once per apply rather than per frame: resolving can LOAD a card
  // family, which evicts fonts under the render task and must not happen from
  // render().
  int appliedFontId_ = 0;

  ThemeMetrics metrics_ = {};
  int afterHeader = 0;
  int bottomReserved = 0;
  int usableHeight = 0;
  int previewHeight = 0;
  int listHeight = 0;
};
