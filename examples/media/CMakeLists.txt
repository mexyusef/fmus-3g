# Media examples CMake configuration

# Pipeline example application
add_executable(media_pipeline_example
    pipeline_example.cpp
)

# Set include directories
target_include_directories(media_pipeline_example PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Link dependencies
target_link_libraries(media_pipeline_example PRIVATE
    fmus_core
    fmus_media
    fmus_network
    ${Boost_LIBRARIES}
)

# Set compile definitions
target_compile_definitions(media_pipeline_example PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Set compile options
target_compile_options(media_pipeline_example PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

# Make the example depend on the copy_dlls target
add_dependencies(media_pipeline_example copy_dlls)

# Install rules
install(TARGETS media_pipeline_example
    RUNTIME DESTINATION bin/examples
)