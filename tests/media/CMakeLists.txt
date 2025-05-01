# Media tests CMake configuration

# Define source files
set(MEDIA_TEST_SOURCES
    media_pipeline_test.cpp
)

# Create test executable
add_executable(fmus_media_tests
    ${MEDIA_TEST_SOURCES}
)

# Set include directories
target_include_directories(fmus_media_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${GTEST_INCLUDE_DIR}
)

# Link dependencies
target_link_libraries(fmus_media_tests PRIVATE
    fmus_core
    fmus_media
    fmus_network
    ${GTEST_LIBRARY}
    ${GTEST_MAIN_LIBRARY}
    ${Boost_LIBRARIES}
)

# Set compile definitions
target_compile_definitions(fmus_media_tests PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Set compile options
target_compile_options(fmus_media_tests PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

# Add test to CTest
add_test(
    NAME MediaTests
    COMMAND fmus_media_tests
)

# Make the test depend on the copy_dlls target
add_dependencies(fmus_media_tests copy_dlls)