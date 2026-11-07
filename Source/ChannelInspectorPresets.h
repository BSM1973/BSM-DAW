#pragma once
#include "AudioEngine.h"

// Factory starting points only; APPLIQUER remains the only DSP commit.
namespace LibertyChannelInspectorPresets
{
using Settings=AudioEngine::ChannelProcessingSettings;
constexpr int count=6;
inline const char* name(int id) noexcept
{
    switch(id)
    {
        case 1:return "Neutre";
        case 2:return "Voix";
        case 3:return "Batterie";
        case 4:return "Basse";
        case 5:return "Guitare";
        case 6:return "Podcast";
        default:return "";
    }
}
inline Settings make(int id) noexcept
{
    Settings s;
    switch(id)
    {
        case 2:
            s.highPassHz=85.f;
            s.compressorEnabled=true;
            s.compressorThresholdDb=-20.f;
            s.compressorRatio=2.5f;
            s.eqEnabled=true;
            s.eqLowDb=-1.f;s.eqMidDb=1.5f;s.eqHighDb=2.f;
            break;
        case 3:
            s.highPassHz=30.f;
            s.compressorEnabled=true;
            s.compressorThresholdDb=-15.f;
            s.compressorRatio=3.5f;
            s.eqEnabled=true;
            s.eqLowDb=2.f;s.eqMidDb=-2.f;s.eqHighDb=2.f;
            break;
        case 4:
            s.highPassHz=25.f;
            s.compressorEnabled=true;
            s.compressorThresholdDb=-20.f;
            s.compressorRatio=4.f;
            s.eqEnabled=true;
            s.eqLowDb=2.f;s.eqMidDb=-1.f;s.eqHighDb=.5f;
            break;
        case 5:
            s.highPassHz=75.f;
            s.compressorEnabled=true;
            s.compressorThresholdDb=-18.f;
            s.compressorRatio=2.f;
            s.eqEnabled=true;
            s.eqLowDb=-1.f;s.eqMidDb=2.f;s.eqHighDb=.5f;
            break;
        case 6:
            s.highPassHz=90.f;
            s.gateEnabled=true;s.gateThresholdDb=-52.f;
            s.compressorEnabled=true;
            s.compressorThresholdDb=-22.f;
            s.compressorRatio=2.5f;
            s.eqEnabled=true;
            s.eqLowDb=-1.f;s.eqMidDb=1.f;s.eqHighDb=2.f;
            break;
        default:break;
    }
    return s;
}
}
