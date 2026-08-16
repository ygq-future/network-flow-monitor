add_executable(nfm_bootstrap_tests bootstrap_tests.cpp)

target_include_directories(
    nfm_bootstrap_tests
    PRIVATE
        "${PROJECT_BINARY_DIR}/generated"
)

nfm_configure_target(nfm_bootstrap_tests)
nfm_enable_static_analysis(nfm_bootstrap_tests)

add_test(NAME bootstrap.toolchain COMMAND nfm_bootstrap_tests)
set_tests_properties(bootstrap.toolchain PROPERTIES LABELS "core")

add_executable(nfm_runtime_mode_tests runtime_mode_tests.cpp)

nfm_configure_target(nfm_runtime_mode_tests)
nfm_enable_static_analysis(nfm_runtime_mode_tests)

target_link_libraries(nfm_runtime_mode_tests PRIVATE nfm_runtime)

add_test(NAME app.runtime_mode COMMAND nfm_runtime_mode_tests)
set_tests_properties(app.runtime_mode PROPERTIES LABELS "core")

add_executable(nfm_runtime_collector_integration_tests runtime_collector_integration_tests.cpp)

nfm_configure_target(nfm_runtime_collector_integration_tests)
nfm_enable_static_analysis(nfm_runtime_collector_integration_tests)

target_link_libraries(nfm_runtime_collector_integration_tests PRIVATE nfm_runtime nfm_collector nfm_ipc)

add_test(NAME app.runtime_collector_integration COMMAND nfm_runtime_collector_integration_tests)
set_tests_properties(app.runtime_collector_integration PROPERTIES LABELS "collector")

# -----------------------------------------------------------------------------
# Legacy C++ UI Tests (Non-interactive only; labeled 'legacy-ui')
# Note: Interactive UI tests (window_logic, ui_controller, integration_smoke,
# settings_window) that launch visible HWNDs or manipulate desktop cursors have
# been retired to guarantee a 100% headless, non-interactive automated quality gate.
# -----------------------------------------------------------------------------

add_executable(nfm_ui_appearance_tests ui_appearance_tests.cpp)
nfm_configure_target(nfm_ui_appearance_tests)
nfm_enable_static_analysis(nfm_ui_appearance_tests)
target_link_libraries(nfm_ui_appearance_tests PRIVATE nfm_ui nfm_config)
add_test(NAME ui.appearance COMMAND nfm_ui_appearance_tests)
set_tests_properties(ui.appearance PROPERTIES LABELS "legacy-ui")

add_executable(nfm_ui_resources_tests ui_resources_tests.cpp)
nfm_configure_target(nfm_ui_resources_tests)
nfm_enable_static_analysis(nfm_ui_resources_tests)
target_link_libraries(nfm_ui_resources_tests PRIVATE nfm_ui nfm_config)
add_test(NAME ui.resources COMMAND nfm_ui_resources_tests)
set_tests_properties(ui.resources PROPERTIES LABELS "legacy-ui")

add_executable(nfm_ui_layout_tests ui_layout_tests.cpp)
nfm_configure_target(nfm_ui_layout_tests)
nfm_enable_static_analysis(nfm_ui_layout_tests)
target_link_libraries(nfm_ui_layout_tests PRIVATE nfm_ui)
add_test(NAME ui.layout COMMAND nfm_ui_layout_tests)
set_tests_properties(ui.layout PROPERTIES LABELS "legacy-ui")

add_executable(nfm_monitor_panel_behavior_tests monitor_panel_behavior_tests.cpp)
nfm_configure_target(nfm_monitor_panel_behavior_tests)
nfm_enable_static_analysis(nfm_monitor_panel_behavior_tests)
target_link_libraries(nfm_monitor_panel_behavior_tests PRIVATE nfm_ui)
add_test(NAME ui.monitor_panel_behavior COMMAND nfm_monitor_panel_behavior_tests)
set_tests_properties(ui.monitor_panel_behavior PROPERTIES LABELS "legacy-ui")

add_executable(nfm_ui_tray_tests ui_tray_tests.cpp)
nfm_configure_target(nfm_ui_tray_tests)
nfm_enable_static_analysis(nfm_ui_tray_tests)
target_link_libraries(nfm_ui_tray_tests PRIVATE nfm_ui D2D1 Dwrite Shell32 User32)
add_test(NAME ui.tray COMMAND nfm_ui_tray_tests)
set_tests_properties(ui.tray PROPERTIES LABELS "legacy-ui" RUN_SERIAL TRUE)

