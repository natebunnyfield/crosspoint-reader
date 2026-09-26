// Knuth-Plass total-fit line breaking for JUSTIFIED blocks -- the DEVICE port.
//
// Owner ruling 2026-09-26, "go with k-p": option B of
// docs/knuth-plass-line-breaking-2026-09-25.md section 7. On a justified block
// that would otherwise go to the greedy hyphenating breaker
// (ParsedText::computeHyphenatedLineBreaks), this chooses the breaks instead,
// with exactly the "candidate" parameters the owner saw in the blind test:
//
//   * stretch-only: a gap stretches by half a word space at r = 1 and NEVER
//     shrinks, because extractLine cannot paint a narrowed gap
//     (computeJustifyExtra returns 0 on a negative spare);
//   * uncapped badness 100 |r|^3 (the doc's section 4: TeX's 10,000 cap is
//     what made TeX-as-shipped lose to greedy on the worst line);
//   * line penalty 10, hyphen penalty 10,000, double-hyphen demerits 10^6,
//     final-hyphen demerits 5,000, adjacent-fitness demerits 10,000 over TeX's
//     fitness classes;
//   * at most TWO hyphenated lines in a row, as a hard constraint.
//
// The host prototype it was ported from, and against which it is tested cut
// for cut, is test/line_break_quality/KnuthPlass.h. Differences from it are
// deliberate and listed here so nobody "fixes" them back:
//
//   1. INTEGER ARITHMETIC. The ESP32-C3 has no FPU, so the prototype's doubles
//      would be soft-float. Badness is carried in sixteenths (b16 = 1600 r^3,
//      rounded) and demerits in 1/256ths, both int64. The one cube is
//      12800 * slack^3 / (gaps * space)^3, exact in int64 for any slack under
//      32,768 px. b16 is clamped at B16_MAX (badness ~3.1e7, r ~ 68: a gap
//      stretched by 34 word spaces); every path total is a SATURATING add.
//      Above the clamp two lines tie that the prototype would order -- both
//      are far past anything either breaker sets on a real page (the corpus
//      worst is r ~ 25).
//   2. Fitness class 0 ("tight", r < -0.5) is dropped: with no shrink, r is
//      never negative, so that class can never hold a finite value. Nine
//      states per position rather than twelve; the result is identical.
//   3. A WINDOW. The DP is run over at most WINDOW_POSITIONS break positions
//      (and WINDOW_TOKENS tokens) at a time. A paragraph that fits in one
//      window gets the true total-fit optimum. A longer one is solved window
//      by window: the best path to the window's end is traced, the lines
//      ending in the window's first half are COMMITTED, and the next window
//      starts from the last committed break with that break's (fitness,
//      hyphen-run) state carried over. Global optimality is given up only
//      across those seams. The cap exists to bound the transient working set
//      (below), which the doc's section 6b flagged as the real risk.
//   4. The general token stream. ParsedText's tokens are not only
//      space-separated words: a continuation token (no-break space, attached
//      punctuation, a focus-reading suffix) may not be broken before; a
//      no-space-before token (CJK) is a break opportunity whose gap has no
//      natural width but still stretches. Both are modeled exactly as
//      extractLine lays them out. For the plain Latin text the prototype
//      handles, the two models coincide.
//
// MEMORY. The DP's own arrays are allocated per call with malloc/realloc
// (never the throwing operator new: with -fno-exceptions that aborts) and
// freed on return; a failure there returns AllocFailed. NOT covered by that:
// the output cut list (a std::vector, one entry per line), and the model's own
// small temporaries -- in ParsedText, Hyphenator::breakOffsets' vector and the
// substring copies measured per hyphen point, the same kinds of allocation
// greedy makes, though made for every word rather than one per line. Those
// can still abort on a heap that cannot give tens of bytes, as greedy would.
// The arrays are kept (not freed) between windows, and a realloc may briefly
// hold old and new blocks together. Per window of P positions and T tokens:
//     positions      10 B x P
//     best (int64)   72 B x P   (9 states)
//     prev (uint16)  18 B x P
//     traced path     2 B x P
//     token sums     10 B x T   (+4 B)
// At the caps (P = 321, T = 640) that is 3,210 + 23,112 + 5,778 + 642 +
// 6,404 = 39,146 bytes, the WORST CASE (KnuthPlassDevice.TheWindowBounds...
// computes the same sum). Measured 2026-09-26: the largest working set on the
// owner's 1,472-paragraph corpus was 35,596 B, and a 104-position paragraph
// (the corpus mean) holds about 12 KB. Any allocation failure returns Result::AllocFailed with
// nothing mutated, and the caller falls back to greedy. So does a paragraph
// the DP cannot find a path through (NoPath) and any malformed input.
//
// Header-only on purpose: the simulator's iOS build compiles this firmware
// from a generated list of translation units, and a new .cpp here would break
// it until someone regenerates that list in the other repository.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <vector>

