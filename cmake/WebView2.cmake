include_guard(GLOBAL)

include(FetchContent)

set(NFM_WEBVIEW2_VERSION "1.0.2903.40" CACHE STRING "Microsoft.Web.WebView2 NuGet package version")

FetchContent_Declare(
    webview2_sdk
    URL "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/${NFM_WEBVIEW2_VERSION}/microsoft.web.webview2.${NFM_WEBVIEW2_VERSION}.nupkg"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(webview2_sdk)

add_library(nfm_webview2_sdk INTERFACE)
target_include_directories(
    nfm_webview2_sdk
    INTERFACE
        "${webview2_sdk_SOURCE_DIR}/build/native/include"
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(NFM_WEBVIEW2_ARCH "x64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(NFM_WEBVIEW2_ARCH "x86")
else()
    set(NFM_WEBVIEW2_ARCH "arm64")
endif()

# Provide both static loader and dynamic loader options
set(NFM_WEBVIEW2_STATIC_LIB "${webview2_sdk_SOURCE_DIR}/build/native/${NFM_WEBVIEW2_ARCH}/WebView2LoaderStatic.lib")
set(NFM_WEBVIEW2_DLL_LIB "${webview2_sdk_SOURCE_DIR}/build/native/${NFM_WEBVIEW2_ARCH}/WebView2Loader.dll.lib")
set(NFM_WEBVIEW2_DLL "${webview2_sdk_SOURCE_DIR}/build/native/${NFM_WEBVIEW2_ARCH}/WebView2Loader.dll")

if(EXISTS "${NFM_WEBVIEW2_STATIC_LIB}")
    target_link_libraries(nfm_webview2_sdk INTERFACE "${NFM_WEBVIEW2_STATIC_LIB}" Shlwapi Version)
else()
    target_link_libraries(nfm_webview2_sdk INTERFACE "${NFM_WEBVIEW2_DLL_LIB}" Shlwapi Version)
endif()
