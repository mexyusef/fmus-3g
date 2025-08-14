add_library(fmus-network
    socket.cpp
    transport.cpp
    stun.cpp
)

target_include_directories(fmus-network PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(fmus-network
    fmus-core
    fmus-sip
    fmus-rtp
)
