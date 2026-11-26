# The key store's pure half (src/util/KeyStoreCore.h): names, trimming, the
# last-4 rule and the one-line entry text, plus source gates that no key is
# read from or written to the card. Owner 2026-11-26;
# docs/keys-and-paste-2026-11-26.md.
add_executable(KeyStoreTest KeyStoreTest.cpp)
target_include_directories(KeyStoreTest BEFORE PRIVATE
  ${REPO_ROOT}/src
)
target_compile_definitions(KeyStoreTest PRIVATE REPO_ROOT_DIR="${REPO_ROOT}")
target_compile_options(KeyStoreTest PRIVATE -Wall -Wextra -pedantic)
target_link_libraries(KeyStoreTest PRIVATE GTest::gtest_main)
add_test(NAME KeyStoreTest COMMAND KeyStoreTest)
