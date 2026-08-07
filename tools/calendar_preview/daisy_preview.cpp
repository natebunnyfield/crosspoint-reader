// Renders the daisywheel strip through the REAL GfxRenderer, for the ring
// redesign decision. Decision aid only — see daisy_variants.h.
//
// Geometry is lifted verbatim from KeyboardPanel::render's daisy branch: a
// 7-petal window centred on the selected petal, each petal three slots stacked
// in the order Up / Confirm / Down pick them, selection drawn as a 2px rect.
// So these are the device's own pixels, not an artist's impression.
//
// Built by build.sh alongside render_harness; writes fs_/daisy_*.bmp.
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <builtinFonts/all.h>

#include <cstdio>
#include <cstring>

#include "daisy_variants.h"
#include "fontIds.h"

extern GfxRenderer renderer;
extern bool simInit();
extern bool writeMonoPortraitBmpExternal(const char* path, const GfxRenderer& r);

namespace {

using daisyvariants::Ring;
using daisyvariants::Util;

// KeyboardPanel::render's daisy branch, verbatim in geometry.
void drawStrip(const Ring& ring, const Util& util, int selectedPetal, int x, int y, int width, int height) {
  const int n = ring.count + 1;  // + the utility petal
  const int visible = n < 7 ? n : 7;
  const int cellW = width / visible;
  const int slotH = height / 3;
  for (int i = 0; i < visible; ++i) {
    const int petal = (selectedPetal - visible / 2 + i + n) % n;
    const int cx = x + i * cellW;
    if (petal == selectedPetal) renderer.drawRect(cx, y, cellW, height, 2, true);
    for (int slot = 0; slot < 3; ++slot) {
      char label[8];
      if (petal == n - 1) {
        const char* const u[3] = {util.top, util.mid, util.bottom};
        snprintf(label, sizeof(label), "%s", u[slot]);
      } else {
        const char c = ring.petals[petal][slot];
        // A space slot draws as an underscore rule so the target is visible;
        // the shipped panel draws a literal blank, which reads as "no key".
        if (c == ' ')
          snprintf(label, sizeof(label), "%s", "spc");
        else if (c == '\n')
          snprintf(label, sizeof(label), "%s", "ret");
        else
          snprintf(label, sizeof(label), "%c", c);
      }
      const int tw = renderer.getTextWidth(UI_12_FONT_ID, label);
      renderer.drawText(UI_12_FONT_ID, cx + (cellW - tw) / 2,
                        y + slot * slotH + (slotH - renderer.getLineHeight(UI_12_FONT_ID)) / 2, label);
    }
  }
}

void caption(int y, const char* text) { renderer.drawText(UI_12_FONT_ID, 12, y, text); }

}  // namespace

// One sheet per variant: several strips stacked, each at a different selected
// petal so the reachability cost is visible rather than asserted.
bool renderDaisySheet(const char* outPath, const char* title, const Ring* rings, int ringCount, const Util& util) {
  renderer.clearScreen();
  const int W = renderer.getScreenWidth();
  const int stripH = 108;
  int y = 18;

  caption(y, title);
  y += 40;

  for (int r = 0; r < ringCount; ++r) {
    // Worst-case rotation is half the ring, since Left/Right wrap.
    const int worst = (rings[r].count + 1) / 2;
    char head[96];
    snprintf(head, sizeof(head), "%s   %d petals   worst case %d presses", rings[r].name, rings[r].count + 1, worst);
    caption(y, head);
    y += 30;
    // Petal 0 selected: what the ring looks like on arrival. The utility petal
    // sits to its left because it is the last petal and the window wraps.
    drawStrip(rings[r], util, 0, 8, y, W - 16, stripH);
    y += stripH + 34;
  }

  return writeMonoPortraitBmpExternal(outPath, renderer);
}


// The Return question on its own, since the utility petal is where it lands.
// Rendered rather than described because the label has to be ASCII: the UI font
// has no U+23CE, so an arrow glyph draws nothing at all.
bool renderReturnOptions(const char* outPath) {
  using namespace daisyvariants;
  renderer.clearScreen();
  const int W = renderer.getScreenWidth();
  const int stripH = 108;
  int y = 18;

  caption(y, "WHERE RETURN LIVES");
  y += 40;

  caption(y, "1. contextual: utility petal, 3rd slot");
  y += 30;
  const Util ctxEditor{"del", "123", "ret"};
  drawStrip({"abc", CUR_ABC, 9}, ctxEditor, 0, 8, y, W - 16, stripH);
  y += stripH + 12;
  caption(y, "   same slot says ask in Claude, ok in WiFi");
  y += 40;

  caption(y, "2. dedicated ret petal, next to space");
  y += 30;
  // y z spc | ret _ _ | del 123 ok  -- one more petal to rotate past, every ring.
  static constexpr char RET_ABC[10][3] = {{'a', 'b', 'c'}, {'d', 'e', 'f'}, {'g', 'h', 'i'}, {'j', 'k', 'l'},
                                          {'m', 'n', 'o'}, {'p', 'q', 'r'}, {'s', 't', 'u'}, {'v', 'w', 'x'},
                                          {'y', 'z', ' '}, {'\n', '\n', '\n'}};
  const Util plainOk{"del", "123", "ok"};
  drawStrip({"abc", RET_ABC, 10}, plainOk, 9, 8, y, W - 16, stripH);
  y += stripH + 12;
  caption(y, "   ret petal selected; costs a petal on every ring");

  return writeMonoPortraitBmpExternal(outPath, renderer);
}

