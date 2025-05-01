# Networking library CMake configuration
set(NET_LIB_NAME fmus-net)

# Source files
set(NET_SOURCES
    sip/sip_client.cpp
    sip/sip_dialog.cpp
    sip/sip_factory.cpp
    sip/sip_message.cpp
    sip/sip_stack.cpp
    sip/sip_transaction.cpp
    sip/sip_udp_transport.cpp
    sip/transaction_factory.cpp
    rtp/rtp_session.cpp
    rtp/rtp_packet.cpp
    rtp/rtcp_packet.cpp
    rtp/rtp_stream.cpp
)

# Header files
set(NET_HEADERS
    ${CMAKE_SOURCE_DIR}/include/fmus/net/sip.hpp
    ${CMAKE_SOURCE_DIR}/include/fmus/net/sip_client.hpp
    ${CMAKE_SOURCE_DIR}/include/fmus/net/rtp_session.hpp
    ${CMAKE_SOURCE_DIR}/include/fmus/net/rtp_packet.hpp
    ${CMAKE_SOURCE_DIR}/include/fmus/net/rtcp_packet.hpp
    ${CMAKE_SOURCE_DIR}/include/fmus/net/rtp_stream.hpp
)

# Create library target
add_library(${NET_LIB_NAME} STATIC
    ${NET_SOURCES}
    ${NET_HEADERS}
)

# Set include directories for this library
target_include_directories(${NET_LIB_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Link dependencies
target_link_libraries(${NET_LIB_NAME} PUBLIC
    fmus-core
    ${Boost_LIBRARIES}
    ${LIBUV_LIBRARIES}
    ${OPENSSL_LIBRARIES}
)

# Set compile definitions
target_compile_definitions(${NET_LIB_NAME} PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Set compile options
target_compile_options(${NET_LIB_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Werror>
)

# Install rules
install(TARGETS ${NET_LIB_NAME}
    EXPORT fmus-3gTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(FILES ${NET_HEADERS}
    DESTINATION include/fmus/net
)