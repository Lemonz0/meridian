#include "UiDispatcher.h"

#include <memory>
#include <windows.h>

namespace meridian::platform {

static HWND g_window = nullptr;
static constexpr UINT kRun = WM_APP + 7;

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == kRun) {
        std::unique_ptr<std::function<void()>> fn(reinterpret_cast<std::function<void()>*>(l));
        (*fn)();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void initUiDispatcher()
{
    if (g_window)
        return;
    WNDCLASSW wc {};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MeridianUiDispatcher";
    RegisterClassW(&wc);
    g_window = CreateWindowW(wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
}

void postToUi(std::function<void()> fn)
{
    auto* heap = new std::function<void()>(std::move(fn));
    if (!g_window || !PostMessageW(g_window, kRun, 0, reinterpret_cast<LPARAM>(heap)))
        delete heap;
}

} // namespace meridian::platform
