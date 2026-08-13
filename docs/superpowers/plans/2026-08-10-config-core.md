# Configuration Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the complete schema-1 `AppConfig`, strict yyjson codec, path policy, and crash-resistant Windows file persistence with exhaustive tests.

**Architecture:** Public headers expose only project-owned C++20 value/result types. A private codec translation unit owns yyjson through RAII, while a separate Windows file store handles path I/O and atomic replacement. Third-party C sources are pinned and isolated from first-party warning and analysis targets without weakening those targets.

**Tech Stack:** C++20, CMake 3.28+, MSVC x64, CTest, Win32 file APIs, yyjson 0.12.0.

## Global Constraints

- Target Windows 11 24H2 x64; do not add a compatibility path for older Windows.
- Vendor exactly yyjson 0.12.0; do not use FetchContent, a package manager, a runtime DLL, JSON5, comments, trailing commas, or extended numbers.
- Public headers must not include `yyjson.h` or expose Win32 UI types.
- All public time values use `std::chrono::milliseconds`; no unitless time integer crosses a public interface.
- User and service JSON documents have explicit, disjoint allowed groups except for their separate `diagnostics` values; never serialize the whole composite model into both paths.
- Missing ordinary schema-1 fields receive defaults; unknown keys, duplicate keys, wrong types, invalid UTF-8, invalid ranges, schema 0, and future schemas produce diagnostic statuses and never overwrite the source file.
- First-party code must pass format, strict Debug, clang-tidy/Clang Static Analyzer, MSVC `/analyze`, all tests, and Release x64 through `tools/quality.ps1`.
- Git commits in this plan are conditional on explicit user authorization at execution time. If authorization is absent, preserve the verified working tree and record the proposed commit instead.

---

### Task 1: Vendor yyjson behind an isolated CMake target

**Files:**
- Create: `third_party/yyjson/yyjson.h`
- Create: `third_party/yyjson/yyjson.c`
- Create: `third_party/yyjson/LICENSE`
- Create: `third_party/yyjson/README.md`
- Create: `third_party/yyjson/CMakeLists.txt`
- Create: `cmake/dependencies.h.in`
- Modify: `CMakeLists.txt`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/bootstrap_tests.cpp`

**Interfaces:**
- Consumes: upstream release tag `0.12.0` from `https://github.com/ibireme/yyjson`.
- Produces: private static target `nfm_yyjson`; no yyjson usage requirement propagates beyond `nfm_config`.

- [x] **Step 1: Add a bootstrap test for the pinned dependency identity**

Extend `tests/bootstrap_tests.cpp` to include a generated project version header and verify constants equivalent to:

```cpp
if (std::string_view{NFM_YYJSON_VERSION} != "0.12.0") {
    std::cerr << "Unexpected yyjson version\n";
    ++failures;
}
```

Generate the macro from CMake rather than including `yyjson.h` in this first-party smoke test.

- [x] **Step 2: Run the focused test build and verify the red phase**

Run:

```powershell
pwsh -NoProfile -File tools/quality.ps1 -Mode Fast
```

Expected: configuration or compilation fails because `NFM_YYJSON_VERSION` and the generated header do not exist; no unrelated existing test fails first.

- [x] **Step 3: Download the three approved upstream files and verify their SHA-256 values**

Use the immutable tag URLs and reject any mismatch:

```text
src/yyjson.h  175867c5493a5df648cec566717fa1c29aa2f6096f5f0cf1efad0b65e1f6d7b3
src/yyjson.c  ac2e9bbb2e2d9149d90878d40506a1d624fa0b33c979a11b61075c54782c6d6a
LICENSE       45e384d3d52c73cba3a64d6e6c25d47cd738cd8a55c30629e3201046eda62947
```

Store the two source files at `third_party/yyjson/yyjson.h` and `third_party/yyjson/yyjson.c`. Record the upstream URL, tag, retrieval date, the three hashes, and MIT license path in `third_party/yyjson/README.md`.

- [x] **Step 4: Add the isolated C target and generated version header**

Change the root project languages to `LANGUAGES C CXX`, add `add_subdirectory(third_party/yyjson)`, and define the third-party target as:

```cmake
add_library(nfm_yyjson STATIC yyjson.c)
target_include_directories(nfm_yyjson PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(nfm_yyjson PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
if(MSVC)
    target_compile_options(nfm_yyjson PRIVATE /W0)
endif()
```

