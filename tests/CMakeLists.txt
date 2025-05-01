# Tests CMake configuration

# Enable testing
enable_testing()

# Test executables
set(TEST_TARGETS
    core_tests
    net_tests
    media_tests
    ivrjs_tests
    api_tests
)

# Core tests
add_executable(core_tests
    core/buffer_test.cpp
    core/memory_pool_test.cpp
    core/thread_pool_test.cpp
    core/logger_test.cpp
    core/config_test.cpp
)

target_link_libraries(core_tests PRIVATE
    fmus-core
    GTest::GTest
    GTest::Main
)

# Network tests
add_executable(net_tests
    net/sip_test.cpp
    net/rtp_test.cpp
)

target_link_libraries(net_tests PRIVATE
    fmus-net
    GTest::GTest
    GTest::Main
)

# Media tests
add_executable(media_tests
    media/codec_test.cpp
    media/pipeline_test.cpp
    media/capture_test.cpp
    media/render_test.cpp
)

target_link_libraries(media_tests PRIVATE
    fmus-media
    GTest::GTest
    GTest::Main
)

# IVR JavaScript tests
add_executable(ivrjs_tests
    ivrjs/engine_test.cpp
    ivrjs/functions_test.cpp
)

target_link_libraries(ivrjs_tests PRIVATE
    fmus-ivrjs
    GTest::GTest
    GTest::Main
)

# API tests
add_executable(api_tests
    api/client_test.cpp
    api/server_test.cpp
    api/rest_test.cpp
    api/websocket_test.cpp
)

target_link_libraries(api_tests PRIVATE
    fmus-api
    GTest::GTest
    GTest::Main
)

# Add protocol-specific test subdirectories
add_subdirectory(sip)
add_subdirectory(webrtc)
add_subdirectory(bridge)

# Add tests to CTest
foreach(test ${TEST_TARGETS})
    add_test(NAME ${test} COMMAND ${test})
    set_tests_properties(${test} PROPERTIES
        ENVIRONMENT "PATH=${CMAKE_RUNTIME_OUTPUT_DIRECTORY};$ENV{PATH}"
    )
endforeach()

# Copy test data files
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data DESTINATION ${CMAKE_CURRENT_BINARY_DIR})