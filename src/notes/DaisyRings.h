// The daisywheel's character rings.
//
// Extracted from DaisyEntryActivity.cpp so the split-screen KeyboardPanel picks
// from the SAME petals as the full-screen wheel, rather than a lookalike. The
// interaction model comes with them: each petal holds three characters and the
// three pick buttons (Up / Confirm / Down) choose top / middle / bottom
// directly — it is not a multi-tap cycle.
#pragma once

namespace daisyrings {

// Petals clockwise from 12 o'clock, each [top, middle, bottom]; the utility
// petal (backspace / swap / OK) is appended as the last petal of each ring, so
// it is wrap-adjacent to petal 1. Coverage: a-z (+ uppercase via long-press),
// 0-9, . , - _ / : ; @ ' " ! ? & ( ) + = # $ % * ~ and space -- the full WiFi
// password requirement per docs/daisywheel.md.
inline constexpr int ABC_CHAR_PETALS = 9;
inline constexpr char ABC_RING[ABC_CHAR_PETALS][3] = {{'a', 'b', 'c'}, {'d', 'e', 'f'}, {'g', 'h', 'i'},
                                                      {'j', 'k', 'l'}, {'m', 'n', 'o'}, {'p', 'q', 'r'},
                                                      {'s', 't', 'u'}, {'v', 'w', 'x'}, {'y', 'z', ' '}};
inline constexpr int NUM_CHAR_PETALS = 11;
inline constexpr char NUM_RING[NUM_CHAR_PETALS][3] = {
    {'1', '2', '3'},  {'4', '5', '6'}, {'7', '8', '9'}, {'0', '.', ','}, {'-', '_', '/'}, {':', ';', '@'},
    {'\'', '"', '!'}, {'?', '&', '('}, {')', '+', '='}, {'#', '$', '%'}, {'*', '~', ' '}};

}  // namespace daisyrings
