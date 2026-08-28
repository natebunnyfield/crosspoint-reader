# scripts/stamp_app_desc.py edits a linked firmware image on the release path,
# so its failure mode is a device that will not boot rather than a build that
# will not compile. Pure Python and synthetic images: no build artifact needed,
# which is what makes it cheap enough to actually run.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME StampAppDescTest
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/stamp_app_desc_test.py)
