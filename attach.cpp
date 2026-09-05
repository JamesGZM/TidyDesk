// SPDX-License-Identifier: MIT
// Short-lived XAML connector. The tray process never loads the XAML runtime.
#include "shared.h"
#include <objbase.h>
#include <cstdio>
#include <cstring>
#include <filesystem>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR command, int) {
    unsigned long controllerPid = 0, explorerPid = 0;
    unsigned long long controllerWindow = 0;
    wchar_t extra = 0;
    if (swscanf_s(command, L"%lu:%llu:%lu%c", &controllerPid, &controllerWindow,
                  &explorerPid, &extra, 1U) != 3) return static_cast<int>(E_INVALIDARG);
    HWND hostWindow = reinterpret_cast<HWND>(static_cast<UINT_PTR>(controllerWindow));
    DWORD actualPid = 0, actualShellPid = 0;
    GetWindowThreadProcessId(hostWindow, &actualPid);
    GetWindowThreadProcessId(FindWindowW(L"Shell_TrayWnd", nullptr), &actualShellPid);
    wchar_t hostName[96]{};
    GetClassNameW(hostWindow, hostName, 96);
    if (!controllerPid || actualPid != controllerPid || !explorerPid || actualShellPid != explorerPid ||
        wcscmp(hostName, HostClass) != 0) return static_cast<int>(E_INVALIDARG);
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(nullptr, path, 32768)) return static_cast<int>(E_FAIL);
    const auto tap = std::filesystem::path(path).parent_path() / L"TidyDeskTap.dll";
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HMODULE xaml = LoadLibraryExW(L"Windows.UI.Xaml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HRESULT result = HRESULT_FROM_WIN32(GetLastError());
    if (xaml) {
        using Initialize = HRESULT(WINAPI*)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, CLSID, LPCWSTR);
        auto address = GetProcAddress(xaml, "InitializeXamlDiagnosticsEx");
        Initialize initialize = nullptr;
        static_assert(sizeof(initialize) == sizeof(address));
        std::memcpy(&initialize, &address, sizeof(initialize));
        result = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
        if (initialize) {
            wchar_t data[96]{};
            swprintf_s(data, L"%lu:%llu", controllerPid, controllerWindow);
            for (unsigned i = 1; i <= 64; ++i) {
                wchar_t endpoint[64]{};
                swprintf_s(endpoint, L"VisualDiagConnection%u", i);
                result = initialize(endpoint, explorerPid, L"", tap.c_str(), TapClsid, data);
                if (result != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) break;
            }
        }
        FreeLibrary(xaml);
    }
    if (SUCCEEDED(com)) CoUninitialize();
    return static_cast<int>(result);
}
