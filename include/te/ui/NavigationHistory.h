#pragma once

// Back / Forward / Up history (T054; data-model: NavigationHistory, spec US3-3).
// Templated on the location type so it can be unit-tested with a stand-in (T049); the
// application uses te::Location, compared with ILIsEqual. Only committed navigations are
// recorded: the owner calls Navigate once a DirectoryRequest reaches Enumerating.

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace te
{

template <typename Location, typename Equal = std::equal_to<Location>> class NavigationHistory
{
  public:
    static constexpr std::size_t kMaxEntries = 100;

    explicit NavigationHistory(Equal equal = Equal{}) : m_equal(std::move(equal)) {}

    // Records `location` after the current entry. Does nothing (returns false) when it
    // equals the current entry. Otherwise drops the forward entries, appends, and drops
    // the oldest entry beyond kMaxEntries.
    bool Navigate(Location location)
    {
        if (const Location* current = Current(); current && m_equal(*current, location))
        {
            return false; // e.g. a refresh of the same place
        }
        if (!m_entries.empty())
        {
            m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(m_index) + 1, m_entries.end());
        }
        m_entries.push_back(std::move(location));
        if (m_entries.size() > kMaxEntries)
        {
            m_entries.erase(m_entries.begin(),
                            m_entries.begin() + static_cast<std::ptrdiff_t>(m_entries.size() - kMaxEntries));
        }
        m_index = m_entries.size() - 1;
        return true;
    }

    // Navigate(parent): an ordinary entry, so Back returns to the child, as Windows
    // Explorer does. `parent` is empty at the root (Desktop); then nothing happens.
    bool Up(std::optional<Location> parent)
    {
        return parent ? Navigate(std::move(*parent)) : false;
    }

    bool Back()
    {
        if (!CanBack())
        {
            return false;
        }
        --m_index;
        return true;
    }

    bool Forward()
    {
        if (!CanForward())
        {
            return false;
        }
        ++m_index;
        return true;
    }

    [[nodiscard]] bool CanBack() const
    {
        return !m_entries.empty() && m_index > 0;
    }

    [[nodiscard]] bool CanForward() const
    {
        return m_index + 1 < m_entries.size();
    }

    // The current entry, or nullptr before the first navigation.
    [[nodiscard]] const Location* Current() const
    {
        return m_entries.empty() ? nullptr : &m_entries[m_index];
    }

    [[nodiscard]] std::size_t Size() const noexcept
    {
        return m_entries.size();
    }
    [[nodiscard]] std::size_t Index() const noexcept
    {
        return m_index;
    }
    [[nodiscard]] const std::vector<Location>& Entries() const noexcept
    {
        return m_entries;
    }

  private:
    Equal m_equal;
    std::vector<Location> m_entries;
    std::size_t m_index = 0;
};

} // namespace te
