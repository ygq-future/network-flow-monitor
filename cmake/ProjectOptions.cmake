include_guard(GLOBAL)

function(nfm_configure_target target_name)
    target_compile_features(${target_name} PUBLIC cxx_std_20)

    target_compile_definitions(
        ${target_name}
        PRIVATE
            UNICODE
            _UNICODE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
    )

    if(MSVC)
        target_compile_options(
            ${target_name}
            PRIVATE
                /W4
                /WX
                /permissive-
                /sdl
                /utf-8
                /Zc:__cplusplus
                /Zc:preprocessor
                /EHsc
                /external:anglebrackets
                /external:W0
        )
    else()
        message(FATAL_ERROR "Network Flow Monitor currently requires MSVC")
    endif()
endfunction()

function(nfm_enable_shared_pch target_name)
    if(NFM_ENABLE_CLANG_TIDY)
        return()
    endif()
    if(NOT TARGET nfm_pch)
        add_library(nfm_pch OBJECT "${PROJECT_BINARY_DIR}/generated/nfm_pch.cpp")
        file(WRITE "${PROJECT_BINARY_DIR}/generated/nfm_pch.cpp" "// Shared PCH host\n")
        nfm_configure_target(nfm_pch)
        if(NFM_ENABLE_MSVC_ANALYZE)
            target_compile_options(nfm_pch PRIVATE /analyze /analyze:external-)
        endif()
        target_precompile_headers(
            nfm_pch
            PUBLIC
                <windows.h>
                <algorithm>
                <array>
                <atomic>
                <chrono>
                <cstdint>
                <filesystem>
                <functional>
                <iostream>
                <memory>
                <mutex>
                <optional>
                <span>
                <string>
                <string_view>
                <utility>
                <vector>
        )
    endif()
    target_precompile_headers(${target_name} REUSE_FROM nfm_pch)
endfunction()
