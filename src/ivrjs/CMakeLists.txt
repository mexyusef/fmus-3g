# IVR JavaScript engine library CMake configuration
set(IVRJS_LIB_NAME fmus-ivrjs)

# Source files
set(IVRJS_SOURCES
    engine/js_engine.cpp
    engine/js_context.cpp
    engine/js_value.cpp
    functions/js_functions.cpp
    functions/js_io.cpp
    functions/js_media.cpp
    functions/js_system.cpp
    functions/js_database.cpp
    functions/js_http.cpp
    functions/js_dtmf.cpp
    functions/js_speech.cpp
)

# Header files
set(IVRJS_HEADERS
    ${CMAKE_SOURCE_DIR}/include/fmus/ivrjs/engine.hpp
    ${CMAKE_SOURCE_DIR}/include/fmus/ivrjs/context.hpp
    ${CMAKE_SOURCE_DIR}/include/fmus/ivrjs/value.hpp
    ${CMAKE_SOURCE_DIR}/include/fmus/ivrjs/functions.hpp
)

# Create library target
add_library(${IVRJS_LIB_NAME} STATIC
    ${IVRJS_SOURCES}
    ${IVRJS_HEADERS}
)

# Set include directories for this library
target_include_directories(${IVRJS_LIB_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Link dependencies
target_link_libraries(${IVRJS_LIB_NAME} PUBLIC
    fmus-core
    fmus-media
    ${Boost_LIBRARIES}
    ${QUICKJS_LIBRARIES}
    ${NLOHMANN_JSON_LIBRARIES}
)

# Set compile definitions
target_compile_definitions(${IVRJS_LIB_NAME} PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Set compile options
target_compile_options(${IVRJS_LIB_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Werror>
)

# Install rules
install(TARGETS ${IVRJS_LIB_NAME}
    EXPORT fmus-3gTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(FILES ${IVRJS_HEADERS}
    DESTINATION include/fmus/ivrjs
)