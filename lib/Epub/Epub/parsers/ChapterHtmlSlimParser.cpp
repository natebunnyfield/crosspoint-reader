#include "ChapterHtmlSlimParser.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>
#include <XmlParserUtils.h>
#include <expat.h>

#include <algorithm>
#include <iterator>
#include <new>

#include "../../../../src/fontIds.h"
#include "Epub.h"
#include "Epub/BookNotes.h"
#include "Epub/Page.h"
#include "XmlEncodingSupport.h"
#include <MissingGlyphLedger.h>
#include "Epub/converters/ImageDecoderFactory.h"
#include "Epub/converters/ImageDimsProbe.h"
#include "Epub/converters/ImageToFramebufferDecoder.h"
#include "Epub/htmlEntities.h"

// Minimum file size (in bytes) to show indexing popup - smaller chapters don't benefit from it
constexpr size_t MIN_SIZE_FOR_POPUP = 10 * 1024;  // 10KB
constexpr size_t PARSE_BUFFER_SIZE = 1024;

// This number comes from PR #73
// If we have > 750 words buffered up, perform the layout and consume out all but the last line
// There should be enough here to build out 1-2 full pages and doing this will free up a lot of
// memory.
// Spotted when reading Intermezzo, there are some really long text blocks in there.
constexpr size_t TEXT_BLOCK_SOFT_FLUSH_WORDS = 750;

// When CSS is enabled, flush earlier to save RAM. 320 is still more than enough to build a CJK
// page at font size 14
constexpr size_t TEXT_BLOCK_SOFT_FLUSH_WORDS_WITH_CSS = 320;

// Hard cap on the number of anchor IDs recorded per chapter. Legitimate navigation
// anchors (TOC entries, footnotes, cross-references) rarely exceed a few hundred per
// chapter. A runaway count usually means a converter injected machine-generated IDs on
// every text fragment (e.g. Kobo KePub spans). The cap prevents unbounded heap growth
// on resource-constrained devices (~380KB heap). TOC anchors bypass this cap.
constexpr size_t MAX_ANCHORS_PER_CHAPTER = 1024;

constexpr const char* HEADER_TAGS[] = {"h1", "h2", "h3", "h4", "h5", "h6"};
constexpr const char* BLOCK_TAGS[] = {"p", "li", "div", "br", "blockquote", "ul", "ol"};

// Default indent per list nesting level, applied to a <ul>/<ol> that carries no
// publisher left margin/padding of its own (there is no UA stylesheet). 1.5 em
// is sized against this panel's short measure (~27 chars/line at 18 pt): it
// swallows a three-digit "999. " marker gutter with room to spare, while three
// full levels still leave ~18 chars of measure — above the ~15-16-char floor
// the 2/5-viewport accumulated-inset clamp guarantees (block-rendering audit
// 2026-08-22, F2). Deeper nests hit that same clamp and compress
// proportionally instead of crushing the text.
constexpr float LIST_INDENT_STEP_EM = 1.5f;
constexpr const char* BOLD_TAGS[] = {"b", "strong"};
constexpr const char* ITALIC_TAGS[] = {"i", "em"};
constexpr const char* UNDERLINE_TAGS[] = {"u", "ins"};
constexpr const char* LINETHROUGH_TAGS[] = {"del", "s", "strike"};
constexpr const char* IMAGE_TAGS[] = {"img", "image"};
// Subtrees whose character data is NOT prose. It was `{"head", "rp"}` alone
// until 2026-08-23 (sweep item #69), so a <script> or a <style> anywhere in the
// body emitted its source into the page as text.
//
// What is deliberately NOT here, and why -- because the obvious additions are
// the ones that would cost a reader something:
//   svg    an SVG-wrapped cover is <svg><image xlink:href="..."/></svg>, and
//          `image` is in IMAGE_TAGS. Skipping the subtree would drop the cover
//          of a large share of books. Its <title>/<desc> are handled below
//          instead, which is where the actual leak was.
//   math   <mi>/<mn>/<mo> ARE the equation. Leaking "x2+1" is poor; dropping
//          the equation entirely is worse. Only <annotation>, which repeats the
//          same expression in TeX or content MathML, is skipped -- that one is
//          pure duplication.
//   object,
//   video,
//   audio  their child content is the FALLBACK, shown precisely when the object
//          cannot be rendered, which here is always.
constexpr const char* SKIP_TAGS[] = {"head",  "rp",     "script",     "style",         "noscript",
                                     "title", "desc",   "annotation", "annotation-xml", "template",
                                     "iframe"};

bool isWhitespace(const char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }

// Over-indent guard (block-rendering audit 2026-08-22,
// docs/block-rendering-audit-2026-08-22.md). BlockStyle::fromCssStyle caps each
// SIDE of each ELEMENT at 2em, but nothing capped the ACCUMULATED total: three
// nested blockquotes at margin+padding 2em each stack ~12em per side, which
// exceeds the viewport — makePages' effectiveWidth guard then falls back to
// FULL width while placeLineOnPage still shifts every line by the full
// leftInset, so the text is laid out wide and painted off the right edge of
// the panel. Cap the accumulated total at 2/5 of the viewport (both sides
// scaled proportionally) so a block always keeps at least 3/5 of the measure
// (~16 of the ~27 chars per line at 18 pt on X3). Applied only where the CSS
// block-style stack accumulates — table cell placement builds its column
// x-offsets as margins and must not be rescaled.
BlockStyle clampAccumulatedHorizontalInsets(BlockStyle style, const uint16_t viewportWidth) {
  const int maxInset = (static_cast<int>(viewportWidth) * 2) / 5;
  const int total = style.totalHorizontalInset();
  if (total <= maxInset) return style;
  const float scale = static_cast<float>(maxInset) / static_cast<float>(total);
  style.marginLeft = static_cast<int16_t>(style.marginLeft * scale);
  style.marginRight = static_cast<int16_t>(style.marginRight * scale);
  style.paddingLeft = static_cast<int16_t>(style.paddingLeft * scale);
  style.paddingRight = static_cast<int16_t>(style.paddingRight * scale);
  return style;
}

std::string trimAndNormalize(const std::string& str) {
  if (str.empty()) return "";
  size_t start = 0;
  while (start < str.size() && isWhitespace(str[start])) {
    start++;
  }
  if (start == str.size()) return "";
  size_t end = str.size() - 1;
  while (end > start && isWhitespace(str[end])) {
    end--;
  }
  std::string result;
  result.reserve(end - start + 1);
  bool inSpace = false;
  for (size_t i = start; i <= end; i++) {
    if (isWhitespace(str[i])) {
      if (!inSpace) {
        result.push_back(' ');
        inSpace = true;
      }
    } else {
      result.push_back(str[i]);
      inSpace = false;
    }
  }
  return result;
}

bool matches(const char* tag_name, const char* const* possible_tags, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(tag_name, possible_tags[i]) == 0) {
      return true;
    }
  }
  return false;
}

const char* getAttribute(const XML_Char** atts, const char* attrName) {
  if (!atts) return nullptr;
  for (int i = 0; atts[i]; i += 2) {
    if (strcmp(atts[i], attrName) == 0) return atts[i + 1];
  }
  return nullptr;
}

// Returns true if the HTML element is a purely inline, non-navigable wrapper.
// IDs on these elements are never meaningful navigation targets in epub content.
// Reading-system converters (Kobo KePub, Calibre, etc.) frequently inject thousands
// of such IDs for progress tracking or internal bookkeeping, and recording each one
// as a navigation anchor exhausts the heap on memory-constrained devices.
// Block-level, sectioning, and structural elements are always considered navigable.
bool isNonNavigableInlineElement(const char* name) { return strcmp(name, "span") == 0; }

bool isInternalEpubLink(const char* href) {
  if (!href || href[0] == '\0') return false;
  if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0) return false;
  if (strncmp(href, "mailto:", 7) == 0) return false;
  if (strncmp(href, "ftp://", 6) == 0) return false;
  if (strncmp(href, "tel:", 4) == 0) return false;
  if (strncmp(href, "javascript:", 11) == 0) return false;
  return true;
}

bool isHeaderOrBlock(const char* name) {
  return matches(name, HEADER_TAGS, std::size(HEADER_TAGS)) || matches(name, BLOCK_TAGS, std::size(BLOCK_TAGS));
}

bool isTableStructuralTag(const char* name) {
  return strcmp(name, "table") == 0 || strcmp(name, "tr") == 0 || strcmp(name, "td") == 0 || strcmp(name, "th") == 0;
}

void ChapterHtmlSlimParser::applyDirectionToEntry(StyleStackEntry& entry, const CssStyle& css) {
  if (css.hasDirection()) {
    entry.hasDirection = true;
    entry.direction = css.direction;
  }
}

EpdFontFamily::Style ChapterHtmlSlimParser::fontStyleForTextDecoration(const CssTextDecoration decoration) {
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  if ((decoration & CssTextDecoration::Underline) != CssTextDecoration::None) {
    style = static_cast<EpdFontFamily::Style>(style | EpdFontFamily::UNDERLINE);
  }
  if ((decoration & CssTextDecoration::LineThrough) != CssTextDecoration::None) {
    style = static_cast<EpdFontFamily::Style>(style | EpdFontFamily::STRIKETHROUGH);
  }
  return style;
}

void ChapterHtmlSlimParser::applyTextDecorationToEntry(StyleStackEntry& entry, const CssStyle& css) {
  if (css.hasTextDecoration()) {
    entry.hasTextDecoration = true;
    entry.textDecoration = css.textDecoration;
  }
}

void ChapterHtmlSlimParser::pushDecorationStyleEntry(const CssTextDecoration defaultDecoration,
                                                     const CssStyle& cssStyle) {
  StyleStackEntry entry;
  entry.depth = depth;
  entry.hasTextDecoration = true;
  entry.textDecoration = cssStyle.hasTextDecoration() ? cssStyle.textDecoration : defaultDecoration;
  if (cssStyle.hasFontWeight()) {
    entry.hasBold = true;
    entry.bold = cssStyle.fontWeight == CssFontWeight::Bold;
  }
  if (cssStyle.hasFontStyle()) {
    entry.hasItalic = true;
    entry.italic = cssStyle.fontStyle == CssFontStyle::Italic;
  }
  applyDirectionToEntry(entry, cssStyle);
  inlineStyleStack.push_back(entry);
  updateEffectiveInlineStyle();
}

// Update effective bold/italic/decorations based on block style and inline style stack
void ChapterHtmlSlimParser::updateEffectiveInlineStyle() {
  // Start with block-level styles
  effectiveBold = currentCssStyle.hasFontWeight() && currentCssStyle.fontWeight == CssFontWeight::Bold;
  effectiveItalic = currentCssStyle.hasFontStyle() && currentCssStyle.fontStyle == CssFontStyle::Italic;
  effectiveTextDecoration =
      currentCssStyle.hasTextDecoration() ? currentCssStyle.textDecoration : CssTextDecoration::None;
  effectiveDirectionDefined = currentCssStyle.hasDirection();
  effectiveDirection = currentCssStyle.direction;
  effectiveSup = false;
  effectiveSub = false;

  // Apply inline style stack in order
  for (const auto& entry : inlineStyleStack) {
    if (entry.hasBold) {
      effectiveBold = entry.bold;
    }
    if (entry.hasItalic) {
      effectiveItalic = entry.italic;
    }
    // CSS line decorations propagate through descendants; child entries add
    // their own lines but cannot cancel an ancestor's already active line.
    if (entry.hasTextDecoration) {
      effectiveTextDecoration = effectiveTextDecoration | entry.textDecoration;
    }
    if (entry.hasDirection) {
      effectiveDirectionDefined = true;
      effectiveDirection = entry.direction;
    }
    if (entry.hasSup) {
      effectiveSup = entry.sup;
      if (entry.sup) effectiveSub = false;
    }
    if (entry.hasSub) {
      effectiveSub = entry.sub;
      if (entry.sub) effectiveSup = false;
    }
  }

  // Keep inherited direction in the active empty text block so upcoming block starts
  // can inherit from non-block ancestors such as <html dir="rtl"> / <body dir="rtl">.
  if (currentTextBlock && currentTextBlock->isEmpty()) {
    auto& style = currentTextBlock->getBlockStyle();
    if (effectiveDirectionDefined) {
      style.directionDefined = true;
      style.isRtl = (effectiveDirection == CssTextDirection::Rtl);
    } else {
      style.directionDefined = false;
      style.isRtl = false;
    }
  }
}

void ChapterHtmlSlimParser::flushPendingAnchor() {
  if (pendingAnchorId.empty()) return;

  // If the pending anchor is a TOC chapter boundary, force a page break after the previous
  // block is flushed so the chapter starts on a fresh page.
  if (std::find(tocAnchors.begin(), tocAnchors.end(), pendingAnchorId) != tocAnchors.end()) {
    if (currentPage && !currentPage->elements.empty()) {
      completePageFn(std::move(currentPage), xpathParagraphIndex, xpathListItemIndex, pageStartAnchor());
      completedPageCount++;
      currentPage = makeUniqueNoThrow<Page>();
      if (!currentPage) {
        noteAllocationFailure("Page at a TOC boundary");
        return;
      }
      currentPageNextY = 0;
    }
  }

  // Record deferred anchor after previous block is flushed (and any TOC page break)
  anchorData.push_back({std::move(pendingAnchorId), static_cast<uint16_t>(completedPageCount)});
  pendingAnchorId.clear();
}

// flush the contents of partWordBuffer to currentTextBlock
void ChapterHtmlSlimParser::flushPartWordBuffer() {
  // Determine font style from depth-based tracking and CSS effective style
  const bool isBold = boldUntilDepth < depth || effectiveBold;
  const bool isItalic = italicUntilDepth < depth || effectiveItalic;

  // Combine style flags using bitwise OR
  EpdFontFamily::Style fontStyle = EpdFontFamily::REGULAR;
  if (isBold) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::BOLD);
  }
  if (isItalic) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::ITALIC);
  }
  fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | fontStyleForTextDecoration(effectiveTextDecoration));
  if (effectiveSup) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::SUP);
  } else if (effectiveSub) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::SUB);
  }

  // A list item's first text word attaches to the marker word regardless of
  // source whitespace after <li> (which resets nextWordContinues in
  // characterData). The glued group keeps the marker→text gap fixed under
  // Justified (a continuation advance is never a stretchable gap) and keeps
  // the breaker from orphaning a marker at the end of a line.
  if (listMarkerGluePending_) {
    nextWordContinues = true;
    listMarkerGluePending_ = false;
  }

  // flush the buffer
  partWordBuffer[partWordBufferIndex] = '\0';
  currentTextBlock->addWord(partWordBuffer, fontStyle, false, nextWordContinues);
  partWordBufferIndex = 0;
  nextWordContinues = false;
  listItemBulletOnly = false;
}

// start a new text block if needed
void ChapterHtmlSlimParser::startNewTextBlock(const BlockStyle& blockStyle) {
  nextWordContinues = false;  // New block = new paragraph, no continuation
  if (currentTextBlock) {
    // already have a text block running and it is empty - just reuse it
    if (currentTextBlock->isEmpty()) {
      // The stack accumulates horizontal margins and text properties from ancestors.
      // Vertical margins are per-element and not inherited through the stack, but
      // container elements deposit their vertical margins on the empty block when they
      // open. Merge those into the new style so the first child in a container inherits
      // the container's vertical spacing.
      const auto style = currentTextBlock->getBlockStyle();
      BlockStyle incoming = blockStyle;
      if (style.fromBrElement) {
        // The empty block was created by a <br> section separator. Inject a full line of
        // blank space before the following paragraph so the scene/section break is visible.
        // This only fires when the <br> block stayed empty (i.e. no inline text was added).
        // Carried as sceneBreakLiftPx_ rather than in the block's style: the
        // style's vertical spacing is subject to the half-line inter-block gap
        // cap (owner ruling 2026-08-22, makePages), and a capped scene break
        // would be indistinguishable from an ordinary paragraph gap. The lift
        // rides on top of the capped gap, preserving the old half-line + one
        // full line rhythm exactly.
        sceneBreakLiftPx_ = static_cast<int16_t>(renderer.getLineHeight(fontId, lineCompression));
      }

      currentTextBlock->setBlockStyle(style.getCombinedBlockStyle(incoming, BlockStyle::CombineAxis::Vertical));

      flushPendingAnchor();
      return;
    }

    // <li> added a bullet as the first word, making the block non-empty. When a nested
    // block-level child (<p>, <div>, etc.) opens, reuse the block instead of flushing
    // the bullet to its own line. The bullet stays inline with the child's text.
    if (listItemBulletOnly) {
      const auto style = currentTextBlock->getBlockStyle();
      currentTextBlock->setBlockStyle(style.getCombinedBlockStyle(blockStyle, BlockStyle::CombineAxis::Vertical));
      listItemBulletOnly = false;
      flushPendingAnchor();
      return;
    }

    makePages();
  }
  // If the pending anchor is a TOC chapter boundary, force a page break after the previous
  // block is flushed so the chapter starts on a fresh page.
  flushPendingAnchor();
  // The block being retired is fully laid out (makePages() above): fold its
  // source bytes into the chapter-global anchor base BEFORE the new block
  // starts counting from zero.
  if (currentTextBlock) chapterSourceBytes_ += currentTextBlock->sourceByteCount();
  currentTextBlock =
      makeUniqueNoThrow<ParsedText>(extraParagraphSpacing, hyphenationEnabled, focusReadingEnabled, blockStyle);
  if (!currentTextBlock) {
    noteAllocationFailure("ParsedText for a new block");
    return;
  }
  wordsExtractedInBlock = 0;
  listItemBulletOnly = false;
  // A genuinely new block means any pending marker glue is stale. The early
  // returns above (empty-block merge, bullet-only reuse) deliberately keep it,
  // so <li><p>text still glues its first word to the marker.
  listMarkerGluePending_ = false;
}

