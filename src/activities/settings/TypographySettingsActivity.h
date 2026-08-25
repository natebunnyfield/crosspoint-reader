#pragma once

#include <I18n.h>
#include <LigatureControl.h>

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "activities/settings/SettingsActivity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

// Typography Settings — the granular typographic controls, on their own screen.
//
// Owner ruling 2026-08-24, verbatim: *"give a full subpage of Typography
// Settings that gives all available typography options with full granularity,
// including toggling each individual ligature."* Placement ruling, same day:
// *"Typography Settings should be between Text Settings and Editing Font"* —
// so it is an ACTION row on the Settings list, opening this screen, exactly
// the shape Reader Font already has.
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
  // One row, of two kinds.
  //
  // A SETTING row is a SettingInfo lifted straight out of getSettingsList() --
  // Line Spacing, Line Grid, Justified Text and the Ligatures master. It is
  // SELECTED, never redefined: the label, the JSON key and the accessor all
  // stay in that one list, so a row appearing on this screen instead of the
  // Settings list cannot change what it stores or what the web API calls it.
  //
  // A LIGATURE row is one input pair of the loaded family, which no static list
  // can express -- see the class comment.
  struct Row {
    bool isLigature = false;
    // Ligature rows: the letters the pair spells ("st", "ffi") and the pair
    // itself. `label` is empty on a setting row, whose title comes from its
    // SettingInfo.
    std::string label;
    uint32_t leftCp = 0;
    uint32_t rightCp = 0;
    // Setting rows only.
    SettingInfo setting;
  };

  // Collects the ligature pairs the active reading family carries, as the
  // UNION across its four styles, deduped and in the order the font's own
  // sorted table gives. Union rather than regular-only because a face may
  // carry a ligature in one style and not another — Edgar's `gy` exists in the
  // italics and not the roman — and a row missing for that reason would read
  // as "this face has no gy ligature", which is false.
  void rebuildRows();
  void toggleCurrentRow();
  // Persist, push the ligature preference down to the font layer, and
  // rebuild the row set. Every write on this screen ends here.
  void applyAndRebuild();

  // True when the only row is the master switch and ligatures are ON, i.e.
  // this face carries nothing to toggle. That state draws a SUBTITLE under the
  // row explaining why, and drawList's row geometry depends on whether
  // subtitles are present -- BaseTheme.h:228 requires the caller's hit-testing
  // and paging to agree with what it drew. Both sides ask this one function so
  // they cannot drift; computing it twice is exactly how a list starts
  // hit-testing rows it did not draw.
  bool isEmptyState() const;

  // Row titles, for the list and for the picker. A setting row's title is its
  // SettingInfo's translated name; a ligature row's is the letters it spells.
  std::string rowTitle(const Row& row) const;

  ButtonNavigator buttonNavigator_;
  std::vector<Row> rows_;
  OptionPopup optionPopup_;
  int selectedIndex_ = 0;
};
