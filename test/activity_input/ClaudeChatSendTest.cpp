// Claude chat: pressing the on-screen keyboard's OK key.
//
// THE BUG THIS EXISTS FOR
//
// Pressing OK killed the process, every time:
//
//   [INF] [CLAUDEUI] phase: closing keyboard
//   Assertion failed: (!holdingRenderLock && "Cannot call
//   requestUpdateAndWait() while holding RenderLock"), function
//   requestUpdateAndWait, file ActivityManager.cpp, line 326.
//
// handlePanelKey() takes a RenderLock for the whole switch — correct, since
// typing mutates buf under the render task's feet — and the Done arm called
// send() inside it. send() paints a phase per step through
// requestUpdateAndWait(), which BLOCKS on the render task; holding the render
// lock while waiting for that task is the deadlock the assert exists to catch,
// so the very first phase ("closing keyboard") aborted.
//
// The invariant: a key handler may hold the render lock, or it may wait on a
// render, but never both at once. Recorded by the harness's ActivityManager
// double as counters().updateAndWaitsHoldingRenderLock — the host cannot use
// the real assert, which takes the process down with it.
//
// WHAT IS REAL HERE AND WHAT IS NOT
//
// ClaudeChatActivity, KeyboardPanel, TextBuffer and the 13-Grid layout are the
// shipped code, unmodified, driven through the real MappedInputManager on real
// frame boundaries. blekbd (NimBLE) and claudechat::runExchange (WiFi + TLS +
// SD) are doubles below: both reach ESP-IDF, and neither is what is under test
// — the defect is entirely in who holds the lock when the phase is painted.
#include <HalGPIO.h>
#include <gtest/gtest.h>

#include <string>

#include "CrossPointSettings.h"
#include "HostHarness.h"
#include "activities/util/ClaudeChatActivity.h"
#include "notes/BleHidHost.h"
#include "notes/ClaudeChat.h"
#include "notes/Grid13Layout.h"

// ---------------------------------------------------------------------------
// Doubles for the two radios. Header signatures, no stack behind them.
// ---------------------------------------------------------------------------
namespace blekbd {
namespace {
State g_state = State::Off;
}
bool canStart() { return true; }
bool hasBondedKeyboard() { return false; }  // on-screen keyboard only, the default
void disconnectKeepingBond() {}
void forgetAllBonds() {}
bool begin() {
  g_state = State::Scanning;
  return true;
}
void end() { g_state = State::Off; }
State state() { return g_state; }
const char* stateName() { return "Off"; }
const char* peerName() { return ""; }
int popChar() { return -1; }
uint32_t lastKeyMillis() { return 0; }
uint32_t notifyCount() { return 0; }
uint32_t droppedCount() { return 0; }
}  // namespace blekbd

namespace claudechat {
namespace {
int g_exchanges = 0;
}
Result runExchange(const std::string&, void (*statusCb)(void*, const char*), void* ctx) {
  ++g_exchanges;
  // The real one reports several phases; one is enough to prove the callback
  // re-enters the activity while send() is still on the stack.
  if (statusCb != nullptr) statusCb(ctx, "asking Claude");
  Result r;
  r.ok = true;
  r.responseText = "an answer";
  return r;
}
int exchanges() { return g_exchanges; }
void resetExchanges() { g_exchanges = 0; }
}  // namespace claudechat

