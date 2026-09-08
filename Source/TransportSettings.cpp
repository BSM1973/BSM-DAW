#include "MainComponent.h"

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
                tempoBpm = value;
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
