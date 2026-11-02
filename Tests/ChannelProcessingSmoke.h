#pragma once
#include "AudioEngine.h"
#include "ChannelProjectState.h"
#include "ChannelInspectorSession.h"
#include "ChannelInspectorPresets.h"
#include "ChannelUserPresets.h"
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

        // Factory presets are non-destructive, in-range A/B starting points.
        AudioEngine presetEngine;
        LibertyChannelInspectorState::Session presetSession(presetEngine,0);
        for(int id=1;id<=LibertyChannelInspectorPresets::count;++id)
        {
            const auto v=LibertyChannelInspectorPresets::make(id);
            require(LibertyChannelInspectorPresets::name(id)[0]!=0,"preset without name");
            require(std::isfinite(v.inputGainDb) && v.inputGainDb>=-24.f && v.inputGainDb<=24.f,"preset gain");
            require(std::isfinite(v.highPassHz) && v.highPassHz>=0.f && v.highPassHz<=500.f,"preset highpass");
            require(std::isfinite(v.gateThresholdDb) && v.gateThresholdDb>=-80.f && v.gateThresholdDb<=-20.f,"preset gate");
            require(std::isfinite(v.compressorThresholdDb) && v.compressorThresholdDb>=-48.f && v.compressorThresholdDb<=0.f,"preset compressor");
            require(std::isfinite(v.compressorRatio) && v.compressorRatio>=1.f && v.compressorRatio<=12.f,"preset ratio");
            require(std::isfinite(v.eqLowDb) && std::abs(v.eqLowDb)<=18.f,"preset low");
            require(std::isfinite(v.eqMidDb) && std::abs(v.eqMidDb)<=18.f,"preset mid");
            require(std::isfinite(v.eqHighDb) && std::abs(v.eqHighDb)<=18.f,"preset high");
            presetSession.setPending(v);
            require(equal(presetSession.pending(),v),"preset draft");
            require(equal(presetEngine.getChannelProcessingSettings(0),neutral),"preset changed audio");
            presetSession.switchVariant();
            require(equal(presetSession.pending(),neutral),"preset changed inactive bank");
            presetSession.switchVariant();
        }
        const auto voicePreset=LibertyChannelInspectorPresets::make(2);
        presetSession.setPending(voicePreset);
        presetSession.apply();
        require(equal(presetEngine.getChannelProcessingSettings(0),voicePreset),"preset APPLY");
        require(equal(presetEngine.getChannelProcessingSettings(1),neutral),"preset leaked to track 2");

        // Named user presets survive XML roundtrip and preserve Unicode names.
        LibertyChannelUserPresets::Library personal;
        require(!personal.upsert("   ",voicePreset),"accepted blank preset name");
        require(!personal.upsert(juce::String::repeatedString("x",49),voicePreset),
                "accepted overly long preset name");
        require(personal.upsert(juce::String::fromUTF8("Voix épurée"),voicePreset),
                "could not create named preset");
        auto bassPreset=LibertyChannelInspectorPresets::make(4);
        require(personal.upsert("Basse chaude",bassPreset),"could not save second preset");
        require(personal.upsert(juce::String::fromUTF8("VOIX ÉPURÉE"),bassPreset),
                "could not update named preset");
        require(personal.items().size()==2,"duplicate preset name");
        require(personal.upsert(juce::String::fromUTF8("À l'été"),voicePreset),
                "could not add accented preset");
        require(personal.upsert(juce::String::fromUTF8("à L'ÉTÉ"),bassPreset),
                "could not update accented preset");
        require(personal.items().size()==3,"duplicate accented preset");
        require(personal.erase(juce::String::fromUTF8(" À L'ÉTÉ ")),
                "cannot delete accented name with different case");
        require(personal.items().size()==2,"accented preset was not deleted");
        const auto xmlPersonal=personal.toXml();
        auto parsedPersonal=juce::parseXML(xmlPersonal->toString());
        require(parsedPersonal!=nullptr,"personal preset XML invalid");
        LibertyChannelUserPresets::Library restoredPersonal;
        require(restoredPersonal.fromXml(*parsedPersonal),"personal preset XML read failed");
        require(restoredPersonal.items().size()==2,"personal preset count after XML");
        require(equal(restoredPersonal.items()[0].settings,bassPreset),
                "personal preset roundtrip changed settings");
        require(restoredPersonal.erase("basse CHAUDE"),"personal preset delete");
        require(restoredPersonal.items().size()==1,"personal preset delete left entry");
        require(equal(presetEngine.getChannelProcessingSettings(0),voicePreset),
                "preset library changed applied DSP");
        auto userDraft=LibertyChannelInspectorState::Session(presetEngine,0);
        userDraft.switchVariant();
        userDraft.setPending(restoredPersonal.items()[0].settings);
        require(equal(presetEngine.getChannelProcessingSettings(0),voicePreset),
                "loading personal preset changed live DSP");
        // A bad XML document must be rejected without destroying the existing list.
        juce::XmlElement badPersonal("OtherPresets");
        require(!restoredPersonal.fromXml(badPersonal),"accepted non-Liberty XML");
        require(restoredPersonal.items().size()==1,"corrupted preset library on read");

        // Exercise the real session object used by the CANAL window.
        LibertyChannelInspectorState::Session::clearClipboardForTests();
        LibertyChannelInspectorState::Session source(engine,0);
        require(!LibertyChannelInspectorState::Session::canPaste(), "clipboard not empty");
        auto draft=source.pending();
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
        source.setPending(draft);
        require(source.variantIndex()==0, "default A variant");
        source.switchVariant();
        require(source.variantIndex()==1, "switch to B");
        require(equal(source.pending(),neutral), "B should start from engine values");
        auto draftB=draft;
        draftB.inputGainDb=-6.0f;
        draftB.eqLowDb=-3.5f;
        draftB.gateEnabled=false;
        source.setPending(draftB);
        require(equal(engine.getChannelProcessingSettings(0),neutral), "B changed live audio");
        source.switchVariant();
        require(equal(source.pending(),draft), "A settings lost");
        source.switchVariant();
        require(equal(source.pending(),draftB), "B settings lost");
        source.switchVariant();
        require(equal(source.pending(),draft), "return to A lost edits");
        require(equal(engine.getChannelProcessingSettings(0),neutral), "staging changed audio");
        source.reset();
        require(equal(source.pending(),neutral), "reset did not prepare neutral values");
        require(equal(engine.getChannelProcessingSettings(0),neutral), "reset changed live audio");
        source.setPending(draft);
        // Both dialogs are opened before the first one copies settings.
        LibertyChannelInspectorState::Session alreadyOpen(engine,1);
        require(!LibertyChannelInspectorState::Session::canPaste(), "clipboard unexpectedly set");
        source.copy();
        require(LibertyChannelInspectorState::Session::canPaste(), "clipboard copy");

        require(alreadyOpen.paste(), "paste between already-open dialogs");
        require(equal(alreadyOpen.pending(),draft), "pasted values differ");
        require(equal(engine.getChannelProcessingSettings(1),neutral), "paste changed live audio");
        alreadyOpen.cancel();
        require(equal(engine.getChannelProcessingSettings(1),neutral), "cancel changed audio");

        // A different CANAL dialog can paste the copied settings onto its track.
        LibertyChannelInspectorState::Session target(engine,1);
        require(target.paste(), "second paste");
        target.apply();
        require(equal(engine.getChannelProcessingSettings(1),draft), "apply pasted settings");
        require(equal(engine.getChannelProcessingSettings(0),neutral), "paste affected source track");
        target.reset();
        require(equal(engine.getChannelProcessingSettings(1),draft), "reset modified audio before apply");
        target.apply();
        require(equal(engine.getChannelProcessingSettings(1),neutral), "reset apply did not neutralise");

        // APPLIQUER from the original dialog publishes all eleven settings.
        source.apply();
        require(equal(engine.getChannelProcessingSettings(0),draft), "apply / mixer sync");
        require(equal(engine.getChannelProcessingSettings(1),neutral), "cross-track DSP leak");

        // B only becomes audible when it is explicitly applied.
        source.switchVariant();
        require(equal(engine.getChannelProcessingSettings(0),draft), "switch changed live audio");
        require(equal(source.pending(),draftB), "B settings changed unexpectedly");
        source.apply();
        require(equal(engine.getChannelProcessingSettings(0),draftB), "B apply failed");
        source.switchVariant();
        require(equal(source.pending(),draft), "A unavailable after B apply");
        source.apply();
        require(equal(engine.getChannelProcessingSettings(0),draft), "A apply after B failed");

        // Closing and reopening CANAL retains both bank memories.
        LibertyChannelInspectorState::Session reopenedDialog(engine,0);
        require(reopenedDialog.variantIndex()==0,"A/B current bank lost");
        require(equal(reopenedDialog.pending(),draft),"A bank lost");
        reopenedDialog.switchVariant();
        require(equal(reopenedDialog.pending(),draftB),"B bank lost");
        reopenedDialog.apply();
        require(equal(engine.getChannelProcessingSettings(0),draftB),"B apply after reopen");
        LibertyChannelInspectorState::Session reopenedB(engine,0);
        require(reopenedB.variantIndex()==1,"B bank selection lost");
        reopenedB.switchVariant();
        require(equal(reopenedB.pending(),draft),"A bank recall");
        reopenedB.switchVariant();
        reopenedB.apply();

        // Project Save/Open must use the same serialization code as the application.
        juce::XmlElement savedTrack("Track");
        LibertyChannelProjectState::write(savedTrack,engine.getChannelProcessingSettings(0));
        LibertyChannelProjectState::writeVariants(savedTrack,engine.getChannelABState(0));
        auto parsed=juce::parseXML(savedTrack.toString());
        require(parsed!=nullptr, "project XML invalid");
        AudioEngine reopened;
        reopened.setChannelProcessingSettings(0,LibertyChannelProjectState::read(*parsed));
        reopened.setChannelABState(0,LibertyChannelProjectState::readVariants(
            *parsed,reopened.getChannelProcessingSettings(0)));
        require(equal(reopened.getChannelProcessingSettings(0),draftB),"project roundtrip");
        LibertyChannelInspectorState::Session fromProject(reopened,0);
        require(fromProject.variantIndex()==1,"project active bank");
        require(equal(fromProject.pending(),draftB),"project B");
        fromProject.switchVariant();
        require(equal(fromProject.pending(),draft),"project A");
        require(equal(reopened.getChannelProcessingSettings(1),neutral), "new track not neutral");

        // Old projects without channel attributes remain compatible.
        juce::XmlElement legacyTrack("Track");
        reopened.setChannelProcessingSettings(1,draft);
        reopened.setChannelProcessingSettings(1,LibertyChannelProjectState::read(legacyTrack));
        require(equal(reopened.getChannelProcessingSettings(1),neutral), "legacy defaults");
        require(!LibertyChannelProjectState::readVariants(legacyTrack,neutral).saved,
                "legacy project invented A/B banks");

        // A new session must explicitly reset the DSP, including on empty tracks.
        reopened.setChannelProcessingSettings(0,neutral);
        reopened.clearChannelABState(0);
        require(!reopened.getChannelABState(0).saved,"new project A/B not reset");
        require(equal(reopened.getChannelProcessingSettings(0),neutral), "new-project channel reset");
        require(equal(engine.getChannelProcessingSettings(0),draftB), "unrelated engine changed");
        return true;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr,"ChannelProcessingSmoke: %s\n",e.what());
        return false;
    }
}
