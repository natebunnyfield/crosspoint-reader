#pragma once

#include <cstdint>

// Automatic justification: the MEASURE decides, not a setting.
//
// Owner ruling 2026-08-23: "remove ragged right or justified ios app settings,
// instead make it automatic by letting the character length decide what is
// optimal." The Text Alignment row shipped on 2026-08-22 (ae6981ddf) and is
// withdrawn here; CrossPointSettings::paragraphAlignment is now a
// `static constexpr` JUSTIFIED, and every block that arrives asking to be
// justified is demoted to its natural ragged edge when its own measure is too
// narrow to justify well.
//
// WHY A THRESHOLD EXISTS AT ALL
//
// Justification distributes a line's leftover space across its word gaps. The
// slack per gap is (measure - the words' own widths) / (gap count), so the
// damage is governed by the gap COUNT, and the gap count is governed by the
// measure. Bringhurst gives the elastic budget per gap rather than per line:
// a word space of M/4 desired, M/5 minimum, M/2 maximum ("The Elements of
// Typographic Style", 2nd/3rd ed., 2.1.1, p. 26). A 40-character line of
// English holds about seven words and therefore six gaps, so the whole legal
// stretch before every gap is at its maximum is 6 x M/4 ~ 1.5 em -- barely
// more than half an average word. Below that the line has to gape or hyphenate.
//
// THE THRESHOLD, AND ITS SOURCE
//
// Bringhurst, "The Elements of Typographic Style", 2.1.2 "Choose a comfortable
// measure", p. 27 (identical in the 2nd ed., Hartley & Marks 1996, and the
// 3rd ed. / version 3.0, 2004):
//
//   "A reasonable working minimum for justified text in English is the
//    40-character line. Shorter lines may compose perfectly well with
//    sufficient luck and patience, but in the long run, justified lines
//    averaging less than 38 or 40 characters will lead to white acne or pig
//    bristles: a rash of erratic and splotchy word spaces or an epidemic of
//    hyphenation. When the line is short, the text should be set ragged right."
//
// That is the rule this file implements, in his units, with his remedy.
//
// It is not a lone opinion. The Chicago Manual of Style, 17th ed. P16.136,
// reaches the same place in picas rather than characters -- "For very short
// lines, such as those in an index, justifying the text usually results in
// either gaping word spaces or excessive hyphenation, making for difficult
// reading. Chicago therefore sets all indexes without justification" -- of a
// 13-pica column of 8 pt, which is about 40-45 characters. And it has been
// measured: Gregory & Poulton, "Even versus Uneven Right-hand Margins and the
// Rate of Comprehension in Reading", Ergonomics 13(4) 427-434 (1970),
// doi:10.1080/00140137008931157, found justification significantly WORSE than
// ragged at seven words per line (~38-39 characters) and no disadvantage at
// twelve (~66), and attributed the difference to "the irregularities in
// spacing introduced by justification when the line length is short".
//
// Where the sources disagree is only in whether they will name a number:
// Craig ("Designing with Type", 5th ed.) gives a comfortable band of 35-70
// characters and names ragged setting as the fix for bad word spacing but sets
// no justification floor; Butterick ("Practical Typography") gives 45-90 and
// declines to set one at all; the CSS Text Modules and the W3C's "Requirements
// for Latin Text Layout" say nothing (its 4.2 Justification section is an
// empty stub). None of them contradicts 40; Bringhurst is the one who states
// it, so 40 is what ships.
//
// HOW CHARACTERS PER LINE IS COUNTED
//
// NOT measure/fontSize. Point size is a design size, not a width: across the
// eight reading families on this card the same 14 pt spans a 1.5x range of set
// width, so a point-size proxy would put two faces on opposite sides of the
// threshold while claiming they were identical.
//
// The typographic instrument is the ALPHABET LENGTH -- the measured width of
// the lowercase alphabet a-z set in the actual face at the actual size --
// which is exactly what Bringhurst's own copyfitting table (pp. 28-29) is
// indexed by: "Measure the length of the basic lowercase alphabet ...  in any
// face and size you are considering, and the table will tell you the average
// number of characters to expect on a given line" (2.1.2, p. 27). His table is
// linear in the measure within a row, so it reduces to
//
//     characters per line = CHARS_PER_ALPHABET x (measure / alphabet length)
//
// and across the rows a 10 pt text roman actually occupies (alphabet 120-140
// pt, his figure and a correct one) the constant is 28.0-28.3. His own worked
// example checks it: a 25-pica measure with a 128 pt alphabet is 2.344
// alphabets, x 28.1 = 65.9, and he calls it "roughly 65 characters per line".
//
// The constant is NOT 26. A line of running prose is not an alphabet: it is
// weighted toward the narrow high-frequency letters and every ~5 letters it
// spends a space, which is narrower still. The frequency model is published --
// the OpenType OS/2 xAvgCharWidth weights, where space carries 166/1000 and
// 'e' 100/1000 -- and evaluating it against real text faces gives an average
// character of ~0.44 em against a per-letter a-z mean of ~0.49 em, i.e.
// alphabet/28 rather than alphabet/26. (Note the trap: CSS's `ch` unit is the
// advance of '0', ~0.5 em, so `40ch` of measure actually holds about 50
// characters, not 40.)
//
// MEASURED HERE, not just inherited. Sweeping this repo's own reading families
// through the simulator on controlled prose -- LibrisADF 12/14/16/18, Libre
// Franklin, TeXGyre Schola, Coelacanth, Edgar, TeXGyre Heros, Inknut Junicode,
// 13 face/size pairs at a 512 px measure -- and dividing the counted characters
// per line by (measure / alphabet) gives 25.6 to 30.0, mean 27.9. Bringhurst's
// 28.1 sits inside that, so it is what the code uses; the residual is +/-3
// characters, and it leans conservative (the counted lines are justified, whose
// stretched gaps hold slightly fewer characters than the natural setting the
// estimate models). Leaning toward ragged near the boundary is the right way to
// be wrong: Bringhurst's failure zone is "less than 38 or 40", so the cost of a
// late justification is worse than the cost of an early rag.
//
// The alphabet is measured through GfxRenderer::getTextAdvanceX, which is the
// same call the line breaker uses to measure words -- kerning included, since
// 2026-08-22 -- so the estimate cannot disagree with the layout it is deciding
// for. See docs/auto-justification.md.
namespace autojustify {

// Bringhurst 2.1.2, p. 27: "A reasonable working minimum for justified text in
// English is the 40-character line."
constexpr int THRESHOLD_CHARS = 40;

// Bringhurst's copyfitting table (pp. 28-29) over the alphabet lengths a text
// roman actually occupies, x10 so the arithmetic stays integer -- this runs
// inside the paginator on an ESP32-C3.
constexpr int CHARS_PER_ALPHABET_X10 = 281;

// Estimated characters per line for a measure of `measurePx` set in a face
// whose lowercase alphabet a-z measures `alphabetPx`. Returns 0 when the
// alphabet could not be measured, which reads as "unknown" and is handled by
// shouldJustify below rather than by inventing a count.
inline int charsPerLine(const int measurePx, const int alphabetPx) {
  if (measurePx <= 0 || alphabetPx <= 0) return 0;
  // Rounded integer divide of (measure * 28.1 / alphabet).
  return (measurePx * CHARS_PER_ALPHABET_X10 + alphabetPx * 5) / (alphabetPx * 10);
}

// The decision. A block asking to be justified keeps it only when its own
// measure carries at least THRESHOLD_CHARS characters.
//
// An unmeasurable alphabet (a face with no lowercase, a metrics read that came
// back empty) returns TRUE: justification is the alignment that was asked for,
// and the fallback must be "leave the request alone", not "silently restyle the
// book because a metric was missing".
inline bool shouldJustify(const int measurePx, const int alphabetPx) {
  if (alphabetPx <= 0) return true;
  return charsPerLine(measurePx, alphabetPx) >= THRESHOLD_CHARS;
}

// The narrowest measure, in pixels, that still justifies for a given alphabet
// length. Exposed for the tests and for the worked examples in the doc; layout
// does not call it.
inline int narrowestJustifiedMeasurePx(const int alphabetPx) {
  if (alphabetPx <= 0) return 0;
  // charsPerLine ROUNDS to nearest, so the edge is half a character below the
  // threshold, not at it: floor((m*281 + 5a) / 10a) >= 40  <=>  m*281 >= 395a.
  // Writing 400a here (the naive inverse) puts the reported flip one to two
  // pixels above the predicate's own, which is exactly the kind of
  // off-by-a-rounding a worked example in a doc would carry forever.
  const int num = (THRESHOLD_CHARS * 10 - 5) * alphabetPx;
  return (num + CHARS_PER_ALPHABET_X10 - 1) / CHARS_PER_ALPHABET_X10;
}

// The string whose advance is the alphabet length. Lowercase only, no spaces:
// this is a metric of the FACE, not a sample of text. Bringhurst spells it out
// in full for the same reason (2.1.2, p. 27).
constexpr const char* ALPHABET = "abcdefghijklmnopqrstuvwxyz";

}  // namespace autojustify