// --- T-012: tables as columns ---------------------------------------------
// Text arrives in expat-sized fragments, so a run is extended rather than
// appended when the style has not changed -- otherwise a cell holds dozens of
// single-word runs and every one of them re-enters the style machinery.
void ChapterHtmlSlimParser::appendTableText(const char* text, const int len) {
  if (len <= 0 || tableBuf.empty() || tableBuf.back().empty()) return;
  tableBufBytes += static_cast<size_t>(len);
  if (tableBufBytes > tablecolumns::kMaxBufferedBytes) {
    abandonTableBuffer();
    // The text that tipped it over still has to be emitted, and the streaming
    // path is live again by now, so hand it back to the normal route.
    characterData(this, text, len);
    return;
  }
  tablecolumns::Cell& cell = tableBuf.back().back();
  const bool bold = effectiveBold;
  const bool italic = effectiveItalic;
  if (!cell.empty() && cell.back().bold == bold && cell.back().italic == italic) {
    cell.back().text.append(text, static_cast<size_t>(len));
    return;
  }
  cell.push_back(tablecolumns::Run{std::string(text, static_cast<size_t>(len)), bold, italic});
}

// Emit one styled fragment into the block being built, through the same route
// the streaming path uses -- so hyphenation, kerning, bidi and word breaking are
// identical to any other paragraph.
void ChapterHtmlSlimParser::emitStyledText(const std::string& text, const bool bold, const bool italic) {
  if (text.empty()) return;
  StyleStackEntry style;
  style.depth = depth;
  style.hasBold = true;
  style.bold = bold;
  style.hasItalic = true;
  style.italic = italic;
  inlineStyleStack.push_back(style);
  updateEffectiveInlineStyle();
  characterData(this, text.c_str(), static_cast<int>(text.size()));
  if (partWordBufferIndex > 0) {
    flushPartWordBuffer();
  }
  inlineStyleStack.pop_back();
  updateEffectiveInlineStyle();
}

// Give up on columns for this table and put the streaming path back in charge.
// Everything buffered so far is emitted flattened first, so the table reads in
// order even though it changed shape halfway through.
void ChapterHtmlSlimParser::abandonTableBuffer() {
  if (!tableBuffering) return;
  tableBuffering = false;
  tableBufferAbandoned = true;
  tableCellOpen = false;
  emitBufferedTableFlattened();
  tableBuf.clear();
  tableBufBytes = 0;
}

// --- The precondition every table emitter shares ---------------------------
//
// Text can arrive inside <table> but outside any cell: a <caption> is the
// common one, and stray text between rows is the other. `characterData` only
// diverts into the buffer while a cell is OPEN, so that text takes the ordinary
// streaming route and is sitting UNLAID in currentTextBlock when </table> is
// reached -- it has consumed no vertical space at all.
//
// That matters because the columns emitter takes currentPageNextY as the top of
// its first row and then rewinds to it for every column. The first cell's
// startNewTextBlock() flushes the pending block, so the caption gets laid out
// AT that row top, from inside the row loop -- and columns 2..n then rewind
// straight on top of it. Reported 2026-08-23 against a Catalan phrasebook:
// caption, "English" and "Say it" all on one line, "Catalan" pushed below.
// The rotated emitter has the same seam pointed the other way: it completes the
// current page and the caption would surface AFTER the table it names.
//
// So: retire it here, in ordinary flow above the table, and every emitter below
// starts from a page cursor that owes nothing.
bool ChapterHtmlSlimParser::pendingTextIsUnlaid() const {
  return partWordBufferIndex > 0 || (currentTextBlock && !currentTextBlock->isEmpty());
}

void ChapterHtmlSlimParser::retirePendingBlockBeforeTable() {
  if (partWordBufferIndex > 0) {
    flushPartWordBuffer();
  }
  if (!currentTextBlock || currentTextBlock->isEmpty()) return;
  // A <li> whose content is a table leaves the block holding just the list
  // marker. startNewTextBlock would take its bullet-only branch and MERGE that
  // marker into the table's first cell instead of laying it out -- and the
  // marker then wraps inside a column planned for the cell's own text, so the
  // bullet takes line 1, the cell takes line 2, and every other column sits on
  // line 1. Same broken shape as the caption bug, from a different cause, and
  // it predates this change: measured on the pre-fix tree, `<ul><li><table>`
  // rendered a bare bullet beside `Gloss` and `Say it` with `Term` beneath.
  // Clearing the flag gives the marker its own line above the table, which is
  // what a list item holding nothing but a table should look like anyway.
  listItemBulletOnly = false;
  // Non-empty, so this takes startNewTextBlock's makePages() path: the block is
  // laid out where it stands and a fresh one replaces it.
  //
  // The successor block is given a NEUTRAL style, not the retired block's own.
  // The argument does not touch the caption -- makePages() reads the style off
  // the block it is retiring -- it only dresses the EMPTY block left behind,
  // and the next thing to open that block is the table's first cell. An empty
  // block is REUSED rather than flushed (startNewTextBlock's first branch), so
  // whatever it carries is merged into that cell's style: a container's
  // marginTop/paddingTop would push column 0 down, and a <br>-flagged block
  // would arm a full line of sceneBreakLift for it. Either one puts column 0
  // below columns 1..n -- which is the reported bug, reintroduced from inside
  // its own fix. Measured, not reasoned: `<div>a<br/>b<table>` rendered `Term`
  // exactly one line under `Gloss` and `Say it` before this line said
  // BlockStyle().
  startNewTextBlock(BlockStyle());
}

// --- Keep the header with its rows (owner 2026-08-26) ----------------------
//
// "don't split up table header or caption from rest (when possible). intact is
// best", sent with two consecutive pages: the first ended with the caption
// "Effort-to-payoff, best first", the header row `Addition | Effort | What it
// buys`, its rule, and then half a page of nothing; the second opened on the
// first body row. The rule is `tablecolumns::breakBeforeHeaderKeep` -- what
// lives here is the MEASURING, which is the half that needs the parser's state.
//
// Both halves of the group have to be measured before EITHER is emitted, and
// that is the whole difficulty. The caption is not in the table buffer: it sits
// unlaid in the streaming block (see retirePendingBlockBeforeTable above), and
// the instant it is retired it belongs to this page and cannot travel. So its
// height has to be known while it is still only potential.

// Exactly the vertical distance retirePendingBlockBeforeTable() is about to
// spend, or 0 when there is nothing pending or what is pending is a paragraph
// rather than a caption.
int ChapterHtmlSlimParser::measureUnlaidLeadHeight() const {
  if (!currentTextBlock || currentTextBlock->isEmpty()) return 0;
  const int lineHeight = renderer.getLineHeight(fontId, lineCompression);
  if (lineHeight <= 0) return 0;

  // A word ceiling BEFORE the copy below, not after the line count. The block
  // that happens to be pending above a table is whatever the document left
  // there, and ParsedText's own header records why that matters: `words` is a
  // deque specifically because a CJK paragraph runs to thousands of tokens and
  // a contiguous allocation that size aborts the firmware on a fragmented C3
  // heap. Copying such a block to discover it is not a caption would double it
  // while the original is live. No line can hold more tokens than its measure
  // divided by a space, so kMaxLeadLines lines cannot hold more than this and
  // anything past it is not a caption.
  const int spaceWidth = std::max(1, renderer.getSpaceWidth(fontId));
  if (currentTextBlock->size() > static_cast<size_t>(kMaxLeadLines) * (viewportWidth / spaceWidth + 1)) return 0;

  const BlockStyle& blockStyle = currentTextBlock->getBlockStyle();
  const int horizontalInset = blockStyle.totalHorizontalInset();
  const uint16_t effectiveWidth =
      (horizontalInset < viewportWidth) ? static_cast<uint16_t>(viewportWidth - horizontalInset) : viewportWidth;

  // layoutAndExtractLines() ERASES the words it consumes, so the block cannot
  // be asked how tall it is without spending it. The probe therefore runs on a
  // COPY. The alternative -- a second, non-destructive path through the line
  // breaker -- is a second definition of where the lines go, and two of those
  // disagreeing is a class of bug no render shows. A caption is a handful of
  // words and this runs once per table.
  const auto snap = [&](const int y) {
    if (!lineGridEnabled || y <= 0) return y;
    const int rem = y % lineHeight;
    return rem != 0 ? y + lineHeight - rem : y;
  };

  ParsedText probe(*currentTextBlock);
  int lineCount = 0;
  int linesHeight = 0;
  probe.layoutAndExtractLines(
      renderer, fontId, effectiveWidth,
      [&](const std::shared_ptr<TextBlock>& line) {
        lineCount++;
        // placeLineOnPage snaps after EVERY line, not once at the end: a ruby
        // line advances by more than one line-height and the grid rounds that
        // up. Accumulating the raw advances would report a caption SHORTER than
        // it is, which is the one direction that can leave it alone on a page.
        if (line) linesHeight = snap(linesHeight + lineAdvanceOf(*line));
      },
      /*includeLastLine=*/true, justifyThresholdChars);
  // Nothing, or too much: a whole paragraph happens to be pending above plenty
  // of tables, and hauling one into the keep group would only ever make the
  // group too tall to keep -- which would cost the header its keep as well as
  // the paragraph its place. Above the ceiling the lead is simply not part of
  // the group; it is laid out where it stands and the header keeps with its
  // rows alone.
  if (lineCount == 0 || lineCount > kMaxLeadLines) return 0;

  // The same arithmetic makePages() performs, in the same order. Kept in step
  // with it by proximity and by the test that pins a caption's page.
  int y = currentPageNextY;
  if (currentPage && !currentPage->elements.empty()) {
    int topSpacing = blockStyle.marginTop > 0 ? blockStyle.marginTop : 0;
    if (blockStyle.paddingTop > 0) topSpacing += blockStyle.paddingTop;
    const int gapCap = lineHeight / 2;
    const int budget = gapCap > prevBlockBottomApplied_ ? gapCap - prevBlockBottomApplied_ : 0;
    if (topSpacing > budget) topSpacing = budget;
    y = snap(y + topSpacing + sceneBreakLiftPx_);
  }
  y += linesHeight;
  int bottomSpacing = blockStyle.marginBottom > 0 ? blockStyle.marginBottom : 0;
  if (blockStyle.paddingBottom > 0) bottomSpacing += blockStyle.paddingBottom;
  if (extraParagraphSpacing) bottomSpacing += lineHeight / 2;
  const int gapCap = lineHeight / 2;
  if (bottomSpacing > gapCap) bottomSpacing = gapCap;
  y = snap(y + bottomSpacing);
  return y - currentPageNextY;
}

bool ChapterHtmlSlimParser::breakBeforeStrandedTableHeader(const tablecolumns::Plan& plan, const int leadHeight) {
  if (!tableBufHasHeader || tableBuf.empty()) return false;
  // An empty page has nothing above the header to strand it against, and a
  // break here would publish a blank one.
  if (!currentPage || currentPage->elements.empty()) return false;

  const int lineHeight = renderer.getLineHeight(fontId, lineCompression);
  if (lineHeight <= 0) return false;
  const int rowGap = std::max(2, lineHeight / 4);
  constexpr int kRuleThickness = 2;

  const auto snap = [&](const int y) {
    if (!lineGridEnabled || y <= 0) return y;
    const int rem = y % lineHeight;
    return rem != 0 ? y + lineHeight - rem : y;
  };

  // A row costs more than its text. Every cell is an ordinary text block, so
  // makePages() adds the paragraph's BOTTOM SPACING under it before the row gap
  // is applied -- half a line whenever the reader has paragraph spacing on,
  // which is the shipped default. measureRowHeight() answers about the text
  // only; it is used elsewhere to ask "is this row taller than a page", where
  // that is the right question. Here it is not, and the difference is a whole
  // body row at 14 pt: modelling a row at 43 px instead of 60 said a group of
  // three fitted in 695 px of a 700 px page, and the render put the third row
  // on the next one.
  const int cellBottomSpacing = extraParagraphSpacing ? lineHeight / 2 : 0;

  // Measured from a FRESH page's origin -- which is where the group lands if
  // this returns true, and which is also grid-aligned, so the same numbers
  // over-state the current page's need by at most one grid step. Over-stating
  // is the safe direction: it costs white space, never a split group.
  int y = 0;
  // TWO snaps, because the row loop performs two: makePages() ends with one
  // after the cell's bottom spacing, and the loop adds the row gap and snaps
  // again. Collapsing them into a single snap of the sum under-measures a row
  // by a whole line-height whenever Line Grid is on -- snap(M + L + L/4) is
  // M + 2L, snap(M + 0.75L) is M + L -- which is the direction
  // breakBeforeHeaderKeep says is unsafe: the page is completed and the group
  // is split anyway. Line Grid is off by default, which is why the render did
  // not show it; the sweep in tests/table_keep_together runs an arm with it on.
  const auto consumeRow = [&](const size_t r) {
    const bool headerRow = (r == 0);
    y = snap(y + measureRowHeight(tableBuf[r], plan, headerRow) + cellBottomSpacing);
    y = snap(y + rowGap);
    if (headerRow) y = snap(y + kRuleThickness + rowGap);
  };

  consumeRow(0);
  const int headerHeight = y;

  int bodyHeights[tablecolumns::kKeepBodyRows] = {};
  int bodyCount = 0;
  for (size_t r = 1; r < tableBuf.size() && bodyCount < tablecolumns::kKeepBodyRows; r++) {
    const int before = y;
    consumeRow(r);
    bodyHeights[bodyCount++] = y - before;
  }

  if (!tablecolumns::breakBeforeHeaderKeep(currentPageNextY, /*freshPageStartY=*/0, viewportHeight,
                                           /*hasContent=*/true, leadHeight, headerHeight, bodyHeights, bodyCount)) {
    return false;
  }

  // The next page opens on whatever is still unlaid -- the caption, when one is
  // travelling with the header -- and on the table itself otherwise.
  // pageStartAnchor() folds in the bytes of a block that has NOT been laid yet,
  // so it names a position past text that is about to open the new page; keyed
  // on what is actually pending rather than on leadHeight, which is 0 both for
  // "nothing pending" and for "pending, but not a caption".
  const uint32_t anchor = pendingTextIsUnlaid() ? chapterSourceBytes_ : pageStartAnchor();
  completePageFn(std::move(currentPage), xpathParagraphIndex, xpathListItemIndex, anchor);
  completedPageCount++;
  currentPage = makeUniqueNoThrow<Page>();
  if (!currentPage) {
    noteAllocationFailure("Page before a table header");
    return true;
  }
  currentPageNextY = 0;
  return true;
}

// The pre-2026-08-19 shape, rebuilt from the buffer: one paragraph per cell,
// each prefixed with the column name (and the row name in bold, for the first
// cell of a row). Kept because it is the only thing that works for a table the
// columns path refuses.
void ChapterHtmlSlimParser::emitBufferedTableFlattened() {
  if (tableBuf.empty()) return;

  std::vector<std::string> colLabels;
  size_t colCount = 0;
  if (tableBufHasHeader) {
    const tablecolumns::Row& head = tableBuf.front();
    colLabels.reserve(head.size());
    for (const tablecolumns::Cell& c : head) {
      colLabels.push_back(TableCellLabel::normalize(tablecolumns::cellText(c)));
    }
    colCount = head.size();
  }

  // A <thead> with nothing under it: the head cells are the whole table, and
  // dropping them would render the table as nothing at all. Joined with middle
  // dots, which is what the streaming path has always done for this case.
  if (tableBufHasHeader && tableBuf.size() == 1) {
    auto headOnlyStyle = BlockStyle();
    headOnlyStyle.textAlignDefined = true;
    headOnlyStyle.alignment = (paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None))
                                  ? CssTextAlign::Justify
                                  : static_cast<CssTextAlign>(paragraphAlignment);
    startNewTextBlock(headOnlyStyle);
    std::string joined;
    for (const std::string& label : colLabels) {
      if (label.empty()) continue;
      if (!joined.empty()) joined += " \xc2\xb7 ";  // U+00B7 MIDDLE DOT
      joined += label;
    }
    if (!joined.empty()) {
      emitStyledText(joined, false, false);
      makePages();
      tableEmittedDataCell = true;
    }
    return;
  }

  // With no header row, EVERY row is data -- starting at 1 would silently eat
  // the first row of a table that simply did not use <thead>.
  for (size_t r = tableBufHasHeader ? 1 : 0; r < tableBuf.size(); r++) {
    const tablecolumns::Row& row = tableBuf[r];
    const std::string rowLabel =
        row.empty() ? std::string() : TableCellLabel::normalize(tablecolumns::cellText(row[0]));
    for (size_t c = 0; c < row.size(); c++) {
      auto cellStyle = BlockStyle();
      cellStyle.textAlignDefined = true;
      cellStyle.alignment = (paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None))
                                ? CssTextAlign::Justify
                                : static_cast<CssTextAlign>(paragraphAlignment);
      startNewTextBlock(cellStyle);
      nextWordContinues = false;

      const bool isRowLabelCell = (c == 0);
      const std::string label = TableCellLabel::forCell(colLabels, rowLabel, c + 1, colCount);
      if (!label.empty()) {
        emitStyledText(label, isRowLabelCell, !isRowLabelCell);
        nextWordContinues = false;
      }
      for (const tablecolumns::Run& run : row[c]) {
        emitStyledText(run.text, run.bold || isRowLabelCell, run.italic);
      }
      nextWordContinues = false;
      makePages();
      tableEmittedDataCell = true;
    }
  }
}

