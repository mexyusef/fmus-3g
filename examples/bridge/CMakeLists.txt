# Bridge example application

# Source files for the Bridge example
set(FMUS_BRIDGE_EXAMPLE_SRC
    bridge_example.cpp
)

# Create the executable
add_executable(fmus_bridge_example ${FMUS_BRIDGE_EXAMPLE_SRC})

# Set include directories
target_include_directories(fmus_bridge_example PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Link dependencies
target_link_libraries(fmus_bridge_example
    fmus_core
    fmus_media
    fmus_rtp
    fmus_network
    fmus_sip
    fmus_webrtc
    fmus_bridge
)

# Add dependency on copy_dlls target
add_dependencies(fmus_bridge_example copy_dlls)

# Installation instructions
install(TARGETS fmus_bridge_example
    RUNTIME DESTINATION bin
)