// Knuth-Plass against the shipped breakers, on the owner's own books.
//
// Experiment E3 (crosspoint-simulator/docs/research-claude-for-kerning-and-
// layout-2026-09-24.md): build true total-fit breaking WITH hyphen points --
// the cell of docs/line-breaking-2026-08-25.md's 2x2 that no stored byte can
// reach -- and measure it against the shipped default (greedy first fit that
// hyphenates, `computeHyphenatedLineBreaks`). Verdict and tables:
// docs/knuth-plass-line-breaking-2026-09-25.md.
//
// A SEPARATE EXECUTABLE from LineBreakQualityTest on purpose. It needs the SD
// font stack (Albo is a .cpfont on the card, not a built-in face), which means
// the directory-capable HalStorage stub from test/sd_kern_measure; swapping
// that under the existing suite would change the environment its pinned
// numbers were measured in. Nothing here is compiled into the firmware.
//
// HOW WIDTHS STAY EXACT. Every arm -- the two real ParsedText breakers and the
// Knuth-Plass one -- is reduced to the same thing: a list of CUT positions
// (word, byte offset, hyphen) through the paragraph's words. One geometry
// model then turns cuts into lines the way ParsedText::extractLine does
// (measured piece advances, kerned natural gaps, the first-line indent, both
// hanging-punctuation edges, stretch-only justification with the integer
// remainder dropped). For the two REAL arms the cuts are read back off the
// TextBlocks the paginator bakes, and the model's word x positions are checked
// against those TextBlocks' own, word for word: ModelReproducesTheShipped-
// BreakersExactly pins it on the built-in paragraphs and the sweep counts any
// mismatch over the whole corpus. So a Knuth-Plass line is laid out by a model
// that has been shown to agree with the firmware to the pixel on every line
// the firmware itself produced.

#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <SdCardFont.h>
#include <SdCardFontManager.h>
#include <SdCardFontRegistry.h>
#include <Utf8.h>
#include <builtinFonts/all.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "Epub/AutoJustify.h"
#include "Epub/KnuthPlassBreaker.h"
#include "Epub/LineBreakMode.h"
#include "Epub/ParsedText.h"
#include "Epub/blocks/TextBlock.h"
#include "Epub/hyphenation/Hyphenator.h"
#include "KnuthPlass.h"
#include "fontIds.h"

HalDisplay display;

namespace {

// ---------------------------------------------------------------------------
// Renderer + faces
// ---------------------------------------------------------------------------

class Env {
 public:
  static Env& instance() {
    static Env e;
    return e;
  }
  GfxRenderer& renderer() { return renderer_; }

  // Returns a font id for the face, or 0 when it is not available. Albo is an
  // SD family and only one size of a family is resident at a time, so asking
  // for it unloads whatever was loaded before.
  int fontFor(const std::string& family, const int pt) {
    if (family == "LibreFranklin") {
      switch (pt) {
        case 12:
          return LIBREFRANKLIN_READER_12_FONT_ID;
        case 14:
          return LIBREFRANKLIN_READER_14_FONT_ID;
        case 18:
          return LIBREFRANKLIN_READER_18_FONT_ID;
        default:
          return 0;
      }
    }
    if (!sdDiscovered_) return 0;
    const SdCardFontFamilyInfo* fam = nullptr;
    for (const auto& f : registry_.getFamilies()) {
      if (f.name == family) fam = &f;
    }
    if (!fam) return 0;
    manager_.unloadAll(renderer_);
    if (!manager_.loadFamily(*fam, renderer_, static_cast<uint8_t>(pt))) return 0;
    return manager_.getFontId(family);
  }

 private:
  Env() : renderer_(display), cache_(renderer_.getFontMap(), renderer_.getSdCardFonts()) {
    renderer_.begin();
    if (!decompressor_.init()) ADD_FAILURE() << "font decompressor init failed";
    cache_.setFontDecompressor(&decompressor_);
    renderer_.setFontCacheManager(&cache_);
    renderer_.insertFont(LIBREFRANKLIN_READER_12_FONT_ID, lf12_);
    renderer_.insertFont(LIBREFRANKLIN_READER_14_FONT_ID, lf14_);
    renderer_.insertFont(LIBREFRANKLIN_READER_18_FONT_ID, lf18_);
    // Without this the greedy breaker cannot hyphenate at all (the trap
    // LineBreakQualityTest documents): the trie is installed only by this call.
    Hyphenator::setPreferredLanguage("en");
    sdDiscovered_ = registry_.discover() && registry_.getFamilyCount() > 0;
  }

  GfxRenderer renderer_;
  FontDecompressor decompressor_;
  FontCacheManager cache_;
  SdCardFontRegistry registry_;
  SdCardFontManager manager_;
  bool sdDiscovered_ = false;
  EpdFont lf12R_{&librefranklin_reader_12_regular}, lf12B_{&librefranklin_reader_12_bold},
      lf12I_{&librefranklin_reader_12_italic}, lf12BI_{&librefranklin_reader_12_bolditalic};
  EpdFontFamily lf12_{&lf12R_, &lf12B_, &lf12I_, &lf12BI_};
  EpdFont lf14R_{&librefranklin_reader_14_regular}, lf14B_{&librefranklin_reader_14_bold},
      lf14I_{&librefranklin_reader_14_italic}, lf14BI_{&librefranklin_reader_14_bolditalic};
  EpdFontFamily lf14_{&lf14R_, &lf14B_, &lf14I_, &lf14BI_};
  EpdFont lf18R_{&librefranklin_reader_18_regular}, lf18B_{&librefranklin_reader_18_bold},
      lf18I_{&librefranklin_reader_18_italic}, lf18BI_{&librefranklin_reader_18_bolditalic};
  EpdFontFamily lf18_{&lf18R_, &lf18B_, &lf18I_, &lf18BI_};
};

// The X3's portrait reading measure at the default screen margin, the same
// 512 px LineBreakQualityTest and docs/auto-justification.md use.
constexpr int kMeasure = 512;

// ---------------------------------------------------------------------------
// Text helpers -- mirrors of ParsedText's file-local ones
// ---------------------------------------------------------------------------

uint32_t firstCp(const std::string& w) {
  if (w.empty()) return 0;
  const auto* p = reinterpret_cast<const unsigned char*>(w.c_str());
  return utf8NextCodepoint(&p);
}

uint32_t lastCp(const std::string& w) {
  if (w.empty()) return 0;
  size_t i = w.size() - 1;
  while (i > 0 && (static_cast<uint8_t>(w[i]) & 0xC0) == 0x80) --i;
  const auto* p = reinterpret_cast<const unsigned char*>(w.c_str() + i);
  return utf8NextCodepoint(&p);
}

bool startsWithForbiddenDash(const std::string& w) {
  const uint32_t cp = firstCp(w);
  return cp == 0x2013 || cp == 0x2014 || cp == 0x2015;
}

// COPY of ParsedText.cpp's HANG_FRACTIONS (file-local there). Validated rather
// than trusted: a drift would show up as an x-position mismatch in the exact-
// geometry test, because the real breaker's lines carry the real hang.
uint8_t hangQuarters(const uint32_t cp, const bool leading) {
  struct H {
    uint32_t cp;
    uint8_t t, l;
  };
  static constexpr H table[] = {
      {'.', 4, 4},    {',', 4, 4},    {';', 2, 0},    {':', 2, 0},    {'!', 2, 0},    {'?', 2, 0},
      {'-', 2, 2},    {'\'', 2, 2},   {'"', 2, 2},    {0x2010, 2, 2}, {0x2018, 2, 2}, {0x2019, 2, 2},
      {0x201C, 2, 2}, {0x201D, 2, 2}, {0x201A, 0, 2}, {0x201E, 0, 2}, {0x00AB, 0, 2}, {0x2039, 0, 2},
      {0x2013, 0, 1}, {0x2014, 0, 1}, {0x2015, 0, 1}, {'(', 0, 1},    {'[', 0, 1},    {'{', 0, 1},
  };
  for (const auto& h : table)
    if (h.cp == cp) return leading ? h.l : h.t;
  return 0;
}

int hangOf(const GfxRenderer& r, const int fontId, const std::string& glyph, const bool leading) {
  const uint8_t q = hangQuarters(firstCp(glyph), leading);
  if (q == 0) return 0;
  return r.getTextAdvanceX(fontId, glyph.c_str(), EpdFontFamily::REGULAR) * q / 4;
}

std::string lastGlyph(const std::string& w) {
  size_t i = w.size() - 1;
  while (i > 0 && (static_cast<uint8_t>(w[i]) & 0xC0) == 0x80) --i;
  return w.substr(i);
}

std::string firstGlyph(const std::string& w) {
  size_t len = 1;
  while (len < w.size() && (static_cast<uint8_t>(w[len]) & 0xC0) == 0x80) ++len;
  return w.substr(0, len);
}

std::vector<std::string> wordsOf(const std::string& text) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && text[i] == ' ') i++;
    const size_t s = i;
    while (i < text.size() && text[i] != ' ') i++;
    if (i > s) out.push_back(utf8ComposeNfc(text.substr(s, i - s)));  // addWord's own first step
  }
  return out;
}

// ---------------------------------------------------------------------------
// A paragraph, measured once
// ---------------------------------------------------------------------------

struct Prepared {
  int fontId = 0;
  std::vector<std::string> words;
  kp::Paragraph para;
  std::shared_ptr<std::map<std::tuple<int, int, int, bool>, int>> cache;
  int pieceCalls = 0;  // advance measurements made to set up the DP (the device's extra work)
};

void ensureMetrics(const int fontId, const std::vector<std::string>& words) {
  auto& r = Env::instance().renderer();
  if (!r.isSdCardFont(fontId)) return;
  std::deque<std::string> d(words.begin(), words.end());
  r.ensureSdCardFontReady(fontId, d, /*includeHyphen=*/true, /*styleMask=*/0x01);
}

Prepared prepare(const std::vector<std::string>& words, const int fontId, const bool withHyphenPoints) {
  auto& r = Env::instance().renderer();
  Prepared P;
  P.fontId = fontId;
  P.words = words;
  ensureMetrics(fontId, words);
  kp::Paragraph& p = P.para;
  p.words = static_cast<int>(words.size());
  p.measurePx = kMeasure;
  p.firstLineIndentPx = r.getSpaceWidth(fontId, EpdFontFamily::REGULAR) * 3;  // resolveFirstLineIndent
  P.cache = std::make_shared<std::map<std::tuple<int, int, int, bool>, int>>();
  auto cache = P.cache;
  // Captured by shared_ptr, never by &P.words: a Prepared is moved into
  // containers, and a lambda holding the old address reads freed memory.
  const auto wordsPtr = std::make_shared<const std::vector<std::string>>(words);
  p.piece = [cache, wordsPtr, fontId](const int k, const int from, const int to, const bool hy) {
    const auto key = std::make_tuple(k, from, to, hy);
    const auto it = cache->find(key);
    if (it != cache->end()) return it->second;
    const std::string& w = (*wordsPtr)[k];
    std::string s = w.substr(from, to < 0 ? std::string::npos : static_cast<size_t>(to - from));
    if (hy) s.push_back('-');
    const int v = Env::instance().renderer().getTextAdvanceX(fontId, s.c_str(), EpdFontFamily::REGULAR);
    (*cache)[key] = v;
    return v;
  };
  for (int k = 0; k < p.words; ++k) {
    p.full.push_back(r.getTextAdvanceX(fontId, words[k].c_str(), EpdFontFamily::REGULAR));
    p.gapAfter.push_back(k + 1 < p.words ? r.getSpaceAdvance(fontId, lastCp(words[k]), firstCp(words[k + 1]),
                                                             EpdFontFamily::REGULAR)
                                         : 0);
  }
  p.positions.push_back({0, 0, false, false});
  for (int k = 0; k < p.words; ++k) {
    if (withHyphenPoints || p.full[k] > kMeasure) {
      auto infos = Hyphenator::breakOffsets(words[k], /*includeFallback=*/p.full[k] > kMeasure);
      std::sort(infos.begin(), infos.end(),
                [](const auto& a, const auto& b) { return a.byteOffset < b.byteOffset; });
      for (const auto& bi : infos) {
        if (bi.byteOffset == 0 || bi.byteOffset >= words[k].size()) continue;
        if (!p.positions.empty() && p.positions.back().word == k &&
            p.positions.back().offset == static_cast<int>(bi.byteOffset))
          continue;
        p.positions.push_back({k, static_cast<int>(bi.byteOffset), bi.requiresInsertedHyphen, true});
        // Pre-measure the two pieces every candidate implies, so the DP's
        // timing below is the DP and not the glyph lookups (counted apart).
        p.piece(k, 0, static_cast<int>(bi.byteOffset), bi.requiresInsertedHyphen);
        p.piece(k, static_cast<int>(bi.byteOffset), -1, false);
        P.pieceCalls += 2;
      }
    }
    if (k + 1 < p.words && !startsWithForbiddenDash(words[k + 1])) p.positions.push_back({k + 1, 0, false, false});
  }
  p.positions.push_back({p.words, 0, false, false});
  return P;
}

// ---------------------------------------------------------------------------
// Cuts -> lines, the extractLine way
// ---------------------------------------------------------------------------

