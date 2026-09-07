// Does a contents entry actually reach its chapter?
//
// A TOC entry is joined to the spine by an exact string comparison of hrefs
// (BookMetadataCache::createTocEntry). Miss it and the entry carries
// spineIndex -1; Chapter Select then CANCELS the pick
// (EpubReaderChapterSelectionActivity.cpp), the reader's handler does nothing,
// and the page it was already on is repainted. From the outside that is
// indistinguishable from "every chapter goes to the same spot" -- the exact
// report this suite was written for, on a book whose whole table of contents
// missed at once.
//
// The two fixtures are ordinary layouts, not torture cases (see
// scripts/generate_toc_href_epubs.py):
//
//   test_toc_ncx_subdir  an EPUB 2 whose toc.ncx sits a folder below
//                        content.opf, so its own hrefs climb back out with
//                        "../". Resolved against the OPF they land a folder
//                        short and NOTHING in the book resolves.
//   test_toc_dot_paths   an EPUB 3 nav that spells the same paths "./Text/...".
//
// Both are all-or-nothing, which is why the failure reads as "the app ignores
// Chapter Select" rather than as one broken chapter.
//
// The path unit cases at the bottom pin the underlying rule: normalisePath
// collapsed ".." and left "." alone, so two spellings of one path compared
// unequal.

#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "Epub.h"
#include "Epub/converters/ImageDecoderFactory.h"
#include "FsHelpers.h"

HalDisplay display;
// Cover decoding is not under test and the real decoders reach JPEGDEC/PNGdec.
ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string&) { return nullptr; }
bool ImageDecoderFactory::isFormatSupported(const std::string&) { return false; }

namespace {

std::string fixture(const char* name) { return std::string(CROSSPOINT_TEST_EPUB_DIR) + "/" + name; }

class Cache {
 public:
  explicit Cache(const char* name) : path_(std::string("/tmp/cp_toc_href_") + name) {
    (void)system(("rm -rf '" + path_ + "'").c_str());
    (void)system(("mkdir -p '" + path_ + "'").c_str());
  }
  ~Cache() { (void)system(("rm -rf '" + path_ + "'").c_str()); }
  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

// Every entry resolves, each to its own spine item, in reading order, keeping
// its fragment. Order matters as much as resolution: an entry that resolves to
// the WRONG chapter is the same silent wrongness one layer along.
void expectEveryEntryReachesItsOwnChapter(const char* fixtureName, const char* cacheName) {
  Cache cache(cacheName);
  auto epub = std::make_shared<Epub>(fixture(fixtureName), cache.path());
  ASSERT_TRUE(epub->load()) << "could not load " << fixtureName;

  const int count = epub->getTocItemsCount();
  ASSERT_EQ(count, 4) << "the fixture has four chapters";

  for (int i = 0; i < count; i++) {
    const auto item = epub->getTocItem(i);
    EXPECT_NE(item.spineIndex, -1) << "entry " << i << " (\"" << item.title << "\", href \"" << item.href
                                   << "\") reaches no spine item, so picking it does nothing at all";
    EXPECT_EQ(item.spineIndex, i) << "entry " << i << " (\"" << item.title << "\") opens spine " << item.spineIndex;
    EXPECT_FALSE(item.anchor.empty()) << "entry " << i << " lost its fragment";
  }
}

TEST(TocHrefResolution, NcxBelowThePackageDocumentStillReachesEveryChapter) {
  expectEveryEntryReachesItsOwnChapter("test_toc_ncx_subdir.epub", "ncx_subdir");
}

TEST(TocHrefResolution, DotSlashSpellingStillReachesEveryChapter) {
  expectEveryEntryReachesItsOwnChapter("test_toc_dot_paths.epub", "dot_paths");
}

// The rule underneath both: two spellings of one path must normalise to one
// string, because that string is compared with ==.
TEST(NormalisePath, DropsCurrentDirectorySegments) {
  EXPECT_EQ(FsHelpers::normalisePath("./Text/ch1.xhtml"), "Text/ch1.xhtml");
  EXPECT_EQ(FsHelpers::normalisePath("OEBPS/./Text/ch1.xhtml"), "OEBPS/Text/ch1.xhtml");
  EXPECT_EQ(FsHelpers::normalisePath("OEBPS/Text/./ch1.xhtml"), "OEBPS/Text/ch1.xhtml");
  // A path that is nothing but "." names the directory itself, not a file.
  EXPECT_EQ(FsHelpers::normalisePath("."), "");
  EXPECT_EQ(FsHelpers::normalisePath("./"), "");
}

TEST(NormalisePath, StillCollapsesParentSegments) {
  EXPECT_EQ(FsHelpers::normalisePath("OEBPS/../Text/ch1.xhtml"), "Text/ch1.xhtml");
  EXPECT_EQ(FsHelpers::normalisePath("OEBPS/nav/../Text/ch1.xhtml"), "OEBPS/Text/ch1.xhtml");
  // "." must not eat the segment that a following ".." is meant to remove.
  EXPECT_EQ(FsHelpers::normalisePath("OEBPS/./nav/../Text/ch1.xhtml"), "OEBPS/Text/ch1.xhtml");
  EXPECT_EQ(FsHelpers::normalisePath("Text/ch1.xhtml"), "Text/ch1.xhtml");
}

}  // namespace
