// SPDX-License-Identifier: MIT
#include "shared.h"
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
HWND host = nullptr;
HANDLE stopEvent = nullptr;
std::filesystem::path folder;
DWORD shellPid = 0;
UINT taskbarCreated = 0;
bool quitting = false;
bool attached = false;
unsigned changed = 0;
std::atomic<bool> attaching{false};
NOTIFYICONDATAW icon{};

void Status(const char* state, HRESULT result = S_OK) noexcept {
    try {
        std::ofstream file(folder / L"status.txt", std::ios::trunc);
        file << "LiteTaskbar 0.2.1 experimental\nstate=" << state
             << "\nhost_pid=" << GetCurrentProcessId() << "\nexplorer_pid=" << shellPid
             << "\nbackground_elements=" << changed << "\nhresult=0x" << std::hex
             << static_cast<unsigned long>(result) << '\n';
    } catch (...) {}
}
void AddTray() {
    icon.cbSize = sizeof(icon);
    icon.hWnd = host;
    icon.uID = 1;
    icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    icon.uCallbackMessage = TrayMessage;
    icon.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(icon.szTip, L"LiteTaskbar - experimental taskbar transparency");
    Shell_NotifyIconW(NIM_ADD, &icon);
}
DWORD WINAPI Attach(void*) noexcept {
    HRESULT result = E_FAIL;
    try {
        const auto helper = (folder / L"LiteTaskbarAttach.exe").wstring();
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
    if (pid == shellPid && attached) { attaching = false; return; }
    shellPid = pid;
    changed = 0;
    attached = false;
    ResetEvent(stopEvent);
    Status("attaching");
    SetTimer(host, Deadline, 10000, nullptr);
    HANDLE thread = CreateThread(nullptr, 0, Attach, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
    else { attaching = false; KillTimer(host, Deadline); Status("thread_failed", HRESULT_FROM_WIN32(GetLastError())); }
}
void Quit() {
    if (quitting) return;
    quitting = true;
    KillTimer(host, Deadline);
    KillTimer(host, RestartDelay);
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
    case WM_CLOSE: Quit(); return 0;
    case WM_QUERYENDSESSION: SetEvent(stopEvent); return TRUE;
    case MsgAttached: attached = true; if (quitting) SetEvent(stopEvent); return 0;
    case MsgChanged:
        changed = static_cast<unsigned>(wparam);
        if (changed) KillTimer(wnd, Deadline);
        Status(changed ? "transparent" : "no_background_elements");
        return 0;
    case MsgError: Status("backend_error", static_cast<HRESULT>(wparam)); return 0;
    case MsgRestored:
        changed = 0;
        attached = false;
        Status(wparam ? "restore_failed" : "restored", wparam ? E_FAIL : S_OK);
        if (quitting) DestroyWindow(wnd);
        return 0;
    case AttachResult:
        if (FAILED(static_cast<HRESULT>(wparam))) {
            KillTimer(wnd, Deadline);
            Status("attach_failed", static_cast<HRESULT>(wparam));
            if (!quitting) MessageBoxW(wnd, L"无法连接任务栏。请查看程序目录中的 status.txt。\n未启动轮询，也不会自动重试。", L"LiteTaskbar", MB_OK | MB_ICONINFORMATION);
        }
        return 0;
    case WM_TIMER:
        if (wparam == Deadline) {
            KillTimer(wnd, Deadline);
            if (!changed) {
                Status("no_supported_background_found");
                SetEvent(stopEvent);
                MessageBoxW(wnd, L"未找到可修改的任务栏背景。本次连接将停止。\n请查看 status.txt；这不代表系统已透明。", L"LiteTaskbar", MB_OK | MB_ICONINFORMATION);
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
        if (lparam == WM_RBUTTONUP || lparam == WM_LBUTTONUP) {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, L"查看状态");
            AppendMenuW(menu, MF_STRING, 2, L"退出并恢复任务栏");
            POINT p{}; GetCursorPos(&p); SetForegroundWindow(wnd);
            const auto selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                                                 p.x, p.y, 0, wnd, nullptr);
            DestroyMenu(menu);
            PostMessageW(wnd, WM_NULL, 0, 0);
            if (selected == 2) Quit();
            else if (selected == 1) ShellExecuteW(wnd, L"open", (folder / L"status.txt").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        return 0;
    case WM_DESTROY: Shell_NotifyIconW(NIM_DELETE, &icon); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(wnd, message, wparam, lparam);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int) {
    if (wcscmp(command, L"--self-test") == 0) {
        wchar_t testPath[32768]{};
        if (!GetModuleFileNameW(nullptr, testPath, 32768)) return 10;
        const auto dllPath = std::filesystem::path(testPath).parent_path() / L"LiteTaskbarTap.dll";
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
    if (*command) return 2;
    // A tray-only application has no text input. Avoid loading IME components.
    ImmDisableIME(GetCurrentThreadId());
    HANDLE singleton = CreateMutexW(nullptr, FALSE, L"Local\\LiteTaskbar.Host.Singleton");
    if (!singleton) return 3;
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(singleton); return 0; }
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, 32768);
    if (!length || length >= 32768) { CloseHandle(singleton); return 4; }
    folder = std::filesystem::path(path).parent_path();
    if (GetFileAttributesW((folder / L"LiteTaskbarTap.dll").c_str()) == INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesW((folder / L"LiteTaskbarAttach.exe").c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(nullptr, L"请解压全部文件到同一目录后再运行。", L"LiteTaskbar", MB_OK);
        CloseHandle(singleton); return 5;
    }
    wchar_t eventName[96]{};
    swprintf_s(eventName, L"Local\\LiteTaskbar.Stop.%lu", GetCurrentProcessId());
    stopEvent = CreateEventW(nullptr, TRUE, FALSE, eventName);
    if (!stopEvent) { CloseHandle(singleton); return 6; }
    WNDCLASSW wc{}; wc.lpfnWndProc = WindowProc; wc.hInstance = instance; wc.lpszClassName = HostClass;
    if (!RegisterClassW(&wc)) { CloseHandle(stopEvent); CloseHandle(singleton); return 7; }
    host = CreateWindowExW(WS_EX_TOOLWINDOW, HostClass, L"LiteTaskbar", WS_POPUP,
                           0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!host) { CloseHandle(stopEvent); CloseHandle(singleton); return 8; }
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    AddTray(); StartAttach();
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    SetEvent(stopEvent);
    CloseHandle(stopEvent);
    CloseHandle(singleton);
    return 0;
}
