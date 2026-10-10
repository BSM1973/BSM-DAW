#include "LibertyDrumSampler.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <iostream>

static bool near(float actual, float expected, float tolerance = 0.002f)
{
    return std::abs(actual - expected) <= tolerance;
}

int main()
{
    LibertyDrumSampler sampler;
    sampler.prepare(48000.0);
    sampler.setPadChokeGroup(0, 3);
    sampler.setPadChokeFade(0, 17.0f);
    sampler.setPadGateMode(0, true);
    sampler.setPadEnvelope(0, 25.0f, 120.0f, 0.35f, 250.0f);
    auto kit = sampler.createKitXml();
    if (kit == nullptr) return 1;

    LibertyDrumSampler restored;
    restored.prepare(48000.0);
    if (!restored.restoreKitXml(*kit)) return 2;
    const auto& pad = restored.getPad(0);
    if (pad.chokeGroup.load() != 3 || !near(pad.chokeFadeMs.load(), 17.0f)
        || !pad.gateMode.load() || !near(pad.attackMs.load(), 25.0f)
        || !near(pad.decayMs.load(), 120.0f)
        || !near(pad.sustain.load(), 0.35f)
        || !near(pad.releaseMs.load(), 250.0f)) return 3;

    // Older manifests without ADSR or gate attributes must restore defaults.
    auto legacy = sampler.createKitXml();
    if (legacy == nullptr) return 4;
    for (auto* node : legacy->getChildIterator())
        if (node->hasTagName("Pad"))
        {
            node->removeAttribute("chokeFadeMs");
            node->removeAttribute("gateMode");
            node->removeAttribute("attackMs");
            node->removeAttribute("decayMs");
            node->removeAttribute("sustain");
            node->removeAttribute("releaseMs");
        }
    LibertyDrumSampler older;
    older.prepare(48000.0);
    if (!older.restoreKitXml(*legacy)) return 5;
    const auto& defaults = older.getPad(0);
    if (!near(defaults.chokeFadeMs.load(), 8.0f) || defaults.gateMode.load()
        || !near(defaults.attackMs.load(), 0.0f)
        || !near(defaults.decayMs.load(), 0.0f)
        || !near(defaults.sustain.load(), 1.0f)
        || !near(defaults.releaseMs.load(), 8.0f)) return 6;

    juce::AudioBuffer<float> output(2, 512);
    output.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, (juce::uint8)100), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, (juce::uint8)100), 40);
    midi.addEvent(juce::MidiMessage::noteOff(1, 36), 80);
    midi.addEvent(juce::MidiMessage::noteOff(1, 36), 120);
    restored.renderMidi(output, midi);
    for (int channel = 0; channel < output.getNumChannels(); ++channel)
        for (int frame = 0; frame < output.getNumSamples(); ++frame)
            if (!std::isfinite(output.getSample(channel, frame))) return 7;

    std::cout << "Drum Sampler smoke tests passed\n";
    return 0;
}
