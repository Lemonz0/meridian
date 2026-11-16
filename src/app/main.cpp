// Meridian entry point.
#include "app/App.h"

#include <objbase.h>
#include <shellapi.h>
#include <windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    OleInitialize(nullptr);

    std::wstring initialURL;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i) {
            if (wcscmp(argv[i], L"--automation") == 0)
                meridian::app::App::instance().enableAutomation();
            else if (wcsncmp(argv[i], L"--profile=", 10) == 0)
                meridian::app::App::instance().setProfileDirectory(argv[i] + 10);
            else if (initialURL.empty())
                initialURL = argv[i];
        }
        LocalFree(argv);
    }

    int result = meridian::app::App::instance().run(initialURL);
    OleUninitialize();
    return result;
}