struct Line {
  std::vector<std::string> texts;
  std::vector<int16_t> xpos;
  std::vector<int> widths;
  std::vector<float> gapStart, gapEnd;  // measurable gaps (>= 1 px), px
  double meanGap = 0.0;
  int gapCount = 0;
  int end = 0;
  int natural = 0;  // natural width incl. gaps, before stretch
  int avail = 0;    // the measure less the indent
  int trailHang = 0;
  std::vector<int> naturalGaps, paintedGaps;  // every gap, natural and as painted
  bool hyphenated = false;
  bool isFinal = false;
};

// `cuts` = [start, line ends...]; the last is the paragraph end.
// `allowShrink` models an extractLine that can also NARROW gaps (a negative
// justifyExtra). The firmware cannot today -- computeJustifyExtra returns 0 on
// a negative spare -- so only the "+shrink" arms pass it, and without it a line
// set tight by the breaker would be painted overflowing the measure.
std::vector<Line> layOut(const Prepared& P, const std::vector<kp::Pos>& cuts, const bool justified,
                         const bool allowShrink = false) {
  auto& r = Env::instance().renderer();
  const int fontId = P.fontId;
  const int budget = std::max(0, (r.getScreenWidth() - kMeasure) / 2);
  std::vector<Line> out;
  for (size_t l = 0; l + 1 < cuts.size(); ++l) {
    const kp::Pos& a = cuts[l];
    const kp::Pos& b = cuts[l + 1];
    const int lastW = b.offset > 0 ? b.word : b.word - 1;
    Line L;
    L.isFinal = l + 2 == cuts.size();
    std::vector<int> gaps;
    int sumW = 0, sumG = 0;
    for (int k = a.word; k <= lastW; ++k) {
      const int from = k == a.word ? a.offset : 0;
      const bool cutHere = k == lastW && b.offset > 0;
      const int to = cutHere ? b.offset : -1;
      const bool hy = cutHere && b.hyphen;
      std::string s = P.words[k].substr(from, to < 0 ? std::string::npos : static_cast<size_t>(to - from));
      if (hy) s.push_back('-');
      const int w = r.getTextAdvanceX(fontId, s.c_str(), EpdFontFamily::REGULAR);
      L.texts.push_back(s);
      L.widths.push_back(w);
      sumW += w;
      if (k < lastW) {
        gaps.push_back(P.para.gapAfter[k]);
        sumG += P.para.gapAfter[k];
      }
    }
    const int indent = l == 0 ? P.para.firstLineIndentPx : 0;
    const int effW = kMeasure - indent;
    const bool stretch = justified && !L.isFinal;
    const int leadHang = std::min(budget, hangOf(r, fontId, firstGlyph(L.texts.front()), true));
    const int trailHang = stretch ? hangOf(r, fontId, lastGlyph(L.texts.back()), false) : 0;
    const int spare = effW - sumW - sumG + trailHang + leadHang;
    int extra = (stretch && !gaps.empty() && spare > 0) ? spare / static_cast<int>(gaps.size()) : 0;
    if (allowShrink && stretch && !gaps.empty() && spare < 0) {
      const int n = static_cast<int>(gaps.size());
      // round AWAY from zero so the line fits, capped at ceil(space / 3) per
      // gap -- ParsedText.cpp's computeJustifyExtra, since 2026-09-26.
      const int cap = kpbreak::shrinkPerGapPx(r.getSpaceAdvance(fontId, 'n', 'n', EpdFontFamily::REGULAR));
      extra = -std::min((-spare + n - 1) / n, cap);
    }
    L.avail = effW;
    L.trailHang = trailHang;
    int x = indent - leadHang;
    double gapSum = 0.0;
    for (size_t i = 0; i < L.texts.size(); ++i) {
      L.xpos.push_back(static_cast<int16_t>(x));
      if (i + 1 < L.texts.size()) {
        const int g = gaps[i] + extra;
        L.naturalGaps.push_back(gaps[i]);
        L.paintedGaps.push_back(g);
        if (g >= 1) {
          L.gapStart.push_back(static_cast<float>(x + L.widths[i]));
          L.gapEnd.push_back(static_cast<float>(x + L.widths[i] + g));
          gapSum += g;
          L.gapCount++;
        }
        x += L.widths[i] + g;
      }
    }
    if (L.gapCount > 0) L.meanGap = gapSum / L.gapCount;
    L.end = L.xpos.back() + L.widths.back();
    L.natural = sumW + sumG;
    L.hyphenated = !L.texts.back().empty() && L.texts.back().back() == '-';
    out.push_back(std::move(L));
  }
  return out;
}

// ---------------------------------------------------------------------------
// The three arms
// ---------------------------------------------------------------------------

struct RealRun {
  std::vector<std::shared_ptr<TextBlock>> blocks;
  double micros = 0.0;
};

// `knuthPlass` false (every instrument below) runs the GREEDY breaker the
// arms are labeled with: since 2026-09-26 a justified hyphenating block goes
// to the device's Knuth-Plass breaker, and "greedy+hy (shipped)" would
// otherwise silently measure it. The device-port tests pass true.
RealRun runReal(const std::vector<std::string>& words, const int fontId, const uint8_t storedMode,
                const bool justified, const bool knuthPlass = false) {
  kpbreak::tuning().enabled = knuthPlass;
  BlockStyle style;
  style.alignment = justified ? CssTextAlign::Justify : CssTextAlign::Left;
  ParsedText block(/*extraParagraphSpacing=*/false, storedMode, /*focusReadingEnabled=*/false, style);
  for (const auto& w : words) block.addWord(w, EpdFontFamily::REGULAR);
  RealRun out;
  const auto t0 = std::chrono::steady_clock::now();
  // Threshold 0 forces a justified block to stay justified, so the justified
  // arm is justified at every face; the default threshold's own verdict per
  // face is printed beside the table instead.
  block.layoutAndExtractLines(
      Env::instance().renderer(), fontId, kMeasure,
      [&](const std::shared_ptr<TextBlock>& line) { out.blocks.push_back(line); }, /*includeLastLine=*/true,
      /*justifyThresholdChars=*/0);
  out.micros = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
  kpbreak::tuning().enabled = true;
  return out;
}

// Read the cut positions back off the real breaker's lines. Returns false if a
// line's text cannot be matched to the source words (never seen; counted).
bool cutsFromBlocks(const std::vector<std::string>& words, const std::vector<std::shared_ptr<TextBlock>>& blocks,
                    std::vector<kp::Pos>& cuts) {
  cuts.clear();
  cuts.push_back({0, 0, false, false});
  int k = 0, o = 0;
  for (const auto& b : blocks) {
    kp::Pos endPos;
    for (uint16_t i = 0; i < b->wordCount(); ++i) {
      if (k >= static_cast<int>(words.size())) return false;
      const std::string tok = b->wordText(i);
      const std::string rest = words[k].substr(o);
      if (tok == rest) {
        k++;
        o = 0;
        endPos = {k, 0, false, false};
        continue;
      }
      if (i + 1 != b->wordCount()) return false;  // only a line's LAST token may be a prefix
      if (rest.compare(0, tok.size(), tok) == 0) {
        o += static_cast<int>(tok.size());  // an explicit hyphen already in the word
        endPos = {k, o, false, true};
      } else if (!tok.empty() && tok.back() == '-' && rest.compare(0, tok.size() - 1, tok, 0, tok.size() - 1) == 0) {
        o += static_cast<int>(tok.size()) - 1;
        endPos = {k, o, true, true};
      } else {
        return false;
      }
    }
    cuts.push_back(endPos);
  }
  return k == static_cast<int>(words.size()) && o == 0;
}

// Lines whose model x positions differ from the real TextBlock's by any pixel.
int mismatchedLines(const std::vector<Line>& model, const std::vector<std::shared_ptr<TextBlock>>& blocks) {
  int bad = 0;
  for (size_t l = 0; l < std::max(model.size(), blocks.size()); ++l) {
    if (l >= model.size() || l >= blocks.size() || model[l].texts.size() != blocks[l]->wordCount()) {
      bad++;
      continue;
    }
    for (uint16_t i = 0; i < blocks[l]->wordCount(); ++i) {
      if (model[l].xpos[i] != blocks[l]->wordXpos(i) || model[l].texts[i] != blocks[l]->wordText(i)) {
        bad++;
        break;
      }
    }
  }
  return bad;
}

std::vector<kp::Pos> kpCuts(const Prepared& P, const kp::Params& prm, kp::Stats* st, double* micros) {
  const auto t0 = std::chrono::steady_clock::now();
  const std::vector<int> ends = kp::breakParagraph(P.para, prm, st);
  if (micros) *micros = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
  std::vector<kp::Pos> cuts = {P.para.positions.front()};
  for (const int e : ends) cuts.push_back(P.para.positions[e]);
  return cuts;
}

// TeX's Computer Modern ratios: a word space stretches by half itself and
// shrinks by a third.
kp::Params paramsFor(const int fontId, const bool justified, const bool shrink) {
  auto& r = Env::instance().renderer();
  const double space = r.getSpaceAdvance(fontId, 'n', 'n', EpdFontFamily::REGULAR);
  kp::Params prm;
  prm.justified = justified;
  prm.stretchPerGapPx = space * 0.5;
  prm.shrinkPerGapPx = shrink ? space / 3.0 : 0.0;
  prm.raggedStretchPx = space * 6.0;  // ~2 em of right-edge glue, TeX's \raggedright
  return prm;
}

// THE CANDIDATE, chosen by DISABLED_HyphenPenaltySweep: uncapped badness, a
// hard limit of two hyphenated lines in a row, and a hyphen penalty high
// enough that the breaker sets no more hyphens than the shipped greedy does on
// either measured face. See the doc's section 4 for the grid it came from.
kp::Params candidateParams(const int fontId, const bool justified, const bool shrink) {
  kp::Params prm = paramsFor(fontId, justified, shrink);
  prm.hyphenPenalty = 10000.0;
  prm.doubleHyphenDemerits = 1000000.0;
  prm.maxConsecutiveHyphens = 2;
  return prm;
}

// What SHIPPED with "Just ship shrink" (2026-09-26): the candidate plus shrink
// of floor(space / 3) px per gap -- TeX's third, made pixel-exact so no gap is
// painted under 2/3 of a space (KnuthPlassBreaker.h). Identical to
// candidateParams(.., true) wherever the space is a multiple of 3 px, which it
// is for both faces at 14 pt.
kp::Params shippedParams(const int fontId) {
  kp::Params prm = candidateParams(fontId, true, true);
  prm.shrinkPerGapPx =
      kpbreak::shrinkPerGapPx(Env::instance().renderer().getSpaceAdvance(fontId, 'n', 'n', EpdFontFamily::REGULAR));
  return prm;
}

// ---------------------------------------------------------------------------
// Metrics (definitions as LineBreakQualityTest's, so the two docs share units)
// ---------------------------------------------------------------------------

double pct(std::vector<double> v, const double q) {
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  const size_t idx = static_cast<size_t>(std::ceil(q * v.size()));
  return v[std::min(v.size(), std::max<size_t>(1, idx)) - 1];
}

struct Summary {
  int lines = 0, paras = 0;
  // Justified: per-line gap as a multiple of the font's word space.
  double mean = 0, p50 = 0, p95 = 0, p99 = 0, max = 0, paraWorst = 0, minGap = 1e9;
  double dev[5] = {0, 0, 0, 0, 0};  // |g/s - 1| in [0,.25) [.25,.5) [.5,1) [1,2) [2,inf), % of lines
  int shrunk = 0;
  // Ragged: slack at the right edge, in word spaces.
  double slackMean = 0, slackSd = 0, slackP95 = 0, slackParaWorst = 0;
  // Hyphens.
  int hyphenated = 0, runs2 = 0, ladders = 0, longest = 0;
  // Rivers: overlap >= half a space, >= 3 lines, per 1000 gaps.
  int rivers = 0, gaps = 0;
  // Non-final lines with NO gap that stop short of the measure: a justified
  // page cannot stretch them, so they paint left-aligned. The gap statistics
  // above cannot see them (no gap to measure), which would let a breaker hide
  // a bad line in one; counted here so it cannot.
  int unjustifiable = 0;
};

