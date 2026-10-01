#pragma once

// Per-monitor DPI state for one window (constitution Principle I, FR-017).

#include <windows.h>

namespace te
{

class IDpiManager
{
  public:
    virtual ~IDpiManager() = default;

    virtual UINT Dpi() const = 0;
    virtual float Scale() const = 0; // dpi / 96
    virtual int ToPx(float dip) const = 0;
    virtual void OnDpiChanged(HWND hwnd, UINT newDpi, const RECT& suggested) = 0;
};

} // namespace te
