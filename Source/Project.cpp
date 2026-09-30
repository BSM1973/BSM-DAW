#include "MainComponent.h"
#include "PluginHost.h"
#include "OneKnobEffects.h"

int getLibertyTrackColourId(int track);
void setLibertyTrackColourId(int track, int colourId);
void resetLibertyTrackColours();
juce::String getLibertyTrackName(int track);
void setLibertyTrackName(int track, const juce::String& name);
void resetLibertyTrackNames();
void saveLibertyMultiMidiClips(MainComponent&, juce::XmlElement&);
void loadLibertyMultiMidiClips(MainComponent&, const juce::XmlElement&);
void resetLibertyMultiMidiProject(MainComponent&);
void refreshLibertyMixConsole(MainComponent*);
void prepareLibertyAudioRecordingForProjectReset(MainComponent*);

namespace
{
constexpr int menuNew = 1;
constexpr int menuOpen = 2;
constexpr int menuSave = 3;
constexpr int menuSaveAs = 4;

bool prepareTrackProjectMedia(const juce::File& projectFile,
                              int trackIndex,
                              const juce::AudioBuffer<float>* buffer,
                              double sampleRate,
                              juce::File& exportedFile,
                              juce::File& tempFile)
{
    if (buffer == nullptr || buffer->getNumSamples() <= 0 || buffer->getNumChannels() <= 0 || sampleRate <= 0.0)
        return false;

    auto mediaFolder = projectFile.getSiblingFile(projectFile.getFileNameWithoutExtension() + "_Media");
    if (!mediaFolder.createDirectory().wasOk() && !mediaFolder.isDirectory())
        return false;

    exportedFile = mediaFolder.getChildFile("Audio_" + juce::String(trackIndex + 1) + ".wav");
    tempFile = exportedFile.getSiblingFile(exportedFile.getFileName() + ".saving");
    tempFile.deleteFile();

    auto output = tempFile.createOutputStream();
    if (output == nullptr)
        return false;

    juce::WavAudioFormat wav;
    auto writer = std::unique_ptr<juce::AudioFormatWriter>(
        wav.createWriterFor(output.release(), sampleRate, (unsigned int)buffer->getNumChannels(), 24, {}, 0));
    if (writer == nullptr)
    {
        tempFile.deleteFile();
        return false;
    }

    if (!writer->writeFromAudioSampleBuffer(*buffer, 0, buffer->getNumSamples()))
    {
        writer.reset();
        tempFile.deleteFile();
        return false;
    }
    writer.reset();

    return true;
}

struct PreparedProjectMedia
{
    juce::File finalFile;
    juce::File tempFile;
    juce::File backupFile;
    bool hadOriginal = false;
    bool committed = false;
};
}

