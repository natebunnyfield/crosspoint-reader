#include <gtest/gtest.h>

#include <string>

#include "FsHelpers.h"

namespace {

using namespace std::string_view_literals;

TEST(IsSafePathComponent, AcceptsNamesWithRepeatedDots) {
  EXPECT_TRUE(FsHelpers::isSafePathComponent("volume..2.epub"sv));
  EXPECT_TRUE(FsHelpers::isSafePathComponent("notes...txt"sv));
  EXPECT_TRUE(FsHelpers::isSafePathComponent(".hidden"sv));
  EXPECT_TRUE(FsHelpers::isSafePathComponent("a.b"sv));
  EXPECT_TRUE(FsHelpers::isSafePathComponent("book.epub"sv));
}

TEST(IsSafePathComponent, RejectsEmptyAndExactDotComponents) {
  EXPECT_FALSE(FsHelpers::isSafePathComponent(""sv));
  EXPECT_FALSE(FsHelpers::isSafePathComponent("."sv));
  EXPECT_FALSE(FsHelpers::isSafePathComponent(".."sv));
}

TEST(IsSafePathComponent, RejectsPathSeparatorsAnywhereInTheComponent) {
  EXPECT_FALSE(FsHelpers::isSafePathComponent("a/b"sv));
  EXPECT_FALSE(FsHelpers::isSafePathComponent("a\\b"sv));
  EXPECT_FALSE(FsHelpers::isSafePathComponent("../x"sv));
  EXPECT_FALSE(FsHelpers::isSafePathComponent("x/.."sv));
}

// checkFatName explains why the card refused a name (review 2026-11-27, R-11
// and R-12). Its rules are SdFat's parsePathName: the reserved set, UTF-16
// units, leading spaces skipped, trailing dots and spaces dropped, 255 at most.

using FsHelpers::FatNameProblem;

// One spelling for every argument type: a std::string converts to both of
// checkFatName's overloads under the host String stub.
FsHelpers::FatNameCheck check(std::string_view name) { return FsHelpers::checkFatName(name); }
std::string describe(std::string_view name) { return FsHelpers::describeFatNameProblem(check(name)); }

TEST(CheckFatName, AcceptsTheNamesBooksReallyHave) {
  for (const auto name : {"book.epub"sv, "C++ Primer.epub"sv, "Caf\xC3\xA9 Book.epub"sv, "A, B; C.epub"sv,
                          "[1984] {draft} #2 & more!.epub"sv, "it's 100% = fine~.txt"sv, ".hidden"sv}) {
    EXPECT_EQ(check(name).problem, FatNameProblem::None) << name;
  }
}

TEST(CheckFatName, NamesEachReservedCharacter) {
  for (const char c : {'"', '*', ':', '<', '>', '?', '\\', '|'}) {
    const std::string name = std::string("Dune") + c + " Messiah.epub";
    const auto result = check(name);
    EXPECT_EQ(result.problem, FatNameProblem::ReservedChar) << name;
    EXPECT_EQ(result.reserved, c) << name;
  }
}

TEST(CheckFatName, ReportsTheFirstReservedCharacter) {
  const auto result = check("Why? Not: this.epub"sv);
  EXPECT_EQ(result.problem, FatNameProblem::ReservedChar);
  EXPECT_EQ(result.reserved, '?');
}

TEST(CheckFatName, RefusesControlCharacters) {
  const auto result = check("tab\there.epub"sv);
  EXPECT_EQ(result.problem, FatNameProblem::ReservedChar);
  EXPECT_EQ(result.reserved, '\t');
}

TEST(CheckFatName, AllowsExactly255Characters) {
  const std::string fits = std::string(250, 'n') + ".epub";
  EXPECT_EQ(check(fits).problem, FatNameProblem::None);
  EXPECT_EQ(check(fits).length, 255u);

  const std::string over = std::string(251, 'n') + ".epub";
  const auto result = check(over);
  EXPECT_EQ(result.problem, FatNameProblem::TooLong);
  EXPECT_EQ(result.length, 256u);
}

TEST(CheckFatName, CountsUtf16UnitsNotBytes) {
  // 255 two-byte letters are 510 bytes and 255 units: the card takes them.
  std::string accented;
  for (int i = 0; i < 255; i++) accented += "\xC3\xA9";
  EXPECT_EQ(check(accented).problem, FatNameProblem::None);
  EXPECT_EQ(check(accented).length, 255u);

  // A code point past U+FFFF is a surrogate pair: two units each.
  std::string emoji;
  for (int i = 0; i < 128; i++) emoji += "\xF0\x9F\x93\x96";
  const auto result = check(emoji);
  EXPECT_EQ(result.problem, FatNameProblem::TooLong);
  EXPECT_EQ(result.length, 256u);
}

TEST(CheckFatName, DoesNotCountWhatTheCardDrops) {
  // Leading spaces and trailing dots and spaces are not stored.
  const std::string padded = "   " + std::string(250, 'n') + ".epub" + ". . .";
  EXPECT_EQ(check(padded).problem, FatNameProblem::None);
  EXPECT_EQ(check(padded).length, 255u);
}

TEST(CheckFatName, RefusesANameWithNothingLeft) {
  EXPECT_EQ(check(""sv).problem, FatNameProblem::Empty);
  EXPECT_EQ(check("   "sv).problem, FatNameProblem::Empty);
  EXPECT_EQ(check("..."sv).problem, FatNameProblem::Empty);
  EXPECT_EQ(check(" . ."sv).problem, FatNameProblem::Empty);
}

TEST(CheckFatName, RefusesBytesThatAreNotUtf8) {
  EXPECT_EQ(check("\xFF.epub"sv).problem, FatNameProblem::BadEncoding);
  EXPECT_EQ(check("caf\xE9.epub"sv).problem, FatNameProblem::BadEncoding);  // Latin-1
}

TEST(DescribeFatNameProblem, SaysWhichCharacterAndWhichAreRefused) {
  const std::string text = describe("Dune: Messiah.epub"sv);
  EXPECT_EQ(text, "File name has a character the card cannot store: ':' (not allowed: \" * : < > ? \\ |)");
}

TEST(DescribeFatNameProblem, SaysHowLongAndTheLimit) {
  const std::string text = describe(std::string(300, 'x'));
  EXPECT_EQ(text, "File name is too long: 300 characters, the card allows 255");
}

TEST(DescribeFatNameProblem, IsEmptyForANameTheCardTakes) { EXPECT_TRUE(describe("book.epub"sv).empty()); }

TEST(DescribeFatNameProblem, NamesAControlCharacterByCode) {
  const std::string text = describe("a\tb"sv);
  EXPECT_EQ(text, "File name has a control character (code 9), which the card cannot store");
}

TEST(NormalisePath, CollapsesParentReferenceWithinPath) {
  EXPECT_EQ(FsHelpers::normalisePath("/Books/../.crosspoint/x"), ".crosspoint/x");
}

TEST(NormalisePath, DropsLeadingParentReferencesPastRoot) { EXPECT_EQ(FsHelpers::normalisePath("/../../etc"), "etc"); }

}  // namespace
