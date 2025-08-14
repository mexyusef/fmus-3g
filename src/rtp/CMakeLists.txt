add_library(fmus-rtp
    packet.cpp
)

target_include_directories(fmus-rtp PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(fmus-rtp
    fmus-core
)
