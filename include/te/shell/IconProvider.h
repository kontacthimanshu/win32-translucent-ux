#pragma once

// IIconProvider on its own STA worker (T059; research R-06, R-07): Shell icons through
// IShellItemImageFactory, posted back as IconReady.

#include <te/shell/IIconProvider.h>

#include <memory>

namespace te
{

class IconProvider final : public IIconProvider
{
  public:
    IconProvider();
    ~IconProvider() override; // Shutdown()

    IconProvider(const IconProvider&) = delete;
    IconProvider& operator=(const IconProvider&) = delete;

    void Request(HWND notifyHwnd, Generation gen, std::size_t itemKey, const ShellLocation& folder,
                 const ShellItemInfo& item, int sizePx, bool forceExtract = false) override;
    // Requests queued for an earlier generation are skipped when their turn comes.
    void CancelOlderThan(Generation gen) override;
    void Shutdown() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace te
