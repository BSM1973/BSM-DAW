#pragma once
#include "AudioEngine.h"
#include <array>
#include <cmath>

// Display-only indicator: the selected CANAL preset and its edits are tracked
// separately in A and B. No audio state is changed by this class.
namespace LibertyChannelPresetStatus
{
using Settings=AudioEngine::ChannelProcessingSettings;
inline bool sameSettings(const Settings& a,const Settings& b) noexcept
{
    const auto near=[](float x,float y) {
        return std::isfinite(x) && std::isfinite(y) && std::abs(x-y)<0.001f;
    };
    return near(a.inputGainDb,b.inputGainDb)
        && near(a.highPassHz,b.highPassHz)
        && a.gateEnabled==b.gateEnabled
        && near(a.gateThresholdDb,b.gateThresholdDb)
        && a.compressorEnabled==b.compressorEnabled
        && near(a.compressorThresholdDb,b.compressorThresholdDb)
        && near(a.compressorRatio,b.compressorRatio)
        && a.eqEnabled==b.eqEnabled
        && near(a.eqLowDb,b.eqLowDb)
        && near(a.eqMidDb,b.eqMidDb)
        && near(a.eqHighDb,b.eqHighDb);
}
class Indicator final
{
public:
    void select(int bank,juce::String name,const Settings& snapshot)
    {
        if(bank<0 || bank>=2)return;
        auto& entry=banks[(size_t)bank];
        entry.name=std::move(name);
        entry.snapshot=snapshot;
    }
    void clear(int bank)
    {
        if(bank>=0 && bank<2)banks[(size_t)bank]=Entry{};
    }
    bool isModified(int bank,const Settings& current)const noexcept
    {
        if(bank<0 || bank>=2)return false;
        const auto& entry=banks[(size_t)bank];
        return entry.name.isNotEmpty() && !sameSettings(entry.snapshot,current);
    }
    juce::String description(int bank,const Settings& current)const
    {
        if(bank<0 || bank>=2)return "RÉGLAGES PERSONNALISÉS";
        const auto& entry=banks[(size_t)bank];
        if(entry.name.isEmpty())return juce::String::fromUTF8("RÉGLAGES PERSONNALISÉS");
        return juce::String::fromUTF8("PRÉRÉGLAGE : ")+entry.name
            +(isModified(bank,current)?"  (MODIFIÉ)":juce::String{});
    }
private:
    struct Entry
    {
        juce::String name;
        Settings snapshot;
    };
    std::array<Entry,2> banks{};
};
}