Summary summarize(const std::vector<std::vector<Line>>& corpus, const double s, const bool justified) {
  Summary S;
  std::vector<double> mult, slack;
  double paraWorstSum = 0, slackWorstSum = 0;
  int worstParas = 0;
  for (const auto& para : corpus) {
    S.paras++;
    double worst = -1, sworst = -1;
    int run = 0;
    std::vector<int> prevLen;
    const Line* prevLine = nullptr;
    for (const auto& L : para) {
      S.lines++;
      if (L.isFinal) continue;
      // hyphen runs
      if (L.hyphenated) {
        S.hyphenated++;
        run++;
        S.longest = std::max(S.longest, run);
      } else {
        if (run == 2) S.runs2++;
        if (run >= 3) S.ladders++;
        run = 0;
      }
      if (justified && L.gapCount == 0 && L.end < kMeasure - 1) S.unjustifiable++;
      if (justified) {
        if (L.gapCount > 0) {
          const double g = L.meanGap / s;
          mult.push_back(g);
          worst = std::max(worst, g);
          S.minGap = std::min(S.minGap, g);
          const double d = std::abs(g - 1.0);
          S.dev[d < 0.25 ? 0 : d < 0.5 ? 1 : d < 1 ? 2 : d < 2 ? 3 : 4] += 1;
        }
        if (L.natural > kMeasure) S.shrunk++;
      } else {
        const double sl = (kMeasure - L.end) / s;
        slack.push_back(sl);
        sworst = std::max(sworst, sl);
      }
      // rivers (overlap linkage, as LineBreakQualityTest::riversByOverlap at 0.5 space)
      std::vector<int> cur(L.gapStart.size(), 1);
      S.gaps += static_cast<int>(L.gapStart.size());
      std::vector<bool> ext(prevLen.size(), false);
      for (size_t g = 0; g < L.gapStart.size(); ++g) {
        for (size_t q = 0; q < prevLen.size(); ++q) {
          const double lo = std::max(prevLine->gapStart[q], L.gapStart[g]);
          const double hi = std::min(prevLine->gapEnd[q], L.gapEnd[g]);
          if (hi - lo < 0.5 * s) continue;
          ext[q] = true;
          cur[g] = std::max(cur[g], prevLen[q] + 1);
        }
      }
      for (size_t q = 0; q < prevLen.size(); ++q)
        if (!ext[q] && prevLen[q] >= 3) S.rivers++;
      prevLen = cur;
      prevLine = &L;
    }
    for (const int len : prevLen)
      if (len >= 3) S.rivers++;
    if (run == 2) S.runs2++;
    if (run >= 3) S.ladders++;
    if (worst >= 0 || sworst >= 0) {
      paraWorstSum += worst;
      slackWorstSum += sworst;
      worstParas++;
    }
  }
  if (!mult.empty()) {
    for (const double g : mult) S.mean += g;
    S.mean /= mult.size();
    S.p50 = pct(mult, 0.5);
    S.p95 = pct(mult, 0.95);
    S.p99 = pct(mult, 0.99);
    S.max = *std::max_element(mult.begin(), mult.end());
    for (double& d : S.dev) d = 100.0 * d / mult.size();
  }
  if (!slack.empty()) {
    for (const double v : slack) S.slackMean += v;
    S.slackMean /= slack.size();
    for (const double v : slack) S.slackSd += (v - S.slackMean) * (v - S.slackMean);
    S.slackSd = std::sqrt(S.slackSd / slack.size());
    S.slackP95 = pct(slack, 0.95);
  }
  if (worstParas > 0) {
    S.paraWorst = paraWorstSum / worstParas;
    S.slackParaWorst = slackWorstSum / worstParas;
  }
  return S;
}

const std::vector<std::string>& builtinParagraphs() {
  // Same public-card text LineBreakQualityTest uses (wingspan-the-whole-bird).
  static const std::vector<std::string> p = {
      "To play a bird you pay its cost - some food, shown on the card - and put it in a row. Now the clever bit: a "
      "bird sitting in a row makes that row better. An empty forest gets you one food. A forest with four birds in it "
      "gets you three food, plus each of those birds may do its own little trick. So the more birds you have in a "
      "row, the more a single turn produces.",
      "The reason for the shrinking is worth knowing, because it makes the number stick. You have eight little wooden "
      "cubes. Each turn you take, you spend one - but you get them all back at the end of the round, except one: at "
      "the end of each round you permanently park one cube on the scoring board to mark how you did on that round's "
      "goal. It never comes back. Eight cubes, then seven, then six, then five.",
      "A useful mental model for a first few games. Your bird values and your eggs will usually be the two biggest "
      "columns on the scorepad. Goals are the swingiest - three contested goals can easily be a twelve-point spread "
      "between first and third. Cached food and tucked cards are the quiet accumulators: individually tiny, but a "
      "bird that tucks a card every time you use a row will out-earn its printed value several times over.",
      "In Wingspan, the birds on your board are the feeders. Early on, one bird gets you almost nothing. By the end, "
      "taking a single action can set off a chain of five birds in a row, each one handing you something. That chain "
      "is your engine. Building an engine means spending your early turns on things that pay you back later, instead "
      "of on things that score points right now.",
  };
  return p;
}

std::vector<std::string> loadCorpus() {
  std::vector<std::string> out;
  const char* path = std::getenv("CROSSPOINT_LINEBREAK_CORPUS");
  if (!path) return out;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line))
    if (line.size() >= 40) out.push_back(line);
  return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Live tests: the model is exact, and the breaker is sane
// ---------------------------------------------------------------------------

// THE PRECONDITION FOR EVERY NUMBER IN THE DOC. If the geometry model did not
// reproduce the real breakers' own lines to the pixel, a Knuth-Plass line laid
// out by it would be measured in different units from the greedy line it is
// compared with.
TEST(KnuthPlass, ModelReproducesTheShippedBreakersExactly) {
  const int fontId = Env::instance().fontFor("LibreFranklin", 14);
  ASSERT_NE(fontId, 0);
  int lines = 0, hyphenated = 0;
  for (const bool justified : {true, false}) {
    for (const uint8_t mode : {linebreak::STORED_HYPHENATED, linebreak::STORED_WHOLE_WORDS}) {
      // Both the greedy breaker and, since 2026-09-26, the device Knuth-Plass
      // one that replaced it on justified blocks.
      for (const bool knuthPlass : {false, true}) {
      for (const auto& text : builtinParagraphs()) {
        const auto words = wordsOf(text);
        const RealRun real = runReal(words, fontId, mode, justified, knuthPlass);
        std::vector<kp::Pos> cuts;
        ASSERT_TRUE(cutsFromBlocks(words, real.blocks, cuts));
        const Prepared P = prepare(words, fontId, true);
        const auto model = layOut(P, cuts, justified, /*allowShrink=*/knuthPlass);
        EXPECT_EQ(mismatchedLines(model, real.blocks), 0) << text.substr(0, 40);
        lines += static_cast<int>(model.size());
        for (const auto& L : model) hyphenated += L.hyphenated;
      }
      }
    }
  }
  EXPECT_GT(lines, 40);
  // Not vacuous: the hyphenated arm must actually split words here, or the
  // prefix/hyphen half of the model is untested.
  EXPECT_GT(hyphenated, 0);
}

TEST(KnuthPlass, EveryWordIsSetOnceAndNoLineOverflows) {
  const int fontId = Env::instance().fontFor("LibreFranklin", 14);
  ASSERT_NE(fontId, 0);
  for (const bool justified : {true, false}) {
    const kp::Params prm = paramsFor(fontId, justified, false);
    for (const auto& text : builtinParagraphs()) {
      const auto words = wordsOf(text);
      const Prepared P = prepare(words, fontId, true);
      const auto cuts = kpCuts(P, prm, nullptr, nullptr);
      const auto lines = layOut(P, cuts, justified);
      ASSERT_EQ(lines.size() + 1, cuts.size());
      std::string rebuilt;
      for (size_t l = 0; l < lines.size(); ++l) {
        const auto& L = lines[l];
        EXPECT_LE(L.natural, kMeasure - (l == 0 ? P.para.firstLineIndentPx : 0)) << "line " << l << " overflows";
        const kp::Pos& b = cuts[l + 1];
        for (size_t t = 0; t < L.texts.size(); ++t) {
          const bool split = t + 1 == L.texts.size() && b.offset > 0;
          std::string piece = L.texts[t];
          if (split && b.hyphen) piece.pop_back();  // the inserted '-' is not source text
          rebuilt += piece;
          if (!split) rebuilt += ' ';
        }
      }
      std::string expect;
      for (const auto& w : words) expect += w + " ";
      EXPECT_EQ(rebuilt, expect) << text.substr(0, 40);
    }
  }
}

// ---------------------------------------------------------------------------
// The instrument
// ---------------------------------------------------------------------------

