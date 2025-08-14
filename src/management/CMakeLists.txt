add_library(fmus-management
    api.cpp
)

target_link_libraries(fmus-management
    fmus-core
    fmus-network
    fmus-sip
    fmus-enterprise
    fmus-webrtc
)

target_include_directories(fmus-management PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