add_executable(nfm_ui_display_format_tests ui_display_format_tests.cpp)
nfm_configure_target(nfm_ui_display_format_tests)
nfm_enable_static_analysis(nfm_ui_display_format_tests)
target_link_libraries(nfm_ui_display_format_tests PRIVATE nfm_ui nfm_config nfm_ranking)
add_test(NAME ui.display_format COMMAND nfm_ui_display_format_tests)
set_tests_properties(ui.display_format PROPERTIES LABELS "legacy-ui")

add_executable(nfm_ui_settings_controller_tests ui_settings_controller_tests.cpp)
nfm_configure_target(nfm_ui_settings_controller_tests)
nfm_enable_static_analysis(nfm_ui_settings_controller_tests)
target_link_libraries(nfm_ui_settings_controller_tests PRIVATE nfm_ui nfm_config nfm_service)
add_test(NAME ui.settings_controller COMMAND nfm_ui_settings_controller_tests)
set_tests_properties(ui.settings_controller PROPERTIES LABELS "legacy-ui")

add_executable(nfm_ui_startup_registration_tests ui_startup_registration_tests.cpp)
nfm_configure_target(nfm_ui_startup_registration_tests)
nfm_enable_static_analysis(nfm_ui_startup_registration_tests)
target_link_libraries(nfm_ui_startup_registration_tests PRIVATE nfm_ui Advapi32 User32)
add_test(NAME ui.startup_registration COMMAND nfm_ui_startup_registration_tests)
set_tests_properties(ui.startup_registration PROPERTIES LABELS "legacy-ui")

# -----------------------------------------------------------------------------
# Core Configuration Tests
# -----------------------------------------------------------------------------

add_executable(nfm_config_model_tests config_model_tests.cpp)
nfm_configure_target(nfm_config_model_tests)
nfm_enable_static_analysis(nfm_config_model_tests)
target_link_libraries(nfm_config_model_tests PRIVATE nfm_config)
add_test(NAME config.model COMMAND nfm_config_model_tests)
set_tests_properties(config.model PROPERTIES LABELS "core")

add_executable(nfm_config_codec_tests config_codec_tests.cpp)
nfm_configure_target(nfm_config_codec_tests)
nfm_enable_static_analysis(nfm_config_codec_tests)
target_link_libraries(nfm_config_codec_tests PRIVATE nfm_config)
add_test(NAME config.codec COMMAND nfm_config_codec_tests)
set_tests_properties(config.codec PROPERTIES LABELS "core")

add_executable(nfm_config_store_tests config_store_tests.cpp)
nfm_configure_target(nfm_config_store_tests)
nfm_enable_static_analysis(nfm_config_store_tests)
target_link_libraries(nfm_config_store_tests PRIVATE nfm_config)
add_test(NAME config.store COMMAND nfm_config_store_tests)
set_tests_properties(config.store PROPERTIES LABELS "core")

# -----------------------------------------------------------------------------
# Core Collector & Ranking & Process Tests
# -----------------------------------------------------------------------------

add_executable(nfm_application_accumulator_tests application_accumulator_tests.cpp)
nfm_configure_target(nfm_application_accumulator_tests)
nfm_enable_static_analysis(nfm_application_accumulator_tests)
target_link_libraries(nfm_application_accumulator_tests PRIVATE nfm_ranking)
add_test(NAME ranking.accumulator COMMAND nfm_application_accumulator_tests)
set_tests_properties(ranking.accumulator PROPERTIES LABELS "collector")

add_executable(nfm_ranking_engine_tests ranking_engine_tests.cpp)
nfm_configure_target(nfm_ranking_engine_tests)
nfm_enable_static_analysis(nfm_ranking_engine_tests)
target_link_libraries(nfm_ranking_engine_tests PRIVATE nfm_ranking)
add_test(NAME ranking.engine COMMAND nfm_ranking_engine_tests)
set_tests_properties(ranking.engine PROPERTIES LABELS "collector")

add_executable(nfm_identity_resolver_tests identity_resolver_tests.cpp)
nfm_configure_target(nfm_identity_resolver_tests)
nfm_enable_static_analysis(nfm_identity_resolver_tests)
target_link_libraries(nfm_identity_resolver_tests PRIVATE nfm_process)
add_test(NAME identity.resolver COMMAND nfm_identity_resolver_tests)
set_tests_properties(identity.resolver PROPERTIES LABELS "collector")

add_executable(nfm_process_tracker_tests process_tracker_tests.cpp)
nfm_configure_target(nfm_process_tracker_tests)
nfm_enable_static_analysis(nfm_process_tracker_tests)
target_link_libraries(nfm_process_tracker_tests PRIVATE nfm_process)
add_test(NAME process.tracker COMMAND nfm_process_tracker_tests)
set_tests_properties(process.tracker PROPERTIES LABELS "collector")

