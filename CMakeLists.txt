cmake_minimum_required(VERSION 3.20)

# Set Visual Studio runtime library options
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(fmus-3g VERSION 0.1.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug")
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_PDB_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Hardcode all package locations
set(LLVM_DIR "C:/llvm-install/lib/cmake/llvm")
set(LIBUV_DIR "C:/libuv-install/lib/cmake/libuv")
set(ZLIB_INCLUDE_DIR "C:/vcpkg/packages/zlib_x64-windows/include")
set(ZLIB_LIBRARY "C:/vcpkg/packages/zlib_x64-windows/lib/zlib.lib")
set(OPENSSL_INCLUDE_DIR "C:/vcpkg/packages/openssl_x64-windows/include")
set(OPENSSL_LIBRARIES "C:/vcpkg/packages/openssl_x64-windows/lib/libssl.lib;C:/vcpkg/packages/openssl_x64-windows/lib/libcrypto.lib")
set(Boost_INCLUDE_DIRS "C:/boost")
set(Boost_LIBRARIES "C:/boost/stage/lib/libboost_system-vc143-mt-x64-1_80.lib;C:/boost/stage/lib/libboost_filesystem-vc143-mt-x64-1_80.lib")
set(USOCKETS_DIR "C:/usockets-install")
set(UWEBSOCKETS_DIR "C:/usockets-install")
set(NLOHMANN_JSON_INCLUDE_DIR "C:/vcpkg/packages/nlohmann-json_x64-windows/include")

# Set GTest paths
set(GTEST_ROOT "C:/vcpkg/packages/gtest_x64-windows")
set(GTEST_INCLUDE_DIR "${GTEST_ROOT}/include")
set(GTEST_LIBRARY "${GTEST_ROOT}/lib/gtest.lib")
set(GTEST_MAIN_LIBRARY "${GTEST_ROOT}/lib/manual-link/gtest_main.lib")
set(GMOCK_LIBRARY "${GTEST_ROOT}/lib/gmock.lib")
set(GMOCK_MAIN_LIBRARY "${GTEST_ROOT}/lib/manual-link/gmock_main.lib")

# Add GTest DLL paths
set(GTEST_DLL "${GTEST_ROOT}/bin/gtest.dll")
set(GTEST_MAIN_DLL "${GTEST_ROOT}/bin/gtest_main.dll")
set(GMOCK_DLL "${GTEST_ROOT}/bin/gmock.dll")
set(GMOCK_MAIN_DLL "${GTEST_ROOT}/bin/gmock_main.dll")

# Include our custom CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LLVM_DIR}/../../../include
    ${LIBUV_DIR}/../../include
    ${Boost_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${NLOHMANN_JSON_INCLUDE_DIR}
    ${ZLIB_INCLUDE_DIR}
    ${USOCKETS_DIR}/include
    ${UWEBSOCKETS_DIR}/include
)

# Check if required DLLs exist and warn if not
if(NOT EXISTS "C:/libuv-install/bin/uv.dll")
    message(WARNING "LibUV DLL not found at C:/libuv-install/bin/uv.dll")
endif()

if(NOT EXISTS "C:/usockets-install/bin/uSockets.dll")
    message(WARNING "uSockets DLL not found at C:/usockets-install/bin/uSockets.dll")
endif()

# Fix: Add a custom command to create the output directory first
add_custom_command(
    OUTPUT ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_BUILD_TYPE}/create_dir_marker
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_BUILD_TYPE}
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_BUILD_TYPE}/create_dir_marker
    COMMENT "Creating output directory for DLLs"
)

# Add a custom target to copy DLLs
add_custom_target(copy_dlls
    DEPENDS ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_BUILD_TYPE}/create_dir_marker
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/libuv-install/bin/uv.dll"
        "C:/usockets-install/bin/uSockets.dll"
        "C:/vcpkg/packages/zlib_x64-windows/bin/zlib1.dll"
        "${GTEST_DLL}"
        "${GTEST_MAIN_DLL}"
        "${GMOCK_DLL}"
        "${GMOCK_MAIN_DLL}"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_BUILD_TYPE}/"
    COMMENT "Copying DLLs to output directory"
)

# Add subdirectories for libraries
add_subdirectory(src/core)
add_subdirectory(src/net)
add_subdirectory(src/media)
add_subdirectory(src/ivrjs)
add_subdirectory(src/api)

# Enable testing
enable_testing()
add_subdirectory(tests)

# Add examples
add_subdirectory(examples)

# Install rules
install(DIRECTORY include/ DESTINATION include)

# Generate package configuration
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/fmus-3gConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Export targets
install(EXPORT fmus-3gTargets
    FILE fmus-3gTargets.cmake
    NAMESPACE fmus::
    DESTINATION lib/cmake/fmus-3g
)

# Install config files
install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fmus-3gConfig.cmake"
    DESTINATION lib/cmake/fmus-3g
)