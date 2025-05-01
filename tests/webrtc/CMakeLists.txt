# WebRTC tests CMakeLists.txt

# Source files for the WebRTC tests
set(FMUS_WEBRTC_TEST_SOURCES
    webrtc_test.cpp
)

# Create the test executable
add_executable(fmus_webrtc_tests ${FMUS_WEBRTC_TEST_SOURCES})

# Set include directories
target_include_directories(fmus_webrtc_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${GTEST_INCLUDE_DIR}
)

# Link dependencies
target_link_libraries(fmus_webrtc_tests
    PRIVATE
        fmus_core
        fmus_media
        fmus_rtp
        fmus_network
        fmus_webrtc
        ${GTEST_LIBRARY}
        ${GTEST_MAIN_LIBRARY}
)

# Add test to CTest
add_test(NAME WebRtcTests COMMAND fmus_webrtc_tests)

# Make the test depend on the copy_dlls target
add_dependencies(fmus_webrtc_tests copy_dlls)