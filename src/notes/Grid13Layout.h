// The 13-grid "class rows" keyboard layout (owner ruling 2026-08-04).
//
// Extracted from KeyboardEntryActivity.cpp so the split-screen KeyboardPanel in
// Create Note / Claude renders the SAME table rather than a copy that could
// drift from it. The activity now includes this header; the definition lives
// here only.
#pragma once

#include <components/keyboard/keyboard.h>

namespace fui = freeink::ui;

namespace grid13 {

#define UK(label, output, value) \
  fui::KeyboardKey { label, output, fui::KeyKind::Normal, fui::StateNormal, value, 1, true, nullptr }
#define UKA(label, output, value, alt) \
  fui::KeyboardKey { label, output, fui::KeyKind::Normal, fui::StateNormal, value, 1, true, alt }
#define UKW(label, output, value, units) \
  fui::KeyboardKey { label, output, fui::KeyKind::Normal, fui::StateNormal, value, units, true, nullptr }
#define UKS(label, kind, value, units) \
  fui::KeyboardKey { label, nullptr, kind, fui::StateNormal, value, units, true, nullptr }
// Space needs BOTH a Space kind (so the SDK draws its space-bar rule rather
// than a blank key) and an output (so it still types). UKS nulls the output and
// UKW forces Normal, hence a third form.
#define UKSP(units) \
  fui::KeyboardKey { " ", " ", fui::KeyKind::Space, fui::StateNormal, ' ', units, true, nullptr }

// ---------------------------------------------------------------------------
// 13-grid split-letters layout ("Class rows", ruled 2026-08-04). One layer:
// symbols, numbers, symbols, symbols, a-m, n-z, Del/Space/OK -- no Shift, no
// symbols mode, full printable ASCII. Every row sums to 13 width units and the
// bottom row is 3+7+3, so the column-anchor navigation in moveSelectionRow/Col
// is integer-exact; a short row pays for the difference in insetUnits (9 keys
// + 2 either side, 7 keys + 3) and stays centred.
//
// NO KEY CARRIES A SECONDARY CHARACTER (owner ruling 2026-08-10: "get rid of
// secondary characters by adding more rows"). Every printable ASCII symbol has
// a key of its own. Long-press used to be the only way to reach 13 of them,
// which is a gesture that has to be taught and then remembered; two extra rows
// cost nothing here because keyboardRect() derives its height from the row
// count and grows the keyboard UPWARD into space that was empty.
//
// SL_NUMSHIFT sits ABOVE SL_NUM and mirrors it key for key -- 1 under !, - under
// _, ' under ", = under + -- the way a keycap prints its shifted character
// above the unshifted one (owner: "shift parallel but the alts go in the above
// row").
//
// Letters still carry no explicit alt: keyboardAltOutputFor's case-flip
// supplies uppercase on long-press without printing it on the key face
// (display ruling). That is the ONLY surviving long-press output.
//
// SEVEN ROWS IS NEAR A CEILING. KeyboardPanel::preferredHeight caps the notes
// editor's split-screen keyboard at 45% of the screen and then lets the widget
// divide whatever it is given, so overshooting the cap does not fail -- it
// silently shortens every key. At keyboardKeyHeight 48 and spacing 0, seven
// rows want 336 px against a cap of 356 on the X3's 792 px panel: it fits, with
// one row of margin and no more. An eighth row needs the cap raised in the same
// commit, and a screenshot of the note editor to prove it.
// ---------------------------------------------------------------------------
// Column-for-column the shifted face of SL_NUM below it.
inline const fui::KeyboardKey SL_NUMSHIFT[] = {UK("!", "!", '!'),  UK("@", "@", '@'), UK("#", "#", '#'),
                                               UK("$", "$", '$'),  UK("%", "%", '%'), UK("^", "^", '^'),
                                               UK("&", "&", '&'),  UK("*", "*", '*'), UK("(", "(", '('),
                                               UK(")", ")", ')'),  UK("_", "_", '_'), UK("\"", "\"", '"'),
                                               UK("+", "+", '+')};
inline const fui::KeyboardKey SL_NUM[] = {UK("1", "1", '1'), UK("2", "2", '2'),   UK("3", "3", '3'),
                                          UK("4", "4", '4'), UK("5", "5", '5'),   UK("6", "6", '6'),
                                          UK("7", "7", '7'), UK("8", "8", '8'),   UK("9", "9", '9'),
                                          UK("0", "0", '0'), UK("-", "-", '-'),   UK("'", "'", '\''),
                                          UK("=", "=", '=')};
// The 16 symbols that are neither a digit's shifted face nor on the number row,
// grouped by kind: sentence punctuation and the three strokes, then the
// brackets with the angle pair and tilde.
inline const fui::KeyboardKey SL_SYM[] = {UK(".", ".", '.'),  UK(",", ",", ','), UK(":", ":", ':'),
                                          UK(";", ";", ';'),  UK("?", "?", '?'), UK("/", "/", '/'),
                                          UK("\\", "\\", '\\'), UK("|", "|", '|'), UK("`", "`", '`')};
inline const fui::KeyboardKey SL_SYM2[] = {UK("[", "[", '['), UK("]", "]", ']'), UK("{", "{", '{'),
                                           UK("}", "}", '}'), UK("<", "<", '<'), UK(">", ">", '>'),
                                           UK("~", "~", '~')};
inline const fui::KeyboardKey SL_AM[] = {UK("a", "a", 'a'), UK("b", "b", 'b'), UK("c", "c", 'c'), UK("d", "d", 'd'),
                                         UK("e", "e", 'e'), UK("f", "f", 'f'), UK("g", "g", 'g'), UK("h", "h", 'h'),
                                         UK("i", "i", 'i'), UK("j", "j", 'j'), UK("k", "k", 'k'), UK("l", "l", 'l'),
                                         UK("m", "m", 'm')};
inline const fui::KeyboardKey SL_NZ[] = {UK("n", "n", 'n'), UK("o", "o", 'o'), UK("p", "p", 'p'), UK("q", "q", 'q'),
                                         UK("r", "r", 'r'), UK("s", "s", 's'), UK("t", "t", 't'), UK("u", "u", 'u'),
                                         UK("v", "v", 'v'), UK("w", "w", 'w'), UK("x", "x", 'x'), UK("y", "y", 'y'),
                                         UK("z", "z", 'z')};
inline const fui::KeyboardKey SL_BOTTOM[] = {UKS("DEL", fui::KeyKind::Delete, fui::QWERTY_KEY_BACKSPACE, 3), UKSP(7),
                                             UKS("OK", fui::KeyKind::Ok, fui::QWERTY_KEY_ENTER, 3)};

// The short rows carry insetUnits so they still measure 13 units (9 + 2*2,
// 7 + 2*3) and stay centred; anything else breaks the column-anchor arithmetic.
inline const fui::KeyboardRow SL_ROWS[] = {{SL_NUMSHIFT, 13, 0}, {SL_NUM, 13, 0}, {SL_SYM, 9, 2},
                                           {SL_SYM2, 7, 3},      {SL_AM, 13, 0},  {SL_NZ, 13, 0},
                                           {SL_BOTTOM, 3, 0}};
inline const fui::KeyboardLayout SL_LAYOUT{SL_ROWS, 7};

#undef UK
#undef UKA
#undef UKW
#undef UKS
#undef UKSP

}  // namespace grid13
