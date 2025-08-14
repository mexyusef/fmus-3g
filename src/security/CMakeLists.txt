add_library(fmus-security
    encryption.cpp
)

target_link_libraries(fmus-security
    fmus-core
    fmus-network
)

target_include_directories(fmus-security PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
