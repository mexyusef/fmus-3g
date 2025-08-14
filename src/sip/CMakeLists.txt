add_library(fmus-sip
    message.cpp
    sdp.cpp
    transaction.cpp
    dialog.cpp
    registrar.cpp
)

target_include_directories(fmus-sip PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(fmus-sip
    fmus-core
)
