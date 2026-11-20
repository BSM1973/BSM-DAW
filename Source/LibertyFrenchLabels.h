#pragma once
#include <juce_core/juce_core.h>

// Centralized French UI captions: compose accented glyphs by Unicode code
// point, independently from the editor/source encoding on macOS and Windows.
// NEVER replace French letters with ASCII approximations or pictograms.
namespace LibertyFrenchLabels
{
inline juce::String mixerDetails()
{
    // U+00C9 is the capital E acute in French DÉTAILS.
    return juce::String("D") + juce::String::charToString(0x00C9) + "TAILS";
}
inline juce::String mixerCompact()
{
    return "COMPACT";
}
}
