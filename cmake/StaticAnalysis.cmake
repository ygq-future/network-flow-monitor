include_guard(GLOBAL)

option(NFM_ENABLE_CLANG_TIDY "Run clang-tidy while compiling first-party targets" OFF)
option(NFM_ENABLE_MSVC_ANALYZE "Run MSVC code analysis while compiling first-party targets" OFF)
set(NFM_CLANG_TIDY_EXECUTABLE "" CACHE FILEPATH "Absolute path to clang-tidy.exe")

function(nfm_enable_static_analysis target_name)
    if(NFM_ENABLE_CLANG_TIDY)
        if(NOT EXISTS "${NFM_CLANG_TIDY_EXECUTABLE}")
            message(FATAL_ERROR "NFM_CLANG_TIDY_EXECUTABLE must point to clang-tidy.exe")
        endif()

        set_property(
            TARGET ${target_name}
            PROPERTY CXX_CLANG_TIDY
                "${NFM_CLANG_TIDY_EXECUTABLE};--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy;--extra-arg-before=--driver-mode=cl"
        )
    endif()

    if(NFM_ENABLE_MSVC_ANALYZE)
        if(NOT MSVC)
            message(FATAL_ERROR "NFM_ENABLE_MSVC_ANALYZE requires MSVC")
        endif()
        target_compile_options(${target_name} PRIVATE /analyze)
    endif()
endfunction()
