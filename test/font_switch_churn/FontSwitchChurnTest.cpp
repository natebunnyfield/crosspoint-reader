// Font-switch churn.
//
// Targets a real leak: FontSelectionActivity's preview path used to call
// ensureLoaded() only inside the SD branch, so picking a BUILT-IN font after an
// SD family cleared sdFontFamilyName without ever calling unloadAll(). The
// previous .cpfont stayed resident — interval tables, kern/ligature tables and
// glyph cache — and the waste compounded with every switch. On a 380KB device
// that is enough to exhaust the heap.
//
// The invariants, at the layer that actually owns residency:
//
//   1. At most ONE family is resident at any time, and going "built-in" leaves
//      ZERO resident — no stale registration in the renderer.
//   2. The resident point size is the one findClosestReaderSize() picks for the
//      current size slot (the slot is ordinal and resolves per-family).
//   3. Live heap does not grow across repeated switches.
//
// Drives the REAL SdCardFontRegistry over the real .cpfont tree in fs_/.fonts
// (see stubs/HalStorage.h), so this exercises production discovery and loading
// rather than a mock.

#include <gtest/gtest.h>

#include <GfxRenderer.h>
#include <SdCardFontManager.h>
#include <SdCardFontRegistry.h>

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <new>
#include <vector>

HalDisplay display;

// ---------------------------------------------------------------------------
// Live-heap accounting.
//
// Global new/delete are replaced so every allocation on the font path is
// counted. SdCardFont and its tables all go through global new, which is what
// makes invariant 3 observable at all — there is no heap API on the host that
// matches the device's ESP.getFreeHeap().
// ---------------------------------------------------------------------------
namespace {
std::size_t g_liveBytes = 0;
std::size_t g_liveBlocks = 0;
constexpr std::size_t kHeaderMagic = 0xC0FFEEu;

struct BlockHeader {
  std::size_t size;
  std::size_t magic;
};
}  // namespace

void* operator new(std::size_t n) {
  void* raw = std::malloc(n + sizeof(BlockHeader));
  if (!raw) throw std::bad_alloc();
  auto* h = static_cast<BlockHeader*>(raw);
  h->size = n;
  h->magic = kHeaderMagic;
  g_liveBytes += n;
  g_liveBlocks++;
  return static_cast<char*>(raw) + sizeof(BlockHeader);
}

void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
  void* raw = std::malloc(n + sizeof(BlockHeader));
  if (!raw) return nullptr;
  auto* h = static_cast<BlockHeader*>(raw);
  h->size = n;
  h->magic = kHeaderMagic;
  g_liveBytes += n;
  g_liveBlocks++;
  return static_cast<char*>(raw) + sizeof(BlockHeader);
}

void* operator new[](std::size_t n) { return ::operator new(n); }
void* operator new[](std::size_t n, const std::nothrow_t& t) noexcept { return ::operator new(n, t); }

void operator delete(void* p) noexcept {
  if (!p) return;
  auto* h = reinterpret_cast<BlockHeader*>(static_cast<char*>(p) - sizeof(BlockHeader));
  if (h->magic == kHeaderMagic) {
    g_liveBytes -= h->size;
    g_liveBlocks--;
    h->magic = 0;
    std::free(h);
    return;
  }
  std::free(p);  // not ours (pre-main allocation)
}
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }

namespace {

// Size slots, mirroring CrossPointSettings::FONT_SIZE (ordinal, not absolute).
constexpr uint8_t kSizeSlots[] = {0, 1, 2, 3};

class FontSwitchChurn : public ::testing::Test {
 protected:
  void SetUp() override {
    renderer_ = new GfxRenderer(display);
    renderer_->begin();

    if (!registry_.discover() || registry_.getFamilyCount() < 2) {
      GTEST_SKIP() << "needs >=2 SD font families under fs_/.fonts (found " << registry_.getFamilyCount()
                   << "); run from the repo root or set CROSSPOINT_TEST_SD";
    }
  }

  void TearDown() override {
    if (renderer_) {
      manager_.unloadAll(*renderer_);
      delete renderer_;
      renderer_ = nullptr;
    }
  }

  // SdCardFontManager::loadFamily() takes a POINT SIZE: resolving a reader slot
  // onto a family's own ramp is policy and lives in SdCardFontSystem, not in the
  // manager. These tests are written in slots (the unit that is stable across
  // families), so resolve here at the call boundary.
  static uint8_t ptForSlot(const SdCardFontFamilyInfo& fam, uint8_t slot) {
    const auto* f = fam.findClosestReaderSize(slot);
    return f ? f->pointSize : 0;
  }

  // Invariant 1+2 for a family that was just loaded.
  void expectExactlyResident(const SdCardFontFamilyInfo& fam, uint8_t slot) {
    EXPECT_EQ(manager_.currentFamilyName(), fam.name);
    EXPECT_EQ(renderer_->getSdCardFonts().size(), 1u)
        << "exactly one SD font may be registered; found " << renderer_->getSdCardFonts().size();

    const auto* expected = fam.findClosestReaderSize(slot);
    ASSERT_NE(expected, nullptr) << fam.name << " has no file for slot " << static_cast<int>(slot);
    EXPECT_EQ(manager_.currentPointSize(), expected->pointSize)
        << "resident point size must be findClosestReaderSize(slot=" << static_cast<int>(slot) << ") for "
        << fam.name;
    EXPECT_NE(manager_.getFontId(fam.name), 0);
  }

  // Going built-in: nothing may stay resident or registered.
  void expectNothingResident() {
    EXPECT_TRUE(manager_.currentFamilyName().empty()) << "stale family: " << manager_.currentFamilyName();
    EXPECT_EQ(manager_.currentPointSize(), 0);
    EXPECT_TRUE(renderer_->getSdCardFonts().empty())
        << renderer_->getSdCardFonts().size() << " SD font(s) still registered after going built-in";
  }

