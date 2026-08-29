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
// shifted faces, numbers, symbols, a-m, n-z, Del/Space/OK -- no Shift, no
// symbols mode, full printable ASCII. Every row measures 13 width units, so the
// column-anchor navigation in moveSelectionRow/Col is integer-exact; a row with
// fewer keys pays the difference in insetUnits and stays centred.
//
// EVERY KEY IS ONE CELL, including Del, Space and Return -- 3/7/3 slabs on a
// row of their own until 2026-08-11, now three cells at the end of the symbol
// row. Nothing in the layout is a special size.
//
// ONLY THREE KEYS CARRY A SECONDARY CHARACTER, all on the symbol row, and each
// is the shifted partner of the key it hangs off on a real keyboard. The
// thirteen that the number row used to hide -- ! @ # $ % ^ & * ( ) _ " + --
// have faces of their own now (owner ruling 2026-08-10), because reaching them
// by long-press was a gesture that had to be taught and then remembered. The
// extra row costs nothing: keyboardRect() derives its height from the row count
// and grows the keyboard UPWARD into space that was empty.
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
// ROW COUNT IS NEAR A CEILING. KeyboardPanel::preferredHeight caps the notes
// editor's split-screen keyboard at 45% of the screen and then lets the widget
// divide whatever it is given, so overshooting the cap does not fail -- it
// silently shortens every key. At keyboardKeyHeight 48 and spacing 0, six rows
// want 288 px against a cap of 356 on the X3's 792 px panel; seven wanted 336,
// which fit with nothing to spare. An eighth needs the cap raised in the same
// commit, and a screenshot of the note editor to prove it.
// ---------------------------------------------------------------------------
// Rows 1 and 2 are a real keyboard's number row and its shifted face, column for
// column: ~ over `, ! over 1, ... _ over -. Only the LAST column departs, and
// deliberately -- Del sits at the top right with Space directly beneath it and
// Return beneath that (owner ruling 2026-08-11), so the three read as one
// right-hand column the way they do on a keyboard. Their displaced pair, = and
// +, moves down to row 3.
inline const fui::KeyboardKey SL_NUMSHIFT[] = {UK("~", "~", '~'),
                                               UK("!", "!", '!'),
                                               UK("@", "@", '@'),
                                               UK("#", "#", '#'),
                                               UK("$", "$", '$'),
                                               UK("%", "%", '%'),
                                               UK("^", "^", '^'),
                                               UK("&", "&", '&'),
                                               UK("*", "*", '*'),
                                               UK("(", "(", '('),
                                               UK(")", ")", ')'),
                                               UK("_", "_", '_'),
                                               UKS("DEL", fui::KeyKind::Delete, fui::QWERTY_KEY_BACKSPACE, 1)};
inline const fui::KeyboardKey SL_NUM[] = {UK("`", "`", '`'),
                                          UK("1", "1", '1'),
                                          UK("2", "2", '2'),
                                          UK("3", "3", '3'),
                                          UK("4", "4", '4'),
                                          UK("5", "5", '5'),
                                          UK("6", "6", '6'),
                                          UK("7", "7", '7'),
                                          UK("8", "8", '8'),
                                          UK("9", "9", '9'),
                                          UK("0", "0", '0'),
                                          UK("-", "-", '-'),
                                          UKSP(1)};
// Everything a keyboard keeps outside its number row, and every alt in the
// layout (owner ruling: "move all alts to row 3").
//
// Which characters hide is decided by frequency: a shift partner you reach for
// often gets a face of its own -- ; and :, / and ?, = and + are all here twice --
// while the six rarest ride as alts on the key a keyboard pairs them with.
// Ordered frequent-first, with the keyboard's own groupings kept together.
inline const fui::KeyboardKey SL_SYM[] = {UKA(".", ".", '.', ">"),
                                          UKA(",", ",", ',', "<"),
                                          UKA("'", "'", '\'', "\""),
                                          UK(";", ";", ';'),
                                          UK(":", ":", ':'),
                                          UK("/", "/", '/'),
                                          UK("?", "?", '?'),
                                          UKA("[", "[", '[', "{"),
                                          UKA("]", "]", ']', "}"),
                                          UKA("\\", "\\", '\\', "|"),
                                          UK("=", "=", '='),
                                          UK("+", "+", '+'),
                                          UKS("OK", fui::KeyKind::Ok, fui::QWERTY_KEY_ENTER, 1)};
inline const fui::KeyboardKey SL_AM[] = {UK("a", "a", 'a'), UK("b", "b", 'b'), UK("c", "c", 'c'), UK("d", "d", 'd'),
                                         UK("e", "e", 'e'), UK("f", "f", 'f'), UK("g", "g", 'g'), UK("h", "h", 'h'),
                                         UK("i", "i", 'i'), UK("j", "j", 'j'), UK("k", "k", 'k'), UK("l", "l", 'l'),
                                         UK("m", "m", 'm')};
inline const fui::KeyboardKey SL_NZ[] = {UK("n", "n", 'n'), UK("o", "o", 'o'), UK("p", "p", 'p'), UK("q", "q", 'q'),
                                         UK("r", "r", 'r'), UK("s", "s", 's'), UK("t", "t", 't'), UK("u", "u", 'u'),
                                         UK("v", "v", 'v'), UK("w", "w", 'w'), UK("x", "x", 'x'), UK("y", "y", 'y'),
                                         UK("z", "z", 'z')};
// Five rows, every one of them a full 13 cells with no inset. There is no
// separate bottom row any more: Del, Space and Return ride the symbol row, and
// the row they used to occupy is now the gap keyboardRect() leaves under the
// keyboard.
inline const fui::KeyboardRow SL_ROWS[] = {
    {SL_NUMSHIFT, 13, 0}, {SL_NUM, 13, 0}, {SL_SYM, 13, 0}, {SL_AM, 13, 0}, {SL_NZ, 13, 0}};
inline const fui::KeyboardLayout SL_LAYOUT{SL_ROWS, 5};

#undef UK
#undef UKA
#undef UKW
#undef UKS
#undef UKSP

}  // namespace grid13
