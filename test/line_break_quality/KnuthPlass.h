// Knuth-Plass total-fit line breaking, HOST-ONLY.
//
// Written 2026-09-25 as experiment E3 of the layout research plan
// (crosspoint-simulator/docs/research-claude-for-kerning-and-layout-2026-09-24.md
// section 3f / 4): "the missing cell" of docs/line-breaking-2026-08-25.md --
// total fit WITH hyphen points, which neither shipped breaker is. The shipped
// DP (ParsedText::computeLineBreaks) cannot weigh a hyphen candidate because
// hyphenation in ParsedText is destructive (section 1 of that doc); this one
// never mutates anything. It is compiled only into test/line_break_quality and
// is NOT wired into the device renderer. The measured verdict is in
// docs/knuth-plass-line-breaking-2026-09-25.md.
//
// THE MODEL, stated because every number in the doc depends on it:
//
//   * A paragraph is a list of WORDS (the same tokens ParsedText::addWord makes
//     for space-separated Latin text: one token per word, continues=false).
//   * A POSITION is where a line may begin: (word, byteOffset). (0,0) is the
//     paragraph start, (k+1,0) the space after word k, (k,o) with o>0 a
//     hyphenation point inside word k, (n,0) the paragraph end. Positions are
//     ordered, so a line is any pair (i,j) of positions with i<j.
//   * WIDTHS ARE NOT ADDITIVE ACROSS A SPLIT (the kern at the split point is
//     lost, the inserted hyphen is gained), so a line's width is assembled
//     from MEASURED pieces -- the suffix a line starts with, full words, the
//     prefix-plus-hyphen it ends with -- and the natural inter-word gaps
//     (space advance with its kerning), exactly as ParsedText measures them.
//     The caller supplies those widths from the firmware's own GfxRenderer.
//   * Boxes/glue/penalties in Knuth's sense: each word boundary is glue whose
//     natural width is the kerned space, stretchable by `stretchPerGapPx` and
//     (optionally) shrinkable by `shrinkPerGapPx`; each hyphenation point is a
//     FLAGGED penalty of `hyphenPenalty` whose width (the hyphen) exists only
//     if the break is taken; the paragraph ends in infinitely stretchable
//     \parfillskip, so the last line has badness 0.
//   * badness = 100 |r|^3 with r the adjustment ratio. UNCAPPED -- TeX clamps
//     at 10000, which throws away exactly the distinction between a bad line
//     and a terrible one that the worst-line metric is about. No tolerance:
//     every line that fits is feasible, so there is always a solution.
//   * demerits = (linePenalty + b)^2 + p^2, + doubleHyphenDemerits for two
//     flagged breaks in a row, + finalHyphenDemerits when the second-to-last
//     line ends in a hyphen, + adjDemerits when adjacent lines' fitness classes
//     (tight / decent / loose / very loose, TeX's boundaries at r = -0.5, 0.5,
//     1) differ by more than one. Looseness 0.
//   * Ragged mode is TeX's \raggedright: inter-word glue is rigid and each line
//     ends in glue stretchable by `raggedStretchPx`, so badness measures the
//     rag and the breaker evens the right edge instead of the word spaces.
//
// THE IMPLEMENTATION is the array form of the algorithm: best[j][fitness][run]
// over positions, each line end looking back only as far as a line can reach
// (the lookback stops at the first start that overflows). With no tolerance
// that is exactly the optimum the active-list form finds, and it is the shape
// a device port would take (a fixed table of 4 x (maxConsecutiveHyphens+1)
// states per position, no allocation per node). Cost is O(positions x
// positions-per-line x states^2 / 4).

#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

