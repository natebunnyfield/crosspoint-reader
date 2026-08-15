#pragma once

#include <GfxRenderer.h>

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Scrolling credits screen: who built this firmware, and what it is built out
// of. Both tables live in ColophonData.h and are generated from the git history
// of the firmware and simulator repositories -- nothing here is hand-written
// about a person, so the screen cannot credit someone the public record does
// not.
//
// The line model is deliberately indirect. There are ~230 contributors, and
// materialising a std::string per rendered line would cost tens of kilobytes of
// heap on a part that has ~380 KB total. Instead `lines` holds a 4-byte
// descriptor per line that points back into the flash-resident tables, and only
// wrapped prose -- the intro, the technology detail lines, and the rare address
// too wide for one line -- is ever copied to the heap.
class ColophonActivity final : public Activity {
 public:
  explicit ColophonActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Colophon", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class LineKind : uint8_t {
    Blank,         // spacer; index unused
    Prose,         // heap string, index into `prose`
    SectionTitle,  // heap string, index into `prose`, drawn in the larger face
    PersonName,    // COLOPHON_PEOPLE[index].name
    PersonMeta,    // COLOPHON_PEOPLE[index] address + first-contribution date
    TechLabel,     // COLOPHON_TECH[index].label
  };

  struct Line {
    LineKind kind;
    uint8_t indent;  // pixels past the content margin
    uint16_t index;
  };

  std::vector<Line> lines;
  std::vector<std::string> prose;

  ButtonNavigator buttonNavigator;
  int topLine = 0;
  int lineHeight = 1;
  int contentTop = 0;
  int linesPerPage = 1;
  int maxWidth = 1;

  void layout();
  // Word-wraps `text` to `maxWidth - indent` and appends the result as `kind`
  // lines. Only for prose, which has spaces to break at.
  void appendWrapped(const char* text, LineKind kind, int indent);
  // Breaks an address across lines. An address is one unbreakable token, so
  // appendWrapped would ellipsise it rather than wrap it; this splits at the
  // address's own seams instead.
  void appendAddress(const char* text, int indent);
  void pageForward();
  void pageBack();
  bool atEnd() const;
  void drawScrollBar() const;
  // The page's drawing, without the clear or the refresh, so render() and the
  // grayscale anti-aliasing pass share one body.
  void drawFrame() const;

  // Formats "address  -  YYYY-MM-DD" for one contributor into `buf`.
  static void formatPersonMeta(uint16_t index, char* buf, size_t bufLen);
};
