# The address the file-transfer screen paints and QR-encodes. Header-only:
# the screen passes the port it learned from the server, and on a host build
# that port is not 80, which is the whole reason this exists.
add_executable(PeerUrlTest PeerUrlTest.cpp)
target_include_directories(PeerUrlTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(PeerUrlTest PRIVATE GTest::gtest_main)
add_test(NAME PeerUrlTest COMMAND PeerUrlTest)
