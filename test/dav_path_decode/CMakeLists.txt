# How the WebDAV handler decodes a request path and a Destination path.
# Header-only: '+' stays a '+' (it is a space only in a query or a form), and
# every other rule matches the server's own decoder.
add_executable(DavPathDecodeTest DavPathDecodeTest.cpp)
target_include_directories(DavPathDecodeTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(DavPathDecodeTest PRIVATE GTest::gtest_main)
add_test(NAME DavPathDecodeTest COMMAND DavPathDecodeTest)
