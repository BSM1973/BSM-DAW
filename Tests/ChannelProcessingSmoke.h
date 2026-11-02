#pragma once
#include "AudioEngine.h"
#include "ChannelProjectState.h"
#include "ChannelInspectorSession.h"
#include "ChannelInspectorPresets.h"
#include "ChannelUserPresets.h"
#include "ChannelPresetStatus.h"
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

        // The CANAL display recognizes edits per A/B variant, including
        // changes in activation toggles and a return to the saved values.
        LibertyChannelPresetStatus::Indicator status;
        const auto baseVoice=LibertyChannelInspectorPresets::make(2);
        const auto baseBass=LibertyChannelInspectorPresets::make(4);
        status.select(0,juce::String::fromUTF8("Voix épurée"),baseVoice);
        status.select(1,juce::String::fromUTF8("Basse chaude"),baseBass);
        require(!status.isModified(0,baseVoice),"fresh A preset marked modified");
        require(!status.isModified(1,baseBass),"fresh B preset marked modified");
        auto voiceEdited=baseVoice;
        voiceEdited.eqHighDb+=1.f;
        require(status.isModified(0,voiceEdited),"EQ edit not detected");
        require(status.description(0,voiceEdited).contains(juce::String::fromUTF8("MODIFIÉ")),
                "modified preset label missing");
        require(!status.isModified(1,baseBass),"A edit affected B label");
        voiceEdited.eqHighDb=baseVoice.eqHighDb;
        require(!status.isModified(0,voiceEdited),"restored EQ still marked modified");
        voiceEdited.compressorEnabled=!baseVoice.compressorEnabled;
        require(status.isModified(0,voiceEdited),"power toggle not detected");
        status.clear(0);
        require(!status.isModified(0,voiceEdited),"cleared label kept modified status");
        require(status.description(0,voiceEdited)
            ==juce::String::fromUTF8("RÉGLAGES PERSONNALISÉS"),
                "unlabelled status missing");
        require(status.description(1,baseBass)
            .contains(juce::String::fromUTF8("Basse chaude")),
                "B label lost after switching A");
        require(equal(engine.getChannelProcessingSettings(0),neutral),
                "status indicator touched live DSP");
        // Copying a label also copies the source reference and its modified
        // status; the destination remains independent after later edits.
        status.select(0,juce::String::fromUTF8("Voix épurée"),baseVoice);
        status.copyBank(0,1);
        require(!status.isModified(1,baseVoice),"copied label mismatches settings");
        require(status.description(1,baseVoice)
            .contains(juce::String::fromUTF8("Voix épurée")),
            "copied label is missing");
        auto changedCopy=baseVoice;
        changedCopy.eqMidDb+=1.f;
        require(status.isModified(1,changedCopy),"copied label lost modified detection");
        require(!status.isModified(0,baseVoice),"label copy modified source");
        status.copyBank(1,1);
        status.copyBank(-1,0);
        require(!status.isModified(0,baseVoice),"invalid label copy modified source");

        // A/B duplication must stage without changing live track audio,
        // even when the destination bank is selected in the dialog.
        AudioEngine copyEngine;
        LibertyChannelInspectorState::Session copySession(copyEngine,0);
        copySession.setPending(baseVoice);
        copySession.switchVariant();
        copySession.setPending(baseBass);
        require(copySession.copyVariant(0,1),"A to B copy rejected");
        require(copySession.variantIndex()==1,"copy switched active bank");
        require(equal(copySession.pending(),baseVoice),"A to B copy failed");
        require(equal(copyEngine.getChannelProcessingSettings(0),neutral),
                "A to B copy changed live audio");
        copySession.setPending(baseBass);
        require(copySession.copyVariant(1,0),"B to A copy rejected");
        copySession.switchVariant();
        require(equal(copySession.pending(),baseBass),"B to A copy failed");
        require(equal(copyEngine.getChannelProcessingSettings(0),neutral),
                "B to A copy changed live audio");
        require(!copySession.copyVariant(-1,0) &&
                !copySession.copyVariant(0,2) &&
                !copySession.copyVariant(0,0),
                "invalid copy was accepted");
        copySession.switchVariant();
        copySession.apply();
        require(equal(copyEngine.getChannelProcessingSettings(0),baseBass),
                "copied bank not applied");
        LibertyChannelInspectorState::Session copyReopened(copyEngine,0);
        copyReopened.switchVariant();
        require(equal(copyReopened.pending(),baseBass),
                "copied inactive bank not preserved after reopen");
        require(equal(copyEngine.getChannelProcessingSettings(1),neutral),
                "A/B copy changed another track");

        // Swapping A/B exchanges both drafts and their display references,
        // keeps the selected bank and remains non-destructive until APPLY.
        AudioEngine swapEngine;
        LibertyChannelInspectorState::Session swapSession(swapEngine,0);
        swapSession.setPending(baseVoice);
        swapSession.switchVariant();
        swapSession.setPending(baseBass);
        swapSession.switchVariant();
        LibertyChannelPresetStatus::Indicator swapStatus;
        swapStatus.select(0,juce::String::fromUTF8("Voix épurée"),baseVoice);
        swapStatus.select(1,juce::String::fromUTF8("Basse chaude"),baseBass);
        swapSession.swapVariants();
        swapStatus.swapBanks();
        require(swapSession.variantIndex()==0,"swap changed active bank");
        require(equal(swapSession.pending(),baseBass),"swap did not put B into A");
        require(swapStatus.description(0,swapSession.pending())
            .contains(juce::String::fromUTF8("Basse chaude")),
            "swap did not carry B preset name to A");
        swapSession.switchVariant();
        require(equal(swapSession.pending(),baseVoice),"swap did not put A into B");
        require(swapStatus.description(1,swapSession.pending())
            .contains(juce::String::fromUTF8("Voix épurée")),
            "swap did not carry A preset name to B");
        require(equal(swapEngine.getChannelProcessingSettings(0),neutral),
                "swap affected live audio before APPLY");
        swapSession.switchVariant();
        swapSession.apply();
        require(equal(swapEngine.getChannelProcessingSettings(0),baseBass),
                "swap applied wrong active variant");
        LibertyChannelInspectorState::Session swappedReopened(swapEngine,0);
        swappedReopened.switchVariant();
        require(equal(swappedReopened.pending(),baseVoice),
                "swap failed to persist inactive variant");
        swappedReopened.swapVariants();
        swappedReopened.switchVariant();
        require(equal(swappedReopened.pending(),baseVoice),
                "second swap did not restore original order");
        require(equal(swapEngine.getChannelProcessingSettings(1),neutral),
                "swap changed a second track");

        // Restoring one bank uses the snapshot captured when CANAL opened.
        // The other bank and its pending edits are preserved.
        AudioEngine restoreEngine;
        LibertyChannelInspectorState::Session setupRestore(restoreEngine,0);
        setupRestore.setPending(baseVoice);
        setupRestore.switchVariant();
        setupRestore.setPending(baseBass);
        setupRestore.switchVariant();
        setupRestore.apply();
        LibertyChannelInspectorState::Session restoring(restoreEngine,0);
        LibertyChannelPresetStatus::Indicator openingLabels,workingLabels;
        openingLabels.select(0,juce::String::fromUTF8("Voix épurée"),baseVoice);
        openingLabels.select(1,juce::String::fromUTF8("Basse chaude"),baseBass);
        workingLabels=openingLabels;
        auto editA=baseVoice;
        editA.eqLowDb+=2.f;
        restoring.setPending(editA);
        workingLabels.select(0,"Edited A",editA);
        restoring.switchVariant();
        auto editB=baseBass;
        editB.inputGainDb+=3.f;
        restoring.setPending(editB);
        workingLabels.select(1,"Edited B",editB);
        restoring.swapVariants();
        workingLabels.swapBanks();
        require(restoring.restoreOpeningVariant(0),"restoring A rejected");
        workingLabels.restoreBankFrom(openingLabels,0);
        require(restoring.variantIndex()==1,"restoring A switched B selection");
        require(equal(restoring.pending(),editA),
                "restoring A damaged prepared B settings after swap");
        restoring.switchVariant();
        require(equal(restoring.pending(),baseVoice),
                "restoring A did not recover opening values");
        require(workingLabels.description(0,restoring.pending())
            .contains(juce::String::fromUTF8("Voix épurée")),
            "restoring A did not recover original preset label");
        restoring.switchVariant();
        require(restoring.restoreOpeningVariant(1),"restoring B rejected");
        workingLabels.restoreBankFrom(openingLabels,1);
        require(equal(restoring.pending(),baseBass),"restoring B did not recover opening values");
        require(workingLabels.description(1,restoring.pending())
            .contains(juce::String::fromUTF8("Basse chaude")),
            "restoring B did not recover original label");
        require(!restoring.restoreOpeningVariant(-1)
                && !restoring.restoreOpeningVariant(2),"invalid bank restore accepted");
        require(equal(restoreEngine.getChannelProcessingSettings(0),baseVoice),
                "restoring banks changed the live sound without APPLY");
        restoring.cancel();
        require(equal(restoring.pending(),baseVoice),
                "CANCEL failed after an individual restore");
        restoring.switchVariant();
        require(equal(restoring.pending(),baseBass),
                "CANCEL lost original B after individual restore");
        require(equal(restoreEngine.getChannelProcessingSettings(1),neutral),
                "restoring A/B changed another channel");

        // Selective A/B copy: each section transfers its full parameters and
        // activation state, leaving all other sections and live audio intact.
        using Section=LibertyChannelInspectorState::Session::Section;
        AudioEngine sectionEngine;
        const auto prepareSectionSettings=[](bool first) {
            AudioEngine::ChannelProcessingSettings values;
            values.inputGainDb=first?4.5f:-7.5f;
            values.highPassHz=first?125.f:45.f;
            values.gateEnabled=first;
            values.gateThresholdDb=first?-38.f:-65.f;
            values.compressorEnabled=!first;
            values.compressorThresholdDb=first?-25.f:-15.f;
            values.compressorRatio=first?5.f:2.f;
            values.eqEnabled=first;
            values.eqLowDb=first?3.f:-4.f;
            values.eqMidDb=first?-2.f:6.f;
            values.eqHighDb=first?5.f:-1.f;
            return values;
        };
        const auto originalA=prepareSectionSettings(true);
        const auto originalB=prepareSectionSettings(false);
        const Section sections[]={
            Section::Input,Section::Gate,Section::Compressor,Section::Equalizer
        };
        for(const auto section:sections)
        {
            for(int from=0;from<2;++from)
            {
                LibertyChannelInspectorState::Session staged(sectionEngine,0);
                staged.setPending(originalA);
                staged.switchVariant();
                staged.setPending(originalB);
                if(from==0)staged.switchVariant();
                const auto selectedBefore=staged.variantIndex();
                const auto copiedFrom=from==0?originalA:originalB;
                const auto originalTo=from==0?originalB:originalA;
                require(staged.copySection(from,1-from,section),
                        "selective section copy rejected");
                require(staged.variantIndex()==selectedBefore,
                        "selective copy changed bank selection");
                staged.switchVariant();
                auto expected=originalTo;
                switch(section)
                {
                    case Section::Input:
                        expected.inputGainDb=copiedFrom.inputGainDb;
                        expected.highPassHz=copiedFrom.highPassHz;
                        break;
                    case Section::Gate:
                        expected.gateEnabled=copiedFrom.gateEnabled;
                        expected.gateThresholdDb=copiedFrom.gateThresholdDb;
                        break;
                    case Section::Compressor:
                        expected.compressorEnabled=copiedFrom.compressorEnabled;
                        expected.compressorThresholdDb=copiedFrom.compressorThresholdDb;
                        expected.compressorRatio=copiedFrom.compressorRatio;
                        break;
                    case Section::Equalizer:
                        expected.eqEnabled=copiedFrom.eqEnabled;
                        expected.eqLowDb=copiedFrom.eqLowDb;
                        expected.eqMidDb=copiedFrom.eqMidDb;
                        expected.eqHighDb=copiedFrom.eqHighDb;
                        break;
                }
                require(equal(staged.pending(),expected),
                        "section copy changed wrong parameters or missed power state");
                require(equal(sectionEngine.getChannelProcessingSettings(0),neutral),
                        "selective section copy changed audio before APPLY");
                staged.switchVariant();
                require(equal(staged.pending(),copiedFrom),
                        "section copy modified source bank");
                require(!staged.copySection(-1,1,section)
                        && !staged.copySection(0,2,section)
                        && !staged.copySection(0,0,section)
                        && !staged.copySection(0,1,static_cast<Section>(9)),
                        "invalid section copy accepted");
                staged.cancel();
                require(equal(sectionEngine.getChannelProcessingSettings(0),neutral),
                        "CANCEL after section copy modified DSP");
            }
        }
        // Combining sections must leave DSP untouched until APPLY, then persist
        // the mixed destination to the project's saved A/B state.
        LibertyChannelInspectorState::Session mix(sectionEngine,0);
        mix.setPending(originalA);
        mix.switchVariant();
        mix.setPending(originalB);
        require(mix.copySection(0,1,Section::Input)
                && mix.copySection(0,1,Section::Equalizer),
                "multi-section copy rejected");
        require(equal(sectionEngine.getChannelProcessingSettings(0),neutral),
                "multi-section copy applied without approval");
        auto mixed=originalB;
        mixed.inputGainDb=originalA.inputGainDb;
        mixed.highPassHz=originalA.highPassHz;
        mixed.eqEnabled=originalA.eqEnabled;
        mixed.eqLowDb=originalA.eqLowDb;
        mixed.eqMidDb=originalA.eqMidDb;
        mixed.eqHighDb=originalA.eqHighDb;
        require(equal(mix.pending(),mixed),"combined section copy wrong result");
        mix.apply();
        require(equal(sectionEngine.getChannelProcessingSettings(0),mixed),
                "combined copy APPLY did not update audio");
        LibertyChannelInspectorState::Session mixedReopened(sectionEngine,0);
        require(equal(mixedReopened.pending(),mixed),
                "mixed section settings lost after reopening");
        mixedReopened.switchVariant();
        require(equal(mixedReopened.pending(),originalA),
                "source variant lost after combined APPLY");
        require(equal(sectionEngine.getChannelProcessingSettings(1),neutral),
                "section copy leaked to another track");

        // Swap only the requested section of A and B, including enable
        // switches. A second swap must restore every original parameter.
        // Neither the selected bank nor live audio may change before APPLY.
        AudioEngine sectionSwapEngine;
        for(const auto section:sections)
        {
            for(int active=0;active<2;++active)
            {
                LibertyChannelInspectorState::Session swapping(sectionSwapEngine,0);
                swapping.setPending(originalA);
                swapping.switchVariant();
                swapping.setPending(originalB);
                if(active==0)swapping.switchVariant();
                auto expectedA=originalA;
                auto expectedB=originalB;
                switch(section)
                {
                    case Section::Input:
                        expectedA.inputGainDb=originalB.inputGainDb;
                        expectedA.highPassHz=originalB.highPassHz;
                        expectedB.inputGainDb=originalA.inputGainDb;
                        expectedB.highPassHz=originalA.highPassHz;
                        break;
                    case Section::Gate:
                        expectedA.gateEnabled=originalB.gateEnabled;
                        expectedA.gateThresholdDb=originalB.gateThresholdDb;
                        expectedB.gateEnabled=originalA.gateEnabled;
                        expectedB.gateThresholdDb=originalA.gateThresholdDb;
                        break;
                    case Section::Compressor:
                        expectedA.compressorEnabled=originalB.compressorEnabled;
                        expectedA.compressorThresholdDb=originalB.compressorThresholdDb;
                        expectedA.compressorRatio=originalB.compressorRatio;
                        expectedB.compressorEnabled=originalA.compressorEnabled;
                        expectedB.compressorThresholdDb=originalA.compressorThresholdDb;
                        expectedB.compressorRatio=originalA.compressorRatio;
                        break;
                    case Section::Equalizer:
                        expectedA.eqEnabled=originalB.eqEnabled;
                        expectedA.eqLowDb=originalB.eqLowDb;
                        expectedA.eqMidDb=originalB.eqMidDb;
                        expectedA.eqHighDb=originalB.eqHighDb;
                        expectedB.eqEnabled=originalA.eqEnabled;
                        expectedB.eqLowDb=originalA.eqLowDb;
                        expectedB.eqMidDb=originalA.eqMidDb;
                        expectedB.eqHighDb=originalA.eqHighDb;
                        break;
                }
                require(swapping.swapSection(section),"section A/B exchange rejected");
                require(swapping.variantIndex()==active,
                        "section A/B exchange changed selected variant");
                require(equal(swapping.pending(),active==0?expectedA:expectedB),
                        "section exchange changed unrelated controls on active bank");
                swapping.switchVariant();
                require(equal(swapping.pending(),active==0?expectedB:expectedA),
                        "section exchange changed unrelated controls on inactive bank");
                require(equal(sectionSwapEngine.getChannelProcessingSettings(0),neutral),
                        "section exchange changed live audio before APPLY");
                require(!swapping.swapSection(static_cast<Section>(9)),
                        "invalid section exchange accepted");
                require(equal(swapping.pending(),active==0?expectedB:expectedA),
                        "invalid section exchange damaged a bank");
                require(swapping.swapSection(section),"second section exchange rejected");
                require(equal(swapping.pending(),active==0?originalB:originalA),
                        "second exchange failed to restore inactive bank");
                swapping.switchVariant();
                require(equal(swapping.pending(),active==0?originalA:originalB),
                        "second exchange failed to restore active bank");
                swapping.cancel();
                require(equal(sectionSwapEngine.getChannelProcessingSettings(0),neutral),
                        "CANCEL after section exchange affected audio");
            }
        }
        // A single exchanged section must be persisted for both variants
        // only after APPLY, while the other track remains untouched.
        LibertyChannelInspectorState::Session exchangeApply(sectionSwapEngine,0);
        exchangeApply.setPending(originalA);
        exchangeApply.switchVariant();
        exchangeApply.setPending(originalB);
        exchangeApply.switchVariant();
        require(exchangeApply.swapSection(Section::Compressor),
                "compressor exchange before APPLY rejected");
        auto exchangedA=originalA;
        exchangedA.compressorEnabled=originalB.compressorEnabled;
        exchangedA.compressorThresholdDb=originalB.compressorThresholdDb;
        exchangedA.compressorRatio=originalB.compressorRatio;
        auto exchangedB=originalB;
        exchangedB.compressorEnabled=originalA.compressorEnabled;
        exchangedB.compressorThresholdDb=originalA.compressorThresholdDb;
        exchangedB.compressorRatio=originalA.compressorRatio;
        require(equal(sectionSwapEngine.getChannelProcessingSettings(0),neutral),
                "exchanged compressor became audible without APPLY");
        exchangeApply.apply();
        require(equal(sectionSwapEngine.getChannelProcessingSettings(0),exchangedA),
                "exchanged compressor not applied");
        LibertyChannelInspectorState::Session exchangeReopened(sectionSwapEngine,0);
        require(exchangeReopened.variantIndex()==0
                && equal(exchangeReopened.pending(),exchangedA),
                "exchanged A compressor not persisted");
        exchangeReopened.switchVariant();
        require(equal(exchangeReopened.pending(),exchangedB),
                "exchanged B compressor not persisted");
        require(equal(sectionSwapEngine.getChannelProcessingSettings(1),neutral),
                "section exchange modified other track");

        // Section-only reset: each control group has independent defaults,
        // and activating B must not reset A or alter any live audio.
        AudioEngine resetEngine;
        for(const auto section:sections)
        {
            for(int bank=0;bank<2;++bank)
            {
                LibertyChannelInspectorState::Session resetting(resetEngine,0);
                resetting.setPending(originalA);
                resetting.switchVariant();
                resetting.setPending(originalB);
                if(bank==0)resetting.switchVariant();
                const auto activeBefore=resetting.variantIndex();
                const auto original=bank==0?originalA:originalB;
                const auto other=bank==0?originalB:originalA;
                auto expected=original;
                switch(section)
                {
                    case Section::Input:
                        expected.inputGainDb=neutral.inputGainDb;
                        expected.highPassHz=neutral.highPassHz;
                        break;
                    case Section::Gate:
                        expected.gateEnabled=neutral.gateEnabled;
                        expected.gateThresholdDb=neutral.gateThresholdDb;
                        break;
                    case Section::Compressor:
                        expected.compressorEnabled=neutral.compressorEnabled;
                        expected.compressorThresholdDb=neutral.compressorThresholdDb;
                        expected.compressorRatio=neutral.compressorRatio;
                        break;
                    case Section::Equalizer:
                        expected.eqEnabled=neutral.eqEnabled;
                        expected.eqLowDb=neutral.eqLowDb;
                        expected.eqMidDb=neutral.eqMidDb;
                        expected.eqHighDb=neutral.eqHighDb;
                        break;
                }
                require(resetting.resetSection(section),"section reset rejected");
                require(resetting.variantIndex()==activeBefore,
                        "reset switched active bank");
                require(equal(resetting.pending(),expected),
                        "section reset altered other processing controls");
                require(equal(resetEngine.getChannelProcessingSettings(0),neutral),
                        "section reset affected live DSP");
                require(!resetting.resetSection(static_cast<Section>(9)),
                        "invalid reset section accepted");
                require(equal(resetting.pending(),expected),
                        "invalid reset changed values");
                resetting.switchVariant();
                require(equal(resetting.pending(),other),
                        "reset damaged other A/B variant");
                resetting.cancel();
                require(equal(resetEngine.getChannelProcessingSettings(0),neutral),
                        "cancel after section reset changed audio");
            }
        }
        LibertyChannelInspectorState::Session resetApply(resetEngine,0);
        resetApply.setPending(originalA);
        resetApply.switchVariant();
        resetApply.setPending(originalB);
        require(resetApply.resetSection(Section::Equalizer),
                "EQ section reset before apply failed");
        auto expectedB=originalB;
        expectedB.eqEnabled=neutral.eqEnabled;
        expectedB.eqLowDb=neutral.eqLowDb;
        expectedB.eqMidDb=neutral.eqMidDb;
        expectedB.eqHighDb=neutral.eqHighDb;
        require(equal(resetApply.pending(),expectedB),
                "EQ section reset lost non-EQ settings");
        resetApply.apply();
        require(equal(resetEngine.getChannelProcessingSettings(0),expectedB),
                "section reset not applied to engine");
        LibertyChannelInspectorState::Session resetReopened(resetEngine,0);
        require(resetReopened.variantIndex()==1
                && equal(resetReopened.pending(),expectedB),
                "reset section lost after reopening CANAL");
        resetReopened.switchVariant();
        require(equal(resetReopened.pending(),originalA),
                "reset section altered saved bank A");
        require(equal(resetEngine.getChannelProcessingSettings(1),neutral),
                "reset section leaked to another track");

        // Restore a single section to the values captured when CANAL opens.
        // Non-neutral opening values distinguish this from RESET TO NEUTRAL.
        for(const auto section:sections)
        {
            for(int bank=0;bank<2;++bank)
            {
                AudioEngine sectionRestoreEngine;
                LibertyChannelInspectorState::Session original(sectionRestoreEngine,0);
                original.setPending(originalA);
                original.switchVariant();
                original.setPending(originalB);
                original.switchVariant();
                original.apply();
                LibertyChannelInspectorState::Session editing(sectionRestoreEngine,0);
                if(bank==1)editing.switchVariant();
                const auto starting=bank==0?originalA:originalB;
                const auto edited=bank==0?originalB:originalA;
                editing.setPending(edited);
                require(editing.restoreOpeningSection(section),
                        "restore opening section rejected");
                auto expected=edited;
                switch(section)
                {
                    case Section::Input:
                        expected.inputGainDb=starting.inputGainDb;
                        expected.highPassHz=starting.highPassHz;
                        break;
                    case Section::Gate:
                        expected.gateEnabled=starting.gateEnabled;
                        expected.gateThresholdDb=starting.gateThresholdDb;
                        break;
                    case Section::Compressor:
                        expected.compressorEnabled=starting.compressorEnabled;
                        expected.compressorThresholdDb=starting.compressorThresholdDb;
                        expected.compressorRatio=starting.compressorRatio;
                        break;
                    case Section::Equalizer:
                        expected.eqEnabled=starting.eqEnabled;
                        expected.eqLowDb=starting.eqLowDb;
                        expected.eqMidDb=starting.eqMidDb;
                        expected.eqHighDb=starting.eqHighDb;
                        break;
                }
                require(equal(editing.pending(),expected),
                        "opening section restore changed another section");
                require(editing.variantIndex()==bank,
                        "section restore changed active variant");
                require(!editing.restoreOpeningSection(static_cast<Section>(9)),
                        "invalid opening section restore accepted");
                require(equal(editing.pending(),expected),
                        "invalid opening section restore changed values");
                require(equal(sectionRestoreEngine.getChannelProcessingSettings(0),originalA),
                        "opening section restore modified live audio before APPLY");
                editing.switchVariant();
                const auto untouched=bank==0?originalB:originalA;
                require(equal(editing.pending(),untouched),
                        "opening section restore modified other A/B bank");
                editing.switchVariant();
                editing.apply();
                require(equal(sectionRestoreEngine.getChannelProcessingSettings(0),expected),
                        "opening section restore failed to APPLY");
                LibertyChannelInspectorState::Session reopened(sectionRestoreEngine,0);
                require(reopened.variantIndex()==bank
                        && equal(reopened.pending(),expected),
                        "opening section restore lost after reopening");
                reopened.switchVariant();
                require(equal(reopened.pending(),untouched),
                        "opening section restore corrupted saved inactive bank");
                require(equal(sectionRestoreEngine.getChannelProcessingSettings(1),neutral),
                        "opening section restore changed a second track");
            }
        }

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
        require(personal.setFavorite(juce::String::fromUTF8("voix ÉPURÉE"),true),
                "could not favorite an accented name");
        require(personal.items()[0].favorite,"favorite not set");
        require(!personal.setFavorite("Missing",true),"favorited absent preset");
        require(personal.upsert(juce::String::fromUTF8("À l'été"),voicePreset),
                "could not add accented preset");
        require(personal.upsert(juce::String::fromUTF8("à L'ÉTÉ"),bassPreset),
                "could not update accented preset");
        require(personal.items().size()==3,"duplicate accented preset");
        require(personal.erase(juce::String::fromUTF8(" À L'ÉTÉ ")),
                "cannot delete accented name with different case");
        require(personal.items().size()==2,"accented preset was not deleted");
        require(personal.rename(juce::String::fromUTF8("VOIX ÉPURÉE"),
                                juce::String::fromUTF8("Voix claire")),
                "could not rename accented preset");
        require(!personal.rename("Voix claire","basse CHAUDE"),
                "rename overwrote another preset");
        require(!personal.rename("Absent","Sans source"),
                "rename invented a source preset");
        require(!personal.rename("Voix claire","   "),
                "rename accepted an empty name");
        require(personal.duplicate("Voix claire",
                                   juce::String::fromUTF8("Voix claire - copie")),
                "could not duplicate a preset");
        require(personal.items().size()==3,"duplicate did not add one entry");
        require(personal.items()[0].favorite,"rename dropped favorite");
        require(!personal.items()[2].favorite,"duplicate inherited favorite");
        require(equal(personal.items()[0].settings,personal.items()[2].settings),
                "duplicate changed sound settings");
        require(!personal.duplicate("Voix claire",
                                    juce::String::fromUTF8("VOIX CLAIRE - COPIE")),
                "duplicate overwrote a case-equivalent name");
        require(!personal.duplicate("Absent","Nouveau"),"duplicate missing source");
        require(personal.rename(juce::String::fromUTF8("VOIX CLAIRE"),
                                juce::String::fromUTF8("VOIX CLAIRE")),
                "case-only rename must be allowed");
        require(personal.items().size()==3,"rename created a new entry");
        require(personal.erase("Voix claire - copie"),"could not delete duplicate");
        require(personal.items().size()==2,"duplicate delete did not restore original count");
        require(equal(presetEngine.getChannelProcessingSettings(0),voicePreset),
                "preset rename or duplication changed DSP");
        const auto xmlPersonal=personal.toXml();
        auto parsedPersonal=juce::parseXML(xmlPersonal->toString());
        require(parsedPersonal!=nullptr,"personal preset XML invalid");
        LibertyChannelUserPresets::Library restoredPersonal;
        require(restoredPersonal.fromXml(*parsedPersonal),"personal preset XML read failed");
        require(restoredPersonal.items().size()==2,"personal preset count after XML");
        require(restoredPersonal.items()[0].name==juce::String::fromUTF8("VOIX CLAIRE"),
                "renamed preset name lost in XML");
        require(restoredPersonal.items()[0].favorite,"favorite lost in XML");
        require(!restoredPersonal.items()[1].favorite,"non-favorite gained favorite in XML");
        require(restoredPersonal.upsert("Voix claire",bassPreset)
                && restoredPersonal.items()[0].favorite,
                "re-saving favorite cleared its status");
        require(equal(restoredPersonal.items()[0].settings,bassPreset),
                "personal preset roundtrip changed settings");
        require(restoredPersonal.erase("basse CHAUDE"),"personal preset delete");
        require(restoredPersonal.items().size()==1,"personal preset delete left entry");
        for(int i=1;i<LibertyChannelUserPresets::Library::maxPresets;++i)
            require(restoredPersonal.upsert("Test "+juce::String(i),voicePreset),
                    "unable to fill personal preset library");
        require((int)restoredPersonal.items().size()==LibertyChannelUserPresets::Library::maxPresets,
                "preset maximum count");
        require(!restoredPersonal.duplicate("VOIX CLAIRE","Beyond limit"),
                "duplicate exceeded preset limit");
        require(restoredPersonal.rename("VOIX CLAIRE","Voix claire"),
                "rename at capacity rejected");
        require(restoredPersonal.items().size()==LibertyChannelUserPresets::Library::maxPresets,
                "rename at capacity changed count");
        require(equal(presetEngine.getChannelProcessingSettings(0),voicePreset),
                "preset library changed applied DSP");

        // Import/export: two libraries must merge without overwriting names,
        // including conflicting names containing uppercase accents.
        LibertyChannelUserPresets::Library destinationPresets,importPresets;
        require(destinationPresets.upsert(juce::String::fromUTF8("Voix Épurée"),voicePreset),
                "cannot create destination preset");
        require(destinationPresets.setFavorite(juce::String::fromUTF8("VOIX ÉPURÉE"),true),
                "cannot mark destination as favorite");
        require(importPresets.upsert(juce::String::fromUTF8("VOIX ÉPURÉE"),bassPreset),
                "cannot create conflicting import");
        require(importPresets.setFavorite(juce::String::fromUTF8("VOIX ÉPURÉE"),true),
                "cannot mark imported favorite");
        require(importPresets.upsert("Guitare",LibertyChannelInspectorPresets::make(5)),
                "cannot create second imported preset");
        require(destinationPresets.mergeFrom(importPresets),"import merge rejected");
        require(destinationPresets.items().size()==3,"import merge count");
        require(destinationPresets.items()[0].name==juce::String::fromUTF8("Voix Épurée"),
                "import changed original name");
        require(equal(destinationPresets.items()[0].settings,voicePreset),
                "import overwrote existing settings");
        require(destinationPresets.items()[0].favorite
                && destinationPresets.items()[1].favorite,
                "import lost preset favorites");
        require(destinationPresets.items()[1].name
                ==juce::String::fromUTF8("VOIX ÉPURÉE (importé)"),
                "import collision suffix");
        require(equal(destinationPresets.items()[1].settings,bassPreset),
                "import lost colliding settings");
        require(destinationPresets.mergeFrom(importPresets),"repeat import merge");
        require(destinationPresets.items().size()==5,"repeat import count");
        require(destinationPresets.items()[3].name
                ==juce::String::fromUTF8("VOIX ÉPURÉE (importé 2)"),
                "repeated import name collision");
        const auto savedImported=destinationPresets.toXml();
        LibertyChannelUserPresets::Library exportedRoundtrip;
        require(exportedRoundtrip.fromXml(*savedImported),"export/import XML roundtrip");
        require(exportedRoundtrip.items().size()==5,"export/import entry count");
        require(equal(exportedRoundtrip.items()[1].settings,bassPreset),
                "export/import lost preset settings");
        require(exportedRoundtrip.items()[0].favorite
                && exportedRoundtrip.items()[1].favorite
                && exportedRoundtrip.items()[3].favorite,
                "export/import lost favorite metadata");
        // Legacy preset XML has no favorite field, and must default to false.
        juce::XmlElement legacyPresetXml("LibertyChannelUserPresets");
        auto* legacyPresetEntry=legacyPresetXml.createNewChildElement("Preset");
        legacyPresetEntry->setAttribute("name","Legacy");
        LibertyChannelProjectState::write(*legacyPresetEntry,voicePreset);
        LibertyChannelUserPresets::Library legacyPresets;
        require(legacyPresets.fromXml(legacyPresetXml)
                && legacyPresets.items().size()==1
                && !legacyPresets.items()[0].favorite,
                "legacy preset library compatibility");
        // If the imported library exceeds capacity, the existing one remains
        // unchanged rather than receiving a partially imported selection.
        LibertyChannelUserPresets::Library tooMany;
        for(int i=0;i<LibertyChannelUserPresets::Library::maxPresets;++i)
            require(tooMany.upsert("Large "+juce::String(i),voicePreset),
                    "cannot populate oversized import");
        require(!destinationPresets.mergeFrom(tooMany),"oversized import succeeded");
        require(destinationPresets.items().size()==5,"oversized import partly changed list");
        require(equal(presetEngine.getChannelProcessingSettings(0),voicePreset),
                "import/export changed applied DSP");
        auto userDraft=LibertyChannelInspectorState::Session(presetEngine,0);
        userDraft.switchVariant();
        userDraft.setPending(restoredPersonal.items()[0].settings);
        require(equal(presetEngine.getChannelProcessingSettings(0),voicePreset),
                "loading personal preset changed live DSP");
        // A bad XML document must be rejected without destroying the existing list.
        juce::XmlElement badPersonal("OtherPresets");
        require(!restoredPersonal.fromXml(badPersonal),"accepted non-Liberty XML");
        require(restoredPersonal.items().size()==LibertyChannelUserPresets::Library::maxPresets,
                "corrupted preset library on read");

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
