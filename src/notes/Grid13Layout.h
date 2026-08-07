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
// numbers, symbols, a-m, n-z, Del/Space/OK -- no Shift, no symbols mode, full
// printable ASCII (the number row gains - ' = so nothing is uneven). Every row
// sums to 13 width units and the bottom row is 3+7+3, so the column-anchor
// navigation in moveSelectionRow/Col is integer-exact. Letters carry no
// explicit alt: keyboardAltOutputFor's case-flip supplies uppercase on
// long-press without printing it on the key face (display ruling).
// ---------------------------------------------------------------------------
inline const fui::KeyboardKey SL_NUM[] = {UKA("1", "1", '1', "!"), UKA("2", "2", '2', "@"), UKA("3", "3", '3', "#"),
                                          UKA("4", "4", '4', "$"), UKA("5", "5", '5', "%"), UKA("6", "6", '6', "^"),
                                          UKA("7", "7", '7', "&"), UKA("8", "8", '8', "*"), UKA("9", "9", '9', "("),
                                          UKA("0", "0", '0', ")"), UKA("-", "-", '-', "_"), UKA("'", "'", '\'', "\""),
                                          UKA("=", "=", '=', "+")};
inline const fui::KeyboardKey SL_SYM[] = {UKA(".", ".", '.', ">"), UKA(",", ",", ',', "<"), UKA("/", "/", '/', "\\"),
                                          UKA(":", ":", ':', "|"), UK(";", ";", ';'),       UK("?", "?", '?'),
                                          UK("!", "!", '!'),       UK("@", "@", '@'),       UK("&", "&", '&'),
                                          UK("+", "+", '+'),       UKA("[", "[", '[', "{"), UKA("]", "]", ']', "}"),
                                          UKA("`", "`", '`', "~")};
inline const fui::KeyboardKey SL_AM[] = {UK("a", "a", 'a'), UK("b", "b", 'b'), UK("c", "c", 'c'), UK("d", "d", 'd'),
                                         UK("e", "e", 'e'), UK("f", "f", 'f'), UK("g", "g", 'g'), UK("h", "h", 'h'),
                                         UK("i", "i", 'i'), UK("j", "j", 'j'), UK("k", "k", 'k'), UK("l", "l", 'l'),
                                         UK("m", "m", 'm')};
inline const fui::KeyboardKey SL_NZ[] = {UK("n", "n", 'n'), UK("o", "o", 'o'), UK("p", "p", 'p'), UK("q", "q", 'q'),
                                         UK("r", "r", 'r'), UK("s", "s", 's'), UK("t", "t", 't'), UK("u", "u", 'u'),
                                         UK("v", "v", 'v'), UK("w", "w", 'w'), UK("x", "x", 'x'), UK("y", "y", 'y'),
                                         UK("z", "z", 'z')};
inline const fui::KeyboardKey SL_BOTTOM[] = {UKS("Del", fui::KeyKind::Delete, fui::QWERTY_KEY_BACKSPACE, 3), UKSP(7),
                                             UKS("OK", fui::KeyKind::Ok, fui::QWERTY_KEY_ENTER, 3)};

inline const fui::KeyboardRow SL_ROWS[] = {
    {SL_NUM, 13, 0}, {SL_SYM, 13, 0}, {SL_AM, 13, 0}, {SL_NZ, 13, 0}, {SL_BOTTOM, 3, 0}};
inline const fui::KeyboardLayout SL_LAYOUT{SL_ROWS, 5};

#undef UK
#undef UKA
#undef UKW
#undef UKS
#undef UKSP

}  // namespace grid13
