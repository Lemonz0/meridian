#include "SystemTheme.h"

#include <dwmapi.h>

namespace meridian::platform {

static DWORD readDword(HKEY root, const wchar_t* path, const wchar_t* name, DWORD fallback)
{
    DWORD value = fallback, size = sizeof(value);
    if (RegGetValueW(root, path, name, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
        return fallback;
    return value;
}

ThemeState queryTheme()
{
    ThemeState t;
    const wchar_t* personalize = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
    t.dark = readDword(HKEY_CURRENT_USER, personalize, L"AppsUseLightTheme", 1) == 0;
    t.transparencyEnabled = readDword(HKEY_CURRENT_USER, personalize, L"EnableTransparency", 1) != 0;

    HIGHCONTRASTW hc { sizeof(hc) };
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0))
        t.highContrast = (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;

    BOOL animations = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0))
        t.animationsEnabled = animations != FALSE;
    return t;
}

bool isRemoteSession()
{
    return GetSystemMetrics(SM_REMOTESESSION) != 0;
}

bool applyWindowMaterial(HWND hwnd, const ThemeState& t)
{
    BOOL dark = t.dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));

    bool useMica = t.transparencyEnabled && !t.highContrast && !isRemoteSession();
    DWM_SYSTEMBACKDROP_TYPE backdrop = useMica ? DWMSBT_MAINWINDOW : DWMSBT_NONE;
    HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    return useMica && SUCCEEDED(hr);
}

} // namespace meridian::platform
