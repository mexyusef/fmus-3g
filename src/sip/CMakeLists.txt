# SIP module CMakeLists.txt

# Source files for the SIP module
set(FMUS_SIP_SOURCES
    uri.cpp
    sdp.cpp
    message.cpp
    transaction.cpp
    registration.cpp
    call.cpp
    agent.cpp
)

# Create the SIP library
add_library(fmus_sip STATIC ${FMUS_SIP_SOURCES})

# Set include directories
target_include_directories(fmus_sip
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

# Link dependencies
target_link_libraries(fmus_sip
    PUBLIC
        fmus_core
        fmus_rtp
)

# Install the library
install(TARGETS fmus_sip
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)