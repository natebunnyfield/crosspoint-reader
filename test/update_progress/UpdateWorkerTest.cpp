// UpdateWorker's HOST path (std::thread), and the lock rule its callers must
// keep. Compiled with -DSIMULATOR (see CMakeLists.txt), because that is the
// only build where the step leaves the loop thread -- every other suite here
// runs it inline and would never see a threading mistake.
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include "activities/settings/UpdateWorker.h"

namespace {
std::atomic<bool> release{false};
std::atomic<int> ran{0};
void blockingStep(void*) {
  while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  ran.fetch_add(1);
}

std::string readRepoFile(const std::string& rel) {
  std::ifstream in(std::string(REPO_ROOT_PATH) + "/" + rel);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// The body of `<cls>::runStep`, up to the next function definition.
std::string methodBody(const std::string& src, const std::string& cls, const std::string& sig) {
  const std::string head = "void " + cls + "::" + sig + " {";
  const size_t a = src.find(head);
  if (a == std::string::npos) return {};
  const size_t b = src.find("\nvoid " + cls + "::", a + head.size());
  return src.substr(a, b == std::string::npos ? std::string::npos : b - a);
}

std::string runStepBody(const std::string& src, const std::string& cls) {
  const std::string head = "void " + cls + "::runStep(void* ctx) {";
  const size_t a = src.find(head);
  if (a == std::string::npos) return {};
  const size_t b = src.find("\nvoid " + cls + "::", a + head.size());
  return src.substr(a, b == std::string::npos ? std::string::npos : b - a);
}
}  // namespace

// On a host the step must NOT run inside start(): that is the whole fix -- the
// loop thread returns and the host presents while the step works.
TEST(UpdateWorkerHost, StartReturnsWhileTheStepIsStillRunning) {
  release = false;
  ran = 0;
  UpdateWorker w;
  w.start(&blockingStep, nullptr);
  EXPECT_TRUE(w.inFlight());
  EXPECT_FALSE(w.done());
  EXPECT_EQ(ran.load(), 0);
  release = true;
  w.join();
  EXPECT_EQ(ran.load(), 1);
  EXPECT_FALSE(w.inFlight());
}

TEST(UpdateWorkerHost, DoneTurnsTrueOnlyAfterTheStepReturns) {
  release = false;
  UpdateWorker w;
  w.start(&blockingStep, nullptr);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_FALSE(w.done());
  release = true;
  for (int i = 0; i < 2000 && !w.done(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  EXPECT_TRUE(w.done());
  w.join();
}

// A GATE, not a comment. A sleep's transition holds RenderLock while onExit()
// joins the worker (ActivityManager.cpp:153 -> exitActivity -> onExit), so any
// RenderLock taken on the worker deadlocks the app for good. It shipped in the
// first draft of this fix (the check-step callback took it) and was caught by
// adversarial review, 2026-09-26. Nothing reachable from runStep may take it.
TEST(UpdateWorkerHost, NoStepTakesTheRenderLock) {
  for (const char* cls : {"FontUpdateActivity", "LibraryUpdateActivity"}) {
    const std::string body = runStepBody(readRepoFile(std::string("src/activities/settings/") + cls + ".cpp"), cls);
    ASSERT_FALSE(body.empty()) << cls << "::runStep not found";
    EXPECT_EQ(body.find("RenderLock lock"), std::string::npos) << cls << "::runStep takes RenderLock on the worker";
  }
}

// A GATE for the keep-awake lease (owner ruling 2026-09-26, S-042). The host
// holds the phone's idle timer off only while the run says it is working, so
// the release on every exit path is the activity's two statements below: the
// per-tick request in loop() -- which must come BEFORE any return, or a tick
// that returns early (a step in flight, the summary screen) never releases --
// and the unconditional false in onExit() (Back, sleep, home, destroyed). The
// host side of every path is driven in crosspoint-simulator's
// tests/keep_awake_test.cpp; this pins that the firmware keeps saying it.
TEST(UpdateWorkerHost, EveryExitReleasesTheKeepAwake) {
  for (const char* cls : {"FontUpdateActivity", "LibraryUpdateActivity"}) {
    const std::string src = readRepoFile(std::string("src/activities/settings/") + cls + ".cpp");
    // Code only: the comments above the request talk about returns too.
    std::string loop;
    {
      const std::string raw = methodBody(src, cls, "loop()");
      size_t pos = 0;
      while (pos < raw.size()) {
        size_t nl = raw.find('\n', pos);
        if (nl == std::string::npos) nl = raw.size();
        const std::string line = raw.substr(pos, nl - pos);
        const size_t first = line.find_first_not_of(' ');
        if (first == std::string::npos || line.compare(first, 2, "//") != 0) loop += line + "\n";
        pos = nl + 1;
      }
    }
    ASSERT_FALSE(loop.empty()) << cls << "::loop not found";
    const size_t req = loop.find("UPD_KEEP_AWAKE(state == State::CHECKING || state == State::SYNCING);");
    ASSERT_NE(req, std::string::npos) << cls << "::loop does not state whether the run is working";
    EXPECT_LT(req, loop.find("return")) << cls << "::loop can return before releasing";
    const std::string exitBody = methodBody(src, cls, "onExit()");
    ASSERT_FALSE(exitBody.empty()) << cls << "::onExit not found";
    EXPECT_NE(exitBody.find("UPD_KEEP_AWAKE(false);"), std::string::npos) << cls << "::onExit does not release";
  }
}
