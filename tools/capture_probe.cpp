// Probe: capture the running Meridian window's content area with
// Windows.Graphics.Capture, time it, and save a BMP. Not shipped.
#include "platform/WindowCapture.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>
#include <winrt/base.h>

int wmain(int argc, wchar_t** argv)
{
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    std::thread(meridian::platform::requestBorderlessCapture).join();

    HWND hwnd = FindWindowW(L"MeridianBrowserWindow", nullptr);
    if (!hwnd) {
        std::printf("Meridian window not found\n");
        return 2;
    }
    std::printf("supported: %d\n", meridian::platform::windowCaptureSupported());
    RECT client;
    GetClientRect(hwnd, &client);
    UINT dpi = GetDpiForWindow(hwnd);
    client.top = MulDiv(52, dpi, 96); // below the toolbar

    for (int i = 0; i < 3; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        auto bmp = meridian::platform::captureClientRect(hwnd, client, 480);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!bmp) {
            std::printf("capture %d failed (%.1f ms)\n", i, ms);
            continue;
        }
        size_t nonBlack = 0;
        for (size_t p = 0; p < bmp->bgra.size(); p += 4)
            nonBlack += (bmp->bgra[p] + bmp->bgra[p + 1] + bmp->bgra[p + 2]) > 30;
        std::printf("capture %d: %dx%d in %.1f ms, non-black %.0f%%\n", i, bmp->width, bmp->height, ms,
            100.0 * nonBlack / (bmp->bgra.size() / 4));
        if (i == 2 && argc > 1) {
            BITMAPFILEHEADER fh { 0x4D42 };
            BITMAPINFOHEADER ih { sizeof(ih), bmp->width, -bmp->height, 1, 32, BI_RGB };
            fh.bfOffBits = sizeof(fh) + sizeof(ih);
            fh.bfSize = DWORD(fh.bfOffBits + bmp->bgra.size());
            std::ofstream f(argv[1], std::ios::binary);
            f.write(reinterpret_cast<char*>(&fh), sizeof(fh));
            f.write(reinterpret_cast<char*>(&ih), sizeof(ih));
            f.write(reinterpret_cast<char*>(bmp->bgra.data()), bmp->bgra.size());
        }
    }
    return 0;
}
