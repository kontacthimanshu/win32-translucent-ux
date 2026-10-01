#pragma once

// IBackdropManager implementation (T035; research R-01, R-04).

#include <te/appearance/IBackdropManager.h>

namespace te
{

class BackdropManager final : public IBackdropManager
{
  public:
    HRESULT Apply(HWND hwnd, const EffectiveAppearance& effective) override;
    bool ProbeSystemBackdrop(HWND hwnd) override;
};

} // namespace te