// Specimen for the two built-in editor monospace faces. Proves the generated
// glyph data actually rasterises — a header that compiles and links says
// nothing at all about whether the bitmaps are right.
bool renderEditorFontSpecimen(const char* outPath) {
  static EpdFont smR(&spacemono_12_regular), smB(&spacemono_12_bold), smI(&spacemono_12_italic),
      smBI(&spacemono_12_bolditalic);
  static EpdFontFamily spaceMono(&smR, &smB, &smI, &smBI);
  static EpdFont pxR(&ibmplexmono_12_regular), pxB(&ibmplexmono_12_bold), pxI(&ibmplexmono_12_italic),
      pxBI(&ibmplexmono_12_bolditalic);
  static EpdFontFamily plexMono(&pxR, &pxB, &pxI, &pxBI);
  renderer.insertFont(SPACEMONO_12_FONT_ID, spaceMono);
  renderer.insertFont(IBMPLEXMONO_12_FONT_ID, plexMono);

  renderer.clearScreen();
  int y = 18;
  caption(y, "EDITOR FONTS (built in, 12 pt)");
  y += 40;

  // The characters that actually matter in a monospace writing face: the 0/O
  // and 1/l/I confusables, the bracket pairs, and the markdown markers.
  const char* lines[] = {
      "The quick brown fox jumps",
      "0O1lI {} [] () <> #*_`~ |/",
      "const int x = arr[i] * 2;",
      "# head **bold** _italic_",
  };

  struct Face {
    const char* name;
    int id;
  };
  const Face faces[] = {{"Space Mono", SPACEMONO_12_FONT_ID}, {"IBM Plex Mono", IBMPLEXMONO_12_FONT_ID}};

  for (const Face& face : faces) {
    caption(y, face.name);
    y += 28;
    const int lh = renderer.getLineHeight(face.id);
    for (const char* l : lines) {
      renderer.drawText(face.id, 12, y, l);
      y += lh;
    }
    // All four styles, every one of which MarkdownSpans can ask for.
    y += 10;
    renderer.drawText(face.id, 12, y, "regular", true, EpdFontFamily::REGULAR);
    renderer.drawText(face.id, 120, y, "bold", true, EpdFontFamily::BOLD);
    renderer.drawText(face.id, 220, y, "italic", true, EpdFontFamily::ITALIC);
    renderer.drawText(face.id, 330, y, "bolditalic", true, EpdFontFamily::BOLD_ITALIC);
    y += lh + 30;
  }

  return writeMonoPortraitBmpExternal(outPath, renderer);
}

bool renderDaisyVariants(const char* outDir) {
  using namespace daisyvariants;
  char path[256];

  // The utility petal. "ret" not an arrow glyph: the UI font has no U+23CE, so
  // an arrow renders blank — the same trap that already broke the side-button
  // hints on this branch.
  const Util kUtilToday{"del", "123", "ok"};
  const Util kUtilEditor{"del", "123", "ret"};
  const Util kUtilCycle{"del", "sym", "ret"};

  const Ring today[] = {{"abc", CUR_ABC, 9}, {"123", CUR_NUM, 11}};
  snprintf(path, sizeof(path), "%s/daisy_today.bmp", outDir);
  if (!renderDaisySheet(path, "TODAY: no Return; ( and ) split", today, 2, kUtilToday))
    return false;

  const Ring optA[] = {{"abc", A_ABC, 9}, {"123", A_NUM, 8}, {"sym", A_SYM, 7}};
  snprintf(path, sizeof(path), "%s/daisy_optionA.bmp", outDir);
  if (!renderDaisySheet(path, "OPTION A: three rings", optA, 3, kUtilCycle)) return false;

  const Ring optB[] = {{"abc", B_ABC, 9}, {"123", B_NUM, 15}};
  snprintf(path, sizeof(path), "%s/daisy_optionB.bmp", outDir);
  if (!renderDaisySheet(path, "OPTION B: two rings", optB, 2, kUtilEditor)) return false;

  snprintf(path, sizeof(path), "%s/daisy_return.bmp", outDir);
  if (!renderReturnOptions(path)) return false;

  snprintf(path, sizeof(path), "%s/editor_fonts.bmp", outDir);
  if (!renderEditorFontSpecimen(path)) return false;

  printf("wrote daisy_today.bmp, daisy_optionA.bmp, daisy_optionB.bmp to %s\n", outDir);
  return true;
}
