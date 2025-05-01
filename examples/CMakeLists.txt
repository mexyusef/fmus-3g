# Examples CMake configuration

# Simple client example
add_executable(simple_client
    simple_client/main.cpp
)

target_link_libraries(simple_client PRIVATE
    fmus-api
)

# Simple server example
add_executable(simple_server
    simple_server/main.cpp
)

target_link_libraries(simple_server PRIVATE
    fmus-api
)

# IVR example
add_executable(ivr_example
    ivr/main.cpp
)

target_link_libraries(ivr_example PRIVATE
    fmus-api
    fmus-ivrjs
)

# Video call example
add_executable(video_call
    video_call/main.cpp
)

target_link_libraries(video_call PRIVATE
    fmus-api
    fmus-media
)

# Conference example
add_executable(conference
    conference/main.cpp
)

target_link_libraries(conference PRIVATE
    fmus-api
    fmus-media
)

# RTP example
add_executable(rtp_example rtp_example.cpp)
target_link_libraries(rtp_example PRIVATE fmus_lib)

# Add subdirectories for protocol-specific examples
add_subdirectory(sip)
add_subdirectory(webrtc)
add_subdirectory(bridge)
add_subdirectory(media)

# Install examples
install(TARGETS
    simple_client
    simple_server
    ivr_example
    video_call
    conference
    RUNTIME DESTINATION bin/examples
)

# Install example source files
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION share/fmus-3g/examples
    FILES_MATCHING PATTERN "*.cpp" PATTERN "*.hpp"
)