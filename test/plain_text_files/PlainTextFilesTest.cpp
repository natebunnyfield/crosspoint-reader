// Which files the plain-text viewer opens, and therefore which render in the
// editor font (owner ruling 2026-08-11: "for txt, json and other unstyled
// files, use the selected editor font").
//
// The font half of that ruling was a one-line change; this half was the real
// gap. Before it, ONLY .txt and .md opened at all -- so asking for json to use
// the editor font would have changed nothing visible, because a .json could not
// be viewed in the first place. That is the kind of half-delivery this pins.

#include <gtest/gtest.h>

#include <FsHelpers.h>

TEST(PlainTextFiles, TheNamedUnstyledFormatsAllOpen) {
  for (const char* name : {"notes.txt", "config.json", "run.log", "rows.csv", "feed.xml", "conf.yaml", "conf.yml",
                           "settings.ini", "app.cfg", "nginx.conf"}) {
    EXPECT_TRUE(FsHelpers::hasPlainTextExtension(name)) << name << " should open in the plain-text viewer";
  }
}

TEST(PlainTextFiles, MatchingIsCaseInsensitive) {
  EXPECT_TRUE(FsHelpers::hasPlainTextExtension("CONFIG.JSON"));
  EXPECT_TRUE(FsHelpers::hasPlainTextExtension("Notes.Txt"));
}

// A NAMED list, deliberately. Falling back to "text" for anything not known to
// be binary would open a zip or a font as a screenful of mojibake; the list can
// only be wrong by omission, which shows up as a file that will not open rather
// than one that opens as garbage.
TEST(PlainTextFiles, BinaryAndStyledFormatsAreNotPlainText) {
  for (const char* name : {"book.epub", "cover.bmp", "font.cpfont", "archive.zip", "firmware.bin", "photo.jpg"}) {
    EXPECT_FALSE(FsHelpers::hasPlainTextExtension(name)) << name << " must not open as plain text";
  }
}

// .md is handled by hasMarkdownExtension and routed alongside these, but it is
// NOT in the plain-text set: it has styling of its own and only renders as
// plain text until there is a markdown reader. Keeping it separate is what lets
// that change later without touching this list.
TEST(PlainTextFiles, MarkdownIsRoutedSeparately) {
  EXPECT_FALSE(FsHelpers::hasPlainTextExtension("notes.md"));
  EXPECT_TRUE(FsHelpers::hasMarkdownExtension("notes.md"));
}

// A file with no extension, and one whose name merely contains a listed
// extension mid-string, must not match.
TEST(PlainTextFiles, PartialAndMissingExtensionsDoNotMatch) {
  EXPECT_FALSE(FsHelpers::hasPlainTextExtension("README"));
  EXPECT_FALSE(FsHelpers::hasPlainTextExtension("notes.txt.epub"));
  EXPECT_FALSE(FsHelpers::hasPlainTextExtension(""));
}
