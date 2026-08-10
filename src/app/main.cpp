#include <windows.h>

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int) {
    ::MessageBoxW(nullptr, L"Network Flow Monitor project bootstrap is ready.",
                  L"Network Flow Monitor", MB_OK | MB_ICONINFORMATION);
    return 0;
}
