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

// A book with thousands of chapters: past LARGE_SPINE_THRESHOLD the index build
// looks zip sizes up in chunks of SIZE_LOOKUP_CHUNK (upstream #3733 -- one
// 16 B target per spine item for the whole spine used to abort on device when
// the deque could not grow). The abort itself needs the device's heap; this pins
// that chunking still gives every spine item its own size, across the chunk
// boundary, in order. The fixture is generated here because 2,500 chapters is
// cheap to write and needless to commit.
TEST(LargeSpine, EveryChapterKeepsItsSizeAcrossLookupChunks) {
  constexpr int kChapters = 2500;  // > SIZE_LOOKUP_CHUNK (2,048): two chunks
  const std::string epubPath = "/tmp/cp_large_spine.epub";
  const std::string script =
      "import zipfile,sys\n"
      "n=int(sys.argv[2])\n"
      "z=zipfile.ZipFile(sys.argv[1],'w',zipfile.ZIP_DEFLATED)\n"
      "z.writestr(zipfile.ZipInfo('mimetype'),'application/epub+zip')\n"
      "z.writestr('META-INF/container.xml','<?xml version=\"1.0\"?><container version=\"1.0\" "
      "xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\"><rootfiles><rootfile full-path=\"OEBPS/content.opf\" "
      "media-type=\"application/oebps-package+xml\"/></rootfiles></container>')\n"
      "man=''.join('<item id=\"c%d\" href=\"c%d.xhtml\" media-type=\"application/xhtml+xml\"/>'%(i,i) for i in "
      "range(n))\n"
      "spine=''.join('<itemref idref=\"c%d\"/>'%i for i in range(n))\n"
      "z.writestr('OEBPS/content.opf','<?xml version=\"1.0\"?><package xmlns=\"http://www.idpf.org/2007/opf\" "
      "version=\"2.0\" unique-identifier=\"id\"><metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
      "<dc:title>Large</dc:title><dc:identifier id=\"id\">x</dc:identifier><dc:language>en</dc:language></metadata>"
      "<manifest><item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>'+man+'</manifest>"
      "<spine toc=\"ncx\">'+spine+'</spine></package>')\n"
      "z.writestr('OEBPS/toc.ncx','<?xml version=\"1.0\"?><ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" "
      "version=\"2005-1\"><navMap><navPoint id=\"n0\"><navLabel><text>One</text></navLabel><content src=\"c0.xhtml\"/>"
      "</navPoint></navMap></ncx>')\n"
      "for i in range(n):\n"
      "  z.writestr('OEBPS/c%d.xhtml'%i,'<html "
      "xmlns=\"http://www.w3.org/1999/xhtml\"><body><p>'+'x'*(i%97+1)+'</p></body></html>')\n"
      "z.close()\n";
  FILE* f = fopen("/tmp/cp_large_spine.py", "w");
  ASSERT_NE(f, nullptr);
  fputs(script.c_str(), f);
  fclose(f);
  ASSERT_EQ(system(("python3 /tmp/cp_large_spine.py " + epubPath + " " + std::to_string(kChapters)).c_str()), 0);

  Cache cache("large_spine");
  auto epub = std::make_shared<Epub>(epubPath, cache.path());
  ASSERT_TRUE(epub->load()) << "the index build failed";
  ASSERT_EQ(epub->getSpineItemsCount(), kChapters);

  // Each chapter's inflated size is a fixed shell plus (i % 97 + 1) letters, so
  // consecutive differences pin every item's own size, not just the total.
  const size_t shell = epub->getCumulativeSpineItemSize(0) - 1;
  size_t prev = 0;
  for (int i = 0; i < kChapters; i++) {
    const size_t cum = epub->getCumulativeSpineItemSize(i);
    ASSERT_EQ(cum - prev, shell + static_cast<size_t>(i % 97 + 1)) << "spine item " << i << " got the wrong size";
    prev = cum;
  }
  remove(epubPath.c_str());
}
