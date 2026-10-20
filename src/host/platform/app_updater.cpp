#include "network_flow_monitor/host/platform/app_updater.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <utility>
#include <vector>
#include <winhttp.h>
#include <yyjson.h>

namespace nfm::host::platform {

namespace {

constexpr DWORD kHttpConnectTimeoutMs = 8'000;
constexpr DWORD kHttpSendReceiveTimeoutMs = 30'000;
constexpr std::uint64_t kMaxReleaseArchiveSizeBytes = 256ULL * 1024ULL * 1024ULL; // 256 MiB

std::wstring TrimWide(std::wstring_view text) {
    constexpr std::wstring_view kWhitespace = L" \t\r\n";
    const auto first = text.find_first_not_of(kWhitespace);
    if (first == std::wstring_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(kWhitespace);
    return std::wstring(text.substr(first, last - first + 1U));
}

std::string WideToUtf8(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), needed,
                        nullptr, nullptr);
    return out;
}

std::wstring ReadEnvironmentProxy() {
    constexpr std::array<const wchar_t*, 6> kProxyVars{
        L"HTTPS_PROXY", L"https_proxy", L"HTTP_PROXY", L"http_proxy", L"ALL_PROXY", L"all_proxy",
    };
    std::array<wchar_t, 512> buf{};
    for (const wchar_t* var_name : kProxyVars) {
        const DWORD len =
            GetEnvironmentVariableW(var_name, buf.data(), static_cast<DWORD>(buf.size()));
        if (len > 0U && len < buf.size()) {
            std::wstring normalized =
                AppUpdater::normalize_winhttp_proxy_server(std::wstring_view(buf.data(), len));
            if (!normalized.empty()) {
                return normalized;
            }
        }
    }
    return {};
}

struct RegistryProxySnapshot {
    std::wstring proxy_server;
    bool proxy_enabled{false};
};

RegistryProxySnapshot ReadInternetSettingsProxy() {
    RegistryProxySnapshot snapshot;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings", 0,
                      KEY_QUERY_VALUE, &key) != ERROR_SUCCESS ||
        key == nullptr) {
        return snapshot;
    }

    DWORD enabled = 0;
    DWORD enabled_size = sizeof(enabled);
    DWORD value_type = 0;
    if (RegQueryValueExW(key, L"ProxyEnable", nullptr, &value_type,
                         reinterpret_cast<LPBYTE>(&enabled), &enabled_size) == ERROR_SUCCESS &&
        value_type == REG_DWORD) {
        snapshot.proxy_enabled = (enabled != 0U);
    }

    std::array<wchar_t, 512> server_buf{};
    DWORD server_bytes = sizeof(server_buf) - sizeof(wchar_t);
    value_type = 0;
    if (RegQueryValueExW(key, L"ProxyServer", nullptr, &value_type,
                         reinterpret_cast<LPBYTE>(server_buf.data()),
                         &server_bytes) == ERROR_SUCCESS &&
        (value_type == REG_SZ || value_type == REG_EXPAND_SZ)) {
        snapshot.proxy_server = server_buf.data();
    }

    RegCloseKey(key);
    return snapshot;
}

std::vector<HttpAccessCandidate> DiscoverRuntimeHttpAccessCandidates() {
    const std::wstring env_proxy = ReadEnvironmentProxy();
    const RegistryProxySnapshot reg_proxy = ReadInternetSettingsProxy();
    return AppUpdater::build_http_access_candidates(HttpProxyConfigSources{
        .env_proxy = env_proxy,
        .registry_proxy = reg_proxy.proxy_server,
        .registry_proxy_enabled = reg_proxy.proxy_enabled,
    });
}

HINTERNET OpenWinHttpSessionForCandidate(const HttpAccessCandidate& candidate) noexcept {
    switch (candidate.mode) {
    case HttpAccessMode::AutomaticProxy:
        return WinHttpOpen(L"NetworkFlowMonitor-Updater/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    case HttpAccessMode::NamedProxy:
        return WinHttpOpen(L"NetworkFlowMonitor-Updater/1.0", WINHTTP_ACCESS_TYPE_NAMED_PROXY,
                           candidate.proxy_server.c_str(), L"<local>", 0);
    case HttpAccessMode::NoProxy:
        return WinHttpOpen(L"NetworkFlowMonitor-Updater/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    return nullptr;
}

std::string DescribeAccessCandidate(const HttpAccessCandidate& candidate) {
    switch (candidate.mode) {
    case HttpAccessMode::AutomaticProxy:
        return "auto-proxy";
    case HttpAccessMode::NamedProxy:
        return "proxy=" + WideToUtf8(candidate.proxy_server);
    case HttpAccessMode::NoProxy:
        return "direct";
    }
    return "unknown";
}

class ScopedInternetHandle {
  public:
    ScopedInternetHandle() noexcept = default;
    explicit ScopedInternetHandle(HINTERNET handle) noexcept : handle_(handle) {}
    ~ScopedInternetHandle() {
        if (handle_ != nullptr) {
            WinHttpCloseHandle(handle_);
        }
    }
    ScopedInternetHandle(const ScopedInternetHandle&) = delete;
    ScopedInternetHandle& operator=(const ScopedInternetHandle&) = delete;
    ScopedInternetHandle(ScopedInternetHandle&&) = delete;
    ScopedInternetHandle& operator=(ScopedInternetHandle&&) = delete;
    [[nodiscard]] HINTERNET get() const noexcept {
        return handle_;
    }

  private:
    HINTERNET handle_{nullptr};
};

class ScopedJsonDoc {
  public:
    explicit ScopedJsonDoc(std::string_view json) noexcept {
        if (!json.empty()) {
            doc_ = yyjson_read(json.data(), json.size(), 0);
        }
    }
    ~ScopedJsonDoc() {
        if (doc_ != nullptr) {
            yyjson_doc_free(doc_);
        }
    }
    ScopedJsonDoc(const ScopedJsonDoc&) = delete;
    ScopedJsonDoc& operator=(const ScopedJsonDoc&) = delete;
    ScopedJsonDoc(ScopedJsonDoc&&) = delete;
    ScopedJsonDoc& operator=(ScopedJsonDoc&&) = delete;
    [[nodiscard]] yyjson_doc* get() const noexcept {
        return doc_;
    }

  private:
    yyjson_doc* doc_{nullptr};
};

std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int needed =
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), needed);
    return out;
}

