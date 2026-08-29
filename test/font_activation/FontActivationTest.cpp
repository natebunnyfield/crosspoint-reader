// The font activation model: which installed families the reader currently
// wants, and every refusal that protects the reader from being left with none.
//
// WHY THIS IS A TEST AND NOT A COMMENT
//
// Every failure mode here is SILENT. A substring match makes deactivating
// "Edgar" also hide a hypothetical "EdgarPro" and nothing reports it. A
// removeToken that leaves a stray separator produces an empty token that
// matches an empty family name. A last-active rule counted over the wrong set
// lets the final reading family be switched off whenever a writing-only face
// happens to be installed -- and the symptom appears later, in the reader, as
// a font cycle that does nothing.
//
// The model is pure precisely so these can be swept exhaustively here rather
// than judged by eye on a device.

#include <gtest/gtest.h>

#include <string.h>

#include "FontActivation.h"

using fontactivation::Result;


const char* kFamilies[] = {"Edgar", "Coelacanth", "InknutJunicode", "LibreFranklin"};
constexpr size_t kFamilyCount = sizeof(kFamilies) / sizeof(kFamilies[0]);

void expectSpec(const char* got, const char* want, const char* what) {
  EXPECT_STREQ(got, want) << what;
}

// A whole-token match, never a substring. This is the check that stops
// deactivating one family from hiding another whose name contains it.
TEST(FontActivation, WholeTokenMatching) {
  EXPECT_TRUE(fontactivation::isDeactivated("Edgar", "Edgar"));
  EXPECT_TRUE(!fontactivation::isDeactivated("EdgarPro", "Edgar"));
  EXPECT_TRUE(!fontactivation::isDeactivated("Edgar", "EdgarPro"));
  EXPECT_TRUE(fontactivation::isDeactivated("Coelacanth,Edgar", "Edgar"));
  EXPECT_TRUE(fontactivation::isDeactivated("Edgar,Coelacanth", "Edgar"));
  EXPECT_TRUE(fontactivation::isDeactivated("A,Edgar,B", "Edgar"));
  EXPECT_TRUE(!fontactivation::isDeactivated("A,EdgarPro,B", "Edgar"));
  // An empty spec deactivates nothing, which is the shipped default.
  EXPECT_TRUE(!fontactivation::isDeactivated("", "Edgar"));
  EXPECT_TRUE(fontactivation::isActive("", "Edgar"));
  // A null spec is the pre-load state and must not crash or hide anything.
  EXPECT_TRUE(!fontactivation::isDeactivated(nullptr, "Edgar"));
  // An empty family name matches nothing, so a stray separator cannot hide a
  // font by accident.
  EXPECT_TRUE(!fontactivation::isDeactivated("Edgar,,Coelacanth", ""));
}

// Removal must leave a well-formed list from every position, because a stray
// separator creates an empty token that later reads as a family.
TEST(FontActivation, RemovalLeavesNoStraySeparator) {
  char spec[fontactivation::SPEC_BUF_SIZE];

  strcpy(spec, "A,B,C");
  fontactivation::removeToken(spec, "A");
  expectSpec(spec, "B,C", "remove first");

  strcpy(spec, "A,B,C");
  fontactivation::removeToken(spec, "B");
  expectSpec(spec, "A,C", "remove middle");

  strcpy(spec, "A,B,C");
  fontactivation::removeToken(spec, "C");
  expectSpec(spec, "A,B", "remove last");

  strcpy(spec, "A");
  fontactivation::removeToken(spec, "A");
  expectSpec(spec, "", "remove only");

  strcpy(spec, "A,B");
  fontactivation::removeToken(spec, "Z");
  expectSpec(spec, "A,B", "remove absent is a no-op");

}

// A full round trip returns the spec to byte-identical, which is what makes
// deactivation reversible rather than merely undoable-looking.
TEST(FontActivation, ToggleRoundTrip) {
  char spec[fontactivation::SPEC_BUF_SIZE] = "";
  EXPECT_TRUE(fontactivation::toggle(spec, sizeof(spec), "Edgar", kFamilies, kFamilyCount) ==
         Result::Deactivated);
  expectSpec(spec, "Edgar", "after deactivate");
  EXPECT_TRUE(fontactivation::isDeactivated(spec, "Edgar"));

  EXPECT_TRUE(fontactivation::toggle(spec, sizeof(spec), "Coelacanth", kFamilies, kFamilyCount) ==
         Result::Deactivated);
  expectSpec(spec, "Edgar,Coelacanth", "after second deactivate");

  EXPECT_TRUE(fontactivation::toggle(spec, sizeof(spec), "Edgar", kFamilies, kFamilyCount) ==
         Result::Reactivated);
  expectSpec(spec, "Coelacanth", "after reactivate from the front");

  EXPECT_TRUE(fontactivation::toggle(spec, sizeof(spec), "Coelacanth", kFamilies, kFamilyCount) ==
         Result::Reactivated);
  expectSpec(spec, "", "round trip returns to empty");
}

