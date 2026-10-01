#pragma once

// IDirectoryEnumerator on an STA worker (T058; research R-07): IEnumShellItems in
// Next(64) calls, EnumBatch every kBatchSize items, then EnumDone; cancellable between
// calls through the request's stop token.

#include <te/shell/IDirectoryEnumerator.h>

#include <cstddef>
#include <memory>

namespace te
{

class DirectoryEnumerator final : public IDirectoryEnumerator
{
  public:
    static constexpr std::size_t kBatchSize = 256;

    DirectoryEnumerator();
    ~DirectoryEnumerator() override; // Shutdown()

    DirectoryEnumerator(const DirectoryEnumerator&) = delete;
    DirectoryEnumerator& operator=(const DirectoryEnumerator&) = delete;

    void Start(HWND notifyHwnd, Generation gen, const ShellLocation& loc) override;
    void CancelAll() override;
    void Shutdown() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace te