Generate `${PROJECT_BINARY_DIR}/generated/network_flow_monitor/build/dependencies.h` from `cmake/dependencies.h.in` with `NFM_YYJSON_VERSION "0.12.0"`, and add `${PROJECT_BINARY_DIR}/generated` only to targets that include it. Do not call `nfm_configure_target()` or either static-analysis helper for `nfm_yyjson`. The first-party `nfm_config` target is created with real source files in Task 2.

- [x] **Step 5: Run the bootstrap test and inspect the link surface**

Run:

```powershell
pwsh -NoProfile -File tools/quality.ps1 -Mode Fast
dumpbin /dependents build\windows-x64-debug\src\NetworkFlowMonitor.exe
```

Expected: all tests pass, `nfm_yyjson` is built statically, and no yyjson DLL appears in dependents.

- [x] **Step 6: Create the optional verified checkpoint**

After a Full gate and progress log entry, if commit authorization exists:

```powershell
git add CMakeLists.txt src/CMakeLists.txt tests/bootstrap_tests.cpp third_party/yyjson
git commit -m "build: vendor pinned yyjson dependency"
```

Without authorization, record `build: vendor pinned yyjson dependency` as the proposed checkpoint and do not commit.

---

### Task 2: Implement the typed `AppConfig` and validation contract

**Files:**
- Create: `include/network_flow_monitor/config/app_config.h`
- Create: `include/network_flow_monitor/config/config_validation.h`
- Create: `src/config/app_config.cpp`
- Create: `src/config/config_validation.cpp`
- Create: `tests/config_model_tests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the exact defaults/ranges in `docs/configuration.md`.
- Produces: `DefaultAppConfig()`, `ValidateAppConfig()`, schema/range constants, and the complete public config value model.

- [x] **Step 1: Write model tests for every default and enum**

Create `tests/config_model_tests.cpp` with table-driven checks covering all 17 public keys. The central assertion shape is:

```cpp
const auto config = nfm::config::DefaultAppConfig();
Check(config.schema_version == nfm::config::kCurrentSchemaVersion, "schema version");
Check(config.ranking.top_n == 5U, "default top N");
Check(config.ranking.window == std::chrono::milliseconds{2000}, "default window");
Check(config.display.refresh_interval == std::chrono::milliseconds{500}, "default refresh");
```

Continue with every value from `docs/configuration.md`, including the four enums and all booleans.

- [x] **Step 2: Run the new test target and verify the red phase**

Add `nfm_config_model_tests` to `tests/CMakeLists.txt`, link it to `nfm_config`, and register `config.model`. Run:

```powershell
cmake --preset windows-x64-debug
cmake --build --preset windows-x64-debug --target nfm_config_model_tests
```

Expected: compilation fails because the config headers and functions do not exist.

- [x] **Step 3: Define the public model exactly once**

Implement these public types in `app_config.h` using fixed underlying enum types and defaulted equality:

```cpp
enum class SpeedUnit : std::uint8_t { Auto, Bytes, Bits };
enum class CollectorMode : std::uint8_t { Portable, Service };
enum class ServiceStartType : std::uint8_t { Manual, Automatic, AutomaticDelayed };
enum class LogLevel : std::uint8_t { Off, Error, Info, Debug };

