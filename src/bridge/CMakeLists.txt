# Bridge module CMakeLists.txt

# Source files for the Bridge module
set(FMUS_BRIDGE_SOURCES
    webrtc_sip_bridge.cpp
)

# Create the Bridge library
add_library(fmus_bridge STATIC ${FMUS_BRIDGE_SOURCES})

# Set include directories
target_include_directories(fmus_bridge
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

# Link dependencies
target_link_libraries(fmus_bridge
    PUBLIC
        fmus_core
        fmus_media
        fmus_rtp
        fmus_network
        fmus_sip
        fmus_webrtc
)

# Install the library
install(TARGETS fmus_bridge
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)