// CROSSPOINT_LINEBREAK_CORPUS=/path/corpus.txt (tools/linebreak_corpus.py)
// CROSSPOINT_KP_FACES="Albo:14,LibreFranklin:14,..."  (default below)
TEST(KnuthPlass, DISABLED_Sweep) {
  const auto corpus = loadCorpus();
  if (corpus.empty()) GTEST_SKIP() << "set CROSSPOINT_LINEBREAK_CORPUS";
  std::vector<std::pair<std::string, int>> faces = {{"LibreFranklin", 14}, {"Albo", 14}, {"LibreFranklin", 12},
                                                    {"Albo", 12},          {"LibreFranklin", 18}, {"Albo", 18}};
  if (const char* f = std::getenv("CROSSPOINT_KP_FACES")) {
    faces.clear();
    std::string s = f;
    size_t i = 0;
    while (i < s.size()) {
      size_t j = s.find(',', i);
      if (j == std::string::npos) j = s.size();
      const std::string item = s.substr(i, j - i);
      const size_t c = item.find(':');
      faces.push_back({item.substr(0, c), std::atoi(item.c_str() + c + 1)});
      i = j + 1;
    }
  }
  printf("corpus: %zu paragraphs\n", corpus.size());
  for (const auto& [family, pt] : faces) {
    const int fontId = Env::instance().fontFor(family, pt);
    if (fontId == 0) {
      printf("\n== %s %d pt: NOT AVAILABLE\n", family.c_str(), pt);
      continue;
    }
    auto& r = Env::instance().renderer();
    const double s = r.getSpaceAdvance(fontId, 'n', 'n', EpdFontFamily::REGULAR);
    const int alphabet = r.getTextAdvanceX(fontId, "abcdefghijklmnopqrstuvwxyz", EpdFontFamily::REGULAR);
    printf("\n== %s %d pt @ %d px: word space %.0f px, ~%.1f chars/line\n", family.c_str(), pt, kMeasure, s,
           kMeasure * 28.1 / alphabet);
    for (const bool justified : {true, false}) {
      const char* arms[] = {"greedy+hy (shipped)", "totalfit whole words", "KP TeX defaults", "KP candidate",
                            "KP candidate+shrink"};
      const int nArms = justified ? 5 : 4;
      std::vector<std::vector<std::vector<Line>>> results(nArms);
      int mismatch = 0, unmatched = 0, realLines = 0;
      double tGreedy = 0, tDp = 0, tKp = 0, tKpMax = 0, tPrep = 0;
      long long pieceCalls = 0, evals = 0, posSum = 0;
      int maxPos = 0;
      size_t maxTable = 0;
      std::vector<std::pair<double, int>> gainByPara;  // (greedy paraWorst - KP paraWorst, index)
      for (size_t pi = 0; pi < corpus.size(); ++pi) {
        const auto words = wordsOf(corpus[pi]);
        for (int arm = 0; arm < 2; ++arm) {
          const RealRun real = runReal(words, fontId,
                                       arm == 0 ? linebreak::STORED_HYPHENATED : linebreak::STORED_WHOLE_WORDS,
                                       justified);
          (arm == 0 ? tGreedy : tDp) += real.micros;
          std::vector<kp::Pos> cuts;
          if (!cutsFromBlocks(words, real.blocks, cuts)) {
            unmatched++;
            results[arm].emplace_back();
            continue;
          }
          const Prepared P = prepare(words, fontId, true);
          auto lines = layOut(P, cuts, justified);
          mismatch += mismatchedLines(lines, real.blocks);
          realLines += static_cast<int>(lines.size());
          results[arm].push_back(std::move(lines));
        }
        const auto t0 = std::chrono::steady_clock::now();
        const Prepared P = prepare(words, fontId, true);
        tPrep += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        pieceCalls += P.pieceCalls;
        for (int arm = 2; arm < nArms; ++arm) {
          kp::Stats st;
          double us = 0;
          const auto cuts = kpCuts(P, arm == 2 ? paramsFor(fontId, justified, false)
                                               : candidateParams(fontId, justified, arm == 4),
                                   &st, &us);
          if (arm == 3) {
            posSum += st.positions;
            tKp += us;
            tKpMax = std::max(tKpMax, us);
            evals += st.evaluations;
            maxPos = std::max(maxPos, st.positions);
            maxTable = std::max(maxTable, st.tableBytes);
          }
          results[arm].push_back(layOut(P, cuts, justified, /*allowShrink=*/arm == 4));
        }
        if (justified) {
          double gw = 0, kw = 0;
          for (const auto& L : results[0].back())
            if (!L.isFinal && L.gapCount) gw = std::max(gw, L.meanGap / s);
          for (const auto& L : results[3].back())
            if (!L.isFinal && L.gapCount) kw = std::max(kw, L.meanGap / s);
          if (results[3].back().size() <= 11) gainByPara.push_back({gw - kw, static_cast<int>(pi)});
        }
      }
      printf("\n  %s  (model vs firmware: %d of %d real lines differ, %d paragraphs unmatched)\n",
             justified ? "JUSTIFIED" : "RAGGED", mismatch, realLines, unmatched);
      if (justified) {
        printf("  %-22s %6s %5s %5s %5s %5s %6s %6s | %5s %5s %5s %5s %5s | %5s %4s %4s %3s | %6s %5s %6s %5s\n", "arm",
               "lines", "mean", "p50", "p95", "p99", "max", "pWorst", "<.25", "<.5", "<1", "<2", ">=2", "hyph",
               "r2", "lad", "lng", "riv/kg", "min", "shrunk", "1word");
      } else {
        printf("  %-22s %6s %6s %6s %6s %7s | %5s %4s %4s %3s\n", "arm", "lines", "slack", "sd", "p95", "pWorst",
               "hyph", "r2", "lad", "lng");
      }
      for (int arm = 0; arm < nArms; ++arm) {
        const Summary S = summarize(results[arm], s, justified);
        if (justified) {
          printf("  %-22s %6d %5.2f %5.2f %5.2f %5.2f %6.2f %6.2f | %5.1f %5.1f %5.1f %5.1f %5.1f | %5d %4d %4d %3d | "
                 "%6.2f %5.2f %6d %5d\n",
                 arms[arm], S.lines, S.mean, S.p50, S.p95, S.p99, S.max, S.paraWorst, S.dev[0], S.dev[1], S.dev[2],
                 S.dev[3], S.dev[4], S.hyphenated, S.runs2, S.ladders, S.longest,
                 S.gaps ? 1000.0 * S.rivers / S.gaps : 0.0, S.minGap, S.shrunk, S.unjustifiable);
        } else {
          printf("  %-22s %6d %6.2f %6.2f %6.2f %7.2f | %5d %4d %4d %3d\n", arms[arm], S.lines, S.slackMean,
                 S.slackSd, S.slackP95, S.slackParaWorst, S.hyphenated, S.runs2, S.ladders, S.longest);
        }
      }
      const double n = static_cast<double>(corpus.size());
      printf("  cost/paragraph (host, -O2): greedy layout %.1f us, whole-words DP layout %.1f us, KP measure-setup "
             "%.1f us (%.1f extra advance lookups), KP candidate DP %.1f us (max %.0f us), %.0f line evaluations; "
             "positions mean %.0f max %d, max host table %zu B\n",
             tGreedy / n, tDp / n, tPrep / n, pieceCalls / n, tKp / n, tKpMax, evals / n, posSum / n, maxPos,
             maxTable);
      if (justified && !gainByPara.empty()) {
        std::sort(gainByPara.begin(), gainByPara.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        int better = 0, worse = 0;
        for (const auto& g : gainByPara) better += g.first > 0.001, worse += g.first < -0.001;
        printf("  per paragraph (<=11 lines): KP worst line better in %d, worse in %d, of %zu. largest gains:",
               better, worse, gainByPara.size());
        for (size_t i = 0; i < std::min<size_t>(6, gainByPara.size()); ++i)
          printf(" #%d(%.2f)", gainByPara[i].second, gainByPara[i].first);
        printf("\n  median-gain paragraph: #%d(%.2f)\n", gainByPara[gainByPara.size() / 2].second,
               gainByPara[gainByPara.size() / 2].first);
      }
    }
  }
}

// The hyphen knobs, swept. TeX's defaults (hyphenpenalty 50, doublehyphen-
// demerits 10000) were tuned against a badness CAPPED at 10000; with the
// uncapped cube here a hyphen is cheap by comparison and the first sweep set
// 28% more hyphens and twice the ladders of the shipped greedy. This grid is
// how the shipped-candidate values in the doc were chosen.
// CROSSPOINT_KP_FACES as above (first face only), CROSSPOINT_KP_MODE=J|R.
TEST(KnuthPlass, DISABLED_HyphenPenaltySweep) {
  const auto corpus = loadCorpus();
  if (corpus.empty()) GTEST_SKIP() << "set CROSSPOINT_LINEBREAK_CORPUS";
  std::string family = "Albo";
  int pt = 14;
  if (const char* f = std::getenv("CROSSPOINT_KP_FACES")) {
    const std::string item = f;
    const size_t c = item.find(':');
    family = item.substr(0, c);
    pt = std::atoi(item.c_str() + c + 1);
  }
  const char* m = std::getenv("CROSSPOINT_KP_MODE");
  const bool justified = !(m && m[0] == 'R');
  const int fontId = Env::instance().fontFor(family, pt);
  ASSERT_NE(fontId, 0);
  const double s = Env::instance().renderer().getSpaceAdvance(fontId, 'n', 'n', EpdFontFamily::REGULAR);
  std::vector<Prepared> prepared;
  std::vector<std::vector<Line>> greedy;
  for (const auto& text : corpus) {
    const auto words = wordsOf(text);
    const RealRun real = runReal(words, fontId, linebreak::STORED_HYPHENATED, justified);
    std::vector<kp::Pos> cuts;
    ASSERT_TRUE(cutsFromBlocks(words, real.blocks, cuts));
    prepared.push_back(prepare(words, fontId, true));
    greedy.push_back(layOut(prepared.back(), cuts, justified));
  }
  const Summary G = summarize(greedy, s, justified);
  printf("%s %d pt %s, %zu paragraphs\n", family.c_str(), pt, justified ? "JUSTIFIED" : "RAGGED", corpus.size());
  printf("  %-32s %6s %6s %6s %6s %6s %5s %4s %4s\n", "arm", "lines", "p95", "p99", "max", "pWorst", "hyph", "r2",
         "lad");
  const auto row = [&](const char* name, const Summary& S) {
    printf("  %-32s %6d %6.2f %6.2f %6.2f %6.2f %5d %4d %4d\n", name, S.lines, justified ? S.p95 : S.slackP95,
           justified ? S.p99 : 0.0, justified ? S.max : 0.0, justified ? S.paraWorst : S.slackParaWorst, S.hyphenated,
           S.runs2, S.ladders);
  };
  row("greedy+hy (shipped)", G);
  struct Arm {
    bool shrink;
    double hp, dh, cap;
    int ladder;
  };
  std::vector<Arm> arms;
  for (const bool shrink : {false, true}) {
    if (shrink && !justified) continue;
    arms.push_back({shrink, 50, 10000, 10000, 0});  // TeX as shipped: capped badness, its own defaults
    for (const int ladder : {0, 2})
      for (const double hp : {50.0, 1000.0, 3000.0, 10000.0})
        for (const double dh : {10000.0, 1000000.0}) arms.push_back({shrink, hp, dh, 0, ladder});
  }
  for (const Arm& a : arms) {
    kp::Params prm = paramsFor(fontId, justified, a.shrink);
    prm.hyphenPenalty = a.hp;
    prm.doubleHyphenDemerits = a.dh;
    prm.badnessCap = a.cap;
    prm.maxConsecutiveHyphens = a.ladder;
    std::vector<std::vector<Line>> out;
    for (const auto& P : prepared)
      out.push_back(layOut(P, kpCuts(P, prm, nullptr, nullptr), justified, /*allowShrink=*/a.shrink));
    char name[96];
    std::snprintf(name, sizeof(name), "KP%s hp=%g dh=%g%s%s", a.shrink ? "+sh" : "", a.hp, a.dh,
                  a.cap > 0 ? " cap" : "", a.ladder ? " max2" : "");
    row(name, summarize(out, s, justified));
  }
}

// ---------------------------------------------------------------------------
// Proofs: the same paragraph under both breakers, drawn by the firmware
// ---------------------------------------------------------------------------

namespace {

bool pixelWhite(const uint8_t* fb, const int panelH, const int panelWB, const int lx, const int ly) {
  const int px = ly;
  const int py = (panelH - 1) - lx;
  return (fb[py * panelWB + (px >> 3)] >> (7 - (px & 7))) & 0x1;
}

void drawLines(const std::vector<Line>& lines, const int fontId, const int x0, int& y, const bool justified) {
  auto& r = Env::instance().renderer();
  BlockStyle bs;
  bs.alignment = justified ? CssTextAlign::Justify : CssTextAlign::Left;
  for (const auto& L : lines) {
    std::vector<EpdFontFamily::Style> st(L.texts.size(), EpdFontFamily::REGULAR);
    TextBlock tb(L.texts, L.xpos, st, {}, {}, bs);
    tb.render(r, fontId, x0, y);
    y += r.getLineHeight(fontId);
  }
}

}  // namespace

// CROSSPOINT_KP_RENDER="Albo:14:J:123,Albo:14:J:456" -> <out>/kp_<face>_<pt>_<J|R>_<para>.pgm, portrait,
// 4 levels (base + both AA planes), native pixels. CROSSPOINT_KP_OUT picks the directory.
TEST(KnuthPlass, DISABLED_Render) {
  const auto corpus = loadCorpus();
  const char* spec = std::getenv("CROSSPOINT_KP_RENDER");
  if (corpus.empty() || !spec) GTEST_SKIP() << "set CROSSPOINT_LINEBREAK_CORPUS and CROSSPOINT_KP_RENDER";
  const char* outDir = std::getenv("CROSSPOINT_KP_OUT");
  if (!outDir) outDir = ".";
  auto& r = Env::instance().renderer();
  std::string s = spec;
  size_t i = 0;
  while (i < s.size()) {
    size_t j = s.find(',', i);
    if (j == std::string::npos) j = s.size();
    char fam[64] = {0};
    int pt = 0, idx = 0;
    char mode = 'J';
    std::sscanf(s.substr(i, j - i).c_str(), "%63[^:]:%d:%c:%d", fam, &pt, &mode, &idx);
    i = j + 1;
    const int fontId = Env::instance().fontFor(fam, pt);
    ASSERT_NE(fontId, 0) << fam;
    ASSERT_LT(static_cast<size_t>(idx), corpus.size());
    const bool justified = mode == 'J';
    const auto words = wordsOf(corpus[idx]);
    const RealRun real = runReal(words, fontId, linebreak::STORED_HYPHENATED, justified);
    std::vector<kp::Pos> gcuts;
    ASSERT_TRUE(cutsFromBlocks(words, real.blocks, gcuts));
    const Prepared P = prepare(words, fontId, true);
    const auto greedy = layOut(P, gcuts, justified);
    const auto kpl = layOut(P, kpCuts(P, candidateParams(fontId, justified, false), nullptr, nullptr), justified);

    if (r.isSdCardFont(fontId)) {
      std::string all = "shipped greedy + hyphens Knuth-Plass total fit ";
      for (const auto& w : words) all += w + " ";
      all += "-";
      r.getSdCardFonts().at(fontId)->prewarm(all.c_str(), 0x0F, /*metadataOnly=*/false);
    }
    const uint32_t bufSize = static_cast<uint32_t>(r.getDisplayWidthBytes()) * r.getDisplayHeight();
    std::vector<uint8_t> planes[3] = {std::vector<uint8_t>(bufSize), std::vector<uint8_t>(bufSize),
                                      std::vector<uint8_t>(bufSize)};
    r.setGrayscaleAaStrength(GfxRenderer::AA_STANDARD);
    const int x0 = (r.getScreenWidth() - kMeasure) / 2;
    int yEnd = 0;
    for (int pass = 0; pass < 3; ++pass) {
      r.setRenderMode(pass == 0 ? GfxRenderer::BW : pass == 1 ? GfxRenderer::GRAYSCALE_LSB : GfxRenderer::GRAYSCALE_MSB);
      r.clearScreen(pass == 0 ? 0xFF : 0x00);
      const int lh = r.getLineHeight(fontId);
      int y = 6;
      r.drawText(fontId, x0, y, "shipped: greedy + hyphens", true, EpdFontFamily::REGULAR);
      y += lh + 4;
      drawLines(greedy, fontId, x0, y, justified);
      y += lh;
      r.drawText(fontId, x0, y, "Knuth-Plass (candidate)", true, EpdFontFamily::REGULAR);
      y += lh + 4;
      drawLines(kpl, fontId, x0, y, justified);
      yEnd = y + 6;
      std::memcpy(planes[pass].data(), r.getFrameBuffer(), bufSize);
    }
    r.setRenderMode(GfxRenderer::BW);
    const int W = r.getScreenWidth(), H = std::min(yEnd, r.getScreenHeight());
    char path[512];
    std::snprintf(path, sizeof(path), "%s/kp_%s_%d_%c_%d.pgm", outDir, fam, pt, mode, idx);
    FILE* f = std::fopen(path, "wb");
    ASSERT_NE(f, nullptr) << path;
    std::fprintf(f, "P5\n%d %d\n255\n", W, H);
    const int panelH = r.getDisplayHeight(), panelWB = r.getDisplayWidthBytes();
    for (int y = 0; y < H; ++y) {
      for (int x = 0; x < W; ++x) {
        const bool base = pixelWhite(planes[0].data(), panelH, panelWB, x, y);
        const bool lsb = pixelWhite(planes[1].data(), panelH, panelWB, x, y);
        const bool msb = pixelWhite(planes[2].data(), panelH, panelWB, x, y);
        // Same four preview levels as tools/calendar_preview's AA specimen.
        const uint8_t v = base ? 255 : msb ? (lsb ? 96 : 200) : lsb ? 96 : 0;
        std::fputc(v, f);
      }
    }
    std::fclose(f);
    printf("wrote %s (%d lines greedy, %d lines KP)\n", path, static_cast<int>(greedy.size()),
           static_cast<int>(kpl.size()));
  }
}

// ---------------------------------------------------------------------------
// The blind side-by-side (owner ruling 2026-09-25, "Blind side-by-side first")
// ---------------------------------------------------------------------------
//
// Two instruments. BlindStats writes one CSV row per paragraph so the pairs can
// be CHOSEN outside the binary (tools in docs/data/knuth-plass-2026-09-25/
// blind-tools/). BlindRender draws the chosen paragraphs, one PGM per ARM, with
// no label of any kind, and pads both arms of a pair to the SAME height so the
// image size cannot say which breaker set fewer lines. The arm name is in the
// PGM's filename only; the build script renames them A/B by a seeded shuffle.

namespace {

void writeParagraphPgm(const std::vector<Line>& lines, const int fontId, const std::vector<std::string>& words,
                       const bool justified, const int heightLines, const char* path) {
  auto& r = Env::instance().renderer();
  if (r.isSdCardFont(fontId)) {
    std::string all;
    for (const auto& w : words) all += w + " ";
    all += "-";
    r.getSdCardFonts().at(fontId)->prewarm(all.c_str(), 0x0F, /*metadataOnly=*/false);
  }
  const uint32_t bufSize = static_cast<uint32_t>(r.getDisplayWidthBytes()) * r.getDisplayHeight();
  std::vector<uint8_t> planes[3] = {std::vector<uint8_t>(bufSize), std::vector<uint8_t>(bufSize),
                                    std::vector<uint8_t>(bufSize)};
  r.setGrayscaleAaStrength(GfxRenderer::AA_STANDARD);
  const int x0 = (r.getScreenWidth() - kMeasure) / 2;
  const int lh = r.getLineHeight(fontId);
  for (int pass = 0; pass < 3; ++pass) {
    r.setRenderMode(pass == 0 ? GfxRenderer::BW : pass == 1 ? GfxRenderer::GRAYSCALE_LSB : GfxRenderer::GRAYSCALE_MSB);
    r.clearScreen(pass == 0 ? 0xFF : 0x00);
    int y = 8;
    drawLines(lines, fontId, x0, y, justified);
    std::memcpy(planes[pass].data(), r.getFrameBuffer(), bufSize);
  }
  r.setRenderMode(GfxRenderer::BW);
  const int W = r.getScreenWidth(), H = std::min(8 + heightLines * lh + 8, r.getScreenHeight());
  FILE* f = std::fopen(path, "wb");
  if (!f) {
    ADD_FAILURE() << path;
    return;
  }
  std::fprintf(f, "P5\n%d %d\n255\n", W, H);
  const int panelH = r.getDisplayHeight(), panelWB = r.getDisplayWidthBytes();
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      const bool base = pixelWhite(planes[0].data(), panelH, panelWB, x, y);
      const bool lsb = pixelWhite(planes[1].data(), panelH, panelWB, x, y);
      const bool msb = pixelWhite(planes[2].data(), panelH, panelWB, x, y);
      std::fputc(base ? 255 : msb ? (lsb ? 96 : 200) : lsb ? 96 : 0, f);
    }
  }
  std::fclose(f);
}

