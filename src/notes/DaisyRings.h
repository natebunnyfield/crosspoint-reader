// The daisywheel's character rings.
//
// Extracted from DaisyEntryActivity.cpp so the split-screen KeyboardPanel picks
// from the SAME petals as the full-screen wheel, rather than a lookalike. The
// interaction model comes with them: each petal holds three characters and the
// three pick buttons (Up / Confirm / Down) choose top / middle / bottom
// directly — it is not a multi-tap cycle.
//
// COVERAGE (owner ruling 2026-08-06). Every printable ASCII character: a-z with
// uppercase on long-press, 0-9, all 32 punctuation marks, and space. Before that
// ruling the wheel was missing < > [ ] { } \ | ^ and the backtick outright, and
// ( ) sat two rotations apart — so a bracket could not be matched without
// crossing the ring, and a WiFi password containing one could not be typed at
// all.
#pragma once

namespace daisyrings {

// Petals clockwise from 12 o'clock, each [top, middle, bottom]. The utility
// petal (backspace / ring swap / OK) is appended as the last petal of each ring,
// so it is wrap-adjacent to petal 1.

inline constexpr int ABC_CHAR_PETALS = 9;
inline constexpr char ABC_RING[ABC_CHAR_PETALS][3] = {{'a', 'b', 'c'}, {'d', 'e', 'f'}, {'g', 'h', 'i'},
                                                      {'j', 'k', 'l'}, {'m', 'n', 'o'}, {'p', 'q', 'r'},
                                                      {'s', 't', 'u'}, {'v', 'w', 'x'}, {'y', 'z', ' '}};

// The number/symbol ring. Two rings rather than three (owner ruling): one swap
// press reaches any symbol, at the cost of a longer ring to cross.
//
// ORDER IS QWERTY, LEFT TO RIGHT. Digits lead, then the symbols follow the
// keyboard: the ` key, then the shifted number row, then - = ; ' , . / in the
// order your hand would find them. Not alphabetical, not frequency — a layout
// you can predict from a keyboard you already know.
//
// BRACKET PETALS keep a matched pair together, open above close (owner ruling):
//
//     top    = the OPEN bracket        (Up)
//     middle = a related character     (Confirm)
//     bottom = the CLOSE bracket       (Down)
//
// so a pair is always one button apart and can never drift to opposite sides of
// the ring. The middle character is chosen by QWERTY adjacency first and
// frequency second, which for three of the four is simply the next key to the
// right: [ ] \ , { } | , < > ? . The pair ( ) is the one exception — the key
// right of ) is - _ , which is already a pair of its own on petal 4 — so it
// takes * from the key immediately LEFT, which is also the far more useful
// character next to parentheses in markdown.
//
// The last petal ends in SPACE, matching the abc ring's {y, z, space}, so the
// space target sits in the same slot on both rings. The two characters above it
// are deliberate DUPLICATES of . and , — the two commonest characters in prose
// after letters, put within reach of the space you are about to type anyway.
// Every character already has a home in petals 1-14; these two are convenience,
// which is why nothing is missing if you ignore this petal entirely.
inline constexpr int NUM_CHAR_PETALS = 15;
inline constexpr char NUM_RING[NUM_CHAR_PETALS][3] = {
    {'1', '2', '3'}, {'4', '5', '6'},  {'7', '8', '9'}, {'0', '`', '~'},  {'!', '@', '#'},
    {'$', '%', '^'}, {'&', '-', '_'},  {'=', '+', ';'}, {':', '\'', '"'}, {',', '.', '/'},
    {'(', '*', ')'}, {'[', '\\', ']'}, {'{', '|', '}'}, {'<', '?', '>'},  {'.', ',', ' '},
};

}  // namespace daisyrings