namespace kpbreak {

// ---------------------------------------------------------------------------
// The candidate's parameters (doc section 2). Integers because the prototype's
// are integral too; only badness needed a scale.
// ---------------------------------------------------------------------------
inline constexpr int64_t LINE_PENALTY = 10;
inline constexpr int64_t HYPHEN_PENALTY = 10000;
inline constexpr int64_t DOUBLE_HYPHEN_DEMERITS = 1000000;
inline constexpr int64_t FINAL_HYPHEN_DEMERITS = 5000;
inline constexpr int64_t ADJ_DEMERITS = 10000;
inline constexpr int MAX_CONSECUTIVE_HYPHENS = 2;

// Badness of the two forced cases, as the prototype's OVERFULL_BADNESS and
// NO_GAP_BADNESS.
inline constexpr int64_t OVERFULL_BADNESS = 10000000;
inline constexpr int64_t NO_GAP_BADNESS = 1000000;

// Fixed point: badness in sixteenths, so demerits are in 1/256ths.
inline constexpr int64_t B_SCALE = 16;
inline constexpr int64_t D_SCALE = B_SCALE * B_SCALE;
// (LINE_PENALTY*16 + B16_MAX)^2 = 2.5e17: one line, with every penalty added,
// stays under 2^58, so the per-line sum cannot overflow before saturation.
inline constexpr int64_t B16_MAX = 500000000;

inline constexpr int64_t INF = std::numeric_limits<int64_t>::max();
inline constexpr int64_t SATURATED = INF - 1;

// Hyphen-run dimension: runs 0..MAX_CONSECUTIVE_HYPHENS, so R = 3.
inline constexpr int RUNS = MAX_CONSECUTIVE_HYPHENS + 1;
// Fitness classes 1..3 (decent, loose, very loose) -- see note 2 above.
inline constexpr int FITS = 3;
inline constexpr int STATES = FITS * RUNS;

// The window. 320 positions is about 250 words of English at the corpus's
// measured 1.3 break positions per word (104 per 79.5-word paragraph).
inline constexpr int WINDOW_POSITIONS = 320;
inline constexpr int WINDOW_TOKENS = 640;

inline constexpr uint16_t NO_PREV = 0xFFFF;

enum class Result : uint8_t { Ok, AllocFailed, NoPath, Invalid };

// A chosen line END. `offset` 0 = the line ends after token word-1 (so the
// next line starts at token `word`); offset > 0 = the line ends inside token
// `word` at that byte, with a '-' appended when `hyphen`. The last cut is
// always {tokenCount, 0}.
struct Cut {
  uint16_t word = 0;
  uint16_t offset = 0;
  bool hyphen = false;
};

struct Config {
  int measure = 0;
  int firstLineIndent = 0;
  int spaceAdvance = 0;  // the face's word space, getSpaceAdvance('n','n')
  int windowPositions = WINDOW_POSITIONS;
  int windowTokens = WINDOW_TOKENS;
  // Test hook: 0 = no limit. Otherwise any allocation that would take the
  // working set past this many bytes fails, exactly as a full heap would.
  size_t allocLimitBytes = 0;
};

struct Stats {
  int positions = 0;  // break positions generated (window overlaps counted again)
  int windows = 0;
  long long evaluations = 0;  // (start, end) line widths computed
  size_t peakBytes = 0;       // largest working set held at once
};

namespace detail {

struct Pos {
  uint16_t word;
  uint16_t offset;
  uint16_t prefixW;  // offset > 0: bytes [0, offset) of the word, plus '-' when hyphen
  uint16_t suffixW;  // offset > 0: bytes [offset, end) of the word
  uint8_t hyphen;
};

// A malloc-backed array of trivially copyable T. Never throws, never aborts:
// reserve() reports failure and the caller falls back to greedy.
template <typename T>
class PodBuf {
 public:
  PodBuf() = default;
  PodBuf(const PodBuf&) = delete;
  PodBuf& operator=(const PodBuf&) = delete;
  ~PodBuf() { std::free(p_); }
  bool reserve(const size_t n, size_t& heldBytes, const size_t limit) {
    if (n <= cap_) return true;
    const size_t grow = (n - cap_) * sizeof(T);
    if (limit != 0 && heldBytes + grow > limit) return false;
    void* q = std::realloc(p_, n * sizeof(T));
    if (q == nullptr) return false;
    p_ = static_cast<T*>(q);
    heldBytes += grow;
    cap_ = n;
    return true;
  }
  T& operator[](const size_t i) { return p_[i]; }
  const T& operator[](const size_t i) const { return p_[i]; }
  size_t capacity() const { return cap_; }