struct ArmPair {
  std::vector<std::string> words;
  std::vector<kp::Pos> gcuts, kcuts;
  std::vector<Line> greedy, kpl;
};

ArmPair bothArms(const std::string& text, const int fontId) {
  ArmPair a;
  a.words = wordsOf(text);
  const RealRun real = runReal(a.words, fontId, linebreak::STORED_HYPHENATED, /*justified=*/true);
  EXPECT_TRUE(cutsFromBlocks(a.words, real.blocks, a.gcuts));
  const Prepared P = prepare(a.words, fontId, true);
  a.kcuts = kpCuts(P, candidateParams(fontId, true, false), nullptr, nullptr);
  a.greedy = layOut(P, a.gcuts, true);
  a.kpl = layOut(P, a.kcuts, true);
  return a;
}

bool sameCuts(const std::vector<kp::Pos>& a, const std::vector<kp::Pos>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (a[i].word != b[i].word || a[i].offset != b[i].offset) return false;
  return true;
}

double worstOf(const std::vector<Line>& lines, const double s) {
  double w = 0;
  for (const auto& L : lines)
    if (!L.isFinal && L.gapCount) w = std::max(w, L.meanGap / s);
  return w;
}

int hyphensOf(const std::vector<Line>& lines) {
  int n = 0;
  for (const auto& L : lines) n += (!L.isFinal && L.hyphenated);
  return n;
}

}  // namespace

// CROSSPOINT_KP_FACES="Albo:14,LibreFranklin:14" -> <out>/stats_<face>_<pt>.csv, JUSTIFIED, candidate vs greedy.
TEST(KnuthPlass, DISABLED_BlindStats) {
  const auto corpus = loadCorpus();
  if (corpus.empty()) GTEST_SKIP() << "set CROSSPOINT_LINEBREAK_CORPUS";
  const char* outDir = std::getenv("CROSSPOINT_KP_OUT");
  if (!outDir) outDir = ".";
  std::string spec = std::getenv("CROSSPOINT_KP_FACES") ? std::getenv("CROSSPOINT_KP_FACES") : "Albo:14";
  size_t i = 0;
  while (i < spec.size()) {
    size_t j = spec.find(',', i);
    if (j == std::string::npos) j = spec.size();
    const std::string item = spec.substr(i, j - i);
    i = j + 1;
    const size_t c = item.find(':');
    const std::string fam = item.substr(0, c);
    const int pt = std::atoi(item.c_str() + c + 1);
    const int fontId = Env::instance().fontFor(fam, pt);
    ASSERT_NE(fontId, 0) << fam;
    const double s = Env::instance().renderer().getSpaceAdvance(fontId, 'n', 'n', EpdFontFamily::REGULAR);
    char path[512];
    std::snprintf(path, sizeof(path), "%s/stats_%s_%d.csv", outDir, fam.c_str(), pt);
    FILE* f = std::fopen(path, "w");
    ASSERT_NE(f, nullptr);
    std::fprintf(f, "idx,differ,greedy_lines,kp_lines,greedy_worst,kp_worst,greedy_hyph,kp_hyph,words\n");
    for (size_t pi = 0; pi < corpus.size(); ++pi) {
      const ArmPair a = bothArms(corpus[pi], fontId);
      std::fprintf(f, "%zu,%d,%zu,%zu,%.3f,%.3f,%d,%d,%zu\n", pi, sameCuts(a.gcuts, a.kcuts) ? 0 : 1, a.greedy.size(),
                   a.kpl.size(), worstOf(a.greedy, s), worstOf(a.kpl, s), hyphensOf(a.greedy), hyphensOf(a.kpl),
                   a.words.size());
    }
    std::fclose(f);
    printf("wrote %s\n", path);
  }
}

// CROSSPOINT_KP_BLIND="Albo:14:443,LibreFranklin:14:678,..." ->
// <out>/<face>_<pt>_<idx>_{greedy,kp}.pgm, justified, unlabeled, equal heights.
TEST(KnuthPlass, DISABLED_BlindRender) {
  const auto corpus = loadCorpus();
  const char* spec = std::getenv("CROSSPOINT_KP_BLIND");
  if (corpus.empty() || !spec) GTEST_SKIP() << "set CROSSPOINT_LINEBREAK_CORPUS and CROSSPOINT_KP_BLIND";
  const char* outDir = std::getenv("CROSSPOINT_KP_OUT");
  if (!outDir) outDir = ".";
  std::string s = spec;
  size_t i = 0;
  while (i < s.size()) {
    size_t j = s.find(',', i);
    if (j == std::string::npos) j = s.size();
    char fam[64] = {0};
    int pt = 0, idx = 0;
    std::sscanf(s.substr(i, j - i).c_str(), "%63[^:]:%d:%d", fam, &pt, &idx);
    i = j + 1;
    const int fontId = Env::instance().fontFor(fam, pt);
    ASSERT_NE(fontId, 0) << fam;
    ASSERT_LT(static_cast<size_t>(idx), corpus.size());
    const ArmPair a = bothArms(corpus[idx], fontId);
    const int h = static_cast<int>(std::max(a.greedy.size(), a.kpl.size()));
    char path[512];
    std::snprintf(path, sizeof(path), "%s/%s_%d_%d_greedy.pgm", outDir, fam, pt, idx);
    writeParagraphPgm(a.greedy, fontId, a.words, true, h, path);
    std::snprintf(path, sizeof(path), "%s/%s_%d_%d_kp.pgm", outDir, fam, pt, idx);
    writeParagraphPgm(a.kpl, fontId, a.words, true, h, path);
  }
}

// ===========================================================================
// THE DEVICE PORT (owner ruling 2026-09-26, "go with k-p").
//
// lib/Epub/Epub/KnuthPlassBreaker.h is what ships: integer demerits, a window,
// ParsedText's full token stream. These tests hold it to the prototype above
// (KnuthPlass.h, the breaker the owner judged in the blind test) cut for cut.
// Two layers, because they fail for different reasons:
//
//   * PURE: the device DP fed the prototype's own positions and widths
//     (ProtoModel). A difference here is the DP -- integer rounding, a state
//     mapping, a tie broken the other way.
//   * THROUGH ParsedText: the real layoutAndExtractLines on a justified,
//     hyphenating block, cuts read back off the TextBlocks it bakes. A
//     difference here that the pure layer does not show is the ParsedText
//     adapter -- a width, a gap, a position, a split.
// ===========================================================================

namespace {

// The device breaker's model over a prototype paragraph: the SAME positions
// and the same measured widths, so the two DPs see identical input.
struct ProtoModel {
  const kp::Paragraph& p;
  int fontId = 0;
  std::vector<char> breakBefore;           // (k,0) is a position
  std::vector<std::vector<int>> hyphenAt;  // indices into p.positions, per word
  explicit ProtoModel(const kp::Paragraph& para)
      : p(para), breakBefore(para.words + 1, 0), hyphenAt(static_cast<size_t>(para.words) + 1) {
    for (size_t i = 0; i < p.positions.size(); ++i) {
      const auto& q = p.positions[i];
      if (q.offset == 0)
        breakBefore[q.word] = 1;
      else
        hyphenAt[q.word].push_back(static_cast<int>(i));
    }
  }
  int tokenCount() const { return p.words; }
  int fullWidth(const int k) const { return p.full[k]; }
  int gapBefore(const int k) const { return p.gapAfter[k - 1]; }
  bool gapStretches(int) const { return true; }
  // Every prototype gap is a regular word space.
  int gapShrinkCap(int) const {
    return kpbreak::shrinkPerGapPx(
        Env::instance().renderer().getSpaceAdvance(fontId, 'n', 'n', EpdFontFamily::REGULAR));
  }
  bool mayBreakBefore(const int k) const { return breakBefore[k] != 0; }
  template <typename F>
  void forEachHyphenPoint(const int k, bool, F&& f) const {
    for (const int i : hyphenAt[k]) {
      const auto& q = p.positions[i];
      f(q.offset, q.hyphen, p.piece(k, 0, q.offset, q.hyphen), p.piece(k, q.offset, -1, false));
    }
  }
  int pieceWidth(const int k, const int from, const int to, const bool hy) const { return p.piece(k, from, to, hy); }
};

kpbreak::Config deviceConfig(const Prepared& P, const int windowPositions = kpbreak::WINDOW_POSITIONS,
                             const bool shrink = true) {
  kpbreak::Config c;
  c.shrink = shrink;
  c.measure = kMeasure;
  c.firstLineIndent = P.para.firstLineIndentPx;
  c.spaceAdvance = Env::instance().renderer().getSpaceAdvance(P.fontId, 'n', 'n', EpdFontFamily::REGULAR);
  c.windowPositions = windowPositions;
  c.windowTokens = std::max(kpbreak::WINDOW_TOKENS, windowPositions * 2);
  return c;
}

// Large enough that no paragraph in the corpus (719 positions at most) is
// windowed: the pure comparison is against the prototype's global optimum.
constexpr int kUnwindowed = 5000;

std::vector<kp::Pos> deviceCuts(const Prepared& P, const int windowPositions, kpbreak::Result* res = nullptr,
                                kpbreak::Stats* st = nullptr, const bool shrink = true) {
  ProtoModel m(P.para);
  m.fontId = P.fontId;
  std::vector<kpbreak::Cut> cuts;
  const kpbreak::Result r = kpbreak::breakParagraph(m, deviceConfig(P, windowPositions, shrink), cuts, st);
  if (res) *res = r;
  std::vector<kp::Pos> out = {{0, 0, false, false}};
  for (const auto& c : cuts) out.push_back({c.word, c.offset, c.hyphen, c.offset > 0});
  return out;
}

std::string cutsText(const std::vector<kp::Pos>& c) {
  std::string s;
  for (const auto& p : c) s += std::to_string(p.word) + ":" + std::to_string(p.offset) + " ";
  return s;
}

// What shrink promises, checked on the painted lines rather than trusted:
//   * the BREAKER's bound: a line's natural width exceeds its measure by at
//     most a third of a space per gap (r >= -1), exactly: 3 x over <= gaps x space;
//   * no painted gap narrower than its natural gap less ceil(space / 3) -- the
//     2/3-of-a-space floor, to the pixel;
//   * no painted line past the measure, beyond its own trailing hang (the
//     punctuation that deliberately hangs into the margin);
//   * a final line is never shrunk, so its natural width fits outright.
// Without shrink the first bound is natural <= measure, as before.
struct GapStats {
  double minPaintedOverSpace = 1e9;
  int shrunkLines = 0;
};
void expectLinesHonest(const Prepared& P, const std::vector<Line>& lines, const bool justified, const bool shrink,
                       const std::string& label, GapStats* gs = nullptr) {
  const int space = Env::instance().renderer().getSpaceAdvance(P.fontId, 'n', 'n', EpdFontFamily::REGULAR);
  const int cap = kpbreak::shrinkPerGapPx(space);
  for (size_t l = 0; l < lines.size(); ++l) {
    const Line& L = lines[l];
    const int over = L.natural - L.avail;
    const int gaps = static_cast<int>(L.naturalGaps.size());
    if (L.isFinal || !justified || !shrink) {
      EXPECT_LE(over, 0) << label << ": line " << l << " overflows";
      continue;
    }
    EXPECT_LE(over, gaps * cap) << label << ": line " << l << " shrinks past 2/3 of a space";
    if (over > 0 && gs) gs->shrunkLines++;
    for (int g = 0; g < gaps; ++g) {
      EXPECT_GE(L.paintedGaps[g], L.naturalGaps[g] - cap) << label << ": line " << l << " gap " << g;
      // The ruling's own words, in whole pixels: no gap that was a full space
      // is painted narrower than 2/3 of one.
      if (L.naturalGaps[g] >= space) EXPECT_GE(3 * L.paintedGaps[g], 2 * space) << label << ": line " << l;
      if (gs) gs->minPaintedOverSpace = std::min(gs->minPaintedOverSpace, static_cast<double>(L.paintedGaps[g]) / space);
    }
    EXPECT_LE(L.end, kMeasure + L.trailHang) << label << ": line " << l << " is painted past the measure";
  }
}

// Every word set once (the TextBlocks rebuild the source) and no non-final
// line wider than the measure, read off what ParsedText actually baked.
void expectSane(const std::vector<std::string>& words, const RealRun& real, const Prepared& P, const bool justified,
                const std::string& label, GapStats* gs = nullptr) {
  std::vector<kp::Pos> cuts;
  ASSERT_TRUE(cutsFromBlocks(words, real.blocks, cuts)) << label << ": the lines do not rebuild the source text";
  const bool shrink = justified && kpbreak::tuning().shrink;
  const auto lines = layOut(P, cuts, justified, shrink);
  ASSERT_EQ(mismatchedLines(lines, real.blocks), 0) << label;
  expectLinesHonest(P, lines, justified, shrink, label, gs);
}

std::string repeatedParagraph(const int copies) {
  std::string t;
  for (int c = 0; c < copies; ++c)
    for (const auto& p : builtinParagraphs()) t += p + " ";
  t.pop_back();
  return t;
}

}  // namespace

