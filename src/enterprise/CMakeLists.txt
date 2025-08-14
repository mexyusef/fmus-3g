add_library(fmus-enterprise
    features.cpp
)

target_link_libraries(fmus-enterprise
    fmus-core
    fmus-sip
    fmus-network
)

target_include_directories(fmus-enterprise PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
