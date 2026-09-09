#include "MainComponent.h"

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    const auto modifiers = key.getModifiers();

   #if JUCE_MAC
    const bool commandOrControl = modifiers.isCommandDown();
   #else
    const bool commandOrControl = modifiers.isCtrlDown();
   #endif

    // LIBERTY WORKFLOW: use the physical JUCE key code for shortcuts.
    // On macOS, the key code can arrive as either lower- or upper-case ASCII,
    // including with AZERTY layouts, while getTextCharacter() may be null.
    const auto keyCode = static_cast<juce_wchar>(key.getKeyCode());
    const auto normalizedKey = juce::CharacterFunctions::toLowerCase(keyCode);

    if (commandOrControl)
    {
        if (normalizedKey == 's')
        {
            if (modifiers.isShiftDown())
                saveProjectAs();
            else
                saveProject();
            return true;
        }

        if (normalizedKey == 'o' && !modifiers.isShiftDown())
        {
            openProject();
            return true;
        }

        if (normalizedKey == 'n' && !modifiers.isShiftDown())
        {
            newProject();
            return true;
        }
    }

    if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
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

    if (normalizedKey == 's' && !modifiers.isAnyModifierKeyDown())
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
