// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
class ForegroundRule {
    HWND previous = nullptr;
public:
    bool Evaluate(HWND foreground);
    HWND TrackedWindow() const { return previous; }
};
int TestForegroundRule();
