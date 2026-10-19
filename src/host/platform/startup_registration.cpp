#include "network_flow_monitor/host/platform/startup_registration.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <cwctype>
#include <shlobj.h>
#include <system_error>
#include <vector>

namespace nfm::host::platform {

namespace {

std::wstring ToLowerWide(std::wstring_view input) {
    std::wstring result;
    result.reserve(input.size());
    for (wchar_t ch : input) {
        result.push_back(static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch))));
    }
    return result;
}

bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::towlower(static_cast<wint_t>(a[i])) != std::towlower(static_cast<wint_t>(b[i]))) {
            return false;
        }
    }
    return true;
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

std::wstring TrimWhitespace(std::wstring_view text) {
    std::size_t first = 0;
    while (first < text.size() && std::iswspace(static_cast<wint_t>(text[first])) != 0) {
        ++first;
    }
    std::size_t last = text.size();
    while (last > first && std::iswspace(static_cast<wint_t>(text[last - 1])) != 0) {
        --last;
    }
    return std::wstring(text.substr(first, last - first));
}

class ScopedRegKey {
  public:
    ScopedRegKey() noexcept = default;
    explicit ScopedRegKey(HKEY key) noexcept : key_(key) {}
    ~ScopedRegKey() {
        if (key_ != nullptr) {
            RegCloseKey(key_);
        }
    }
    ScopedRegKey(const ScopedRegKey&) = delete;
    ScopedRegKey& operator=(const ScopedRegKey&) = delete;

    [[nodiscard]] HKEY get() const noexcept {
        return key_;
    }

  private:
    HKEY key_{nullptr};
};

std::filesystem::path ResolveStartupFolderPath(const StartupRegistrationOptions& options) {
    if (!options.startup_folder_override.empty()) {
        return options.startup_folder_override;
    }
    if (options.registry_subkey != kDefaultStartupRegistrySubKey) {
        // When operating against an isolated test registry subkey without an explicit
        // startup_folder_override, do not touch the user's real Startup folder.
        return {};
    }
    std::array<wchar_t, MAX_PATH> folder{};
    if (SUCCEEDED(
            SHGetFolderPathW(nullptr, CSIDL_STARTUP, nullptr, SHGFP_TYPE_CURRENT, folder.data()))) {
        return std::filesystem::path(folder.data());
    }
    return {};
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::size_t RemoveMatchingStartupFolderLinks(const std::filesystem::path& startup_folder,
                                             const std::filesystem::path& current_exe,
                                             std::wstring_view app_identity_name) {
    if (startup_folder.empty()) {
        return 0;
    }
    const DWORD folder_attrs = GetFileAttributesW(startup_folder.c_str());
    if (folder_attrs == INVALID_FILE_ATTRIBUTES ||
        (folder_attrs & FILE_ATTRIBUTE_DIRECTORY) == 0U) {
        return 0;
    }

    const std::wstring exe_stem_lower = ToLowerWide(current_exe.stem().wstring());
    const std::wstring exe_name_lower = ToLowerWide(current_exe.filename().wstring());
    const std::wstring identity_lower = ToLowerWide(app_identity_name);

    const std::filesystem::path pattern = startup_folder / L"*";
    WIN32_FIND_DATAW find_data{};
    HANDLE find_handle = FindFirstFileW(pattern.c_str(), &find_data);
    if (find_handle == INVALID_HANDLE_VALUE) {
        return 0;
    }

    std::size_t removed = 0;
    do {
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
            continue;
        }
        const std::filesystem::path file_path = startup_folder / find_data.cFileName;
        const std::wstring ext_lower = ToLowerWide(file_path.extension().wstring());
        if (ext_lower != L".lnk" && ext_lower != L".url" && ext_lower != L".bat" &&
            ext_lower != L".cmd") {
            continue;
        }
        const std::wstring stem_lower = ToLowerWide(file_path.stem().wstring());
        if (stem_lower == identity_lower ||
            (!exe_stem_lower.empty() && stem_lower == exe_stem_lower) ||
            (!exe_name_lower.empty() && stem_lower == exe_name_lower)) {
            if (DeleteFileW(file_path.c_str()) != FALSE) {
                ++removed;
            }
        }
    } while (FindNextFileW(find_handle, &find_data) != FALSE);

    FindClose(find_handle);
    return removed;
}

} // namespace

std::filesystem::path StartupRegistration::current_executable_path() {
    std::vector<wchar_t> buffer(MAX_PATH, L'\0');
    while (true) {
        const DWORD copied =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (copied == 0) {
            return {};
        }
        if (copied < buffer.size() - 1) {
            return std::filesystem::path(std::wstring(buffer.data(), copied));
        }
        if (buffer.size() >= 32768U) {
            return {};
        }
        buffer.resize(buffer.size() * 2U, L'\0');
    }
}

