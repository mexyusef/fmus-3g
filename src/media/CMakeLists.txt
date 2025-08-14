add_library(fmus-media
    frame.cpp
    codec.cpp
)

target_include_directories(fmus-media PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(fmus-media
    fmus-core
)