namespace {

// 13-Grid is the default layout, so Ok is reachable by a fixed walk from the
// panel's start position (row 0, col 0): down to Ok's row, then right to its
// column. The column-anchor rules in KeyboardPanel::moveRow are what make that
// walk exact, and every row is a full 13 cells so column 0 survives the descent.
//
// BOTH COORDINATES ARE DERIVED. They were hardcoded 4 and 2, which broke twice
// in two days -- once when the symbol rows landed and once when Ok moved off a
// bottom row of its own onto the end of the symbol row. A stale count does not
// fail as "the layout moved"; it fails as "OK did not reach send()", pointing
// at the code under test.
struct OkCell {
  int row = 0;
  int col = 0;
};
inline OkCell findOk() {
  for (int r = 0; r < grid13::SL_LAYOUT.rowCount; ++r) {
    const fui::KeyboardRow& row = grid13::SL_LAYOUT.rows[r];
    for (int c = 0; c < row.count; ++c) {
      if (row.keys[c].kind == fui::KeyKind::Ok) return OkCell{r, c};
    }
  }
  return OkCell{-1, -1};
}
const OkCell kOk = findOk();
const int kRowsDownToBottom = kOk.row;
const int kColsRightToOk = kOk.col;

class ClaudeChatSend : public ::testing::Test {
 protected:
  void SetUp() override {
    host::reset();
    claudechat::resetExchanges();
    SETTINGS.keyboardLayout = CrossPointSettings::KEYBOARD_GRID13;
    host::setRootActivity(std::make_unique<ClaudeChatActivity>(host::renderer(), host::input()));
    host::frame();  // the manager applies a pending swap on a frame; an edge
                    // delivered before the activity is current would be lost
    host::resetCounters();
  }
  void TearDown() override { host::reset(); }
};

TEST_F(ClaudeChatSend, OkKeyAsksClaudeWithoutWaitingUnderTheRenderLock) {
  // Type one character first: send() returns early on an empty buffer, and an
  // OK press that returns early proves nothing.
  host::tap(HalGPIO::BTN_CONFIRM);  // row 0 col 0 of 13-Grid is "1"

  for (int i = 0; i < kRowsDownToBottom; ++i) host::tap(HalGPIO::BTN_DOWN);
  for (int i = 0; i < kColsRightToOk; ++i) host::tap(HalGPIO::BTN_RIGHT);

  host::tap(HalGPIO::BTN_CONFIRM);  // OK

  ASSERT_EQ(claudechat::exchanges(), 1) << "OK did not reach send() — the walk to the OK key is wrong, "
                                           "so the rest of this test proves nothing";
  EXPECT_GE(host::counters().updateAndWaits, 2) << "expected the 'closing keyboard' phase paint and one more "
                                                   "from the exchange callback";
  EXPECT_EQ(host::counters().updateAndWaitsHoldingRenderLock, 0)
      << "requestUpdateAndWait() called with the render lock held: on device this is "
         "the abort at ActivityManager.cpp:326, which is what pressing OK did";
}

// The other half of the invariant: typing must not wait on a render at all.
// handlePanelKey holds the render lock across the whole switch — buf and the
// panel's selection are read by render() on its own task — so a wait added to
// any other arm would abort exactly as the Done arm did.
TEST_F(ClaudeChatSend, TypingAKeyNeverWaitsOnTheRenderTask) {
  host::tap(HalGPIO::BTN_CONFIRM);  // types "1": no send, no phase

  EXPECT_EQ(claudechat::exchanges(), 0) << "a character key must not reach send()";
  EXPECT_EQ(host::counters().updateAndWaits, 0) << "typing waited on the render task";
  EXPECT_EQ(host::counters().updateAndWaitsHoldingRenderLock, 0);
}

// ST-006. The prompt is a multi-line composer, and the host has one Return key
// to split between the text and the Confirm button. It can only tell them
// apart from what the activity announces, and an editor that says "Single"
// gets Return routed to BTN_CONFIRM — so pressing it on a hardware keyboard
// presses Select on the on-screen panel instead of breaking the line.
//
// The same one-line announcement is what NoteEditorActivity makes; it is not
// linked in this target (MarkdownRender, TextBuffer and BleHidHost come with
// it), and is covered end-to-end by the simulator instead — the simulator
// repo's tests/test_text_entry.sh drives a real Return through Create Note.
TEST_F(ClaudeChatSend, AnnouncesTheComposerAsMultiLineSoReturnBreaksTheLine) {
  ASSERT_TRUE(host::buttons().simTextEntryActive()) << "the composer never announced an open field at all";
  EXPECT_EQ(host::buttons().simTextEntryLines(), HalGPIO::TextEntryLines::Multi)
      << "a multi-line editor announced itself as a one-line field: a host keyboard's "
         "Return would press Select instead of inserting a newline (ST-006)";
}

}  // namespace