struct RankingConfig { std::uint8_t top_n; std::chrono::milliseconds window; bool operator==(const RankingConfig&) const = default; };
struct DisplayConfig { std::chrono::milliseconds refresh_interval; SpeedUnit speed_unit; bool show_upload; bool show_download; bool operator==(const DisplayConfig&) const = default; };
struct InteractionConfig { std::chrono::milliseconds hover_delay; std::chrono::milliseconds dismiss_delay; bool operator==(const InteractionConfig&) const = default; };
struct FloatingConfig { std::int32_t snap_distance_dip; bool auto_hide_enabled; std::chrono::milliseconds auto_hide_delay; std::int32_t reveal_strip_dip; std::uint8_t opacity_percent; bool operator==(const FloatingConfig&) const = default; };
struct StartupConfig { bool ui_at_login; bool operator==(const StartupConfig&) const = default; };
struct CollectorConfig { CollectorMode preferred_mode; bool operator==(const CollectorConfig&) const = default; };
struct ServiceConfig { ServiceStartType start_type; bool operator==(const ServiceConfig&) const = default; };
struct DiagnosticsConfig { LogLevel log_level; bool operator==(const DiagnosticsConfig&) const = default; };
struct AppConfig { std::uint32_t schema_version; RankingConfig ranking; DisplayConfig display; InteractionConfig interaction; FloatingConfig floating; StartupConfig startup; CollectorConfig collector; ServiceConfig service; DiagnosticsConfig diagnostics; bool operator==(const AppConfig&) const = default; };
```

Declare `inline constexpr std::uint32_t kCurrentSchemaVersion = 1U;` and named minimum/maximum constants for every numeric field. Implement `DefaultAppConfig()` without reading environment or files.

- [x] **Step 4: Add exhaustive validation tests**

For every ranged field, test minimum, maximum, below-minimum, and above-maximum. Verify the result contains stable paths such as `ranking.top_n` and error codes. Do not manufacture out-of-domain enum objects with `static_cast`; strict string-to-enum rejection is covered in Task 3 before an enum value is constructed.

```cpp
enum class ConfigIssueCode : std::uint8_t { OutOfRange, InvalidEnum };
struct ConfigIssue { ConfigIssueCode code; std::string field_path; bool operator==(const ConfigIssue&) const = default; };
struct ConfigValidationResult { std::vector<ConfigIssue> issues; [[nodiscard]] bool ok() const noexcept; };
```

Include an explicit test proving `window=500ms` with `refresh_interval=5000ms` is valid.

- [x] **Step 5: Implement validation and run focused tests**

Implement `ConfigValidationResult ValidateAppConfig(const AppConfig&)` with all range constants and enum membership checks. Do not clamp or mutate input. Run:

```powershell
cmake --build --preset windows-x64-debug --target nfm_config_model_tests
ctest --test-dir build/windows-x64-debug -R config.model --output-on-failure
```

Expected: `config.model` passes with all boundary cases.

In `src/CMakeLists.txt`, create `nfm_config` from `config/app_config.cpp` and `config/config_validation.cpp`, expose `${PROJECT_SOURCE_DIR}/include`, apply both first-party configuration/analysis helpers, and link `nfm_yyjson` privately so Task 3 can add the codec without changing the public surface.

- [x] **Step 6: Run the Fast gate and create the optional checkpoint**

Run `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast`. If a Full gate is also green and commit authorization exists:

```powershell
git add include/network_flow_monitor/config src/config src/CMakeLists.txt tests/CMakeLists.txt tests/config_model_tests.cpp
git commit -m "feat: add typed application configuration"
```

Otherwise leave the verified change uncommitted and record the proposed message.

---

### Task 3: Implement strict schema-1 JSON parsing and serialization

**Files:**
- Create: `include/network_flow_monitor/config/config_codec.h`
- Create: `src/config/config_codec.cpp`
- Create: `tests/config_codec_tests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `AppConfig`, `DefaultAppConfig()`, `ValidateAppConfig()`, and private `nfm_yyjson`.
- Produces: `ParseConfigJson(std::string_view)` and `SerializeConfigJson(const AppConfig&)` with project-owned result types.

- [x] **Step 1: Define codec result tests before the implementation**

Use this public contract:

```cpp
enum class ConfigLoadStatus : std::uint8_t {
    Loaded,
    LoadedWithDefaults,
    Missing,
    Damaged,
    FutureVersion,
    UnsupportedLegacyVersion,
    IoError,
};
enum class ConfigDocumentKind : std::uint8_t { User, Service };
enum class ConfigErrorCode : std::uint8_t {
    None,
    ParseFailed,
    InvalidRoot,
    MissingSchemaVersion,
    InvalidSchemaVersion,
    DuplicateKey,
    UnknownKey,
    TypeMismatch,
    InvalidEnum,
    OutOfRange,
    FileTooLarge,
    OpenFailed,
    ReadFailed,
    WriteFailed,
    ReplaceFailed,
};
struct ConfigError { ConfigErrorCode code; std::string field_path; std::string detail; std::uint32_t system_error; };
struct ConfigLoadResult { AppConfig config; ConfigLoadStatus status; std::optional<ConfigError> error; };
struct ConfigJsonResult { std::string json; std::optional<ConfigError> error; [[nodiscard]] bool ok() const noexcept; };
```

The public functions are `ParseConfigJson(std::string_view json, ConfigDocumentKind kind)` and `SerializeConfigJson(const AppConfig& config, ConfigDocumentKind kind)`. Tests must cover: `{}` missing schema, root array, malformed JSON, invalid UTF-8, duplicate nested key, unknown root/group key, a user-only group in a service document, a service-only group in a user document, wrong scalar type, schema 0, schema 2, missing ordinary field, all enum strings, every numeric narrowing boundary, and complete schema-1 user/service documents.

- [x] **Step 2: Verify the codec red phase**

