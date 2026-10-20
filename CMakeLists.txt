cmake_minimum_required(VERSION 3.28)

project(
    NetworkFlowMonitor
    VERSION 0.1.1
    DESCRIPTION "Lightweight Windows 11 per-application network flow monitor"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectOptions)
include(StaticAnalysis)
include(WebView2)
include(CTest)

set(NFM_YYJSON_VERSION "0.12.0")
set(NFM_GITHUB_REPO "ygq-future/network-flow-monitor" CACHE STRING "GitHub owner/repo for release update checks")
configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/dependencies.h.in"
    "${PROJECT_BINARY_DIR}/generated/network_flow_monitor/build/dependencies.h"
    @ONLY
)

add_subdirectory(third_party/yyjson)
add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

include(GNUInstallDirs)

install(
    TARGETS nfm_collector_executable nfm_host_executable
    RUNTIME DESTINATION .
    COMPONENT runtime
)
install(
    DIRECTORY "${PROJECT_SOURCE_DIR}/apps/nfm-ui/dist/"
    DESTINATION ui
    COMPONENT runtime
)
install(
    FILES README.md LICENSE
    DESTINATION .
    COMPONENT documentation
)

set(CPACK_PACKAGE_NAME "NetworkFlowMonitor")
set(CPACK_PACKAGE_VENDOR "Network Flow Monitor")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Lightweight Windows 11 per-application network flow monitor")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "NetworkFlowMonitor-${PROJECT_VERSION}-win-x64")
set(CPACK_GENERATOR "ZIP")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)
include(CPack)
