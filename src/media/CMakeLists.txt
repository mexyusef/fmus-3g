# Media library CMake configuration
set(MEDIA_LIB_NAME fmus_media)

# Source files
set(MEDIA_SOURCES
    media.cpp
    codec.cpp
    device.cpp

    # Pipeline implementation
    pipeline/media_pipeline.cpp
    pipeline/pipeline_node.cpp
    pipeline/media_source.cpp

    # Codec implementations
    codec/codec_factory.cpp
    codec/h263_codec.cpp
    codec/mp4v_codec.cpp
    codec/pcmu_codec.cpp
    codec/amr_codec.cpp
    codec/ilbc_codec.cpp

    # Pipeline components
    pipeline/media_sink.cpp
    pipeline/media_filter.cpp

    # Capture implementations
    capture/audio_capture.cpp
    capture/video_capture.cpp

    # Render implementations
    render/audio_render.cpp
    render/video_render.cpp

    # Format handlers
    format/media_format.cpp
)

# Header files
set(MEDIA_HEADERS
    ${CMAKE_SOURCE_DIR}/include/fmus/media/media.hpp
    ${CMAKE_SOURCE_DIR}/include/fmus/media/pipeline.hpp
)

# Create library target
add_library(${MEDIA_LIB_NAME} STATIC
    ${MEDIA_SOURCES}
    ${MEDIA_HEADERS}
)

# Set include directories for this library
target_include_directories(${MEDIA_LIB_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Link dependencies
target_link_libraries(${MEDIA_LIB_NAME} PUBLIC
    fmus_core
    fmus_network
    ${Boost_LIBRARIES}
)

# Set compile definitions
target_compile_definitions(${MEDIA_LIB_NAME} PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Set compile options
target_compile_options(${MEDIA_LIB_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Werror>
)

# Install rules
install(TARGETS ${MEDIA_LIB_NAME}
    EXPORT fmus-3gTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(FILES ${MEDIA_HEADERS}
    DESTINATION include/fmus/media
)