// --- T-021: the rotated page, and the key block it falls back to -----------
//
// Owner ruling 2026-08-19, against nine renders: a table that cannot be columns
// upright becomes a CLOCKWISE-turned page of its own -- the columns get the
// viewport's long axis instead of its short one -- and when even that will not
// hold the table, the column names stack as a bold block with each row beneath
// it as plain values.
//
// Landscape coordinates here mean: `across` runs along the reading direction and
// `down` marches through the rows. They map onto the page as
//   page x = down, page y = across
// because drawTextRotated90CCW takes the band's x and the run's start y, and the
// run descends the page. The parser owns all of this; PageRotatedText carries a
// finished line and its landing spot, nothing more.
bool ChapterHtmlSlimParser::emitBufferedTableRotated() {
  if (tableBuf.size() < 2) return false;

  // 2px margins: a table that has already cost the reader a device turn should
  // not also hand back a quarter of the axis it turned for (owner, 2026-08-19).
  constexpr int kRotMargin = 2;
  const int axis = viewportHeight - kRotMargin * 2;  // the long edge, reading direction
  const int cross = viewportWidth - kRotMargin * 2;  // the short edge, row direction
  // A size down from the body. Measured, not assumed: at the reading size a
  // five-column table's own word-floors summed to 899px against 668 available,
  // so "rotate it" without "and set it smaller" refuses tables that the renders
  // showed fitting comfortably.
  const int tableFont = smallFontId != 0 ? smallFontId : fontId;
  const int lineHeight = renderer.getLineHeight(tableFont, lineCompression);
  if (axis <= 0 || cross <= 0 || lineHeight <= 0) return false;

  struct MeasureCtx {
    ChapterHtmlSlimParser* self;
  } ctx{this};
  const auto measure = [](void* c, const char* text, const bool bold) {
    auto* m = static_cast<MeasureCtx*>(c);
    return m->self->renderer.getTextWidth(m->self->tableFontForRotation(), text,
                                          bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  };
  const tablecolumns::Plan plan =
      tablecolumns::planColumns(tableBuf, axis, renderer.getSpaceWidth(tableFont), measure, &ctx);
  if (!plan.usable) return false;

  // Lay the whole thing out FIRST, into a scratch list. Nothing is emitted until
  // it is known to fit: a rotated table that ran off the page mid-way would have
  // to be un-drawn, and there is no un-drawing a page.
  struct Placed {
    std::string text;
    bool bold;
    int across;
    int down;
  };
  std::vector<Placed> placed;
  placed.reserve(tableBuf.size() * plan.columnCount);
  int ruleDown = -1;  // where the header rule goes, in row-axis pixels

  const int rowGap = std::max(2, lineHeight / 4);
  int down = kRotMargin;
  for (size_t r = 0; r < tableBuf.size(); r++) {
    const bool headerRow = (r == 0) && tableBufHasHeader;
    int rowBottom = down;
    for (size_t c = 0; c < tableBuf[r].size() && c < plan.columnCount; c++) {
      const std::string cell = tablecolumns::cellText(tableBuf[r][c]);
      if (cell.empty()) continue;
      int lineDown = down;
      for (const auto& line : renderer.wrappedText(tableFont, cell.c_str(), plan.w[c], 8,
                                                   headerRow ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR)) {
        placed.push_back(Placed{line, headerRow, kRotMargin + plan.x[c], lineDown});
        lineDown += lineHeight;
      }
      rowBottom = std::max(rowBottom, lineDown);
    }
    down = rowBottom + rowGap;
    if (headerRow) {
      ruleDown = down;
      down += 2 + rowGap;  // the rule's thickness, and air under it
    }
    // One page only. A rotated table spanning pages would ask the reader to turn
    // the device back and forth mid-table; the key block is the better answer,
    // and this is the check that chooses it.
    if (down > cross) return false;
  }

  // It fits, and every `return false` above is behind us -- so this is the
  // first point at which emitting is allowed, and the last at which the
  // "nothing is emitted on a false return" contract still holds. A pending
  // <caption> is retired here so it lands above the table rather than on the
  // page after it.
  retirePendingBlockBeforeTable();

  // Give it a page of its own -- a rotated table sharing a page with upright
  // prose would be unreadable in both orientations at once.
  if (currentPage && !currentPage->elements.empty()) {
    completePageFn(std::move(currentPage), xpathParagraphIndex, xpathListItemIndex, pageStartAnchor());
    completedPageCount++;
    currentPage.reset();
  }
  if (!currentPage) {
    currentPage = makeUniqueNoThrow<Page>();
    if (!currentPage) {
      noteAllocationFailure("Page for a rotated table");
      return true;  // emitted nothing, but the parse is over either way
    }
    currentPageNextY = newPageStartY();
  }

  for (const Placed& item : placed) {
    // Two mappings, both learned from the render rather than from the maths:
    //
    //  * the ROW axis is inverted. Page x grows the way the reader's view moves
    //    UP once the device is turned, so a row `down` pixels from the top of
    //    the table sits at viewportWidth - down. Without this the header prints
    //    at the foot of the page and the rows read bottom-to-top.
    //  * x is the band's RIGHT edge for this call (the CW twin takes a left
    //    edge), so no line-height is added here; the subtraction already lands
    //    the band where it belongs.
    const int pageX = viewportWidth - kRotMargin - item.down;
    auto element = std::shared_ptr<PageRotatedText>(
        new (std::nothrow) PageRotatedText(item.text, item.bold, static_cast<int32_t>(tableFont),
                                           static_cast<int16_t>(pageX), static_cast<int16_t>(item.across)));
    if (!element) {
      noteAllocationFailure("a rotated table line");
      return true;
    }
    currentPage->elements.push_back(std::move(element));
  }
  // The one rule the ruling asks for, under the header. In the reader's turned
  // view it runs along the reading direction, which on the upright page is a
  // VERTICAL line -- drawn as a rule of zero-ish width would be invisible, so it
  // is a line element rather than a PageHorizontalRule.
  if (ruleDown >= 0) {
    const int16_t ruleX = static_cast<int16_t>(viewportWidth - kRotMargin - ruleDown);
    const int16_t ruleLen =
        static_cast<int16_t>(plan.x[plan.columnCount - 1] + plan.w[plan.columnCount - 1] + kRotMargin);
    auto rule = std::shared_ptr<PageVerticalRule>(new (std::nothrow) PageVerticalRule(
        static_cast<uint16_t>(std::max<int16_t>(1, ruleLen)), 2, ruleX, static_cast<int16_t>(kRotMargin)));
    if (rule) {
      currentPage->elements.push_back(std::move(rule));
    }
  }

  currentPageNextY = static_cast<int16_t>(viewportHeight);  // the page is spoken for
  resetInterBlockCollapse();  // a full-page table breaks paragraph adjacency (2026-08-22)
  tableEmittedDataCell = true;
  completePageFn(std::move(currentPage), xpathParagraphIndex, xpathListItemIndex, pageStartAnchor());
  completedPageCount++;
  currentPage.reset();
  currentPageNextY = 0;
  return true;
}

// The ruled fallback: the names once, as a block, then every row identical.
void ChapterHtmlSlimParser::emitBufferedTableKeyBlock() {
  if (tableBuf.empty()) return;

  auto blockStyle = [this] {
    auto style = BlockStyle();
    style.textAlignDefined = true;
    style.alignment = (paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None))
                          ? CssTextAlign::Left
                          : static_cast<CssTextAlign>(paragraphAlignment);
    return style;
  };

  if (tableBufHasHeader) {
    for (const tablecolumns::Cell& cell : tableBuf.front()) {
      const std::string name = tablecolumns::cellText(cell);
      if (name.empty()) continue;
      startNewTextBlock(blockStyle());
      nextWordContinues = false;
      emitStyledText(name, true, false);
      nextWordContinues = false;
      makePages();
    }
  }

  for (size_t r = tableBufHasHeader ? 1 : 0; r < tableBuf.size(); r++) {
    for (const tablecolumns::Cell& cell : tableBuf[r]) {
      startNewTextBlock(blockStyle());
      nextWordContinues = false;
      for (const tablecolumns::Run& run : cell) {
        emitStyledText(run.text, run.bold, run.italic);
      }
      nextWordContinues = false;
      makePages();
      tableEmittedDataCell = true;
    }
  }
}

