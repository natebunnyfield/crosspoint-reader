#pragma once

#include <HalStorage.h>
#include <expat.h>

#include <climits>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Epub/FootnoteEntry.h"
#include "Epub/ParsedText.h"
#include "Epub/blocks/ImageBlock.h"
#include "Epub/blocks/TextBlock.h"
#include "Epub/css/CssParser.h"
#include "Epub/css/CssStyle.h"
#include "TableCellLabel.h"
#include "TableColumnLayout.h"

class Page;
class GfxRenderer;
class Epub;

#define MAX_WORD_SIZE 200

class ChapterHtmlSlimParser {
  std::shared_ptr<Epub> epub;
  const std::string& filepath;
  GfxRenderer& renderer;
  // (page, paragraphIndex, listItemIndex, wordAnchor) — wordAnchor is the
  // chapter-global source byte position where the NEXT page begins; see
  // pageStartAnchor().
  std::function<void(std::unique_ptr<Page>, uint16_t, uint16_t, uint32_t)> completePageFn;
  std::function<void()> popupFn;  // Popup callback
  bool imagePopupFired = false;   // popupFn fired for the first image probe (single-shot)
  int depth = 0;
  int skipUntilDepth = INT_MAX;
  int boldUntilDepth = INT_MAX;
  int italicUntilDepth = INT_MAX;
  // buffer for building up words from characters, will auto break if longer than this
  // leave one char at end for null pointer
  char partWordBuffer[MAX_WORD_SIZE + 1] = {};
  int partWordBufferIndex = 0;
  bool nextWordContinues = false;  // true when next flushed word attaches to previous (inline element boundary)
  std::unique_ptr<ParsedText> currentTextBlock = nullptr;
  // Ruby text state
  bool inRuby = false;
  int rubyStartWordIndex = -1;
  bool collectingRubyText = false;
  std::string rubyTextBuffer;
  // Sticky: set by noteAllocationFailure(), never cleared within a parse.
  bool allocFailed_ = false;
  // Records the OOM, stops expat, and leaves the parse to fail at the next
  // parseStep(). The caller must return immediately after calling it: the
  // current callback keeps running to its end even after XML_StopParser.
  void noteAllocationFailure(const char* what);

  std::unique_ptr<Page> currentPage = nullptr;
  int16_t currentPageNextY = 0;

  // --- Inter-block gap cap (owner ruling 2026-08-22) ---------------------
  // Owner, verbatim: "keep half-line gap, but collapse any gap that is more
  // than a half-line gap." The vertical gap between two blocks in flow is
  // CAPPED at lineHeight/2 — whatever CSS margins, padding and the extra
  // paragraph spacing compute, a larger gap collapses to exactly half a
  // line; a smaller one stays as computed. prevBlockBottomApplied_ is what
  // the previous block's bottom pass already spent of that budget, so the
  // next block's top pass only adds the remainder. Consumed and cleared by
  // every makePages() call; any non-text element placed between two blocks
  // (image, rule, table) breaks adjacency and clears it via
  // resetInterBlockCollapse(). sceneBreakLiftPx_ carries the <br> section
  // separator's designed blank line, which rides ON TOP of the capped gap —
  // it is generated space marking a scene break, not a CSS gap, and capping
  // it would make section breaks indistinguishable from paragraph gaps.
  int16_t prevBlockBottomApplied_ = 0;
  int16_t sceneBreakLiftPx_ = 0;
  void resetInterBlockCollapse() {
    prevBlockBottomApplied_ = 0;
    sceneBreakLiftPx_ = 0;
  }

