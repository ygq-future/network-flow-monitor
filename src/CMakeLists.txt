add_library(
    nfm_config
    STATIC
        config/app_config.cpp
        config/config_codec.cpp
        config/config_paths.cpp
        config/config_store.cpp
        config/config_validation.cpp
)
target_include_directories(
    nfm_config
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
        "${PROJECT_BINARY_DIR}/generated"
)
target_link_libraries(nfm_config PRIVATE nfm_yyjson)
nfm_configure_target(nfm_config)
nfm_enable_shared_pch(nfm_config)
nfm_enable_static_analysis(nfm_config)

add_library(
    nfm_process
    STATIC
        process/identity_resolver.cpp
        process/process_tracker.cpp
        process/windows_process_probe.cpp
)
target_include_directories(nfm_process PUBLIC "${PROJECT_SOURCE_DIR}/include")
nfm_configure_target(nfm_process)
nfm_enable_shared_pch(nfm_process)
nfm_enable_static_analysis(nfm_process ENABLE_EXCEPTIONS)

add_library(
    nfm_ranking
    STATIC
        ranking/application_accumulator.cpp
        ranking/ranking_engine.cpp
)
target_include_directories(nfm_ranking PUBLIC "${PROJECT_SOURCE_DIR}/include")
nfm_configure_target(nfm_ranking)
nfm_enable_shared_pch(nfm_ranking)
nfm_enable_static_analysis(nfm_ranking)

add_library(
    nfm_collector
    STATIC
        collector/collector_core.cpp
        collector/collector_host.cpp
        collector/etw_authorization.cpp
        collector/etw_snapshot_provider.cpp
        collector/network_event_decoder.cpp
        collector/windows_etw_record_decoder.cpp
        collector/windows_etw_session.cpp
        collector/windows_process_probe_adapter.cpp
)
target_include_directories(nfm_collector PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(nfm_collector PUBLIC nfm_process nfm_ranking PRIVATE Advapi32 Tdh Netapi32)
nfm_configure_target(nfm_collector)
nfm_enable_shared_pch(nfm_collector)
nfm_enable_static_analysis(nfm_collector ENABLE_EXCEPTIONS)

add_library(
    nfm_ipc
    STATIC
        ipc/frame_codec.cpp
        ipc/ipc_client.cpp
        ipc/ipc_server.cpp
        ipc/named_pipe_transport.cpp
        ipc/reconnect_policy.cpp
        ipc/security_policy.cpp
        ipc/snapshot_publisher.cpp
        ipc/windows_pipe_security.cpp
)
target_include_directories(nfm_ipc PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(nfm_ipc PUBLIC nfm_ranking PRIVATE nfm_config nfm_yyjson Advapi32)
nfm_configure_target(nfm_ipc)
nfm_enable_shared_pch(nfm_ipc)
nfm_enable_static_analysis(nfm_ipc ENABLE_EXCEPTIONS)

add_library(nfm_runtime STATIC app/runtime_mode.cpp app/collector_runtime.cpp)
target_include_directories(nfm_runtime PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(nfm_runtime PUBLIC nfm_collector nfm_ipc)
nfm_configure_target(nfm_runtime)
nfm_enable_shared_pch(nfm_runtime)
nfm_enable_static_analysis(nfm_runtime ENABLE_EXCEPTIONS)

add_executable(
    nfm_collector_executable
        app/collector_main.cpp
        "${PROJECT_SOURCE_DIR}/resources/network_flow_monitor.rc"
)
set_target_properties(nfm_collector_executable PROPERTIES OUTPUT_NAME "nfm-collector")
nfm_configure_target(nfm_collector_executable)
nfm_enable_shared_pch(nfm_collector_executable)
nfm_enable_static_analysis(nfm_collector_executable ENABLE_EXCEPTIONS)
target_link_libraries(nfm_collector_executable PRIVATE nfm_runtime Shell32)

add_library(
    nfm_host
    STATIC
        host/bridge/bridge_types.cpp
        host/bridge/bridge_dispatcher.cpp
        host/bridge/native_bridge.cpp
        host/coordinator/dock_capsule_coordinator.cpp
        host/history/traffic_history_store.cpp
        host/lifecycle/collector_companion.cpp
        host/platform/app_updater.cpp
        host/platform/rotating_file_logger.cpp
        host/platform/startup_registration.cpp
        host/platform/webview2_environment.cpp
        host/platform/win32_window.cpp
        host/webview/webview_controller.cpp
        host/app/product_host.cpp
)
target_include_directories(
    nfm_host
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
        "${PROJECT_BINARY_DIR}/generated"
)
target_link_libraries(
    nfm_host
    PUBLIC
        nfm_ipc
        nfm_config
        nfm_webview2_sdk
    PRIVATE
        nfm_yyjson
        User32
        Gdi32
        Shell32
        Ole32
        Advapi32
        Comctl32
        Dwmapi
        Winhttp
)
nfm_configure_target(nfm_host)
nfm_enable_shared_pch(nfm_host)
nfm_enable_static_analysis(nfm_host ENABLE_EXCEPTIONS)

add_executable(
    nfm_host_executable
    WIN32
        host/app/main.cpp
        "${PROJECT_SOURCE_DIR}/resources/network_flow_monitor.rc"
)
set_target_properties(nfm_host_executable PROPERTIES OUTPUT_NAME "NetworkFlowMonitor")
nfm_configure_target(nfm_host_executable)
nfm_enable_shared_pch(nfm_host_executable)
nfm_enable_static_analysis(nfm_host_executable ENABLE_EXCEPTIONS)
target_link_libraries(nfm_host_executable PRIVATE nfm_host Shell32 Ole32 Comctl32 Dwmapi)

add_custom_command(
    TARGET nfm_host_executable POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_SOURCE_DIR}/apps/nfm-ui/dist"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:nfm_host_executable>/ui"
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${PROJECT_SOURCE_DIR}/apps/nfm-ui/dist"
        "$<TARGET_FILE_DIR:nfm_host_executable>/ui"
    COMMENT "Copying frontend assets to build ui/ directory"
)