int ChapterHtmlSlimParser::measureRowHeight(const tablecolumns::Row& row, const tablecolumns::Plan& plan,
                                            const bool headerRow) const {
  const int lineHeight = renderer.getLineHeight(fontId, lineCompression);
  int lines = 1;
  for (size_t c = 0; c < row.size() && c < plan.columnCount; c++) {
    const std::string text = tablecolumns::cellText(row[c]);
    if (text.empty()) continue;
    const auto wrapped = renderer.wrappedText(fontId, text.c_str(), plan.w[c], 32,
                                              headerRow ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    lines = std::max<int>(lines, static_cast<int>(wrapped.size()));
  }
  return lines * lineHeight;
}

void ChapterHtmlSlimParser::emitBufferedTableAsColumns(const tablecolumns::Plan& plan) {
  // Keep-with-next, first pass: BEFORE the caption is retired, because retiring
  // it spends it on this page. A part-built word has to be flushed first or the
  // block it belongs to measures short -- retirePendingBlockBeforeTable() does
  // the same thing for the same reason, one step later.
  if (partWordBufferIndex > 0) {
    flushPartWordBuffer();
  }
  // Keep-with-next. There are two moments at which the page can be completed
  // and EXACTLY ONE of them is used, decided by whether a caption is travelling:
  //
  //   a caption is pending   Decide BEFORE it is retired, with its height in
  //                          the group, and then never ask again. Retiring it
  //                          spends it on this page, so a later break could
  //                          only ever leave it behind -- which is half of what
  //                          the report is about. If the group cannot be kept
  //                          at all the constraint yields WITH the caption
  //                          still attached to its header, rather than buying a
  //                          row back by stranding it.
  //   nothing is pending,    Decide AFTER, against the real cursor. This is the
  //   or what is pending     one that fires for a table with no caption. The
  //   is a paragraph         cursor here is the honest one: measureUnlaidLead-
  //                          Height() answers 0 both for "nothing pending" and
  //                          for "pending, but a whole paragraph" -- and in the
  //                          second case the cursor BEFORE the retire is stale,
  //                          because that paragraph is about to be laid on this
  //                          page. Deciding against it breaks early, and then
  //                          the second decision breaks again after the
  //                          paragraph, leaving it alone on a page of its own.
  //                          Reachable from any `<div>text<table>`, which is
  //                          the shape B-037's adversarial pass also found.
  const int leadHeight = measureUnlaidLeadHeight();
  if (leadHeight > 0) {
    breakBeforeStrandedTableHeader(plan, leadHeight);
    // Establishes tablecolumns::columnStartY's `rowTopIsClear` precondition for
    // the first row: nothing unlaid may be owed a place at the row top the
    // columns are about to rewind to.
    retirePendingBlockBeforeTable();
  } else {
    retirePendingBlockBeforeTable();
    breakBeforeStrandedTableHeader(plan, 0);
  }
  // Table rows between two paragraphs break their margin adjacency (2026-08-22).
  resetInterBlockCollapse();
  const int lineHeight = renderer.getLineHeight(fontId, lineCompression);
  const int rowGap = std::max(2, lineHeight / 4);
  constexpr int kRuleThickness = 2;

  for (size_t r = 0; r < tableBuf.size(); r++) {
    const tablecolumns::Row& row = tableBuf[r];
    const bool headerRow = (r == 0) && tableBufHasHeader;
    const int rowHeight = measureRowHeight(row, plan, headerRow);
    const int needed = rowHeight + rowGap + (headerRow ? kRuleThickness + rowGap : 0);

    if (!currentPage) {
      currentPage = makeUniqueNoThrow<Page>();
      if (!currentPage) {
        noteAllocationFailure("Page for a table row");
        return;
      }
      currentPageNextY = newPageStartY();
    }
    // Rows move as a unit. Breaking inside one would page-break each column
    // independently, which puts half a row on each page with no way to tell.
    if (!currentPage->elements.empty() && currentPageNextY + needed > viewportHeight) {
      completePageFn(std::move(currentPage), xpathParagraphIndex, xpathListItemIndex, pageStartAnchor());
      completedPageCount++;
      currentPage = makeUniqueNoThrow<Page>();
      if (!currentPage) {
        noteAllocationFailure("Page after a table row break");
        return;
      }
      currentPageNextY = 0;
    }

    const int16_t rowTop = currentPageNextY;
    // Both preconditions of the per-column rewind, captured once per row while
    // they are still facts rather than assumptions. See tablecolumns::
    // columnStartY -- it owns the rule, this owns the evidence.
    const bool rowTopIsClear = !pendingTextIsUnlaid();
    const int rowStartPage = completedPageCount;
    int16_t rowBottom = rowTop;
    for (size_t c = 0; c < row.size() && c < plan.columnCount; c++) {
      currentPageNextY = static_cast<int16_t>(tablecolumns::columnStartY(
          rowTop, currentPageNextY, rowTopIsClear, completedPageCount == rowStartPage));
      auto cellStyle = BlockStyle();
      cellStyle.marginLeft = static_cast<int16_t>(plan.x[c]);
      cellStyle.marginRight = static_cast<int16_t>(viewportWidth - (plan.x[c] + plan.w[c]));
      cellStyle.textAlignDefined = true;
      cellStyle.alignment = plan.rightAlign[c] ? CssTextAlign::Right : CssTextAlign::Left;
      startNewTextBlock(cellStyle);
      nextWordContinues = false;
      for (const tablecolumns::Run& run : row[c]) {
        emitStyledText(run.text, run.bold || headerRow, run.italic);
      }
      nextWordContinues = false;
      makePages();
      // The row's bottom is the deepest column -- but only among columns still
      // on the page the row started on. Once a cell has spilled onto a new
      // page, a y measured on the old one is not comparable, and taking the max
      // would start the next row below the bottom of the wrong page.
      if (completedPageCount != rowStartPage || currentPageNextY > rowBottom) {
        rowBottom = currentPageNextY;
      }
    }
    currentPageNextY = static_cast<int16_t>(rowBottom + rowGap);
    snapToLineGrid();  // Line Grid: row gap rounds up so table baselines stay on it
    tableEmittedDataCell = true;

    // The one rule the ruling asks for: under the header, spanning the columns.
    if (headerRow && currentPage) {
      const int16_t ruleWidth = static_cast<int16_t>(plan.x[plan.columnCount - 1] + plan.w[plan.columnCount - 1]);
      auto rule = std::shared_ptr<PageHorizontalRule>(
          new (std::nothrow) PageHorizontalRule(static_cast<uint16_t>(ruleWidth), kRuleThickness, 0, currentPageNextY));
      if (!rule) {
        noteAllocationFailure("the table's header rule");
        return;
      }
      currentPage->elements.push_back(rule);
      currentPageNextY = static_cast<int16_t>(currentPageNextY + kRuleThickness + rowGap);
      snapToLineGrid();  // Line Grid: closing rule spacing rounds up too
    }
  }
}

void ChapterHtmlSlimParser::emitHorizontalRule(const BlockStyle& blockStyle) {
  if (partWordBufferIndex > 0) {
    flushPartWordBuffer();
  }
  // A rule between two paragraphs breaks their margin adjacency (2026-08-22).
  resetInterBlockCollapse();

  if (currentTextBlock) {
    const BlockStyle parentBlockStyle = currentTextBlock->getBlockStyle();
    startNewTextBlock(parentBlockStyle);
  }

  if (!currentPage) {
    currentPage.reset(new (std::nothrow) Page());
    if (!currentPage) {
      LOG_ERR("EHP", "Failed to create page for horizontal rule");
      return;
    }
    currentPageNextY = 0;
  }

  const int16_t lineHeight = static_cast<int16_t>(renderer.getLineHeight(fontId, lineCompression));
  const int16_t defaultVerticalSpacing = static_cast<int16_t>(lineHeight / 2);
  const int16_t topSpacing =
      static_cast<int16_t>((blockStyle.marginTop > 0 ? blockStyle.marginTop : defaultVerticalSpacing) +
                           (blockStyle.paddingTop > 0 ? blockStyle.paddingTop : 0));
  const int16_t bottomSpacing =
      static_cast<int16_t>((blockStyle.marginBottom > 0 ? blockStyle.marginBottom : defaultVerticalSpacing) +
                           (blockStyle.paddingBottom > 0 ? blockStyle.paddingBottom : 0));
  constexpr uint8_t ruleThickness = 2;
  const int16_t availableWidth =
      std::max<int16_t>(1, static_cast<int16_t>(viewportWidth - blockStyle.totalHorizontalInset()));
  const int16_t width = std::max<int16_t>(1, static_cast<int16_t>(availableWidth / 4));
  const int16_t xPos = static_cast<int16_t>(blockStyle.leftInset() + ((availableWidth - width) / 2));
  const int16_t totalHeight = static_cast<int16_t>(topSpacing + ruleThickness + bottomSpacing);

  if (!currentPage->elements.empty() && currentPageNextY + totalHeight > viewportHeight) {
    completePageFn(std::move(currentPage), xpathParagraphIndex, xpathListItemIndex, pageStartAnchor());
    completedPageCount++;
    currentPage.reset(new (std::nothrow) Page());
    if (!currentPage) {
      LOG_ERR("EHP", "Failed to create page after horizontal-rule page break");
      return;
    }
    currentPageNextY = 0;
  }

  currentPageNextY += topSpacing;

  auto pageRule = std::shared_ptr<PageHorizontalRule>(
      new (std::nothrow) PageHorizontalRule(width, ruleThickness, xPos, currentPageNextY));
  if (!pageRule) {
    LOG_ERR("EHP", "Failed to create PageHorizontalRule");
    return;
  }
  currentPage->elements.push_back(pageRule);
  currentPageNextY = static_cast<int16_t>(currentPageNextY + ruleThickness + bottomSpacing);
  // Line Grid: hr/br spacing rounds up to the next line-height multiple.
  snapToLineGrid();

  if (!pendingAnchorId.empty()) {
    anchorData.push_back({std::move(pendingAnchorId), static_cast<uint16_t>(completedPageCount)});
    pendingAnchorId.clear();
  }
}

void XMLCALL ChapterHtmlSlimParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);

  // Middle of skip
  if (self->skipUntilDepth < self->depth) {
    self->depth += 1;
    return;
  }

  if (strcmp(name, "p") == 0) {
    self->xpathParagraphIndex++;
  }
  if (strcmp(name, "li") == 0) {
    self->xpathListItemIndex++;
  }

  // Extract class, style, id, and dir attributes for CSS/RTL processing
  std::string classAttr;
  std::string styleAttr;
  std::string dirAttr;
  if (atts != nullptr) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "class") == 0) {
        classAttr = atts[i + 1];
      } else if (strcmp(atts[i], "style") == 0) {
        styleAttr = atts[i + 1];
      } else if (strcmp(atts[i], "id") == 0) {
        // Defer both anchor recording and TOC page breaks until startNewTextBlock,
        // after the previous block is flushed to pages via makePages().
        //
        // Skip IDs on non-navigable inline elements (e.g. <span>): these are never
        // link targets in epub content, but reading-system converters can inject tens
        // of thousands of them per chapter, exhausting the heap. TOC anchors are
        // always recorded regardless of element type, since they drive page breaks.
        const char* idValue = atts[i + 1];
        const bool isTocAnchor =
            std::find(self->tocAnchors.begin(), self->tocAnchors.end(), idValue) != self->tocAnchors.end();
        if (isTocAnchor || (!isNonNavigableInlineElement(name) && self->anchorData.size() < MAX_ANCHORS_PER_CHAPTER)) {
          // Flush a displaced anchor before overwriting. Consecutive non-block elements
          // (e.g. <aside id="fn1">text</aside><aside id="fn2">) with no intervening block
          // never trigger startNewTextBlock, so fn1 gets silently overwritten. That leaves
          // fn1 missing from the anchor map -> getPageForAnchor returns nullopt -> reader
          // lands at page 0 (section start) instead of the footnote.
          if (!self->pendingAnchorId.empty()) {
            self->flushPendingAnchor();
          }
          self->pendingAnchorId = idValue;
        }
      } else if (strcmp(atts[i], "dir") == 0) {
        dirAttr = atts[i + 1];
      }
    }
  }

  auto centeredBlockStyle = BlockStyle();
  centeredBlockStyle.textAlignDefined = true;
  centeredBlockStyle.alignment = CssTextAlign::Center;

  // Compute CSS style for this element early so display:none can short-circuit
  // before tag-specific branches emit any content or metadata.
  CssStyle cssStyle;
  if (self->cssParser) {
    cssStyle = self->cssParser->resolveStyle(name, classAttr);
    if (!styleAttr.empty()) {
      CssStyle inlineStyle = CssParser::parseInlineStyle(styleAttr);
      cssStyle.applyOver(inlineStyle);
    }
  }

  // HTML dir attribute overrides CSS direction (case-insensitive per HTML spec)
  if (!dirAttr.empty()) {
    if (strcasecmp(dirAttr.c_str(), "rtl") == 0) {
      cssStyle.direction = CssTextDirection::Rtl;
      cssStyle.defined.direction = 1;
    } else if (strcasecmp(dirAttr.c_str(), "ltr") == 0) {
      cssStyle.direction = CssTextDirection::Ltr;
      cssStyle.defined.direction = 1;
    }
  }

  // Direction is inherited in HTML/CSS. If this element does not define one, carry
  // the currently active inherited direction into its computed style.
  if (!cssStyle.hasDirection() && self->effectiveDirectionDefined) {
    cssStyle.direction = self->effectiveDirection;
    cssStyle.defined.direction = 1;
  }

  // Skip elements with display:none before all fast paths (tables, links, etc.).
  if (cssStyle.hasDisplay() && cssStyle.display == CssDisplay::None) {
    self->skipUntilDepth = self->depth;
    self->depth += 1;
    return;
  }

  // <pre> is not implemented: the whitespace collapser downstream treats its
  // content like any other prose, so the line breaks, the indentation and the
  // runs of spaces that ARE the content of a code listing or a piece of ASCII
  // art are gone by the time it reaches a block. Nothing here can fix that
  // cheaply; the reader can at least be told the shape he is looking at is not
  // the shape the book shipped.
  if (strcmp(name, "pre") == 0) {
    booknotes::current().raise(booknotes::Note::PreformattedCollapsed);
  }

  // Special handling for tables/cells: flatten into per-cell paragraphs with a prefixed header.
  if (strcmp(name, "table") == 0) {
    // skip nested tables -- their text is discarded, not reflowed
    if (self->tableDepth > 0) {
      booknotes::current().raise(booknotes::Note::TablesFlattened);
      self->tableDepth += 1;
      return;
    }
    booknotes::current().raise(booknotes::Note::TablesFlattened);

    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
    }
    self->tableDepth += 1;
    self->tableRowIndex = 0;
    self->tableColIndex = 0;
    self->tableInHead = false;
    self->tableColCount = 0;
    self->tableColLabels.clear();
    self->tableRowLabel.clear();
    self->tableLabelCapture.clear();
    self->tableCapturingLabel = false;
    self->tableEmittedDataCell = false;
    // Start buffering this table as a columns candidate. Anything the columns
    // path cannot lay out abandons it, and the streaming code below takes over
    // from that point -- so every branch after this stays reachable.
    self->tableBuffering = true;
    self->tableBufferAbandoned = false;
    self->tableCellOpen = false;
    self->tableBufHasHeader = false;
    self->tableBuf.clear();
    self->tableBufBytes = 0;
    self->depth += 1;
    return;
  }

  // Anything inside a cell that is not plain inline emphasis -- an image, a
  // link with a footnote, a list, a nested table -- is content the buffered
  // path would silently drop, because buffering keeps text and styles and
  // nothing else. Hand the table back to the streaming code, which handles all
  // of it exactly as it did before.
  if (self->tableBuffering && self->tableCellOpen) {
    static constexpr const char* kInlineOk[] = {"b", "strong", "i", "em", "span"};
    bool ok = false;
    for (const char* tag : kInlineOk) {
      if (strcmp(name, tag) == 0) {
        ok = true;
        break;
      }
    }
    if (!ok) {
      self->abandonTableBuffer();
    }
  }

  // Deliberately does not return: <thead> had no handler before and fell
  // through to the generic path, and it should keep doing so.
  if (self->tableDepth == 1 && strcmp(name, "thead") == 0) {
    self->tableInHead = true;
    if (self->tableBuffering) {
      self->tableBufHasHeader = true;
    }
  }

  if (self->tableDepth == 1 && strcmp(name, "tr") == 0) {
    if (self->tableBuffering) {
      if (self->tableBuf.size() >= tablecolumns::kMaxRows) {
        self->abandonTableBuffer();
      } else {
        self->tableBuf.emplace_back();
        self->tableCellOpen = false;
      }
    }
    self->tableRowIndex += 1;
    self->tableColIndex = 0;
    self->tableRowLabel.clear();
    self->depth += 1;
    return;
  }

  if (self->tableDepth == 1 && (strcmp(name, "td") == 0 || strcmp(name, "th") == 0)) {
    if (self->tableBuffering) {
      // A cell outside any row, or one column too many, is a shape this cannot
      // plan; hand the table back to the streaming path rather than guess.
      if (self->tableBuf.empty() || self->tableBuf.back().size() >= tablecolumns::kMaxColumns) {
        self->abandonTableBuffer();
      } else {
        // A <th> in the first row makes it a header even without a <thead>,
        // which is how most hand-written EPUB tables are marked up.
        if (self->tableBuf.size() == 1 && strcmp(name, "th") == 0) {
          self->tableBufHasHeader = true;
        }
        self->tableBuf.back().emplace_back();
        self->tableCellOpen = true;
        self->depth += 1;
        return;
      }
    }
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
    }
    self->tableColIndex += 1;
    if (self->tableRowIndex <= 1) {
      self->tableColCount = static_cast<size_t>(self->tableColIndex);
    }

    // A <thead> cell names its column instead of being read out. Divert its
    // text; nothing is emitted for it, and the name reappears in front of every
    // cell below it.
    if (self->tableInHead) {
      self->tableLabelCapture.clear();
      self->tableCapturingLabel = true;
      self->depth += 1;
      return;
    }

    self->tableEmittedDataCell = true;

    auto tableCellBlockStyle = BlockStyle();
    tableCellBlockStyle.textAlignDefined = true;
    const auto align = (self->paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None))
                           ? CssTextAlign::Justify
                           : static_cast<CssTextAlign>(self->paragraphAlignment);
    tableCellBlockStyle.alignment = align;
    self->startNewTextBlock(tableCellBlockStyle);
    // Unconditional: a cell always begins a new word. Emitting the label used
    // to be the only thing that reset this, and a two-column cell emits no
    // label, so its first word would attach to the previous cell's last one.
    self->nextWordContinues = false;

    // The first cell of a row is that row's name, so it is teed into
    // tableRowLabel on its way out and set bold — the pair of a two-column
    // table then reads as a term and its definition.
    const bool isRowLabelCell = self->tableColIndex <= 1;
    if (isRowLabelCell) {
      self->tableLabelCapture.clear();
      self->tableCapturingLabel = true;
    }

    const std::string headerText = TableCellLabel::forCell(
        self->tableColLabels, self->tableRowLabel, static_cast<size_t>(self->tableColIndex), self->tableColCount);
    if (!headerText.empty()) {
      StyleStackEntry headerStyle;
      headerStyle.depth = self->depth;
      headerStyle.hasBold = true;
      headerStyle.bold = isRowLabelCell;
      headerStyle.hasItalic = true;
      headerStyle.italic = !isRowLabelCell;
      self->inlineStyleStack.push_back(headerStyle);
      self->updateEffectiveInlineStyle();
      const CssTextDecoration savedTextDecoration = self->effectiveTextDecoration;
      self->effectiveTextDecoration = CssTextDecoration::None;
      // The label must not be teed back into the row name it is built from.
      const bool savedCapturing = self->tableCapturingLabel;
      self->tableCapturingLabel = false;
      self->characterData(userData, headerText.c_str(), static_cast<int>(headerText.length()));
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }
      self->tableCapturingLabel = savedCapturing;
      self->effectiveTextDecoration = savedTextDecoration;
      self->nextWordContinues = false;
      self->inlineStyleStack.pop_back();
      self->updateEffectiveInlineStyle();
    }

    if (isRowLabelCell) {
      StyleStackEntry rowNameStyle;
      rowNameStyle.depth = self->depth;
      rowNameStyle.hasBold = true;
      rowNameStyle.bold = true;
      self->inlineStyleStack.push_back(rowNameStyle);
      self->updateEffectiveInlineStyle();
    }

    self->depth += 1;
    return;
  }

  if (self->tableDepth == 1 && strcmp(name, "hr") == 0) {
    self->depth += 1;
    return;
  }

  if (matches(name, IMAGE_TAGS, std::size(IMAGE_TAGS))) {
    std::string src;
    std::string alt;
    if (atts != nullptr) {
      for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "src") == 0) {
          src = atts[i + 1];
        } else if (src.empty() && (strcmp(atts[i], "href") == 0 || strcmp(atts[i], "xlink:href") == 0)) {
          src = atts[i + 1];
        } else if (strcmp(atts[i], "alt") == 0) {
          alt = atts[i + 1];
        }
      }

      const size_t fragmentPos = src.find('#');
      if (fragmentPos != std::string::npos) {
        src.resize(fragmentPos);
      }

      // imageRendering: 0=display, 1=placeholder (alt text only), 2=suppress entirely
      if (self->imageRendering == 2) {
        self->skipUntilDepth = self->depth;
        self->depth += 1;
        return;
      }

      if (!src.empty() && self->imageRendering != 1) {
        LOG_DBG("EHP", "Found image: src=%s", src.c_str());

        {
          // Resolve the image path relative to the HTML file
          std::string resolvedPath = FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(self->contentBase + src));

          if (ImageDecoderFactory::isFormatSupported(resolvedPath)) {
            // Create a unique filename for the cached image
            std::string ext;
            size_t extPos = resolvedPath.rfind('.');
            if (extPos != std::string::npos) {
              ext = resolvedPath.substr(extPos);
            }
            std::string cachedImagePath = self->imageBasePath + std::to_string(self->imageCounter++) + ext;

            {
              // Probe the dimensions from the entry's first bytes (early-aborted
              // inflate, a few KB) instead of extracting the whole image now —
              // extraction is deferred to the first render of the page (see
              // ImageBlock's lazy extractor). This is what keeps first-open of an
              // image-heavy chapter from stalling for seconds per image.
              ImageDimensions dims = {0, 0};
              ImageDimsProbe headerProbe;
              self->epub->readItemContentsToStream(resolvedPath, headerProbe, 1024, /*allowEarlyStop=*/true);
              bool gotDimensions = headerProbe.getDimensions(dims);

              if (!gotDimensions) {
                // No header within the stream (rare) — fall back to extracting the
                // whole image and probing the file. That can take seconds, so
                // surface the indexing popup first (single-shot per parser).
                if (self->popupFn && !self->imagePopupFired) {
                  self->imagePopupFired = true;
                  self->popupFn();
                }
                HalFile cachedImageFile;
                bool extractSuccess = false;
                if (Storage.openFileForWrite("EHP", cachedImagePath, cachedImageFile)) {
                  extractSuccess = self->epub->readItemContentsToStream(resolvedPath, cachedImageFile, 4096);
                  cachedImageFile.flush();
                  cachedImageFile.close();
                }
                if (extractSuccess) {
                  // Retry to absorb SD-card sync latency on slow cards, and to close
                  // the silent-drop bug where a single getDimensions failure was fatal.
                  ImageToFramebufferDecoder* decoder = ImageDecoderFactory::getDecoder(cachedImagePath);
                  for (int attempt = 0; attempt < 3 && !gotDimensions; attempt++) {
                    if (attempt > 0) {
                      delay(50);  // Give a slow SD card time to finish syncing before retrying
                    }
                    gotDimensions = decoder && decoder->getDimensions(cachedImagePath, dims);
                  }
                } else {
                  LOG_ERR("EHP", "Failed to extract image");
                }
              }

              if (gotDimensions) {
                LOG_DBG("EHP", "Image dimensions: %dx%d", dims.width, dims.height);

                int displayWidth = 0;
                int displayHeight = 0;
                const float emSize = static_cast<float>(self->renderer.getFontAscenderSize(self->fontId));
                const CssStyle& imgStyle = cssStyle;
                const bool hasCssHeight = imgStyle.hasImageHeight();
                const bool hasCssWidth = imgStyle.hasImageWidth();

                // Compute effective container width for percentage-based image sizes.
                // If the image is inside a block with horizontal margins/padding (e.g.
                // <div style="margin: 1em 40%">), percentage widths like width:100%
                // should resolve against the container width, not the full viewport.
                int containerWidth = self->viewportWidth;
                if (self->currentTextBlock) {
                  const int inset = self->currentTextBlock->getBlockStyle().totalHorizontalInset();
                  if (inset > 0 && inset < self->viewportWidth) {
                    containerWidth = self->viewportWidth - inset;
                  }
                }

                if (hasCssHeight && hasCssWidth && dims.width > 0 && dims.height > 0) {
                  // Both CSS height and width set: resolve both, then clamp to viewport preserving requested ratio
                  displayHeight = static_cast<int>(
                      imgStyle.imageHeight.toPixels(emSize, static_cast<float>(self->viewportHeight)) + 0.5f);
                  displayWidth =
                      static_cast<int>(imgStyle.imageWidth.toPixels(emSize, static_cast<float>(containerWidth)) + 0.5f);
                  if (displayHeight < 1) displayHeight = 1;
                  if (displayWidth < 1) displayWidth = 1;
                  if (displayWidth > containerWidth || displayHeight > self->viewportHeight) {
                    float scaleX =
                        (displayWidth > containerWidth) ? static_cast<float>(containerWidth) / displayWidth : 1.0f;
                    float scaleY = (displayHeight > self->viewportHeight)
                                       ? static_cast<float>(self->viewportHeight) / displayHeight
                                       : 1.0f;
                    float scale = (scaleX < scaleY) ? scaleX : scaleY;
                    displayWidth = static_cast<int>(displayWidth * scale + 0.5f);
                    displayHeight = static_cast<int>(displayHeight * scale + 0.5f);
                    if (displayWidth < 1) displayWidth = 1;
                    if (displayHeight < 1) displayHeight = 1;
                  }
                  LOG_DBG("EHP", "Display size from CSS height+width: %dx%d", displayWidth, displayHeight);
                } else if (hasCssHeight && !hasCssWidth && dims.width > 0 && dims.height > 0) {
                  // Use CSS height (resolve % against viewport height) and derive width from aspect ratio
                  displayHeight = static_cast<int>(
                      imgStyle.imageHeight.toPixels(emSize, static_cast<float>(self->viewportHeight)) + 0.5f);
                  if (displayHeight < 1) displayHeight = 1;
                  displayWidth =
                      static_cast<int>(displayHeight * (static_cast<float>(dims.width) / dims.height) + 0.5f);
                  if (displayHeight > self->viewportHeight) {
                    displayHeight = self->viewportHeight;
                    // Rescale width to preserve aspect ratio when height is clamped
                    displayWidth =
                        static_cast<int>(displayHeight * (static_cast<float>(dims.width) / dims.height) + 0.5f);
                    if (displayWidth < 1) displayWidth = 1;
                  }
                  if (displayWidth > containerWidth) {
                    displayWidth = containerWidth;
                    // Rescale height to preserve aspect ratio when width is clamped
                    displayHeight =
                        static_cast<int>(displayWidth * (static_cast<float>(dims.height) / dims.width) + 0.5f);
                    if (displayHeight < 1) displayHeight = 1;
                  }
                  if (displayWidth < 1) displayWidth = 1;
                  LOG_DBG("EHP", "Display size from CSS height: %dx%d", displayWidth, displayHeight);
                } else if (hasCssWidth && !hasCssHeight && dims.width > 0 && dims.height > 0) {
                  // Use CSS width (resolve % against container width) and derive height from aspect ratio
                  displayWidth =
                      static_cast<int>(imgStyle.imageWidth.toPixels(emSize, static_cast<float>(containerWidth)) + 0.5f);
                  if (displayWidth > containerWidth) displayWidth = containerWidth;
                  if (displayWidth < 1) displayWidth = 1;
                  displayHeight =
                      static_cast<int>(displayWidth * (static_cast<float>(dims.height) / dims.width) + 0.5f);
                  if (displayHeight > self->viewportHeight) {
                    displayHeight = self->viewportHeight;
                    // Rescale width to preserve aspect ratio when height is clamped
                    displayWidth =
                        static_cast<int>(displayHeight * (static_cast<float>(dims.width) / dims.height) + 0.5f);
                    if (displayWidth < 1) displayWidth = 1;
                  }
                  if (displayHeight < 1) displayHeight = 1;
                  LOG_DBG("EHP", "Display size from CSS width: %dx%d", displayWidth, displayHeight);
                } else {
                  // Scale to fit container while maintaining aspect ratio
                  int maxWidth = containerWidth;
                  int maxHeight = self->viewportHeight;
                  float scaleX = (dims.width > maxWidth) ? (float)maxWidth / dims.width : 1.0f;
                  float scaleY = (dims.height > maxHeight) ? (float)maxHeight / dims.height : 1.0f;
                  float scale = (scaleX < scaleY) ? scaleX : scaleY;
                  if (scale > 1.0f) scale = 1.0f;

                  displayWidth = (int)(dims.width * scale);
                  displayHeight = (int)(dims.height * scale);
                  LOG_DBG("EHP", "Display size: %dx%d (scale %.2f)", displayWidth, displayHeight, scale);
                }

                // Flush any pending text block so it appears before the image
                if (self->partWordBufferIndex > 0) {
                  self->flushPartWordBuffer();
                }
                if (self->currentTextBlock && !self->currentTextBlock->isEmpty()) {
                  const BlockStyle parentBlockStyle = self->currentTextBlock->getBlockStyle();
                  self->startNewTextBlock(parentBlockStyle);
                }

                // Apply vertical margins from the container to the image.
                // Top margin lives on the empty text block (deposited via vertical merge
                // in startNewTextBlock). Bottom margin was stripped by withoutBottom() for
                // deferred application at element close, so read it from the stack.
                int16_t imageMarginTop = 0;
                int16_t imageMarginBottom = 0;
                if (self->currentTextBlock && self->currentTextBlock->isEmpty()) {
                  const auto& bs = self->currentTextBlock->getBlockStyle();
                  imageMarginTop = bs.topInset();
                  if (self->blockStyleStack.size() > 1) {
                    imageMarginBottom = self->blockStyleStack.back().bottomInset();
                  }
                }

                // Create page for image - only break if image won't fit remaining space
                if (self->currentPage && !self->currentPage->elements.empty() &&
                    (self->currentPageNextY + imageMarginTop + displayHeight + imageMarginBottom >
                     self->viewportHeight)) {
                  self->completePageFn(std::move(self->currentPage), self->xpathParagraphIndex,
                                       self->xpathListItemIndex, self->pageStartAnchor());
                  self->completedPageCount++;
                  self->currentPage.reset(new (std::nothrow) Page());
                  if (!self->currentPage) {
                    LOG_ERR("EHP", "Failed to create new page");
                    return;
                  }
                  self->currentPageNextY = 0;
                } else if (!self->currentPage) {
                  self->currentPage.reset(new (std::nothrow) Page());
                  if (!self->currentPage) {
                    LOG_ERR("EHP", "Failed to create initial page");
                    return;
                  }
                  self->currentPageNextY = self->newPageStartY();
                }

                // Apply top margin from container block — except at PAGE TOP,
                // where space-before collapses like every other block's (the
                // 2026-08-22 page-top collapse; the chapter sinkage above is
                // the one designed exception and must not be added to).
                if (!self->currentPage->elements.empty()) {
                  self->currentPageNextY += imageMarginTop;
                }

                // Create ImageBlock and add to page
                // nothrow: make_shared uses bare new, which aborts on OOM under
                // -fno-exceptions; images arrive mid-parse when the heap is at its
                // most loaded, so this must fail soft into the null-check below.
                auto imageBlock = std::shared_ptr<ImageBlock>(
                    new (std::nothrow) ImageBlock(cachedImagePath, resolvedPath, displayWidth, displayHeight));
                if (!imageBlock) {
                  LOG_ERR("EHP", "Failed to create ImageBlock");
                  return;
                }
                int xPos = (self->viewportWidth - displayWidth) / 2;
                auto pageImage =
                    std::shared_ptr<PageImage>(new (std::nothrow) PageImage(imageBlock, xPos, self->currentPageNextY));
                if (!pageImage) {
                  LOG_ERR("EHP", "Failed to create PageImage");
                  return;
                }
                self->currentPage->elements.push_back(pageImage);
                self->currentPageNextY += displayHeight + imageMarginBottom;
                // An image between two paragraphs breaks their margin
                // adjacency (2026-08-22): the next block's marginTop must not
                // collapse against the pre-image paragraph's bottom spacing.
                self->resetInterBlockCollapse();
                // Line Grid: the image's whole advance rounds up so the text
                // after it returns to the baseline grid.
                self->snapToLineGrid();

                // The image consumed the empty block's accumulated vertical spacing.
                // Reset the block so the Vertical merge in startNewTextBlock doesn't
                // re-apply the same margins to the next text paragraph.
                if (self->currentTextBlock && self->currentTextBlock->isEmpty()) {
                  BlockStyle resetStyle;
                  resetStyle.alignment = (self->paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None))
                                             ? CssTextAlign::Justify
                                             : static_cast<CssTextAlign>(self->paragraphAlignment);
                  self->currentTextBlock->setBlockStyle(resetStyle);
                }

                self->depth += 1;
                return;
              } else {
                LOG_ERR("EHP", "Failed to get image dimensions");
                Storage.remove(cachedImagePath.c_str());
              }
            }
          }  // isFormatSupported
        }
      }

      // Fallback to alt text if image processing fails
      if (!alt.empty()) {
        alt = "[Image: " + alt + "]";
        self->startNewTextBlock(self->blockStyleStack.back()
                                    .getCombinedBlockStyle(centeredBlockStyle, BlockStyle::CombineAxis::Horizontal)
                                    .withoutBottom());
        self->italicUntilDepth = std::min(self->italicUntilDepth, self->depth);
        self->depth += 1;
        self->characterData(userData, alt.c_str(), alt.length());
        // Skip any child content (skip until parent as we pre-advanced depth above)
        self->skipUntilDepth = self->depth - 1;
        return;
      }

      // No alt text, skip. The picture is gone with nothing in its place, so
      // the reader sees a gap he cannot account for -- the one image outcome
      // worth a note. (Alt text above is a substitution, not a loss.)
      booknotes::current().countDroppedImage();
      self->skipUntilDepth = self->depth;
      self->depth += 1;
      return;
    }
  }

  // Ruby tag handling
  if (strcmp(name, "ruby") == 0) {
    // <ruby> is an inline element: a base that follows text with no whitespace between them
    // continues the same visual word, exactly like <b>/<i> handling in endElement().
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    self->inRuby = true;
    self->rubyStartWordIndex = self->currentTextBlock ? static_cast<int>(self->currentTextBlock->size()) : 0;
    if (self->currentTextBlock) {
      self->currentTextBlock->ensureRubyCapacity();
    }
    self->rubyTextBuffer.clear();
    self->depth += 1;
    return;
  }
  if (strcmp(name, "rt") == 0) {
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
    }
    self->collectingRubyText = true;
    self->depth += 1;
    return;
  }

  if (matches(name, SKIP_TAGS, std::size(SKIP_TAGS))) {
    // start skip
    self->skipUntilDepth = self->depth;
    self->depth += 1;
    return;
  }

  // Skip blocks with role="doc-pagebreak" and epub:type="pagebreak"
  if (atts != nullptr) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "role") == 0 && strcmp(atts[i + 1], "doc-pagebreak") == 0 ||
          strcmp(atts[i], "epub:type") == 0 && strcmp(atts[i + 1], "pagebreak") == 0) {
        self->skipUntilDepth = self->depth;
        self->depth += 1;
        return;
      }
    }
  }

  // Detect internal <a href="..."> links (footnotes, cross-references)
  // Note: <aside epub:type="footnote"> elements are rendered as normal content
  // without special handling. Links pointing to them are collected as footnotes.
  if (strcmp(name, "a") == 0) {
    const char* href = getAttribute(atts, "href");

    bool isInternalLink = isInternalEpubLink(href);

    // Special case: javascript:void(0) links with data attributes
    // Example: <a href="javascript:void(0)"
    // data-xyz="{&quot;name&quot;:&quot;OPS/ch2.xhtml&quot;,&quot;frag&quot;:&quot;id46&quot;}">
    if (href && strncmp(href, "javascript:", 11) == 0) {
      isInternalLink = false;
      // TODO: Parse data-* attributes to extract actual href
    }

    if (isInternalLink) {
      // Flush buffer before style change
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
        self->nextWordContinues = true;
      }
      self->insideFootnoteLink = true;
      self->footnoteLinkDepth = self->depth;
      strncpy(self->currentFootnote.href, href, sizeof(self->currentFootnote.href) - 1);
      self->currentFootnote.href[sizeof(self->currentFootnote.href) - 1] = '\0';
      self->currentFootnote.number[0] = '\0';
      self->currentFootnoteLinkTextLen = 0;

      // Links are deliberately not underlined: the reader has no way to select
      // or follow a link inline, so the decoration is pure visual noise. The
      // entry is still pushed so direction inheritance and the depth-keyed pop
      // in endElement stay symmetric. <u>/<ins> tags and CSS text-decoration
      // keep their underlines through the normal style paths.
      StyleStackEntry entry;
      entry.depth = self->depth;
      applyDirectionToEntry(entry, cssStyle);
      self->inlineStyleStack.push_back(entry);
      self->updateEffectiveInlineStyle();

      // Skip CSS resolution — we already handled styling for this <a> tag
      self->depth += 1;
      return;
    }
  }

  const float emSize = static_cast<float>(self->renderer.getFontAscenderSize(self->fontId));
  const auto userAlignmentBlockStyle = BlockStyle::fromCssStyle(
      cssStyle, emSize, static_cast<CssTextAlign>(self->paragraphAlignment), self->viewportWidth);

  if (strcmp(name, "hr") == 0) {
    auto hrBlockStyle = BlockStyle::fromCssStyle(cssStyle, emSize, CssTextAlign::Left, self->viewportWidth);
    if (!self->embeddedStyle) {
      hrBlockStyle.marginLeft = 0;
      hrBlockStyle.marginRight = 0;
      hrBlockStyle.marginTop = 0;
      hrBlockStyle.marginBottom = 0;
      hrBlockStyle.paddingLeft = 0;
      hrBlockStyle.paddingRight = 0;
      hrBlockStyle.paddingTop = 0;
      hrBlockStyle.paddingBottom = 0;
      hrBlockStyle.textIndentDefined = false;
      hrBlockStyle.textIndent = 0;
    }
    self->emitHorizontalRule(hrBlockStyle);
    self->depth += 1;
    return;
  }

  if (matches(name, HEADER_TAGS, std::size(HEADER_TAGS))) {
    self->currentCssStyle = cssStyle;
    auto headerBlockStyle = BlockStyle::fromCssStyle(cssStyle, emSize, CssTextAlign::Center, self->viewportWidth);
    headerBlockStyle.textAlignDefined = true;
    if (self->embeddedStyle && cssStyle.hasTextAlign()) {
      headerBlockStyle.alignment = cssStyle.textAlign;
    }
    const auto accumulated = clampAccumulatedHorizontalInsets(
        self->blockStyleStack.back().getCombinedBlockStyle(headerBlockStyle, BlockStyle::CombineAxis::Horizontal),
        self->viewportWidth);
    self->blockStyleStack.push_back(accumulated);
    self->startNewTextBlock(accumulated.withoutBottom());
    // h1-h3 always open a fresh page, the same way a TOC chapter boundary does
    // (flushPendingAnchor). Beyond the typographic nicety, this is what makes a
    // heading the reader saw at the top of a page STAY at the top across a
    // font or size reflow: a page that begins at a forced break begins at the
    // same source position under every pagination, so the word-anchor
    // reposition lands exactly on it instead of on whichever page happens to
    // contain it. h4-h6 keep flowing inline — minor subheadings would
    // otherwise shatter dense technical books into page-per-heading. A TOC
    // boundary heading has already been split by flushPendingAnchor inside
    // startNewTextBlock, in which case the page is empty and this is a no-op.
    if (name[1] >= '1' && name[1] <= '3' && self->currentPage && !self->currentPage->elements.empty()) {
      self->completePageFn(std::move(self->currentPage), self->xpathParagraphIndex, self->xpathListItemIndex,
                           self->pageStartAnchor());
      self->completedPageCount++;
      self->currentPage.reset(new (std::nothrow) Page());
      if (!self->currentPage) {
        LOG_ERR("EHP", "Failed to create page after heading page break");
        return;
      }
      self->currentPageNextY = 0;
    }
    self->boldUntilDepth = std::min(self->boldUntilDepth, self->depth);
    self->updateEffectiveInlineStyle();
  } else if (matches(name, BLOCK_TAGS, std::size(BLOCK_TAGS))) {
    if (strcmp(name, "br") == 0) {
      if (self->partWordBufferIndex > 0) {
        // flush word preceding <br/> to currentTextBlock before calling startNewTextBlock
        self->flushPartWordBuffer();
      }
      // A <br> after text is a line break: start the next block with the container's
      // vertical margins stripped, matching browsers, which never apply paragraph
      // margins at a <br>. This is what keeps <br>-per-paragraph books (common CJK
      // web-novel formatting) from re-adding container spacing at every paragraph
      // and collapsing page capacity.
      // A <br> on an empty block (consecutive <br>s, or a standalone <br> between
      // blocks) is a scene-break separator: keep the container margins so deposited
      // vertical spacing survives. Either way the block is tagged so that if it
      // stays empty, startNewTextBlock injects a full line-height gap when the next
      // block opens; once text follows the tag is inert.
      // Style comes from the block style stack, not the current block, so a closed
      // element's style can't leak through (#2679).
      BlockStyle brStyle = self->blockStyleStack.back();
      if (self->currentTextBlock && !self->currentTextBlock->isEmpty()) {
        brStyle = brStyle.withoutTop().withoutBottom();
      }
      brStyle.fromBrElement = true;
      self->startNewTextBlock(brStyle);
    } else {
      self->currentCssStyle = cssStyle;
      BlockStyle elementStyle = userAlignmentBlockStyle;
      // This is the only place a NON-heading block's alignment is decided, and
      // BlockStyle::fromCssStyle discards the book's value outright when the
      // setting is not "Book's Style" -- which since 2026-08-23 it never is
      // (CrossPointSettings::paragraphAlignment is a constexpr JUSTIFIED, so
      // the honoring branch is dead code). Headings take a different path above
      // and DO keep their CSS, so the note is raised here rather than beside
      // fromCssStyle, where it would fire for elements that were never
      // overridden.
      if (cssStyle.hasTextAlign() && cssStyle.textAlign != elementStyle.alignment) {
        booknotes::current().raise(booknotes::Note::AlignmentOverridden);
      }
      const bool isListContainer = strcmp(name, "ul") == 0 || strcmp(name, "ol") == 0;
      const bool isListItem = strcmp(name, "li") == 0;
      std::string listMarker;
      if (isListContainer) {
        // One indent step per nesting level, but only when the publisher gave
        // this list no left inset of its own — explicit CSS (including an
        // explicit 0) is list fidelity and wins.
        if (!cssStyle.hasMarginLeft() && !cssStyle.hasPaddingLeft()) {
          elementStyle.marginLeft = static_cast<int16_t>(emSize * LIST_INDENT_STEP_EM);
        }
        ListContext ctx;
        ctx.ordered = (name[0] == 'o');
        if (ctx.ordered) {
          // HTML "start" attribute: first item's number. Lowercase per XHTML.
          const char* startAttr = getAttribute(atts, "start");
          if (startAttr != nullptr) {
            char* endp = nullptr;
            const long v = strtol(startAttr, &endp, 10);
            if (endp != startAttr && *endp == '\0' && v >= -99999 && v <= 99999) {
              ctx.nextValue = static_cast<int>(v);
            }
          }
        }
        self->listStack_.push_back(ctx);
      } else if (isListItem) {
        // The marker is an ordinary word carrying its own trailing space, so
        // its full advance is what the layout engine measures — and the item's
        // first text word is GLUED to it (listMarkerGluePending_), so under
        // Justified the marker→text gap is a continuation advance, never a
        // stretchable inter-word gap. Hanging indent: the block's first line
        // starts at -gutter (negative text-indent), placing the marker in the
        // gutter and the text at the block's content edge, where every wrapped
        // line returns. Plain decimal only — the CSS parser has no
        // list-style-type, so roman/alpha counters are not available.
        const bool ordered = !self->listStack_.empty() && self->listStack_.back().ordered;
        if (ordered) {
          ListContext& ctx = self->listStack_.back();
          // HTML "value" attribute on <li> resets the counter.
          const char* valueAttr = getAttribute(atts, "value");
          if (valueAttr != nullptr) {
            char* endp = nullptr;
            const long v = strtol(valueAttr, &endp, 10);
            if (endp != valueAttr && *endp == '\0' && v >= -99999 && v <= 99999) {
              ctx.nextValue = static_cast<int>(v);
            }
          }
          char markerBuf[16];
          snprintf(markerBuf, sizeof(markerBuf), "%d. ", ctx.nextValue);
          ctx.nextValue++;
          listMarker = markerBuf;
        } else {
          listMarker = "\xe2\x80\xa2 ";  // U+2022 bullet, same gutter scheme
        }
        const int gutter = self->renderer.getTextAdvanceX(self->fontId, listMarker.c_str(), EpdFontFamily::REGULAR);
        elementStyle.textIndent = static_cast<int16_t>(-gutter);
        elementStyle.textIndentDefined = true;
      }
      auto accumulated = clampAccumulatedHorizontalInsets(
          self->blockStyleStack.back().getCombinedBlockStyle(elementStyle, BlockStyle::CombineAxis::Horizontal),
          self->viewportWidth);
      if (isListContainer) {
        // A list container never carries a first-line indent: an ancestor
        // paragraph's text-indent (or an outer item's hanging indent, for a
        // list nested inside <li>) must not leak onto this list's items —
        // each <li> sets its own.
        accumulated.textIndent = 0;
        accumulated.textIndentDefined = false;
      }
      if (isListItem && accumulated.textIndent < 0 && accumulated.leftInset() < -accumulated.textIndent) {
        // The marker gutter is part of the item's left inset: if the
        // accumulated inset is narrower than the gutter (publisher zeroed the
        // list's margins, or a bare <li> outside any list), widen it so the
        // hanging marker cannot paint left of the page margin.
        accumulated.marginLeft =
            static_cast<int16_t>(accumulated.marginLeft + (-accumulated.textIndent - accumulated.leftInset()));
      }
      self->blockStyleStack.push_back(accumulated);
      self->startNewTextBlock(accumulated.withoutBottom());
      self->updateEffectiveInlineStyle();

      if (isListItem) {
        self->currentTextBlock->addWord(listMarker, EpdFontFamily::REGULAR);
        self->listItemBulletOnly = true;
        self->listMarkerGluePending_ = true;
      }
    }
  } else if (matches(name, UNDERLINE_TAGS, std::size(UNDERLINE_TAGS))) {
    // Flush buffer before style change so preceding text gets current style
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    self->pushDecorationStyleEntry(CssTextDecoration::Underline, cssStyle);
  } else if (matches(name, LINETHROUGH_TAGS, std::size(LINETHROUGH_TAGS))) {
    // Flush buffer before style change so preceding text gets current style
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    self->pushDecorationStyleEntry(CssTextDecoration::LineThrough, cssStyle);
  } else if (matches(name, BOLD_TAGS, std::size(BOLD_TAGS))) {
    // Flush buffer before style change so preceding text gets current style
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    self->boldUntilDepth = std::min(self->boldUntilDepth, self->depth);
    // Push inline style entry for bold tag
    StyleStackEntry entry;
    entry.depth = self->depth;  // Track depth for matching pop
    entry.hasBold = true;
    entry.bold = true;
    if (cssStyle.hasFontStyle()) {
      entry.hasItalic = true;
      entry.italic = cssStyle.fontStyle == CssFontStyle::Italic;
    }
    applyTextDecorationToEntry(entry, cssStyle);
    applyDirectionToEntry(entry, cssStyle);
    self->inlineStyleStack.push_back(entry);
    self->updateEffectiveInlineStyle();
  } else if (matches(name, ITALIC_TAGS, std::size(ITALIC_TAGS))) {
    // Flush buffer before style change so preceding text gets current style
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    self->italicUntilDepth = std::min(self->italicUntilDepth, self->depth);
    // Push inline style entry for italic tag
    StyleStackEntry entry;
    entry.depth = self->depth;  // Track depth for matching pop
    entry.hasItalic = true;
    entry.italic = true;
    if (cssStyle.hasFontWeight()) {
      entry.hasBold = true;
      entry.bold = cssStyle.fontWeight == CssFontWeight::Bold;
    }
    applyTextDecorationToEntry(entry, cssStyle);
    applyDirectionToEntry(entry, cssStyle);
    self->inlineStyleStack.push_back(entry);
    self->updateEffectiveInlineStyle();
  } else if (strcmp(name, "sup") == 0 || strcmp(name, "sub") == 0) {
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    StyleStackEntry entry;
    entry.depth = self->depth;
    if (strcmp(name, "sup") == 0) {
      entry.hasSup = true;
      entry.sup = true;
    } else {
      entry.hasSub = true;
      entry.sub = true;
    }
    self->inlineStyleStack.push_back(entry);
    self->updateEffectiveInlineStyle();
  } else if (strcmp(name, "span") == 0 || !isHeaderOrBlock(name)) {
    // Handle span and other inline elements for CSS styling
    if (cssStyle.hasFontWeight() || cssStyle.hasFontStyle() || cssStyle.hasTextDecoration() ||
        cssStyle.hasDirection() || cssStyle.hasVerticalAlign()) {
      // Flush buffer before style change so preceding text gets current style
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
        self->nextWordContinues = true;
      }
      StyleStackEntry entry;
      entry.depth = self->depth;  // Track depth for matching pop
      if (cssStyle.hasFontWeight()) {
        entry.hasBold = true;
        entry.bold = cssStyle.fontWeight == CssFontWeight::Bold;
      }
      if (cssStyle.hasFontStyle()) {
        entry.hasItalic = true;
        entry.italic = cssStyle.fontStyle == CssFontStyle::Italic;
      }
      applyTextDecorationToEntry(entry, cssStyle);
      applyDirectionToEntry(entry, cssStyle);
      if (cssStyle.hasVerticalAlign()) {
        if (cssStyle.verticalAlign == CssVerticalAlign::Super) {
          entry.hasSup = true;
          entry.sup = true;
        } else if (cssStyle.verticalAlign == CssVerticalAlign::Sub) {
          entry.hasSub = true;
          entry.sub = true;
        }
      }
      self->inlineStyleStack.push_back(entry);
      self->updateEffectiveInlineStyle();
    }
  }

  // Unprocessed tag, just increasing depth and continue forward
  self->depth += 1;
}