juce::String MainComponent::getProjectStateSignature() const
{
    const auto appendStateHash = [](juce::String& target, const juce::MemoryBlock& state)
    {
        std::uint64_t hash = 14695981039346656037ull;
        const auto* bytes = static_cast<const std::uint8_t*>(state.getData());
        for (std::size_t i = 0; i < state.getSize(); ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        target << ";stateHash=" << juce::String::toHexString((juce::int64) hash);
    };
    juce::String signature;
    signature << "tempo=" << juce::String(tempoBpm, 6)
              << ";meter=" << timeSignatureNumerator << "/" << timeSignatureDenominator
              << ";master=" << juce::String(audioEngine.getMasterGain(), 6)
              << ";audioTracks=" << getAudioTrackCount()
              << ";midiTracks=" << getMidiTrackCount()
              << ";instrumentTracks=" << getInstrumentTrackCount()
              << ";midiClipStart=" << juce::String(midiClipStartSeconds, 6)
              << ";midiClipLength=" << juce::String(midiClipLengthSeconds, 6)
              << ";midiColour=" << getLibertyTrackColourId(getAudioTrackCount())
              << ";midiName=" << getLibertyTrackName(getAudioTrackCount())
              << ";midiMute=" << (audioEngine.isMidiTrackMuted() ? 1 : 0)
              << ";midiSolo=" << (audioEngine.isMidiTrackSolo() ? 1 : 0)
              << ";instrumentMute=" << (audioEngine.isInstrumentTrackMuted() ? 1 : 0)
              << ";instrumentSolo=" << (audioEngine.isInstrumentTrackSolo() ? 1 : 0);

    for (int i = 0; i < getAudioTrackCount(); ++i)
    {
        signature << "|track=" << i
                  << ";loaded=" << (audioEngine.hasAudioFile(i) ? 1 : 0)
                  << ";source=" << trackSourceFiles[(size_t)i].getFullPathName()
                  << ";name=" << audioEngine.getAudioFileName(i)
                  << ";trackName=" << getLibertyTrackName(i)
                  << ";length=" << juce::String(audioEngine.getAudioFileLengthSeconds(i), 6)
                  << ";start=" << juce::String(audioEngine.getTrackStartSeconds(i), 6)
                  << ";gain=" << juce::String(audioEngine.getTrackGain(i), 6)
                  << ";pan=" << juce::String(audioEngine.getTrackPan(i), 6)
                  << ";mute=" << (audioEngine.isTrackMuted(i) ? 1 : 0)
                  << ";solo=" << (audioEngine.isTrackSolo(i) ? 1 : 0)
                  << ";colour=" << getLibertyTrackColourId(i);
    }

    for (int i = 0; i < getMidiTrackCount(); ++i)
    {
        const int logical = getAudioTrackCount() + i;
        signature << "|midiTrack=" << i
                  << ";name=" << getLibertyTrackName(logical)
                  << ";colour=" << getLibertyTrackColourId(logical);
    }

    for (int i = 0; i < getInstrumentTrackCount(); ++i)
    {
        const int logical = getAudioTrackCount() + getMidiTrackCount() + i;
        signature << "|instrument=" << i
                  << ";name=" << getLibertyTrackName(logical)
                  << ";colour=" << getLibertyTrackColourId(logical)
                  << ";gain=" << juce::String(audioEngine.getInstrumentTrackGain(i), 6)
                  << ";pan=" << juce::String(audioEngine.getInstrumentTrackPan(i), 6)
                  << ";mute=" << (audioEngine.isInstrumentTrackMuted(i) ? 1 : 0)
                  << ";solo=" << (audioEngine.isInstrumentTrackSolo(i) ? 1 : 0);
    }

    {
        auto& host = LibertyPluginHost::instance();
        auto& one = LibertyOneKnobManager::instance();
        for (int lane = 0; lane < getAudioTrackCount(); ++lane)
            for (int slot = 0; slot < LibertyPluginHost::effectSlotsPerTrack; ++slot)
            {
                juce::PluginDescription d;
                signature << "|afx=" << lane << ',' << slot << ',';
                if (host.getEffectDescriptionForTrackSlot(lane, slot, d))
                {
                    signature << d.fileOrIdentifier;
                    juce::MemoryBlock state;
                    if (host.getEffectStateForTrackSlot(lane, slot, state)) appendStateHash(signature, state);
                }
                const int key = lane * 8 + slot;
                if (one.hasEffect(key)) signature << ";ok=" << (int)one.getEffect(key) << ',' << juce::String(one.getAmount(key), 6);
            }
        for (int lane = 0; lane < getInstrumentTrackCount(); ++lane)
        {
            juce::PluginDescription instrument;
            signature << "|instPlugin=" << lane << ',';
            if (host.getInstrumentDescriptionForTrack(lane, instrument))
            {
                signature << instrument.fileOrIdentifier;
                juce::MemoryBlock state;
                if (host.getInstrumentStateForTrack(lane, state)) appendStateHash(signature, state);
            }
            for (int slot = 0; slot < LibertyPluginHost::effectSlotsPerTrack; ++slot)
            {
                juce::PluginDescription d;
                signature << "|ifx=" << lane << ',' << slot << ',';
                if (host.getEffectDescriptionForInstrumentTrackSlot(lane, slot, d))
                {
                    signature << d.fileOrIdentifier;
                    juce::MemoryBlock state;
                    if (host.getEffectStateForInstrumentTrackSlot(lane, slot, state)) appendStateHash(signature, state);
                }
                const int key = 100000 + lane * 8 + slot;
                if (one.hasEffect(key)) signature << ";ok=" << (int)one.getEffect(key) << ',' << juce::String(one.getAmount(key), 6);
            }
        }
    }

    signature << "|midi=";
    for (const auto& note : midiEngine.getNotesCopy())
        signature << note.startTick << ',' << note.lengthTicks << ',' << (int)note.pitch << ',' << (int)note.velocity << ',' << (int)note.channel << ';';

    return signature;
}

void MainComponent::initializeProjectTracking()
{
    savedProjectStateSignature = getProjectStateSignature();
}

void MainComponent::markProjectClean()
{
    savedProjectStateSignature = getProjectStateSignature();
}

bool MainComponent::hasUnsavedChanges() const
{
    return getProjectStateSignature() != savedProjectStateSignature;
}

void MainComponent::confirmBeforeProjectAction(std::function<void()> action)
{
    if (!hasUnsavedChanges())
    {
        action();
        return;
    }

    pendingProjectAction = std::move(action);

    juce::AlertWindow::showYesNoCancelBox(
        juce::MessageBoxIconType::WarningIcon,
        "Liberty - Unsaved Changes",
        "The current project has unsaved changes.",
        "SAVE",
        "DON'T SAVE",
        "CANCEL",
        this,
        juce::ModalCallbackFunction::create([this](int result)
        {
            if (result == 0)
            {
                pendingProjectAction = {};
                return;
            }

            if (result == 2)
            {
                auto next = std::move(pendingProjectAction);
                pendingProjectAction = {};
                if (next)
                    next();
                return;
            }

            if (currentProjectFile.existsAsFile())
            {
                if (saveProjectToFile(currentProjectFile))
                {
                    auto next = std::move(pendingProjectAction);
                    pendingProjectAction = {};
                    if (next)
                        next();
                }
            }
            else
            {
                saveProjectAs();
            }
        }));
}

void MainComponent::requestClose(std::function<void(bool)> completion)
{
    if (!hasUnsavedChanges())
    {
        completion(true);
        return;
    }

    juce::AlertWindow::showYesNoCancelBox(
        juce::MessageBoxIconType::WarningIcon,
        "Liberty - Unsaved Changes",
        "The current project has unsaved changes.",
        "SAVE",
        "DON'T SAVE",
        "CANCEL",
        this,
        juce::ModalCallbackFunction::create([this, completion = std::move(completion)](int result) mutable
        {
            if (result == 0)
                return;

            if (result == 2)
            {
                completion(true);
                return;
            }

            if (currentProjectFile.existsAsFile())
            {
                if (saveProjectToFile(currentProjectFile))
                    completion(true);
            }
            else
            {
                pendingProjectAction = [completion = std::move(completion)]() mutable
                {
                    completion(true);
                };
                saveProjectAs();
            }
        }));
}

void MainComponent::showProjectMenu()
{
    juce::PopupMenu menu;
    menu.addItem(menuNew, "New Project");
    menu.addItem(menuOpen, "Open Project...");
    menu.addSeparator();
    menu.addItem(menuSave, "Save Project");
    menu.addItem(menuSaveAs, "Save Project As...");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(projectButton.get()),
                       [this](int result)
                       {
                           switch (result)
                           {
                               case menuNew: newProject(); break;
                               case menuOpen: openProject(); break;
                               case menuSave: saveProject(); break;
                               case menuSaveAs: saveProjectAs(); break;
                               default: break;
                           }
                       });
}

