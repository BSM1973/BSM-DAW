#pragma once
#include <juce_core/juce_core.h>

// Display/grouping aliases only. Never rewrite a scanned PluginDescription,
// its identifier, binary location, collection key or saved project state.
namespace LibertyManufacturerGrouping
{
inline juce::String canonicalName(const juce::String& raw)
{
    const auto name = raw.trim();
    if (name.equalsIgnoreCase("Liberty") || name.equalsIgnoreCase("Liberty Music Software"))
        return "Liberty Music Software";
    return name.isEmpty() ? juce::String("AUTRES") : name;
}
inline bool isPinned(const juce::String& raw)
{
    return canonicalName(raw) == "Liberty Music Software";
}
inline int compareManufacturers(const juce::String& left, const juce::String& right)
{
    const auto a = canonicalName(left), b = canonicalName(right);
    const bool pa = isPinned(a), pb = isPinned(b);
    if (pa != pb) return pa ? -1 : 1;
    return a.compareNatural(b, false);
}
}