void XMLCALL ChapterHtmlSlimParser::characterData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);

  // Skip content of nested table
  if (self->tableDepth > 1) {
    return;
  }

  // Middle of skip
  if (self->skipUntilDepth < self->depth) {
    return;
  }

  // Collect ruby text instead of normal word processing
  if (self->collectingRubyText) {
    self->rubyTextBuffer.append(s, len);
    return;
  }

  // A buffered table swallows its text until </table>, when the whole thing can
  // be measured at once. Nothing is emitted here, and nothing is lost: either
  // the columns are laid out from the buffer, or abandonTableBuffer() replays
  // it through the flattened path.
  if (self->tableBuffering && self->tableCellOpen) {
    self->appendTableText(s, len);
    return;
  }

  // Table labels. A <thead> cell is captured INSTEAD of being emitted; the
  // first cell of a body row is captured AS WELL, since it is both the row's
  // name and content the reader still wants to see. The cap is on the buffer,
  // not the cell — a long cell keeps rendering, only its label is bounded.
  if (self->tableCapturingLabel) {
    if (self->tableLabelCapture.size() < TableCellLabel::kMaxLabelLen * 2) {
      self->tableLabelCapture.append(s, static_cast<size_t>(len));
    }
    if (self->tableInHead) {
      return;
    }
  }

  // Collect footnote link display text (for the number label)
  // Skip whitespace and brackets to normalize noterefs like "[1]" → "1"
  if (self->insideFootnoteLink) {
    int start = 0;
    int end = len - 1;

    // Example input and output texts:
    // "     [  12  ]   " => "12"
    // "   turn to 256  " => "turn to 256"

    // Ignore leading whitespaces and left square brackets
    while (start < len && (isWhitespace(s[start]) || (s[start] == '['))) {
      ++start;
    }

    // Ignore trailing whitespaces and right square brackets
    while (end >= start && (isWhitespace(s[end]) || (s[end] == ']'))) {
      --end;
    }

    // Extract footnote link text
    for (int i = start; (self->currentFootnoteLinkTextLen < sizeof(self->currentFootnote.number) - 1) && (i <= end);
         ++i) {
      self->currentFootnote.number[self->currentFootnoteLinkTextLen++] = s[i];
    }
    self->currentFootnote.number[self->currentFootnoteLinkTextLen] = '\0';
  }

  for (int i = 0; i < len; i++) {
    if (isWhitespace(s[i])) {
      // Currently looking at whitespace, if there's anything in the partWordBuffer, flush it
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }
      // Whitespace is a real word boundary — reset continuation state
      self->nextWordContinues = false;
      // Skip the whitespace char
      continue;
    }

    // Detect U+00A0 (non-breaking space, UTF-8: 0xC2 0xA0) or
    //        U+202F (narrow no-break space, UTF-8: 0xE2 0x80 0xAF).
    //
    // Both are rendered as a visible space but must never allow a line break around them.
    // We split the no-break space into its own word token and link the surrounding words
    // with continuation flags so the layout engine treats them as an indivisible group.
    //
    // Example: "200&#xA0;Quadratkilometer" or "200&#x202F;Quadratkilometer"
    //   Input bytes:  "200\xC2\xA0Quadratkilometer"  (or 0xE2 0x80 0xAF for U+202F)
    //   Tokens produced:
    //     [0] "200"               continues=false
    //     [1] " "                 continues=true   (attaches to "200", no gap)
    //     [2] "Quadratkilometer"  continues=true   (attaches to " ", no gap)
    //
    //   The continuation flags prevent the line-breaker from inserting a line break
    //   between "200" and "Quadratkilometer". However, "Quadratkilometer" is now a
    //   standalone word for hyphenation purposes, so Liang patterns can produce
    //   "200 Quadrat-" / "kilometer" instead of the unusable "200" / "Quadratkilometer".
    if (static_cast<uint8_t>(s[i]) == 0xC2 && i + 1 < len && static_cast<uint8_t>(s[i + 1]) == 0xA0) {
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }

      self->partWordBuffer[0] = ' ';
      self->partWordBuffer[1] = '\0';
      self->partWordBufferIndex = 1;
      self->nextWordContinues = true;  // Attach space to previous word (no break).
      self->flushPartWordBuffer();

      self->nextWordContinues = true;  // Next real word attaches to this space (no break).

      i++;  // Skip the second byte (0xA0)
      continue;
    }

    // U+202F (narrow no-break space) — identical logic to U+00A0 above.
    if (static_cast<uint8_t>(s[i]) == 0xE2 && i + 2 < len && static_cast<uint8_t>(s[i + 1]) == 0x80 &&
        static_cast<uint8_t>(s[i + 2]) == 0xAF) {
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }

      self->partWordBuffer[0] = ' ';
      self->partWordBuffer[1] = '\0';
      self->partWordBufferIndex = 1;
      self->nextWordContinues = true;
      self->flushPartWordBuffer();

      self->nextWordContinues = true;

      i += 2;  // Skip the remaining two bytes (0x80 0xAF)
      continue;
    }

    // Skip Zero Width No-Break Space / BOM (U+FEFF) = 0xEF 0xBB 0xBF
    const XML_Char FEFF_BYTE_1 = static_cast<XML_Char>(0xEF);
    const XML_Char FEFF_BYTE_2 = static_cast<XML_Char>(0xBB);
    const XML_Char FEFF_BYTE_3 = static_cast<XML_Char>(0xBF);

    if (s[i] == FEFF_BYTE_1) {
      // Check if the next two bytes complete the 3-byte sequence
      if ((i + 2 < len) && (s[i + 1] == FEFF_BYTE_2) && (s[i + 2] == FEFF_BYTE_3)) {
        // Sequence 0xEF 0xBB 0xBF found!
        i += 2;    // Skip the next two bytes
        continue;  // Move to the next iteration
      }
    }

    // If we're about to run out of space, then cut the word off and start a new one.
    // For CJK text (no spaces), this is the primary word-breaking mechanism.
    // We must avoid splitting multi-byte UTF-8 sequences across word boundaries,
    // otherwise the trailing bytes become orphaned continuation bytes that the
    // decoder can't interpret.
    if (self->partWordBufferIndex >= MAX_WORD_SIZE) {
      int safeLen = utf8SafeTruncateBuffer(self->partWordBuffer, self->partWordBufferIndex);

      if (safeLen < self->partWordBufferIndex && safeLen > 0) {
        // Incomplete UTF-8 sequence at the end — save it before flushing
        int overflow = self->partWordBufferIndex - safeLen;
        char saved[4];
        for (int j = 0; j < overflow; j++) {
          saved[j] = self->partWordBuffer[safeLen + j];
        }
        self->partWordBufferIndex = safeLen;
        self->flushPartWordBuffer();
        self->nextWordContinues = true;
        for (int j = 0; j < overflow; j++) {
          self->partWordBuffer[j] = saved[j];
        }
        self->partWordBufferIndex = overflow;
      } else {
        self->flushPartWordBuffer();
        self->nextWordContinues = true;
      }
    }

    self->partWordBuffer[self->partWordBufferIndex++] = s[i];
  }

  // Keep token growth bounded: CSS-heavy spans can fragment text into many tiny
  // words, so flush earlier when embedded CSS is active. We still keep the
  // "exclude last line" behavior to preserve paragraph flow across chunks.
  const size_t blockWordCount = self->currentTextBlock->size();
  const size_t softFlushThreshold =
      self->embeddedStyle ? TEXT_BLOCK_SOFT_FLUSH_WORDS_WITH_CSS : TEXT_BLOCK_SOFT_FLUSH_WORDS;
  if (blockWordCount > softFlushThreshold) {
    LOG_DBG("EHP", "Text block soft flush (%u words)", static_cast<unsigned>(blockWordCount));
    const int horizontalInset = self->currentTextBlock->getBlockStyle().totalHorizontalInset();
    const uint16_t effectiveWidth = (horizontalInset < self->viewportWidth)
                                        ? static_cast<uint16_t>(self->viewportWidth - horizontalInset)
                                        : self->viewportWidth;
    self->currentTextBlock->layoutAndExtractLines(
        self->renderer, self->fontId, effectiveWidth,
        [self](const std::shared_ptr<TextBlock>& textBlock) { self->addLineToPage(textBlock); }, false,
        self->justifyThresholdChars);
  }
}

