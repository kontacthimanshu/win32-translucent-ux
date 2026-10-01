#include <te/ui/SelectionModel.h>

#include <algorithm>

namespace te
{

void SelectionModel::Reset(std::size_t count)
{
    m_selected.assign(count, false);
    m_focus.reset();
    m_anchor.reset();
}

void SelectionModel::Grow(std::size_t count)
{
    if (count > m_selected.size())
    {
        m_selected.resize(count, false);
    }
}

void SelectionModel::Select(std::size_t index)
{
    if (index >= m_selected.size())
    {
        return;
    }
    std::fill(m_selected.begin(), m_selected.end(), false);
    m_selected[index] = true;
    m_focus = index;
    m_anchor = index;
}

void SelectionModel::Toggle(std::size_t index)
{
    if (index >= m_selected.size())
    {
        return;
    }
    m_selected[index] = !m_selected[index];
    m_focus = index;
    m_anchor = index;
}

void SelectionModel::ExtendTo(std::size_t index)
{
    if (index >= m_selected.size())
    {
        return;
    }
    const std::size_t anchor = m_anchor.value_or(index);
    const auto [first, last] = std::minmax(anchor, index);
    std::fill(m_selected.begin(), m_selected.end(), false);
    for (std::size_t i = first; i <= last; ++i)
    {
        m_selected[i] = true;
    }
    m_anchor = anchor;
    m_focus = index;
}

void SelectionModel::Focus(std::size_t index)
{
    if (index < m_selected.size())
    {
        m_focus = index;
    }
}

void SelectionModel::SelectAll()
{
    std::fill(m_selected.begin(), m_selected.end(), true);
}

void SelectionModel::Clear()
{
    std::fill(m_selected.begin(), m_selected.end(), false);
}

void SelectionModel::ApplyPermutation(const std::vector<std::size_t>& oldIndexAt)
{
    if (oldIndexAt.size() != m_selected.size())
    {
        return; // not a permutation of this listing
    }
    std::vector<bool> selected(m_selected.size(), false);
    std::vector<std::size_t> newIndexOf(m_selected.size(), 0);
    for (std::size_t newIndex = 0; newIndex < oldIndexAt.size(); ++newIndex)
    {
        selected[newIndex] = m_selected[oldIndexAt[newIndex]];
        newIndexOf[oldIndexAt[newIndex]] = newIndex;
    }
    m_selected = std::move(selected);
    if (m_focus)
    {
        m_focus = newIndexOf[*m_focus];
    }
    if (m_anchor)
    {
        m_anchor = newIndexOf[*m_anchor];
    }
}

std::size_t SelectionModel::Count() const noexcept
{
    return m_selected.size();
}

bool SelectionModel::IsSelected(std::size_t index) const noexcept
{
    return index < m_selected.size() && m_selected[index];
}

std::size_t SelectionModel::SelectedCount() const noexcept
{
    return static_cast<std::size_t>(std::count(m_selected.begin(), m_selected.end(), true));
}

std::vector<std::size_t> SelectionModel::SelectedIndices() const
{
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < m_selected.size(); ++i)
    {
        if (m_selected[i])
        {
            indices.push_back(i);
        }
    }
    return indices;
}

} // namespace te