  // --- Widow/orphan control (keep-2/2, 2026-08-22) -----------------------
  // Lines are HELD BACK (up to three) between extraction and page placement so
  // the paginator knows, when it places a paragraph's first line, that at
  // least two more follow, and knows at the paragraph's end which line is the
  // last. addLineToPage() buffers; placeLineOnPage() is the old placement
  // body; flushPendingLines() runs at the end of makePages() — the paragraph
  // boundary — and applies both rules. Paragraphs of 1–2 lines are exempt.
  // Each pending line carries its own source anchor, captured at extraction
  // time, because lastExtractedLineSourceStart() has moved on by placement
  // time.
  struct PendingLine {
    std::shared_ptr<TextBlock> line;
    uint32_t anchor;  // chapter-global source anchor of this line's first word
  };
  std::deque<PendingLine> pendingLines_;
  int paraLinesPlaced_ = 0;  // lines of the current paragraph already placed
  void placeLineOnPage(std::shared_ptr<TextBlock> line, uint32_t anchor);
  void flushPendingLines();
  // Completes the current page (if it holds anything) so the next placement
  // starts a fresh one. `anchor` is the source position the new page starts at.
  void breakPageBefore(uint32_t anchor);
  // The leaded height this line advances the cursor by / the ink extent it
  // needs to FIT as the last line of a page (see placeLineOnPage).
  int lineAdvanceOf(const TextBlock& line) const;
  int lineFitExtentOf(const TextBlock& line) const;

  // Line Grid (2026-08-22, spec.lineGridEnabled): rounds currentPageNextY UP
  // to the next whole line-height multiple. No-op when the option is off.
  void snapToLineGrid();
  // The y a freshly created page starts at: the chapter sinkage on the
  // SECTION'S FIRST page (completedPageCount == 0), 0 on every later page.
  // See the definition for the owner ruling and the sizing.
  int16_t newPageStartY() const;
  int fontId;
  // The smallest cut of the same family, for the rotated table page (T-021).
  // Zero means "no smaller face" and the reading size is used.
  int smallFontId = 0;
  float lineCompression;
  bool extraParagraphSpacing;
  uint8_t paragraphAlignment;
  uint16_t viewportWidth;
  uint16_t viewportHeight;
  bool hyphenationEnabled;
  bool focusReadingEnabled;
  bool lineGridEnabled;
  const CssParser* cssParser;
  bool embeddedStyle;
  uint8_t imageRendering;
  std::string contentBase;
  std::string imageBasePath;
  int imageCounter = 0;

  // Style tracking (replaces depth-based approach)
  struct StyleStackEntry {
    int depth = 0;
    bool hasBold = false, bold = false;
    bool hasItalic = false, italic = false;
    bool hasTextDecoration = false;
    CssTextDecoration textDecoration = CssTextDecoration::None;
    bool hasDirection = false;
    CssTextDirection direction = CssTextDirection::Ltr;
    bool hasSup = false, sup = false;
    bool hasSub = false, sub = false;
  };
  std::vector<StyleStackEntry> inlineStyleStack;
  std::vector<BlockStyle> blockStyleStack;  // accumulated block styles from open ancestor elements
  CssStyle currentCssStyle;
  bool effectiveBold = false;
  bool effectiveItalic = false;
  CssTextDecoration effectiveTextDecoration = CssTextDecoration::None;
  bool effectiveDirectionDefined = false;
  CssTextDirection effectiveDirection = CssTextDirection::Ltr;
  bool effectiveSup = false;
  bool effectiveSub = false;
  int tableDepth = 0;
  int tableRowIndex = 0;
  int tableColIndex = 0;
  // Table cell labelling — see TableCellLabel.h. A <thead> row is captured as
  // column names rather than emitted (every data cell then carries its column
  // name, so nothing is lost), and the first cell of each body row is teed into
  // tableRowLabel as it streams past. Only an explicit <thead> counts: guessing
  // that row one is a header would swallow real content from the many books
  // that open a table with data.
  bool tableInHead = false;  // inside <thead> of the outermost table
  size_t tableColCount = 0;  // cells counted in the table's first row
  std::vector<std::string> tableColLabels;
  std::string tableRowLabel;      // first cell of the row being emitted
  std::string tableLabelCapture;  // accumulates a cell's text for the two above
  bool tableCapturingLabel = false;
  bool tableEmittedDataCell = false;  // guards a <thead>-only table losing its head

