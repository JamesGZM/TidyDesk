// SPDX-License-Identifier: MIT
// Read-only taskbar compatibility probe. Does not modify or inject into Explorer.
#include <windows.h>
#include <psapi.h>
#include <winternl.h>
#include <cstdio>
#include <cwchar>
#include <cstring>

namespace {
unsigned taskbars = 0;
unsigned children = 0;

BOOL CALLBACK Child(HWND window, LPARAM) {
    // Bound output even if a customized shell contains a very large tree.
    if (++children > 256) return FALSE;
    wchar_t name[256]{};
    GetClassNameW(window, name, 256);
    std::wprintf(L"  child_class=%ls\n", name);
    return TRUE;
}

BOOL CALLBACK Top(HWND window, LPARAM) {
    wchar_t name[256]{};
    GetClassNameW(window, name, 256);
    if (std::wcscmp(name, L"Shell_TrayWnd") != 0 &&
        std::wcscmp(name, L"Shell_SecondaryTrayWnd") != 0) return TRUE;
    ++taskbars;
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    std::wprintf(L"taskbar_class=%ls process_id=%lu\n", name, processId);
    children = 0;
    EnumChildWindows(window, Child, 0);
    return TRUE;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wcscmp(argv[1], L"--help") == 0) {
        std::wprintf(L"LiteTaskbarProbe [--help]\nRead-only Windows/taskbar compatibility report.\n"
                     L"No taskbar changes, network access, persistence, or window titles.\n");
        return 0;
    }
    if (argc != 1) {
        std::fwprintf(stderr, L"Unknown argument. Use --help.\n");
        return 2;
    }
    std::wprintf(L"LiteTaskbar compatibility probe 0.1.0\n");
    using VersionFn = NTSTATUS(WINAPI*)(PRTL_OSVERSIONINFOW);
    const auto ntdll = GetModuleHandleW(L"ntdll.dll");
    // Copy the function address to avoid MSVC's incompatible-function-cast warning.
    const auto address = ntdll ? GetProcAddress(ntdll, "RtlGetVersion") : nullptr;
    VersionFn versionFn = nullptr;
    static_assert(sizeof(versionFn) == sizeof(address));
    std::memcpy(&versionFn, &address, sizeof(versionFn));
    RTL_OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (!versionFn || versionFn(&version) != 0) {
        std::fwprintf(stderr, L"Unable to read Windows version.\n");
        return 3;
    }
    std::wprintf(L"windows=%lu.%lu build=%lu\n", version.dwMajorVersion,
                 version.dwMinorVersion, version.dwBuildNumber);
    SYSTEM_INFO system{};
    GetNativeSystemInfo(&system);
    std::wprintf(L"native_architecture=%u logical_processors=%lu\n",
                 static_cast<unsigned>(system.wProcessorArchitecture), system.dwNumberOfProcessors);
    EnumWindows(Top, 0);
    std::wprintf(L"taskbar_count=%u\n", taskbars);
    PROCESS_MEMORY_COUNTERS memory{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory))) {
        std::wprintf(L"probe_working_set_bytes=%llu\n",
                     static_cast<unsigned long long>(memory.WorkingSetSize));
    }
    if (version.dwBuildNumber >= 22621) {
        std::wprintf(L"backend_status=modern_xaml_candidate_requires_runtime_validation\n");
    } else {
        std::wprintf(L"backend_status=not_yet_implemented\n");
    }
    std::wprintf(L"No settings changed. No compatibility or performance claim is implied.\n");
    return taskbars ? 0 : 4;
}