  SdCardFontRegistry registry_;
  SdCardFontManager manager_;
  GfxRenderer* renderer_ = nullptr;
};

// Switching family to family must never leave the previous one resident.
TEST_F(FontSwitchChurn, FamilyToFamilyKeepsExactlyOneResident) {
  const auto& fams = registry_.getFamilies();
  for (size_t i = 0; i < fams.size(); ++i) {
    const auto& fam = fams[i];
    const uint8_t slot = kSizeSlots[i % 4];
    ASSERT_TRUE(manager_.loadFamily(fam, *renderer_, ptForSlot(fam, slot))) << "failed to load " << fam.name;
    expectExactlyResident(fam, slot);
  }
}

// The regression this suite exists for: SD family, then a built-in. The
// built-in path must unload — otherwise the .cpfont stays resident forever.
TEST_F(FontSwitchChurn, SelectingBuiltinAfterSdFamilyUnloadsTheCpfont) {
  const auto& fams = registry_.getFamilies();

  for (int round = 0; round < 4; ++round) {
    const auto& fam = fams[static_cast<size_t>(round) % fams.size()];
    ASSERT_TRUE(manager_.loadFamily(fam, *renderer_, ptForSlot(fam, 1)));
    expectExactlyResident(fam, 1);

    // "User picked a built-in": sdFontFamilyName cleared => unloadAll.
    manager_.unloadAll(*renderer_);
    expectNothingResident();
  }
}

// Every slot, every family: the resident size must track the slot mapping,
// including families whose available sizes do not cover the built-in ramp.
TEST_F(FontSwitchChurn, ResidentSizeAlwaysMatchesClosestReaderSize) {
  for (const auto& fam : registry_.getFamilies()) {
    for (const uint8_t slot : kSizeSlots) {
      ASSERT_TRUE(manager_.loadFamily(fam, *renderer_, ptForSlot(fam, slot))) << fam.name << " slot " << static_cast<int>(slot);
      expectExactlyResident(fam, slot);
    }
  }
}

// Invariant 3. Churn family switches and SD<->built-in transitions, and assert
// live heap returns to the same level. A missed unloadAll shows up here as
// monotonic growth even when the residency assertions above still pass.
TEST_F(FontSwitchChurn, HeapDoesNotGrowAcrossSwitches) {
  const auto& fams = registry_.getFamilies();

  // Warm up: first load populates one-time caches, so measure from a settled
  // state rather than counting first-touch allocations as growth.
  ASSERT_TRUE(manager_.loadFamily(fams[0], *renderer_, ptForSlot(fams[0], 1)));
  manager_.unloadAll(*renderer_);

  std::vector<std::size_t> liveAfterRound;
  for (int round = 0; round < 6; ++round) {
    for (size_t i = 0; i < fams.size(); ++i) {
      ASSERT_TRUE(manager_.loadFamily(fams[i], *renderer_, ptForSlot(fams[i], kSizeSlots[i % 4])));
    }
    manager_.unloadAll(*renderer_);  // back to built-in
    expectNothingResident();
    liveAfterRound.push_back(g_liveBytes);
  }

  // Settled rounds must stay within a noise band of the second one. Comparing
  // to [1] rather than [0] tolerates lazily-built caches that populate once.
  //
  // Why a band and not equality: measured jitter across settled rounds is
  // non-monotonic and a few dozen bytes (measured: 40 and 72), coming from
  // allocator and std::string/vector capacity churn in the registry, not from
  // retained font data. The signal this test is for is a whole retained
  // .cpfont, which LeakedFamilyIsDetectable measures at 3198 bytes — 6x this
  // band, and larger still if more than one is retained.
  constexpr long long kNoiseToleranceBytes = 512;
  for (size_t i = 2; i < liveAfterRound.size(); ++i) {
    const long long delta =
        static_cast<long long>(liveAfterRound[i]) - static_cast<long long>(liveAfterRound[1]);
    EXPECT_LE(std::llabs(delta), kNoiseToleranceBytes)
        << "live heap moved between settled rounds: round1=" << liveAfterRound[1] << " round" << i << "="
        << liveAfterRound[i] << " (delta " << delta << " bytes) — a switch is leaking";
  }
}

// Control for the test above: proves the heap instrument actually SEES a
// retained family. Without this, HeapDoesNotGrowAcrossSwitches passing would
// be meaningless — it could be measuring nothing at all.
//
// This is precisely the shape of the bug that was fixed: load an SD family,
// then "go built-in" WITHOUT calling unloadAll(). The .cpfont stays resident.
TEST_F(FontSwitchChurn, LeakedFamilyIsDetectable) {
  const auto& fams = registry_.getFamilies();

  ASSERT_TRUE(manager_.loadFamily(fams[0], *renderer_, ptForSlot(fams[0], 1)));
  manager_.unloadAll(*renderer_);
  const std::size_t settled = g_liveBytes;

  // The missing-unloadAll case. loadFamily() internally unloads the PREVIOUS
  // family, so to model "never unloaded" we simply leave one loaded and
  // measure what a single retained family costs.
  ASSERT_TRUE(manager_.loadFamily(fams[0], *renderer_, ptForSlot(fams[0], 1)));
  const long long retained = static_cast<long long>(g_liveBytes) - static_cast<long long>(settled);

  EXPECT_GT(retained, 2048) << "a resident .cpfont should cost multiple KB; the heap instrument may be broken";

  manager_.unloadAll(*renderer_);
  EXPECT_LE(std::llabs(static_cast<long long>(g_liveBytes) - static_cast<long long>(settled)), 512)
      << "unloadAll did not return the heap to its settled level";
}

}  // namespace
