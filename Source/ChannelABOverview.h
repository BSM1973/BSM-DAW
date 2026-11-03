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
constexpr int copySectionAToBBase=5220;
constexpr int copySectionBToABase=5230;
constexpr int copyAllAToB=5240;
constexpr int copyAllBToA=5241;

enum class CommandKind { none, details, sectionAToB, sectionBToA, allAToB, allBToA };

// Whole-bank alignment also replaces its preset reference, so it requires
// explicit confirmation. Section copies retain their existing quick workflow.
constexpr bool needsFullCopyConfirmation(CommandKind command) noexcept
{
    return command==CommandKind::allAToB || command==CommandKind::allBToA;
}

struct Command
{
    CommandKind kind=CommandKind::none;
    int section=-1;
};

constexpr Command decodeCommand(int selected) noexcept
{
    if(selected>=openSectionBase && selected<openSectionBase+sectionCount)
        return {CommandKind::details,selected-openSectionBase};
    if(selected>=copySectionAToBBase && selected<copySectionAToBBase+sectionCount)
        return {CommandKind::sectionAToB,selected-copySectionAToBBase};
    if(selected>=copySectionBToABase && selected<copySectionBToABase+sectionCount)
        return {CommandKind::sectionBToA,selected-copySectionBToABase};
    if(selected==copyAllAToB)return {CommandKind::allAToB,-1};
    if(selected==copyAllBToA)return {CommandKind::allBToA,-1};
    return {};
}

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

    constexpr int affectedSections() const noexcept
    {
        int count=0;
        for(const int value:bySection)
            if(value>0)++count;
        return count;
    }

    constexpr bool canCopySection(int section) const noexcept
    {
        return section>=0 && section<sectionCount
            && bySection[static_cast<unsigned>(section)]>0;
    }
};

constexpr int sectionFromMenu(int selected) noexcept
{
    return selected>=openSectionBase && selected<openSectionBase+sectionCount
        ?selected-openSectionBase:-1;
}
}
