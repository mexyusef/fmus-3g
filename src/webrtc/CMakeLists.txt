# WebRTC module CMakeLists.txt

# Source files for the WebRTC module
set(FMUS_WEBRTC_SOURCES
    webrtc.cpp
    peer_connection.cpp
    signaling.cpp
    session.cpp
    data_channel.cpp
    media_track.cpp
    ice_candidate.cpp
)

# Create the WebRTC library
add_library(fmus_webrtc STATIC ${FMUS_WEBRTC_SOURCES})

# Set include directories
target_include_directories(fmus_webrtc
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${NLOHMANN_JSON_INCLUDE_DIR}
)

# Link dependencies
target_link_libraries(fmus_webrtc
    PUBLIC
        fmus_core
        fmus_media
        fmus_rtp
        fmus_network
)

# Install the library
install(TARGETS fmus_webrtc
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)