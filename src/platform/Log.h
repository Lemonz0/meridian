#pragma once
// platform/: diagnostics log. Disabled unless a path is set (automation/test
// runs set <profile>\meridian.log). No telemetry: the file stays local.
#include <string>

namespace meridian::platform {

void setLogFile(const std::wstring& path);
void log(const wchar_t* format, ...);

} // namespace meridian::platform