void XMLCALL ChapterHtmlSlimParser::defaultHandlerExpand(void* userData, const XML_Char* s, const int len) {
  // Check if this looks like an entity reference (&...;)
  if (len >= 3 && s[0] == '&' && s[len - 1] == ';') {
    const char* utf8Value = lookupHtmlEntity(s, static_cast<size_t>(len));
    if (utf8Value != nullptr) {
      // Known entity: expand to its UTF-8 value
      characterData(userData, utf8Value, strlen(utf8Value));
      return;
    }
    // Unknown entity: preserve original &...; sequence
    characterData(userData, s, len);
    return;
  }
  // Not an entity we recognize - skip it
}

void XMLCALL ChapterHtmlSlimParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);

  // Ruby text: </rt> distributes ruby to base words, </ruby> resets ruby state
  if (strcmp(name, "rt") == 0) {
    self->collectingRubyText = false;
    if (self->inRuby && self->currentTextBlock) {
      const int currentWordCount = static_cast<int>(self->currentTextBlock->size());
      const int baseWordCount = currentWordCount - self->rubyStartWordIndex;
      std::string cleanRuby = trimAndNormalize(self->rubyTextBuffer);
      if (!cleanRuby.empty()) {
        if (baseWordCount > 0) {
          self->currentTextBlock->setRubyGroupAt(self->rubyStartWordIndex, baseWordCount, cleanRuby);
          self->rubyStartWordIndex = currentWordCount;
        } else if (self->rubyStartWordIndex > 0) {
          int leaderIdx = self->rubyStartWordIndex - 1;
          while (leaderIdx >= 0 &&
                 (self->currentTextBlock->getWordStyleAt(leaderIdx) & EpdFontFamily::RUBY_CONTINUE) != 0) {
            leaderIdx--;
          }
          if (leaderIdx >= 0) {
            std::string prevRuby = self->currentTextBlock->getRubyTextAt(leaderIdx);
            self->currentTextBlock->setRubyForWordAt(leaderIdx, prevRuby + cleanRuby);
          }
        }
      }
    }
    self->rubyTextBuffer.clear();
    // Inline close: the next base (e.g. 字 in <ruby>漢<rt>かん</rt>字<rt>じ</rt></ruby>) joins the
    // preceding one with no space. Whitespace in the source resets this in characterData().
    if (self->currentTextBlock && !self->currentTextBlock->isEmpty()) {
      self->nextWordContinues = true;
    }
    self->depth -= 1;
    return;
  }
  if (strcmp(name, "ruby") == 0 && self->inRuby) {
    self->inRuby = false;
    self->rubyStartWordIndex = -1;
    self->rubyTextBuffer.clear();
    // Inline close: text following </ruby> joins the annotated base with no space.
    if (self->currentTextBlock && !self->currentTextBlock->isEmpty()) {
      self->nextWordContinues = true;
    }
    self->depth -= 1;
    return;
  }
  // Check if any style state will change after we decrement depth
  // If so, we MUST flush the partWordBuffer with the CURRENT style first
  // Note: depth hasn't been decremented yet, so we check against (depth - 1)
  const bool willPopStyleStack =
      !self->inlineStyleStack.empty() && self->inlineStyleStack.back().depth == self->depth - 1;
  const bool willClearBold = self->boldUntilDepth == self->depth - 1;
  const bool willClearItalic = self->italicUntilDepth == self->depth - 1;

  const bool styleWillChange = willPopStyleStack || willClearBold || willClearItalic;
  const bool headerOrBlockTag = isHeaderOrBlock(name);
  const bool tableStructuralTag = isTableStructuralTag(name);

  if (self->tableDepth > 1 && strcmp(name, "table") == 0) {
    // get rid of all text inside the nested table
    self->partWordBufferIndex = 0;
    self->tableDepth -= 1;
    LOG_DBG("EHP", "nested table detected, get rid of its content");
    return;
  }

  // Flush buffer with current style BEFORE any style changes
  if (self->partWordBufferIndex > 0) {
    // Flush if style will change OR if we're closing a block/structural element
    const bool isInlineTag = !headerOrBlockTag && !tableStructuralTag &&
                             !matches(name, IMAGE_TAGS, std::size(IMAGE_TAGS)) && self->depth != 1;
    const bool shouldFlush = styleWillChange || headerOrBlockTag || matches(name, BOLD_TAGS, std::size(BOLD_TAGS)) ||
                             matches(name, ITALIC_TAGS, std::size(ITALIC_TAGS)) ||
                             matches(name, UNDERLINE_TAGS, std::size(UNDERLINE_TAGS)) ||
                             matches(name, LINETHROUGH_TAGS, std::size(LINETHROUGH_TAGS)) || tableStructuralTag ||
                             matches(name, IMAGE_TAGS, std::size(IMAGE_TAGS)) || self->depth == 1;

    if (shouldFlush) {
      self->flushPartWordBuffer();
      // If closing an inline element, the next word fragment continues the same visual word
      if (isInlineTag) {
        self->nextWordContinues = true;
      }
    }
  }

  self->depth -= 1;

  // Closing a footnote link — create entry from collected text and href
  if (self->insideFootnoteLink && self->depth == self->footnoteLinkDepth) {
    if (self->currentFootnote.number[0] != '\0' && self->currentFootnote.href[0] != '\0') {
      FootnoteEntry entry;
      strncpy(entry.number, self->currentFootnote.number, sizeof(entry.number) - 1);
      entry.number[sizeof(entry.number) - 1] = '\0';
      strncpy(entry.href, self->currentFootnote.href, sizeof(entry.href) - 1);
      entry.href[sizeof(entry.href) - 1] = '\0';
      int wordIndex =
          self->wordsExtractedInBlock + (self->currentTextBlock ? static_cast<int>(self->currentTextBlock->size()) : 0);
      self->pendingFootnotes.push_back({wordIndex, entry});
    }
    self->insideFootnoteLink = false;
  }

  // Leaving skip
  if (self->skipUntilDepth == self->depth) {
    self->skipUntilDepth = INT_MAX;
  }

  if (self->tableDepth == 1 && (strcmp(name, "td") == 0 || strcmp(name, "th") == 0)) {
    if (self->tableBuffering) {
      self->tableCellOpen = false;
      return;
    }
    if (self->tableCapturingLabel) {
      std::string label = TableCellLabel::normalize(self->tableLabelCapture);
      if (self->tableInHead) {
        if (self->tableColLabels.size() < TableCellLabel::kMaxCols) {
          self->tableColLabels.push_back(std::move(label));
        }
      } else {
        self->tableRowLabel = std::move(label);
      }
      self->tableLabelCapture.clear();
      self->tableCapturingLabel = false;
    }
    self->nextWordContinues = false;
  }

  if (self->tableDepth == 1 && strcmp(name, "thead") == 0) {
    self->tableInHead = false;
  }

  if (self->tableDepth == 1 && (strcmp(name, "tr") == 0)) {
    self->nextWordContinues = false;
  }

  if (self->tableDepth == 1 && strcmp(name, "table") == 0) {
    // The buffered table is decided here, where every cell has been seen. If the
    // plan is usable it is emitted as columns; if not -- ragged, too wide, too
    // narrow, too many columns -- it falls through to exactly the flattened
    // output this parser produced before columns existed.
    if (self->tableBuffering) {
      self->tableBuffering = false;
      self->tableCellOpen = false;
      const int spaceWidth = self->renderer.getSpaceWidth(self->fontId);
      struct MeasureCtx {
        ChapterHtmlSlimParser* self;
      } ctx{self};
      const auto measure = [](void* c, const char* text, const bool bold) {
        auto* m = static_cast<MeasureCtx*>(c);
        return m->self->renderer.getTextWidth(m->self->fontId, text,
                                              bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      };
      tablecolumns::Plan plan =
          tablecolumns::planColumns(self->tableBuf, self->viewportWidth, spaceWidth, measure, &ctx);

      // A row taller than the page cannot move as a unit, and a row that does
      // not move as a unit breaks each column onto a different page. Checked
      // before anything is emitted, so the fallback is still available.
      if (plan.usable) {
        for (size_t r = 0; r < self->tableBuf.size(); r++) {
          if (self->measureRowHeight(self->tableBuf[r], plan, r == 0) > self->viewportHeight) {
            plan.usable = false;
            break;
          }
        }
      }

      if (plan.usable) {
        self->emitBufferedTableAsColumns(plan);
      } else if (!self->emitBufferedTableRotated()) {
        // Ruled fallback order (T-021): columns upright, else a clockwise page,
        // else the key block. The old repeat-the-name-per-cell form is retired.
        self->emitBufferedTableKeyBlock();
      }
      self->tableBuf.clear();
      self->tableBufBytes = 0;
    }

    // A table whose only row was its <thead> would otherwise render as nothing,
    // because head cells are captured rather than emitted.
    if (!self->tableEmittedDataCell && !self->tableColLabels.empty()) {
      auto headOnlyStyle = BlockStyle();
      headOnlyStyle.textAlignDefined = true;
      headOnlyStyle.alignment = (self->paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None))
                                    ? CssTextAlign::Justify
                                    : static_cast<CssTextAlign>(self->paragraphAlignment);
      self->startNewTextBlock(headOnlyStyle);
      std::string joined;
      for (const auto& label : self->tableColLabels) {
        if (label.empty()) continue;
        if (!joined.empty()) joined += " \xc2\xb7 ";  // U+00B7 MIDDLE DOT
        joined += label;
      }
      if (!joined.empty()) {
        self->characterData(userData, joined.c_str(), static_cast<int>(joined.length()));
        if (self->partWordBufferIndex > 0) {
          self->flushPartWordBuffer();
        }
      }
    }

    self->tableDepth -= 1;
    self->tableRowIndex = 0;
    self->tableColIndex = 0;
    self->tableInHead = false;
    self->tableColCount = 0;
    self->tableColLabels.clear();
    self->tableRowLabel.clear();
    self->tableLabelCapture.clear();
    self->tableCapturingLabel = false;
    self->tableEmittedDataCell = false;
    self->nextWordContinues = false;
  }

  // Leaving bold tag
  if (self->boldUntilDepth == self->depth) {
    self->boldUntilDepth = INT_MAX;
  }

  // Leaving italic tag
  if (self->italicUntilDepth == self->depth) {
    self->italicUntilDepth = INT_MAX;
  }

  // Pop from inline style stack if we pushed an entry at this depth
  // This handles all inline elements: b, i, u, span, etc.
  if (!self->inlineStyleStack.empty() && self->inlineStyleStack.back().depth == self->depth) {
    self->inlineStyleStack.pop_back();
    self->updateEffectiveInlineStyle();
  }

  // Clear block style when leaving header or block elements
  if (headerOrBlockTag) {
    self->currentCssStyle.reset();
    self->updateEffectiveInlineStyle();

    // br is self-closing and not a container — it doesn't push/pop the stack.
    if (strcmp(name, "br") != 0 && self->blockStyleStack.size() > 1) {
      // Apply closing element's bottom margin to the current text block so
      // container spacing appears after the element's content (on the last child),
      // not on the first child via the empty-block merge in startNewTextBlock.
      if (self->currentTextBlock) {
        const auto style = self->currentTextBlock->getBlockStyle();
        self->currentTextBlock->setBlockStyle(style.addBottom(self->blockStyleStack.back()));
      }
      self->blockStyleStack.pop_back();
      // Start a new text block with the parent style to prevent subsequent bare text
      // from inheriting the closed block style (e.g. alignment or margins).
      self->startNewTextBlock(self->blockStyleStack.back());
    }

    // </li> closes: if the bullet never got inline text (empty <li> or <li> with only
    // block children that were flushed), clear the flag so the next sibling doesn't
    // merge into this block.
    if (strcmp(name, "li") == 0) {
      self->listItemBulletOnly = false;
    }

    // </ul>/</ol> closes its list context (nested <ol> numbering restarts per
    // level because each pushes its own counter).
    if ((strcmp(name, "ul") == 0 || strcmp(name, "ol") == 0) && !self->listStack_.empty()) {
      self->listStack_.pop_back();
    }
  }
}

