# scripts/check_board_tag.py fails a device build whose firmware.bin does not
# carry its CROSSPOINT-BOARD-V1 tag exactly once. Pure Python: it reads the real
# src/network/FirmwareBoardTag.cpp chain and checks synthetic images, so no
# build artifact is needed.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME CheckBoardTagTest
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_board_tag_test.py)
