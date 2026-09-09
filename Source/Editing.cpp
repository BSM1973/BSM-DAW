#include "MainComponent.h"

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
    {
        if (!audioEngine.hasAudioFile(selectedTrack))
            return true;

        audioEngine.clearAudioTrack(selectedTrack);
        trackSourceFiles[(size_t)selectedTrack] = {};
        waveformMin[(size_t)selectedTrack].clear();
        waveformMax[(size_t)selectedTrack].clear();
        playheadSeconds = 0.0;
        isPlaying = false;
        repaint();
        return true;
    }

    if (key.getTextCharacter() == 's' || key.getTextCharacter() == 'S')
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
