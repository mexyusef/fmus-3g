add_library(fmus-core
    logger.cpp
)

target_include_directories(fmus-core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(fmus-core
    Threads::Threads
)
