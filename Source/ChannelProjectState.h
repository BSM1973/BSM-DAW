#pragma once

#include "AudioEngine.h"
#include <juce_core/juce_core.h>
#include <cmath>

// Shared project serialization used by the real project loader and the
// channel-processing regression test. Legacy projects have neutral defaults.
namespace LibertyChannelProjectState
{
inline void write(juce::XmlElement& track, const AudioEngine::ChannelProcessingSettings& channel)
{
    track.setAttribute("channelInputGainDb", (double)channel.inputGainDb);
    track.setAttribute("channelHighPassHz", (double)channel.highPassHz);
    track.setAttribute("channelCompressorOn", channel.compressorEnabled);
    track.setAttribute("channelCompressorThresholdDb", (double)channel.compressorThresholdDb);
    track.setAttribute("channelCompressorRatio", (double)channel.compressorRatio);
    track.setAttribute("channelGateOn", channel.gateEnabled);
    track.setAttribute("channelGateThresholdDb", (double)channel.gateThresholdDb);
    track.setAttribute("channelEqOn", channel.eqEnabled);
    track.setAttribute("channelEqLowDb", (double)channel.eqLowDb);
    track.setAttribute("channelEqMidDb", (double)channel.eqMidDb);
    track.setAttribute("channelEqHighDb", (double)channel.eqHighDb);
}

inline AudioEngine::ChannelProcessingSettings read(const juce::XmlElement& track)
{
    const auto finiteOr = [](double value, double fallback) noexcept
    {
        return std::isfinite(value) ? value : fallback;
    };
    AudioEngine::ChannelProcessingSettings channel;
    channel.inputGainDb = (float)finiteOr(track.getDoubleAttribute("channelInputGainDb", 0.0), 0.0);
    channel.highPassHz = (float)finiteOr(track.getDoubleAttribute("channelHighPassHz", 0.0), 0.0);
    channel.compressorEnabled = track.getBoolAttribute("channelCompressorOn", false);
    channel.compressorThresholdDb = (float)finiteOr(track.getDoubleAttribute("channelCompressorThresholdDb", -18.0), -18.0);
    channel.compressorRatio = (float)finiteOr(track.getDoubleAttribute("channelCompressorRatio", 4.0), 4.0);
    channel.gateEnabled = track.getBoolAttribute("channelGateOn", false);
    channel.gateThresholdDb = (float)finiteOr(track.getDoubleAttribute("channelGateThresholdDb", -55.0), -55.0);
    channel.eqEnabled = track.getBoolAttribute("channelEqOn", false);
    channel.eqLowDb = (float)finiteOr(track.getDoubleAttribute("channelEqLowDb", 0.0), 0.0);
    channel.eqMidDb = (float)finiteOr(track.getDoubleAttribute("channelEqMidDb", 0.0), 0.0);
    channel.eqHighDb = (float)finiteOr(track.getDoubleAttribute("channelEqHighDb", 0.0), 0.0);
    return channel;
}

inline void writeVariants(juce::XmlElement& track,const AudioEngine::ChannelABState& state)
{
    if(!state.saved)return;
    auto* variants=track.createNewChildElement("ChannelVariants");
    variants->setAttribute("active",juce::jlimit(0,1,state.active));
    write(*variants->createNewChildElement("A"),state.variants[0]);
    write(*variants->createNewChildElement("B"),state.variants[1]);
}
inline AudioEngine::ChannelABState readVariants(
    const juce::XmlElement& track,const AudioEngine::ChannelProcessingSettings& live)
{
    AudioEngine::ChannelABState state;
    state.variants.fill(live);
    const auto* root=track.getChildByName("ChannelVariants");
    if(!root)return state; // Legacy project
    state.active=juce::jlimit(0,1,root->getIntAttribute("active",0));
    if(const auto* a=root->getChildByName("A"))state.variants[0]=read(*a);
    if(const auto* b=root->getChildByName("B"))state.variants[1]=read(*b);
    state.variants[(size_t)state.active]=live;
    state.saved=true;
    return state;
}
}
