cmake_minimum_required(VERSION 3.28)

project(
    NetworkFlowMonitor
    VERSION 0.1.0
    DESCRIPTION "Lightweight Windows 11 per-application network flow monitor"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectOptions)
include(StaticAnalysis)
include(CTest)

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
