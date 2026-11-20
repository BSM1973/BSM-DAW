#pragma once
#include "AudioEngine.h"
#include "ChannelABHistory.h"
#include "ChannelInspectorSession.h"
#include "ChannelPresetStatus.h"
#include "ChannelInspectorPresets.h"
#include "ChannelSectionComparison.h"
#include "ChannelABOverview.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

// End-to-end CANAL regression using the actual editor Session, A/B history,
// reference preset labels and Engine. The UI's callbacks use these same
// primitives. No hardware device or real-time playback is required.
inline void runLibertyChannelABSyncReversibilitySmoke()
{
    using Session=LibertyChannelInspectorState::Session;
    using Settings=Session::Settings;
    using Section=Session::Section;
    using Labels=LibertyChannelPresetStatus::Indicator;
    const auto require=[](bool condition,const char* message)
    {
        if(!condition)throw std::runtime_error(message);
    };
    const auto equal=[](const Settings& a,const Settings& b)
    {
        const auto near=[](float x,float y){return std::isfinite(x)
            && std::isfinite(y) && std::abs(x-y)<0.0001f;};
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
    };

    AudioEngine engine;
    const Settings originalDSP=engine.getChannelProcessingSettings(0);
    const Settings otherTrackDSP=engine.getChannelProcessingSettings(1);
    Session session(engine,0);
    const auto initialA=LibertyChannelInspectorPresets::make(2);
    const auto initialB=LibertyChannelInspectorPresets::make(4);
    session.setPending(initialA);
    session.switchVariant();
    session.setPending(initialB);
    session.switchVariant();

    Labels labels;
    labels.select(0,juce::String::fromUTF8("Voix épurée"),initialA);
    labels.select(1,juce::String::fromUTF8("Basse chaude"),initialB);
    struct SyncFrame
    {
        Session::Snapshot state;
        Labels labels;
    };
    LibertyChannelABHistory::History<SyncFrame> history;
    const auto capture=[&]()->SyncFrame{return {session.snapshot(),labels};};
    const auto sameFrame=[&](const SyncFrame& expected)
    {
        const auto actual=capture();
        if(actual.state.activeVariant!=expected.state.activeVariant)return false;
        for(int bank=0;bank<2;++bank)
        {
            const auto& active=actual.state.variants[(size_t)bank];
            const auto& reference=expected.state.variants[(size_t)bank];
            if(!equal(active,reference)
                || actual.labels.description(bank,active)
                    !=expected.labels.description(bank,reference)
                || actual.labels.isModified(bank,active)
                    !=expected.labels.isModified(bank,reference))
                return false;
        }
        return true;
    };
    const auto noLiveChange=[&]()
    {
        return equal(engine.getChannelProcessingSettings(0),originalDSP)
            && equal(engine.getChannelProcessingSettings(1),otherTrackDSP);
    };
    std::vector<SyncFrame> timeline{capture()};
    const auto step=[&](auto command)
    {
        history.record(capture());
        require(command(),"CANAL A/B sync command rejected");
        timeline.push_back(capture());
        require(noLiveChange(),"CANAL A/B sync changed DSP before APPLIQUER");
    };

    // Interleave partial and full copies, edits, preset references and bank
    // switches. All eleven actions must remain independently reversible.
    step([&]{return session.copySection(0,1,Section::Input);});
    step([&]{session.switchVariant();return true;});
    step([&]{auto edited=session.pending();edited.eqMidDb+=2.f;
              session.setPending(edited);return true;});
    step([&]{if(!session.copyVariant(1,0))return false;
              labels.copyBank(1,0);return true;});
    step([&]{session.switchVariant();return true;});
    step([&]{auto edited=session.pending();
              edited.compressorEnabled=!edited.compressorEnabled;
              edited.compressorRatio+=0.5f;
              session.setPending(edited);return true;});
    step([&]{return session.copySection(0,1,Section::Compressor);});
    step([&]{if(!session.copyVariant(0,1))return false;
              labels.copyBank(0,1);return true;});
    step([&]{session.switchVariant();return true;});
    step([&]{auto edited=session.pending();
              edited.gateEnabled=!edited.gateEnabled;
              edited.gateThresholdDb-=2.f;
              session.setPending(edited);return true;});
    step([&]{return session.copySection(1,0,Section::Gate);});
    require(timeline.size()==12,"CANAL A/B sync sequence truncated");
    require(history.undoDepth()==11 && history.redoDepth()==0,
            "CANAL A/B sync did not record one step per action");

    // The overview must be read-only; its per-section difference count is
    // derived from both real pending snapshots, without applying either.
    const auto beforeOverview=capture();
    LibertyChannelABOverview::Counts differences;
    for(int section=0;section<LibertyChannelABOverview::sectionCount;++section)
        differences.bySection[(size_t)section]=
            LibertyChannelSectionComparison::differenceCount(
                LibertyChannelSectionComparison::rows(
                    beforeOverview.state.variants[0],
                    beforeOverview.state.variants[1],
                    static_cast<Section>(section)));
    require(differences.identical(),"CANAL B-to-A Gate sync incomplete");
    require(sameFrame(beforeOverview) && noLiveChange(),
            "CANAL overview changed pending settings or DSP");
    require(!LibertyChannelABOverview::commitConfirmedWholeCopy(0,differences),
            "CANAL accepted cancelled whole-bank copy");
    require(!LibertyChannelABOverview::commitConfirmedWholeCopy(1,differences),
            "CANAL accepted redundant whole-bank copy");

    // Walk the entire timeline backwards, then forwards, comparing both
    // banks, selected variant, preset names and MODIFIÉ state at EVERY step.
    for(size_t index=timeline.size()-1;index>0;--index)
    {
        auto frame=capture();
        require(history.undo(frame),"CANAL A/B multi-step Undo failed");
        require(session.restoreSnapshot(frame.state),
                "CANAL A/B Undo could not restore Session snapshot");
        labels=frame.labels;
        require(sameFrame(timeline[index-1]),
                "CANAL A/B Undo lost values, selected bank or preset labels");
        require(noLiveChange(),"CANAL A/B Undo applied live audio");
    }
    require(!history.canUndo() && history.redoDepth()==11,
            "CANAL A/B Undo did not reach initial state");
    for(size_t index=1;index<timeline.size();++index)
    {
        auto frame=capture();
        require(history.redo(frame),"CANAL A/B multi-step Redo failed");
        require(session.restoreSnapshot(frame.state),
                "CANAL A/B Redo could not restore Session snapshot");
        labels=frame.labels;
        require(sameFrame(timeline[index]),
                "CANAL A/B Redo lost values, selected bank or preset labels");
        require(noLiveChange(),"CANAL A/B Redo applied live audio");
    }
    require(!history.canRedo() && history.undoDepth()==11,
            "CANAL A/B Redo did not reach final state");

    // Branching from an earlier state must discard stale future Redo steps.
    for(int i=0;i<2;++i)
    {
        auto frame=capture();
        require(history.undo(frame) && session.restoreSnapshot(frame.state),
                "CANAL A/B branching Undo failed");
        labels=frame.labels;
    }
    require(sameFrame(timeline[timeline.size()-3]) && history.canRedo(),
            "CANAL A/B did not preserve a redo branch");
    history.record(capture());
    auto altered=session.pending();
    altered.eqLowDb+=1.5f;
    session.setPending(altered);
    require(!history.canRedo() && history.redoDepth()==0,
            "CANAL A/B new edit retained invalid Redo steps");
    require(noLiveChange(),"CANAL A/B branch changed live audio");

    // Only APPLIQUER commits the chosen bank and stores BOTH bank snapshots.
    const auto approved=capture();
    session.apply();
    const auto reopened=Session(engine,0).snapshot();
    require(reopened.activeVariant==approved.state.activeVariant,
            "CANAL A/B APPLY lost active bank");
    for(int bank=0;bank<2;++bank)
        require(equal(reopened.variants[(size_t)bank],
                      approved.state.variants[(size_t)bank]),
                "CANAL A/B APPLY lost an inactive bank or copied section");
    require(equal(engine.getChannelProcessingSettings(0),
                  approved.state.variants[(size_t)approved.state.activeVariant]),
            "CANAL A/B APPLY published the wrong variant");
    require(equal(engine.getChannelProcessingSettings(1),otherTrackDSP),
            "CANAL A/B synchronization affected another track");

    // A cancelled later edit restores the committed draft without touching DSP.
    Session cancelled(engine,0);
    auto afterApply=cancelled.pending();
    afterApply.inputGainDb+=2.f;
    cancelled.setPending(afterApply);
    cancelled.cancel();
    require(equal(cancelled.pending(),
                  approved.state.variants[(size_t)approved.state.activeVariant]),
            "CANAL A/B CANCEL did not discard the later edit");
    require(equal(engine.getChannelProcessingSettings(0),
                  approved.state.variants[(size_t)approved.state.activeVariant]),
            "CANAL A/B CANCEL modified the DSP");
    std::fprintf(stdout,
        "CANAL A/B synchronization: 11 operations, 11 Undo, 11 Redo, "
        "branch invalidation, preset labels, APPLY/CANCEL OK\\n");
}