void MainComponent::resetProjectState()
{
    prepareLibertyAudioRecordingForProjectReset(this);
    resetLibertyMultiMidiProject(*this);
    audioEngine.setPlaying(false);
    audioEngine.resetTransport();
    audioEngine.setProjectExtraLengthSeconds(0.0);
    isPlaying = false;
    playheadSeconds = 0.0;
    tempoBpm = 120.0;
    timeSignatureNumerator = 4;
    timeSignatureDenominator = 4;
    selectedTrack = 0;
    midiClipStartSeconds = 0.0;
    midiClipLengthSeconds = 2.0;
    midiClipLengthUserDefined = false;
    audioEngine.setMasterGain(1.0f);
    audioEngine.setMidiTrackMuted(false);
    audioEngine.setMidiTrackSolo(false);
    audioEngine.setInstrumentTrackMuted(false);
    audioEngine.setInstrumentTrackSolo(false);
    LibertyPluginHost::instance().clearProjectPlugins();
    LibertyOneKnobManager::instance().clearAllEffects();
    midiEngine.clear();
    audioEngine.resetInstrumentPlayback(1);
    resetLibertyTrackColours();
    resetLibertyTrackNames();

    while (audioEngine.getAudioTrackCount() > AudioEngine::initialAudioTracks)
        audioEngine.removeAudioTrack(audioEngine.getAudioTrackCount() - 1);
    dynamicMidiTrackCount = 1;
    dynamicInstrumentTrackCount = 1;
    trackScrollRows = 0;
    waveformMin.resize((size_t)AudioEngine::initialAudioTracks);
    waveformMax.resize((size_t)AudioEngine::initialAudioTracks);
    trackSourceFiles.resize((size_t)AudioEngine::initialAudioTracks);

    for (int i = 0; i < AudioEngine::initialAudioTracks; ++i)
    {
        audioEngine.clearAudioTrack(i);
        audioEngine.setTrackGain(i, 1.0f);
        audioEngine.setTrackPan(i, 0.0f);
        audioEngine.setTrackMuted(i, false);
        audioEngine.setTrackSolo(i, false);
        trackSourceFiles[(size_t)i] = juce::File{};
        waveformMin[(size_t)i].clear();
        waveformMax[(size_t)i].clear();
    }

    tempoControls.refresh();
    refreshLibertyMixConsole(this);
    repaint();
}

void MainComponent::newProject()
{
    confirmBeforeProjectAction([this]
    {
        resetProjectState();
        currentProjectFile = juce::File{};
        markProjectClean();
    });
}

void MainComponent::openProject()
{
    projectFileChooser = std::make_unique<juce::FileChooser>(
        "Open Liberty Project", juce::File{}, "*.bsmproj");

    projectFileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();
            if (!file.existsAsFile()) return;

            confirmBeforeProjectAction([this, file]
            {
                loadProjectFromFile(file);
            });
        });
}

void MainComponent::saveProject()
{
    if (currentProjectFile.existsAsFile())
    {
        saveProjectToFile(currentProjectFile);
        return;
    }

    saveProjectAs();
}

void MainComponent::saveProjectAs()
{
    projectFileChooser = std::make_unique<juce::FileChooser>(
        "Save Liberty Project", currentProjectFile, "*.bsmproj");

    projectFileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();
            if (file == juce::File{})
            {
                pendingProjectAction = {};
                return;
            }
            if (file.getFileExtension().isEmpty())
                file = file.withFileExtension("bsmproj");
            if (saveProjectToFile(file))
            {
                currentProjectFile = file;
                markProjectClean();

                auto next = std::move(pendingProjectAction);
                pendingProjectAction = {};
                if (next)
                    next();
            }
        });
}

