#include "Log.h"

#ifdef TF2VRBB_LOGGING

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <share.h>

namespace tf2vr
{
namespace
{
std::mutex g_logMutex;
FILE* g_logFile = nullptr;
}

void LogOpen(const std::wstring& path)
{
    std::lock_guard lock(g_logMutex);
    if (g_logFile)
        return;
    g_logFile = _wfsopen(path.c_str(), L"w", _SH_DENYWR);
}

void Log(const char* format, ...)
{
    std::lock_guard lock(g_logMutex);
    if (!g_logFile)
        return;

    SYSTEMTIME time;
    GetLocalTime(&time);
    std::fprintf(g_logFile, "[%02u:%02u:%02u.%03u] [t%lu] ", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, GetCurrentThreadId());

    va_list args;
    va_start(args, format);
    std::vfprintf(g_logFile, format, args);
    va_end(args);

    std::fputc('\n', g_logFile);
    std::fflush(g_logFile);
}
}

#endif
