include_guard(GLOBAL)

option(NFM_ENABLE_CLANG_TIDY "Run clang-tidy while compiling first-party targets" OFF)
option(NFM_ENABLE_MSVC_ANALYZE "Run MSVC code analysis while compiling first-party targets" OFF)
set(NFM_CLANG_TIDY_EXECUTABLE "" CACHE FILEPATH "Absolute path to clang-tidy.exe")

function(nfm_enable_static_analysis target_name)
    cmake_parse_arguments(NFM_ANALYSIS "ENABLE_EXCEPTIONS" "" "" ${ARGN})

    if(NFM_ENABLE_CLANG_TIDY)
        if(NOT EXISTS "${NFM_CLANG_TIDY_EXECUTABLE}")
            message(FATAL_ERROR "NFM_CLANG_TIDY_EXECUTABLE must point to clang-tidy.exe")
        endif()

        set(tidy_command
            "${NFM_CLANG_TIDY_EXECUTABLE}"
            "--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy"
            # Visual Studio's bundled clang-tidy can omit Clang's cpuid.h resource header,
            # while the Windows SDK DirectXMath header includes it whenever __clang__ is set.
            # Use DirectXMath's supported scalar path for analysis; production MSVC builds
            # retain their normal intrinsic implementation.
            "--extra-arg=/D_XM_NO_INTRINSICS_"
        )
        if(NFM_ANALYSIS_ENABLE_EXCEPTIONS)
            list(APPEND tidy_command "--extra-arg=/EHsc")
        endif()
        set_property(TARGET ${target_name} PROPERTY CXX_CLANG_TIDY "${tidy_command}")
    endif()

    if(NFM_ENABLE_MSVC_ANALYZE)
        if(NOT MSVC)
            message(FATAL_ERROR "NFM_ENABLE_MSVC_ANALYZE requires MSVC")
        endif()
        target_compile_options(${target_name} PRIVATE /analyze /analyze:external-)
    endif()
endfunction()
