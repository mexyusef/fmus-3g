cmake_minimum_required(VERSION 3.16)
project(fmus-3g VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Find required packages
find_package(Threads REQUIRED)

# Core library
add_subdirectory(src/core)

# SIP library
add_subdirectory(src/sip)

# RTP library  
add_subdirectory(src/rtp)

# WebRTC library
add_subdirectory(src/webrtc)

# Media library
add_subdirectory(src/media)

# Network library
add_subdirectory(src/network)

# Enterprise library
add_subdirectory(src/enterprise)

# Management library
add_subdirectory(src/management)

# Security library
add_subdirectory(src/security)

# Main executable
add_executable(fmus-3g src/main.cpp)
target_link_libraries(fmus-3g
    fmus-core
    fmus-sip
    fmus-rtp
    fmus-webrtc
    fmus-media
    fmus-network
    fmus-enterprise
    fmus-management
    fmus-security
    Threads::Threads
)

# Tests (optional)
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
