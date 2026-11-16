// Meridian entry point.
#include "config/Brand.h"
#include "engine/WebView.h"
#include "ui/BrowserWindow.h"

#include <objbase.h>
#include <shellapi.h>
#include <windows.h>

using namespace meridian;

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    OleInitialize(nullptr);

    std::wstring initialURL;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        if (argc > 1)
            initialURL = argv[1];
        LocalFree(argv);
    }

    engine::EngineContext engine(/*ephemeral*/ false);
    auto* window = ui::BrowserWindow::create(engine, initialURL);
    if (!window)
        return 1;

    // The window deletes itself on WM_NCDESTROY; check the HWND before touching it.
    HWND mainWindow = window->hwnd();
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (IsWindow(mainWindow) && window->handleShortcut(msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    OleUninitialize();
    return 0;
}
