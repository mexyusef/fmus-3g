add_library(fmus-webrtc
    session.cpp
    signaling.cpp
)

target_include_directories(fmus-webrtc PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(fmus-webrtc
    fmus-core
)
