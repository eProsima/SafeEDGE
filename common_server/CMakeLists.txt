cmake_minimum_required(VERSION 3.16)

project(safe_edge_server_common_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CURL REQUIRED)

if(CMAKE_SYSTEM_NAME STREQUAL "QNX")
    set(SAFE_EDGE_PLATFORM_SOCKET_LIB socket)
endif()

set(SAFE_EDGE_SERVER_COMMON_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

# ---------------------------------------------------------------------------
# GTest
# ---------------------------------------------------------------------------
find_package(GTest QUIET)

if(NOT GTest_FOUND)
    message(STATUS "GTest not found — fetching from source")
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
        URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    if(NOT TARGET GTest::gtest_main)
        add_library(GTest::gtest_main ALIAS gtest_main)
    endif()
endif()

# ---------------------------------------------------------------------------
# Test binary
# ---------------------------------------------------------------------------
add_executable(
    test_server_common
    src/PilotServerClient.cpp
    src/PilotServerPayloadParser.cpp
    test/test_server_common.cpp
)

target_include_directories(
    test_server_common
    PRIVATE
        "${SAFE_EDGE_SERVER_COMMON_INCLUDE_DIR}"
)

target_compile_options(
    test_server_common
    PRIVATE
        -Wall -Wextra -Wpedantic
)

target_link_libraries(
    test_server_common
    PRIVATE
        CURL::libcurl
        GTest::gtest_main
        ${SAFE_EDGE_PLATFORM_SOCKET_LIB}
)

enable_testing()
add_test(NAME server_common COMMAND test_server_common)

install(
    TARGETS test_server_common
    RUNTIME DESTINATION bin
)
