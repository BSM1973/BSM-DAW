#pragma once
#include "AudioEngine.h"
#include "ChannelProjectState.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>

inline bool runLibertyChannelProcessingSmoke()
{
    try
    {
        const auto require = [](bool ok, const char* why)
        {
            if (!ok) throw std::runtime_error(why);
        };
        const auto equal = [&](const AudioEngine::ChannelProcessingSettings& a,
                               const AudioEngine::ChannelProcessingSettings& b)
        {
            const auto near = [](float x, float y) { return std::abs(x-y) < 0.0001f; };
            return near(a.inputGainDb,b.inputGainDb) && near(a.highPassHz,b.highPassHz)
                && a.compressorEnabled==b.compressorEnabled
                && near(a.compressorThresholdDb,b.compressorThresholdDb)
                && near(a.compressorRatio,b.compressorRatio)
                && a.gateEnabled==b.gateEnabled && near(a.gateThresholdDb,b.gateThresholdDb)
                && a.eqEnabled==b.eqEnabled && near(a.eqLowDb,b.eqLowDb)
                && near(a.eqMidDb,b.eqMidDb) && near(a.eqHighDb,b.eqHighDb);
        };
        AudioEngine engine;
        require(engine.getAudioTrackCount()>=2, "audio track count");
        const AudioEngine::ChannelProcessingSettings neutral;
        require(equal(engine.getChannelProcessingSettings(0),neutral), "initial state");

        // The inspector stages local values. Cancel and Reset must not touch the DSP.
        auto draft=engine.getChannelProcessingSettings(0);
        draft.inputGainDb=7.5f;
        draft.highPassHz=125.f;
        draft.gateEnabled=true;
        draft.gateThresholdDb=-42.f;
        draft.compressorEnabled=true;
        draft.compressorThresholdDb=-24.f;
        draft.compressorRatio=3.5f;
        draft.eqEnabled=true;
        draft.eqLowDb=2.5f;
        draft.eqMidDb=-4.5f;
        draft.eqHighDb=6.f;
        require(equal(engine.getChannelProcessingSettings(0),neutral), "cancel changed live audio");
        auto pendingReset=neutral;
        require(equal(engine.getChannelProcessingSettings(0),neutral), "reset changed live audio");
        (void)pendingReset;

        // APPLIQUER must publish all eleven fields to AudioEngine, on only this track.
        engine.setChannelProcessingSettings(0,draft);
        require(equal(engine.getChannelProcessingSettings(0),draft), "apply / mixer sync");
        require(equal(engine.getChannelProcessingSettings(1),neutral), "cross-track DSP leak");

        // Project Save/Open must use the same serialization code as the application.
        juce::XmlElement savedTrack("Track");
        LibertyChannelProjectState::write(savedTrack,engine.getChannelProcessingSettings(0));
        auto parsed=juce::parseXML(savedTrack.toString());
        require(parsed!=nullptr, "project XML invalid");
        AudioEngine reopened;
        reopened.setChannelProcessingSettings(0,LibertyChannelProjectState::read(*parsed));
        require(equal(reopened.getChannelProcessingSettings(0),draft), "project roundtrip");
        require(equal(reopened.getChannelProcessingSettings(1),neutral), "new track not neutral");

        // Old projects without channel attributes remain compatible.
        juce::XmlElement legacyTrack("Track");
        reopened.setChannelProcessingSettings(1,draft);
        reopened.setChannelProcessingSettings(1,LibertyChannelProjectState::read(legacyTrack));
        require(equal(reopened.getChannelProcessingSettings(1),neutral), "legacy defaults");

        // A new session must explicitly reset the DSP, including on empty tracks.
        reopened.setChannelProcessingSettings(0,neutral);
        require(equal(reopened.getChannelProcessingSettings(0),neutral), "new-project channel reset");
        require(equal(engine.getChannelProcessingSettings(0),draft), "unrelated engine changed");
        return true;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr,"ChannelProcessingSmoke: %s\n",e.what());
        return false;
    }
}
