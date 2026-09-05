// SPDX-License-Identifier: MIT
#include "shared.h"
#include "settings.h"
#include "desktop_model.h"
#include "window_rule.h"
#include <shellapi.h>
#include <objbase.h>
#include <ocidl.h>
#include <imm.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <filesystem>
#include <fstream>
#include <atomic>

namespace {
constexpr UINT TrayMessage = WM_APP + 1;
constexpr UINT AttachResult = WM_APP + 2;
constexpr UINT Deadline = 1;
constexpr UINT ShutdownDeadline = 2;
constexpr UINT RestartDelay = 3;
constexpr UINT RestoreDeadline = 5;
HWND host = nullptr;
HANDLE stopEvent = nullptr;
std::filesystem::path folder;
DWORD shellPid = 0;
UINT taskbarCreated = 0;
bool quitting = false;
bool attached = false;
bool backendFailed = false;
bool retryAfterRestore = false;
unsigned changed = 0;
std::atomic<bool> attaching{false};
NOTIFYICONDATAW icon{};
Preferences preferences;
HWND backend = nullptr;
HWINEVENTHOOK foregroundHook = nullptr, locationHook = nullptr;
HWINEVENTHOOK minimizeHook = nullptr, lifetimeHook = nullptr;
HWINEVENTHOOK cloakHook = nullptr;
bool ruleUpdatePending = false;
unsigned effectiveOpacity = 0;
HWND lastSentBackend = nullptr;
unsigned lastSentOpacity = 101;
void UpdateOpacity() {
    effectiveOpacity = preferences.maximized && HasMaximizedWindow() ? 100U : preferences.opacity;
    if (backend && !backendFailed && (backend != lastSentBackend || effectiveOpacity != lastSentOpacity)) {
        if (PostMessageW(backend, MsgSetOpacity, effectiveOpacity, 0)) {
            changed = 0;
            SetTimer(host, Deadline, 10000, nullptr);
            lastSentBackend = backend;
            lastSentOpacity = effectiveOpacity;
        } else {
            backendFailed = true;
            SettingsStatus(L"应用失败，请重试。");
        }
    }
}
void CALLBACK WindowEvent(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG, DWORD, DWORD) {
    if (event == EVENT_SYSTEM_FOREGROUND || event == EVENT_SYSTEM_MINIMIZESTART || event == EVENT_SYSTEM_MINIMIZEEND ||
        (object == OBJID_WINDOW && (!window || GetAncestor(window, GA_ROOT) == window || event == EVENT_OBJECT_DESTROY)))
        if (!ruleUpdatePending) ruleUpdatePending = SetTimer(host, 4, 160, nullptr) != 0;
}
void ConfigureRule() {
    if (foregroundHook) UnhookWinEvent(foregroundHook);
    if (locationHook) UnhookWinEvent(locationHook);
    if (minimizeHook) UnhookWinEvent(minimizeHook);
    if (lifetimeHook) UnhookWinEvent(lifetimeHook);
    if (cloakHook) UnhookWinEvent(cloakHook);
    cloakHook = nullptr;
    minimizeHook = nullptr; lifetimeHook = nullptr;
    foregroundHook = nullptr; locationHook = nullptr;
    if (preferences.maximized) {
        foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, WindowEvent, 0, 0, WINEVENT_OUTOFCONTEXT);
        locationHook = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, WindowEvent, 0, 0, WINEVENT_OUTOFCONTEXT);
        minimizeHook = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, nullptr, WindowEvent, 0, 0, WINEVENT_OUTOFCONTEXT);
        lifetimeHook = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE, nullptr, WindowEvent, 0, 0, WINEVENT_OUTOFCONTEXT);
        cloakHook = SetWinEventHook(EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED, nullptr, WindowEvent, 0, 0, WINEVENT_OUTOFCONTEXT);
    }
    UpdateOpacity();
}

