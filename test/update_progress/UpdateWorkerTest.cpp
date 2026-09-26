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