// Instrument: each face's word space, and what automatic justification decides
// for it at the X3's 512 px measure (its estimated characters per line against
// the default threshold).
TEST(KnuthPlassDevice, DISABLED_SpaceWidths) {
  for (const auto& f : std::vector<std::pair<std::string, int>>{{"LibreFranklin", 12},
                                                                {"LibreFranklin", 14},
                                                                {"LibreFranklin", 18},
                                                                {"Albo", 8},
                                                                {"Albo", 10},
                                                                {"Albo", 12},
                                                                {"Albo", 14},
                                                                {"Albo", 16},
                                                                {"Albo", 18}}) {
    const int id = Env::instance().fontFor(f.first, f.second);
    if (!id) continue;
    auto& r = Env::instance().renderer();
    if (r.isSdCardFont(id)) {
      std::deque<std::string> d = {autojustify::ALPHABET, "n n"};
      r.ensureSdCardFontReady(id, d, true, 0x01);
    }
    const int alpha = r.getTextAdvanceX(id, autojustify::ALPHABET, EpdFontFamily::REGULAR);
    const int cpl = autojustify::charsPerLine(kMeasure, alpha);
    printf("[space] %s %d: space %d px, alphabet %d px, ~%d chars/line at %d px -> %s at threshold %d\n",
           f.first.c_str(), f.second, r.getSpaceAdvance(id, 'n', 'n', EpdFontFamily::REGULAR), alpha, cpl, kMeasure,
           autojustify::shouldJustify(kMeasure, alpha) ? "JUSTIFIED" : "ragged", autojustify::THRESHOLD_CHARS);
  }
}

TEST(KnuthPlassDevice, PureBreakerMatchesThePrototypeCutForCut) {
  int paragraphs = 0, hyphenated = 0, shrinkChangedSomething = 0;
  for (const int pt : {12, 14, 18}) {
    const int fontId = Env::instance().fontFor("LibreFranklin", pt);
    ASSERT_NE(fontId, 0);
    for (const auto& text : builtinParagraphs()) {
      const Prepared P = prepare(wordsOf(text), fontId, true);
      const auto proto = kpCuts(P, shippedParams(fontId), nullptr, nullptr);
      kpbreak::Result r;
      const auto dev = deviceCuts(P, kUnwindowed, &r);
      ASSERT_EQ(r, kpbreak::Result::Ok);
      EXPECT_TRUE(sameCuts(proto, dev)) << "LF " << pt << "\n proto " << cutsText(proto) << "\n dev   "
                                        << cutsText(dev);
      // And the stretch-only arm the blind test showed, still reachable.
      const auto protoStretch = kpCuts(P, candidateParams(fontId, true, false), nullptr, nullptr);
      const auto devStretch = deviceCuts(P, kUnwindowed, &r, nullptr, /*shrink=*/false);
      ASSERT_EQ(r, kpbreak::Result::Ok);
      EXPECT_TRUE(sameCuts(protoStretch, devStretch)) << "stretch-only, LF " << pt;
      shrinkChangedSomething += !sameCuts(dev, devStretch);
      for (const auto& c : dev) hyphenated += c.offset > 0;
      paragraphs++;
    }
  }
  EXPECT_EQ(paragraphs, 12);
  EXPECT_GT(hyphenated, 0) << "no hyphenated line in the fixture: the flagged-penalty half is untested";
  EXPECT_GT(shrinkChangedSomething, 0) << "shrink moved no break in the fixture: the shrink half is untested";
}

TEST(KnuthPlassDevice, ParsedTextMatchesThePrototypeOnJustifiedBlocks) {
  int lines = 0, hyphenated = 0;
  for (const int pt : {12, 14, 18}) {
    const int fontId = Env::instance().fontFor("LibreFranklin", pt);
    ASSERT_NE(fontId, 0);
    for (const auto& text : builtinParagraphs()) {
      const auto words = wordsOf(text);
      const Prepared P = prepare(words, fontId, true);
      const auto proto = kpCuts(P, shippedParams(fontId), nullptr, nullptr);
      const int before = kpbreak::tuning().attempts;
      const RealRun real = runReal(words, fontId, linebreak::STORED_HYPHENATED, true, /*knuthPlass=*/true);
      ASSERT_EQ(kpbreak::tuning().attempts, before + 1) << "the justified block never reached Knuth-Plass";
      ASSERT_EQ(kpbreak::tuning().lastResult, kpbreak::Result::Ok);
      std::vector<kp::Pos> got;
      ASSERT_TRUE(cutsFromBlocks(words, real.blocks, got));
      EXPECT_TRUE(sameCuts(proto, got)) << "LF " << pt << "\n proto " << cutsText(proto) << "\n parsed "
                                        << cutsText(got);
      expectSane(words, real, P, true, "LF " + std::to_string(pt));
      lines += static_cast<int>(got.size()) - 1;
      for (const auto& c : got) hyphenated += c.offset > 0;
    }
  }
  EXPECT_GT(lines, 60);
  EXPECT_GT(hyphenated, 0);
}

// Automatic justification decides "justified" per block; a block it demotes
// (threshold 255 here) and a block that asked for Left both keep greedy.
TEST(KnuthPlassDevice, RaggedBlocksAreUntouched) {
  const int fontId = Env::instance().fontFor("LibreFranklin", 14);
  ASSERT_NE(fontId, 0);
  for (const auto& text : builtinParagraphs()) {
    const auto words = wordsOf(text);
    for (const bool demoted : {false, true}) {
      auto run = [&](const bool kpOn) {
        kpbreak::tuning().enabled = kpOn;
        BlockStyle style;
        style.alignment = demoted ? CssTextAlign::Justify : CssTextAlign::Left;
        ParsedText block(false, linebreak::STORED_HYPHENATED, false, style);
        for (const auto& w : words) block.addWord(w, EpdFontFamily::REGULAR);
        std::vector<std::shared_ptr<TextBlock>> out;
        block.layoutAndExtractLines(
            Env::instance().renderer(), fontId, kMeasure,
            [&](const std::shared_ptr<TextBlock>& l) { out.push_back(l); }, true, demoted ? 255 : 0);
        kpbreak::tuning().enabled = true;
        return out;
      };
      const int before = kpbreak::tuning().attempts;
      const auto withKp = run(true);
      EXPECT_EQ(kpbreak::tuning().attempts, before) << "a ragged block reached Knuth-Plass";
      const auto greedy = run(false);
      ASSERT_EQ(withKp.size(), greedy.size());
      for (size_t l = 0; l < greedy.size(); ++l) {
        ASSERT_EQ(withKp[l]->wordCount(), greedy[l]->wordCount());
        for (uint16_t i = 0; i < greedy[l]->wordCount(); ++i) {
          EXPECT_STREQ(withKp[l]->wordText(i), greedy[l]->wordText(i));
          EXPECT_EQ(withKp[l]->wordXpos(i), greedy[l]->wordXpos(i));
        }
      }
    }
  }
}

// A heap that cannot hold the working set must fall back to greedy with the
// block untouched: the output is then byte-for-byte the greedy breaker's.
TEST(KnuthPlassDevice, AllocationFailureFallsBackToGreedyExactly) {
  const int fontId = Env::instance().fontFor("LibreFranklin", 14);
  ASSERT_NE(fontId, 0);
  int differedFromKp = 0;
  for (const size_t limit : {size_t{1}, size_t{200}, size_t{2000}}) {
    for (const auto& text : builtinParagraphs()) {
      const auto words = wordsOf(text);
      const RealRun greedy = runReal(words, fontId, linebreak::STORED_HYPHENATED, true, false);
      const RealRun kpRun = runReal(words, fontId, linebreak::STORED_HYPHENATED, true, true);
      kpbreak::tuning().allocLimitBytes = limit;
      const RealRun starved = runReal(words, fontId, linebreak::STORED_HYPHENATED, true, true);
      const kpbreak::Result r = kpbreak::tuning().lastResult;
      kpbreak::tuning().allocLimitBytes = 0;
      ASSERT_EQ(r, kpbreak::Result::AllocFailed) << "limit " << limit;
      ASSERT_EQ(starved.blocks.size(), greedy.blocks.size());
      for (size_t l = 0; l < greedy.blocks.size(); ++l) {
        ASSERT_EQ(starved.blocks[l]->wordCount(), greedy.blocks[l]->wordCount());
        for (uint16_t i = 0; i < greedy.blocks[l]->wordCount(); ++i) {
          EXPECT_STREQ(starved.blocks[l]->wordText(i), greedy.blocks[l]->wordText(i));
          EXPECT_EQ(starved.blocks[l]->wordXpos(i), greedy.blocks[l]->wordXpos(i));
        }
      }
      std::vector<kp::Pos> a, b;
      ASSERT_TRUE(cutsFromBlocks(words, greedy.blocks, a));
      ASSERT_TRUE(cutsFromBlocks(words, kpRun.blocks, b));
      differedFromKp += !sameCuts(a, b);
    }
  }
  // Not vacuous: on these paragraphs the fallback visibly is NOT Knuth-Plass.
  EXPECT_GT(differedFromKp, 0);

  // And the pure breaker says so without touching its output vector's caller.
  const Prepared P = prepare(wordsOf(builtinParagraphs()[0]), fontId, true);
  ProtoModel m(P.para);
  m.fontId = P.fontId;
  kpbreak::Config c = deviceConfig(P);
  c.allocLimitBytes = 100;
  std::vector<kpbreak::Cut> cuts = {{1, 2, true}};
  EXPECT_EQ(kpbreak::breakParagraph(m, c, cuts), kpbreak::Result::AllocFailed);
  EXPECT_TRUE(cuts.empty());
}

// The window: a paragraph past WINDOW_POSITIONS is solved in windows, still
// sets every word once without overflow, and holds no more than the stated
// worst case. A small window on the fixture exercises the seams many times.
TEST(KnuthPlassDevice, TheWindowBoundsMemoryAndStillSetsEveryWord) {
  const int fontId = Env::instance().fontFor("LibreFranklin", 14);
  ASSERT_NE(fontId, 0);

  // (a) the shipped window, on a paragraph longer than it.
  const auto words = wordsOf(repeatedParagraph(3));
  const Prepared P = prepare(words, fontId, true);
  ASSERT_GT(static_cast<int>(P.para.positions.size()), 2 * kpbreak::WINDOW_POSITIONS)
      << "the long fixture no longer needs more than one window";
  const RealRun real = runReal(words, fontId, linebreak::STORED_HYPHENATED, true, true);
  ASSERT_EQ(kpbreak::tuning().lastResult, kpbreak::Result::Ok);
  const kpbreak::Stats st = kpbreak::tuning().lastStats;
  EXPECT_GE(st.windows, 3);
  // The header's worst case, computed from the same terms.
  const size_t P1 = kpbreak::WINDOW_POSITIONS + 1;
  const size_t worst = P1 * sizeof(kpbreak::detail::Pos) + P1 * kpbreak::STATES * (sizeof(int64_t) + sizeof(uint16_t)) +
                       P1 * sizeof(uint16_t) + (kpbreak::WINDOW_TOKENS + 1) * sizeof(int32_t) +
                       kpbreak::WINDOW_TOKENS * (sizeof(int32_t) + sizeof(uint16_t));
  EXPECT_LE(st.peakBytes, worst);
  printf("[window] %zu words, %zu positions: %d windows, peak %zu B (worst case %zu B)\n", words.size(),
         P.para.positions.size(), st.windows, st.peakBytes, worst);
  expectSane(words, real, P, true, "long paragraph");

  // The windowed result against the prototype's global optimum: report, and
  // bound the damage at the seams -- no line looser than the prototype's worst.
  const auto proto = kpCuts(P, shippedParams(fontId), nullptr, nullptr);
  std::vector<kp::Pos> got;
  ASSERT_TRUE(cutsFromBlocks(words, real.blocks, got));
  const double s = Env::instance().renderer().getSpaceAdvance(fontId, 'n', 'n', EpdFontFamily::REGULAR);
  const double protoWorst = worstOf(layOut(P, proto, true, true), s);
  const double gotWorst = worstOf(layOut(P, got, true, true), s);
  printf("[window] same cuts as the unwindowed prototype: %s; worst line %.2f vs %.2f spaces\n",
         sameCuts(proto, got) ? "yes" : "no", gotWorst, protoWorst);

  // (b) a tiny window through the pure breaker: many seams.
  for (const int w : {8, 24, 40}) {
    for (const auto& text : builtinParagraphs()) {
      const Prepared Q = prepare(wordsOf(text), fontId, true);
      kpbreak::Result r;
      kpbreak::Stats qs;
      const auto dev = deviceCuts(Q, w, &r, &qs);
      ASSERT_EQ(r, kpbreak::Result::Ok) << "window " << w;
      EXPECT_GT(qs.windows, 1) << "window " << w;
      const auto lines = layOut(Q, dev, true, true);
      expectLinesHonest(Q, lines, true, true, "window " + std::to_string(w));
      std::string rebuilt;
      for (size_t l = 0; l < lines.size(); ++l) {
        const kp::Pos& b = dev[l + 1];
        for (size_t t = 0; t < lines[l].texts.size(); ++t) {
          const bool split = t + 1 == lines[l].texts.size() && b.offset > 0;
          std::string piece = lines[l].texts[t];
          if (split && b.hyphen) piece.pop_back();
          rebuilt += piece;
          if (!split) rebuilt += ' ';
        }
      }
      std::string expect;
      for (const auto& x : Q.words) expect += x + " ";
      EXPECT_EQ(rebuilt, expect) << "window " << w;
    }
  }
}

