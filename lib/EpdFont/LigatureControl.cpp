#include "LigatureControl.h"

#include <Utf8.h>

#include <algorithm>
#include <cstring>

namespace ligatures {
namespace {

// Parse a spec into canonical packed pairs: sorted ascending, deduped,
// malformed tokens dropped. Fills `out` (capacity MAX_SUPPRESSED) and returns
// how many pairs it holds.
//
// "Malformed" is anything that is not exactly two codepoints between commas.
// It is dropped rather than treated as a parse failure because this string
// arrives from three places that can all get it wrong -- a hand-edited
// settings.json, the web settings API, and an older build's file -- and the
// failure mode of rejecting the whole spec is every suppressed ligature coming
// silently back on.
//
// Note the limit of that word: a token of two INVALID UTF-8 bytes decodes to
// two U+FFFD and is therefore well-formed by this rule, so it is stored and
// occupies one of the MAX_SUPPRESSED slots. That is harmless -- getLigature is
// only ever asked about codepoints a font's pair table actually carries, and
// no face maps U+FFFD as a ligature input -- but it is storage, not a drop.
//
// ALLOCATION-FREE, and that is not premature. fingerprint() calls this,
// CrossPointSettings::readerRenderSpec() calls fingerprint(), and the reader
// builds a spec on EVERY page turn. A vector for the results and a std::string
// per token would be heap traffic on every page turn of a device with 320 KB
// of heap, for a set bounded at twenty-four 32-bit words.
size_t parse(const char* spec, uint32_t (&out)[MAX_SUPPRESSED]) {
  size_t count = 0;
  if (!spec || !*spec) return 0;

  const char* cursor = spec;
  while (*cursor && count < MAX_SUPPRESSED) {
    // Take one comma-delimited token.
    const char* tokenEnd = strchr(cursor, ',');
    const size_t tokenLen = tokenEnd ? static_cast<size_t>(tokenEnd - cursor) : strlen(cursor);

    // Decode it. A token must yield exactly two codepoints and then end.
    //
    // utf8NextCodepoint reads to the NUL, so the token is copied out to bound
    // it. Sixteen bytes covers every LEGAL token by a wide margin -- two BMP
    // codepoints are at most six bytes -- so anything that does not fit is
    // already malformed and is skipped without decoding.
    char token[16];
    if (tokenLen >= sizeof(token)) {
      if (!tokenEnd) break;
      cursor = tokenEnd + 1;
      continue;
    }
    memcpy(token, cursor, tokenLen);
    token[tokenLen] = '\0';
    const auto* walk = reinterpret_cast<const unsigned char*>(token);
    const uint32_t leftCp = utf8NextCodepoint(&walk);
    const uint32_t rightCp = leftCp ? utf8NextCodepoint(&walk) : 0;
    const bool exhausted = rightCp && utf8NextCodepoint(&walk) == 0;
    // Codepoints above the BMP cannot be a ligature input: EpdLigaturePair
    // packs each side into 16 bits, so getLigature() already refuses them.
    // Accepting one here would store a pair that can never match.
    if (exhausted && leftCp <= 0xFFFFu && rightCp <= 0xFFFFu) {
      out[count++] = packPair(leftCp, rightCp);
    }

    if (!tokenEnd) break;
    cursor = tokenEnd + 1;
  }

  std::sort(out, out + count);
  count = static_cast<size_t>(std::unique(out, out + count) - out);
  return count;
}

std::string render(const uint32_t* pairs, const size_t count) {
  std::string out;
  for (size_t i = 0; i < count; i++) {
    if (!out.empty()) out += ',';
    utf8AppendCodepoint(pairs[i] >> 16, out);
    utf8AppendCodepoint(pairs[i] & 0xFFFFu, out);
  }
  return out;
}

// The live preference. A plain sorted array rather than a set: it holds at
// most MAX_SUPPRESSED entries and is read far more often than it is written.
uint32_t g_suppressed[MAX_SUPPRESSED] = {};
size_t g_suppressedCount = 0;
bool g_enabled = true;

}  // namespace

bool specSuppresses(const char* spec, const uint32_t leftCp, const uint32_t rightCp) {
  uint32_t pairs[MAX_SUPPRESSED];
  const size_t count = parse(spec, pairs);
  return std::binary_search(pairs, pairs + count, packPair(leftCp, rightCp));
}

std::string canonicalize(const char* spec) {
  uint32_t pairs[MAX_SUPPRESSED];
  const size_t count = parse(spec, pairs);
  return render(pairs, count);
}

std::string specWith(const char* spec, const uint32_t leftCp, const uint32_t rightCp, const bool suppress) {
  uint32_t pairs[MAX_SUPPRESSED];
  size_t count = parse(spec, pairs);
  const uint32_t packed = packPair(leftCp, rightCp);
  const size_t at = static_cast<size_t>(std::lower_bound(pairs, pairs + count, packed) - pairs);
  const bool present = at < count && pairs[at] == packed;

  if (suppress && !present) {
    // At the ceiling: hand back what was already there. The caller has to see
    // the value not change, which is the only honest way to report a full set
    // through a toggle row that has nowhere to put an error.
    if (count >= MAX_SUPPRESSED) return render(pairs, count);
    std::move_backward(pairs + at, pairs + count, pairs + count + 1);
    pairs[at] = packed;
    count++;
  } else if (!suppress && present) {
    std::move(pairs + at + 1, pairs + count, pairs + at);
    count--;
  }
  return render(pairs, count);
}

uint32_t fingerprint(const bool enabledFlag, const char* spec) {
  // FNV-1a over the canonical packed pairs, seeded differently per polarity.
  // "Ligatures off" and "ligatures on with nothing suppressed" are different
  // pages, so they must be different fingerprints; seeding rather than mixing
  // a flag keeps them apart even when the suppression list is empty in both.
  uint32_t h = enabledFlag ? 2166136261u : 2166136262u;
  const auto mix = [&h](const uint32_t v) {
    h ^= v;
    h *= 16777619u;
  };
  // With ligatures off the suppression list has no effect on the page, so it
  // is deliberately not mixed in: editing a row that is currently doing
  // nothing must not repaginate the book.
  if (enabledFlag) {
    uint32_t pairs[MAX_SUPPRESSED];
    const size_t count = parse(spec, pairs);
    for (size_t i = 0; i < count; i++) mix(pairs[i]);
  }
  return h;
}

std::string spellPair(const EpdLigaturePair* pairs, const uint32_t pairCount, const uint32_t leftCp,
                      const uint32_t rightCp) {
  // Expand the left side while it is itself the OUTPUT of some pair in this
  // table. The table is sorted by input pair, not by output, so this is a
  // linear scan -- it runs once per row when the Typography screen is built,
  // never during layout.
  std::string out;
  uint32_t chain[4] = {};
  size_t depth = 0;
  uint32_t cp = leftCp;
  while (depth < 4) {
    const EpdLigaturePair* producer = nullptr;
    for (uint32_t i = 0; i < pairCount; i++) {
      if (pairs[i].ligatureCp == cp) {
        producer = &pairs[i];
        break;
      }
    }
    if (!producer) break;
    // Right side of the producing pair, remembered; its left side is what we
    // keep unwinding.
    chain[depth++] = producer->pair & 0xFFFFu;
    cp = producer->pair >> 16;
  }
  utf8AppendCodepoint(cp, out);
  while (depth > 0) utf8AppendCodepoint(chain[--depth], out);
  utf8AppendCodepoint(rightCp, out);
  return out;
}

void configure(const bool enabledFlag, const char* spec) {
  g_enabled = enabledFlag;
  g_suppressedCount = parse(spec, g_suppressed);
}

bool enabled() { return g_enabled; }

bool allowed(const uint32_t leftCp, const uint32_t rightCp) {
  if (!g_enabled) return false;
  if (g_suppressedCount == 0) return true;
  return !std::binary_search(g_suppressed, g_suppressed + g_suppressedCount, packPair(leftCp, rightCp));
}

}  // namespace ligatures
