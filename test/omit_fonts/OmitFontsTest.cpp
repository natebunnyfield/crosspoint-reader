// The OMIT_FONTS contract: which built-in fonts a stripped build MUST still
// register.
//
// THE BUG THIS EXISTS FOR
//
// OMIT_FONTS is defined by exactly one build -- the iOS target -- to strip the
// built-in reading faces it cannot reach. It removes the font DATA, but
// src/fontIds.h keeps handing out the ids unconditionally, so every caller
// still believes those fonts exist. Nothing fails to compile, nothing warns,
// and the only symptom is a renderer that quietly has no glyphs for an id it
// was just handed.
//
// That is exactly how the editor fonts shipped dead on iOS: Space Mono and IBM
// Plex Mono are the fallback the editor resolves to on a blank card, both sat
// inside the guard, and on the phone -- the ONE build that defines it -- all
// five Editor Font rows fell through to UI chrome. It was invisible on desktop
// because desktop never defines OMIT_FONTS. Three separate bugs had to be
// fixed before the setting worked, and this was the deepest.
//
// The rule that fixes the CLASS of bug: a font a build always needs must be
// registered outside the guard, and that must be checked by something other
// than a comment. This test parses main.cpp's guard structure and pins the
// exact set that survives, so moving one back inside trips here instead of
// shipping.
//
// Parsing the source is deliberate. The alternative -- linking main.cpp -- is
// impossible in a host test (Arduino, SdFat, the whole device stack), and the
// invariant is genuinely textual: it is about which side of a preprocessor
// guard a line sits on.

#include <gtest/gtest.h>

#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string trimmed(const std::string& s) {
  const size_t b = s.find_first_not_of(" \t");
  return b == std::string::npos ? "" : s.substr(b);
}

// Font ids registered at guard depth 0 -- i.e. present even when OMIT_FONTS is
// defined. Tracks only OMIT_FONTS guards; other #ifdefs are irrelevant here and
// counting them would mis-attribute depth.
std::set<std::string> fontsSurvivingOmitFonts(const std::string& path) {
  std::ifstream in(path);
  EXPECT_TRUE(in.good()) << "cannot read " << path;
  std::set<std::string> out;
  std::string line;
  int depth = 0;
  while (std::getline(in, line)) {
    const std::string s = trimmed(line);
    const bool opensGuard = s.rfind("#if", 0) == 0 && s.find("OMIT_FONTS") != std::string::npos;
    const bool closesGuard = s.rfind("#endif", 0) == 0 && s.find("OMIT_FONTS") != std::string::npos;
    if (opensGuard) {
      depth++;
      continue;
    }
    if (closesGuard) {
      depth = depth > 0 ? depth - 1 : 0;
      continue;
    }
    const size_t call = s.find("renderer.insertFont(");
    if (call == std::string::npos || depth != 0) continue;
    const size_t open = s.find('(', call);
    const size_t comma = s.find(',', open);
    if (open == std::string::npos || comma == std::string::npos) continue;
    out.insert(trimmed(s.substr(open + 1, comma - open - 1)));
  }
  return out;
}

const char* kMainCpp = CROSSPOINT_MAIN_CPP;

}  // namespace

TEST(OmitFonts, TheEditorFacesSurviveAStrippedBuild) {
  const auto survive = fontsSurvivingOmitFonts(kMainCpp);
  // All five editor faces must survive OMIT_FONTS: they are the answer every
  // Editor Font row must resolve to in every build, iOS included. Inside the
  // guard they vanish on iOS and every row dies silently.
  EXPECT_TRUE(survive.count("SPACEMONO_12_FONT_ID"))
      << "Space Mono must register even with OMIT_FONTS -- editor blank-card fallback";
  EXPECT_TRUE(survive.count("IBMPLEXMONO_12_FONT_ID"))
      << "IBM Plex Mono must register even with OMIT_FONTS, same reason";
  EXPECT_TRUE(survive.count("IAWRITERQUATTRO_12_FONT_ID"))
      << "iA Writer Quattro must register even with OMIT_FONTS (ruling 2026-08-11)";
  EXPECT_TRUE(survive.count("IAWRITERDUO_12_FONT_ID"))
      << "iA Writer Duo must register even with OMIT_FONTS (ruling 2026-08-11)";
  EXPECT_TRUE(survive.count("IAWRITERMONO_12_FONT_ID"))
      << "iA Writer Mono must register even with OMIT_FONTS (ruling 2026-08-11)";
}

TEST(OmitFonts, TheReaderDefaultSurvivesAStrippedBuild) {
  const auto survive = fontsSurvivingOmitFonts(kMainCpp);
  // getReaderFontId()'s default answer has to resolve in every build, or the
  // reader opens with no face at all.
  EXPECT_TRUE(survive.count("LIBREFRANKLIN_READER_14_FONT_ID"))
      << "the reader's default 14 pt face must resolve in every build";
}

TEST(OmitFonts, TheSurvivingSetIsExactlyWhatWeIntend) {
  // Pinned whole, not just membership: a font added outside the guard by
  // accident is the mirror of this bug -- it silently re-inflates the binary
  // the guard exists to shrink. Adding one deliberately means updating this
  // list and saying why.
  const std::set<std::string> want = {
      "LIBREFRANKLIN_READER_14_FONT_ID",
      "SPACEMONO_12_FONT_ID",
      "IBMPLEXMONO_12_FONT_ID",
      "IAWRITERQUATTRO_12_FONT_ID",
      "IAWRITERDUO_12_FONT_ID",
      "IAWRITERMONO_12_FONT_ID",
      // Added 2026-08-14 with the PragmataPro row. Outside the guard for the
      // same reason as the five above: OMIT_FONTS is the iOS build, and an
      // editor row that resolves to nothing there is the bug that shipped as
      // "Create Note renders one pixel". Its insert carries a SECOND, narrower
      // guard -- #ifdef CROSSPOINT_HAS_PRAGMATAPRO -- because the face is
      // commercial and its generated headers are gitignored; that guard is
      // about whether the glyphs exist in the tree, not about OMIT_FONTS, so it
      // does not belong in this set's reasoning.
      "PRAGMATAPRO_12_FONT_ID",
  };
  EXPECT_EQ(fontsSurvivingOmitFonts(kMainCpp), want);
}

TEST(OmitFonts, TheGuardStillStripsSomething) {
  // Precondition: if OMIT_FONTS ever stops guarding anything, these tests keep
  // passing while proving nothing. The stripped set must be non-empty.
  std::ifstream in(kMainCpp);
  ASSERT_TRUE(in.good());
  std::string line;
  int depth = 0, guarded = 0;
  while (std::getline(in, line)) {
    const std::string s = trimmed(line);
    if (s.rfind("#if", 0) == 0 && s.find("OMIT_FONTS") != std::string::npos) {
      depth++;
      continue;
    }
    if (s.rfind("#endif", 0) == 0 && s.find("OMIT_FONTS") != std::string::npos) {
      depth = depth > 0 ? depth - 1 : 0;
      continue;
    }
    if (depth > 0 && s.find("renderer.insertFont(") != std::string::npos) guarded++;
  }
  EXPECT_GT(guarded, 0) << "OMIT_FONTS guards no font registrations -- either the guard is dead "
                           "or the parser stopped matching, and either way these tests are theatre";
}
