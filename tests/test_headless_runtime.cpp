#include <windows.h>

#include <crtdbg.h>
#include <cstdlib>

namespace {

int HeadlessCrtReportHook(int /*report_type*/, char* message, int* return_value) noexcept {
    if (message != nullptr) {
        OutputDebugStringA(message);
    }
    if (return_value != nullptr) {
        *return_value = 0;
    }
    // Returning FALSE allows _CrtSetReportMode(_CRTDBG_MODE_FILE) to write to stderr,
    // while preventing any GUI dialog box from opening.
    return FALSE;
}

struct HeadlessTestRuntimeInitializer {
    HeadlessTestRuntimeInitializer() noexcept {
        static_cast<void>(SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS |
                                       SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX));
        static_cast<void>(_set_abort_behavior(0U, _WRITE_ABORT_MSG | _CALL_REPORTFAULT));
        static_cast<void>(_CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, &HeadlessCrtReportHook));
#ifdef _DEBUG
        static_cast<void>(_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE));
        static_cast<void>(_CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR));
        static_cast<void>(_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE));
        static_cast<void>(_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR));
        static_cast<void>(_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE));
        static_cast<void>(_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR));
#endif
    }
};

// NOLINTNEXTLINE(cert-err58-cpp,cppcoreguidelines-avoid-non-const-global-variables)
const HeadlessTestRuntimeInitializer kHeadlessTestRuntimeInitializer{};

} // namespace
