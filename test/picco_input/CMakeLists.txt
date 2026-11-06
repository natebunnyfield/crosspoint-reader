# BOOX Picco input (src/components/PiccoInput.h): the rocker remap and the
# gesture decoder that turns one touch contact into a synthesized button.
# Header-only and pure; MappedInputManager.h is included for its Button enum
# only, against stubs/HalGPIO.h. No renderer, no Arduino, no device.
add_executable(PiccoInputTest PiccoInputTest.cpp)
target_include_directories(PiccoInputTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
)
target_compile_definitions(PiccoInputTest PRIVATE FREEINK_DEVICE_PICCO=1)
target_compile_options(PiccoInputTest PRIVATE -Wall -Wextra -pedantic)
target_link_libraries(PiccoInputTest PRIVATE GTest::gtest_main)
add_test(NAME PiccoInputTest COMMAND PiccoInputTest)
