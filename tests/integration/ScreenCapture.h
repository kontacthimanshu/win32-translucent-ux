#pragma once

// Manual visual checks (DISABLED_ capture tests): copies a window from the screen to a
// 24-bit BMP named `name` in %TE_CAPTURE_DIR%, or %TEMP% when it is not set.

#include <windows.h>

#include <filesystem>
#include <string>

namespace te::test
{

inline void SaveScreen(HWND hwnd, const std::wstring& name)
{
    RECT r{};
    GetWindowRect(hwnd, &r);
    const int width = r.right - r.left;
    const int height = r.bottom - r.top;
    const HDC screen = GetDC(nullptr);
    const HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 24, BI_RGB};
    void* bits = nullptr;
    const HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    const HGDIOBJ old = SelectObject(memory, bitmap);
    BitBlt(memory, 0, 0, width, height, screen, r.left, r.top, SRCCOPY);
    GdiFlush();

    const DWORD stride = ((static_cast<DWORD>(width) * 3 + 3) & ~3u);
    const DWORD imageSize = stride * static_cast<DWORD>(height);
    BITMAPFILEHEADER file{0x4D42, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + imageSize, 0, 0,
                          sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)};
    wchar_t folder[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"TE_CAPTURE_DIR", folder, MAX_PATH) == 0)
    {
        GetTempPathW(MAX_PATH, folder);
    }
    const std::filesystem::path path = std::filesystem::path(folder) / name;
    const HANDLE out = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD written = 0;
    WriteFile(out, &file, sizeof(file), &written, nullptr);
    WriteFile(out, &info.bmiHeader, sizeof(info.bmiHeader), &written, nullptr);
    WriteFile(out, bits, imageSize, &written, nullptr);
    CloseHandle(out);

    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
}

} // namespace te::test