Register `config.codec`, build `nfm_config_codec_tests`, and expect a compile failure for the missing codec API:

```powershell
cmake --build --preset windows-x64-debug --target nfm_config_codec_tests
```

- [x] **Step 3: Implement a private yyjson RAII adapter and structural validator**

In `config_codec.cpp`, define local deleters and owning aliases only inside the translation unit:

```cpp
struct JsonDocumentDeleter { void operator()(yyjson_doc* document) const noexcept; };
using UniqueJsonDocument = std::unique_ptr<yyjson_doc, JsonDocumentDeleter>;
struct MutableDocumentDeleter { void operator()(yyjson_mut_doc* document) const noexcept; };
using UniqueMutableDocument = std::unique_ptr<yyjson_mut_doc, MutableDocumentDeleter>;
```

Parse with flags `0`. Recursively visit each object once, store keys in a local set, and reject a duplicate before reading values. Validate every allowed key against fixed arrays for the root and each group; report the full dot path of the first deterministic error.

- [x] **Step 4: Decode schema and fields with no lossy conversion**

Read `schema_version` before ordinary fields. Map schema 0 to `UnsupportedLegacyVersion`, values above 1 to `FutureVersion`, and non-unsigned-integer values to `Damaged/InvalidSchemaVersion`. Start with `DefaultAppConfig()`, select the fixed allowed-group table for the requested document kind, overlay present fields only after exact type/range checks, then validate the fields in that scope. Set `LoadedWithDefaults` if any ordinary field for that document kind was absent; otherwise set `Loaded`.

Use exact enum strings from `docs/configuration.md`: `auto/bytes/bits`, `portable/service`, `manual/automatic/automatic_delayed`, and `off/error/info/debug`.

- [x] **Step 5: Serialize in fixed order and verify round trips**

Construct the mutable document in the key order listed in `docs/configuration.md`, including only groups allowed for the selected document kind. Write with `YYJSON_WRITE_PRETTY_TWO_SPACES`, copy the owned buffer into `std::string`, release it with `free`, and append exactly one newline if yyjson did not. Reject invalid in-memory fields for the selected scope before creating JSON.

Add assertions that `Serialize -> Parse` returns the original values for the selected scope, leaves out-of-scope values at defaults, and that two serializations are byte-identical. Run:

```powershell
ctest --test-dir build/windows-x64-debug -R config.codec --output-on-failure
```

- [x] **Step 6: Run the Fast gate and create the optional checkpoint**

Run `pwsh -NoProfile -File tools/quality.ps1 -Mode Fast`. If a Full gate and commit authorization exist:

```powershell
git add include/network_flow_monitor/config/config_codec.h src/config/config_codec.cpp src/CMakeLists.txt tests/CMakeLists.txt tests/config_codec_tests.cpp
git commit -m "feat: add strict versioned config codec"
```

Otherwise record the proposed message without committing.

---

### Task 4: Add explicit path policy and atomic Windows persistence

**Files:**
- Create: `include/network_flow_monitor/config/config_paths.h`
- Create: `include/network_flow_monitor/config/config_store.h`
- Create: `src/config/config_paths.cpp`
- Create: `src/config/config_store.cpp`
- Create: `tests/config_store_tests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ParseConfigJson()`, `SerializeConfigJson()`, and caller-supplied base paths.
- Produces: pure path composition plus `LoadConfigFile(path, kind)` and `SaveConfigFile(path, config, kind)` using explicit scope, result statuses, and Win32 error codes.

- [x] **Step 1: Write pure path-policy tests**

Define and test:

```cpp
std::filesystem::path PortableConfigPath(const std::filesystem::path& executable_path);
std::filesystem::path InstalledUserConfigPath(const std::filesystem::path& local_app_data);
std::filesystem::path MachineServiceConfigPath(const std::filesystem::path& program_data);
```

Expected suffixes are `NetworkFlowMonitor.json`, `NetworkFlowMonitor/config.json`, and `NetworkFlowMonitor/service.json`. No function probes writability or silently substitutes another base directory.

- [x] **Step 2: Write file-store tests including non-overwrite behavior**

Use a per-test directory under `std::filesystem::temp_directory_path()` with an RAII cleanup guard. Cover missing file, valid file, malformed file preservation, first save, replacement save, and a locked target handle opened without `FILE_SHARE_DELETE`:

```cpp
const HANDLE lock = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
const auto result = nfm::config::SaveConfigFile(
    target, replacement, nfm::config::ConfigDocumentKind::User);
Check(!result.saved, "locked target rejects replacement");
Check(ReadAll(target) == original_text, "failed replacement preserves original");
CloseHandle(lock);
```

