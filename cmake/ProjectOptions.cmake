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
