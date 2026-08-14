#include "network_flow_monitor/config/config_codec.h"
#include "network_flow_monitor/config/config_paths.h"
#include "network_flow_monitor/config/config_store.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

namespace {

class TestContext {
  public:
    void Check(bool condition, std::string_view name) {
        if (!condition) {
            std::cerr << "Case failed: " << name << '\n';
            ++failures_;
        }
    }

    [[nodiscard]] int failures() const noexcept {
        return failures_;
    }

  private:
    int failures_{};
};

class TestDirectory {
  public:
    TestDirectory() {
        const auto base = std::filesystem::temp_directory_path();
        const auto identity =
            std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        for (std::uint32_t attempt = 0; attempt < 100U; ++attempt) {
            path_ = base / (L"nfm-config-tests-" + identity + L"-" + std::to_wstring(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
        }
        path_.clear();
    }

    ~TestDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    TestDirectory(const TestDirectory&) = delete;
    TestDirectory& operator=(const TestDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

class UniqueHandle {
  public:
    explicit UniqueHandle(HANDLE handle) noexcept : handle_{handle} {}

    ~UniqueHandle() {
        if (valid()) {
            CloseHandle(handle_);
        }
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    [[nodiscard]] bool valid() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

  private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
};

bool WriteAll(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return stream.good();
}

std::string ReadAll(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

std::size_t EntryCount(const std::filesystem::path& directory) {
    std::size_t count{};
    for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator{directory}) {
        ++count;
    }
    return count;
}

void CheckPaths(TestContext& context) {
    context.Check(nfm::config::PortableConfigPath(LR"(C:\Tools\NetworkFlowMonitor.exe)") ==
                      LR"(C:\Tools\NetworkFlowMonitor.json)",
                  "portable config path");
    context.Check(nfm::config::InstalledUserConfigPath(LR"(C:\Users\Test\AppData\Local)") ==
                      LR"(C:\Users\Test\AppData\Local\NetworkFlowMonitor\config.json)",
                  "installed user config path");
    context.Check(nfm::config::MachineServiceConfigPath(LR"(C:\ProgramData)") ==
                      LR"(C:\ProgramData\NetworkFlowMonitor\service.json)",
                  "machine service config path");
}

void CheckLoading(TestContext& context, const TestDirectory& directory) {
    const auto missing = directory.path() / L"missing.json";
    const auto missing_result =
        nfm::config::LoadConfigFile(missing, nfm::config::ConfigDocumentKind::User);
    context.Check(missing_result.status == nfm::config::ConfigLoadStatus::Missing,
                  "missing file status");
    context.Check(missing_result.config == nfm::config::DefaultAppConfig(),
                  "missing file returns defaults");

    const auto damaged = directory.path() / L"damaged.json";
    constexpr std::string_view damaged_text{"{ definitely not JSON"};
    context.Check(WriteAll(damaged, damaged_text), "prepare damaged file");
    const auto damaged_result =
        nfm::config::LoadConfigFile(damaged, nfm::config::ConfigDocumentKind::User);
    context.Check(damaged_result.status == nfm::config::ConfigLoadStatus::Damaged,
                  "damaged file status");
    context.Check(damaged_result.error &&
                      damaged_result.error->code == nfm::config::ConfigErrorCode::ParseFailed,
                  "damaged file parse error");
    context.Check(ReadAll(damaged) == damaged_text, "damaged load preserves original file");

    const auto development = directory.path() / L"development.json";
    constexpr std::string_view development_text{
        R"json({"schema_version":1,"floating":{"snap_distance_dip":20,"reveal_strip_dip":6}})json"};
    context.Check(WriteAll(development, development_text), "prepare development config");
    const auto development_result =
        nfm::config::LoadConfigFile(development, nfm::config::ConfigDocumentKind::User);
    context.Check(development_result.status == nfm::config::ConfigLoadStatus::Loaded,
                  "recognized development config is replaced");
    context.Check(development_result.config == nfm::config::DefaultAppConfig(),
                  "development replacement uses current defaults");
    const auto replaced_text = ReadAll(development);
    context.Check(replaced_text.find("\"schema_version\": 2") != std::string::npos,
                  "development replacement writes current schema");
    context.Check(replaced_text.find("snap_distance_dip") == std::string::npos &&
                      replaced_text.find("reveal_strip_dip") == std::string::npos,
                  "development replacement removes obsolete concepts");

    const auto oversized = directory.path() / L"oversized.json";
    const std::string oversized_text(1024U * 1024U + 1U, 'x');
    context.Check(WriteAll(oversized, oversized_text), "prepare oversized file");
    const auto oversized_result =
        nfm::config::LoadConfigFile(oversized, nfm::config::ConfigDocumentKind::User);
    context.Check(oversized_result.status == nfm::config::ConfigLoadStatus::Damaged,
                  "oversized file status");
    context.Check(oversized_result.error &&
                      oversized_result.error->code == nfm::config::ConfigErrorCode::FileTooLarge,
                  "oversized file error");
}

void CheckSaving(TestContext& context, const TestDirectory& directory) {
    const auto target = directory.path() / L"config.json";
    const auto entries_before = EntryCount(directory.path());
    auto config = nfm::config::DefaultAppConfig();
    config.ranking.top_n = 8U;

    const auto first =
        nfm::config::SaveConfigFile(target, config, nfm::config::ConfigDocumentKind::User);
    context.Check(first.saved, "first save succeeds");
    context.Check(!first.error, "first save has no error");
    const auto first_load =
        nfm::config::LoadConfigFile(target, nfm::config::ConfigDocumentKind::User);
    context.Check(first_load.status == nfm::config::ConfigLoadStatus::Loaded,
                  "first save creates complete document");
    context.Check(first_load.config.ranking.top_n == 8U, "first save persists value");
    context.Check(EntryCount(directory.path()) == entries_before + 1U,
                  "first save leaves no temporary sibling");

    config.ranking.top_n = 11U;
    const auto replacement =
        nfm::config::SaveConfigFile(target, config, nfm::config::ConfigDocumentKind::User);
    context.Check(replacement.saved, "replacement save succeeds");
    const auto replacement_load =
        nfm::config::LoadConfigFile(target, nfm::config::ConfigDocumentKind::User);
    context.Check(replacement_load.config.ranking.top_n == 11U, "replacement save persists value");
    context.Check(EntryCount(directory.path()) == entries_before + 1U,
                  "replacement leaves no temporary sibling");
}

void CheckLockedReplacement(TestContext& context, const TestDirectory& directory) {
    const auto target = directory.path() / L"locked.json";
    const auto initial = nfm::config::SerializeConfigJson(nfm::config::DefaultAppConfig(),
                                                          nfm::config::ConfigDocumentKind::User);
    context.Check(initial.ok() && WriteAll(target, initial.json), "prepare locked target");
    const auto original_text = ReadAll(target);
    const auto entries_before = EntryCount(directory.path());

    const UniqueHandle lock{CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    context.Check(lock.valid(), "open replacement lock");

    auto replacement = nfm::config::DefaultAppConfig();
    replacement.ranking.top_n = 9U;
    const auto result =
        nfm::config::SaveConfigFile(target, replacement, nfm::config::ConfigDocumentKind::User);
    context.Check(!result.saved, "locked target rejects replacement");
    context.Check(result.error && result.error->code == nfm::config::ConfigErrorCode::ReplaceFailed,
                  "locked target reports replacement error");
    context.Check(result.error && result.error->system_error != 0U,
                  "locked target exposes Win32 error");
    context.Check(ReadAll(target) == original_text, "failed replacement preserves original");
    context.Check(EntryCount(directory.path()) == entries_before,
                  "failed replacement removes temporary sibling");
}

} // namespace

int main() {
    TestContext context;
    const TestDirectory directory;
    context.Check(!directory.path().empty(), "create unique test directory");
    if (!directory.path().empty()) {
        CheckPaths(context);
        CheckLoading(context, directory);
        CheckSaving(context, directory);
        CheckLockedReplacement(context, directory);
    }
    return context.failures();
}
