# Core library CMake configuration
set(CORE_LIB_NAME fmus-core)

# Source files
set(CORE_SOURCES
    config.cpp
    error.cpp
    logger.cpp
    utils.cpp
    buffer.cpp
    memory_pool.cpp
    thread_pool.cpp
)

# Header files
set(CORE_HEADERS
    ${CMAKE_SOURCE_DIR}/include/fmus/core/config.hpp
    ${CMAKE_SOURCE_DIR}/include/fmus/core/error.hpp
    ${CMAKE_SOURCE_DIR}/include/fmus/core/logger.hpp
    ${CMAKE_SOURCE_DIR}/include/fmus/core/utils.hpp
    ${CMAKE_SOURCE_DIR}/include/fmus/core/buffer.hpp
    ${CMAKE_SOURCE_DIR}/include/fmus/core/memory_pool.hpp
    ${CMAKE_SOURCE_DIR}/include/fmus/core/thread_pool.hpp
)

# Create library target
add_library(${CORE_LIB_NAME} STATIC
    ${CORE_SOURCES}
    ${CORE_HEADERS}
)

# Set include directories for this library
target_include_directories(${CORE_LIB_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Link dependencies
target_link_libraries(${CORE_LIB_NAME} PUBLIC
    ${Boost_LIBRARIES}
    ${LIBUV_LIBRARIES}
)

# Set compile definitions
target_compile_definitions(${CORE_LIB_NAME} PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Set compile options
target_compile_options(${CORE_LIB_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Werror>
)

# Install rules
install(TARGETS ${CORE_LIB_NAME}
    EXPORT fmus-3gTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(FILES ${CORE_HEADERS}
    DESTINATION include/fmus/core
)