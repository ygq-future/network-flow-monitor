set(NFM_TEST_SUITES
    "bootstrap_tests.cpp|bootstrap.toolchain|core"
    "runtime_mode_tests.cpp|app.runtime_mode|core"
    "runtime_collector_integration_tests.cpp|app.runtime_collector_integration|collector"
    "config_model_tests.cpp|config.model|core"
    "config_codec_tests.cpp|config.codec|core"
    "config_store_tests.cpp|config.store|core"
    "application_accumulator_tests.cpp|ranking.accumulator|collector"
    "ranking_engine_tests.cpp|ranking.engine|collector"
    "identity_resolver_tests.cpp|identity.resolver|collector"
    "process_tracker_tests.cpp|process.tracker|collector"
    "windows_process_probe_tests.cpp|process.windows_probe|collector"
    "network_event_decoder_tests.cpp|collector.network_decoder|collector"
    "collector_core_tests.cpp|collector.core|collector"
    "collector_host_tests.cpp|collector.host|collector"
    "ipc_frame_codec_tests.cpp|ipc.frame_codec|protocol"
    "ipc_snapshot_codec_tests.cpp|ipc.snapshot_codec|protocol"
    "ipc_application_catalog_tests.cpp|ipc.application_catalog|protocol"
    "ipc_security_policy_tests.cpp|ipc.security_policy|ipc"
    "ipc_named_pipe_transport_tests.cpp|ipc.named_pipe_transport|ipc"
    "ipc_server_tests.cpp|ipc.server|ipc"
    "ipc_server_integration_tests.cpp|ipc.server_integration|ipc"
    "ipc_snapshot_publisher_tests.cpp|ipc.snapshot_publisher|ipc"
    "ipc_client_tests.cpp|ipc.client|ipc"
    "ipc_integration_tests.cpp|ipc.integration|ipc,requires-admin"
    "windows_etw_record_decoder_tests.cpp|collector.windows_record_decoder|collector"
    "windows_etw_session_tests.cpp|collector.windows_session|collector"
    "etw_collector_integration_tests.cpp|collector.etw_integration|collector,requires-admin"
    "host_bridge_tests.cpp|host.bridge|host"
    "host_webview_policy_tests.cpp|host.webview_policy|host"
    "host_lifecycle_tests.cpp|host.lifecycle|host"
    "dock_capsule_coordinator_tests.cpp|host.coordinator|host"
)

set(NFM_TEST_SOURCES test_headless_runtime.cpp)

foreach(entry IN LISTS NFM_TEST_SUITES)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 source_file)
    get_filename_component(stem "${source_file}" NAME_WE)
    set_source_files_properties(
        "${source_file}"
        PROPERTIES COMPILE_DEFINITIONS "main=nfm_suite_${stem}"
    )
    list(APPEND NFM_TEST_SOURCES "${source_file}")
endforeach()

add_executable(nfm_tests ${NFM_TEST_SOURCES})
target_include_directories(nfm_tests PRIVATE "${PROJECT_BINARY_DIR}/generated")
nfm_configure_target(nfm_tests)
nfm_enable_shared_pch(nfm_tests)
nfm_enable_static_analysis(nfm_tests)
target_link_libraries(
    nfm_tests
    PRIVATE
        nfm_runtime
        nfm_host
        nfm_collector
        nfm_ipc
        nfm_process
        nfm_ranking
        nfm_config
        Advapi32
        Tdh
        Ws2_32
)

foreach(entry IN LISTS NFM_TEST_SUITES)
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 1 test_name)
    list(GET parts 2 raw_labels)
    string(REPLACE "," ";" labels "${raw_labels}")
    add_test(NAME ${test_name} COMMAND nfm_tests ${test_name})
    set_tests_properties(${test_name} PROPERTIES LABELS "${labels}")
endforeach()

set_tests_properties(
    collector.host
    ipc.named_pipe_transport
    ipc.server_integration
    ipc.snapshot_publisher
    ipc.client
    ipc.integration
    host.lifecycle
    PROPERTIES RESOURCE_LOCK nfm_named_pipe
)
set_tests_properties(ipc.integration PROPERTIES RUN_SERIAL TRUE)
set_tests_properties(collector.etw_integration PROPERTIES RUN_SERIAL TRUE)