// The prototype's own (double) total demerits for a cut list -- the objective
// both DPs minimize, evaluated on the path rather than searched. Two different
// cut lists with EQUAL totals are an exact tie: both are the optimum, and which
// one a DP returns is down to loop order (integer) or summation rounding
// (double). The corpus test counts those apart rather than as differences.
double pathDemerits(const Prepared& P, const kp::Params& prm, const std::vector<kp::Pos>& cuts) {
  std::vector<long long> fs(P.para.words + 1, 0), gs(P.para.words + 1, 0);
  for (int k = 0; k < P.para.words; ++k) {
    fs[k + 1] = fs[k] + P.para.full[k];
    gs[k + 1] = gs[k] + (k + 1 < P.para.words ? P.para.gapAfter[k] : 0);
  }
  auto indexOf = [&](const kp::Pos& c) {
    for (size_t i = 0; i < P.para.positions.size(); ++i)
      if (P.para.positions[i].word == c.word && P.para.positions[i].offset == c.offset) return static_cast<int>(i);
    return -1;
  };
  double total = 0;
  int prevFit = 1;
  for (size_t l = 1; l < cuts.size(); ++l) {
    const int i = indexOf(cuts[l - 1]), j = indexOf(cuts[l]);
    if (i < 0 || j < 0) return -1;
    const kp::LineGeom g = kp::lineGeom(P.para, fs, gs, i, j);
    const bool isEnd = l + 1 == cuts.size();
    const int avail = P.para.measurePx - (i == 0 ? P.para.firstLineIndentPx : 0);
    const double slack = avail - g.width;
    double r = 0, b = 0;
    if (!isEnd && g.gaps > 0) {
      r = slack >= 0 ? slack / (g.gaps * prm.stretchPerGapPx) : slack / (g.gaps * prm.shrinkPerGapPx);
      b = 100 * std::abs(r * r * r);
    } else if (!isEnd && slack > 0) {
      r = 10;
      b = 1e6;
    }
    const int fit = isEnd ? 1 : kp::fitnessOf(r);
    const bool endH = P.para.positions[j].flagged && !isEnd;
    const bool startF = P.para.positions[i].flagged && i != 0;
    double d = (prm.linePenalty + b) * (prm.linePenalty + b) + (endH ? prm.hyphenPenalty * prm.hyphenPenalty : 0);
    if (endH && startF) d += prm.doubleHyphenDemerits;
    if (isEnd && startF) d += prm.finalHyphenDemerits;
    if (std::abs(prevFit - fit) > 1) d += prm.adjDemerits;
    prevFit = fit;
    total += d;
  }
  return total;
}

bool exactTie(const Prepared& P, const kp::Params& prm, const std::vector<kp::Pos>& a, const std::vector<kp::Pos>& b) {
  const double x = pathDemerits(P, prm, a), y = pathDemerits(P, prm, b);
  return x >= 0 && y >= 0 && std::abs(x - y) <= 1e-9 * std::max(1.0, std::abs(x));
}

// THE CORPUS: the owner's own books (tools/linebreak_corpus.py, doc section 2).
// Skips without CROSSPOINT_LINEBREAK_CORPUS; the doc records the run.
//   * pure, unwindowed: must equal the prototype on EVERY paragraph;
//   * through ParsedText with the shipped window: must equal it on every
//     paragraph that fits one window, and is counted on the rest;
//   * every paragraph: every word set once, no line over the measure;
//   * host timing: ParsedText greedy vs ParsedText Knuth-Plass, whole call.
TEST(KnuthPlassDevice, CorpusMatchesThePrototype) {
  const auto corpus = loadCorpus();
  if (corpus.empty()) GTEST_SKIP() << "set CROSSPOINT_LINEBREAK_CORPUS";
  std::string spec = std::getenv("CROSSPOINT_KP_FACES") ? std::getenv("CROSSPOINT_KP_FACES") : "LibreFranklin:14,Albo:14";
  size_t i = 0;
  while (i < spec.size()) {
    size_t j = spec.find(',', i);
    if (j == std::string::npos) j = spec.size();
    const std::string item = spec.substr(i, j - i);
    i = j + 1;
    const size_t c = item.find(':');
    const std::string fam = item.substr(0, c);
    const int pt = std::atoi(item.c_str() + c + 1);
    const int fontId = Env::instance().fontFor(fam, pt);
    if (fontId == 0) {
      printf("[corpus] %s %d not available, skipped\n", fam.c_str(), pt);
      continue;
    }
    int pureDiff = 0, parsedDiff = 0, windowed = 0, windowedDiff = 0, lines = 0, hyph = 0, maxPos = 0;
    double greedyUs = 0, kpUs = 0, protoDpUs = 0, devDpUs = 0, maxKpUs = 0, seamWorst = 0, protoWorst = 0;
    int seamWorse = 0, seamHyph = 0, protoHyph = 0;
    int ties = 0, parsedTies = 0;
    int stretchDiff = 0, stretchLines = 0, stretchHyph = 0, shrinkMoved = 0;
    double shrinkWorstSum = 0, stretchWorstSum = 0, shrinkMin = 1e9;
    GapStats gs;
    size_t peak = 0;
    for (size_t pi = 0; pi < corpus.size(); ++pi) {
      const auto words = wordsOf(corpus[pi]);
      const Prepared P = prepare(words, fontId, true);
      maxPos = std::max(maxPos, static_cast<int>(P.para.positions.size()));
      double protoUs = 0;
      const auto proto = kpCuts(P, shippedParams(fontId), nullptr, &protoUs);
      protoDpUs += protoUs;
      const auto t0 = std::chrono::steady_clock::now();
      kpbreak::Result r;
      const auto pure = deviceCuts(P, kUnwindowed, &r);
      devDpUs += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
      ASSERT_EQ(r, kpbreak::Result::Ok) << pi;
      if (!sameCuts(proto, pure) && exactTie(P, shippedParams(fontId), proto, pure)) {
        ties++;
      } else if (!sameCuts(proto, pure)) {
        if (pureDiff < 5)
          ADD_FAILURE() << fam << " " << pt << " paragraph " << pi << " pure\n proto " << cutsText(proto)
                        << "\n dev   " << cutsText(pure);
        pureDiff++;
      }
      // The stretch-only arm (the blind test's): parity still holds, and it is
      // the "before" every shrink figure is read against.
      const auto protoStretch = kpCuts(P, candidateParams(fontId, true, false), nullptr, nullptr);
      const auto pureStretch = deviceCuts(P, kUnwindowed, &r, nullptr, /*shrink=*/false);
      stretchDiff += !sameCuts(protoStretch, pureStretch);
      shrinkMoved += !sameCuts(pure, pureStretch);
      {
        const double sp = Env::instance().renderer().getSpaceAdvance(fontId, 'n', 'n', EpdFontFamily::REGULAR);
        const auto a = layOut(P, pure, true, true), b = layOut(P, pureStretch, true, false);
        shrinkWorstSum += worstOf(a, sp);
        stretchWorstSum += worstOf(b, sp);
        stretchLines += static_cast<int>(b.size());
        stretchHyph += hyphensOf(b);
        for (const auto& L : a)
          if (!L.isFinal && L.gapCount) shrinkMin = std::min(shrinkMin, L.meanGap / sp);
      }
      const RealRun g = runReal(words, fontId, linebreak::STORED_HYPHENATED, true, false);
      greedyUs += g.micros;
      const RealRun k = runReal(words, fontId, linebreak::STORED_HYPHENATED, true, true);
      kpUs += k.micros;
      maxKpUs = std::max(maxKpUs, k.micros);
      ASSERT_EQ(kpbreak::tuning().lastResult, kpbreak::Result::Ok) << pi;
      peak = std::max(peak, kpbreak::tuning().lastStats.peakBytes);
      expectSane(words, k, P, true, fam + " paragraph " + std::to_string(pi), &gs);
      std::vector<kp::Pos> got;
      ASSERT_TRUE(cutsFromBlocks(words, k.blocks, got));
      lines += static_cast<int>(got.size()) - 1;
      for (const auto& q : got) hyph += q.offset > 0;
      const bool oneWindow = static_cast<int>(P.para.positions.size()) <= kpbreak::WINDOW_POSITIONS + 1;
      if (!oneWindow) windowed++;
      if (!sameCuts(proto, got) && oneWindow && exactTie(P, shippedParams(fontId), proto, got)) {
        parsedTies++;
      } else if (!sameCuts(proto, got)) {
        if (oneWindow) {
          if (parsedDiff < 5)
            ADD_FAILURE() << fam << " " << pt << " paragraph " << pi << " ParsedText\n proto " << cutsText(proto)
                          << "\n parsed " << cutsText(got);
          parsedDiff++;
        } else {
          windowedDiff++;
        }
      }
      if (!oneWindow) {
        // What the seams cost: this paragraph's loosest line, windowed vs not.
        const double sp = Env::instance().renderer().getSpaceAdvance(fontId, 'n', 'n', EpdFontFamily::REGULAR);
        const double a = worstOf(layOut(P, got, true, true), sp), b = worstOf(layOut(P, proto, true, true), sp);
        seamWorst += a;
        protoWorst += b;
        seamWorse += a > b + 1e-9;
        seamHyph += hyphensOf(layOut(P, got, true, true));
        protoHyph += hyphensOf(layOut(P, proto, true, true));
      }
    }
    const double n = static_cast<double>(corpus.size());
    printf("[corpus] %s %d: %zu paragraphs, %d lines, %d hyphenated; pure differs %d, ParsedText differs %d "
           "(one window); exact ties %d / %d; %d paragraphs past the window, %d of them differ; max positions %d; "
           "peak %zu B\n",
           fam.c_str(), pt, corpus.size(), lines, hyph, pureDiff, parsedDiff, ties, parsedTies, windowed, windowedDiff,
           maxPos, peak);
    if (windowed > 0)
      printf("[corpus] %s %d windowed paragraphs: mean worst line %.3f vs unwindowed %.3f spaces, %d of %d worse; "
             "hyphenated lines %d vs %d\n",
             fam.c_str(), pt, seamWorst / windowed, protoWorst / windowed, seamWorse, windowed, seamHyph, protoHyph);
    printf("[corpus] %s %d shrink vs stretch-only: lines %d vs %d, hyphenated %d vs %d, mean paragraph-worst line "
           "%.3f vs %.3f spaces, tightest line mean gap %.3f; %d of %zu paragraphs broke differently; lines set tight "
           "(natural > measure) %d; narrowest painted gap %.3f space; stretch-only parity differs %d\n",
           fam.c_str(), pt, lines, stretchLines, hyph, stretchHyph, shrinkWorstSum / n, stretchWorstSum / n, shrinkMin,
           shrinkMoved, corpus.size(), gs.shrunkLines, gs.minPaintedOverSpace, stretchDiff);
    printf("[corpus] %s %d host timing per paragraph: ParsedText greedy %.1f us, ParsedText Knuth-Plass %.1f us "
           "(max %.0f us); DP alone: prototype (double) %.1f us, device (int64) %.1f us\n",
           fam.c_str(), pt, greedyUs / n, kpUs / n, maxKpUs, protoDpUs / n, devDpUs / n);
    EXPECT_EQ(pureDiff, 0);
    EXPECT_EQ(parsedDiff, 0);
    EXPECT_EQ(stretchDiff, 0);
  }
}