add_executable(nfm_windows_process_probe_tests windows_process_probe_tests.cpp)
nfm_configure_target(nfm_windows_process_probe_tests)
nfm_enable_static_analysis(nfm_windows_process_probe_tests)
target_link_libraries(nfm_windows_process_probe_tests PRIVATE nfm_process)
add_test(NAME process.windows_probe COMMAND nfm_windows_process_probe_tests)
set_tests_properties(process.windows_probe PROPERTIES LABELS "collector")

add_executable(nfm_network_event_decoder_tests network_event_decoder_tests.cpp)
nfm_configure_target(nfm_network_event_decoder_tests)
nfm_enable_static_analysis(nfm_network_event_decoder_tests)
target_link_libraries(nfm_network_event_decoder_tests PRIVATE nfm_collector)
add_test(NAME collector.network_decoder COMMAND nfm_network_event_decoder_tests)
set_tests_properties(collector.network_decoder PROPERTIES LABELS "collector")

add_executable(nfm_collector_core_tests collector_core_tests.cpp)
nfm_configure_target(nfm_collector_core_tests)
nfm_enable_static_analysis(nfm_collector_core_tests)
target_link_libraries(nfm_collector_core_tests PRIVATE nfm_collector)
add_test(NAME collector.core COMMAND nfm_collector_core_tests)
set_tests_properties(collector.core PROPERTIES LABELS "collector")

add_executable(nfm_collector_host_tests collector_host_tests.cpp)
nfm_configure_target(nfm_collector_host_tests)
nfm_enable_static_analysis(nfm_collector_host_tests)
target_link_libraries(nfm_collector_host_tests PRIVATE nfm_collector nfm_ipc Advapi32 Tdh)
add_test(NAME collector.host COMMAND nfm_collector_host_tests)
set_tests_properties(collector.host PROPERTIES LABELS "collector")

add_executable(nfm_service_control_tests service_control_tests.cpp)
nfm_configure_target(nfm_service_control_tests)
nfm_enable_static_analysis(nfm_service_control_tests)
target_link_libraries(nfm_service_control_tests PRIVATE nfm_service)
add_test(NAME service.control COMMAND nfm_service_control_tests)
set_tests_properties(service.control PROPERTIES LABELS "collector")

# -----------------------------------------------------------------------------
# Protocol V1 & IPC Tests
# -----------------------------------------------------------------------------

add_executable(nfm_ipc_frame_codec_tests ipc_frame_codec_tests.cpp)
nfm_configure_target(nfm_ipc_frame_codec_tests)
nfm_enable_static_analysis(nfm_ipc_frame_codec_tests)
target_link_libraries(nfm_ipc_frame_codec_tests PRIVATE nfm_ipc)
add_test(NAME ipc.frame_codec COMMAND nfm_ipc_frame_codec_tests)
set_tests_properties(ipc.frame_codec PROPERTIES LABELS "protocol")

add_executable(nfm_ipc_snapshot_codec_tests ipc_snapshot_codec_tests.cpp)
nfm_configure_target(nfm_ipc_snapshot_codec_tests)
nfm_enable_static_analysis(nfm_ipc_snapshot_codec_tests)
target_link_libraries(nfm_ipc_snapshot_codec_tests PRIVATE nfm_ipc)
add_test(NAME ipc.snapshot_codec COMMAND nfm_ipc_snapshot_codec_tests)
set_tests_properties(ipc.snapshot_codec PROPERTIES LABELS "protocol")

add_executable(nfm_ipc_application_catalog_tests ipc_application_catalog_tests.cpp)
nfm_configure_target(nfm_ipc_application_catalog_tests)
nfm_enable_static_analysis(nfm_ipc_application_catalog_tests)
target_link_libraries(nfm_ipc_application_catalog_tests PRIVATE nfm_ipc)
add_test(NAME ipc.application_catalog COMMAND nfm_ipc_application_catalog_tests)
set_tests_properties(ipc.application_catalog PROPERTIES LABELS "protocol")

add_executable(nfm_ipc_security_policy_tests ipc_security_policy_tests.cpp)
nfm_configure_target(nfm_ipc_security_policy_tests)
nfm_enable_static_analysis(nfm_ipc_security_policy_tests)
target_link_libraries(nfm_ipc_security_policy_tests PRIVATE nfm_ipc)
add_test(NAME ipc.security_policy COMMAND nfm_ipc_security_policy_tests)
set_tests_properties(ipc.security_policy PROPERTIES LABELS "ipc")

