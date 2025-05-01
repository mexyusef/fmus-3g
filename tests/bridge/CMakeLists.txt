# Bridge tests CMakeLists.txt

# Source files for the bridge tests
set(FMUS_BRIDGE_TEST_SOURCES
    bridge_test.cpp
)

# Create the test executable
add_executable(fmus_bridge_tests ${FMUS_BRIDGE_TEST_SOURCES})

# Set include directories
target_include_directories(fmus_bridge_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${GTEST_INCLUDE_DIR}
)

# Link dependencies
target_link_libraries(fmus_bridge_tests
    PRIVATE
        fmus_core
        fmus_media
        fmus_rtp
        fmus_network
        fmus_sip
        fmus_webrtc
        fmus_bridge
        ${GTEST_LIBRARY}
        ${GTEST_MAIN_LIBRARY}
)

# Add test to CTest
add_test(NAME BridgeTests COMMAND fmus_bridge_tests)

# Make the test depend on the copy_dlls target
add_dependencies(fmus_bridge_tests copy_dlls)