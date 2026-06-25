cmake_minimum_required(VERSION 3.16)

project(safe_edge_non_safety_domain LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(safedds REQUIRED)

if(CMAKE_SYSTEM_NAME STREQUAL "QNX")
    set(SAFE_EDGE_PLATFORM_SOCKET_LIB socket)
endif()

set(SAFE_EDGE_NON_SAFETY_DOMAIN_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(SAFE_EDGE_IDL_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../idl")

function(safe_edge_apply_common_build_settings target_name)
    target_include_directories(
        "${target_name}"
        PUBLIC
            "${SAFE_EDGE_NON_SAFETY_DOMAIN_INCLUDE_DIR}"
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
    safe_edge_non_safety_domain_common
    STATIC
        src/common/RuntimeConfig.cpp
        src/common/TopicNames.cpp
        src/common/HeaderFactory.cpp
)
safe_edge_apply_common_build_settings(safe_edge_non_safety_domain_common)
target_include_directories(
    safe_edge_non_safety_domain_common
    PUBLIC
        "${SAFE_EDGE_IDL_INCLUDE_DIR}"
)
target_link_libraries(
    safe_edge_non_safety_domain_common
    PUBLIC
        safedds
)

add_executable(
    safe_edge_cloud_gateway
    src/apps/cloud_gateway_main.cpp
    src/nodes/CloudGatewayNode.cpp
)
safe_edge_apply_common_build_settings(safe_edge_cloud_gateway)
target_include_directories(
    safe_edge_cloud_gateway
    PRIVATE
        "${SAFE_EDGE_IDL_INCLUDE_DIR}"
)
target_link_libraries(
    safe_edge_cloud_gateway
    PRIVATE
        safe_edge_non_safety_domain_common
        safedds
        ${SAFE_EDGE_PLATFORM_SOCKET_LIB}
)

add_executable(
    safe_edge_ota_service
    src/apps/ota_service_main.cpp
    src/nodes/OtaServiceNode.cpp
)
safe_edge_apply_common_build_settings(safe_edge_ota_service)
target_include_directories(
    safe_edge_ota_service
    PRIVATE
        "${SAFE_EDGE_IDL_INCLUDE_DIR}"
)
target_link_libraries(
    safe_edge_ota_service
    PRIVATE
        safe_edge_non_safety_domain_common
        safedds
        ${SAFE_EDGE_PLATFORM_SOCKET_LIB}
)

add_executable(
    safe_edge_infotainment
    src/apps/infotainment_main.cpp
    src/nodes/InfotainmentNode.cpp
)
safe_edge_apply_common_build_settings(safe_edge_infotainment)
target_include_directories(
    safe_edge_infotainment
    PRIVATE
        "${SAFE_EDGE_IDL_INCLUDE_DIR}"
)
target_link_libraries(
    safe_edge_infotainment
    PRIVATE
        safe_edge_non_safety_domain_common
        safedds
        ${SAFE_EDGE_PLATFORM_SOCKET_LIB}
)

install(
    TARGETS
        safe_edge_cloud_gateway
        safe_edge_ota_service
        safe_edge_infotainment
    RUNTIME DESTINATION bin
)