Wrap the test handle in a local RAII type; do not leave raw cleanup paths.

- [x] **Step 3: Verify the store red phase**

Register `config.store`, build `nfm_config_store_tests`, and expect missing-symbol or missing-header failure:

```powershell
cmake --build --preset windows-x64-debug --target nfm_config_store_tests
```

- [x] **Step 4: Implement bounded file loading**

Set a named internal maximum config size of 1 MiB. Open with `CreateFileW`, distinguish `ERROR_FILE_NOT_FOUND`/`ERROR_PATH_NOT_FOUND` as `Missing`, reject directories and oversized files, read the complete byte sequence, and pass it with the caller's document kind to `ParseConfigJson()`. On every failure return `DefaultAppConfig()` plus a stable error code and the original Win32 code; never write from the load path.

- [x] **Step 5: Implement same-directory atomic save**

Validate and serialize the requested document kind first. Create a collision-resistant temporary sibling using `CreateFileW(..., CREATE_NEW, ...)`, write all bytes, call `FlushFileBuffers`, close the handle, then:

```text
existing target -> ReplaceFileW(target, temporary, nullptr, 0, nullptr, nullptr)
missing target  -> MoveFileExW(temporary, target, MOVEFILE_WRITE_THROUGH)
```

`REPLACEFILE_WRITE_THROUGH` is explicitly unsupported by Microsoft and must not be used. Durability comes from flushing the replacement file before `ReplaceFileW`; the API supplies the atomic name replacement. Use RAII for both the file handle and temporary-path cleanup. If replacement fails, delete only this operation's temporary file and return the Win32 error. Never delete or truncate the original target on the failure path.

- [x] **Step 6: Run store tests, Fast gate, and optional checkpoint**

Run:

```powershell
ctest --test-dir build/windows-x64-debug -R "config.(model|codec|store)" --output-on-failure
pwsh -NoProfile -File tools/quality.ps1 -Mode Fast
```

If a Full gate and commit authorization exist:

```powershell
git add include/network_flow_monitor/config src/config src/CMakeLists.txt tests/CMakeLists.txt tests/config_store_tests.cpp
git commit -m "feat: persist configuration atomically"
```

Otherwise record the proposed message without committing.

---

### Task 5: Verify and document the configuration subsystem

**Files:**
- Modify: `docs/configuration.md`
- Modify: `docs/planning/task_plan.md`
- Modify: `docs/planning/progress.md`
- Modify: `README.md`

**Interfaces:**
- Consumes: all verified outputs from Tasks 1-4.
- Produces: a documented, quality-gated configuration checkpoint ready for settings UI consumption.

- [x] **Step 1: Measure the Release executable after yyjson integration against the recorded baseline**

The pre-integration Release artifact from the Full-gate-passing `af63968` source state is 111,616 bytes at `build/windows-x64-release/src/NetworkFlowMonitor.exe`; recheck and record that baseline before Task 1 changes if the artifact has been regenerated. After integration, rebuild Release, read the current byte length with `Get-Item`, and record both exact commands and byte counts in `docs/planning/progress.md`. Describe the delta without declaring a pass/fail threshold.

- [x] **Step 2: Run the full mandatory quality gate**

Run:

```powershell
pwsh -NoProfile -File tools/quality.ps1 -Mode Full
```

Expected: format, strict Debug, all config and existing tests, clang-tidy/Clang Static Analyzer, MSVC `/analyze`, Release x64, and Release tests pass.

- [x] **Step 3: Synchronize public documentation and progress**

Document schema 1 strictness, missing-field defaults, unknown/duplicate rejection, 1 MiB internal file limit, no silent portable-path fallback, and atomic save behavior in `docs/configuration.md`. Add test counts and exact quality results to `docs/planning/progress.md`. Mark only the configuration half of Milestone 1 complete; ranking remains active.

- [x] **Step 4: Validate documentation and the working tree**

Run the established navigational Markdown link scan excluding code-example implementation plans, then:

```powershell
git diff --check
git status --short
```

Expected: no whitespace errors, no generated build files staged, and only intended source/documentation changes present.

- [x] **Step 5: Create the optional subsystem checkpoint**

If the user has explicitly authorized a commit:

```powershell
git add CMakeLists.txt src tests include third_party docs README.md
git commit -m "feat: complete versioned configuration core"
```

Otherwise leave the Full-gate-passing worktree uncommitted and report the proposed checkpoint.
