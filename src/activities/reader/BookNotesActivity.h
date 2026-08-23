#pragma once

#include <Epub/BookNotes.h>

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// The verbose half of the book-notice feature (owner ruling 2026-08-23). The
// Select Chapter screen carries a single row saying how many notes this book
// has; this screen is what that row opens, and it is where the explaining
// happens -- one headline and one full paragraph per note, in a reader's words.
//
// It is a SEPARATE screen rather than a band on the chapter list because the
// chapter list is the point of that screen: a notice area verbose enough to be
// useful is several paragraphs, and several paragraphs is the whole panel.
//
// Nothing is computed here. The note set was accumulated during parsing
// (booknotes::Notes) and this screen only wraps text for it, so opening it
// costs one wrap pass over the notes that exist -- at most fourteen.
class BookNotesActivity final : public Activity {
 public:
  explicit BookNotesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BookNotes", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // One wrapped display line, plus whether it is a headline (drawn bold) and
  // whether a blank line precedes it. Built once in onEnter and scrolled
  // through, because wrapping on every render would re-measure every glyph of
  // every paragraph on a device that repaints for a cursor move.
  struct Line {
    std::string text;
    bool headline = false;
    bool gapBefore = false;
  };

  void buildLines();
  int visibleLineCount() const;

  std::vector<Line> lines;
  int scrollLine = 0;
  ButtonNavigator buttonNavigator;
};
