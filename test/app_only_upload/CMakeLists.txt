# scripts/app_only_upload.py gates `pio run -e picco -t upload`. Its PSRAM
# check reads the linker map, and a wrong answer either flashes an image that
# never initializes PSRAM or refuses every upload. Pure Python, synthetic maps
# shaped like the real ones: no build artifact needed.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME AppOnlyUploadTest
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/app_only_upload_test.py)
