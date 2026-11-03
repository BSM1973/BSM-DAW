#pragma once
#include <array>

// Read-only presentation of the four processing blocks for the CANAL A/B
// overview. No JUCE dependencies, no DSP writes, no variant selection.
namespace LibertyChannelABOverview
{
constexpr int chooseA=5200;
constexpr int chooseB=5201;
constexpr int openSectionBase=5210;
constexpr int sectionCount=4;

struct Counts
{
    std::array<int,sectionCount> bySection{};

    constexpr int total() const noexcept
    {
        int value=0;
        for(const int count:bySection)
            value+=count>0?count:0;
        return value;
    }

    constexpr bool identical() const noexcept { return total()==0; }
};

constexpr int sectionFromMenu(int selected) noexcept
{
    return selected>=openSectionBase && selected<openSectionBase+sectionCount
        ?selected-openSectionBase:-1;
}
}
