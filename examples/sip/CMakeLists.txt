# SIP example CMakeLists.txt

# Source files for the SIP example
set(FMUS_SIP_EXAMPLE_SOURCES
    sip_example.cpp
)

# Create the example executable
add_executable(fmus_sip_example ${FMUS_SIP_EXAMPLE_SOURCES})

# Set include directories
target_include_directories(fmus_sip_example
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

# Link dependencies
target_link_libraries(fmus_sip_example
    PRIVATE
        fmus_core
        fmus_rtp
        fmus_sip
)

# Make the example depend on the copy_dlls target
add_dependencies(fmus_sip_example copy_dlls)

# Install the example
install(TARGETS fmus_sip_example
    RUNTIME DESTINATION bin/examples
)