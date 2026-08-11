add_library(nfm_yyjson STATIC yyjson.c)

target_include_directories(nfm_yyjson SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

set_target_properties(
    nfm_yyjson
    PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
)

if(MSVC)
    target_compile_options(nfm_yyjson PRIVATE /W0)
endif()
