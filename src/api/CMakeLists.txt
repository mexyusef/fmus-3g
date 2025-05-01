# API library CMake configuration
set(API_LIB_NAME fmus-api)

# Source files
set(API_SOURCES
    client/client.cpp
    client/session.cpp
    client/call.cpp
    server/server.cpp
    server/connection.cpp
    server/handler.cpp
    rest/rest_server.cpp
    rest/rest_client.cpp
    rest/rest_handler.cpp
    websocket/ws_server.cpp
    websocket/ws_client.cpp
    websocket/ws_handler.cpp
)

# Header files
set(API_HEADERS
    ${CMAKE_SOURCE_DIR}/include/fmus/api/client.hpp
    ${CMAKE_SOURCE_DIR}/include/fmus/api/server.hpp
    ${CMAKE_SOURCE_DIR}/include/fmus/api/rest.hpp
    ${CMAKE_SOURCE_DIR}/include/fmus/api/websocket.hpp
)

# Create library target
add_library(${API_LIB_NAME} STATIC
    ${API_SOURCES}
    ${API_HEADERS}
)

# Set include directories for this library
target_include_directories(${API_LIB_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Link dependencies
target_link_libraries(${API_LIB_NAME} PUBLIC
    fmus-core
    fmus-net
    fmus-media
    fmus-ivrjs
    ${Boost_LIBRARIES}
    ${LIBUV_LIBRARIES}
    ${UWEBSOCKETS_LIBRARIES}
    ${NLOHMANN_JSON_LIBRARIES}
)

# Set compile definitions
target_compile_definitions(${API_LIB_NAME} PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Set compile options
target_compile_options(${API_LIB_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Werror>
)

# Install rules
install(TARGETS ${API_LIB_NAME}
    EXPORT fmus-3gTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(FILES ${API_HEADERS}
    DESTINATION include/fmus/api
)