#include "MainComponent.h"

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
