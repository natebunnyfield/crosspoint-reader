#pragma once

class GfxRenderer;

// Points SMALL_FONT_ID / UI_10_FONT_ID / UI_12_FONT_ID at the family named by
// SETTINGS.systemFont, and on a 2x build registers the matching hi-res
// companions. Defined in main.cpp, where the font families themselves live —
// same arrangement as SilentRestart.h.
//
// Call it after changing SETTINGS.systemFont: it rebinds the entries the whole
// UI already draws through (via replaceFont -- insertFont refuses to overwrite),
// so the next render picks the new face up without a restart. Layout follows
// too, because metrics come from these same families rather than a cached copy.
void applySystemFont(GfxRenderer& renderer);
