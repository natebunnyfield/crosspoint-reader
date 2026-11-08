// When may a store's serialized JSON replace its file? (fork survey #16)
//
// The two failures it refuses only happen under heap pressure on the device,
// so the test builds them directly: a JsonDocument whose allocator stops
// granting memory part-way through filling it (ArduinoJson then drops members
// and sets overflowed()), and serialized text shorter than measureJson() says,
// which is what a String that could not grow leaves behind.
#include <JsonSaveGuard.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

namespace {

// Grants the first `budget` allocations, then fails every later one.
class FailingAllocator : public ArduinoJson::Allocator {
 public:
  explicit FailingAllocator(int budget) : budget_(budget) {}
  void* allocate(size_t n) override { return budget_-- > 0 ? malloc(n) : nullptr; }
  void deallocate(void* p) override { free(p); }
  void* reallocate(void* p, size_t n) override { return budget_-- > 0 ? realloc(p, n) : nullptr; }

 private:
  int budget_;
};

void fillLikeASettingsStore(JsonDocument& doc) {
  for (int i = 0; i < 200; i++) {
    doc["setting_" + std::to_string(i)] = std::string("value number ") + std::to_string(i);
  }
}

}  // namespace

TEST(JsonSaveGuard, AWholeDocumentSerializedInFullIsSaved) {
  JsonDocument doc;
  fillLikeASettingsStore(doc);
  std::string out;
  const size_t written = serializeJson(doc, out);
  EXPECT_TRUE(jsonsave::serializedIsComplete(doc, written, out.size()));
}

TEST(JsonSaveGuard, ADocumentThatRanOutOfPoolIsRefused) {
  FailingAllocator alloc(3);
  JsonDocument doc(&alloc);
  fillLikeASettingsStore(doc);
  ASSERT_TRUE(doc.overflowed()) << "the allocator should have run out while the store filled the document";
  std::string out;
  const size_t written = serializeJson(doc, out);
  // The text is well-formed and self-consistent -- just missing members. That
  // is exactly what used to be written over the good file.
  EXPECT_FALSE(jsonsave::serializedIsComplete(doc, written, out.size()));
}

TEST(JsonSaveGuard, TextCutShortIsRefused) {
  JsonDocument doc;
  fillLikeASettingsStore(doc);
  const size_t full = measureJson(doc);
  EXPECT_FALSE(jsonsave::serializedIsComplete(doc, full / 2, full / 2));
  EXPECT_FALSE(jsonsave::serializedIsComplete(doc, full, full / 2));
}

TEST(JsonSaveGuard, AnEmptyDocumentIsRefused) {
  JsonDocument doc;
  EXPECT_FALSE(jsonsave::serializedIsComplete(doc, 0, 0));
}
