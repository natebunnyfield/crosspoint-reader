#pragma once

#include <builtinFonts/notoserif_12_bold.h>
#include <builtinFonts/notoserif_12_bolditalic.h>
#include <builtinFonts/notoserif_12_italic.h>
#include <builtinFonts/notoserif_12_regular.h>
#include <builtinFonts/notoserif_14_bold.h>
#include <builtinFonts/notoserif_14_bolditalic.h>
#include <builtinFonts/notoserif_14_italic.h>
#include <builtinFonts/notoserif_14_regular.h>
#include <builtinFonts/notoserif_16_bold.h>
#include <builtinFonts/notoserif_16_bolditalic.h>
#include <builtinFonts/notoserif_16_italic.h>
#include <builtinFonts/notoserif_16_regular.h>
#include <builtinFonts/notoserif_18_bold.h>
#include <builtinFonts/notoserif_18_bolditalic.h>
#include <builtinFonts/notoserif_18_italic.h>
#include <builtinFonts/notoserif_18_regular.h>
#include <builtinFonts/notosans_8_regular.h>
#include <builtinFonts/notosans_12_bold.h>
#include <builtinFonts/notosans_12_bolditalic.h>
#include <builtinFonts/notosans_12_italic.h>
#include <builtinFonts/notosans_12_regular.h>
#include <builtinFonts/notosans_14_bold.h>
#include <builtinFonts/notosans_14_bolditalic.h>
#include <builtinFonts/notosans_14_italic.h>
#include <builtinFonts/notosans_14_regular.h>
#include <builtinFonts/notosans_16_bold.h>
#include <builtinFonts/notosans_16_bolditalic.h>
#include <builtinFonts/notosans_16_italic.h>
#include <builtinFonts/notosans_16_regular.h>
#include <builtinFonts/notosans_18_bold.h>
#include <builtinFonts/notosans_18_bolditalic.h>
#include <builtinFonts/notosans_18_italic.h>
#include <builtinFonts/notosans_18_regular.h>
#include <builtinFonts/ubuntu_10_bold.h>
#include <builtinFonts/ubuntu_10_regular.h>
#include <builtinFonts/ubuntu_12_bold.h>
#include <builtinFonts/ubuntu_12_regular.h>

// SYSTEM-FONT CUTS. The UI draws at three sizes -- 8, 10 and 12 pt -- and the
// System font setting swaps all three to one family, so every offered family
// needs regular and bold at each. Ubuntu and the two Noto families already had
// most of the matrix; these fill it in. Libre Franklin is wholly new here --
// it shipped only as an SD .cpfont before, which the chrome cannot draw with.
#include <builtinFonts/librefranklin_10_bold.h>
#include <builtinFonts/librefranklin_10_regular.h>
#include <builtinFonts/librefranklin_12_bold.h>
#include <builtinFonts/librefranklin_12_regular.h>
#include <builtinFonts/librefranklin_8_bold.h>
#include <builtinFonts/librefranklin_8_regular.h>
#include <builtinFonts/notosans_10_bold.h>
#include <builtinFonts/notosans_10_regular.h>
#include <builtinFonts/notosans_8_bold.h>
#include <builtinFonts/notoserif_10_bold.h>
#include <builtinFonts/notoserif_10_regular.h>
#include <builtinFonts/notoserif_8_bold.h>
#include <builtinFonts/notoserif_8_regular.h>
#include <builtinFonts/ubuntu_8_bold.h>
#include <builtinFonts/ubuntu_8_regular.h>

// HI-RES COMPANIONS FOR THE UI FACES, host builds only.
//
// The three faces every piece of chrome is drawn with -- SMALL (Noto Sans 8),
// UI_10 and UI_12 (Ubuntu) -- rasterised at double the ppem, so a 2x render
// scale can blit real glyphs instead of replicating each 1x pixel into a 2x2
// block. Metrics still come from the 1x tables (see registerHiResBuiltinFont);
// these carry glyph bitmaps and nothing else, and the names encode the 1x face
// they stand in for, not their own point size.
//
// Gated because the device renders at scale 1 and would pay ~1.8 MB of flash
// for bitmaps it can never blit. CROSSPOINT_RENDER_SCALE is 2 on the iOS target
// and on desktop simulator runs that opt in.
#if defined(CROSSPOINT_RENDER_SCALE) && CROSSPOINT_RENDER_SCALE > 1
#include <builtinFonts/librefranklin_10_bold_2x.h>
#include <builtinFonts/librefranklin_10_regular_2x.h>
#include <builtinFonts/librefranklin_12_bold_2x.h>
#include <builtinFonts/librefranklin_12_regular_2x.h>
#include <builtinFonts/librefranklin_8_bold_2x.h>
#include <builtinFonts/librefranklin_8_regular_2x.h>
#include <builtinFonts/notosans_10_bold_2x.h>
#include <builtinFonts/notosans_10_regular_2x.h>
#include <builtinFonts/notosans_12_bold_2x.h>
#include <builtinFonts/notosans_12_regular_2x.h>
#include <builtinFonts/notosans_8_bold_2x.h>
#include <builtinFonts/notosans_8_regular_2x.h>
#include <builtinFonts/notoserif_10_bold_2x.h>
#include <builtinFonts/notoserif_10_regular_2x.h>
#include <builtinFonts/notoserif_12_bold_2x.h>
#include <builtinFonts/notoserif_12_regular_2x.h>
#include <builtinFonts/notoserif_8_bold_2x.h>
#include <builtinFonts/notoserif_8_regular_2x.h>
#include <builtinFonts/ubuntu_10_bold_2x.h>
#include <builtinFonts/ubuntu_10_regular_2x.h>
#include <builtinFonts/ubuntu_12_bold_2x.h>
#include <builtinFonts/ubuntu_12_regular_2x.h>
#include <builtinFonts/ubuntu_8_bold_2x.h>
#include <builtinFonts/ubuntu_8_regular_2x.h>
#endif