  // --- T-012: tables as columns ------------------------------------------
  // Columns cannot be laid out until every cell has been measured, and this is
  // a single-pass SAX parse, so a candidate table is BUFFERED and emitted at
  // </table>. Buffering is abandoned the moment the table turns out to be
  // something this cannot lay out -- an image, a link, a nested table, a list,
  // or simply too much text -- and the flattened streaming path takes over
  // mid-table exactly as it always did. That is why the old path is still here
  // untouched: it is the fallback, not dead code.
  bool tableBuffering = false;
  bool tableBufferAbandoned = false;  // this table already fell back to streaming
  std::vector<tablecolumns::Row> tableBuf;
  size_t tableBufBytes = 0;
  bool tableCellOpen = false;  // a <td>/<th> is receiving text right now
  // Whether row 0 of the buffer is a HEADER or just the first row of data.
  // Without this, a table with no <thead> loses its first row: the flattened
  // fallback would treat it as labels and emit rows 1..n, and the columns path
  // would set it bold under a rule it never earned.
  bool tableBufHasHeader = false;

  void abandonTableBuffer();  // replay what was buffered, then stream the rest
  void appendTableText(const char* text, int len);
  void emitBufferedTableFlattened();
  void emitBufferedTableAsColumns(const tablecolumns::Plan& plan);
  // T-021. A table too wide for the upright page becomes a page of its own,
  // turned clockwise, so the columns get the long axis. Returns false when it
  // does not fit even rotated -- the caller then emits the key-block form,
  // which is the ruled fallback. Nothing is emitted on a false return.
  bool emitBufferedTableRotated();
  // The face a rotated table is measured and drawn in: a size down when the
  // family has one, the reading face otherwise.
  int tableFontForRotation() const { return smallFontId != 0 ? smallFontId : fontId; }
  void emitBufferedTableKeyBlock();
  void emitStyledText(const std::string& text, bool bold, bool italic);
  // Height a row needs, in pixels, at the planned column widths. Used BEFORE
  // anything is emitted: a row taller than a page means columns are abandoned
  // for the whole table, because a row split across a page boundary breaks each
  // column independently and reads as garbage.
  int measureRowHeight(const tablecolumns::Row& row, const tablecolumns::Plan& plan, bool headerRow) const;
  bool listItemBulletOnly = false;  // true when currentTextBlock has only the <li> bullet

  // Anchor-to-page mapping: tracks which page each HTML id attribute lands on
  int completedPageCount = 0;
  std::vector<std::pair<std::string, uint16_t>> anchorData;
  std::string pendingAnchorId;          // deferred until after previous text block is flushed
  std::vector<std::string> tocAnchors;  // the list of anchors that are TOC chapter boundaries
  uint16_t xpathParagraphIndex = 0;
  uint16_t xpathListItemIndex = 0;
  // Source bytes of every COMPLETED text block. Together with the live
  // block's own counters this yields a chapter-global, layout-invariant
  // anchor for any page boundary.
  uint32_t chapterSourceBytes_ = 0;
  // Anchor for a page break at a BLOCK boundary (TOC/HR/image breaks, final
  // flush): the live block — empty, or fully laid out but not yet folded into
  // chapterSourceBytes_ — contributes its whole byte count. Mid-block breaks
  // in addLineToPage use the incoming line's own anchor instead.
  uint32_t pageStartAnchor() const {
    return chapterSourceBytes_ + (currentTextBlock ? currentTextBlock->sourceByteCount() : 0);
  }

  // Footnote link tracking
  bool insideFootnoteLink = false;
  int footnoteLinkDepth = -1;
  FootnoteEntry currentFootnote = {};
  int currentFootnoteLinkTextLen = 0;
  std::vector<std::pair<int, FootnoteEntry>> pendingFootnotes;  // <wordIndex, entry>
  int wordsExtractedInBlock = 0;

  // Resumable parse state. The one-shot parseAndBuildPages() drives these
  // internally; the incremental section builder drives them across render ticks
  // so a large single chapter can yield between pages instead of blocking the UI
  // until the whole thing is laid out. parseFile_ and the expat parser stay alive
  // for the lifetime of the parse so it can be paused and resumed at buffer
  // boundaries.
  XML_Parser xmlParser_ = nullptr;
  HalFile parseFile_;
  uint32_t parseStartTime_ = 0;