namespace kp {

struct Params {
  bool justified = true;
  double stretchPerGapPx = 0.0;  // justified: how far one gap may stretch at r = 1
  double shrinkPerGapPx = 0.0;   // justified: 0 = never shrink (what extractLine can paint today)
  double raggedStretchPx = 0.0;  // ragged: the right-edge glue's stretch at r = 1
  double linePenalty = 10.0;     // TeX \linepenalty
  double hyphenPenalty = 50.0;   // TeX \hyphenpenalty
  double doubleHyphenDemerits = 10000.0;
  double finalHyphenDemerits = 5000.0;
  double adjDemerits = 10000.0;
  // 0 = no cap (the default here, see the header). 10000 = TeX's own clamp.
  double badnessCap = 0.0;
  // 0 = unlimited. N > 0 = a HARD limit of N hyphenated lines in a row (the
  // "hyphen ladder" rule every composing program but TeX has). Implemented as
  // a third state dimension, so it costs (N+1)x the table and the work.
  int maxConsecutiveHyphens = 0;
};

struct Pos {
  int word = 0;    // the word this line starts in
  int offset = 0;  // byte offset inside it; 0 = at the word's start
  bool hyphen = false;  // a line ENDING here ends with an inserted '-'
  bool flagged = false; // a line ending here ends inside a word (TeX's flagged penalty)
};

// What the breaker needs to know about a paragraph. Every width is an integer
// pixel count measured by the caller with the firmware's renderer.
struct Paragraph {
  int words = 0;
  std::vector<int> full;      // full[k]: advance of word k
  std::vector<int> gapAfter;  // gapAfter[k]: natural gap between word k and k+1 (kerned space)
  // piece(k, from, to, hyphen): advance of bytes [from,to) of word k, with a
  // '-' appended when `hyphen`. to == -1 means "to the end of the word".
  std::function<int(int, int, int, bool)> piece;
  std::vector<Pos> positions;  // ordered; [0] = start, back() = end
  int measurePx = 0;
  int firstLineIndentPx = 0;
};

struct LineGeom {
  int width = 0;  // natural width, px
  int gaps = 0;   // stretchable gaps
};

// Natural width and gap count of the line that starts at position i and ends
// at position j.
inline LineGeom lineGeom(const Paragraph& p, const std::vector<long long>& fullSum,
                         const std::vector<long long>& gapSum, const int i, const int j) {
  const Pos& a = p.positions[i];
  const Pos& b = p.positions[j];
  const int lastW = b.offset > 0 ? b.word : b.word - 1;
  LineGeom g;
  if (lastW < a.word) return g;  // empty line; never produced for i<j
  if (lastW == a.word) {
    const int to = b.offset > 0 ? b.offset : -1;
    g.width = (a.offset == 0 && to == -1) ? p.full[a.word] : p.piece(a.word, a.offset, to, b.hyphen);
    return g;
  }
  const int first = a.offset == 0 ? p.full[a.word] : p.piece(a.word, a.offset, -1, false);
  const int last = b.offset > 0 ? p.piece(b.word, 0, b.offset, b.hyphen) : p.full[lastW];
  g.width = first + static_cast<int>(fullSum[lastW] - fullSum[a.word + 1]) + last +
            static_cast<int>(gapSum[lastW] - gapSum[a.word]);
  g.gaps = lastW - a.word;
  return g;
}

struct Stats {
  int positions = 0;
  long long evaluations = 0;  // (start, end) pairs whose width was computed
  size_t tableBytes = 0;      // best[] + prev[] as this host build lays them out
};

inline int fitnessOf(const double r) {
  if (r < -0.5) return 0;
  if (r <= 0.5) return 1;
  if (r <= 1.0) return 2;
  return 3;
}

// Returns the chosen line ENDS as indices into p.positions (the last is always
// p.positions.size()-1).
inline std::vector<int> breakParagraph(const Paragraph& p, const Params& prm, Stats* stats = nullptr) {
  const int m = static_cast<int>(p.positions.size()) - 1;
  std::vector<long long> fullSum(p.words + 1, 0), gapSum(p.words + 1, 0);
  for (int k = 0; k < p.words; ++k) {
    fullSum[k + 1] = fullSum[k] + p.full[k];
    gapSum[k + 1] = gapSum[k] + (k + 1 < p.words ? p.gapAfter[k] : 0);
  }
  constexpr double INF = std::numeric_limits<double>::infinity();
  constexpr double OVERFULL_BADNESS = 1e7;  // a single piece wider than the measure
  constexpr double NO_GAP_BADNESS = 1e6;    // a one-word line short of the measure: cannot justify
  // State = (position, fitness class, hyphenated lines in a row ending here).
  const int R = prm.maxConsecutiveHyphens > 0 ? prm.maxConsecutiveHyphens + 1 : 1;
  const int S = 4 * R;
  const auto idx = [S, R](const int pos, const int fit, const int run) { return pos * S + fit * R + run; };
  std::vector<double> best(static_cast<size_t>(m + 1) * S, INF);
  std::vector<int32_t> prev(static_cast<size_t>(m + 1) * S, -1);
  best[idx(0, 1, 0)] = 0.0;  // the paragraph starts "decent", as TeX's does
  long long evals = 0;

  for (int j = 1; j <= m; ++j) {
    const bool isEnd = j == m;
    bool any = false;
    for (int pass = 0; pass < 2 && !any; ++pass) {
      // pass 0: every start that fits. pass 1 (only when pass 0 found none):
      // the immediately preceding position, overfull -- the one-piece line the
      // shipped breakers also force when a word will not fit.
      for (int i = j - 1; i >= 0; --i) {
        if (pass == 1 && i < j - 1) break;
        const LineGeom g = lineGeom(p, fullSum, gapSum, i, j);
        ++evals;
        const int avail = p.measurePx - (i == 0 ? p.firstLineIndentPx : 0);
        const double slack = static_cast<double>(avail - g.width);
        double badness = 0.0, r = 0.0;
        if (pass == 1) {
          badness = OVERFULL_BADNESS;
        } else if (isEnd) {
          if (slack < 0) break;  // the last line is never justified, so never shrunk
        } else if (prm.justified) {
          if (slack >= 0) {
            if (g.gaps > 0) {
              r = slack / (g.gaps * prm.stretchPerGapPx);
              badness = 100.0 * r * r * r;
            } else if (slack > 0) {
              r = 10.0;
              badness = NO_GAP_BADNESS;
            }
          } else {
            if (prm.shrinkPerGapPx <= 0 || g.gaps == 0) break;
            r = slack / (g.gaps * prm.shrinkPerGapPx);
            if (r < -1.0) break;  // shrinks no further than its shrink
            badness = 100.0 * -r * -r * -r;
          }
        } else {
          if (slack < 0) break;
          r = slack / prm.raggedStretchPx;
          badness = 100.0 * r * r * r;
        }
        if (prm.badnessCap > 0 && pass == 0) badness = std::min(badness, prm.badnessCap);
        const int fit = isEnd ? 1 : fitnessOf(r);
        const Pos& endPos = p.positions[j];
        const bool endHyph = endPos.flagged && !isEnd;
        const double pen = endHyph ? prm.hyphenPenalty : 0.0;
        double d = (prm.linePenalty + badness) * (prm.linePenalty + badness) + pen * pen;
        const bool startFlagged = p.positions[i].flagged && i != 0;
        if (endHyph && startFlagged) d += prm.doubleHyphenDemerits;
        if (isEnd && startFlagged) d += prm.finalHyphenDemerits;
        for (int c = 0; c < 4; ++c) {
          for (int run = 0; run < R; ++run) {
            const double base = best[idx(i, c, run)];
            if (base == INF) continue;
            int nrun = 0;
            if (R > 1 && endHyph) {
              nrun = run + 1;
              if (nrun >= R) continue;  // would make a ladder longer than allowed
            }
            double total = base + d;
            if (std::abs(c - fit) > 1) total += prm.adjDemerits;
            if (total < best[idx(j, fit, nrun)]) {
              best[idx(j, fit, nrun)] = total;
              prev[idx(j, fit, nrun)] = idx(i, c, run);
              any = true;
            }
          }
        }
      }
    }
  }

  // Trace back from the cheapest state at the end.
  int state = -1;
  double bestTotal = INF;
  for (int k = 0; k < S; ++k) {
    if (best[static_cast<size_t>(m) * S + k] < bestTotal) {
      bestTotal = best[static_cast<size_t>(m) * S + k];
      state = m * S + k;
    }
  }
  std::vector<int> ends;
  while (state >= S) {  // states 0..S-1 are the paragraph start
    ends.push_back(state / S);
    state = prev[state];
  }
  std::vector<int> out(ends.rbegin(), ends.rend());
  if (stats) {
    stats->positions = m + 1;
    stats->evaluations = evals;
    stats->tableBytes = best.size() * sizeof(double) + prev.size() * sizeof(int32_t);
  }
  return out;
}

}  // namespace kp
