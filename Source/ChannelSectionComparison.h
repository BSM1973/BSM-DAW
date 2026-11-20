#pragma once
#include "ChannelInspectorSession.h"
#include <cmath>
#include <vector>

// Read-only presentation of each processing section's pending A/B settings.
// Does not select a bank, mutate a draft, or apply any DSP changes.
namespace LibertyChannelSectionComparison
{
using Settings=AudioEngine::ChannelProcessingSettings;
using Section=LibertyChannelInspectorState::Session::Section;

struct Row
{
    juce::String label;
    juce::String valueA;
    juce::String valueB;
    bool different=false;
};

inline std::vector<Row> rows(const Settings& a,const Settings& b,
                             Section section)
{
    std::vector<Row> output;
    const auto addNumber=[&output](juce::String name,float x,float y,
                                    const char* unit,int decimals)
    {
        const bool different=!std::isfinite(x) || !std::isfinite(y)
            || std::abs(x-y)>=0.001f;
        output.push_back({std::move(name),juce::String(x,decimals)+unit,
                          juce::String(y,decimals)+unit,different});
    };
    const auto addSwitch=[&output](const char* name,bool x,bool y)
    {
        output.push_back({name,x?"ACTIF":"INACTIF",y?"ACTIF":"INACTIF",x!=y});
    };
    switch(section)
    {
        case Section::Input:
            addNumber("GAIN",a.inputGainDb,b.inputGainDb," dB",1);
            addNumber("COUPE-BAS",a.highPassHz,b.highPassHz," Hz",0);
            break;
        case Section::Gate:
            addSwitch("ACTIVATION",a.gateEnabled,b.gateEnabled);
            addNumber("SEUIL",a.gateThresholdDb,b.gateThresholdDb," dB",1);
            break;
        case Section::Compressor:
            addSwitch("ACTIVATION",a.compressorEnabled,b.compressorEnabled);
            addNumber("SEUIL",a.compressorThresholdDb,b.compressorThresholdDb,
                      " dB",1);
            addNumber("RATIO",a.compressorRatio,b.compressorRatio," :1",1);
            break;
        case Section::Equalizer:
            addSwitch("ACTIVATION",a.eqEnabled,b.eqEnabled);
            addNumber("GRAVES",a.eqLowDb,b.eqLowDb," dB",1);
            addNumber(juce::String::fromUTF8("MÉDIUMS"),
                      a.eqMidDb,b.eqMidDb," dB",1);
            addNumber("AIGUS",a.eqHighDb,b.eqHighDb," dB",1);
            break;
        default:
            break;
    }
    return output;
}

inline int differenceCount(const std::vector<Row>& rows) noexcept
{
    int count=0;
    for(const auto& row:rows)
        if(row.different)++count;
    return count;
}
}
