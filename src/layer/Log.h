#pragma once

#include <string>

namespace tf2vr
{
#ifdef TF2VRBB_LOGGING
void LogOpen(const std::wstring& path);
void Log(const char* format, ...);
#else
inline void LogOpen(const std::wstring&)
{
}

inline void Log(const char*, ...)
{
}
#endif
}