 private:
  T* p_ = nullptr;
  size_t cap_ = 0;
};

inline int64_t satAdd(const int64_t a, const int64_t b) {
  // Both operands are >= 0 here; INF never reaches an add.
  return a > SATURATED - b ? SATURATED : a + b;
}

// b16 = round(1600 r^3) with r = slack / (gaps * space / 2) = 2 slack / G.
inline int64_t badness16(const int slack, const int64_t g) {
  const int64_t s = std::min(slack, 32767);
  const int64_t num = 12800 * s * s * s;
  const int64_t den = g * g * g;
  if (den <= 0) return B16_MAX;
  const int64_t b = (num + den / 2) / den;
  return b > B16_MAX ? B16_MAX : b;
}

// TeX's classes at r = 0.5 and 1, exactly: r <= 0.5 <=> 4 slack <= G.
inline int fitnessOf(const int slack, const int64_t g) {
  if (4 * static_cast<int64_t>(slack) <= g) return 1;
  if (2 * static_cast<int64_t>(slack) <= g) return 2;
  return 3;
}

}  // namespace detail

// The model a caller supplies. M must provide:
//   int  tokenCount() const;
//   int  fullWidth(int k) const;          // advance of token k
//   int  gapBefore(int k) const;          // k >= 1: natural px between k-1 and k on one line
//   bool gapStretches(int k) const;       // k >= 1: does that gap take justification
//   bool mayBreakBefore(int k) const;     // 1 <= k < n
//   template <class F> void forEachHyphenPoint(int k, bool includeFallback, F&& f) const;
//        -> f(int offset, bool hyphen, int prefixWidth, int suffixWidth), ascending,
//           unique, 0 < offset < token bytes
//   int  pieceWidth(int k, int from, int to, bool hyphen) const;  // bytes [from,to) of k
namespace detail {
template <typename M>
Result breakParagraphImpl(const M& m, const Config& cfg, std::vector<Cut>& out, Stats* stats);
}  // namespace detail

// On any result but Ok, `out` is left EMPTY -- including a failure in a later
// window, after earlier windows had committed cuts.
template <typename M>
Result breakParagraph(const M& m, const Config& cfg, std::vector<Cut>& out, Stats* stats = nullptr) {
  const Result r = detail::breakParagraphImpl(m, cfg, out, stats);
  if (r != Result::Ok) out.clear();
  return r;
}

template <typename M>
Result detail::breakParagraphImpl(const M& m, const Config& cfg, std::vector<Cut>& out, Stats* stats) {
  using detail::Pos;
  out.clear();
  const int n = m.tokenCount();
  // A traced cell is (position * STATES + state) in a uint16, so a window can
  // hold at most 65535 / STATES positions; the shipped 320 is far inside.
  if (n <= 0 || n > 0xFFFE || cfg.spaceAdvance <= 0 || cfg.measure <= 0 || cfg.windowPositions < 2 ||
      cfg.windowPositions >= 0xFFFF / STATES - 1 || cfg.windowTokens < 1) {
    return Result::Invalid;
  }

  size_t held = 0;
  size_t peak = 0;
  const size_t limit = cfg.allocLimitBytes;
  const int posCap = cfg.windowPositions + 1;
  detail::PodBuf<Pos> pos;
  detail::PodBuf<int64_t> best;
  detail::PodBuf<uint16_t> prev;
  detail::PodBuf<int32_t> fullSum;   // fullSum[t] = sum of full widths of window tokens [0, t)
  detail::PodBuf<int32_t> gapSum;    // gapSum[t] = sum of natural gaps before window tokens [1, t]
  detail::PodBuf<uint16_t> stretch;  // stretch[t] = stretchable gaps before window tokens [1, t]

  detail::PodBuf<uint16_t> path;  // the traced path's cells, one per line

  Pos start{0, 0, 0, 0, 0};
  // State index = (fitness - 1) * RUNS + run. The paragraph starts "decent"
  // (fitness 1) with no hyphen run, as TeX's does: index 0.
  int startState = 0;
  bool firstWindow = true;
  Stats st;

  while (true) {
    // ---- 1. The window's break positions --------------------------------
    int P = 0;
    bool reachedEnd = false;
    // Grown geometrically (16, 32, ... capped at posCap), so a short
    // paragraph never pays for the whole window.
    const auto push = [&](const Pos& p) -> bool {
      if (static_cast<size_t>(P) >= pos.capacity()) {
        const size_t want = std::min<size_t>(posCap, std::max<size_t>(16, pos.capacity() * 2));
        if (!pos.reserve(want, held, limit)) return false;
      }
      pos[P++] = p;
      return true;
    };
    if (!push(start)) return Result::AllocFailed;
    const int w0 = start.word;
    int k = w0;
    bool allocFailed = false;
    while (P < posCap) {
      const int avail = cfg.measure - (k == 0 ? cfg.firstLineIndent : 0);
      const bool fallback = m.fullWidth(k) > avail;
      const int minOffset = k == start.word ? start.offset : 0;
      m.forEachHyphenPoint(k, fallback, [&](const int off, const bool hy, const int pw, const int sw) {
        if (allocFailed || P >= posCap || off <= minOffset || off > 0xFFFF) return;
        const Pos p{static_cast<uint16_t>(k), static_cast<uint16_t>(off),
                    static_cast<uint16_t>(std::clamp(pw, 0, 0xFFFF)), static_cast<uint16_t>(std::clamp(sw, 0, 0xFFFF)),
                    static_cast<uint8_t>(hy ? 1 : 0)};
        if (!push(p)) allocFailed = true;
      });
      if (allocFailed) return Result::AllocFailed;
      if (P >= posCap) break;
      if (k + 1 == n) {
        if (!push(Pos{static_cast<uint16_t>(n), 0, 0, 0, 0})) return Result::AllocFailed;
        reachedEnd = true;
        break;
      }
      if (m.mayBreakBefore(k + 1)) {
        if (!push(Pos{static_cast<uint16_t>(k + 1), 0, 0, 0, 0})) return Result::AllocFailed;
      }
      ++k;
      if (k - w0 + 1 > cfg.windowTokens) break;
    }
    if (P < 2) return Result::NoPath;  // no break within the token cap: leave it to greedy
    st.positions += P;
    st.windows++;

    // ---- 2. Token prefix sums over the window --------------------------
    const Pos& lastP = pos[P - 1];
    const int lastTok = lastP.offset > 0 ? lastP.word : lastP.word - 1;
    const int T = lastTok - w0 + 1;  // >= 1
    if (!fullSum.reserve(static_cast<size_t>(T) + 1, held, limit) ||
        !gapSum.reserve(static_cast<size_t>(T), held, limit) || !stretch.reserve(static_cast<size_t>(T), held, limit)) {
      return Result::AllocFailed;
    }
    fullSum[0] = 0;
    for (int t = 0; t < T; ++t) {
      fullSum[t + 1] = fullSum[t] + m.fullWidth(w0 + t);
      if (t == 0) {
        gapSum[0] = 0;
        stretch[0] = 0;
      } else {
        gapSum[t] = gapSum[t - 1] + m.gapBefore(w0 + t);
        stretch[t] = static_cast<uint16_t>(stretch[t - 1] + (m.gapStretches(w0 + t) ? 1 : 0));
      }
    }

    // ---- 3. The DP tables ----------------------------------------------
    const size_t cells = static_cast<size_t>(P) * STATES;
    if (!best.reserve(cells, held, limit) || !prev.reserve(cells, held, limit)) return Result::AllocFailed;
    peak = std::max(peak, held);
    for (size_t c = 0; c < cells; ++c) {
      best[c] = INF;
      prev[c] = NO_PREV;
    }
    best[startState] = 0;

    // Line geometry of (i, j): natural width and stretchable gap count.
    const auto geom = [&](const int i, const int j, int& width, int& gaps) {
      const Pos& a = pos[i];
      const Pos& b = pos[j];
      const int lastW = b.offset > 0 ? b.word : b.word - 1;
      gaps = 0;
      if (lastW <= a.word) {  // one token, or a fragment of one
        if (a.offset == 0) {
          width = b.offset > 0 ? b.prefixW : m.fullWidth(a.word);
        } else {
          width = b.offset > 0 ? m.pieceWidth(a.word, a.offset, b.offset, b.hyphen != 0) : a.suffixW;
        }
        return;
      }
      const int fa = a.word - w0;
      const int la = lastW - w0;
      const int first = a.offset == 0 ? m.fullWidth(a.word) : a.suffixW;
      const int last = b.offset > 0 ? b.prefixW : m.fullWidth(lastW);
      width = first + last + (fullSum[la] - fullSum[fa + 1]) + (gapSum[la] - gapSum[fa]);
      gaps = stretch[la] - stretch[fa];
    };

    const int64_t space = cfg.spaceAdvance;
    constexpr int64_t lp16 = LINE_PENALTY * B_SCALE;
    for (int j = 1; j < P; ++j) {
      const bool isEnd = reachedEnd && j == P - 1;
      const bool endHyph = pos[j].offset > 0 && !isEnd;
      bool any = false;
      for (int pass = 0; pass < 4 && !any; ++pass) {
        // pass 0: every start that fits. pass 1 (only when pass 0 updated
        // nothing): the immediately preceding position, overfull -- the
        // one-piece line every breaker forces when a token will not fit.
        // Passes 2 and 3 repeat 0 and 1 with the two-hyphens-in-a-row cap
        // RELAXED, and run only when nothing else reaches this position: a
        // word longer than three lines (a URL, a long compound on a narrow
        // measure) otherwise has no legal path at all, and the whole
        // paragraph fell to greedy (adversarial review, 2026-09-26). The
        // prototype has no such pass -- it returns no path -- so wherever it
        // finds one, these passes never run and the two agree.
        const bool forced = (pass & 1) != 0;
        const bool relaxed = pass >= 2;
        for (int i = j - 1; i >= 0; --i) {
          if (forced && i < j - 1) break;
          int width = 0, gaps = 0;
          geom(i, j, width, gaps);
          ++st.evaluations;
          const int avail = cfg.measure - (firstWindow && i == 0 ? cfg.firstLineIndent : 0);
          const int slack = avail - width;
          int64_t b16 = 0;
          int fit = 1;
          if (forced) {
            b16 = OVERFULL_BADNESS * B_SCALE;
          } else if (isEnd) {
            if (slack < 0) break;  // the last line is never justified, so never shrunk
          } else {
            if (slack < 0) break;  // stretch-only: no shrink
            if (gaps > 0) {
              const int64_t g = gaps * space;
              b16 = detail::badness16(slack, g);
              fit = detail::fitnessOf(slack, g);
            } else if (slack > 0) {
              b16 = NO_GAP_BADNESS * B_SCALE;
              fit = 3;  // the prototype's r = 10
            }
          }
          const int64_t base = lp16 + b16;
          int64_t d = base * base;
          if (endHyph) d += HYPHEN_PENALTY * HYPHEN_PENALTY * D_SCALE;
          // The paragraph start is never flagged; a later window's start is
          // flagged exactly when the committed line before it ended in a word.
          const bool startFlagged = pos[i].offset > 0;
          if (endHyph && startFlagged) d += DOUBLE_HYPHEN_DEMERITS * D_SCALE;
          if (isEnd && startFlagged) d += FINAL_HYPHEN_DEMERITS * D_SCALE;
          for (int c = 1; c <= FITS; ++c) {
            const int64_t adj = (c - fit > 1 || fit - c > 1) ? ADJ_DEMERITS * D_SCALE : 0;
            for (int run = 0; run < RUNS; ++run) {
              const size_t from = static_cast<size_t>(i) * STATES + (c - 1) * RUNS + run;
              const int64_t b0 = best[from];
              if (b0 == INF) continue;
              int nrun = 0;
              if (endHyph) {
                nrun = run + 1;
                if (nrun >= RUNS) {
                  if (!relaxed) continue;  // a ladder longer than allowed
                  nrun = RUNS - 1;
                }
              }
              const int64_t total = detail::satAdd(detail::satAdd(b0, d), adj);
              const size_t to = static_cast<size_t>(j) * STATES + (fit - 1) * RUNS + nrun;
              if (total < best[to]) {
                best[to] = total;
                prev[to] = static_cast<uint16_t>(from);
                any = true;
              }
            }
          }
        }
      }
    }

    // ---- 4. Trace back ------------------------------------------------
    int endJ = -1;
    int endState = -1;
    for (int j = P - 1; j >= 1 && endJ < 0; --j) {
      if (reachedEnd && j != P - 1) break;  // the paragraph end itself must be reachable
      int64_t bestTotal = INF;
      for (int s = 0; s < STATES; ++s) {
        const int64_t v = best[static_cast<size_t>(j) * STATES + s];
        if (v < bestTotal) {
          bestTotal = v;
          endState = s;
        }
      }
      if (bestTotal != INF) endJ = j;
    }
    if (endJ < 0) return Result::NoPath;

    // A path has at most one line per position after the start.
    if (!path.reserve(static_cast<size_t>(P), held, limit)) return Result::AllocFailed;
    peak = std::max(peak, held);
    size_t lines = 0;
    {
      size_t cell = static_cast<size_t>(endJ) * STATES + endState;
      while (cell >= static_cast<size_t>(STATES)) {                  // cells 0..STATES-1 are the window start
        if (lines >= static_cast<size_t>(P)) return Result::NoPath;  // a cycle; cannot happen
        path[lines++] = static_cast<uint16_t>(cell);
        const uint16_t pv = prev[cell];
        if (pv == NO_PREV) return Result::NoPath;  // cannot happen for a finite state
        cell = pv;
      }
    }
    std::reverse(&path[0], &path[0] + lines);

    size_t commit = lines;
    if (!reachedEnd) {
      // Commit the lines that end in the window's first half, and at least one.
      const int half = (P - 1) / 2;
      commit = 0;
      while (commit < lines && static_cast<int>(path[commit] / STATES) <= half) ++commit;
      if (commit == 0) commit = 1;
    }
    for (size_t l = 0; l < commit; ++l) {
      const Pos& p = pos[path[l] / STATES];
      out.push_back(Cut{p.word, p.offset, p.hyphen != 0});
    }
    if (reachedEnd) break;
    const uint16_t lastCell = path[commit - 1];
    start = pos[lastCell / STATES];
    startState = lastCell % STATES;
    firstWindow = false;
  }

  // Sanity: strictly increasing and ending at the paragraph end. Anything else
  // would split words wrongly, and the caller has not mutated anything yet.
  for (size_t l = 0; l < out.size(); ++l) {
    const Cut& c = out[l];
    if (l > 0) {
      const Cut& p = out[l - 1];
      if (c.word < p.word || (c.word == p.word && c.offset <= p.offset)) {
        out.clear();
        return Result::Invalid;
      }
    }
  }
  if (out.empty() || out.back().word != n || out.back().offset != 0) {
    out.clear();
    return Result::Invalid;
  }
  st.peakBytes = peak;
  if (stats) *stats = st;
  return Result::Ok;
}

// Host tests can reach the knobs a device never should: whether the breaker
// runs at all, an allocation budget to force the fallback, and the window.
// Same pattern as CROSSPOINT_RAGGED_GATE_TUNABLE in LineBreakMode.h -- on a
// device a mutable layout parameter is a way for two pages of one book to
// disagree about where the lines go.
#ifdef CROSSPOINT_KNUTH_PLASS_TUNABLE
struct Tuning {
  bool enabled = true;
  size_t allocLimitBytes = 0;
  int windowPositions = WINDOW_POSITIONS;
  int windowTokens = WINDOW_TOKENS;
  // Written by ParsedText after every attempt.
  Result lastResult = Result::Ok;
  Stats lastStats{};
  int attempts = 0;
};
inline Tuning& tuning() {
  static Tuning t;
  return t;
}
#endif

}  // namespace kpbreak
