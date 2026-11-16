#include "Log.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <windows.h>

namespace meridian::platform {

static std::mutex g_mutex;
static std::wstring g_path;

void setLogFile(const std::wstring& path)
{
    std::lock_guard lock(g_mutex);
    g_path = path;
}

void log(const wchar_t* format, ...)
{
    std::lock_guard lock(g_mutex);
    if (g_path.empty())
        return;
    wchar_t buffer[1024];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(buffer, _TRUNCATE, format, args);
    va_end(args);
    FILE* f = nullptr;
    if (_wfopen_s(&f, g_path.c_str(), L"a, ccs=UTF-8") == 0 && f) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        fwprintf(f, L"%02d:%02d:%02d.%03d [%lu] %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentThreadId(), buffer);
        fclose(f);
    }
}

} // namespace meridian::platform
