set(RTP_SOURCES
    packet.cpp
    session.cpp
    transport.cpp
)

target_sources(fmus_lib PRIVATE ${RTP_SOURCES})

# Add tests
if(BUILD_TESTS)
    add_subdirectory(../../tests/rtp ${CMAKE_BINARY_DIR}/tests/rtp)
endif()