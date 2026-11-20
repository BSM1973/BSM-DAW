#pragma once
// View of 16 notes per page, up to four pages (64 steps).
// Reaching a page beyond the current cycle explicitly extends it;
// returning to earlier pages never deletes or shortens any notes.
#include "PolyphonicStepGrid.h"
#include <algorithm>

namespace LibertyPolyGridPages
{
inline bool advance(LibertyPolyphonicGrid& grid, int& page) noexcept
{
    const int safe = std::clamp(page, 0, 3);
    const int next = (safe + 1) % 4;
    const int previousCount = grid.stepCount;
    bool extended = false;
    if (next > 0 && next * 16 >= previousCount && previousCount < 64)
    {
        const int newCount = (next + 1) * 16;
        if (newCount > previousCount)
        {
            // The original default rows followed the old total cycle length.
            // Preserve a row's deliberately shorter loop if it was customised.
            for (auto& row : grid.lanes)
                if (row.steps == previousCount)
                    row.steps = (std::uint8_t)newCount;
            grid.stepCount = newCount;
            grid.active = true;
            extended = true;
        }
    }
    page = next;
    return extended;
}
inline bool willExtend(const LibertyPolyphonicGrid& grid, int page) noexcept
{
    const int next = (std::clamp(page, 0, 3) + 1) % 4;
    return next > 0 && (next + 1) * 16 > grid.stepCount;
}
inline int firstStep(int page) noexcept { return std::clamp(page, 0, 3) * 16; }
inline int lastStep(int page) noexcept { return firstStep(page) + 16; }
}
