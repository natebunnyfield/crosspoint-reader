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
  if (const int builtin = editorfonts::builtinFontIdFor(storedIndex, 12); builtin != 0) {
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

TEST(EditorFontPreview, EveryEditorFaceIsCompiledInSoNoneCanBeMissing) {
  // This used to assert the opposite — that SOME face is card-only, so the
  // "Not on card" mark has something to mark. That stopped being true when
  // every editor face became built-in (2026-08-11), and the case failed on a
  // clean tree from then until 2026-08-14 with nobody owning it.
  //
  // Inverted deliberately rather than deleted. The invariant it now pins is
  // load-bearing: because no face can be missing, the marker was dropped from
  // the picker's list rows when the colophon moved to the preview pane
  // (2026-08-14). Add a card-only face and this fires — which is the moment to
  // go and give the mark a home in the rows again, since the pane's label only
  // speaks for the APPLIED face.
  const auto allRegistered = [](int) { return true; };
  const auto emptyCard = [](const char*) { return false; };

  int cardOnly = 0;
  for (uint8_t i = 0; i < editorfonts::FAMILY_COUNT; i++) {
    const bool builtin = editorfonts::FAMILIES[i].builtinFontId[0] != 0;
    const bool avail = isAvailable(i, allRegistered, emptyCard);
    EXPECT_EQ(avail, builtin) << editorfonts::FAMILIES[i].label
                              << ": a compiled-in face is always available, a card-only face is not";
    if (!avail) ++cardOnly;
  }
  EXPECT_EQ(cardOnly, 0) << "an editor face now depends on the card — the picker's rows no longer carry the "
                            "availability mark, so it would go unshown. See EditorFontSelectionActivity's drawList.";
}

TEST(EditorFontPreview, ACompiledInFaceThisBuildOmittedIsNotAvailable) {
  // A font id is just an int: holding one implies nothing about the family
  // being linked in. An OMIT_FONTS build has the ids and none of the glyphs, and
  // reporting those rows as available would put the owner back where they
  // started -- picking a row that quietly does nothing.
  const auto noneRegistered = [](int) { return false; };
  const auto fullCard = [](const char*) { return true; };
  for (uint8_t i = 0; i < editorfonts::FAMILY_COUNT; i++) {
    if (editorfonts::FAMILIES[i].builtinFontId[0] == 0) continue;
    EXPECT_FALSE(isAvailable(i, noneRegistered, fullCard))
        << editorfonts::FAMILIES[i].label << " is compiled in by the table but absent from this build";
  }
}

// PragmataPro is a BUILT-IN row whose glyph tables are gitignored (commercial).
// That combination is new, and it is exactly the shape that could regress into
// "built-in face reported missing": the family name never appears on a card, so
// anything that reached for the card to answer "is this available" would mark it
// unreachable on every device.
//
// The rule: a built-in row's availability is decided ONLY by whether the
// renderer has the glyphs. An empty card must not change the answer for it, and
// a full card must not rescue it -- the two lookups are separate on purpose.
TEST(EditorFontPreview, ABuiltinRowIgnoresTheCardEntirely) {
  const auto registered = [](int) { return true; };
  for (uint8_t i = 0; i < editorfonts::FAMILY_COUNT; i++) {
    if (editorfonts::FAMILIES[i].builtinFontId[0] == 0) continue;
    const bool emptyCard = isAvailable(i, registered, [](const char*) { return false; });
    const bool fullCard = isAvailable(i, registered, [](const char*) { return true; });
    EXPECT_TRUE(emptyCard) << editorfonts::FAMILIES[i].label << " is compiled in and must never be reported missing";
    EXPECT_EQ(emptyCard, fullCard) << editorfonts::FAMILIES[i].label << ": the card must not affect a built-in row";
  }
}

TEST(EditorFontPreview, OutOfRangeRowIsNotAvailable) {
  const auto yes = [](int) { return true; };
  const auto onCard = [](const char*) { return true; };
  EXPECT_FALSE(isAvailable(editorfonts::FAMILY_COUNT, yes, onCard));
  EXPECT_FALSE(isAvailable(200, yes, onCard));
}

// --- Row presentation: parity with Text Settings ----------------------------
//
// Owner ruling 2026-08-09: this picker presents its list identically to the
// reading picker -- typeface-name title, "Designer · YEAR PLACE" colophon
// subtitle over two lines. The subtitle used to be the availability note alone,
// so every one of these is new behaviour.

constexpr const char* kNotOnCard = "Not on card";

TEST(EditorFontPreview, AvailableRowShowsTheColophonAndNothingElse) {
  for (uint8_t i = 0; i < editorfonts::FAMILY_COUNT; i++) {
    const std::string sub = editorfonts::rowSubtitle(i, /*available=*/true, kNotOnCard);
    EXPECT_FALSE(sub.empty()) << editorfonts::FAMILIES[i].label << " must carry a colophon like a reading family";
    EXPECT_EQ(sub.find(kNotOnCard), std::string::npos)
        << "a reachable row must not be marked: a badge on four rows out of five is noise";
    // The reading picker's shape: the designer, then a LINE BREAK, then their
    // year and place. The middle dot that used to join them was removed on
    // 2026-08-14 — it read as a list bullet and it spent width the picker column
    // did not have, ellipsizing the half of the credit that matters. A deeper
    // lineage (more than four lines of information) collapses back to one
    // bulleted line per stage, so accept either separator and require only that
    // designer and lineage are divided at all.
    const bool separated = sub.find('\n') != std::string::npos || sub.find("\xC2\xB7") != std::string::npos;
    EXPECT_TRUE(separated) << "subtitle must separate designer from lineage: " << sub;
  }
}

TEST(EditorFontPreview, UnavailableRowLeadsWithTheMarkSoItCannotBeEllipsized) {
  // PREPENDED, not appended. The theme wraps the subtitle over kColophonLines
  // and truncates the overflow, so a mark on the tail of a two-line colophon is
  // exactly what gets cut -- and the one fact the owner needs is the one that
  // would vanish. rfind(..., 0) == 0 is "starts with".
  for (uint8_t i = 0; i < editorfonts::FAMILY_COUNT; i++) {
    const std::string sub = editorfonts::rowSubtitle(i, /*available=*/false, kNotOnCard);
    EXPECT_EQ(sub.rfind(kNotOnCard, 0), 0u) << "the availability mark must lead the subtitle: " << sub;
    // and the colophon is still there, after it -- the mark ADDS to the row,
    // it does not replace what the reading picker would have shown.
    EXPECT_GT(sub.size(), std::strlen(kNotOnCard)) << "the mark must not replace the colophon";
    EXPECT_NE(sub.find(editorfonts::rowSubtitle(i, /*available=*/true, kNotOnCard)), std::string::npos)
        << "the full colophon must survive the mark: " << sub;
    EXPECT_EQ(sub.find(editorfonts::FAMILIES[i].label), std::string::npos)
        << "the title line already carries the face name; repeating it in the subtitle wastes both lines";
  }
}

TEST(EditorFontPreview, RowSubtitleDegradesRatherThanShowingPunctuationAlone) {
  // No marker supplied (or an empty one) must not leave a dangling separator,
  // and an out-of-range stored index -- a settings.json written against a
  // longer table -- must produce nothing rather than read past the array.
  const std::string noMarker = editorfonts::rowSubtitle(0, /*available=*/false, "");
  EXPECT_EQ(noMarker, editorfonts::rowSubtitle(0, /*available=*/true, ""));
  EXPECT_EQ(editorfonts::rowSubtitle(0, /*available=*/false, nullptr), noMarker);

  EXPECT_EQ(editorfonts::rowSubtitle(editorfonts::FAMILY_COUNT, false, kNotOnCard), "");
  EXPECT_EQ(editorfonts::rowSubtitle(200, true, kNotOnCard), "");
}

// The new row carries a real colophon, so it presents like every other row
// rather than falling to the bottom of the picker as an undated unknown -- the
// failure mode a face added to FAMILIES without a FontDisplayNames entry has.
TEST(EditorFontPreview, ThePragmataProRowPresentsLikeTheOthers) {
  bool found = false;
  for (uint8_t i = 0; i < editorfonts::FAMILY_COUNT; i++) {
    if (std::strcmp(editorfonts::FAMILIES[i].family, "PragmataPro") != 0) continue;
    found = true;
    const std::string sub = editorfonts::rowSubtitle(i, /*available=*/true, kNotOnCard);
    EXPECT_FALSE(sub.empty()) << "PragmataPro must carry a colophon";
    // Either separator: a one-stage lineage like this one stacks the designer
    // over their year and place with a line break, and only a lineage deeper
    // than four information lines falls back to the middle dot. This case was
    // written against the pre-2026-08-14 dot-only form.
    const bool separated = sub.find('\n') != std::string::npos || sub.find("\xC2\xB7") != std::string::npos;
    EXPECT_TRUE(separated) << "designer and lineage must be separated: " << sub;
    EXPECT_EQ(sub.find(kNotOnCard), std::string::npos) << "a compiled-in face must not be marked: " << sub;
  }
  EXPECT_TRUE(found) << "PragmataPro row is gone from FAMILIES";
}
