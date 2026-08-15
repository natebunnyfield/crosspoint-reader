#pragma once

// The 1x tables, always compiled: these are the faces the DEVICE draws with.

#include <builtinFonts/iawriterquattro_12_bold.h>
#include <builtinFonts/iawriterquattro_14_bold.h>
#include <builtinFonts/iawriterquattro_12_bolditalic.h>
#include <builtinFonts/iawriterquattro_14_bolditalic.h>
#include <builtinFonts/iawriterquattro_12_italic.h>
#include <builtinFonts/iawriterquattro_14_italic.h>
#include <builtinFonts/iawriterquattro_12_regular.h>
#include <builtinFonts/iawriterquattro_14_regular.h>
#include <builtinFonts/librefranklin_10_bold.h>
#include <builtinFonts/librefranklin_10_regular.h>
#include <builtinFonts/librefranklin_12_bold.h>
#include <builtinFonts/librefranklin_12_regular.h>
#include <builtinFonts/librefranklin_8_bold.h>
#include <builtinFonts/librefranklin_8_regular.h>
#include <builtinFonts/librefranklin_reader_12_bold.h>
#include <builtinFonts/librefranklin_reader_12_bolditalic.h>
#include <builtinFonts/librefranklin_reader_12_italic.h>
#include <builtinFonts/librefranklin_reader_12_regular.h>
#include <builtinFonts/librefranklin_reader_14_bold.h>
#include <builtinFonts/librefranklin_reader_14_bolditalic.h>
#include <builtinFonts/librefranklin_reader_14_italic.h>
#include <builtinFonts/librefranklin_reader_14_regular.h>
#include <builtinFonts/librefranklin_reader_16_bold.h>
#include <builtinFonts/librefranklin_reader_16_bolditalic.h>
#include <builtinFonts/librefranklin_reader_16_italic.h>
#include <builtinFonts/librefranklin_reader_16_regular.h>
#include <builtinFonts/librefranklin_reader_18_bold.h>
#include <builtinFonts/librefranklin_reader_18_bolditalic.h>
#include <builtinFonts/librefranklin_reader_18_italic.h>
#include <builtinFonts/librefranklin_reader_18_regular.h>

// Hi-res companions, selected by CROSSPOINT_RENDER_SCALE.
//
// Guarded rather than unconditional so a device build parses NONE of them. It
// previously included the 2x tier always and relied on --gc-sections to drop
// the arrays at link; that worked, but it made every device compile read
// megabytes of glyph tables it could never reach, and it silently assumed the
// only hi-res tier would ever be 2x.
//
// A tier listed in convert-builtin-fonts.sh's HIRES_SCALES needs a block here
// to match. Only the OFL families appear: PragmataPro and Nitti Typewriter are
// commercial, their headers are gitignored, and main.cpp includes those behind
// __has_include so a clone without the licensed sources still builds.
#if defined(CROSSPOINT_RENDER_SCALE) && CROSSPOINT_RENDER_SCALE == 2
#include <builtinFonts/iawriterquattro_12_bold_2x.h>
#include <builtinFonts/iawriterquattro_14_bold_2x.h>
#include <builtinFonts/iawriterquattro_12_bolditalic_2x.h>
#include <builtinFonts/iawriterquattro_14_bolditalic_2x.h>
#include <builtinFonts/iawriterquattro_12_italic_2x.h>
#include <builtinFonts/iawriterquattro_14_italic_2x.h>
#include <builtinFonts/iawriterquattro_12_regular_2x.h>
#include <builtinFonts/iawriterquattro_14_regular_2x.h>
#include <builtinFonts/librefranklin_10_bold_2x.h>
#include <builtinFonts/librefranklin_10_regular_2x.h>
#include <builtinFonts/librefranklin_12_bold_2x.h>
#include <builtinFonts/librefranklin_12_regular_2x.h>
#include <builtinFonts/librefranklin_8_bold_2x.h>
#include <builtinFonts/librefranklin_8_regular_2x.h>
#elif defined(CROSSPOINT_RENDER_SCALE) && CROSSPOINT_RENDER_SCALE == 3
#include <builtinFonts/iawriterquattro_12_bold_3x.h>
#include <builtinFonts/iawriterquattro_14_bold_3x.h>
#include <builtinFonts/iawriterquattro_12_bolditalic_3x.h>
#include <builtinFonts/iawriterquattro_14_bolditalic_3x.h>
#include <builtinFonts/iawriterquattro_12_italic_3x.h>
#include <builtinFonts/iawriterquattro_14_italic_3x.h>
#include <builtinFonts/iawriterquattro_12_regular_3x.h>
#include <builtinFonts/iawriterquattro_14_regular_3x.h>
#include <builtinFonts/librefranklin_10_bold_3x.h>
#include <builtinFonts/librefranklin_10_regular_3x.h>
#include <builtinFonts/librefranklin_12_bold_3x.h>
#include <builtinFonts/librefranklin_12_regular_3x.h>
#include <builtinFonts/librefranklin_8_bold_3x.h>
#include <builtinFonts/librefranklin_8_regular_3x.h>
#endif