bool MainComponent::saveProjectToFile(const juce::File& file)
{
    if (file == juce::File{}) return false;

    std::vector<PreparedProjectMedia> preparedMedia;

    juce::XmlElement project("LibertyProject");
    project.setAttribute("version", 15);
    project.setAttribute("audioTrackCount", getAudioTrackCount());
    project.setAttribute("midiTrackCount", getMidiTrackCount());
    project.setAttribute("instrumentTrackCount", getInstrumentTrackCount());
    project.setAttribute("tempo", tempoBpm);
    project.setAttribute("timeSignatureNumerator", timeSignatureNumerator);
    project.setAttribute("timeSignatureDenominator", timeSignatureDenominator);
    project.setAttribute("selectedTrack", selectedTrack);
    project.setAttribute("playheadSeconds", playheadSeconds);
    project.setAttribute("masterGain", (double)audioEngine.getMasterGain());
    project.setAttribute("midiClipStartSeconds", midiClipStartSeconds);
    project.setAttribute("midiClipLengthSeconds", midiClipLengthSeconds);
    project.setAttribute("midiColourId", getLibertyTrackColourId(getAudioTrackCount()));
    project.setAttribute("instrumentColourId", getLibertyTrackColourId(getAudioTrackCount() + getMidiTrackCount()));
    project.setAttribute("midiTrackName", getLibertyTrackName(getAudioTrackCount()));
    project.setAttribute("instrumentTrackName", getLibertyTrackName(getAudioTrackCount() + getMidiTrackCount()));
    project.setAttribute("midiMuted", audioEngine.isMidiTrackMuted());
    project.setAttribute("midiSolo", audioEngine.isMidiTrackSolo());
    project.setAttribute("instrumentMuted", audioEngine.isInstrumentTrackMuted());
    project.setAttribute("instrumentSolo", audioEngine.isInstrumentTrackSolo());

    auto* midi = project.createNewChildElement("MIDI");
    midi->setAttribute("ticksPerQuarterNote", (int)MidiEngine::ticksPerQuarterNote);
    midi->setAttribute("track", 0);
    for (const auto& note : midiEngine.getNotesCopy())
    {
        auto* noteElement = midi->createNewChildElement("Note");
        noteElement->setAttribute("startTick", (double)note.startTick);
        noteElement->setAttribute("lengthTicks", (double)note.lengthTicks);
        noteElement->setAttribute("pitch", (int)note.pitch);
        noteElement->setAttribute("velocity", (int)note.velocity);
        noteElement->setAttribute("channel", (int)note.channel);
    }

    for (int i = 0; i < getAudioTrackCount(); ++i)
    {
        auto* track = project.createNewChildElement("Track");
        track->setAttribute("index", i);
        track->setAttribute("loaded", audioEngine.hasAudioFile(i));
        track->setAttribute("colourId", getLibertyTrackColourId(i));
        track->setAttribute("trackName", getLibertyTrackName(i));

        juce::File sourceFile = trackSourceFiles[(size_t)i];
        const auto* buffer = audioEngine.getAudioBuffer(i);

        if (audioEngine.hasAudioFile(i) && buffer != nullptr)
        {
            juce::File exportedFile, preparedFile;
            if (!prepareTrackProjectMedia(file, i, buffer, audioEngine.getSampleRate(), exportedFile, preparedFile))
            {
                for (auto& media : preparedMedia) media.tempFile.deleteFile();
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                       "Liberty - Project Save",
                                                       "Could not safely export Audio " + juce::String(i + 1)
                                                           + " to the project media folder. The project was not saved.",
                                                       "OK");
                return false;
            }
            preparedMedia.push_back({ exportedFile, preparedFile, {}, false, false });
            sourceFile = exportedFile;
        }

        track->setAttribute("sourceFile", sourceFile.getFullPathName());
        track->setAttribute("fileName", audioEngine.getAudioFileName(i));
        track->setAttribute("startSeconds", audioEngine.getTrackStartSeconds(i));
        track->setAttribute("lengthSeconds", audioEngine.getAudioFileLengthSeconds(i));
        track->setAttribute("gain", (double)audioEngine.getTrackGain(i));
        track->setAttribute("pan", (double)audioEngine.getTrackPan(i));
        track->setAttribute("muted", audioEngine.isTrackMuted(i));
        track->setAttribute("solo", audioEngine.isTrackSolo(i));
    }

    {
        auto* trackMetadata = project.createNewChildElement("DynamicTrackMetadata");
        for (int i = 0; i < getMidiTrackCount(); ++i)
        {
            const int logical = getAudioTrackCount() + i;
            auto* track = trackMetadata->createNewChildElement("Track");
            track->setAttribute("kind", "midi");
            track->setAttribute("index", i);
            track->setAttribute("name", getLibertyTrackName(logical));
            track->setAttribute("colourId", getLibertyTrackColourId(logical));
        }
        for (int i = 0; i < getInstrumentTrackCount(); ++i)
        {
            const int logical = getAudioTrackCount() + getMidiTrackCount() + i;
            auto* track = trackMetadata->createNewChildElement("Track");
            track->setAttribute("kind", "instrument");
            track->setAttribute("index", i);
            track->setAttribute("name", getLibertyTrackName(logical));
            track->setAttribute("colourId", getLibertyTrackColourId(logical));
        }
    }

    {
        auto* instrumentMixer = project.createNewChildElement("InstrumentMixer");
        for (int i = 0; i < getInstrumentTrackCount(); ++i)
        {
            auto* track = instrumentMixer->createNewChildElement("Track");
            track->setAttribute("index", i);
            track->setAttribute("gain", (double) audioEngine.getInstrumentTrackGain(i));
            track->setAttribute("pan", (double) audioEngine.getInstrumentTrackPan(i));
            track->setAttribute("muted", audioEngine.isInstrumentTrackMuted(i));
            track->setAttribute("solo", audioEngine.isInstrumentTrackSolo(i));
        }
    }

    {
        auto* inserts = project.createNewChildElement("DynamicInserts");
        auto& host = LibertyPluginHost::instance();
        auto& one = LibertyOneKnobManager::instance();
        for(int i=0;i<getAudioTrackCount();++i){
            for(int slot=0;slot<LibertyPluginHost::effectSlotsPerTrack;++slot){
                juce::PluginDescription d;
                if(host.getEffectDescriptionForTrackSlot(i,slot,d)){auto*e=inserts->createNewChildElement("Plugin");e->setAttribute("kind","audioFX");e->setAttribute("lane",i);e->setAttribute("slot",slot);e->setAttribute("identifier",d.fileOrIdentifier);juce::MemoryBlock state;if(host.getEffectStateForTrackSlot(i,slot,state)&&state.getSize()>0)e->setAttribute("state",state.toBase64Encoding());}
                const int key=i*8+slot;if(one.hasEffect(key)){auto*e=inserts->createNewChildElement("OneKnob");e->setAttribute("kind","audio");e->setAttribute("lane",i);e->setAttribute("slot",slot);e->setAttribute("type",(int)one.getEffect(key));e->setAttribute("amount",(double)one.getAmount(key));}
            }
        }
        for(int i=0;i<getInstrumentTrackCount();++i){
            juce::PluginDescription d;
            if(host.getInstrumentDescriptionForTrack(i,d)){auto*e=inserts->createNewChildElement("Plugin");e->setAttribute("kind","instrument");e->setAttribute("lane",i);e->setAttribute("identifier",d.fileOrIdentifier);juce::MemoryBlock state;if(host.getInstrumentStateForTrack(i,state)&&state.getSize()>0)e->setAttribute("state",state.toBase64Encoding());}
            for(int slot=0;slot<LibertyPluginHost::effectSlotsPerTrack;++slot){
                if(host.getEffectDescriptionForInstrumentTrackSlot(i,slot,d)){auto*e=inserts->createNewChildElement("Plugin");e->setAttribute("kind","instrumentFX");e->setAttribute("lane",i);e->setAttribute("slot",slot);e->setAttribute("identifier",d.fileOrIdentifier);juce::MemoryBlock state;if(host.getEffectStateForInstrumentTrackSlot(i,slot,state)&&state.getSize()>0)e->setAttribute("state",state.toBase64Encoding());}
                const int key=100000+i*8+slot;if(one.hasEffect(key)){auto*e=inserts->createNewChildElement("OneKnob");e->setAttribute("kind","instrument");e->setAttribute("lane",i);e->setAttribute("slot",slot);e->setAttribute("type",(int)one.getEffect(key));e->setAttribute("amount",(double)one.getAmount(key));}
            }
        }
    }

    saveLibertyMultiMidiClips(*this, project);

    const auto tempFile = file.getSiblingFile(file.getFileName() + ".saving");
    tempFile.deleteFile();

    {
        auto output = tempFile.createOutputStream();
        if (output == nullptr)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Liberty - Project Save",
                                                   "Could not create the temporary project file.",
                                                   "OK");
            return false;
        }

        const auto xmlText = project.toString();
        if (!output->writeText(xmlText, false, false, "UTF-8"))
        {
            output.reset();
            tempFile.deleteFile();
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Liberty - Project Save",
                                                   "Could not write the Liberty project data.",
                                                   "OK");
            return false;
        }

        output->flush();
    }

    auto verifiedProject = juce::parseXML(tempFile);
    if (verifiedProject == nullptr || verifiedProject->getTagName() != "LibertyProject")
    {
        tempFile.deleteFile();
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               "Liberty - Project Save",
                                               "The temporary project file could not be verified. The previous project was left unchanged.",
                                               "OK");
        return false;
    }

    const auto rollbackMedia = [&preparedMedia]()
    {
        bool restoredAll = true;
        for (auto it = preparedMedia.rbegin(); it != preparedMedia.rend(); ++it)
        {
            bool restored = true;
            if (it->committed)
            {
                const bool finalRemoved = !it->finalFile.existsAsFile() || it->finalFile.deleteFile();
                if (!finalRemoved)
                {
                    restored = false;
                }
                else if (it->hadOriginal && it->backupFile.existsAsFile())
                {
                    restored = it->backupFile.moveFileTo(it->finalFile);
                }
            }
            it->tempFile.deleteFile();

            // Never destroy the last recoverable copy if the filesystem refused
            // to restore it. A leftover .backup is preferable to lost project audio.
            if (restored && it->backupFile.existsAsFile())
                it->backupFile.deleteFile();
            restoredAll = restoredAll && restored;
        }
        return restoredAll;
    };

    for (auto& media : preparedMedia)
    {
        media.hadOriginal = media.finalFile.existsAsFile();
        media.backupFile = media.finalFile.getSiblingFile(media.finalFile.getFileName() + ".backup");
        media.backupFile.deleteFile();
        if (media.hadOriginal && !media.finalFile.moveFileTo(media.backupFile))
        {
            const bool rollbackComplete = rollbackMedia();
            tempFile.deleteFile();
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Liberty - Project Save",
                                                   rollbackComplete
                                                       ? "Could not protect the previous project audio. The project was not saved."
                                                       : "Project audio rollback was incomplete. Recovery .backup files were preserved in the project media folder.",
                                                   "OK");
            return false;
        }
        if (!media.tempFile.moveFileTo(media.finalFile))
        {
            // The original may already have been moved to .backup even though
            // the new media was not committed. Let the central rollback restore it
            // and report any restoration failure consistently.
            media.committed = media.hadOriginal;
            const bool rollbackComplete = rollbackMedia();
            tempFile.deleteFile();
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Liberty - Project Save",
                                                   rollbackComplete
                                                       ? "Could not commit the project audio. The previous project was left unchanged."
                                                       : "Project audio rollback was incomplete. Recovery .backup files were preserved in the project media folder.",
                                                   "OK");
            return false;
        }
        media.committed = true;
    }

    if (!tempFile.replaceFileIn(file))
    {
        const bool rollbackComplete = rollbackMedia();
        tempFile.deleteFile();
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               "Liberty - Project Save",
                                               rollbackComplete
                                                   ? "Could not replace the existing project file. The previous project was left unchanged."
                                                   : "Project file replacement failed and audio rollback was incomplete. Recovery .backup files were preserved in the project media folder.",
                                               "OK");
        return false;
    }

    for (auto& media : preparedMedia)
        media.backupFile.deleteFile();

    currentProjectFile = file;
    markProjectClean();
    return true;
}

