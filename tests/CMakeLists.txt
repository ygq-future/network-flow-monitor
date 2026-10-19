set(NFM_TEST_SUITES
    "app_runtime_tests.cpp|app.runtime|core"
    "config_tests.cpp|config.suite|core"
    "process_tests.cpp|process.suite|collector"
    "ranking_tests.cpp|ranking.suite|collector"
    "collector_tests.cpp|collector.suite|collector"
    "etw_integration_tests.cpp|collector.etw_integration|collector,requires-admin"
    "ipc_protocol_tests.cpp|ipc.protocol|protocol,ipc"
    "ipc_session_tests.cpp|ipc.session|ipc"
    "ipc_integration_tests.cpp|ipc.integration|ipc,requires-admin"
    "host_tests.cpp|host.suite|host"
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
    collector.suite
    ipc.protocol
    ipc.session
    ipc.integration
    host.suite
    PROPERTIES RESOURCE_LOCK nfm_named_pipe
)
set_tests_properties(ipc.integration PROPERTIES RUN_SERIAL TRUE)
set_tests_properties(collector.etw_integration PROPERTIES RUN_SERIAL TRUE)
