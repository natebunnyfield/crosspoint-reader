// The Editor Font specimen, and what the picker must be able to say about each
// face.
//
// THE BUG THIS EXISTS FOR
//
// Editor Font was a five-name popup, and three of those five names did nothing.
// editorfonts::resolve() falls through to a compiled-in mono when the chosen
// family cannot be resolved, silently, so the setting rendered and persisted
// and changed nothing on screen -- twice over:
//
//   1. no shipped card carried the iA families at all (fixed by building them);
//   2. the resolver the editor passed was resolveFontId(), which returns a font
//      id only for the family the READER has resident, and never loads. So even
//      WITH the families installed, a note set to iAWriterQuattro rendered
//      byte-identically to one set to SpaceMono. Measured on the simulator by
//      comparing the text band of the two screenshots.
//
// A preview pane over that is worse than no preview: it would draw the fallback
// under the missing face's name. So the picker resolves exactly the way the
// editor does, and says so when a face is not reachable.
//
// These tests cover the pure parts: what the specimen is made of, and the
// availability decision. Whether a glyph is legible is a device/simulator
// question and asserting it here would be theatre.

#include <gtest/gtest.h>

#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "notes/EditorFonts.h"
#include "notes/MarkdownSpans.h"

namespace {

// STR_EDITOR_FONT_PREVIEW_TEXT (lib/I18n/translations/english.yaml). Restated
// rather than linked: pulling it in means I18nStrings.cpp, the language enum and
// the whole I18n singleton. The point of these tests is that the SHAPE below is
// what the pane draws -- if the yaml changes, this should be updated with it.
const char* kSpecimen =
    "## Thursday, half four\n"
    "Rain again; the **fix** was to *stop*.\n"
    "- 0O 1lI| \xE2\x80\x94 l vs 1, O vs 0\n"
    "> quoted, with a hanging indent\n"
    "1. numbered, `code_span --flag`";

std::vector<std::string> specimenLines() {
  std::vector<std::string> out;
  const char* cursor = kSpecimen;
  while (cursor != nullptr && *cursor != '\0') {
    const char* nl = std::strchr(cursor, '\n');
    out.emplace_back(cursor, nl ? static_cast<size_t>(nl - cursor) : std::strlen(cursor));
    cursor = nl ? nl + 1 : nullptr;
  }
  return out;
}

mdspans::Line analyze(const std::string& s) { return mdspans::analyze(s.c_str(), s.size()); }

bool hasStyle(const mdspans::Line& md, mdspans::Style want) {
  for (size_t i = 0; i < md.spanCount; ++i) {
    if (md.spans[i].style == want) return true;
  }
  return false;
}

// The availability rule the picker applies per row, with the two lookups
// injected the same way editorfonts::resolve takes them.
bool isAvailable(uint8_t storedIndex, const std::function<bool(int)>& isRegistered,
                 const std::function<bool(const char*)>& onCard) {
  if (storedIndex >= editorfonts::FAMILY_COUNT) return false;
  if (const int builtin = editorfonts::builtinFontIdFor(storedIndex); builtin != 0) {
    return isRegistered(builtin);
  }
  return onCard(editorfonts::FAMILIES[storedIndex].family);
}

}  // namespace

TEST(EditorFontPreview, SpecimenCoversEveryBlockTheEditorDraws) {
  const auto lines = specimenLines();
  ASSERT_EQ(lines.size(), 5u);

  // A heading, which the editor renders BOLD rather than bigger -- the editor
  // has one size, so that is the only thing "heading" can mean there.
  const auto head = analyze(lines[0]);
  EXPECT_EQ(head.block, mdspans::Block::Heading2);
  EXPECT_TRUE(mdspans::blockIsBold(head.block));

  // Body prose carrying both emphasis styles: the pair a name cannot show you.
  const auto body = analyze(lines[1]);
  EXPECT_EQ(body.block, mdspans::Block::Paragraph);
  EXPECT_TRUE(hasStyle(body, mdspans::Style::Bold)) << "the specimen must exercise the bold cut";
  EXPECT_TRUE(hasStyle(body, mdspans::Style::Italic)) << "the specimen must exercise the italic cut";

  // The gutter-marker blocks. These are where the faces differ most visibly at
  // a glance, because the marker sits outside the hanging indent.
  EXPECT_EQ(analyze(lines[2]).block, mdspans::Block::Bullet);
  EXPECT_EQ(analyze(lines[3]).block, mdspans::Block::Quote);
  EXPECT_EQ(analyze(lines[4]).block, mdspans::Block::Numbered);
}

TEST(EditorFontPreview, SpecimenKeepsTheCharacterDiscriminationLine) {
  const auto lines = specimenLines();
  // The whole reason a duospace or quattrospace face exists is that these six
  // are distinguishable. If this line is ever edited away, the specimen stops
  // showing the one thing the choice is actually about.
  for (const char* glyphs : {"0O", "1lI"}) {
    EXPECT_NE(lines[2].find(glyphs), std::string::npos) << "missing " << glyphs << " from the specimen";
  }
}

TEST(EditorFontPreview, EveryFaceIsAvailableWhenCompiledInOrOnCard) {
  const auto allRegistered = [](int) { return true; };
  const auto allOnCard = [](const char*) { return true; };
  for (uint8_t i = 0; i < editorfonts::FAMILY_COUNT; i++) {
    EXPECT_TRUE(isAvailable(i, allRegistered, allOnCard)) << editorfonts::FAMILIES[i].label;
  }
}

TEST(EditorFontPreview, CardOnlyFacesAreMarkedWhenTheCardLacksThem) {
  const auto allRegistered = [](int) { return true; };
  const auto emptyCard = [](const char*) { return false; };

  int marked = 0;
  for (uint8_t i = 0; i < editorfonts::FAMILY_COUNT; i++) {
    const bool builtin = editorfonts::FAMILIES[i].builtinFontId != 0;
    const bool avail = isAvailable(i, allRegistered, emptyCard);
    EXPECT_EQ(avail, builtin) << editorfonts::FAMILIES[i].label
                              << ": a compiled-in face is always available, a card-only face is not";
    if (!avail) ++marked;
  }
  EXPECT_GT(marked, 0) << "the whole point is that SOME faces can be missing";
}

TEST(EditorFontPreview, ACompiledInFaceThisBuildOmittedIsNotAvailable) {
  // A font id is just an int: holding one implies nothing about the family
  // being linked in. An OMIT_FONTS build has the ids and none of the glyphs, and
  // reporting those rows as available would put the owner back where they
  // started -- picking a row that quietly does nothing.
  const auto noneRegistered = [](int) { return false; };
  const auto fullCard = [](const char*) { return true; };
  for (uint8_t i = 0; i < editorfonts::FAMILY_COUNT; i++) {
    if (editorfonts::FAMILIES[i].builtinFontId == 0) continue;
    EXPECT_FALSE(isAvailable(i, noneRegistered, fullCard))
        << editorfonts::FAMILIES[i].label << " is compiled in by the table but absent from this build";
  }
}

TEST(EditorFontPreview, OutOfRangeRowIsNotAvailable) {
  const auto yes = [](int) { return true; };
  const auto onCard = [](const char*) { return true; };
  EXPECT_FALSE(isAvailable(editorfonts::FAMILY_COUNT, yes, onCard));
  EXPECT_FALSE(isAvailable(200, yes, onCard));
}
