#pragma once

// File-list selection (T055; data-model: SelectionModel): selected rows by display
// index, the focused row and the Shift-range anchor. Cleared on navigation; kept by item
// identity across a re-sort (ApplyPermutation); unaffected by the color picker.

#include <cstddef>
#include <optional>
#include <vector>

namespace te
{

class SelectionModel
{
  public:
    // A new listing (navigation): nothing selected, no focus, no anchor.
    void Reset(std::size_t count);
    // More rows arrived (EnumBatch): new rows start unselected; the rest is unchanged.
    void Grow(std::size_t count);

    // Click: only `index` selected; focus and anchor move to it.
    void Select(std::size_t index);
    // Ctrl+click / Ctrl+Space: toggles `index`; focus and anchor move to it.
    void Toggle(std::size_t index);
    // Shift+click / Shift+arrow: selects anchor..index only (anchor stays); focus moves.
    void ExtendTo(std::size_t index);
    // Ctrl+arrow: focus moves without changing the selection.
    void Focus(std::size_t index);
    void SelectAll();
    void Clear();

    // After a re-sort: oldIndexAt[newIndex] is the row's previous index
    // (SortModel::Sort). Selection, focus and anchor follow their items.
    void ApplyPermutation(const std::vector<std::size_t>& oldIndexAt);

    [[nodiscard]] std::size_t Count() const noexcept;
    [[nodiscard]] bool IsSelected(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t SelectedCount() const noexcept;
    [[nodiscard]] std::vector<std::size_t> SelectedIndices() const;
    [[nodiscard]] std::optional<std::size_t> FocusIndex() const noexcept
    {
        return m_focus;
    }
    [[nodiscard]] std::optional<std::size_t> AnchorIndex() const noexcept
    {
        return m_anchor;
    }

  private:
    std::vector<bool> m_selected;
    std::optional<std::size_t> m_focus;
    std::optional<std::size_t> m_anchor;
};

} // namespace te
