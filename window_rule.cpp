// SPDX-License-Identifier: MIT
#include "window_rule.h"
#include <dwmapi.h>
#include <cwchar>
namespace {
bool Usable(HWND window) {
    if (!window || !IsWindow(window) || !IsWindowVisible(window) || IsIconic(window)) return false;
    DWORD cloaked = 0;
    return FAILED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) || !cloaked;
}
bool SameClass(HWND window, const wchar_t* name) {
    wchar_t cls[128]{};
    GetClassNameW(window, cls, 128);
    return wcscmp(cls, name) == 0;
}
bool Transient(HWND window) {
    bool shellFlyout = false;
    if (SameClass(window, L"Windows.UI.Core.CoreWindow")) {
        DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (process) {
            wchar_t path[32768]{}; DWORD length = 32768;
            if (QueryFullProcessImageNameW(process, 0, path, &length)) {
                const auto slash = wcsrchr(path, L'\\');
                const auto name = slash ? slash + 1 : path;
                shellFlyout = _wcsicmp(name, L"StartMenuExperienceHost.exe") == 0 ||
                    _wcsicmp(name, L"SearchHost.exe") == 0 || _wcsicmp(name, L"ShellExperienceHost.exe") == 0;
            }
            CloseHandle(process);
        }
    }
    return SameClass(window, L"Shell_TrayWnd") || SameClass(window, L"Shell_SecondaryTrayWnd") ||
        SameClass(window, L"#32768") || SameClass(window, L"XamlExplorerHostIslandWindow") ||
        shellFlyout || SameClass(window, L"LiteTaskbar.Settings") || SameClass(window, L"TidyDesk.Settings") ||
        (GetWindowLongPtrW(window, GWL_EXSTYLE) & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE));
}
bool MaximizedOrFillsWorkArea(HWND window) {
    if (!Usable(window)) return false;
    if (IsZoomed(window)) return true;
    RECT bounds{};
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds))) &&
        !GetWindowRect(window, &bounds)) return false;
    MONITORINFO monitor{sizeof(monitor)};
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) return false;
    // Some borderless/custom-titlebar apps fill the work area without WS_MAXIMIZE.
    constexpr LONG tolerance = 2;
    return bounds.left <= monitor.rcWork.left + tolerance && bounds.top <= monitor.rcWork.top + tolerance &&
        bounds.right >= monitor.rcWork.right - tolerance && bounds.bottom >= monitor.rcWork.bottom - tolerance;
}
}
bool HasMaximizedWindow(DWORD onlyProcess) {
    struct Query { DWORD process; bool found; } query{onlyProcess, false};
    EnumWindows([](HWND window, LPARAM context) -> BOOL {
        auto& scan = *reinterpret_cast<Query*>(context);
        if (scan.process) {
            DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
            if (pid != scan.process) return TRUE;
        }
        if (GetAncestor(window, GA_ROOT) != window || !Usable(window) || Transient(window) ||
            SameClass(window, L"Progman") || SameClass(window, L"WorkerW")) return TRUE;
        if (MaximizedOrFillsWorkArea(window)) { scan.found = true; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&query));
    return query.found;
}
int TestWindowRule() {
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"LiteTaskbar.RuleTest";
    RegisterClassW(&wc);
    HWND a = CreateWindowW(wc.lpszClassName, L"Rule test A", WS_OVERLAPPEDWINDOW,
        0, 0, 200, 100, nullptr, nullptr, wc.hInstance, nullptr);
    HWND b = CreateWindowW(wc.lpszClassName, L"Rule test B", WS_OVERLAPPEDWINDOW,
        0, 0, 200, 100, nullptr, nullptr, wc.hInstance, nullptr);
    HWND dialog = CreateWindowW(wc.lpszClassName, L"Owned dialog", WS_POPUP,
        0, 0, 100, 100, a, nullptr, wc.hInstance, nullptr);
    HWND tool = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Transient", WS_POPUP,
        0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
    int result = 1;
    if (a && b && dialog && tool) {
        ShowWindow(a, SW_SHOWMAXIMIZED); ShowWindow(b, SW_SHOWNOACTIVATE);
        ShowWindow(dialog, SW_SHOWNOACTIVATE); ShowWindow(tool, SW_SHOWNOACTIVATE);
        const auto pid = GetCurrentProcessId();
        SetForegroundWindow(b);
        const bool behindNormal = HasMaximizedWindow(pid);
        SetForegroundWindow(dialog);
        const bool behindDialog = HasMaximizedWindow(pid);
        ShowWindow(a, SW_MINIMIZE);
        const bool allNormal = HasMaximizedWindow(pid);
        ShowWindow(b, SW_SHOWMAXIMIZED);
        const bool secondMax = HasMaximizedWindow(pid);
        ShowWindow(b, SW_HIDE);
        const bool hidden = HasMaximizedWindow(pid);
        ShowWindow(b, SW_RESTORE);
        SetWindowLongPtrW(b, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(b, MONITOR_DEFAULTTONEAREST), &monitor);
        SetWindowPos(b, nullptr, monitor.rcWork.left, monitor.rcWork.top,
            monitor.rcWork.right - monitor.rcWork.left, monitor.rcWork.bottom - monitor.rcWork.top,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        DwmFlush();
        const bool borderless = HasMaximizedWindow(pid);
        result = behindNormal && behindDialog && !allNormal && secondMax && !hidden && borderless ? 0 : 2;
    }
    if (tool) DestroyWindow(tool);
    if (dialog) DestroyWindow(dialog);
    if (b) DestroyWindow(b);
    if (a) DestroyWindow(a);
    return result;
}

