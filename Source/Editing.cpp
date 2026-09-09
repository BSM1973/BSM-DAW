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
        if (!audioEngine.hasAudioFile(selectedTrack))
            return true;

        audioEngine.clearAudioTrack(selectedTrack);
        trackSourceFiles[(size_t)selectedTrack] = juce::File{};
        waveformMin[(size_t)selectedTrack].clear();
        waveformMax[(size_t)selectedTrack].clear();
        playheadSeconds = 0.0;
        isPlaying = false;
        repaint();
        return true;
    }

    if (isS && !modifiers.isAnyModifierKeyDown())
    {
        if (!audioEngine.hasAudioFile(selectedTrack))
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