bool MainComponent::loadProjectFromFile(const juce::File& file)
{
    auto project = juce::parseXML(file);
    if (project == nullptr || project->getTagName() != "LibertyProject")
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               "Liberty - Project Open",
                                               "This is not a valid Liberty project file.",
                                               "OK");
        return false;
    }

    constexpr int currentProjectVersion = 15;
    const int projectVersion = project->getIntAttribute("version", 1);
    if (projectVersion < 1 || projectVersion > currentProjectVersion)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               "Liberty - Project Open",
                                               projectVersion > currentProjectVersion
                                                   ? "This project was created by a newer version of Liberty and cannot be opened safely."
                                                   : "This Liberty project version is invalid.",
                                               "OK");
        return false;
    }

    resetProjectState();

    constexpr int maxRestoredTracksPerType = 512;
    const int savedAudioTracks = juce::jlimit(AudioEngine::initialAudioTracks, maxRestoredTracksPerType,
                                              project->getIntAttribute("audioTrackCount", AudioEngine::initialAudioTracks));
    const int savedMidiTracks = juce::jlimit(1, maxRestoredTracksPerType,
                                             project->getIntAttribute("midiTrackCount", 1));
    const int savedInstrumentTracks = juce::jlimit(1, maxRestoredTracksPerType,
                                                   project->getIntAttribute("instrumentTrackCount", 1));
    while (getAudioTrackCount() < savedAudioTracks) addAudioTrack();
    while (getMidiTrackCount() < savedMidiTracks) addMidiTrack();
    while (getInstrumentTrackCount() < savedInstrumentTracks) addInstrumentTrack();
    audioEngine.resetInstrumentPlayback(savedInstrumentTracks);
    trackScrollRows = 0;

    const auto finiteOr = [](double value, double fallback) noexcept
    {
        return std::isfinite(value) ? value : fallback;
    };
    tempoBpm = juce::jlimit(20.0, 400.0, finiteOr(project->getDoubleAttribute("tempo", 120.0), 120.0));
    timeSignatureNumerator = juce::jlimit(1, 32, project->getIntAttribute("timeSignatureNumerator", 4));
    timeSignatureDenominator = juce::jlimit(1, 32, project->getIntAttribute("timeSignatureDenominator", 4));
    selectedTrack = juce::jlimit(0, juce::jmax(0, getTotalArrangeTrackCount() - 1), project->getIntAttribute("selectedTrack", 0));
    playheadSeconds = juce::jmax(0.0, finiteOr(project->getDoubleAttribute("playheadSeconds", 0.0), 0.0));
    audioEngine.setMasterGain(juce::jlimit(0.0f, 4.0f, (float) finiteOr(project->getDoubleAttribute("masterGain", 1.0), 1.0)));
    midiClipStartSeconds = juce::jmax(0.0, finiteOr(project->getDoubleAttribute("midiClipStartSeconds", 0.0), 0.0));
    setMidiClipLengthFromProject(juce::jmax(0.001, finiteOr(project->getDoubleAttribute("midiClipLengthSeconds", 2.0), 2.0)));
    setLibertyTrackColourId(getAudioTrackCount(), project->getIntAttribute("midiColourId", 0));
    setLibertyTrackColourId(getAudioTrackCount() + getMidiTrackCount(), project->getIntAttribute("instrumentColourId", 0));
    setLibertyTrackName(getAudioTrackCount(), project->getStringAttribute("midiTrackName", "MIDI 1"));
    setLibertyTrackName(getAudioTrackCount() + getMidiTrackCount(), project->getStringAttribute("instrumentTrackName", "Instrument 1"));
    audioEngine.setMidiTrackMuted(project->getBoolAttribute("midiMuted", false));
    audioEngine.setMidiTrackSolo(project->getBoolAttribute("midiSolo", false));
    audioEngine.setInstrumentTrackMuted(project->getBoolAttribute("instrumentMuted", false));
    audioEngine.setInstrumentTrackSolo(project->getBoolAttribute("instrumentSolo", false));

    if (auto* midi = project->getChildByName("MIDI"))
    {
        midiEngine.clear();
        constexpr std::size_t maxVisitedLegacyMidiElements = 32768;
        std::size_t visitedLegacyMidiElements = 0;
        for (auto* noteElement = midi->getFirstChildElement(); noteElement != nullptr; noteElement = noteElement->getNextElement())
        {
            if (++visitedLegacyMidiElements > maxVisitedLegacyMidiElements) break;
            if (noteElement->getTagName() != "Note") continue;
            const double rawStartTick = noteElement->getDoubleAttribute("startTick", 0.0);
            const double rawLengthTicks = noteElement->getDoubleAttribute("lengthTicks", (double) MidiEngine::ticksPerQuarterNote);
            if (!std::isfinite(rawStartTick) || !std::isfinite(rawLengthTicks)
                || rawStartTick < 0.0 || rawLengthTicks <= 0.0
                || rawStartTick > (double) std::numeric_limits<std::int64_t>::max()
                || rawLengthTicks > (double) std::numeric_limits<std::int64_t>::max())
                continue;
            const auto restoredStartTick = (std::int64_t) std::llround(rawStartTick);
            const auto restoredLengthTicks = (std::int64_t) std::llround(rawLengthTicks);
            if (restoredStartTick > std::numeric_limits<std::int64_t>::max() - restoredLengthTicks)
                continue;
            midiEngine.addNote(
                restoredStartTick,
                restoredLengthTicks,
                noteElement->getIntAttribute("pitch", 60),
                noteElement->getIntAttribute("velocity", 100),
                noteElement->getIntAttribute("channel", 1));
        }
    }

    juce::String missingFiles;
    juce::String missingPlugins;
    juce::String pluginStateWarnings;
    constexpr int maxProjectDiagnosticChars = 32768;
    const auto appendProjectDiagnostic = [](juce::String& target, const juce::String& message)
    {
        static constexpr auto omitted = "... additional messages omitted\n";
        if (target.endsWith(omitted)) return;
        if (target.length() + message.length() + 1 <= maxProjectDiagnosticChars)
        {
            target << message << "\n";
            return;
        }
        const int keep = juce::jmax(0, maxProjectDiagnosticChars - (int) juce::String(omitted).length());
        target = target.substring(0, keep);
        target << omitted;
    };
    constexpr std::size_t maxVisitedProjectTrackElements = 4096;
    std::size_t visitedProjectTrackElements = 0;
    for (auto* track = project->getFirstChildElement(); track != nullptr; track = track->getNextElement())
    {
        if (++visitedProjectTrackElements > maxVisitedProjectTrackElements) break;
        if (track->getTagName() != "Track") continue;
        const int index = track->getIntAttribute("index", -1);
        if (index < 0 || index >= getAudioTrackCount()) continue;
        setLibertyTrackColourId(index, track->getIntAttribute("colourId", 0));
        setLibertyTrackName(index, track->getStringAttribute("trackName", "Audio " + juce::String(index + 1)));
        audioEngine.setTrackMuted(index, track->getBoolAttribute("muted", false));
        audioEngine.setTrackSolo(index, track->getBoolAttribute("solo", false));
        if (!track->getBoolAttribute("loaded", false)) continue;

        const auto sourcePath = track->getStringAttribute("sourceFile");
        juce::File sourceFile(sourcePath);

        if (!sourceFile.existsAsFile() && sourcePath.isNotEmpty())
            sourceFile = file.getParentDirectory().getChildFile(sourcePath);

        if (!sourceFile.existsAsFile())
        {
            const auto projectMediaFile = file.getSiblingFile(file.getFileNameWithoutExtension() + "_Media")
                                              .getChildFile("Audio_" + juce::String(index + 1) + ".wav");
            if (projectMediaFile.existsAsFile())
                sourceFile = projectMediaFile;
        }

        if (!sourceFile.existsAsFile())
        {
            appendProjectDiagnostic(missingFiles, "Audio " + juce::String(index + 1) + ": " + sourcePath);
            continue;
        }

        juce::String error;
        if (!audioEngine.loadAudioFileIntoTrack(index, sourceFile, error))
        {
            appendProjectDiagnostic(missingFiles, "Audio " + juce::String(index + 1) + ": " + error);
            continue;
        }

        trackSourceFiles[(size_t)index] = sourceFile;
        audioEngine.setTrackStartSeconds(index, juce::jmax(0.0, finiteOr(track->getDoubleAttribute("startSeconds", 0.0), 0.0)));
        audioEngine.setTrackGain(index, juce::jlimit(0.0f, 4.0f, (float) finiteOr(track->getDoubleAttribute("gain", 1.0), 1.0)));
        audioEngine.setTrackPan(index, juce::jlimit(-1.0f, 1.0f, (float) finiteOr(track->getDoubleAttribute("pan", 0.0), 0.0)));
        audioEngine.setTrackMuted(index, track->getBoolAttribute("muted", false));
        audioEngine.setTrackSolo(index, track->getBoolAttribute("solo", false));
        rebuildWaveformCache(index);
    }

    if (auto* trackMetadata = project->getChildByName("DynamicTrackMetadata"))
    {
        constexpr std::size_t maxVisitedTrackMetadataElements = 2048;
        std::size_t visitedTrackMetadataElements = 0;
        for (auto* track = trackMetadata->getFirstChildElement(); track != nullptr; track = track->getNextElement())
        {
            if (++visitedTrackMetadataElements > maxVisitedTrackMetadataElements) break;
            if (track->getTagName() != "Track") continue;
            const int index = track->getIntAttribute("index", -1);
            const auto kind = track->getStringAttribute("kind");
            int logical = -1;
            if (kind == "midi" && index >= 0 && index < getMidiTrackCount())
                logical = getAudioTrackCount() + index;
            else if (kind == "instrument" && index >= 0 && index < getInstrumentTrackCount())
                logical = getAudioTrackCount() + getMidiTrackCount() + index;
            if (logical < 0) continue;
            setLibertyTrackName(logical, track->getStringAttribute("name"));
            setLibertyTrackColourId(logical, track->getIntAttribute("colourId", 0));
        }
    }

    if (auto* instrumentMixer = project->getChildByName("InstrumentMixer"))
    {
        constexpr std::size_t maxVisitedInstrumentMixerElements = 1024;
        std::size_t visitedInstrumentMixerElements = 0;
        for (auto* track = instrumentMixer->getFirstChildElement(); track != nullptr; track = track->getNextElement())
        {
            if (++visitedInstrumentMixerElements > maxVisitedInstrumentMixerElements) break;
            if (track->getTagName() != "Track") continue;
            const int index = track->getIntAttribute("index", -1);
            if (index < 0 || index >= getInstrumentTrackCount()) continue;
            audioEngine.setInstrumentTrackGain(index, juce::jlimit(0.0f, 4.0f, (float) finiteOr(track->getDoubleAttribute("gain", 1.0), 1.0)));
            audioEngine.setInstrumentTrackPan(index, juce::jlimit(-1.0f, 1.0f, (float) finiteOr(track->getDoubleAttribute("pan", 0.0), 0.0)));
            audioEngine.setInstrumentTrackMuted(index, track->getBoolAttribute("muted", false));
            audioEngine.setInstrumentTrackSolo(index, track->getBoolAttribute("solo", false));
        }
    }

    if(auto* inserts=project->getChildByName("DynamicInserts")){
        auto& host=LibertyPluginHost::instance(); auto& one=LibertyOneKnobManager::instance();
        constexpr std::size_t maxVisitedDynamicInsertElements = 16384;
        std::size_t visitedDynamicInsertElements = 0;
        std::set<juce::String> restoredInsertLocations;
        for(auto*e=inserts->getFirstChildElement();e;e=e->getNextElement()){
            if (++visitedDynamicInsertElements > maxVisitedDynamicInsertElements) break;
            const int lane=e->getIntAttribute("lane",-1); const auto kind=e->getStringAttribute("kind");
            const int insertSlot=e->getIntAttribute("slot",-1);
            const bool validInsertSlot=insertSlot>=0&&insertSlot<LibertyPluginHost::effectSlotsPerTrack;
            juce::String locationKey;
            if(e->getTagName()=="Plugin"&&kind=="instrument"&&lane>=0&&lane<getInstrumentTrackCount())locationKey="instrument:"+juce::String(lane);
            else if(e->getTagName()=="Plugin"&&kind=="audioFX"&&lane>=0&&lane<getAudioTrackCount()&&validInsertSlot)locationKey="audioSlot:"+juce::String(lane)+":"+juce::String(insertSlot);
            else if(e->getTagName()=="Plugin"&&kind=="instrumentFX"&&lane>=0&&lane<getInstrumentTrackCount()&&validInsertSlot)locationKey="instrumentSlot:"+juce::String(lane)+":"+juce::String(insertSlot);
            else if(e->getTagName()=="OneKnob"&&kind=="audio"&&lane>=0&&lane<getAudioTrackCount()&&validInsertSlot)locationKey="audioSlot:"+juce::String(lane)+":"+juce::String(insertSlot);
            else if(e->getTagName()=="OneKnob"&&kind=="instrument"&&lane>=0&&lane<getInstrumentTrackCount()&&validInsertSlot)locationKey="instrumentSlot:"+juce::String(lane)+":"+juce::String(insertSlot);
            if(locationKey.isNotEmpty()&&restoredInsertLocations.count(locationKey)>0)continue;
            if(e->getTagName()=="Plugin"){if(locationKey.isEmpty())continue;const auto identifier=e->getStringAttribute("identifier");juce::PluginDescription d;if(host.findKnownPluginByIdentifier(identifier,d)){juce::String error;bool loaded=false;if(kind=="instrument"&&lane<getInstrumentTrackCount())loaded=host.loadInstrumentForTrack(lane,d,error);else if(kind=="audioFX"&&lane<getAudioTrackCount())loaded=host.loadEffectForTrackSlot(lane,insertSlot,d,error);else if(kind=="instrumentFX"&&lane<getInstrumentTrackCount())loaded=host.loadEffectForInstrumentTrackSlot(lane,insertSlot,d,error);if(!loaded&&error.isNotEmpty())appendProjectDiagnostic(missingPlugins, identifier + " : " + error);if(loaded&&locationKey.isNotEmpty())restoredInsertLocations.insert(locationKey);if(loaded&&e->hasAttribute("state")){constexpr int maxSavedPluginStateBase64Chars=96*1024*1024;const auto savedState=e->getStringAttribute("state");if(savedState.length()>maxSavedPluginStateBase64Chars){appendProjectDiagnostic(pluginStateWarnings, identifier + " (saved state too large)");}else{juce::MemoryBlock state;const bool decoded=state.fromBase64Encoding(savedState);if(decoded&&state.getSize()>0&&state.getSize()<=64u*1024u*1024u){bool restored=false;if(kind=="instrument")restored=host.setInstrumentStateForTrack(lane,state);else if(kind=="audioFX")restored=host.setEffectStateForTrackSlot(lane,insertSlot,state);else if(kind=="instrumentFX")restored=host.setEffectStateForInstrumentTrackSlot(lane,insertSlot,state);if(!restored)appendProjectDiagnostic(pluginStateWarnings, identifier + " (state could not be applied)");}else appendProjectDiagnostic(pluginStateWarnings, identifier + " (invalid saved state)");}}}else if(identifier.isNotEmpty())appendProjectDiagnostic(missingPlugins, identifier + " (not found)");}
            else if(e->getTagName()=="OneKnob"){
                const bool validAudio = kind=="audio" && lane>=0 && lane<getAudioTrackCount() && validInsertSlot;
                const bool validInstrument = kind=="instrument" && lane>=0 && lane<getInstrumentTrackCount() && validInsertSlot;
                if(validAudio || validInstrument){
                    const int key=validInstrument?100000+lane*8+insertSlot:lane*8+insertSlot;
                    constexpr int lastOneKnobType = (int) LibertyOneKnobRack::Type::softClip;
                    const int savedType = juce::jlimit((int) LibertyOneKnobRack::Type::none,
                                                       lastOneKnobType,
                                                       e->getIntAttribute("type", 0));
                    const float savedAmount = juce::jlimit(0.0f, 1.0f,
                                                           (float) finiteOr(e->getDoubleAttribute("amount", 0.5), 0.5));
                    one.setEffect(key, (LibertyOneKnobRack::Type) savedType);
                    one.setAmount(key, savedAmount);
                    if(savedType != (int) LibertyOneKnobRack::Type::none && locationKey.isNotEmpty())
                        restoredInsertLocations.insert(locationKey);
                }
            }
        }
    }
    loadLibertyMultiMidiClips(*this, *project);
    updateMidiClipTiming();
    audioEngine.setCurrentTimeSeconds(playheadSeconds);
    playheadSeconds = audioEngine.getCurrentTimeSeconds();
    isPlaying = false;
    audioEngine.setPlaying(false);
    currentProjectFile = file;
    tempoControls.refresh();
    refreshLibertyMixConsole(this);
    markProjectClean();
    repaint();

    if (missingFiles.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               "Liberty - Project Media",
                                               "Some audio files could not be restored:\n\n" + missingFiles,
                                               "OK");
    if (missingPlugins.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               "Liberty - Project Plugins",
                                               "Some plugins could not be restored:\n\n" + missingPlugins,
                                               "OK");
    if (pluginStateWarnings.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               "Liberty - Plugin State",
                                               "Some plugin settings could not be restored. The plugins remain loaded with their current/default settings:\n\n" + pluginStateWarnings,
                                               "OK");

    return true;
}