// Adversarial review 2026-09-26, finding 1: a word longer than three lines has
// no path under a HARD two-hyphens-in-a-row cap (its fourth fragment can only
// follow a third hyphenated line), so the whole paragraph silently fell to
// greedy. The breaker now relaxes the cap for exactly the positions nothing
// else can reach; this pins that such a paragraph is set by Knuth-Plass, whole
// and within the measure.
TEST(KnuthPlassDevice, AWordLongerThanThreeLinesStillGetsKnuthPlass) {
  const int fontId = Env::instance().fontFor("LibreFranklin", 14);
  ASSERT_NE(fontId, 0);
  std::string giant;
  for (int i = 0; i < 6; ++i) giant += "Donaudampfschifffahrtsgesellschaftskapitaen";
  std::string text;
  for (int i = 0; i < 3; ++i) text += "the river boats of the old company were named " + giant + " and more ";
  const auto words = wordsOf(text);
  const Prepared P = prepare(words, fontId, true);
  ASSERT_GT(P.para.full[9], 3 * kMeasure) << "the fixture word no longer spans four lines";
  const RealRun real = runReal(words, fontId, linebreak::STORED_HYPHENATED, true, true);
  EXPECT_EQ(kpbreak::tuning().lastResult, kpbreak::Result::Ok);
  expectSane(words, real, P, true, "giant word");
}

// "Just ship shrink" proofs (2026-09-26): the SAME paragraph through the real
// ParsedText, stretch-only (before) and with shrink (after), each drawn by
// TextBlock::render from the TextBlocks the paginator bakes -- the firmware's
// own x positions, not a model's. Albo 14 justified at the X3's 512 px.
// CROSSPOINT_KP_OUT=<dir>; picks the 3 paragraphs whose worst line shrink
// improves most plus 3 drawn at random (seed 20260926) from those it changes,
// 3-12 lines each; writes <dir>/shrink_<idx>_{before,after}.pgm padded to one
// height, and <dir>/shrink_manifest.txt.
TEST(KnuthPlass, DISABLED_ShrinkProof) {
  const auto corpus = loadCorpus();
  if (corpus.empty()) GTEST_SKIP() << "set CROSSPOINT_LINEBREAK_CORPUS";
  const char* outDir = std::getenv("CROSSPOINT_KP_OUT");
  if (!outDir) outDir = ".";
  const int fontId = Env::instance().fontFor("Albo", 14);
  ASSERT_NE(fontId, 0);
  const double sp = Env::instance().renderer().getSpaceAdvance(fontId, 'n', 'n', EpdFontFamily::REGULAR);
  auto run = [&](const std::vector<std::string>& words, const bool shrink) {
    kpbreak::tuning().shrink = shrink;
    RealRun rr = runReal(words, fontId, linebreak::STORED_HYPHENATED, true, true);
    kpbreak::tuning().shrink = true;
    std::vector<Line> lines;
    for (size_t l = 0; l < rr.blocks.size(); ++l) {
      Line L;
      for (uint16_t w = 0; w < rr.blocks[l]->wordCount(); ++w) {
        L.texts.push_back(rr.blocks[l]->wordText(w));
        L.xpos.push_back(rr.blocks[l]->wordXpos(w));
      }
      L.isFinal = l + 1 == rr.blocks.size();
      lines.push_back(std::move(L));
    }
    return std::make_pair(rr, lines);
  };
  struct Cand {
    size_t idx;
    double gain;
  };
  std::vector<Cand> cands;
  for (size_t pi = 0; pi < corpus.size(); ++pi) {
    const auto words = wordsOf(corpus[pi]);
    const Prepared P = prepare(words, fontId, true);
    const auto a = deviceCuts(P, kUnwindowed, nullptr, nullptr, false);
    const auto b = deviceCuts(P, kUnwindowed, nullptr, nullptr, true);
    if (sameCuts(a, b) || a.size() < 4 || a.size() > 13 || b.size() > 13) continue;
    cands.push_back({pi, worstOf(layOut(P, a, true, false), sp) - worstOf(layOut(P, b, true, true), sp)});
  }
  ASSERT_GE(cands.size(), 6u);
  std::vector<Cand> byGain = cands;
  std::sort(byGain.begin(), byGain.end(), [](const Cand& x, const Cand& y) { return x.gain > y.gain; });
  std::vector<size_t> pick = {byGain[0].idx, byGain[1].idx, byGain[2].idx};
  uint32_t seed = 20260926u;
  while (pick.size() < 6) {
    seed = seed * 1664525u + 1013904223u;
    const size_t k = cands[(seed >> 8) % cands.size()].idx;
    if (std::find(pick.begin(), pick.end(), k) == pick.end()) pick.push_back(k);
  }
  char path[512];
  std::snprintf(path, sizeof(path), "%s/shrink_manifest.txt", outDir);
  FILE* man = std::fopen(path, "w");
  ASSERT_NE(man, nullptr);
  std::fprintf(man, "idx,kind,lines_before,lines_after,worst_before,worst_after,hyph_before,hyph_after\n");
  for (size_t n = 0; n < pick.size(); ++n) {
    const auto words = wordsOf(corpus[pick[n]]);
    const auto before = run(words, false).second;
    const auto after = run(words, true).second;
    const Prepared P = prepare(words, fontId, true);
    std::vector<kp::Pos> ca, cb;
    ASSERT_TRUE(cutsFromBlocks(words, run(words, false).first.blocks, ca));
    ASSERT_TRUE(cutsFromBlocks(words, run(words, true).first.blocks, cb));
    const auto la = layOut(P, ca, true, false), lb = layOut(P, cb, true, true);
    const int h = static_cast<int>(std::max(before.size(), after.size()));
    std::snprintf(path, sizeof(path), "%s/shrink_%zu_before.pgm", outDir, pick[n]);
    writeParagraphPgm(before, fontId, words, true, h, path);
    std::snprintf(path, sizeof(path), "%s/shrink_%zu_after.pgm", outDir, pick[n]);
    writeParagraphPgm(after, fontId, words, true, h, path);
    std::fprintf(man, "%zu,%s,%zu,%zu,%.2f,%.2f,%d,%d\n", pick[n], n < 3 ? "largest-gain" : "random", before.size(),
                 after.size(), worstOf(la, sp), worstOf(lb, sp), hyphensOf(la), hyphensOf(lb));
  }
  std::fclose(man);
}

// Adversarial review of shrink, 2026-09-26: three inputs the corpus never
// holds (it is regular-style Latin with ordinary spaces), each of which broke a
// promise before the per-gap caps. Read off the TextBlocks ParsedText bakes.
//   * italic and bold: at Albo 14 their space is 8 px against a regular 9, so a
//     cap taken from the regular space painted a gap at 5/8 of its own space;
//   * CJK: a break with no natural width was given shrink and painted NEGATIVE,
//     so glyphs overlapped;
//   * a no-break space followed by a real one: counted as a gap, painted
//     without the extra, so a shrunk line overflowed the measure.
namespace {
struct Tok {
  std::string text;
  EpdFontFamily::Style style;
  bool attach;
};
std::vector<std::shared_ptr<TextBlock>> layOutToks(const std::vector<Tok>& toks, const int fontId, const int measure) {
  BlockStyle style;
  style.alignment = CssTextAlign::Justify;
  ParsedText block(false, linebreak::STORED_HYPHENATED, false, style);
  for (const auto& t : toks) block.addWord(t.text, t.style, false, t.attach);
  std::vector<std::shared_ptr<TextBlock>> out;
  block.layoutAndExtractLines(
      Env::instance().renderer(), fontId, measure, [&](const std::shared_ptr<TextBlock>& l) { out.push_back(l); }, true,
      0);
  return out;
}
struct Painted {
  int overflowPx = 0;    // worst: right edge past the measure beyond the trailing hang
  int negativeGaps = 0;  // painted gaps below zero
  int underTwoThirds = 0;  // word-space gaps painted under 2/3 of their own style's space
  int shrunkGaps = 0;    // gaps painted narrower than natural: the test is not vacuous
};
Painted inspect(const std::vector<std::shared_ptr<TextBlock>>& lines, const int fontId, const int measure,
                const bool wordSpaces) {
  auto& r = Env::instance().renderer();
  Painted p;
  for (size_t l = 0; l + 1 < lines.size(); ++l) {
    const auto& b = *lines[l];
    const int n = b.wordCount();
    for (int i = 0; i + 1 < n; ++i) {
      const std::string a = b.wordText(i), c = b.wordText(i + 1);
      const int end = b.wordXpos(i) + r.getTextAdvanceX(fontId, a.c_str(), b.wordStyle(i));
      const int gap = b.wordXpos(i + 1) - end;
      p.negativeGaps += gap < 0;
      if (!wordSpaces || a == " " || c == " ") continue;
      const int natural = r.getSpaceAdvance(fontId, lastCp(a), firstCp(c), b.wordStyle(i));
      const int space = r.getSpaceAdvance(fontId, 'n', 'n', b.wordStyle(i));
      p.shrunkGaps += gap < natural;
      if (natural >= space && 3 * gap < 2 * space) p.underTwoThirds++;
    }
    const std::string last = b.wordText(n - 1);
    const int end = b.wordXpos(n - 1) + r.getTextAdvanceX(fontId, last.c_str(), b.wordStyle(n - 1));
    // The trailing hang extractLine grants, in the glyph's OWN style (hangOf
    // measures REGULAR, which undercounts an italic or bold period).
    const std::string g = lastGlyph(last);
    const int hang = r.getTextAdvanceX(fontId, g.c_str(), b.wordStyle(n - 1)) * hangQuarters(firstCp(g), false) / 4;
    p.overflowPx = std::max(p.overflowPx, end - (measure + hang));
  }
  return p;
}
}  // namespace

TEST(KnuthPlassDevice, ShrinkHonorsEveryGapsOwnFloor) {
  // (1) Styled text, where the space differs from the regular one.
  for (const auto& face : std::vector<std::pair<std::string, int>>{{"Albo", 14}, {"LibreFranklin", 14}}) {
    const int fontId = Env::instance().fontFor(face.first, face.second);
    if (fontId == 0) continue;  // Albo needs CROSSPOINT_TEST_SD
    for (const auto st : {EpdFontFamily::ITALIC, EpdFontFamily::BOLD, EpdFontFamily::BOLD_ITALIC}) {
      int shrunk = 0;
      for (int measure = 250; measure <= 512; measure += 23) {
        for (const auto& text : builtinParagraphs()) {
          std::vector<Tok> toks;
          for (const auto& w : wordsOf(text)) toks.push_back({w, st, false});
          const auto lines = layOutToks(toks, fontId, measure);
          const Painted p = inspect(lines, fontId, measure, true);
          EXPECT_EQ(p.underTwoThirds, 0) << face.first << " style " << int(st) << " at " << measure;
          EXPECT_LE(p.overflowPx, 0) << face.first << " style " << int(st) << " at " << measure;
          EXPECT_EQ(p.negativeGaps, 0);
          // Not the greedy fallback: an all-italic SD-font paragraph used to be
          // refused (a regular space of 0 read as Invalid).
          EXPECT_EQ(kpbreak::tuning().lastResult, kpbreak::Result::Ok) << face.first << " style " << int(st);
          shrunk += p.shrunkGaps;
        }
      }
      printf("[styled] %s %d style %d: %d gaps painted narrower than natural\n", face.first.c_str(), face.second,
             int(st), shrunk);
      EXPECT_GT(shrunk, 0) << face.first << " style " << int(st) << ": nothing shrank, the check is vacuous";
    }
  }
  const int lf = Env::instance().fontFor("LibreFranklin", 14);
  ASSERT_NE(lf, 0);
  // (2) CJK: no break with no natural width may be painted narrower than zero.
  {
    // One 600-character run (addWord splits it into no-space-before breaks),
    // and CJK glued to Latin, the review's two probes.
    std::string run;
    for (int i = 0; i < 300; ++i) run += "\xE4\xB8\xAD\xE6\x96\x87";
    std::vector<Tok> mix;
    for (int i = 0; i < 150; ++i) {
      mix.push_back({"\xE4\xB8\xAD" "abc", EpdFontFamily::REGULAR, false});
      mix.push_back({"def\xE6\x96\x87", EpdFontFamily::REGULAR, true});
    }
    const std::vector<Tok> single = {{run, EpdFontFamily::REGULAR, false}};
    for (const std::vector<Tok>* toks : {&single, static_cast<const std::vector<Tok>*>(&mix)}) {
      for (const int measure : {512, 400}) {
        const auto lines = layOutToks(*toks, lf, measure);
        ASSERT_GT(lines.size(), 3u);
        EXPECT_EQ(inspect(lines, lf, measure, false).negativeGaps, 0) << "CJK glyphs overlap at " << measure;
      }
    }
  }
  // (3) "Mr.&nbsp; Smith": a no-break space token, then an ordinary word.
  {
    std::vector<Tok> toks;
    int n = 0;
    for (const auto& text : builtinParagraphs()) {
      for (const auto& w : wordsOf(text)) {
        toks.push_back({w, EpdFontFamily::REGULAR, false});
        if (++n % 3 == 0) toks.push_back({" ", EpdFontFamily::REGULAR, true});
      }
    }
    for (const int measure : {512, 400, 300}) {
      const auto lines = layOutToks(toks, lf, measure);
      EXPECT_LE(inspect(lines, lf, measure, false).overflowPx, 0) << "an NBSP line overflows at " << measure;
    }
  }
}