std::wstring StartupRegistration::extract_executable_path_from_command(
    std::wstring_view command_line) {
    const std::wstring trimmed = TrimWhitespace(command_line);
    if (trimmed.empty()) {
        return {};
    }

    if (trimmed.front() == L'"') {
        const std::size_t closing_quote = trimmed.find(L'"', 1);
        if (closing_quote != std::wstring::npos) {
            return TrimWhitespace(std::wstring_view(trimmed).substr(1, closing_quote - 1));
        }
        return TrimWhitespace(std::wstring_view(trimmed).substr(1));
    }

    const std::wstring lower = ToLowerWide(trimmed);
    const std::size_t exe_pos = lower.find(L".exe");
    if (exe_pos != std::wstring::npos) {
        const std::size_t end_pos = exe_pos + 4U;
        if (end_pos == trimmed.size() ||
            std::iswspace(static_cast<wint_t>(trimmed[end_pos])) != 0 || trimmed[end_pos] == L'"') {
            return TrimWhitespace(std::wstring_view(trimmed).substr(0, end_pos));
        }
    }

    std::size_t space_pos = 0;
    while (space_pos < trimmed.size() &&
           std::iswspace(static_cast<wint_t>(trimmed[space_pos])) == 0) {
        ++space_pos;
    }
    return trimmed.substr(0, space_pos);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool StartupRegistration::is_matching_application_entry(std::wstring_view value_name,
                                                        std::wstring_view command_line,
                                                        const std::filesystem::path& current_exe,
                                                        std::wstring_view app_identity_name) {
    const std::wstring trimmed_name = TrimWhitespace(value_name);
    const std::wstring exe_filename = current_exe.filename().wstring();
    const std::wstring exe_stem = current_exe.stem().wstring();

    if (!app_identity_name.empty() && EqualsIgnoreCase(trimmed_name, app_identity_name)) {
        return true;
    }
    if (!exe_stem.empty() && EqualsIgnoreCase(trimmed_name, exe_stem)) {
        return true;
    }
    if (!exe_filename.empty() && EqualsIgnoreCase(trimmed_name, exe_filename)) {
        return true;
    }

    const std::wstring extracted_path_str = extract_executable_path_from_command(command_line);
    if (extracted_path_str.empty()) {
        return false;
    }

    const std::filesystem::path extracted_path(extracted_path_str);
    const std::wstring extracted_filename = extracted_path.filename().wstring();
    const std::wstring extracted_stem = extracted_path.stem().wstring();

    if (!exe_filename.empty() && EqualsIgnoreCase(extracted_filename, exe_filename)) {
        return true;
    }
    if (!exe_stem.empty() && EqualsIgnoreCase(extracted_stem, exe_stem)) {
        return true;
    }
    if (!app_identity_name.empty() &&
        (EqualsIgnoreCase(extracted_stem, app_identity_name) ||
         EqualsIgnoreCase(extracted_filename, std::wstring(app_identity_name) + L".exe"))) {
        return true;
    }

    return false;
}

StartupRegistrationResult StartupRegistration::sync_login_startup(
    bool enabled, const std::filesystem::path& current_exe,
    const StartupRegistrationOptions& options) {
    StartupRegistrationResult result{};

    if (enabled && current_exe.empty()) {
        result.win32_error = ERROR_INVALID_PARAMETER;
        result.diagnostic_summary = "Cannot enable login startup: executable path is empty";
        return result;
    }

    HKEY raw_key = nullptr;
    DWORD disposition = 0;
    const LSTATUS open_status = RegCreateKeyExW(
        HKEY_CURRENT_USER, options.registry_subkey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
        KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &raw_key, &disposition);
    if (open_status != ERROR_SUCCESS) {
        result.win32_error = static_cast<std::uint32_t>(open_status);
        result.diagnostic_summary = "RegCreateKeyExW failed for Run key (win32_error=" +
                                    std::to_string(result.win32_error) + ")";
        return result;
    }
    const ScopedRegKey run_key(raw_key);

    // 1. Enumerate all values in the Run key and collect any entry matching this application
    //    regardless of value name or target directory path.
    std::vector<std::wstring> values_to_delete;
    DWORD index = 0;
    std::vector<wchar_t> name_buf(16384U, L'\0');
    std::vector<BYTE> data_buf(65536U, 0);

    while (true) {
        auto name_len = static_cast<DWORD>(name_buf.size());
        auto data_len = static_cast<DWORD>(data_buf.size());
        DWORD value_type = 0;
        const LSTATUS enum_status = RegEnumValueW(run_key.get(), index, name_buf.data(), &name_len,
                                                  nullptr, &value_type, data_buf.data(), &data_len);
        if (enum_status == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (enum_status != ERROR_SUCCESS) {
            ++index;
            continue;
        }

        const std::wstring_view value_name(name_buf.data(), name_len);
        std::wstring value_data;
        if ((value_type == REG_SZ || value_type == REG_EXPAND_SZ) && data_len >= sizeof(wchar_t)) {
            const auto* wide_ptr = reinterpret_cast<const wchar_t*>(data_buf.data());
            std::size_t char_count = data_len / sizeof(wchar_t);
            while (char_count > 0 && wide_ptr[char_count - 1] == L'\0') {
                --char_count;
            }
            value_data.assign(wide_ptr, char_count);
        }

        if (is_matching_application_entry(value_name, value_data, current_exe,
                                          options.app_identity_name)) {
            values_to_delete.emplace_back(value_name);
        }
        ++index;
    }

    for (const auto& value_name : values_to_delete) {
        const LSTATUS del_status = RegDeleteValueW(run_key.get(), value_name.c_str());
        if (del_status == ERROR_SUCCESS) {
            ++result.removed_entries_count;
        } else if (del_status != ERROR_FILE_NOT_FOUND) {
            result.win32_error = static_cast<std::uint32_t>(del_status);
            result.diagnostic_summary = "RegDeleteValueW failed for '" + WideToUtf8(value_name) +
                                        "' (win32_error=" + std::to_string(result.win32_error) +
                                        ")";
            return result;
        }
    }

    // Also clean up any matching shortcut in the user Startup folder
    const auto startup_folder = ResolveStartupFolderPath(options);
    result.removed_entries_count +=
        RemoveMatchingStartupFolderLinks(startup_folder, current_exe, options.app_identity_name);

    if (!enabled) {
        result.success = true;
        result.diagnostic_summary = "Login startup disabled (removed_entries=" +
                                    std::to_string(result.removed_entries_count) + ")";
        return result;
    }

    // 2. Register the currently running executable with quotes
    std::error_code abs_ec;
    std::filesystem::path normalized_exe = std::filesystem::absolute(current_exe, abs_ec);
    if (abs_ec || normalized_exe.empty()) {
        normalized_exe = current_exe;
    }
    const std::wstring quoted_command = L"\"" + normalized_exe.wstring() + L"\"";
    const auto byte_size = static_cast<DWORD>((quoted_command.size() + 1U) * sizeof(wchar_t));
    const LSTATUS set_status =
        RegSetValueExW(run_key.get(), options.app_identity_name.c_str(), 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(quoted_command.c_str()), byte_size);
    if (set_status != ERROR_SUCCESS) {
        result.win32_error = static_cast<std::uint32_t>(set_status);
        result.diagnostic_summary = "RegSetValueExW failed for '" +
                                    WideToUtf8(options.app_identity_name) +
                                    "' (win32_error=" + std::to_string(result.win32_error) + ")";
        return result;
    }

    result.success = true;
    result.diagnostic_summary =
        "Login startup enabled -> " + WideToUtf8(quoted_command) +
        " (cleaned_previous=" + std::to_string(result.removed_entries_count) + ")";
    return result;
}

bool StartupRegistration::is_registered_for_executable(const std::filesystem::path& current_exe,
                                                       const StartupRegistrationOptions& options) {
    if (current_exe.empty()) {
        return false;
    }

    HKEY raw_key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, options.registry_subkey.c_str(), 0, KEY_QUERY_VALUE,
                      &raw_key) != ERROR_SUCCESS) {
        return false;
    }
    const ScopedRegKey run_key(raw_key);

    std::array<wchar_t, 2048> data_buf{};
    auto data_bytes = static_cast<DWORD>(data_buf.size() * sizeof(wchar_t));
    DWORD value_type = 0;
    const LSTATUS query_status =
        RegQueryValueExW(run_key.get(), options.app_identity_name.c_str(), nullptr, &value_type,
                         reinterpret_cast<LPBYTE>(data_buf.data()), &data_bytes);
    if (query_status != ERROR_SUCCESS || (value_type != REG_SZ && value_type != REG_EXPAND_SZ)) {
        return false;
    }

    std::size_t char_count = data_bytes / sizeof(wchar_t);
    while (char_count > 0 && data_buf[char_count - 1] == L'\0') {
        --char_count;
    }
    const std::wstring registered_cmd(data_buf.data(), char_count);
    const std::wstring extracted = extract_executable_path_from_command(registered_cmd);
    if (extracted.empty()) {
        return false;
    }

    return EqualsIgnoreCase(current_exe.lexically_normal().wstring(),
                            std::filesystem::path(extracted).lexically_normal().wstring());
}

} // namespace nfm::host::platform