ChapterHtmlSlimParser::~ChapterHtmlSlimParser() { abortParse(); }

bool ChapterHtmlSlimParser::beginParse() {
  // Initialize block style stack with a root entry representing "no ancestor block elements".
  // The user's paragraph alignment is set as the default so child elements without explicit
  // text-align inherit it correctly through getCombinedBlockStyle.
  BlockStyle rootBlockStyle;
  rootBlockStyle.alignment = (this->paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None))
                                 ? CssTextAlign::Justify
                                 : static_cast<CssTextAlign>(this->paragraphAlignment);
  blockStyleStack.clear();
  blockStyleStack.reserve(8);
  blockStyleStack.push_back(rootBlockStyle);
  listStack_.clear();
  listMarkerGluePending_ = false;

  auto paragraphAlignmentBlockStyle = BlockStyle();
  paragraphAlignmentBlockStyle.textAlignDefined = true;
  const auto align = rootBlockStyle.alignment;
  paragraphAlignmentBlockStyle.alignment = align;
  startNewTextBlock(paragraphAlignmentBlockStyle);

  xmlParser_ = XML_ParserCreate(nullptr);
  if (!xmlParser_) {
    LOG_ERR("EHP", "Couldn't allocate memory for parser");
    return false;
  }

  // The legacy code pages. Without this a chapter declaring windows-1252 --
  // the most common non-UTF-8 encoding in EPUB -- fails with "unknown
  // encoding" and the reader has no way past it (sweep item #52).
  installXmlEncodingSupport(xmlParser_);

  // Handle HTML entities (like &nbsp;) that aren't in XML spec or DTD
  // Using DefaultHandlerExpand preserves normal entity expansion from DOCTYPE
  XML_SetDefaultHandlerExpand(xmlParser_, defaultHandlerExpand);

  if (!Storage.openFileForRead("EHP", filepath, parseFile_)) {
    destroyXmlParser(xmlParser_);
    xmlParser_ = nullptr;
    return false;
  }

  // Get file size to decide whether to show indexing popup.
  if (popupFn && parseFile_.size() >= MIN_SIZE_FOR_POPUP) {
    popupFn();
  }

  XML_SetUserData(xmlParser_, this);
  XML_SetElementHandler(xmlParser_, startElement, endElement);
  XML_SetCharacterDataHandler(xmlParser_, characterData);

  // Count the codepoints this reading face has no shape for, for the duration
  // of the chapter parse and no longer. ARMED here rather than globally because
  // every UI screen draws through the same renderer, and a filename or a
  // model's answer carrying an emoji is not a fact about the book.
  missingglyphs::current().arm();

  parseStartTime_ = millis();
  return true;
}

ChapterHtmlSlimParser::ParseStatus ChapterHtmlSlimParser::parseStep() {
  void* const buf = XML_GetBuffer(xmlParser_, PARSE_BUFFER_SIZE);
  if (!buf) {
    LOG_ERR("EHP", "Couldn't allocate memory for buffer");
    return ParseStatus::Error;
  }

  const size_t len = parseFile_.read(buf, PARSE_BUFFER_SIZE);

  if (len == 0 && parseFile_.available() > 0) {
    LOG_ERR("EHP", "File read error");
    return ParseStatus::Error;
  }

  const int done = parseFile_.available() == 0;

  if (XML_ParseBuffer(xmlParser_, static_cast<int>(len), done) == XML_STATUS_ERROR) {
    // An OOM stopped the parser deliberately; say so rather than reporting
    // expat's "parsing aborted", which reads like a malformed document.
    if (allocFailed_) {
      return ParseStatus::Error;
    }
    LOG_ERR("EHP", "Parse error at line %lu:\n%s", XML_GetCurrentLineNumber(xmlParser_),
            XML_ErrorString(XML_GetErrorCode(xmlParser_)));
    return ParseStatus::Error;
  }
  if (allocFailed_) {
    return ParseStatus::Error;
  }

  return done ? ParseStatus::Done : ParseStatus::More;
}

// Close the ledger and hand the figure to the note. Called from both endings
// of a parse: a chapter abandoned halfway still met real characters this font
// cannot draw, and dropping the count would make the note appear and disappear
// with the memory pressure of the moment.
void ChapterHtmlSlimParser::reportMissingGlyphs() {
  auto& ledger = missingglyphs::current();
  ledger.disarm();
  if (ledger.distinct() > 0) {
    booknotes::current().setMissingCodepoints(ledger.distinct());
    LOG_DBG("EHP", "Reading font has no shape for %u distinct codepoints so far%s", ledger.distinct(),
            ledger.isSaturated() ? " (ledger full; the true number is higher)" : "");
  }
}

void ChapterHtmlSlimParser::abortParse() {
  reportMissingGlyphs();
  if (xmlParser_) {
    destroyXmlParser(xmlParser_);
    xmlParser_ = nullptr;
  }
  // Only close the file if it was successfully opened in beginParse()
  if (parseFile_.isOpen()) {
    parseFile_.close();
  }
}

bool ChapterHtmlSlimParser::finishParse() {
  // Never flush a trailing page from a layout that ran out of memory partway:
  // the caller caches what it is handed, and a short chapter cached to disk is
  // silent text loss that survives every later read.
  if (allocFailed_) {
    abortParse();
    return false;
  }
  if (xmlParser_) {
    LOG_DBG("EHP", "Time to parse and build pages: %lu ms", millis() - parseStartTime_);
    destroyXmlParser(xmlParser_);
    xmlParser_ = nullptr;
  }
  parseFile_.close();

  // Process last page if there is still text
  if (currentTextBlock) {
    makePages();
    if (!pendingAnchorId.empty()) {
      anchorData.push_back({std::move(pendingAnchorId), static_cast<uint16_t>(completedPageCount)});
      pendingAnchorId.clear();
    }
    completePageFn(std::move(currentPage), xpathParagraphIndex, xpathListItemIndex, pageStartAnchor());
    completedPageCount++;
    currentPage.reset();
    currentTextBlock.reset();
  }

  // LAST, not first: makePages() above lays out the trailing page, and its
  // measure walk is as much a part of this chapter as the parse was.
  reportMissingGlyphs();
  return true;
}

