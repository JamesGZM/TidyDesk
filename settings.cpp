// SPDX-License-Identifier: MIT
#include "settings.h"
#include <commctrl.h>
#include <string>
#include <cwchar>

namespace {
constexpr wchar_t Key[] = L"Software\\LiteTaskbar";
constexpr wchar_t Run[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
HWND page = nullptr, ownerWindow = nullptr;
HFONT font = nullptr, titleFont = nullptr;
Preferences draft;
int Scale(int n) { return MulDiv(n, static_cast<int>(GetDpiForWindow(page)), 96); }
HWND Control(const wchar_t* cls, const wchar_t* text, DWORD style, int id, int x, int y, int w, int h) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
        Scale(x), Scale(y), Scale(w), Scale(h), page,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return c;
}
bool StartupEnabled() {
    wchar_t text[32768]{}; DWORD bytes = sizeof(text);
    return RegGetValueW(HKEY_CURRENT_USER, Run, L"LiteTaskbar", RRF_RT_REG_SZ, nullptr, text, &bytes) == ERROR_SUCCESS && *text;
}
LSTATUS Save(bool startup) {
    HKEY key = nullptr;
    LSTATUS result = RegCreateKeyExW(HKEY_CURRENT_USER, Run, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS) return result;
    if (startup) {
        wchar_t path[32768]{};
        const DWORD count = GetModuleFileNameW(nullptr, path, 32768);
        if (!count || count >= 32768) { RegCloseKey(key); return ERROR_BAD_PATHNAME; }
        std::wstring command = L"\"" + std::wstring(path) + L"\" --background";
        // Run entries have a documented maximum command length of 260 characters.
        if (command.size() >= 260) { RegCloseKey(key); return ERROR_FILENAME_EXCED_RANGE; }
        result = RegSetValueExW(key, L"LiteTaskbar", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(key, L"LiteTaskbar");
        if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) return result;
    result = RegCreateKeyExW(HKEY_CURRENT_USER, Key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS) return result;
    DWORD opacity = draft.opacity, maximized = draft.maximized ? 1U : 0U;
    result = RegSetValueExW(key, L"Opacity", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&opacity), sizeof(opacity));
    if (result == ERROR_SUCCESS) result = RegSetValueExW(key, L"Maximized", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&maximized), sizeof(maximized));
    RegCloseKey(key);
    return result;
}
void Label() {
    wchar_t text[128]{};
    swprintf_s(text, L"背景不透明度：%u%%   （0%% 全透明 / 100%% 系统默认）", draft.opacity);
    SetDlgItemTextW(page, 11, text);
}
void Build() {
    font = CreateFontW(-Scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    titleFont = CreateFontW(-Scale(25), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    auto logo = Control(L"STATIC", L"", SS_ICON, 1, 28, 25, 40, 40);
    SendMessageW(logo, STM_SETICON, reinterpret_cast<WPARAM>(LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101))), 0);
    auto title = Control(L"STATIC", L"LiteTaskbar", 0, 2, 80, 20, 380, 38);
    SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont), TRUE);
    Control(L"STATIC", L"轻量任务栏 · 0.3.0 实验版", 0, 3, 80, 62, 430, 25);
    Control(L"STATIC", L"任务栏外观", 0, 4, 28, 110, 490, 26);
    Control(L"STATIC", L"", 0, 11, 28, 148, 505, 28);
    auto slider = Control(TRACKBAR_CLASSW, L"背景不透明度", TBS_AUTOTICKS | WS_TABSTOP, 12, 24, 182, 510, 42);
    SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(slider, TBM_SETTICFREQ, 25, 0);
    SendMessageW(slider, TBM_SETPOS, TRUE, draft.opacity);
    auto rule = Control(L"BUTTON", L"当前窗口最大化时，使用系统默认背景", BS_AUTOCHECKBOX | WS_TABSTOP, 13, 28, 244, 500, 28);
    SendMessageW(rule, BM_SETCHECK, draft.maximized ? BST_CHECKED : BST_UNCHECKED, 0);
    Control(L"STATIC", L"启动与托盘", 0, 5, 28, 298, 490, 26);
    auto startup = Control(L"BUTTON", L"登录 Windows 时自动启动", BS_AUTOCHECKBOX | WS_TABSTOP, 14, 28, 336, 495, 28);
    SendMessageW(startup, BM_SETCHECK, StartupEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
    Control(L"STATIC", L"关闭设置后继续在托盘运行。图标可能在右下角 ∧ 中。\n再次双击程序或点击托盘图标，即可打开设置。", 0, 6, 28, 383, 505, 48);
    Control(L"STATIC", L"", 0, 15, 28, 445, 500, 26);
    Control(L"BUTTON", L"退出并恢复", WS_TABSTOP, 22, 28, 487, 128, 36);
    Control(L"BUTTON", L"关闭", WS_TABSTOP, 21, 302, 487, 100, 36);
    Control(L"BUTTON", L"应用", BS_DEFPUSHBUTTON | WS_TABSTOP, 20, 418, 487, 110, 36);
    Label();
}
LRESULT CALLBACK Proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: page = wnd; Build(); return 0;
    case WM_HSCROLL: draft.opacity = static_cast<unsigned>(SendDlgItemMessageW(wnd, 12, TBM_GETPOS, 0, 0)); Label(); SetDlgItemTextW(wnd, 15, L"点击“应用”保存更改"); return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == 20) {
            draft.maximized = IsDlgButtonChecked(wnd, 13) == BST_CHECKED;
            const auto result = Save(IsDlgButtonChecked(wnd, 14) == BST_CHECKED);
            if (result == ERROR_SUCCESS) {
                SendMessageW(ownerWindow, SettingsApply, draft.opacity, draft.maximized ? 1 : 0);
                SetDlgItemTextW(wnd, 15, L"设置已保存并应用");
            } else {
                wchar_t text[120]{}; swprintf_s(text, L"保存未完成（错误 %ld），请检查后重试。", result);
                SetDlgItemTextW(wnd, 15, text);
                CheckDlgButton(wnd, 14, StartupEnabled() ? BST_CHECKED : BST_UNCHECKED);
            }
        } else if (LOWORD(wp) == 21 || LOWORD(wp) == IDCANCEL) DestroyWindow(wnd);
        else if (LOWORD(wp) == 22) PostMessageW(ownerWindow, WM_CLOSE, 0, 0);
        return 0;
    case WM_DPICHANGED: {
        // Recreate controls with fonts and coordinates for the new monitor DPI.
        for (HWND child = GetWindow(wnd, GW_CHILD); child; child = GetWindow(wnd, GW_CHILD)) DestroyWindow(child);
        DeleteObject(font); DeleteObject(titleFont);
        auto r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(wnd, nullptr, r->left, r->top, r->right-r->left, r->bottom-r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        Build(); return 0;
    }
    case WM_CLOSE: DestroyWindow(wnd); return 0;
    case WM_DESTROY: DeleteObject(font); DeleteObject(titleFont); font = nullptr; titleFont = nullptr; page = nullptr; return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}
}
Preferences LoadPreferences() {
    Preferences result; DWORD value = 0, bytes = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, Key, L"Opacity", RRF_RT_REG_DWORD, nullptr, &value, &bytes) == ERROR_SUCCESS && value <= 100) result.opacity = value;
    bytes = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, Key, L"Maximized", RRF_RT_REG_DWORD, nullptr, &value, &bytes) == ERROR_SUCCESS) result.maximized = value != 0;
    return result;
}
void ShowSettings(HWND owner, Preferences value) {
    if (page) { ShowWindow(page, SW_RESTORE); SetForegroundWindow(page); return; }
    draft = value; ownerWindow = owner;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES}; InitCommonControlsEx(&controls);
    WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"LiteTaskbar.Settings"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(101)); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    const auto dpi = GetDpiForSystem();
    RECT rect{0, 0, MulDiv(560, static_cast<int>(dpi), 96), MulDiv(548, static_cast<int>(dpi), 96)};
    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRectExForDpi(&rect, style, FALSE, WS_EX_APPWINDOW | WS_EX_CONTROLPARENT, dpi);
    page = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_CONTROLPARENT, wc.lpszClassName, L"LiteTaskbar 设置", style,
        CW_USEDEFAULT, CW_USEDEFAULT, rect.right-rect.left, rect.bottom-rect.top, owner, nullptr, wc.hInstance, nullptr);
    ShowWindow(page, SW_SHOW); SetForegroundWindow(page);
}
void CloseSettings() { if (page) DestroyWindow(page); }
bool SettingsMessage(MSG* message) { return page && IsDialogMessageW(page, message); }
