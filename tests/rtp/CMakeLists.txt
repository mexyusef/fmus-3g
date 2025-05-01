set(RTP_TEST_SOURCES
    rtp_test.cpp
)

# Add RTP tests to the test executable
target_sources(fmus_tests PRIVATE ${RTP_TEST_SOURCES})