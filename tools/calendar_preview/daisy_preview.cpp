// Renders the daisywheel strip through the REAL GfxRenderer, for the ring
// redesign decision. Decision aid only — see daisy_variants.h.
//
// Geometry is lifted verbatim from KeyboardPanel::render's daisy branch: a
// 7-petal window centered on the selected petal, each petal three slots stacked
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

#include "notes/DaisyRings.h"
#include "fontIds.h"

extern GfxRenderer renderer;
extern bool simInit();
extern bool writeMonoPortraitBmpExternal(const char* path, const GfxRenderer& r);

namespace {

// Was daisy_variants.h, which carried three competing ring proposals. The owner
// ruled on 2026-08-06, so the proposals are gone and this renders the SHIPPED
// table from src/notes/DaisyRings.h directly.
struct Ring {
  const char* name;
  const char (*petals)[3];
  int count;
};
struct Util {
  const char* top;
  const char* mid;
  const char* bottom;
};

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
          snprintf(label, sizeof(label), "%s", "SPC");
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


// Specimen for the two built-in editor monospace faces. Proves the generated
// glyph data actually rasterises — a header that compiles and links says
// nothing at all about whether the bitmaps are right.
bool renderEditorFontSpecimen(const char* outPath) {
  // Space Mono was removed from the app on 2026-08-15 and this specimen still
  // named it, so the harness stopped building the moment the headers went. The
  // iA Writer Mono cut takes its place -- a shipped editor face, so the
  // specimen keeps proving two real families rasterise rather than one.
  static EpdFont iaR(&iawritermono_12_regular), iaB(&iawritermono_12_bold), iaI(&iawritermono_12_italic),
      iaBI(&iawritermono_12_bolditalic);
  static EpdFontFamily iaWriterMono(&iaR, &iaB, &iaI, &iaBI);
  static EpdFont pxR(&ibmplexmono_12_regular), pxB(&ibmplexmono_12_bold), pxI(&ibmplexmono_12_italic),
      pxBI(&ibmplexmono_12_bolditalic);
  static EpdFontFamily plexMono(&pxR, &pxB, &pxI, &pxBI);
  renderer.insertFont(IAWRITERMONO_12_FONT_ID, iaWriterMono);
  renderer.insertFont(IBMPLEXMONO_12_FONT_ID, plexMono);

  renderer.clearScreen();
  int y = 18;
  caption(y, "EDITOR FONTS (built in, 12 pt)");
  y += 40;

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
  const Face faces[] = {{"iA Writer Mono", IAWRITERMONO_12_FONT_ID}, {"IBM Plex Mono", IBMPLEXMONO_12_FONT_ID}};

  for (const Face& face : faces) {
    caption(y, face.name);
    y += 28;
    const int lh = renderer.getLineHeight(face.id);
    for (const char* l : lines) {
      renderer.drawText(face.id, 12, y, l);
      y += lh;
    }
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
  char path[256];
  // The SHIPPED rings, straight from src/notes/DaisyRings.h — not a copy, so
  // this sheet cannot drift from what the device draws.
  const Util utilEditor{"DEL", "123", "RET"};
  const Ring rings[] = {{"abc", daisyrings::ABC_RING, daisyrings::ABC_CHAR_PETALS},
                        {"123", daisyrings::NUM_RING, daisyrings::NUM_CHAR_PETALS}};
  snprintf(path, sizeof(path), "%s/daisy_shipped.bmp", outDir);
  if (!renderDaisySheet(path, "DAISY RINGS (shipped)", rings, 2, utilEditor)) return false;

  // The four bracket petals and the space petal, centered so they are readable.
  renderer.clearScreen();
  const int W = renderer.getScreenWidth();
  int y = 18;
  caption(y, "BRACKET PETALS  up=open  mid=related  down=close");
  y += 40;
  drawStrip(rings[1], utilEditor, 11, 8, y, W - 16, 108);
  y += 130;
  caption(y, "...and the space petal, bottom slot like abc");
  y += 32;
  drawStrip(rings[1], utilEditor, 14, 8, y, W - 16, 108);
  snprintf(path, sizeof(path), "%s/daisy_brackets.bmp", outDir);
  if (!writeMonoPortraitBmpExternal(path, renderer)) return false;

  snprintf(path, sizeof(path), "%s/editor_fonts.bmp", outDir);
  if (!renderEditorFontSpecimen(path)) return false;

  printf("wrote daisy_shipped.bmp, daisy_brackets.bmp, editor_fonts.bmp to %s\n", outDir);
  return true;
}