  void updateEffectiveInlineStyle();
  void startNewTextBlock(const BlockStyle& blockStyle);
  void flushPendingAnchor();
  void flushPartWordBuffer();
  void makePages();
  static EpdFontFamily::Style fontStyleForTextDecoration(CssTextDecoration decoration);
  static void applyDirectionToEntry(StyleStackEntry& entry, const CssStyle& css);
  static void applyTextDecorationToEntry(StyleStackEntry& entry, const CssStyle& css);
  void pushDecorationStyleEntry(CssTextDecoration defaultDecoration, const CssStyle& cssStyle);
  void emitHorizontalRule(const BlockStyle& blockStyle);
  // XML callbacks
  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL characterData(void* userData, const XML_Char* s, int len);
  static void XMLCALL defaultHandlerExpand(void* userData, const XML_Char* s, int len);
  static void XMLCALL endElement(void* userData, const XML_Char* name);

 public:
  explicit ChapterHtmlSlimParser(
      std::shared_ptr<Epub> epub, const std::string& filepath, GfxRenderer& renderer, const int fontId,
      const int smallFontId, const float lineCompression, const bool extraParagraphSpacing,
      const uint8_t paragraphAlignment, const uint16_t viewportWidth, const uint16_t viewportHeight,
      const bool hyphenationEnabled, const bool focusReadingEnabled, const bool lineGridEnabled,
      const std::function<void(std::unique_ptr<Page>, uint16_t, uint16_t, uint32_t)>& completePageFn,
      const bool embeddedStyle, const std::string& contentBase, const std::string& imageBasePath,
      const uint8_t imageRendering = 0, std::vector<std::string> tocAnchors = {},
      const std::function<void()>& popupFn = nullptr, const CssParser* cssParser = nullptr)

      : epub(epub),
        filepath(filepath),
        renderer(renderer),
        fontId(fontId),
        smallFontId(smallFontId),
        lineCompression(lineCompression),
        extraParagraphSpacing(extraParagraphSpacing),
        paragraphAlignment(paragraphAlignment),
        viewportWidth(viewportWidth),
        viewportHeight(viewportHeight),
        hyphenationEnabled(hyphenationEnabled),
        focusReadingEnabled(focusReadingEnabled),
        lineGridEnabled(lineGridEnabled),
        completePageFn(completePageFn),
        popupFn(popupFn),
        cssParser(cssParser),
        embeddedStyle(embeddedStyle),
        imageRendering(imageRendering),
        contentBase(contentBase),
        imageBasePath(imageBasePath),
        tocAnchors(std::move(tocAnchors)) {}

  ~ChapterHtmlSlimParser();

  // One-shot parse: builds every page before returning (begin + step* + finish).
  bool parseAndBuildPages();

  // Resumable parse, for the incremental section builder. Drive as:
  //   if (!beginParse()) fail;
  //   loop: switch (parseStep()) { More: keep going / yield; Done: finishParse(); Error: abortParse(); }
  // Pages are emitted via completePageFn as they complete during parseStep(), so
  // the caller can stop once enough pages are built and resume on a later tick.
  enum class ParseStatus { More, Done, Error };
  bool beginParse();

  // True once any page/text-block allocation failed. Expat callbacks cannot
  // return an error, so a failure sets this AND calls XML_StopParser -- which
  // stops further callbacks arriving, so the 46 unguarded `currentPage->` /
  // `currentTextBlock->` dereferences below stay unreachable rather than
  // needing 46 null checks. parseStep() turns it into ParseStatus::Error.
  bool allocationFailed() const { return allocFailed_; }
  ParseStatus parseStep();
  bool finishParse();  // flush the trailing page and tear down; returns true
  void abortParse();   // tear down without flushing (error / abandon)

  void addLineToPage(std::shared_ptr<TextBlock> line);
  const std::vector<std::pair<std::string, uint16_t>>& getAnchors() const { return anchorData; }

  // Byte progress of the in-flight parse, used to estimate a still-building section's total page
  // count (a giant single-spine book never fully lays out, so its real count is unknown). Valid
  // between beginParse() and finishParse()/abortParse().
  size_t parseBytesConsumed() { return parseFile_ ? parseFile_.position() : 0; }
  size_t parseTotalBytes() { return parseFile_ ? parseFile_.size() : 0; }
};
