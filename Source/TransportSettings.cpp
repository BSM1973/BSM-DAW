#include "MainComponent.h"

bool commitLibertyAudioClipResize(MainComponent& owner,
                                  int trackIndex,
                                  double requestedStartSeconds,
                                  double requestedLengthSeconds,
                                  bool preservePitchStretch,
                                  bool resizeLeft,
                                  juce::String& error);

void syncLibertyMultiMidiPlayback(MainComponent& owner);
void scaleLibertyMultiMidiClipsForTempoChange(MainComponent& owner, double ratio);
bool commitLibertyAudioTempoChange(MainComponent& owner, double tempoRatio, juce::String& error, bool restorePlaying);

void MainComponent::editTempo()
{
    auto* alert = new juce::AlertWindow("BSM DAW - Tempo", "Enter tempo (BPM):", juce::MessageBoxIconType::NoIcon);
    alert->addTextEditor("tempo", juce::String(tempoBpm, 2), "BPM:");
    alert->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    alert->enterModalState(true, juce::ModalCallbackFunction::create([this, alert](int result)
    {
        if (result == 1)
        {
            const double value = alert->getTextEditorContents("tempo").getDoubleValue();
            if (value >= 20.0 && value <= 300.0)
            {
                const double oldTempo = juce::jmax(1.0, tempoBpm);
                const double transportBeforeTempoChange = audioEngine.getCurrentTimeSeconds();
                const bool wasPlayingBeforeTempoChange = audioEngine.isPlaying();
                if (std::abs(value - oldTempo) > 0.000001)
                {
                    // Audio clips are treated as musical clips: their number of measures stays fixed.
                    // Example in 4/4: 4 measures at 120 BPM = 8 s, and become 12 s at 80 BPM.
                    // Signalsmith Stretch is used through commitLibertyAudioClipResize(), with pitch preserved.
                    const double tempoRatio = oldTempo / value;
                    juce::String stretchError;
                    if (!commitLibertyAudioTempoChange(*this, tempoRatio, stretchError, false))
                    {
                        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                               "Liberty - Tempo Stretch",
                                                               stretchError,
                                                               "OK");
                        delete alert;
                        repaint();
                        return;
                    }
                }
                tempoBpm = value;
                if (std::abs(value - oldTempo) > 0.000001)
                {
                    const double tempoRatio = oldTempo / value;
                    scaleLibertyMultiMidiClipsForTempoChange(*this, tempoRatio);
                    audioEngine.setCurrentTimeSeconds(transportBeforeTempoChange * tempoRatio);
                    if (wasPlayingBeforeTempoChange)
                        audioEngine.setPlaying(true);
                }
                else
                    syncLibertyMultiMidiPlayback(*this);
                tempoControls.refresh();
            }
        }
        delete alert;
        repaint();
    }), true);
}

void MainComponent::editTimeSignature()
{
    auto* alert = new juce::AlertWindow("BSM DAW - Time Signature", "Enter time signature:", juce::MessageBoxIconType::NoIcon);
    alert->addTextEditor("numerator", juce::String(timeSignatureNumerator), "Numerator:");
    alert->addTextEditor("denominator", juce::String(timeSignatureDenominator), "Denominator:");
    alert->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    alert->enterModalState(true, juce::ModalCallbackFunction::create([this, alert](int result)
    {
        if (result == 1)
        {
            const int numerator = alert->getTextEditorContents("numerator").getIntValue();
            const int denominator = alert->getTextEditorContents("denominator").getIntValue();
            const bool validDenominator = denominator == 2 || denominator == 4 || denominator == 8 || denominator == 16;
            if (numerator >= 1 && numerator <= 32 && validDenominator)
            {
                timeSignatureNumerator = numerator;
                timeSignatureDenominator = denominator;
                tempoControls.refresh();
            }
        }
        delete alert;
        repaint();
    }), true);
}
