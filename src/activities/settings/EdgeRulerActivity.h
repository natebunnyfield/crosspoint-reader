#pragma once

// The Picco's edge ruler (owner 2026-11-26, "Ruler screen"): the Picco uses
// the X4's viewable insets (SDK BoardConfig.h PICCO, "viewableInsets: none
// measured"), and with no hint bar Home's menu runs to the panel's bottom edge.
// This screen measures where the bezel actually ends.
//
// Along each edge sits a row of blocks. Block N starts exactly N logical pixels
// in from that edge and is labeled N, for N = 0, 2, 4 ... 30. The bezel hides
// the blocks that start under it; the owner reads, per edge, the smallest N
// whose block's outer edge is fully visible. Those four numbers become the
// Picco's insets (docs/picco-layout-plan-2026-11-26.md, B4).
//
// A tap anywhere or Back leaves. Picco only; reached from Settings. Header-only
// so no translation unit is added (the simulator's iOS source list is
// generated, CLAUDE.md).

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "activities/RenderLock.h"
#include "fontIds.h"

class EdgeRulerActivity final : public Activity {
 public:
  EdgeRulerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("EdgeRuler", renderer, mappedInput) {}

  void onEnter() override {
    Activity::onEnter();
    requestUpdate();
  }

  void loop() override {
    int x = 0;
    int y = 0;
    mappedInput.claimScreenTaps();
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
      finish();
    }
  }

  void render(RenderLock&&) override {
    renderer.clearScreen();
    const int w = renderer.getScreenWidth();
    const int h = renderer.getScreenHeight();
    const int labelH = renderer.getLineHeight(SMALL_FONT_ID);
    char label[4];
    for (int i = 0; i < kSteps; ++i) {
      const int n = i * kStep;
      snprintf(label, sizeof(label), "%d", n);
      const int labelW = renderer.getTextWidth(SMALL_FONT_ID, label);
      // Top and bottom edges: blocks spread along x, centered.
      const int bx = (w - ((kSteps - 1) * kPitch + kBlockW)) / 2 + i * kPitch;
      renderer.fillRect(bx, n, kBlockW, kBlockDepth);
      renderer.drawText(SMALL_FONT_ID, bx + (kBlockW - labelW) / 2, n + kBlockDepth + 2, label);
      renderer.fillRect(bx, h - n - kBlockDepth, kBlockW, kBlockDepth);
      renderer.drawText(SMALL_FONT_ID, bx + (kBlockW - labelW) / 2, h - n - kBlockDepth - 2 - labelH, label);
      // Left and right edges: blocks spread along y, starting at y = 110.
      const int by = kFirstAlongSide + i * kPitchSide;
      renderer.fillRect(n, by, kBlockDepth, kBlockW);
      renderer.drawText(SMALL_FONT_ID, n + kBlockDepth + 4, by + (kBlockW - labelH) / 2, label);
      renderer.fillRect(w - n - kBlockDepth, by, kBlockDepth, kBlockW);
      renderer.drawText(SMALL_FONT_ID, w - n - kBlockDepth - 4 - labelW, by + (kBlockW - labelH) / 2, label);
    }
    // Below the side rows (they end at y = 462), so no label is crossed.
    const int textY = kFirstAlongSide + kSteps * kPitchSide + 60;
    renderer.drawCenteredText(UI_12_FONT_ID, textY, tr(STR_EDGE_RULER));
    renderer.drawCenteredText(UI_10_FONT_ID, textY + 2 * labelH, tr(STR_EDGE_RULER_HINT_1));
    renderer.drawCenteredText(UI_10_FONT_ID, textY + 3 * labelH + 6, tr(STR_EDGE_RULER_HINT_2));
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  }

 private:
  static constexpr int kStep = 2;         // logical px between block starts
  static constexpr int kSteps = 16;       // 0 .. 30
  static constexpr int kBlockW = 14;      // along the edge
  static constexpr int kBlockDepth = 18;  // in from the edge
  static constexpr int kPitch = 28;       // top/bottom: room for a 2-digit label
  static constexpr int kPitchSide = 22;   // left/right: 16 blocks span 352 px of 800
  static constexpr int kFirstAlongSide = 110;
};
