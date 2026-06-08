cmake_minimum_required(VERSION 3.16)

project(safe_edge_safety_domain LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(safedds REQUIRED)

if(CMAKE_SYSTEM_NAME STREQUAL "QNX")
    set(SAFE_EDGE_PLATFORM_SOCKET_LIB socket)
else()
    set(SAFE_EDGE_PLATFORM_SOCKET_LIB "")
endif()

set(SAFE_EDGE_SAFETY_DOMAIN_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(SAFE_EDGE_IDL_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../idl")

function(safe_edge_apply_common_build_settings target_name)
    target_include_directories(
        "${target_name}"
        PUBLIC
            "${SAFE_EDGE_SAFETY_DOMAIN_INCLUDE_DIR}"
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
    safe_edge_safety_domain_common
    STATIC
        src/common/RuntimeConfig.cpp
        src/common/TopicNames.cpp
        src/common/HeaderFactory.cpp
        src/policy/IdlAdapters.cpp
)
safe_edge_apply_common_build_settings(safe_edge_safety_domain_common)
target_include_directories(
    safe_edge_safety_domain_common
    PUBLIC
        "${SAFE_EDGE_IDL_INCLUDE_DIR}"
)
target_link_libraries(
    safe_edge_safety_domain_common
    PUBLIC
        safedds
)

add_library(
    safe_edge_policy_core
    STATIC
        src/policy/PolicyEngine.cpp
)
safe_edge_apply_common_build_settings(safe_edge_policy_core)

add_executable(
    safe_edge_safety_io_adapters
    src/apps/safety_io_adapters_main.cpp
    src/nodes/SafetyIoAdaptersNode.cpp
)
safe_edge_apply_common_build_settings(safe_edge_safety_io_adapters)
target_include_directories(
    safe_edge_safety_io_adapters
    PRIVATE
        "${SAFE_EDGE_IDL_INCLUDE_DIR}"
)
target_link_libraries(
    safe_edge_safety_io_adapters
    PRIVATE
        safe_edge_safety_domain_common
        safe_edge_policy_core
        safedds
        ${SAFE_EDGE_PLATFORM_SOCKET_LIB}
)

add_executable(
    safe_edge_policy_engine
    src/apps/policy_engine_main.cpp
    src/nodes/PolicyEngineNode.cpp
)
safe_edge_apply_common_build_settings(safe_edge_policy_engine)
target_include_directories(
    safe_edge_policy_engine
    PRIVATE
        "${SAFE_EDGE_IDL_INCLUDE_DIR}"
)
target_link_libraries(
    safe_edge_policy_engine
    PRIVATE
        safe_edge_safety_domain_common
        safe_edge_policy_core
        safedds
        ${SAFE_EDGE_PLATFORM_SOCKET_LIB}
)

add_executable(
    safe_edge_vehicle_mock
    src/apps/vehicle_mock_main.cpp
    src/nodes/VehicleMockNode.cpp
)
safe_edge_apply_common_build_settings(safe_edge_vehicle_mock)
target_include_directories(
    safe_edge_vehicle_mock
    PRIVATE
        "${SAFE_EDGE_IDL_INCLUDE_DIR}"
)
target_link_libraries(
    safe_edge_vehicle_mock
    PRIVATE
        safe_edge_safety_domain_common
        safedds
        ${SAFE_EDGE_PLATFORM_SOCKET_LIB}
)

install(
    TARGETS
        safe_edge_safety_io_adapters
        safe_edge_policy_engine
        safe_edge_vehicle_mock
    RUNTIME DESTINATION bin
)
