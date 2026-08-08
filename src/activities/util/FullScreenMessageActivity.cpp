#include "FullScreenMessageActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "components/UITheme.h"
#include "fontIds.h"

void FullScreenMessageActivity::onEnter() {
  Activity::onEnter();

  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (renderer.getScreenHeight() - height) / 2;

  renderer.clearScreen();
  renderer.drawCenteredText(UI_10_FONT_ID, top, text.c_str(), true, style);

  // Launched via replaceActivity with an empty stack (ActivityManager::
  // goToFullScreenMessage): finish() lands on Home, so the hint says Home —
  // mirrors CrashActivity.
  const auto labels = mappedInput.mapLabels(tr(STR_HOME), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(refreshMode);
}

void FullScreenMessageActivity::loop() {
  int x = 0;
  int y = 0;
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
    finish();
  }
}