struct ParsedUrl {
    std::wstring host;
    std::wstring path_and_query;
    INTERNET_PORT port{INTERNET_DEFAULT_HTTPS_PORT};
    bool is_https{true};
};

std::optional<ParsedUrl> CrackUrl(std::string_view url_utf8) {
    const std::wstring url_wide = Utf8ToWide(url_utf8);
    if (url_wide.empty()) {
        return std::nullopt;
    }

    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (WinHttpCrackUrl(url_wide.c_str(), static_cast<DWORD>(url_wide.size()), 0, &components) ==
        FALSE) {
        return std::nullopt;
    }

    if (components.lpszHostName == nullptr || components.dwHostNameLength == 0U) {
        return std::nullopt;
    }

    ParsedUrl result;
    result.host.assign(components.lpszHostName, components.dwHostNameLength);
    if (components.lpszUrlPath != nullptr && components.dwUrlPathLength > 0U) {
        result.path_and_query.assign(components.lpszUrlPath, components.dwUrlPathLength);
    } else {
        result.path_and_query = L"/";
    }
    if (components.lpszExtraInfo != nullptr && components.dwExtraInfoLength > 0U) {
        result.path_and_query.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    result.port = components.nPort;
    result.is_https = (components.nScheme == INTERNET_SCHEME_HTTPS);
    return result;
}

std::optional<std::string> HttpGetBufferSingle(const ParsedUrl& parsed,
                                               const HttpAccessCandidate& candidate,
                                               std::string* error_out) {
    ScopedInternetHandle session(OpenWinHttpSessionForCandidate(candidate));
    if (session.get() == nullptr) {
        if (error_out != nullptr) {
            *error_out = "WinHttpOpen failed (win32=" + std::to_string(GetLastError()) + ")";
        }
        return std::nullopt;
    }

    WinHttpSetTimeouts(session.get(), kHttpConnectTimeoutMs, kHttpConnectTimeoutMs,
                       kHttpSendReceiveTimeoutMs, kHttpSendReceiveTimeoutMs);

    ScopedInternetHandle connection(
        WinHttpConnect(session.get(), parsed.host.c_str(), parsed.port, 0));
    if (connection.get() == nullptr) {
        if (error_out != nullptr) {
            *error_out = "WinHttpConnect failed (win32=" + std::to_string(GetLastError()) + ")";
        }
        return std::nullopt;
    }

    const DWORD flags = parsed.is_https ? WINHTTP_FLAG_SECURE : 0U;
    ScopedInternetHandle request(
        WinHttpOpenRequest(connection.get(), L"GET", parsed.path_and_query.c_str(), nullptr,
                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (request.get() == nullptr) {
        if (error_out != nullptr) {
            *error_out = "WinHttpOpenRequest failed (win32=" + std::to_string(GetLastError()) + ")";
        }
        return std::nullopt;
    }

    const std::wstring headers = L"Accept: application/vnd.github+json\r\n"
                                 L"X-GitHub-Api-Version: 2022-11-28\r\n";
    if (WinHttpSendRequest(request.get(), headers.c_str(), static_cast<DWORD>(headers.size()),
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) == FALSE ||
        WinHttpReceiveResponse(request.get(), nullptr) == FALSE) {
        if (error_out != nullptr) {
            *error_out = "HTTP request failed (win32=" + std::to_string(GetLastError()) + ")";
        }
        return std::nullopt;
    }

    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    if (WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size,
                            WINHTTP_NO_HEADER_INDEX) == FALSE) {
        if (error_out != nullptr) {
            *error_out = "Failed to query HTTP status code";
        }
        return std::nullopt;
    }

    if (status_code != 200U) {
        if (error_out != nullptr) {
            *error_out = "GitHub API returned HTTP " + std::to_string(status_code);
        }
        return std::nullopt;
    }

    std::string body;
    for (;;) {
        DWORD available = 0;
        if (WinHttpQueryDataAvailable(request.get(), &available) == FALSE) {
            if (error_out != nullptr) {
                *error_out = "WinHttpQueryDataAvailable failed";
            }
            return std::nullopt;
        }
        if (available == 0U) {
            break;
        }
        if (body.size() + available > 4ULL * 1024ULL * 1024ULL) {
            if (error_out != nullptr) {
                *error_out = "GitHub API response exceeded 4 MiB limit";
            }
            return std::nullopt;
        }
        const std::size_t offset = body.size();
        body.resize(offset + available);
        DWORD bytes_read = 0;
        if (WinHttpReadData(request.get(), body.data() + offset, available, &bytes_read) == FALSE) {
            if (error_out != nullptr) {
                *error_out = "WinHttpReadData failed";
            }
            return std::nullopt;
        }
        body.resize(offset + bytes_read);
    }

    return body;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool HttpGetBuffer(std::string_view url_utf8, std::string* response_body, std::string* error_out) {
    auto parsed = CrackUrl(url_utf8);
    if (!parsed.has_value()) {
        if (error_out != nullptr) {
            *error_out = "Invalid URL format";
        }
        return false;
    }

    const auto candidates = DiscoverRuntimeHttpAccessCandidates();
    std::string first_error;
    std::string last_error;
    for (const auto& candidate : candidates) {
        std::string attempt_error;
        if (auto body = HttpGetBufferSingle(*parsed, candidate, &attempt_error); body.has_value()) {
            if (response_body != nullptr) {
                *response_body = std::move(*body);
            }
            return true;
        }
        const std::string tagged = attempt_error + " [" + DescribeAccessCandidate(candidate) + "]";
        if (first_error.empty()) {
            first_error = tagged;
        }
        last_error = tagged;
    }
    if (error_out != nullptr) {
        *error_out = (first_error == last_error || last_error.empty())
                         ? first_error
                         : (first_error + "; fallback: " + last_error);
    }
    return false;
}

bool HttpDownloadFileSingle(const ParsedUrl& parsed, const HttpAccessCandidate& candidate,
                            const std::filesystem::path& destination_file,
                            std::uint64_t expected_size_bytes,
                            const std::atomic_bool& stop_requested,
                            const std::function<void(std::uint8_t)>& on_progress,
                            std::string* error_out) {
    ScopedInternetHandle session(OpenWinHttpSessionForCandidate(candidate));
    if (session.get() == nullptr) {
        if (error_out != nullptr) {
            *error_out = "WinHttpOpen failed (win32=" + std::to_string(GetLastError()) + ")";
        }
        return false;
    }

    WinHttpSetTimeouts(session.get(), kHttpConnectTimeoutMs, kHttpConnectTimeoutMs,
                       kHttpSendReceiveTimeoutMs, kHttpSendReceiveTimeoutMs);

    ScopedInternetHandle connection(
        WinHttpConnect(session.get(), parsed.host.c_str(), parsed.port, 0));
    if (connection.get() == nullptr) {
        if (error_out != nullptr) {
            *error_out = "WinHttpConnect failed (win32=" + std::to_string(GetLastError()) + ")";
        }
        return false;
    }

    const DWORD flags = parsed.is_https ? WINHTTP_FLAG_SECURE : 0U;
    ScopedInternetHandle request(
        WinHttpOpenRequest(connection.get(), L"GET", parsed.path_and_query.c_str(), nullptr,
                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (request.get() == nullptr) {
        if (error_out != nullptr) {
            *error_out = "WinHttpOpenRequest failed (win32=" + std::to_string(GetLastError()) + ")";
        }
        return false;
    }

    if (WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA,
                           0, 0, 0) == FALSE ||
        WinHttpReceiveResponse(request.get(), nullptr) == FALSE) {
        if (error_out != nullptr) {
            *error_out =
                "Asset download request failed (win32=" + std::to_string(GetLastError()) + ")";
        }
        return false;
    }

    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    if (WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size,
                            WINHTTP_NO_HEADER_INDEX) == FALSE ||
        status_code != 200U) {
        if (error_out != nullptr) {
            *error_out = "Asset download returned HTTP " + std::to_string(status_code);
        }
        return false;
    }

    std::uint64_t total_bytes = expected_size_bytes;
    std::array<wchar_t, 64> content_length_buf{};
    DWORD content_length_size = sizeof(content_length_buf);
    if (WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_CONTENT_LENGTH,
                            WINHTTP_HEADER_NAME_BY_INDEX, content_length_buf.data(),
                            &content_length_size, WINHTTP_NO_HEADER_INDEX) != FALSE) {
        const auto parsed_len = std::wcstoull(content_length_buf.data(), nullptr, 10);
        if (parsed_len > 0U) {
            total_bytes = parsed_len;
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(destination_file.parent_path(), ec);
    std::ofstream out(destination_file, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        if (error_out != nullptr) {
            *error_out = "Failed to open update archive for writing";
        }
        return false;
    }

    std::vector<char> buffer(std::size_t{64U} * 1024U);
    std::uint64_t downloaded_bytes = 0;
    std::uint8_t last_reported_percent = 0;

    for (;;) {
        if (stop_requested.load()) {
            if (error_out != nullptr) {
                *error_out = "Update download cancelled";
            }
            return false;
        }

        DWORD available = 0;
        if (WinHttpQueryDataAvailable(request.get(), &available) == FALSE) {
            if (error_out != nullptr) {
                *error_out = "WinHttpQueryDataAvailable failed during download";
            }
            return false;
        }
        if (available == 0U) {
            break;
        }

        const DWORD chunk_size = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
        DWORD bytes_read = 0;
        if (WinHttpReadData(request.get(), buffer.data(), chunk_size, &bytes_read) == FALSE) {
            if (error_out != nullptr) {
                *error_out = "WinHttpReadData failed during download";
            }
            return false;
        }
        if (bytes_read == 0U) {
            break;
        }

        downloaded_bytes += bytes_read;
        if (downloaded_bytes > kMaxReleaseArchiveSizeBytes) {
            if (error_out != nullptr) {
                *error_out = "Update archive exceeded maximum allowed size";
            }
            return false;
        }

        out.write(buffer.data(), static_cast<std::streamsize>(bytes_read));
        if (!out.good()) {
            if (error_out != nullptr) {
                *error_out = "Failed to write update archive to disk";
            }
            return false;
        }

        if (total_bytes > 0U && on_progress) {
            const auto pct = static_cast<std::uint8_t>(
                std::min<std::uint64_t>(99ULL, (downloaded_bytes * 100ULL) / total_bytes));
            if (pct != last_reported_percent) {
                last_reported_percent = pct;
                on_progress(pct);
            }
        }
    }

    out.flush();
    out.close();
    if (downloaded_bytes == 0U) {
        if (error_out != nullptr) {
            *error_out = "Downloaded update archive is empty";
        }
        return false;
    }
    if (on_progress) {
        on_progress(100);
    }
    return true;
}

bool HttpDownloadFile(std::string_view url_utf8, const std::filesystem::path& destination_file,
                      std::uint64_t expected_size_bytes, const std::atomic_bool& stop_requested,
                      const std::function<void(std::uint8_t)>& on_progress,
                      std::string* error_out) {
    auto parsed = CrackUrl(url_utf8);
    if (!parsed.has_value()) {
        if (error_out != nullptr) {
            *error_out = "Invalid asset download URL";
        }
        return false;
    }

    const auto candidates = DiscoverRuntimeHttpAccessCandidates();
    std::string first_error;
    std::string last_error;
    for (const auto& candidate : candidates) {
        if (stop_requested.load()) {
            if (error_out != nullptr) {
                *error_out = "Update download cancelled";
            }
            return false;
        }

        std::string attempt_error;
        if (HttpDownloadFileSingle(*parsed, candidate, destination_file, expected_size_bytes,
                                   stop_requested, on_progress, &attempt_error)) {
            return true;
        }

        std::error_code remove_ec;
        std::filesystem::remove(destination_file, remove_ec);

        const std::string tagged = attempt_error + " [" + DescribeAccessCandidate(candidate) + "]";
        if (first_error.empty()) {
            first_error = tagged;
        }
        last_error = tagged;
    }

    if (error_out != nullptr) {
        *error_out = (first_error == last_error || last_error.empty())
                         ? first_error
                         : (first_error + "; fallback: " + last_error);
    }
    return false;
}

bool ExtractZipArchiveToDirectory(const std::filesystem::path& zip_path,
                                  const std::filesystem::path& stage_dir, std::string* error_out) {
    std::error_code ec;
    std::filesystem::remove_all(stage_dir, ec);
    std::filesystem::create_directories(stage_dir, ec);

    std::wstring cmd =
        L"tar.exe -xf \"" + zip_path.wstring() + L"\" -C \"" + stage_dir.wstring() + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> mutable_cmd(cmd.begin(), cmd.end());
    mutable_cmd.push_back(L'\0');

    if (CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                       nullptr, nullptr, &si, &pi) == FALSE) {
        if (error_out != nullptr) {
            *error_out = "Failed to launch tar.exe archive extractor (win32=" +
                         std::to_string(GetLastError()) + ")";
        }
        return false;
    }

    WaitForSingleObject(pi.hProcess, 60'000);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exit_code != 0U) {
        if (error_out != nullptr) {
            *error_out =
                "tar.exe archive extraction failed with exit code " + std::to_string(exit_code);
        }
        return false;
    }

    if (!std::filesystem::exists(stage_dir / L"NetworkFlowMonitor.exe", ec) ||
        !std::filesystem::exists(stage_dir / L"nfm-collector.exe", ec) ||
        !std::filesystem::exists(stage_dir / L"ui" / L"index.html", ec)) {
        if (error_out != nullptr) {
            *error_out =
                "Extracted archive is missing required portable files (NetworkFlowMonitor.exe, "
                "nfm-collector.exe, or ui/index.html)";
        }
        return false;
    }

    return true;
}

} // namespace

AppUpdater::AppUpdater(AppUpdaterOptions options) : options_(std::move(options)) {
    status_.state = "Idle";
    status_.current_version = options_.current_version;
}

AppUpdater::~AppUpdater() {
    cancel_and_wait();
}

void AppUpdater::set_status_callback(StatusCallback callback) {
    std::scoped_lock lock(mutex_);
    status_callback_ = std::move(callback);
}

void AppUpdater::set_diagnostic_callback(DiagnosticCallback callback) {
    std::scoped_lock lock(mutex_);
    diagnostic_callback_ = std::move(callback);
}

void AppUpdater::set_shutdown_request_callback(ShutdownRequestCallback callback) {
    std::scoped_lock lock(mutex_);
    shutdown_callback_ = std::move(callback);
}

bridge::UpdateStatusDto AppUpdater::status() const {
    std::scoped_lock lock(mutex_);
    return status_;
}

void AppUpdater::cancel_and_wait() noexcept {
    stop_requested_.store(true);
    if (worker_thread_.joinable()) {
        try {
            worker_thread_.join();
        } catch (...) {
        }
    }
    worker_busy_.store(false);
}

std::optional<SemanticVersion> AppUpdater::parse_semver(std::string_view version_text) noexcept {
    while (!version_text.empty() && (version_text.front() == ' ' || version_text.front() == '\t' ||
                                     version_text.front() == 'v' || version_text.front() == 'V')) {
        version_text.remove_prefix(1);
    }
    while (!version_text.empty() && (version_text.back() == ' ' || version_text.back() == '\t' ||
                                     version_text.back() == '\r' || version_text.back() == '\n')) {
        version_text.remove_suffix(1);
    }
    // Strip optional pre-release or build metadata suffix (-alpha, +build)
    if (const auto dash_pos = version_text.find_first_of("-+");
        dash_pos != std::string_view::npos) {
        version_text = version_text.substr(0, dash_pos);
    }

    const auto first_dot = version_text.find('.');
    if (first_dot == std::string_view::npos) {
        return std::nullopt;
    }
    const auto second_dot = version_text.find('.', first_dot + 1U);
    if (second_dot == std::string_view::npos) {
        return std::nullopt;
    }
    if (version_text.find('.', second_dot + 1U) != std::string_view::npos) {
        return std::nullopt;
    }

    const std::string_view major_part = version_text.substr(0, first_dot);
    const std::string_view minor_part =
        version_text.substr(first_dot + 1U, second_dot - first_dot - 1U);
    const std::string_view patch_part = version_text.substr(second_dot + 1U);

    if (major_part.empty() || minor_part.empty() || patch_part.empty()) {
        return std::nullopt;
    }

    SemanticVersion ver{};
    const auto [p1, ec1] =
        std::from_chars(major_part.data(), major_part.data() + major_part.size(), ver.major);
    const auto [p2, ec2] =
        std::from_chars(minor_part.data(), minor_part.data() + minor_part.size(), ver.minor);
    const auto [p3, ec3] =
        std::from_chars(patch_part.data(), patch_part.data() + patch_part.size(), ver.patch);

    if (ec1 != std::errc{} || p1 != major_part.data() + major_part.size() || ec2 != std::errc{} ||
        p2 != minor_part.data() + minor_part.size() || ec3 != std::errc{} ||
        p3 != patch_part.data() + patch_part.size()) {
        return std::nullopt;
    }
    return ver;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool AppUpdater::is_newer_version(std::string_view current_version,
                                  std::string_view candidate_version) noexcept {
    const auto current = parse_semver(current_version);
    const auto candidate = parse_semver(candidate_version);
    if (!current.has_value() || !candidate.has_value()) {
        return false;
    }
    return *candidate > *current;
}

std::optional<ReleaseAssetInfo> AppUpdater::parse_github_release_json(std::string_view json_payload,
                                                                      std::string* error_out) {
    ScopedJsonDoc doc(json_payload);
    if (doc.get() == nullptr) {
        if (error_out != nullptr) {
            *error_out = "Malformed JSON from GitHub Releases API";
        }
        return std::nullopt;
    }

    yyjson_val* root = yyjson_doc_get_root(doc.get());
    if (root == nullptr || !yyjson_is_obj(root)) {
        if (error_out != nullptr) {
            *error_out = "Expected JSON object from GitHub Releases API";
        }
        return std::nullopt;
    }

    yyjson_val* tag_val = yyjson_obj_get(root, "tag_name");
    if (tag_val == nullptr || !yyjson_is_str(tag_val)) {
        if (error_out != nullptr) {
            *error_out = "GitHub release JSON missing 'tag_name'";
        }
        return std::nullopt;
    }

    std::string tag_name = yyjson_get_str(tag_val);
    const auto parsed_ver = parse_semver(tag_name);
    if (!parsed_ver.has_value()) {
        if (error_out != nullptr) {
            *error_out = "Release tag_name is not a valid semantic version: " + tag_name;
        }
        return std::nullopt;
    }

    ReleaseAssetInfo info;
    info.version = std::to_string(parsed_ver->major) + "." + std::to_string(parsed_ver->minor) +
                   "." + std::to_string(parsed_ver->patch);
    yyjson_val* assets_val = yyjson_obj_get(root, "assets");
    if (assets_val != nullptr && yyjson_is_arr(assets_val)) {
        std::size_t idx = 0;
        std::size_t max = 0;
        yyjson_val* asset = nullptr;
        yyjson_arr_foreach(assets_val, idx, max, asset) {
            if (asset == nullptr || !yyjson_is_obj(asset)) {
                continue;
            }
            yyjson_val* name_val = yyjson_obj_get(asset, "name");
            yyjson_val* dl_val = yyjson_obj_get(asset, "browser_download_url");
            if (name_val == nullptr || !yyjson_is_str(name_val) || dl_val == nullptr ||
                !yyjson_is_str(dl_val)) {
                continue;
            }
            std::string_view asset_name = yyjson_get_str(name_val);
            if (asset_name.ends_with(".zip") &&
                asset_name.find("win-x64") != std::string_view::npos) {
                info.download_url = yyjson_get_str(dl_val);
                if (yyjson_val* size_val = yyjson_obj_get(asset, "size");
                    size_val != nullptr && yyjson_is_uint(size_val)) {
                    info.asset_size_bytes = yyjson_get_uint(size_val);
                }
                break;
            }
        }
    }

    if (info.download_url.empty()) {
        if (error_out != nullptr) {
            *error_out = "No Windows x64 portable .zip asset found in release " + tag_name;
        }
        return std::nullopt;
    }

    return info;
}

std::wstring AppUpdater::build_apply_update_script(std::uint32_t host_pid,
                                                   const std::filesystem::path& staged_dir,
                                                   const std::filesystem::path& app_root_dir,
                                                   const std::filesystem::path& host_exe_path) {
    const std::wstring staged_str = staged_dir.wstring();
    const std::wstring root_str = app_root_dir.wstring();
    const std::wstring exe_str = host_exe_path.wstring();
    const std::wstring update_root_str = staged_dir.parent_path().wstring();

    std::wstring script;
    script += L"@echo off\r\n";
    script += L"chcp 65001 >nul 2>&1\r\n";
    script += L"set \"HOST_PID=" + std::to_wstring(host_pid) + L"\"\r\n";
    script += L"set \"STAGED_DIR=" + staged_str + L"\"\r\n";
    script += L"set \"APP_ROOT=" + root_str + L"\"\r\n";
    script += L"set \"HOST_EXE=" + exe_str + L"\"\r\n";
    script += L"set \"UPDATE_ROOT=" + update_root_str + L"\"\r\n";
    script += L"for /L %%I in (1,1,60) do (\r\n";
    script += L"    tasklist /FI \"PID eq %HOST_PID%\" /NH 2>nul | findstr /I "
              L"\"NetworkFlowMonitor\" >nul\r\n";
    script += L"    if errorlevel 1 goto :host_exited\r\n";
    script += L"    ping 127.0.0.1 -n 2 -w 200 >nul 2>&1\r\n";
    script += L")\r\n";
    script += L":host_exited\r\n";
    script += L"for /L %%I in (1,1,20) do (\r\n";
    script += L"    xcopy \"%STAGED_DIR%\\*\" \"%APP_ROOT%\\\" /E /I /Y /Q >nul 2>&1\r\n";
    script += L"    if not errorlevel 1 goto :copy_done\r\n";
    script += L"    ping 127.0.0.1 -n 2 -w 250 >nul 2>&1\r\n";
    script += L")\r\n";
    script += L":copy_done\r\n";
    script += L"start \"\" /D \"%APP_ROOT%\" \"%HOST_EXE%\"\r\n";
    script += L"ping 127.0.0.1 -n 2 -w 500 >nul 2>&1\r\n";
    script += L"rmdir /S /Q \"%UPDATE_ROOT%\" >nul 2>&1\r\n";
    return script;
}

bool AppUpdater::write_utf8_script(const std::filesystem::path& script_path,
                                   std::wstring_view script_content, std::string* error_out) {
    std::ofstream script_out(script_path, std::ios::binary | std::ios::trunc);
    if (!script_out.is_open()) {
        if (error_out != nullptr) {
            *error_out = "Failed to write update helper script";
        }
        return false;
    }
    if (!script_content.empty()) {
        const int utf8_len = WideCharToMultiByte(CP_UTF8, 0, script_content.data(),
                                                 static_cast<int>(script_content.size()), nullptr,
                                                 0, nullptr, nullptr);
        if (utf8_len > 0) {
            std::string utf8_script(static_cast<std::size_t>(utf8_len), '\0');
            WideCharToMultiByte(CP_UTF8, 0, script_content.data(),
                                static_cast<int>(script_content.size()), utf8_script.data(),
                                utf8_len, nullptr, nullptr);
            script_out.write(utf8_script.data(), static_cast<std::streamsize>(utf8_script.size()));
        }
    }
    script_out.flush();
    if (!script_out.good()) {
        if (error_out != nullptr) {
            *error_out = "Failed to flush update helper script";
        }
        return false;
    }
    return true;
}

bool AppUpdater::launch_detached_update_script(const std::filesystem::path& script_path,
                                               std::string* error_out) {
    std::wstring cmd = L"cmd.exe /D /C \"\"" + script_path.wstring() + L"\"\"";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> mutable_cmd(cmd.begin(), cmd.end());
    mutable_cmd.push_back(L'\0');

    const std::filesystem::path work_dir = script_path.parent_path();
    const wchar_t* work_dir_ptr = work_dir.empty() ? nullptr : work_dir.c_str();

    if (CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, nullptr, work_dir_ptr, &si,
                       &pi) == FALSE) {
        if (error_out != nullptr) {
            *error_out =
                "Failed to spawn update installer script (win32=" + std::to_string(GetLastError()) +
                ")";
        }
        return false;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

std::wstring AppUpdater::normalize_winhttp_proxy_server(std::wstring_view raw_proxy) {
    std::wstring trimmed = TrimWide(raw_proxy);
    if (trimmed.empty()) {
        return {};
    }

    // Reject SOCKS-only proxy URLs that WinHTTP HTTP/HTTPS CONNECT does not speak natively
    if (trimmed.starts_with(L"socks") || trimmed.starts_with(L"SOCKS")) {
        return {};
    }

    // Handle per-scheme WinINET proxy strings like "http=127.0.0.1:7890;https=127.0.0.1:7890"
    if (trimmed.find(L'=') != std::wstring::npos) {
        std::wstring selected;
        std::size_t pos = 0;
        while (pos < trimmed.size()) {
            const std::size_t next = trimmed.find(L';', pos);
            const std::wstring_view token =
                (next == std::wstring::npos) ? std::wstring_view(trimmed).substr(pos)
                                             : std::wstring_view(trimmed).substr(pos, next - pos);
            const std::size_t eq = token.find(L'=');
            if (eq != std::wstring_view::npos) {
                const std::wstring scheme = TrimWide(token.substr(0, eq));
                const std::wstring value = TrimWide(token.substr(eq + 1U));
                if ((scheme == L"https" || scheme == L"HTTPS") && !value.empty()) {
                    selected = value;
                    break;
                }
                if ((scheme == L"http" || scheme == L"HTTP") && selected.empty() &&
                    !value.empty()) {
                    selected = value;
                }
            }
            if (next == std::wstring::npos) {
                break;
            }
            pos = next + 1U;
        }
        if (!selected.empty()) {
            trimmed = std::move(selected);
        }
    }

    for (std::wstring_view prefix : {L"http://", L"HTTP://", L"https://", L"HTTPS://"}) {
        if (trimmed.starts_with(prefix)) {
            trimmed.erase(0, prefix.size());
            break;
        }
    }

    // Strip any credentials prefix user:pass@
    if (const auto at_pos = trimmed.rfind(L'@'); at_pos != std::wstring::npos) {
        trimmed.erase(0, at_pos + 1U);
    }

    // Strip any trailing path slash
    if (const auto slash_pos = trimmed.find(L'/'); slash_pos != std::wstring::npos) {
        trimmed.erase(slash_pos);
    }

    trimmed = TrimWide(trimmed);
    if (trimmed.empty() || trimmed.find(L':') == std::wstring::npos) {
        return {};
    }
    return trimmed;
}

std::vector<HttpAccessCandidate> AppUpdater::build_http_access_candidates(
    const HttpProxyConfigSources& sources) {
    std::vector<HttpAccessCandidate> candidates;
    candidates.push_back(HttpAccessCandidate{
        .mode = HttpAccessMode::AutomaticProxy,
        .proxy_server = {},
    });

    auto add_named_proxy = [&candidates](std::wstring_view raw) {
        std::wstring normalized = normalize_winhttp_proxy_server(raw);
        if (normalized.empty()) {
            return;
        }
        for (const auto& existing : candidates) {
            if (existing.mode == HttpAccessMode::NamedProxy &&
                existing.proxy_server == normalized) {
                return;
            }
        }
        candidates.push_back(HttpAccessCandidate{
            .mode = HttpAccessMode::NamedProxy,
            .proxy_server = std::move(normalized),
        });
    };

    add_named_proxy(sources.env_proxy);
    add_named_proxy(sources.registry_proxy);
    if (!sources.registry_proxy_enabled && candidates.size() == 1U) {
        // Common local HTTP proxy fallback ports (Clash / Mihomo / v2rayN) when no explicit
        // ProxyServer string was saved in registry
        add_named_proxy(L"127.0.0.1:7890");
        add_named_proxy(L"127.0.0.1:7897");
        add_named_proxy(L"127.0.0.1:10809");
    }

    // Always append explicit Direct (WINHTTP_ACCESS_TYPE_NO_PROXY) as final fallback in case
    // system/named proxy is unreachable or broken
    candidates.push_back(HttpAccessCandidate{
        .mode = HttpAccessMode::NoProxy,
        .proxy_server = {},
    });
    return candidates;
}

void AppUpdater::update_state(const bridge::UpdateStatusDto& next_status) {
    StatusCallback cb;
    {
        std::scoped_lock lock(mutex_);
        status_ = next_status;
        cb = status_callback_;
    }
    if (cb) {
        cb(next_status);
    }
}

void AppUpdater::log_diagnostic(bool is_error, std::string_view message) const noexcept {
    try {
        DiagnosticCallback cb;
        {
            std::scoped_lock lock(mutex_);
            cb = diagnostic_callback_;
        }
        if (cb) {
            cb(is_error, message);
        }
    } catch (...) {
    }
}

void AppUpdater::check_for_updates_async() {
    bool expected = false;
    if (!worker_busy_.compare_exchange_strong(expected, true)) {
        return;
    }
    stop_requested_.store(false);
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    worker_thread_ = std::thread([this]() noexcept {
        try {
            run_check_worker();
        } catch (...) {
        }
        worker_busy_.store(false);
    });
}

void AppUpdater::download_and_apply_async() {
    std::optional<ReleaseAssetInfo> target_release;
    {
        std::scoped_lock lock(mutex_);
        target_release = latest_release_;
    }
    if (!target_release.has_value() || target_release->download_url.empty()) {
        bridge::UpdateStatusDto err_status = status();
        err_status.state = "Error";
        err_status.error_message = "No available release package to download";
        update_state(err_status);
        return;
    }

    bool expected = false;
    if (!worker_busy_.compare_exchange_strong(expected, true)) {
        return;
    }
    stop_requested_.store(false);
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    worker_thread_ = std::thread([this, rel = std::move(*target_release)]() noexcept {
        try {
            run_download_and_apply_worker(rel);
        } catch (...) {
        }
        worker_busy_.store(false);
    });
}

void AppUpdater::run_check_worker() {
    bridge::UpdateStatusDto checking{};
    checking.state = "Checking";
    checking.current_version = options_.current_version;
    update_state(checking);

    const std::string api_url =
        "https://api.github.com/repos/" + options_.github_repo + "/releases/latest";
    std::string response_json;
    std::string error_msg;

    if (!HttpGetBuffer(api_url, &response_json, &error_msg)) {
        log_diagnostic(true, "Update check failed: " + error_msg);
        bridge::UpdateStatusDto err{};
        err.state = "Error";
        err.current_version = options_.current_version;
        err.error_message = error_msg;
        update_state(err);
        return;
    }

    auto release_info = parse_github_release_json(response_json, &error_msg);
    if (!release_info.has_value()) {
        log_diagnostic(true, "Update check parse failed: " + error_msg);
        bridge::UpdateStatusDto err{};
        err.state = "Error";
        err.current_version = options_.current_version;
        err.error_message = error_msg;
        update_state(err);
        return;
    }

    {
        std::scoped_lock lock(mutex_);
        latest_release_ = *release_info;
    }

    bridge::UpdateStatusDto next{};
    next.current_version = options_.current_version;
    next.latest_version = release_info->version;
    if (is_newer_version(options_.current_version, release_info->version)) {
        next.state = "UpdateAvailable";
        log_diagnostic(false, "Update available: v" + release_info->version + " (current v" +
                                  options_.current_version + ")");
    } else {
        next.state = "UpToDate";
        log_diagnostic(false, "Application is up to date (v" + options_.current_version + ")");
    }
    update_state(next);
}

void AppUpdater::run_download_and_apply_worker(const ReleaseAssetInfo& release_info) {
    bridge::UpdateStatusDto dl_status{};
    dl_status.state = "Downloading";
    dl_status.current_version = options_.current_version;
    dl_status.latest_version = release_info.version;
    dl_status.download_progress_percent = 0;
    update_state(dl_status);

    const std::filesystem::path update_dir = options_.app_root_dir / L"data" / L"update";
    const std::filesystem::path zip_path =
        update_dir / ("NetworkFlowMonitor-" + release_info.version + "-win-x64.zip");
    const std::filesystem::path staged_dir = update_dir / L"staged";

    std::string error_msg;
    const bool downloaded = HttpDownloadFile(
        release_info.download_url, zip_path, release_info.asset_size_bytes, stop_requested_,
        [this, &dl_status](std::uint8_t pct) {
            dl_status.download_progress_percent = pct;
            update_state(dl_status);
        },
        &error_msg);

    if (!downloaded) {
        log_diagnostic(true, "Update download failed: " + error_msg);
        dl_status.state = "Error";
        dl_status.error_message = error_msg;
        update_state(dl_status);
        return;
    }

    dl_status.state = "Applying";
    dl_status.download_progress_percent = 100;
    update_state(dl_status);

    if (!ExtractZipArchiveToDirectory(zip_path, staged_dir, &error_msg)) {
        log_diagnostic(true, "Update staging failed: " + error_msg);
        dl_status.state = "Error";
        dl_status.error_message = error_msg;
        update_state(dl_status);
        return;
    }

    const std::filesystem::path script_path = update_dir / L"apply-update.cmd";
    const std::wstring script_content = build_apply_update_script(
        GetCurrentProcessId(), staged_dir, options_.app_root_dir, options_.host_executable_path);

    if (!write_utf8_script(script_path, script_content, &error_msg)) {
        log_diagnostic(true, "Update script write failed: " + error_msg);
        dl_status.state = "Error";
        dl_status.error_message = error_msg;
        update_state(dl_status);
        return;
    }

    if (!launch_detached_update_script(script_path, &error_msg)) {
        log_diagnostic(true, "Failed to launch update script: " + error_msg);
        dl_status.state = "Error";
        dl_status.error_message = error_msg;
        update_state(dl_status);
        return;
    }

    log_diagnostic(false,
                   "Applying update v" + release_info.version + " and restarting application");

    ShutdownRequestCallback shutdown_cb;
    {
        std::scoped_lock lock(mutex_);
        shutdown_cb = shutdown_callback_;
    }
    if (shutdown_cb) {
        shutdown_cb();
    }
}

} // namespace nfm::host::platform