void ChapterHtmlSlimParser::noteAllocationFailure(const char* what) {
  if (allocFailed_) return;
  allocFailed_ = true;
  LOG_ERR("EHP", "OOM: %s; abandoning this chapter's layout", what);
  // Stop expat NOW. Every dereference of currentPage / currentTextBlock in this
  // file assumes they are non-null, and the only thing that can null them is the
  // allocation that just failed -- so the fix is to stop callbacks arriving
  // rather than to null-check 46 sites. XML_FALSE = not resumable.
  if (xmlParser_) {
    XML_StopParser(xmlParser_, XML_FALSE);
  }
}

bool ChapterHtmlSlimParser::parseAndBuildPages() {
  if (!beginParse()) {
    return false;
  }
  for (;;) {
    const ParseStatus status = parseStep();
    if (status == ParseStatus::Error) {
      abortParse();
      return false;
    }
    if (status == ParseStatus::Done) {
      break;
    }
  }
  return finishParse();
}

// Chapter sinkage: the fraction of the viewport height a chapter opening drops
// before its first line. Owner ruling 2026-08-22, verbatim: "include chapter
// drop as the only exception to collapse space above rule." Every OTHER
// page-top space collapses (see makePages); the first page of a spine section
// instead gets this DESIGNED drop — deliberate, uniform across chapters,
// chosen by the layout rather than inherited from the file's CSS, which is
// classical chapter sinkage. 1/5 of the viewport, SNAPPED DOWN to a whole
// number of line-heights so the line grid below it holds; that sits between
// the old accidental ~1.5-line CSS leak and the classical 1/3-page drop,
// sized for this small panel.
constexpr int kChapterSinkageFraction = 5;  // sinkage = viewportHeight / 5

int16_t ChapterHtmlSlimParser::newPageStartY() const {
  if (completedPageCount > 0) return 0;
  const int lineHeight = renderer.getLineHeight(fontId, lineCompression);
  if (lineHeight <= 0) return 0;
  const int lines = (viewportHeight / kChapterSinkageFraction) / lineHeight;
  return static_cast<int16_t>(lines * lineHeight);
}

// The leaded height a line advances the cursor by.
int ChapterHtmlSlimParser::lineAdvanceOf(const TextBlock& line) const {
  return renderer.getLineHeight(fontId, lineCompression) + line.getRubyShift(renderer.getFontAscenderSize(fontId));
}

// The LAST line on a page only needs its INK to fit: ascender + descender
// (plus any ruby riding above the ascender) MINUS the cap-ink trim the
// renderers subtract at paint time — the whole painted page sits capInkTrim
// px higher than the layout's y, so that air is real bottom room. For the
// built-in reader family the bare ascender+descender EXCEEDS the leaded
// advanceY (47 vs 45 at 18 pt — the metrics carry negative external
// leading), which is why the trim term is not optional: without it this test
// would paginate EARLIER than the old full-line-box rule. With it the ink
// extent is 38 px at 18 pt against a 45 px slot, which is what lets a page
// whose last slot the old rule rejected keep that line (2026-08-22 layout
// exactness pass; crosspoint-simulator/docs/zen-page-margins.md). Clamped to
// the leaded height so no metric combination can ever paginate earlier than
// before. The advance (currentPageNextY += lineHeight) still uses the
// full leaded height, so line pitch is untouched. Pagination change —
// covered by the SECTION_FILE_VERSION bump to v38 (Section.cpp).
int ChapterHtmlSlimParser::lineFitExtentOf(const TextBlock& line) const {
  const int rubyShift = line.getRubyShift(renderer.getFontAscenderSize(fontId));
  int inkExtent = renderer.getFontAscenderSize(fontId) + renderer.getFontDescenderSize(fontId) + rubyShift -
                  renderer.getCapInkTrim(fontId);
  if (inkExtent < 1) inkExtent = 1;
  const int lineHeight = renderer.getLineHeight(fontId, lineCompression) + rubyShift;
  return inkExtent < lineHeight ? inkExtent : lineHeight;
}

void ChapterHtmlSlimParser::snapToLineGrid() {
  if (!lineGridEnabled) return;
  const int lineHeight = renderer.getLineHeight(fontId, lineCompression);
  if (lineHeight <= 0 || currentPageNextY <= 0) return;
  const int rem = currentPageNextY % lineHeight;
  if (rem != 0) currentPageNextY = static_cast<int16_t>(currentPageNextY + lineHeight - rem);
}

void ChapterHtmlSlimParser::breakPageBefore(const uint32_t anchor) {
  if (!currentPage || currentPage->elements.empty()) return;
  completePageFn(std::move(currentPage), xpathParagraphIndex, xpathListItemIndex, anchor);
  completedPageCount++;
  currentPage = makeUniqueNoThrow<Page>();
  if (!currentPage) {
    noteAllocationFailure("Page at a line break");
    return;
  }
  currentPageNextY = 0;
}

// Widow/orphan control (keep-2/2, 2026-08-22): lines are buffered here and
// placed by placeLineOnPage()/flushPendingLines(). Holding THREE lines back is
// what resolves the 3-line-paragraph conflict case exactly: when the fourth
// line arrives the first may be placed knowing at least two follow it (orphan
// side), and a paragraph whose lines are all still pending at flush time is
// short enough to decide with full knowledge. Pagination change — covered by
// the SECTION_FILE_VERSION bump to v39 (Section.cpp).
void ChapterHtmlSlimParser::addLineToPage(std::shared_ptr<TextBlock> line) {
  // This line's source anchor, captured NOW: by placement time
  // lastExtractedLineSourceStart() has moved on to a later line.
  const uint32_t anchor =
      chapterSourceBytes_ + (currentTextBlock ? currentTextBlock->lastExtractedLineSourceStart() : 0);
  pendingLines_.push_back({std::move(line), anchor});
  if (pendingLines_.size() <= 3) return;

  PendingLine l = std::move(pendingLines_.front());
  pendingLines_.pop_front();
  if (paraLinesPlaced_ == 0 && currentPage && !currentPage->elements.empty()) {
    // Orphan control: the paragraph's FIRST line, with (holdback) at least
    // three more lines following. It may only land on this page if the second
    // line fits under it; otherwise break early so 2+ travel together. Only on
    // a non-empty page — on an empty one there is nothing above to orphan it.
    const TextBlock& second = *pendingLines_.front().line;
    if (currentPageNextY + lineAdvanceOf(*l.line) + lineFitExtentOf(second) > viewportHeight) {
      breakPageBefore(l.anchor);
    }
  }
  placeLineOnPage(std::move(l.line), l.anchor);
  paraLinesPlaced_++;
}

// Places the paragraph's still-buffered tail. Runs at the paragraph boundary
// (end of makePages), where the LAST line is finally known.
void ChapterHtmlSlimParser::flushPendingLines() {
  if (pendingLines_.empty()) {
    paraLinesPlaced_ = 0;
    return;
  }
  const size_t totalLines = static_cast<size_t>(paraLinesPlaced_) + pendingLines_.size();

  if (totalLines <= 2) {
    // 1–2 line paragraphs are exempt from both rules.
    while (!pendingLines_.empty()) {
      PendingLine l = std::move(pendingLines_.front());
      pendingLines_.pop_front();
      placeLineOnPage(std::move(l.line), l.anchor);
    }
    paraLinesPlaced_ = 0;
    return;
  }

  if (paraLinesPlaced_ == 0) {
    // The whole (3-line) paragraph is pending: keep-2/2 on three lines means
    // either all three land on this page or all three open the next one — a
    // 2/1 split widows the last line and a 1/2 split orphans the first.
    int y = currentPageNextY;
    bool allFit = currentPage && !currentPage->elements.empty();
    if (allFit) {
      for (size_t i = 0; i < pendingLines_.size(); i++) {
        const TextBlock& ln = *pendingLines_[i].line;
        const bool isLastPending = i + 1 == pendingLines_.size();
        if (y + (isLastPending ? lineFitExtentOf(ln) : lineAdvanceOf(ln)) > viewportHeight) {
          allFit = false;
          break;
        }
        y += lineAdvanceOf(ln);
      }
      if (!allFit) breakPageBefore(pendingLines_.front().anchor);
    }
  } else {
    // Paragraph of 4+ lines: everything before the final two places normally
    // (each still has two or more lines after it).
    while (pendingLines_.size() > 2) {
      PendingLine l = std::move(pendingLines_.front());
      pendingLines_.pop_front();
      placeLineOnPage(std::move(l.line), l.anchor);
      paraLinesPlaced_++;
    }
    // Widow control: if the LAST line would not fit on this page after the
    // second-to-last, break before the second-to-last so both travel. Only
    // when the second-to-last itself still fits here — otherwise the natural
    // page break already moves them together.
    const TextBlock& secondToLast = *pendingLines_[0].line;
    const TextBlock& last = *pendingLines_[1].line;
    if (currentPage && !currentPage->elements.empty() &&
        currentPageNextY + lineFitExtentOf(secondToLast) <= viewportHeight &&
        currentPageNextY + lineAdvanceOf(secondToLast) + lineFitExtentOf(last) > viewportHeight) {
      breakPageBefore(pendingLines_.front().anchor);
    }
  }

  while (!pendingLines_.empty()) {
    PendingLine l = std::move(pendingLines_.front());
    pendingLines_.pop_front();
    placeLineOnPage(std::move(l.line), l.anchor);
  }
  paraLinesPlaced_ = 0;
}

void ChapterHtmlSlimParser::placeLineOnPage(std::shared_ptr<TextBlock> line, const uint32_t anchor) {
  const int lineHeight = lineAdvanceOf(*line);

  if (!currentPage) {
    currentPage = makeUniqueNoThrow<Page>();
    if (!currentPage) {
      noteAllocationFailure("first Page of a chapter");
      return;
    }
    currentPageNextY = newPageStartY();
  }

  // See lineFitExtentOf for why the last line of a page fits by INK, not by
  // its leaded box.
  if (currentPageNextY + lineFitExtentOf(*line) > viewportHeight) {
    // The page ends here; the incoming line opens the next one, so the next
    // page's start anchor is that line's source position within the chapter.
    completePageFn(std::move(currentPage), xpathParagraphIndex, xpathListItemIndex, anchor);
    completedPageCount++;
    currentPage = makeUniqueNoThrow<Page>();
    if (!currentPage) {
      noteAllocationFailure("Page at a line break");
      return;
    }
    currentPageNextY = 0;
  }

  // Track cumulative words to assign footnotes to the page containing their anchor
  wordsExtractedInBlock += line->wordCount();
  auto footnoteIt = pendingFootnotes.begin();
  while (footnoteIt != pendingFootnotes.end() && footnoteIt->first <= wordsExtractedInBlock) {
    currentPage->addFootnote(footnoteIt->second.number, footnoteIt->second.href);
    ++footnoteIt;
  }
  pendingFootnotes.erase(pendingFootnotes.begin(), footnoteIt);

  // Apply horizontal left inset (margin + padding) as x position offset
  const int16_t xOffset = line->getBlockStyle().leftInset();
  currentPage->elements.push_back(std::make_shared<PageLine>(line, xOffset, currentPageNextY));
  currentPageNextY += lineHeight;
  // Line Grid: a ruby line advances by more than one line-height; round up so
  // the next baseline returns to the grid. No-op for plain lines already on it.
  snapToLineGrid();
}

void ChapterHtmlSlimParser::makePages() {
  if (!currentTextBlock) {
    LOG_ERR("EHP", "!! No text block to make pages for !!");
    return;
  }

  if (!currentPage) {
    currentPage = makeUniqueNoThrow<Page>();
    if (!currentPage) {
      noteAllocationFailure("Page for a laid-out block");
      return;
    }
    currentPageNextY = newPageStartY();
  }

  const int lineHeight = renderer.getLineHeight(fontId, lineCompression);

  // Apply top spacing before the paragraph (stored in pixels) — but NOT at the
  // top of a page. Classic page-top margin collapse: space-before exists to
  // separate a block from the content above it, and at the top of a page there
  // is nothing above. Honoring it there put a heading's full marginTop (46 px
  // at 12 pt, 68 px at 18 pt, ~1.8 em — measured, crosspoint-simulator/
  // docs/zen-page-margins.md §3 item 4) of dead space above every
  // chapter-opening heading. "At the top" is judged by the page being EMPTY,
  // not by y == 0, because the section's first page starts at the chapter
  // sinkage (newPageStartY above) — the one designed exception, which REPLACES
  // the file's space-before rather than adding to it (owner ruling
  // 2026-08-22). 2026-08-22 layout exactness pass; pagination change, covered
  // by the SECTION_FILE_VERSION bump to v38 (Section.cpp).
  const BlockStyle& blockStyle = currentTextBlock->getBlockStyle();
  // Inter-block gap cap (owner ruling 2026-08-22, verbatim: "keep half-line
  // gap, but collapse any gap that is more than a half-line gap"; report: the
  // Wingspan title page showed ~3-line-height gaps between its styled blocks
  // because marginBottom + half-line + marginTop all SUMMED). The whole gap
  // between two blocks in flow — previous block's bottom spacing plus this
  // block's top spacing — is capped at lineHeight/2. The previous block's
  // bottom pass already spent prevBottomApplied of that budget, so this top
  // pass adds at most the remainder. Gaps that compute smaller than half a
  // line stay as computed. The page-top rules are untouched: space-before
  // still collapses on an empty page, and the chapter sinkage stays the one
  // designed exception (owner, same report: "chapter heading 'Wingspan' is
  // placed okay though").
  const int16_t prevBottomApplied = prevBlockBottomApplied_;
  const int16_t sceneBreakLift = sceneBreakLiftPx_;
  resetInterBlockCollapse();
  if (!currentPage->elements.empty()) {
    int topSpacing = blockStyle.marginTop > 0 ? blockStyle.marginTop : 0;
    if (blockStyle.paddingTop > 0) {
      topSpacing += blockStyle.paddingTop;
    }
    const int gapCap = lineHeight / 2;
    const int budget = gapCap > prevBottomApplied ? gapCap - prevBottomApplied : 0;
    if (topSpacing > budget) {
      topSpacing = budget;
    }
    // A <br> section separator's designed blank line rides ON TOP of the
    // capped gap — see startNewTextBlock.
    currentPageNextY = static_cast<int16_t>(currentPageNextY + topSpacing + sceneBreakLift);
    // Line Grid: block top spacing rounds up to the next line-height multiple
    // so the paragraph's first baseline stays on the grid.
    snapToLineGrid();
  }

  // Calculate effective width accounting for horizontal margins/padding
  const int horizontalInset = blockStyle.totalHorizontalInset();
  const uint16_t effectiveWidth =
      (horizontalInset < viewportWidth) ? static_cast<uint16_t>(viewportWidth - horizontalInset) : viewportWidth;

  currentTextBlock->layoutAndExtractLines(
      renderer, fontId, effectiveWidth,
      [this](const std::shared_ptr<TextBlock>& textBlock) { addLineToPage(textBlock); }, /*includeLastLine=*/true,
      justifyThresholdChars);

  // Paragraph boundary: place the held-back tail with widow/orphan control.
  // Must run before the footnote fallback and bottom spacing below, and before
  // any non-line element (image, rule, table) is emitted onto the page.
  flushPendingLines();

  // Fallback: transfer any remaining pending footnotes to current page.
  // Normally addLineToPage handles this via word-index tracking, but this catches
  // edge cases where a footnote's word index equals the exact block size.
  if (!pendingFootnotes.empty() && currentPage) {
    for (const auto& [idx, fn] : pendingFootnotes) {
      currentPage->addFootnote(fn.number, fn.href);
    }
    pendingFootnotes.clear();
  }

  // Apply bottom spacing after the paragraph (stored in pixels), capped at
  // half a line (owner ruling 2026-08-22, see the top pass above): CSS
  // marginBottom, paddingBottom and the extra paragraph spacing compute a
  // gap, and anything over lineHeight/2 collapses to exactly lineHeight/2.
  // A plain paragraph (no CSS spacing) computes exactly the half-line, so
  // the default rhythm is byte-identical to what always shipped; the styled
  // blocks that used to stack margin + half-line + margin are what the cap
  // exists for. What was applied here is remembered so the next block's top
  // pass only adds the remainder of the same half-line budget.
  {
    int bottomSpacing = blockStyle.marginBottom > 0 ? blockStyle.marginBottom : 0;
    if (blockStyle.paddingBottom > 0) {
      bottomSpacing += blockStyle.paddingBottom;
    }
    if (extraParagraphSpacing) {
      bottomSpacing += lineHeight / 2;
    }
    const int gapCap = lineHeight / 2;
    if (bottomSpacing > gapCap) {
      bottomSpacing = gapCap;
    }
    currentPageNextY = static_cast<int16_t>(currentPageNextY + bottomSpacing);
    prevBlockBottomApplied_ = static_cast<int16_t>(bottomSpacing);
  }
  // Line Grid: paragraph bottom spacing rounds up to the next line-height
  // multiple so the following paragraph's baselines stay on the grid.
  snapToLineGrid();
}
