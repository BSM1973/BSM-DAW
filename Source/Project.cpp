#include "MainComponent.h"

namespace
{
constexpr int menuNew = 1;
constexpr int menuOpen = 2;
constexpr int menuSave = 3;
constexpr int menuSaveAs = 4;

bool exportTrackToProjectMedia(const juce::File& projectFile,
                               int trackIndex,
                               const juce::AudioBuffer<float>* buffer,
                               double sampleRate,
                               juce::File& exportedFile)
{
    if (buffer == nullptr || buffer->getNumSamples() <= 0 || buffer->getNumChannels() <= 0 || sampleRate <= 0.0)
        return false;

    auto mediaFolder = projectFile.getSiblingFile(projectFile.getFileNameWithoutExtension() + "_Media");
    if (!mediaFolder.createDirectory().wasOk() && !mediaFolder.isDirectory())
        return false;

    exportedFile = mediaFolder.getChildFile("Audio_" + juce::String(trackIndex + 1) + ".wav");
    auto output = exportedFile.createOutputStream();
    if (output == nullptr)
        return false;

    juce::WavAudioFormat wav;
    auto writer = std::unique_ptr<juce::AudioFormatWriter>(
        wav.createWriterFor(output.release(), sampleRate, (unsigned int)buffer->getNumChannels(), 24, {}, 0));
    if (writer == nullptr)
        return false;

    return writer->writeFromAudioSampleBuffer(*buffer, 0, buffer->getNumSamples());
}
}

