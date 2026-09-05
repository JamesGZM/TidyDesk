// SPDX-License-Identifier: MIT
#include "shared.h"
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    HWND host = FindWindowW(HostClass, nullptr);
    if(auto desktop=FindWindowW(L"TidyDesk.Desktop.Controller",nullptr))PostMessageW(desktop,WM_CLOSE,0,0);
    if (!host) return 0;
    return PostMessageW(host, WM_CLOSE, 0, 0) ? 0 : 2;
}
