// Daisywheel ring VARIANTS, for the design decision only.
//
// This header exists so the proposed rings can be rendered through the real
// GfxRenderer at real device geometry before any of them is committed to
// src/notes/DaisyRings.h. It is a decision aid, not firmware: nothing in src/
// includes it, and once a variant is chosen its table moves into DaisyRings.h
// and the rest of this file is deleted.
//
// The strip geometry in daisy_preview.cpp is copied from KeyboardPanel::render's
// daisy branch (7-petal window centred on the selection, three slots stacked in
// the order Up/Confirm/Down pick them), so these BMPs are what the device draws.
//
// LABELS ARE ASCII ON PURPOSE. The UI font carries no U+23CE / U+21B5, so a
// Return key cannot be drawn as an arrow glyph — it renders blank. The same
// trap already cost a fix earlier on this branch, where the side-button hints
// were drawn with the missing U+25B2/U+25BC and showed nothing.
#pragma once

namespace daisyvariants {

struct Ring {
  const char* name;
  const char (*petals)[3];
  int count;
};

// Utility petal labels, top -> bottom (Up / Confirm / Down).
struct Util {
  const char* top;
  const char* mid;
  const char* bottom;
};

// ---------------------------------------------------------------------------
// SHIPPED TODAY. Two rings. ( and ) are two rotations apart; there is no
// Return; and < > [ ] { } \ | ^ ` do not exist on the wheel at all.
// ---------------------------------------------------------------------------
inline constexpr char CUR_ABC[9][3] = {{'a', 'b', 'c'}, {'d', 'e', 'f'}, {'g', 'h', 'i'},
                                       {'j', 'k', 'l'}, {'m', 'n', 'o'}, {'p', 'q', 'r'},
                                       {'s', 't', 'u'}, {'v', 'w', 'x'}, {'y', 'z', ' '}};
inline constexpr char CUR_NUM[11][3] = {
    {'1', '2', '3'},  {'4', '5', '6'}, {'7', '8', '9'}, {'0', '.', ','}, {'-', '_', '/'}, {':', ';', '@'},
    {'\'', '"', '!'}, {'?', '&', '('}, {')', '+', '='}, {'#', '$', '%'}, {'*', '~', ' '}};

// ---------------------------------------------------------------------------
// OPTION A — three rings: abc / 123 / sym. The swap slot cycles instead of
// toggling. Every ring stays short to cross; symbols cost one extra swap.
// ---------------------------------------------------------------------------
inline constexpr char A_ABC[9][3] = {{'a', 'b', 'c'}, {'d', 'e', 'f'}, {'g', 'h', 'i'},
                                     {'j', 'k', 'l'}, {'m', 'n', 'o'}, {'p', 'q', 'r'},
                                     {'s', 't', 'u'}, {'v', 'w', 'x'}, {'y', 'z', ' '}};
inline constexpr char A_NUM[8][3] = {{'1', '2', '3'},  {'4', '5', '6'}, {'7', '8', '9'}, {'0', '.', ','},
                                     {'-', '_', '/'},  {':', ';', '@'}, {'\'', '"', '!'}, {'?', '&', ' '}};
// One matched pair per petal, close directly below open, third slot a related
// character. Backtick rides with <> so markdown code spans are one petal.
inline constexpr char A_SYM[7][3] = {{'(', ')', '^'}, {'[', ']', '\\'}, {'{', '}', '|'}, {'<', '>', '`'},
                                     {'+', '=', '*'}, {'#', '$', '%'},  {'~', ' ', ' '}};

// ---------------------------------------------------------------------------
// OPTION B — two rings; the number ring absorbs everything. One swap press
// reaches any symbol, but the ring is 16 petals to cross rather than 12.
// ---------------------------------------------------------------------------
inline constexpr char B_ABC[9][3] = {{'a', 'b', 'c'}, {'d', 'e', 'f'}, {'g', 'h', 'i'},
                                     {'j', 'k', 'l'}, {'m', 'n', 'o'}, {'p', 'q', 'r'},
                                     {'s', 't', 'u'}, {'v', 'w', 'x'}, {'y', 'z', ' '}};
inline constexpr char B_NUM[15][3] = {
    {'1', '2', '3'}, {'4', '5', '6'},  {'7', '8', '9'}, {'0', '.', ','}, {'-', '_', '/'},
    {':', ';', '@'}, {'\'', '"', '!'}, {'?', '&', '*'}, {'+', '=', '~'}, {'#', '$', '%'},
    {'(', ')', '^'}, {'[', ']', '\\'}, {'{', '}', '|'}, {'<', '>', '`'}, {' ', ' ', ' '}};

}  // namespace daisyvariants
