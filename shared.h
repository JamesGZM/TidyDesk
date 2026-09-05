// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
inline constexpr wchar_t HostClass[] = L"LiteTaskbar.Host.2026";
inline constexpr wchar_t TapClass[] = L"LiteTaskbar.Tap.2026.v03";
inline constexpr UINT MsgAttached = WM_APP + 10;
inline constexpr UINT MsgChanged = WM_APP + 11;
inline constexpr UINT MsgRestored = WM_APP + 12;
inline constexpr UINT MsgError = WM_APP + 13;
inline constexpr CLSID TapClsid = {0xf33b8e63,0xf180,0x40f4,{0x93,0x77,0x45,0x5b,0xbb,0xce,0x67,0xa1}};

inline constexpr UINT MsgSetOpacity = WM_APP + 14;
