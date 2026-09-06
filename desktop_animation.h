// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
namespace desk::motion {
inline double Milliseconds(){LARGE_INTEGER now{},frequency{};QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);return static_cast<double>(now.QuadPart)*1000.0/static_cast<double>(frequency.QuadPart);}
inline double Ease(double t){t=std::clamp(t,0.0,1.0);return t*t*(3.0-2.0*t);}
inline LONG Mix(LONG from,LONG to,double amount){return static_cast<LONG>(std::lround(static_cast<double>(from)+(static_cast<double>(to)-from)*amount));}
inline RECT Bounds(const RECT& from,const RECT& to,double amount){return {Mix(from.left,to.left,amount),Mix(from.top,to.top,amount),Mix(from.right,to.right,amount),Mix(from.bottom,to.bottom,amount)};}
}
