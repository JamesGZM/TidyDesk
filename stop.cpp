// SPDX-License-Identifier: MIT
#include "shared.h"
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    HWND host = FindWindowW(HostClass, nullptr);
    if (!host) return 1;
    return PostMessageW(host, WM_CLOSE, 0, 0) ? 0 : 2;
}
