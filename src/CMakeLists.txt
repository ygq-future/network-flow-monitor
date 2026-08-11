add_library(nfm_runtime STATIC app/runtime_mode.cpp)

target_include_directories(
    nfm_runtime
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)

nfm_configure_target(nfm_runtime)
nfm_enable_static_analysis(nfm_runtime)

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
)

target_link_libraries(nfm_config PRIVATE nfm_yyjson)

nfm_configure_target(nfm_config)
nfm_enable_static_analysis(nfm_config)

add_library(
    nfm_process
    STATIC
        process/identity_resolver.cpp
        process/process_tracker.cpp
        process/windows_process_probe.cpp
)

target_include_directories(
    nfm_process
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)

nfm_configure_target(nfm_process)
nfm_enable_static_analysis(nfm_process ENABLE_EXCEPTIONS)

add_library(
    nfm_ranking
    STATIC
        ranking/application_accumulator.cpp
        ranking/ranking_engine.cpp
)

target_include_directories(
    nfm_ranking
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)

nfm_configure_target(nfm_ranking)
nfm_enable_static_analysis(nfm_ranking)

add_library(
    nfm_collector
    STATIC
        collector/collector_core.cpp
        collector/network_event_decoder.cpp
        collector/windows_etw_record_decoder.cpp
        collector/windows_etw_session.cpp
        collector/windows_process_probe_adapter.cpp
)

target_include_directories(
    nfm_collector
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(nfm_collector PUBLIC nfm_process nfm_ranking PRIVATE Advapi32 Tdh)

nfm_configure_target(nfm_collector)
nfm_enable_static_analysis(nfm_collector ENABLE_EXCEPTIONS)

add_library(
    nfm_ipc
    STATIC
        ipc/application_catalog.cpp
        ipc/frame_codec.cpp
        ipc/ipc_server.cpp
        ipc/named_pipe_transport.cpp
        ipc/security_policy.cpp
        ipc/snapshot_codec.cpp
        ipc/snapshot_publisher.cpp
        ipc/windows_pipe_security.cpp
)

target_include_directories(
    nfm_ipc
    PUBLIC
        "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(nfm_ipc PRIVATE nfm_config Advapi32)

nfm_configure_target(nfm_ipc)
nfm_enable_static_analysis(nfm_ipc ENABLE_EXCEPTIONS)

add_executable(NetworkFlowMonitor WIN32 app/main.cpp)

nfm_configure_target(NetworkFlowMonitor)
nfm_enable_static_analysis(NetworkFlowMonitor)

target_link_libraries(NetworkFlowMonitor PRIVATE nfm_runtime Shell32 User32)
