find_package(EXPAT REQUIRED)

# Upstream 652ae0d83's suite. Fork: the parser also raises book notes and wires
# the XML encoding support, so their real sources are linked here.
add_executable(ContentOpfParserTest
  ContentOpfParserTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ContentOpfParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/XmlEncodingSupport.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/XmlEncodingTables.cpp
  ${REPO_ROOT}/lib/Epub/Epub/BookNotes.cpp
)

target_include_directories(ContentOpfParserTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub/parsers
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Serialization
)

target_link_libraries(ContentOpfParserTest PRIVATE
  crosspoint_test_common
  EXPAT::EXPAT
  GTest::gtest_main
)

gtest_discover_tests(ContentOpfParserTest)
