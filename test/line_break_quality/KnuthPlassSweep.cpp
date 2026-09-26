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
      extra = -((-spare + n - 1) / n);  // round AWAY from zero so the line fits
    }
    int x = indent - leadHang;
    double gapSum = 0.0;
    for (size_t i = 0; i < L.texts.size(); ++i) {
      L.xpos.push_back(static_cast<int16_t>(x));
      if (i + 1 < L.texts.size()) {
        const int g = gaps[i] + extra;
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
        const auto model = layOut(P, cuts, justified);
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

kpbreak::Config deviceConfig(const Prepared& P, const int windowPositions = kpbreak::WINDOW_POSITIONS) {
  kpbreak::Config c;
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
                                kpbreak::Stats* st = nullptr) {
  const ProtoModel m(P.para);
  std::vector<kpbreak::Cut> cuts;
  const kpbreak::Result r = kpbreak::breakParagraph(m, deviceConfig(P, windowPositions), cuts, st);
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

// Every word set once (the TextBlocks rebuild the source) and no non-final
// line wider than the measure, read off what ParsedText actually baked.
void expectSane(const std::vector<std::string>& words, const RealRun& real, const Prepared& P, const bool justified,
                const std::string& label) {
  std::vector<kp::Pos> cuts;
  ASSERT_TRUE(cutsFromBlocks(words, real.blocks, cuts)) << label << ": the lines do not rebuild the source text";
  const auto lines = layOut(P, cuts, justified);
  ASSERT_EQ(mismatchedLines(lines, real.blocks), 0) << label;
  for (size_t l = 0; l < lines.size(); ++l) {
    EXPECT_LE(lines[l].natural, kMeasure - (l == 0 ? P.para.firstLineIndentPx : 0))
        << label << ": line " << l << " overflows";
  }
}

std::string repeatedParagraph(const int copies) {
  std::string t;
  for (int c = 0; c < copies; ++c)
    for (const auto& p : builtinParagraphs()) t += p + " ";
  t.pop_back();
  return t;
}

}  // namespace

TEST(KnuthPlassDevice, PureBreakerMatchesThePrototypeCutForCut) {
  int paragraphs = 0, hyphenated = 0;
  for (const int pt : {12, 14, 18}) {
    const int fontId = Env::instance().fontFor("LibreFranklin", pt);
    ASSERT_NE(fontId, 0);
    for (const auto& text : builtinParagraphs()) {
      const Prepared P = prepare(wordsOf(text), fontId, true);
      const auto proto = kpCuts(P, candidateParams(fontId, true, false), nullptr, nullptr);
      kpbreak::Result r;
      const auto dev = deviceCuts(P, kUnwindowed, &r);
      ASSERT_EQ(r, kpbreak::Result::Ok);
      EXPECT_TRUE(sameCuts(proto, dev)) << "LF " << pt << "\n proto " << cutsText(proto) << "\n dev   "
                                        << cutsText(dev);
      for (const auto& c : dev) hyphenated += c.offset > 0;
      paragraphs++;
    }
  }
  EXPECT_EQ(paragraphs, 12);
  EXPECT_GT(hyphenated, 0) << "no hyphenated line in the fixture: the flagged-penalty half is untested";
}

TEST(KnuthPlassDevice, ParsedTextMatchesThePrototypeOnJustifiedBlocks) {
  int lines = 0, hyphenated = 0;
  for (const int pt : {12, 14, 18}) {
    const int fontId = Env::instance().fontFor("LibreFranklin", pt);
    ASSERT_NE(fontId, 0);
    for (const auto& text : builtinParagraphs()) {
      const auto words = wordsOf(text);
      const Prepared P = prepare(words, fontId, true);
      const auto proto = kpCuts(P, candidateParams(fontId, true, false), nullptr, nullptr);
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
  const ProtoModel m(P.para);
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
  const auto proto = kpCuts(P, candidateParams(fontId, true, false), nullptr, nullptr);
  std::vector<kp::Pos> got;
  ASSERT_TRUE(cutsFromBlocks(words, real.blocks, got));
  const double s = Env::instance().renderer().getSpaceAdvance(fontId, 'n', 'n', EpdFontFamily::REGULAR);
  const double protoWorst = worstOf(layOut(P, proto, true), s);
  const double gotWorst = worstOf(layOut(P, got, true), s);
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
      const auto lines = layOut(Q, dev, true);
      std::string rebuilt;
      for (size_t l = 0; l < lines.size(); ++l) {
        EXPECT_LE(lines[l].natural, kMeasure - (l == 0 ? Q.para.firstLineIndentPx : 0)) << "window " << w;
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
    size_t peak = 0;
    for (size_t pi = 0; pi < corpus.size(); ++pi) {
      const auto words = wordsOf(corpus[pi]);
      const Prepared P = prepare(words, fontId, true);
      maxPos = std::max(maxPos, static_cast<int>(P.para.positions.size()));
      double protoUs = 0;
      const auto proto = kpCuts(P, candidateParams(fontId, true, false), nullptr, &protoUs);
      protoDpUs += protoUs;
      const auto t0 = std::chrono::steady_clock::now();
      kpbreak::Result r;
      const auto pure = deviceCuts(P, kUnwindowed, &r);
      devDpUs += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
      ASSERT_EQ(r, kpbreak::Result::Ok) << pi;
      if (!sameCuts(proto, pure)) {
        if (pureDiff < 5)
          ADD_FAILURE() << fam << " " << pt << " paragraph " << pi << " pure\n proto " << cutsText(proto)
                        << "\n dev   " << cutsText(pure);
        pureDiff++;
      }
      const RealRun g = runReal(words, fontId, linebreak::STORED_HYPHENATED, true, false);
      greedyUs += g.micros;
      const RealRun k = runReal(words, fontId, linebreak::STORED_HYPHENATED, true, true);
      kpUs += k.micros;
      maxKpUs = std::max(maxKpUs, k.micros);
      ASSERT_EQ(kpbreak::tuning().lastResult, kpbreak::Result::Ok) << pi;
      peak = std::max(peak, kpbreak::tuning().lastStats.peakBytes);
      expectSane(words, k, P, true, fam + " paragraph " + std::to_string(pi));
      std::vector<kp::Pos> got;
      ASSERT_TRUE(cutsFromBlocks(words, k.blocks, got));
      lines += static_cast<int>(got.size()) - 1;
      for (const auto& q : got) hyph += q.offset > 0;
      const bool oneWindow = static_cast<int>(P.para.positions.size()) <= kpbreak::WINDOW_POSITIONS + 1;
      if (!oneWindow) windowed++;
      if (!sameCuts(proto, got)) {
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
        const double a = worstOf(layOut(P, got, true), sp), b = worstOf(layOut(P, proto, true), sp);
        seamWorst += a;
        protoWorst += b;
        seamWorse += a > b + 1e-9;
        seamHyph += hyphensOf(layOut(P, got, true));
        protoHyph += hyphensOf(layOut(P, proto, true));
      }
    }
    const double n = static_cast<double>(corpus.size());
    printf("[corpus] %s %d: %zu paragraphs, %d lines, %d hyphenated; pure differs %d, ParsedText differs %d "
           "(one window); %d paragraphs past the window, %d of them differ; max positions %d; peak %zu B\n",
           fam.c_str(), pt, corpus.size(), lines, hyph, pureDiff, parsedDiff, windowed, windowedDiff, maxPos, peak);
    if (windowed > 0)
      printf("[corpus] %s %d windowed paragraphs: mean worst line %.3f vs unwindowed %.3f spaces, %d of %d worse; "
             "hyphenated lines %d vs %d\n",
             fam.c_str(), pt, seamWorst / windowed, protoWorst / windowed, seamWorse, windowed, seamHyph, protoHyph);
    printf("[corpus] %s %d host timing per paragraph: ParsedText greedy %.1f us, ParsedText Knuth-Plass %.1f us "
           "(max %.0f us); DP alone: prototype (double) %.1f us, device (int64) %.1f us\n",
           fam.c_str(), pt, greedyUs / n, kpUs / n, maxKpUs, protoDpUs / n, devDpUs / n);
    EXPECT_EQ(pureDiff, 0);
    EXPECT_EQ(parsedDiff, 0);
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