add_executable(nfm_ipc_named_pipe_transport_tests ipc_named_pipe_transport_tests.cpp)
nfm_configure_target(nfm_ipc_named_pipe_transport_tests)
nfm_enable_static_analysis(nfm_ipc_named_pipe_transport_tests)
target_link_libraries(nfm_ipc_named_pipe_transport_tests PRIVATE nfm_ipc)
add_test(NAME ipc.named_pipe_transport COMMAND nfm_ipc_named_pipe_transport_tests)
set_tests_properties(ipc.named_pipe_transport PROPERTIES LABELS "ipc")

add_executable(nfm_ipc_server_tests ipc_server_tests.cpp)
nfm_configure_target(nfm_ipc_server_tests)
nfm_enable_static_analysis(nfm_ipc_server_tests)
target_link_libraries(nfm_ipc_server_tests PRIVATE nfm_ipc)
add_test(NAME ipc.server COMMAND nfm_ipc_server_tests)
set_tests_properties(ipc.server PROPERTIES LABELS "ipc")

add_executable(nfm_ipc_server_integration_tests ipc_server_integration_tests.cpp)
nfm_configure_target(nfm_ipc_server_integration_tests)
nfm_enable_static_analysis(nfm_ipc_server_integration_tests)
target_link_libraries(nfm_ipc_server_integration_tests PRIVATE nfm_ipc Advapi32)
add_test(NAME ipc.server_integration COMMAND nfm_ipc_server_integration_tests)
set_tests_properties(ipc.server_integration PROPERTIES LABELS "ipc")

add_executable(nfm_ipc_snapshot_publisher_tests ipc_snapshot_publisher_tests.cpp)
nfm_configure_target(nfm_ipc_snapshot_publisher_tests)
nfm_enable_static_analysis(nfm_ipc_snapshot_publisher_tests)
target_link_libraries(nfm_ipc_snapshot_publisher_tests PRIVATE nfm_ipc)
add_test(NAME ipc.snapshot_publisher COMMAND nfm_ipc_snapshot_publisher_tests)
set_tests_properties(ipc.snapshot_publisher PROPERTIES LABELS "ipc")

add_executable(nfm_ipc_client_tests ipc_client_tests.cpp)
nfm_configure_target(nfm_ipc_client_tests)
nfm_enable_static_analysis(nfm_ipc_client_tests)
target_link_libraries(nfm_ipc_client_tests PRIVATE nfm_ipc)
add_test(NAME ipc.client COMMAND nfm_ipc_client_tests)
set_tests_properties(ipc.client PROPERTIES LABELS "ipc")

add_executable(nfm_ipc_integration_tests ipc_integration_tests.cpp)
nfm_configure_target(nfm_ipc_integration_tests)
nfm_enable_static_analysis(nfm_ipc_integration_tests)
target_link_libraries(nfm_ipc_integration_tests PRIVATE nfm_ipc Advapi32)
add_test(NAME ipc.integration COMMAND nfm_ipc_integration_tests)
set_tests_properties(ipc.integration PROPERTIES LABELS "ipc;requires-admin" RUN_SERIAL TRUE)

# -----------------------------------------------------------------------------
# Privileged ETW Tests
# -----------------------------------------------------------------------------

add_executable(nfm_windows_etw_record_decoder_tests windows_etw_record_decoder_tests.cpp)
nfm_configure_target(nfm_windows_etw_record_decoder_tests)
nfm_enable_static_analysis(nfm_windows_etw_record_decoder_tests)
target_link_libraries(nfm_windows_etw_record_decoder_tests PRIVATE nfm_collector)
add_test(NAME collector.windows_record_decoder COMMAND nfm_windows_etw_record_decoder_tests)
set_tests_properties(collector.windows_record_decoder PROPERTIES LABELS "collector")

add_executable(nfm_windows_etw_session_tests windows_etw_session_tests.cpp)
nfm_configure_target(nfm_windows_etw_session_tests)
nfm_enable_static_analysis(nfm_windows_etw_session_tests)
target_link_libraries(nfm_windows_etw_session_tests PRIVATE nfm_collector)
add_test(NAME collector.windows_session COMMAND nfm_windows_etw_session_tests)
set_tests_properties(collector.windows_session PROPERTIES LABELS "collector")

add_executable(nfm_etw_collector_integration_tests etw_collector_integration_tests.cpp)
nfm_configure_target(nfm_etw_collector_integration_tests)
nfm_enable_static_analysis(nfm_etw_collector_integration_tests)
target_link_libraries(
    nfm_etw_collector_integration_tests
    PRIVATE nfm_collector Advapi32 Ws2_32
)
add_test(NAME collector.etw_integration COMMAND nfm_etw_collector_integration_tests)
set_tests_properties(collector.etw_integration PROPERTIES LABELS "collector;requires-admin" RUN_SERIAL TRUE)
