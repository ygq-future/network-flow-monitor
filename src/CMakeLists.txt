add_library(nfm_runtime STATIC app/runtime_mode.cpp)

target_include_directories(
    nfm_runtime
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)

nfm_configure_target(nfm_runtime)
nfm_enable_static_analysis(nfm_runtime)

add_executable(NetworkFlowMonitor WIN32 app/main.cpp)

nfm_configure_target(NetworkFlowMonitor)
nfm_enable_static_analysis(NetworkFlowMonitor)

target_link_libraries(NetworkFlowMonitor PRIVATE nfm_runtime Shell32 User32)
