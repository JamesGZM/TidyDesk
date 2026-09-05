// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
struct Preferences { unsigned opacity = 0; bool maximized = false; };
Preferences LoadPreferences();
void ShowSettings(HWND owner, Preferences value);
void CloseSettings();
bool SettingsMessage(MSG* message);
inline constexpr UINT SettingsApply = WM_APP + 40;
inline constexpr UINT ShowSettingsMessage = WM_APP + 41;
