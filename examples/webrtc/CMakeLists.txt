# WebRTC example application

# Source files for the WebRTC example
set(FMUS_WEBRTC_EXAMPLE_SRC
    webrtc_example.cpp
)

# Create the executable
add_executable(fmus_webrtc_example ${FMUS_WEBRTC_EXAMPLE_SRC})

# Set include directories
target_include_directories(fmus_webrtc_example PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Link dependencies
target_link_libraries(fmus_webrtc_example
    fmus_core
    fmus_media
    fmus_rtp
    fmus_network
    fmus_webrtc
)

# Add dependency on copy_dlls target
add_dependencies(fmus_webrtc_example copy_dlls)

# Installation instructions
install(TARGETS fmus_webrtc_example
    RUNTIME DESTINATION bin
)