#pragma once

#include <I18n.h>
#include <LigatureControl.h>

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Typography Settings — the granular typographic controls, on their own screen.
//
// Owner ruling 2026-08-24, verbatim: *"give a full subpage of Typography
// Settings that gives all available typography options with full granularity,
// including toggling each individual ligature."* Placement ruling, same day:
// *"Typography Settings should be between Text Settings and Editing Font"* —
// so it is an ACTION row on the Settings list, opening this screen, exactly
// the shape Text Settings already has.
//
// WHY IT IS A SCREEN AND NOT ROWS ON THE SETTINGS LIST
//
// The per-ligature rows are one per pair the CURRENTLY LOADED family carries,
// and that number moves: Edgar ships fourteen across its four styles, Almendra
// four. A shared getSettingsList() entry cannot express a row set that depends
// on which .cpfont is resident, and pouring fourteen ligature rows into the
// device's one flat Settings list would bury the eleven settings already
// there. Its own screen is also what the ruling asked for.
class TypographySettingsActivity final : public Activity {
 public:
  explicit TypographySettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Typography", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // One row. `pair` is 0 for the rows that are not a ligature (the master
  // switch), which is safe because no real pair packs to 0 — U+0000 is not a
  // codepoint any face maps.
  struct Row {
    StrId nameId = StrId::STR_NONE_OPT;  // used when `label` is empty
    std::string label;                   // the letters a ligature spells, e.g. "st"
    uint32_t leftCp = 0;
    uint32_t rightCp = 0;
    bool isLigature = false;
  };

  // Collects the ligature pairs the active reading family carries, as the
  // UNION across its four styles, deduped and in the order the font's own
  // sorted table gives. Union rather than regular-only because a face may
  // carry a ligature in one style and not another — Edgar's `gy` exists in the
  // italics and not the roman — and a row missing for that reason would read
  // as "this face has no gy ligature", which is false.
  void rebuildRows();
  void toggleCurrentRow();

  // True when the only row is the master switch and ligatures are ON, i.e.
  // this face carries nothing to toggle. That state draws a SUBTITLE under the
  // row explaining why, and drawList's row geometry depends on whether
  // subtitles are present -- BaseTheme.h:228 requires the caller's hit-testing
  // and paging to agree with what it drew. Both sides ask this one function so
  // they cannot drift; computing it twice is exactly how a list starts
  // hit-testing rows it did not draw.
  bool isEmptyState() const;

  ButtonNavigator buttonNavigator_;
  std::vector<Row> rows_;
  int selectedIndex_ = 0;
};
