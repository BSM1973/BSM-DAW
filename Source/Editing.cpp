#include "MainComponent.h"
#include <set>

namespace
{
MainComponent* findMidiMainComponent() noexcept
{
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
    {
        if (auto* window = dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i)))
            if (window->getName() == "Liberty - MIDI 1")
                if (auto* main = dynamic_cast<MainComponent*>(window->getContentComponent()))
                    return main;
    }
    return nullptr;
}
}

bool handleLibertyMidiUndoRedoKeyPress(const juce::KeyPress& key)
{
    const auto modifiers = key.getModifiers();
   #if JUCE_MAC
    const bool commandOrControl = modifiers.isCommandDown();
   #else
    const bool commandOrControl = modifiers.isCtrlDown();
   #endif

    if (!commandOrControl || modifiers.isAltDown())
        return false;

    const int keyCode = key.getKeyCode();
    const bool isZ = (keyCode == 'z' || keyCode == 'Z');
    const bool isY = (keyCode == 'y' || keyCode == 'Y');
    if (!isZ && !isY)
        return false;

    auto* main = findMidiMainComponent();
    if (main == nullptr || main->getMidiEngine().getNumNotes() == 0)
        return false;

    const bool redo = isY || (isZ && modifiers.isShiftDown());
    const bool changed = redo ? main->getMidiEngine().redo() : main->getMidiEngine().undo();
    if (!changed)
        return true;

    main->updateMidiClipTiming();
    main->repaint();
    return true;
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    const auto modifiers = key.getModifiers();

   #if JUCE_MAC
    const bool commandOrControl = modifiers.isCommandDown();
   #else
    const bool commandOrControl = modifiers.isCtrlDown();
   #endif

    // LIBERTY WORKFLOW: use JUCE key codes directly. Do not convert through
    // juce_wchar/CharacterFunctions: JUCE key codes are integer values.
    const int keyCode = key.getKeyCode();
    const bool isS = (keyCode == 's' || keyCode == 'S');
    const bool isO = (keyCode == 'o' || keyCode == 'O');
    const bool isN = (keyCode == 'n' || keyCode == 'N');

    if (commandOrControl)
    {
        if (isS)
        {
            if (modifiers.isShiftDown())
                saveProjectAs();
            else
                saveProject();
            return true;
        }

        if (isO && !modifiers.isShiftDown())
        {
            openProject();
            return true;
        }

        if (isN && !modifiers.isShiftDown())
        {
            newProject();
            return true;
        }

        if ((keyCode == 'z' || keyCode == 'Z' || keyCode == 'y' || keyCode == 'Y') && selectedTrack < 0)
        {
            const bool redo = (keyCode == 'y' || keyCode == 'Y') || modifiers.isShiftDown();
            const bool changed = redo ? midiEngine.redo() : midiEngine.undo();
            if (changed)
            {
                updateMidiClipTiming();
                repaint();
            }
            return true;
        }
    }

    if (keyCode == juce::KeyPress::deleteKey || keyCode == juce::KeyPress::backspaceKey)
    {
        // MIDI 1 is a real timeline clip. Delete removes the complete clip
        // (notes + timeline placement/length), without touching any audio track.
        if (selectedTrack < 0)
        {
            if (midiEngine.getNumNotes() == 0 && midiClipLengthSeconds <= 0.0)
                return true;

            midiEngine.clear();
            midiClipStartSeconds = 0.0;
            midiClipLengthSeconds = 0.0;
            midiClipLengthUserDefined = false;
            updateMidiClipTiming();
            playheadSeconds = 0.0;
            isPlaying = false;
            audioEngine.setPlaying(false);
            repaint();
            return true;
        }

        if (selectedTrack >= AudioEngine::maxAudioTracks || !audioEngine.hasAudioFile(selectedTrack))
            return true;

        audioEngine.clearAudioTrack(selectedTrack);
        trackSourceFiles[(size_t)selectedTrack] = juce::File{};
        waveformMin[(size_t)selectedTrack].clear();
        waveformMax[(size_t)selectedTrack].clear();
        playheadSeconds = 0.0;
        isPlaying = false;
        audioEngine.setPlaying(false);
        repaint();
        return true;
    }

    if (isS && !modifiers.isAnyModifierKeyDown())
    {
        // MIDI 1 has its own editor and must not be treated as an audio split target.
        if (selectedTrack < 0)
            return true;

        if (selectedTrack >= AudioEngine::maxAudioTracks || !audioEngine.hasAudioFile(selectedTrack))
            return true;

        int newTrack = -1;
        juce::String error;
        if (audioEngine.splitAudioTrack(selectedTrack, playheadSeconds, newTrack, error))
        {
            trackSourceFiles[(size_t)newTrack] = trackSourceFiles[(size_t)selectedTrack];
            rebuildWaveformCache(selectedTrack);
            rebuildWaveformCache(newTrack);
            selectedTrack = newTrack;
            repaint();
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Liberty - Split",
                                                   error,
                                                   "OK");
        }
        return true;
    }

    return false;
}
