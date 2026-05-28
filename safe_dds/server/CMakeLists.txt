cmake_minimum_required(VERSION 3.16)

project(safe_edge_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(safedds REQUIRED)
find_package(CURL REQUIRED)

if(CMAKE_SYSTEM_NAME STREQUAL "QNX")
    set(SAFE_EDGE_PLATFORM_SOCKET_LIB socket)
endif()

set(SAFE_EDGE_SERVER_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(SAFE_EDGE_IDL_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../idl")
set(SAFE_EDGE_SERVER_COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../common_server")

function(safe_edge_apply_common_build_settings target_name)
    target_include_directories(
        "${target_name}"
        PUBLIC
            "${SAFE_EDGE_SERVER_INCLUDE_DIR}"
    )

    target_compile_options(
        "${target_name}"
        PRIVATE
            -fno-exceptions
            -fno-rtti
            -Wall
            -Werror
            -Wextra
            -Wpedantic
    )
endfunction()

add_library(
    safe_edge_server_common
    STATIC
        ${SAFE_EDGE_SERVER_COMMON_DIR}/src/PilotServerClient.cpp
        ${SAFE_EDGE_SERVER_COMMON_DIR}/src/PilotServerPayloadParser.cpp
        src/common/PilotServerPublishHelper.cpp
        src/common/RuntimeConfig.cpp
        src/common/TopicNames.cpp
)
safe_edge_apply_common_build_settings(safe_edge_server_common)
target_include_directories(
    safe_edge_server_common
    PUBLIC
        "${SAFE_EDGE_IDL_INCLUDE_DIR}"
        "${SAFE_EDGE_SERVER_COMMON_DIR}/include"
)
target_link_libraries(
    safe_edge_server_common
    PUBLIC
        safedds
        CURL::libcurl
)

add_executable(
    safe_edge_server
    src/apps/server_main.cpp
    src/nodes/ServerNode.cpp
)
safe_edge_apply_common_build_settings(safe_edge_server)
target_include_directories(
    safe_edge_server
    PRIVATE
        "${SAFE_EDGE_IDL_INCLUDE_DIR}"
)
target_link_libraries(
    safe_edge_server
    PRIVATE
        safe_edge_server_common
        safedds
        ${SAFE_EDGE_PLATFORM_SOCKET_LIB}
)

install(
    TARGETS
        safe_edge_server
    RUNTIME DESTINATION bin
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
option(SAFE_EDGE_BUILD_TESTS "Build integration tests" ON)

if(SAFE_EDGE_BUILD_TESTS)
    # Try system GTest first; fall back to fetching from source.
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
        # Do not install GTest alongside the project
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)

        # Alias to the same target name used when found via find_package
        if(NOT TARGET GTest::gtest)
            add_library(GTest::gtest ALIAS gtest)
        endif()
    endif()

    add_executable(
        test_server_integration
        test/test_server_integration.cpp
    )

    # Tests need exceptions and RTTI (GTest requirement).
    # Do not reuse safe_edge_apply_common_build_settings which disables them.
    target_include_directories(
        test_server_integration
        PRIVATE
            "${SAFE_EDGE_SERVER_INCLUDE_DIR}"
            "${SAFE_EDGE_IDL_INCLUDE_DIR}"
    )
    target_compile_options(
        test_server_integration
        PRIVATE
            -Wall -Wextra -Wpedantic
    )
    target_link_libraries(
        test_server_integration
        PRIVATE
            safe_edge_server_common
            safedds
            ${SAFE_EDGE_PLATFORM_SOCKET_LIB}
            GTest::gtest
    )

    enable_testing()
    add_test(NAME server_integration COMMAND test_server_integration)

    install(
        TARGETS test_server_integration
        RUNTIME DESTINATION bin
    )
endif()