juce::String MainComponent::getProjectStateSignature() const
{
    juce::String signature;
    signature << "tempo=" << juce::String(tempoBpm, 6)
              << ";meter=" << timeSignatureNumerator << "/" << timeSignatureDenominator
              << ";master=" << juce::String(audioEngine.getMasterGain(), 6);

    for (int i = 0; i < AudioEngine::maxAudioTracks; ++i)
    {
        signature << "|track=" << i
                  << ";loaded=" << (audioEngine.hasAudioFile(i) ? 1 : 0)
                  << ";source=" << trackSourceFiles[(size_t)i].getFullPathName()
                  << ";name=" << audioEngine.getAudioFileName(i)
                  << ";length=" << juce::String(audioEngine.getAudioFileLengthSeconds(i), 6)
                  << ";start=" << juce::String(audioEngine.getTrackStartSeconds(i), 6)
                  << ";gain=" << juce::String(audioEngine.getTrackGain(i), 6)
                  << ";pan=" << juce::String(audioEngine.getTrackPan(i), 6)
                  << ";mute=" << (audioEngine.isTrackMuted(i) ? 1 : 0)
                  << ";solo=" << (audioEngine.isTrackSolo(i) ? 1 : 0);
    }

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
    // Liberty must always ask before quitting, even when the current project
    // is already saved. This restores the explicit SAVE / DON'T SAVE / CANCEL
    // close workflow requested for the application.
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

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&projectButton),
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
    audioEngine.setPlaying(false);
    audioEngine.resetTransport();
    isPlaying = false;
    playheadSeconds = 0.0;
    tempoBpm = 120.0;
    timeSignatureNumerator = 4;
    timeSignatureDenominator = 4;
    selectedTrack = 0;
    audioEngine.setMasterGain(1.0f);

    for (int i = 0; i < AudioEngine::maxAudioTracks; ++i)
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

    juce::XmlElement project("LibertyProject");
    project.setAttribute("version", 2);
    project.setAttribute("tempo", tempoBpm);
    project.setAttribute("timeSignatureNumerator", timeSignatureNumerator);
    project.setAttribute("timeSignatureDenominator", timeSignatureDenominator);
    project.setAttribute("selectedTrack", selectedTrack);
    project.setAttribute("playheadSeconds", playheadSeconds);
    project.setAttribute("masterGain", (double)audioEngine.getMasterGain());

    for (int i = 0; i < AudioEngine::maxAudioTracks; ++i)
    {
        auto* track = project.createNewChildElement("Track");
        track->setAttribute("index", i);
        track->setAttribute("loaded", audioEngine.hasAudioFile(i));

        juce::File sourceFile = trackSourceFiles[(size_t)i];
        const auto* buffer = audioEngine.getAudioBuffer(i);

        // Every saved project gets its own media copy. This makes the project
        // reopenable even when the original source file is moved or changed,
        // and also preserves edited/split clip buffers.
        if (audioEngine.hasAudioFile(i) && buffer != nullptr)
        {
            juce::File exportedFile;
            if (exportTrackToProjectMedia(file, i, buffer, audioEngine.getSampleRate(), exportedFile))
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

    auto output = file.createOutputStream();
    if (output == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               "Liberty - Project Save",
                                               "Could not write the project file.",
                                               "OK");
        return false;
    }

    const auto xmlText = project.toString();
    if (!output->writeText(xmlText, false, false, "UTF-8"))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               "Liberty - Project Save",
                                               "Could not write the Liberty project data.",
                                               "OK");
        return false;
    }

    output->flush();
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

    resetProjectState();

    tempoBpm = juce::jlimit(20.0, 400.0, project->getDoubleAttribute("tempo", 120.0));
    timeSignatureNumerator = juce::jlimit(1, 32, project->getIntAttribute("timeSignatureNumerator", 4));
    timeSignatureDenominator = juce::jlimit(1, 32, project->getIntAttribute("timeSignatureDenominator", 4));
    selectedTrack = juce::jlimit(0, AudioEngine::maxAudioTracks - 1, project->getIntAttribute("selectedTrack", 0));
    playheadSeconds = juce::jmax(0.0, project->getDoubleAttribute("playheadSeconds", 0.0));
    audioEngine.setMasterGain((float)project->getDoubleAttribute("masterGain", 1.0));

    juce::String missingFiles;
    for (auto* track = project->getFirstChildElement(); track != nullptr; track = track->getNextElement())
    {
        if (track->getTagName() != "Track") continue;
        const int index = track->getIntAttribute("index", -1);
        if (index < 0 || index >= AudioEngine::maxAudioTracks) continue;
        if (!track->getBoolAttribute("loaded", false)) continue;

        const auto sourcePath = track->getStringAttribute("sourceFile");
        juce::File sourceFile(sourcePath);

        // Version 2 stores project media beside the .bsmproj. Keep backwards
        // compatibility with older projects that stored the original absolute path.
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
            missingFiles << "Audio " << (index + 1) << ": " << sourcePath << "\n";
            continue;
        }

        juce::String error;
        if (!audioEngine.loadAudioFileIntoTrack(index, sourceFile, error))
        {
            missingFiles << "Audio " << (index + 1) << ": " << error << "\n";
            continue;
        }

        trackSourceFiles[(size_t)index] = sourceFile;
        audioEngine.setTrackStartSeconds(index, track->getDoubleAttribute("startSeconds", 0.0));
        audioEngine.setTrackGain(index, (float)track->getDoubleAttribute("gain", 1.0));
        audioEngine.setTrackPan(index, (float)track->getDoubleAttribute("pan", 0.0));
        audioEngine.setTrackMuted(index, track->getBoolAttribute("muted", false));
        audioEngine.setTrackSolo(index, track->getBoolAttribute("solo", false));
        rebuildWaveformCache(index);
    }

    audioEngine.setCurrentTimeSeconds(playheadSeconds);
    playheadSeconds = audioEngine.getCurrentTimeSeconds();
    isPlaying = false;
    audioEngine.setPlaying(false);
    currentProjectFile = file;
    tempoControls.refresh();
    markProjectClean();
    repaint();

    if (missingFiles.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               "Liberty - Project Media",
                                               "Some audio files could not be restored:\n\n" + missingFiles,
                                               "OK");

    return true;
}
