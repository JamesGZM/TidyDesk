// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
struct Preferences { unsigned opacity = 0; bool maximized = false; bool desktop=false; unsigned theme=0; };
Preferences LoadPreferences();
void ShowSettings(HWND owner, Preferences value);
void CloseSettings();
bool SettingsMessage(MSG* message);
inline constexpr UINT SettingsApply = WM_APP + 40;
inline constexpr UINT ShowSettingsMessage = WM_APP + 41;
inline constexpr UINT SettingsRetry = WM_APP + 42;
void SettingsStatus(const wchar_t* text);
