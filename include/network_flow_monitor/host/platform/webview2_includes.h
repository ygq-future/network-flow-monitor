#ifndef NETWORK_FLOW_MONITOR_HOST_PLATFORM_WEBVIEW2_INCLUDES_H
#define NETWORK_FLOW_MONITOR_HOST_PLATFORM_WEBVIEW2_INCLUDES_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

// clang-format off
#include <windows.h>
#include <unknwn.h>
#include <WebView2.h>
#ifndef __clang__
#include <WebView2EnvironmentOptions.h>
#endif
// clang-format on

#endif // NETWORK_FLOW_MONITOR_HOST_PLATFORM_WEBVIEW2_INCLUDES_H
