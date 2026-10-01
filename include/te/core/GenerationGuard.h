#pragma once

// Stale-result filter for worker payloads (research R-07; T051, T064). Each navigation
// advances the generation; a payload tagged with any other generation is freed on
// arrival instead of being applied, so a slow enumeration of a folder the user has
// already left can never show up in the list.

#include <te/core/Messages.h>
#include <te/core/Types.h>

#include <windows.h>

#include <cstddef>
#include <memory>

namespace te
{

class GenerationGuard
{
  public:
    [[nodiscard]] Generation Current() const noexcept
    {
        return m_current;
    }

    // Starts a new generation (a navigation or refresh) and returns it; every payload
    // tagged with an earlier generation is stale from now on.
    Generation Advance() noexcept
    {
        m_current = ++m_issued;
        return m_current;
    }

    // Issues a new number without changing the current generation, for requests that are
    // invalidated on their own schedule (the file icons after a DPI change, T081). Numbers
    // come from the same counter, so they never repeat one used for a navigation.
    Generation Issue() noexcept
    {
        return ++m_issued;
    }

    // Makes `generation` current again, e.g. the folder still on screen after a
    // navigation failed. Later Advance() calls still issue new numbers (never one that a
    // failed request used).
    void Rewind(Generation generation) noexcept
    {
        m_current = generation;
    }

    [[nodiscard]] bool IsCurrent(Generation generation) const noexcept
    {
        // Before the first navigation nothing is current (generation 0 is never issued).
        return m_current != 0 && generation == m_current;
    }

    // Takes ownership of `payload` (which has a `gen` member). Returns it if it is
    // current; otherwise frees it with its deleter and returns null.
    template <class T, class Deleter>
    [[nodiscard]] std::unique_ptr<T, Deleter> Accept(std::unique_ptr<T, Deleter> payload)
    {
        if (!payload)
        {
            return payload;
        }
        if (!IsCurrent(payload->gen))
        {
            payload.reset(); // freed here, with the payload's own deleter
            ++m_rejected;
        }
        return payload;
    }

    // TakeOwned + Accept for a WM_TE_* LPARAM.
    template <class T> [[nodiscard]] std::unique_ptr<T> Take(LPARAM lParam)
    {
        return Accept(TakeOwned<T>(lParam));
    }

    // Payloads freed as stale since construction (diagnostics).
    [[nodiscard]] std::size_t RejectedCount() const noexcept
    {
        return m_rejected;
    }

  private:
    Generation m_current = 0;
    Generation m_issued = 0; // highest generation ever issued
    std::size_t m_rejected = 0;
};

} // namespace te