// THE RULE THE READER DEPENDS ON. Deactivating down to one is allowed;
// deactivating the last one is refused, and refused WITHOUT modifying the spec.
TEST(FontActivation, LastActiveIsRefused) {
  char spec[fontactivation::SPEC_BUF_SIZE] = "";
  EXPECT_TRUE(fontactivation::toggle(spec, sizeof(spec), "Edgar", kFamilies, kFamilyCount) ==
         Result::Deactivated);
  EXPECT_TRUE(fontactivation::toggle(spec, sizeof(spec), "Coelacanth", kFamilies, kFamilyCount) ==
         Result::Deactivated);
  EXPECT_TRUE(fontactivation::toggle(spec, sizeof(spec), "InknutJunicode", kFamilies, kFamilyCount) ==
         Result::Deactivated);
  EXPECT_TRUE(fontactivation::activeCount(spec, kFamilies, kFamilyCount) == 1);

  char before[fontactivation::SPEC_BUF_SIZE];
  strcpy(before, spec);
  EXPECT_TRUE(fontactivation::toggle(spec, sizeof(spec), "LibreFranklin", kFamilies, kFamilyCount) ==
         Result::RefusedLast);
  expectSpec(spec, before, "a refused toggle must not modify the spec");

  // Reactivating anything is still allowed at the floor -- the rule bounds
  // deactivation only, or the state would be a trap.
  EXPECT_TRUE(fontactivation::toggle(spec, sizeof(spec), "Edgar", kFamilies, kFamilyCount) ==
         Result::Reactivated);
}

// The count is taken over the families the PICKER offers. A count over the raw
// registry would include writing-only faces and let the last READING family be
// switched off.
TEST(FontActivation, CountIsOverTheOfferedSet) {
  char spec[fontactivation::SPEC_BUF_SIZE] = "";
  const char* onlyTwo[] = {"Edgar", "Coelacanth"};
  EXPECT_TRUE(fontactivation::toggle(spec, sizeof(spec), "Edgar", onlyTwo, 2) == Result::Deactivated);
  // One left in the offered set: refused, even though the wider list has four.
  EXPECT_TRUE(fontactivation::toggle(spec, sizeof(spec), "Coelacanth", onlyTwo, 2) == Result::RefusedLast);
  // Against the wider list the same toggle is fine, which is exactly the
  // difference the caller must get right.
  EXPECT_TRUE(fontactivation::toggle(spec, sizeof(spec), "Coelacanth", kFamilies, kFamilyCount) ==
         Result::Deactivated);
}

// A name carrying the separator cannot be stored, and is refused rather than
// corrupting every other entry in the list.
TEST(FontActivation, UnstorableNameIsRefused) {
  char spec[fontactivation::SPEC_BUF_SIZE] = "Edgar";
  const char* families[] = {"Edgar", "Bad,Name", "Coelacanth"};
  EXPECT_TRUE(fontactivation::toggle(spec, sizeof(spec), "Bad,Name", families, 3) == Result::RefusedName);
  expectSpec(spec, "Edgar", "a refused name must not modify the spec");
  EXPECT_TRUE(fontactivation::toggle(spec, sizeof(spec), "", families, 3) == Result::RefusedName);
}

// The cap is enforced, not silently truncated. A truncated list would silently
// reactivate a font -- the failure would look like the toggle not sticking.
TEST(FontActivation, CapIsEnforcedNotTruncated) {
  // A tiny cap makes the boundary exact and testable.
  char spec[8] = "";
  const char* families[] = {"AAA", "BBB", "CCC"};
  EXPECT_TRUE(fontactivation::toggle(spec, sizeof(spec), "AAA", families, 3) == Result::Deactivated);
  expectSpec(spec, "AAA", "first fits");
  // "AAA,BBB" is 7 chars + NUL = 8, exactly the cap.
  EXPECT_TRUE(fontactivation::toggle(spec, sizeof(spec), "BBB", families, 3) == Result::Deactivated);
  expectSpec(spec, "AAA,BBB", "second fits exactly");
  // A third cannot fit and must be refused with the spec intact -- but the
  // last-active rule bites first here, which is itself correct, so use a
  // wider family list to reach the NoRoom branch.
  const char* wider[] = {"AAA", "BBB", "CCC", "DDD"};
  EXPECT_TRUE(fontactivation::toggle(spec, sizeof(spec), "CCC", wider, 4) == Result::NoRoom);
  expectSpec(spec, "AAA,BBB", "a refused toggle must not truncate the spec");
}


