# SIP tests CMakeLists.txt

# Source files for the SIP tests
set(FMUS_SIP_TEST_SOURCES
    sip_test.cpp
)

# Create the test executable
add_executable(fmus_sip_tests ${FMUS_SIP_TEST_SOURCES})

# Set include directories
target_include_directories(fmus_sip_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${GTEST_INCLUDE_DIR}
)

# Link dependencies
target_link_libraries(fmus_sip_tests
    PRIVATE
        fmus_core
        fmus_rtp
        fmus_sip
        ${GTEST_LIBRARY}
        ${GTEST_MAIN_LIBRARY}
)

# Add test to CTest
add_test(NAME SipTests COMMAND fmus_sip_tests)

# Make the test depend on the copy_dlls target
add_dependencies(fmus_sip_tests copy_dlls)