void Status(const char* state, HRESULT result = S_OK) noexcept {
    try {
        const auto log = folder / L"events.txt";
        if (!std::filesystem::exists(log) || std::filesystem::file_size(log) < 65536) {
            std::ofstream trace(log, std::ios::app);
            trace << GetTickCount64() << ' ' << state << ' ' << changed << " 0x" << std::hex << static_cast<unsigned long>(result) << '\n';
        }
        std::ofstream file(folder / L"status.txt", std::ios::trunc);
        BOOL inJob = FALSE;
        const BOOL jobKnown = IsProcessInJob(GetCurrentProcess(), nullptr, &inJob);
        file << "TidyDesk 0.4.4 experimental\nstate=" << state
             << "\nhost_pid=" << GetCurrentProcessId() << "\nexplorer_pid=" << shellPid
             << "\nbackground_elements=" << changed << "\nmaximized_rule=" << preferences.maximized
             << "\nrequested_opacity=" << preferences.opacity << "\neffective_opacity=" << effectiveOpacity
             << "\nprocess_in_job=" << (jobKnown ? (inJob ? "yes" : "no") : "unknown")
             << "\nhresult=0x" << std::hex
             << static_cast<unsigned long>(result) << '\n';
    } catch (...) {}
}
void AddTray() {
    icon.cbSize = sizeof(icon);
    icon.hWnd = host;
    icon.uID = 1;
    icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    icon.uCallbackMessage = TrayMessage;
    icon.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
    wcscpy_s(icon.szTip, L"TidyDesk · 整洁桌面");
    if (Shell_NotifyIconW(NIM_ADD, &icon)) {
        icon.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &icon);
    } else ShowSettings(host, preferences);
}
DWORD WINAPI Attach(void*) noexcept {
    HRESULT result = E_FAIL;
    try {
        const auto helper = (folder / L"TidyDeskAttach.exe").wstring();
        wchar_t arguments[128]{};
        swprintf_s(arguments, L" %lu:%llu:%lu", GetCurrentProcessId(),
                   static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(host)), shellPid);
        std::wstring command = L"\"" + helper + L"\"" + arguments;
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        if (CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, folder.c_str(), &startup, &child)) {
            CloseHandle(child.hThread);
            if (WaitForSingleObject(child.hProcess, 12000) == WAIT_OBJECT_0) {
                DWORD code = 0;
                result = GetExitCodeProcess(child.hProcess, &code) ? static_cast<HRESULT>(code) : E_FAIL;
            } else {
                TerminateProcess(child.hProcess, static_cast<UINT>(E_ABORT));
                result = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            }
            CloseHandle(child.hProcess);
        } else result = HRESULT_FROM_WIN32(GetLastError());
    } catch (...) { result = E_OUTOFMEMORY; }
    attaching = false;
    PostMessageW(host, AttachResult, static_cast<WPARAM>(result), 0);
    return 0;
}
void StartAttach() {
    if (quitting || attaching.exchange(true)) return;
    auto taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    DWORD pid = 0;
    if (taskbar) GetWindowThreadProcessId(taskbar, &pid);
    if (!pid) { attaching = false; Status("taskbar_not_found"); return; }
    if (pid == shellPid && attached && !backendFailed && IsWindow(backend)) { attaching = false; return; }
    shellPid = pid;
    changed = 0;
    attached = false;
    backend = nullptr;
    lastSentBackend = nullptr; lastSentOpacity = 101;
    backendFailed = false;
    ResetEvent(stopEvent);
    Status("attaching");
    SetTimer(host, Deadline, 10000, nullptr);
    HANDLE thread = CreateThread(nullptr, 0, Attach, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
    else { attaching = false; KillTimer(host, Deadline); Status("thread_failed", HRESULT_FROM_WIN32(GetLastError())); }
}
void RetryTaskbar() {
    SettingsStatus(L"正在应用…");
    if (attached || backend) { retryAfterRestore = true; SetEvent(stopEvent); SetTimer(host, RestoreDeadline, 5000, nullptr); }
    else StartAttach();
}
void Quit() {
    if (quitting) return;
    quitting = true;
    desk::Stop();
    KillTimer(host, Deadline);
    KillTimer(host, RestartDelay);
    KillTimer(host, RestoreDeadline);
    SetEvent(stopEvent);
    Status("restoring");
    SetTimer(host, ShutdownDeadline, 5000, nullptr);
    if (!attached && !attaching && !changed) DestroyWindow(host);
}
LRESULT CALLBACK WindowProc(HWND wnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == taskbarCreated && taskbarCreated) {
        AddTray();
        SetTimer(wnd, RestartDelay, 1500, nullptr);
        return 0;
    }
    switch (message) {
    case ShowSettingsMessage: ShowSettings(host, preferences); return 0;
    case SettingsRetry: RetryTaskbar(); return 0;
    case SettingsApply:
        if (wparam <= 100) { preferences.opacity = static_cast<unsigned>(wparam); preferences.maximized = lparam != 0; lastSentOpacity = 101; ConfigureRule(); if (!attached || backendFailed) RetryTaskbar(); }
        return attached && !backendFailed ? 1 : 0;
    case WM_CLOSE: Quit(); return 0;
    case WM_QUERYENDSESSION: SetEvent(stopEvent); return TRUE;
    case WM_APP + 15: Status("backend_stage", static_cast<HRESULT>(wparam)); return 0;
    case MsgAttached: backend = reinterpret_cast<HWND>(wparam); lastSentBackend = nullptr; lastSentOpacity = 101; attached = true; Status("attached"); SettingsStatus(L"正在应用…"); UpdateOpacity(); if (quitting) SetEvent(stopEvent); return 0;
    case MsgChanged:
        changed = static_cast<unsigned>(wparam);
        SettingsStatus(backendFailed?L"应用失败，请重试。":changed?L"应用成功":L"正在应用…");
        if (changed) KillTimer(wnd, Deadline);
        Status(changed ? (effectiveOpacity == 100 ? "system_default" : "custom_background") : "no_background_elements");
        return 0;
    case MsgError:
        backendFailed = true;
        KillTimer(wnd, Deadline);
        Status("backend_error", static_cast<HRESULT>(wparam));
        SettingsStatus(L"应用失败，请重试。");
        return 0;
    case MsgRestored:
        KillTimer(wnd, RestoreDeadline);
        changed = 0;
        attached = false;
        backend = nullptr; lastSentBackend = nullptr; lastSentOpacity = 101;
        if (retryAfterRestore && !quitting) { retryAfterRestore = false; StartAttach(); }
        if (!backendFailed || quitting) Status(wparam ? "restore_failed" : "restored", wparam ? E_FAIL : S_OK);
        if (quitting) DestroyWindow(wnd);
        return 0;
    case AttachResult:
        if (FAILED(static_cast<HRESULT>(wparam))) {
            KillTimer(wnd, Deadline);
            Status("attach_failed", static_cast<HRESULT>(wparam));
            SettingsStatus(L"应用失败，请重试。");
        }
        return 0;
      case WM_TIMER:
        if (wparam == RestoreDeadline) {
            KillTimer(wnd, RestoreDeadline);
            retryAfterRestore = false;
            backendFailed = true;
            SettingsStatus(L"应用失败，请重试。");
            Status("restore_timeout");
            return 0;
        }
        if (wparam == 4) { KillTimer(wnd, 4); ruleUpdatePending = false; UpdateOpacity(); return 0; }
        if (wparam == Deadline) {
            KillTimer(wnd, Deadline);
            if (!changed) {
                backendFailed = true;
                Status("no_supported_background_found");
                SetEvent(stopEvent);
                SettingsStatus(L"应用失败：当前系统未能完成任务栏设置。");
            }
        } else if (wparam == ShutdownDeadline) {
            Status("exit_restore_not_confirmed");
            DestroyWindow(wnd);
        } else if (wparam == RestartDelay) {
            KillTimer(wnd, RestartDelay);
            StartAttach();
        }
        return 0;
    case TrayMessage:
        if (LOWORD(lparam) == NIN_SELECT || LOWORD(lparam) == NIN_KEYSELECT) { ShowSettings(host, preferences); return 0; }
        if (LOWORD(lparam) == WM_CONTEXTMENU || LOWORD(lparam) == WM_RBUTTONUP) {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, L"打开设置");
            AppendMenuW(menu, MF_STRING, 2, L"退出并恢复任务栏");
            POINT p{}; GetCursorPos(&p); SetForegroundWindow(wnd);
            const auto selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                                                 p.x, p.y, 0, wnd, nullptr);
            DestroyMenu(menu);
            PostMessageW(wnd, WM_NULL, 0, 0);
            if (selected == 2) Quit();
            else if (selected == 1) ShowSettings(host, preferences);
        }
        return 0;
    case WM_DESTROY: if (cloakHook) UnhookWinEvent(cloakHook); if (minimizeHook) UnhookWinEvent(minimizeHook); if (lifetimeHook) UnhookWinEvent(lifetimeHook); CloseSettings(); if (foregroundHook) UnhookWinEvent(foregroundHook); if (locationHook) UnhookWinEvent(locationHook); Shell_NotifyIconW(NIM_DELETE, &icon); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(wnd, message, wparam, lparam);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int) {
    if (wcscmp(command, L"--test-settings") == 0) return TestSettings();
    if (wcscmp(command, L"--test-window-rule") == 0) return TestWindowRule();
    if (wcscmp(command, L"--self-test") == 0) {
        wchar_t testPath[32768]{};
        if (!GetModuleFileNameW(nullptr, testPath, 32768)) return 10;
        const auto dllPath = std::filesystem::path(testPath).parent_path() / L"TidyDeskTap.dll";
        HMODULE dll = LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!dll) return 11;
        using GetFactory = HRESULT(__stdcall*)(REFCLSID, REFIID, void**);
        const auto address = GetProcAddress(dll, "DllGetClassObject");
        GetFactory getFactory = nullptr;
        std::memcpy(&getFactory, &address, sizeof(getFactory));
        int result = 12;
        if (getFactory) {
            IClassFactory* factory = nullptr;
            if (SUCCEEDED(getFactory(TapClsid, __uuidof(IClassFactory), reinterpret_cast<void**>(&factory)))) {
                IObjectWithSite* tap = nullptr;
                if (SUCCEEDED(factory->CreateInstance(nullptr, __uuidof(IObjectWithSite), reinterpret_cast<void**>(&tap)))) {
                    void* site = reinterpret_cast<void*>(1);
                    result = (tap->GetSite(__uuidof(IUnknown), &site) == E_FAIL && site == nullptr && tap->SetSite(nullptr) == S_OK) ? 0 : 13;
                    tap->Release();
                }
                factory->Release();
            }
        }
        FreeLibrary(dll);
        return result;
    }
    if (wcscmp(command, L"--stop") == 0) {
        auto existing = FindWindowW(HostClass, nullptr);
        if (existing) PostMessageW(existing, WM_CLOSE, 0, 0);
        return existing ? 0 : 1;
    }
    const bool background = wcscmp(command, L"--background") == 0;
    if (*command && !background) return 2;
    // Settings contain editable Chinese names; keep the normal IME available.
    HANDLE singleton = CreateMutexW(nullptr, FALSE, L"Local\\LiteTaskbar.Host.Singleton");
    if (!singleton) return 3;
    if (GetLastError() == ERROR_ALREADY_EXISTS) { if (!background) PostMessageW(FindWindowW(HostClass, nullptr), ShowSettingsMessage, 0, 0); CloseHandle(singleton); return 0; }
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, 32768);
    if (!length || length >= 32768) { CloseHandle(singleton); return 4; }
    folder = std::filesystem::path(path).parent_path();
    if (GetFileAttributesW((folder / L"TidyDeskTap.dll").c_str()) == INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesW((folder / L"TidyDeskAttach.exe").c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(nullptr, L"请解压全部文件到同一目录后再运行。", L"TidyDesk", MB_OK);
        CloseHandle(singleton); return 5;
    }
    wchar_t eventName[96]{};
    swprintf_s(eventName, L"Local\\LiteTaskbar.Stop.%lu", GetCurrentProcessId());
    stopEvent = CreateEventW(nullptr, TRUE, FALSE, eventName);
    if (!stopEvent) { CloseHandle(singleton); return 6; }
    WNDCLASSW wc{}; wc.lpfnWndProc = WindowProc; wc.hInstance = instance; wc.lpszClassName = HostClass;
    if (!RegisterClassW(&wc)) { CloseHandle(stopEvent); CloseHandle(singleton); return 7; }
    host = CreateWindowExW(WS_EX_TOOLWINDOW, HostClass, L"TidyDesk", WS_POPUP,
                           0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!host) { CloseHandle(stopEvent); CloseHandle(singleton); return 8; }
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    preferences = LoadPreferences();
    AddTray(); ConfigureRule(); StartAttach();
    if (preferences.desktop) desk::Start(host);
    if (!background) ShowSettings(host, preferences);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { if (!SettingsMessage(&message)) { TranslateMessage(&message); DispatchMessageW(&message); } }
    SetEvent(stopEvent);
    CloseHandle(stopEvent);
    CloseHandle(singleton);
    return 0;
}


