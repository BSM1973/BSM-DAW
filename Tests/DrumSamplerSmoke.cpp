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

    // Generate a deterministic WAV fixture and test actual playback gain.
    const auto fixture = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("liberty-drum-smoke", ".wav");
    {
        juce::WavAudioFormat format;
        std::unique_ptr<juce::FileOutputStream> stream(fixture.createOutputStream());
        if (!stream) return 8;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.get(), 48000.0, 1, 16, {}, 0));
        if (!writer) return 9;
        stream.release();
        juce::AudioBuffer<float> tone(1, 4800);
        for (int i = 0; i < tone.getNumSamples(); ++i)
            tone.setSample(0, i, 0.5f);
        if (!writer->writeFromAudioSampleBuffer(tone, 0, tone.getNumSamples())) return 10;
    }
    LibertyDrumSampler sounding;
    sounding.prepare(48000.0);
    if (!sounding.loadPad(0, fixture)) { fixture.deleteFile(); return 11; }
    sounding.setPadEnvelope(0, 0.0f, 0.0f, 1.0f, 8.0f);
    juce::AudioBuffer<float> audible(2, 256);
    audible.clear();
    juce::MidiBuffer hit;
    hit.addEvent(juce::MidiMessage::noteOn(1, 36, (juce::uint8)127), 0);
    sounding.renderMidi(audible, hit);
    if (!near(audible.getSample(0, 100), 0.5f, 0.003f)) { fixture.deleteFile(); return 12; }

    // Attack should ramp from silence toward the sample's nominal level.
    sounding.reset();
    sounding.setPadEnvelope(0, 4.0f, 0.0f, 1.0f, 8.0f);
    audible.clear();
    sounding.renderMidi(audible, hit);
    if (std::abs(audible.getSample(0, 0)) > 0.003f
        || !(audible.getSample(0, 150) > audible.getSample(0, 40)))
    { fixture.deleteFile(); return 13; }

    // Gate note-off must attenuate the held voice after release time.
    sounding.reset();
    sounding.setPadGateMode(0, true);
    sounding.setPadEnvelope(0, 0.0f, 0.0f, 1.0f, 2.0f);
    audible.clear();
    juce::MidiBuffer gate;
    gate.addEvent(juce::MidiMessage::noteOn(1, 36, (juce::uint8)127), 0);
    gate.addEvent(juce::MidiMessage::noteOff(1, 36), 32);
    sounding.renderMidi(audible, gate);
    if (!(audible.getSample(0, 40) > 0.0f)
        || std::abs(audible.getSample(0, 180)) > 0.003f)
    { fixture.deleteFile(); return 14; }
    // A second hit in the same choke group must fade the first hit smoothly.
    sounding.reset();
    if (!sounding.loadPad(1, fixture)) { fixture.deleteFile(); return 15; }
    sounding.setPadGateMode(0, false);
    sounding.setPadEnvelope(0, 0.0f, 0.0f, 1.0f, 8.0f);
    sounding.setPadEnvelope(1, 0.0f, 0.0f, 1.0f, 8.0f);
    sounding.setPadChokeGroup(0, 1);
    sounding.setPadChokeGroup(1, 1);
    sounding.setPadChokeFade(1, 2.0f);
    sounding.setPadGain(1, 0.0f); // Silent trigger isolates the outgoing voice.
    juce::AudioBuffer<float> chokeOutput(2, 320);
    chokeOutput.clear();
    juce::MidiBuffer chokeMidi;
    chokeMidi.addEvent(juce::MidiMessage::noteOn(1, 36, (juce::uint8)127), 0);
    chokeMidi.addEvent(juce::MidiMessage::noteOn(1, 37, (juce::uint8)127), 100);
    sounding.renderMidi(chokeOutput, chokeMidi);
    const float beforeChoke = chokeOutput.getSample(0, 99);
    const float startChoke = chokeOutput.getSample(0, 100);
    const float midChoke = chokeOutput.getSample(0, 145);
    const float endChoke = chokeOutput.getSample(0, 210);
    if (!near(beforeChoke, 0.5f, 0.003f)
        || std::abs(startChoke - beforeChoke) > 0.015f
        || !(midChoke > 0.0f && midChoke < startChoke)
        || std::abs(endChoke) > 0.003f)
    { fixture.deleteFile(); return 16; }

    // Triggering an empty pad must not choke an audible voice.
    sounding.reset();
    sounding.setPadChokeGroup(2, 1);
    juce::AudioBuffer<float> emptyOutput(2, 256);
    emptyOutput.clear();
    juce::MidiBuffer emptyMidi;
    emptyMidi.addEvent(juce::MidiMessage::noteOn(1, 36, (juce::uint8)127), 0);
    emptyMidi.addEvent(juce::MidiMessage::noteOn(1, 38, (juce::uint8)127), 100);
    sounding.renderMidi(emptyOutput, emptyMidi);
    if (!near(emptyOutput.getSample(0, 200), 0.5f, 0.003f))
    { fixture.deleteFile(); return 17; }
    // With 32 active voices, the 33rd hit must replace the oldest voice.
    // Unique velocities make an incorrect voice-stealing policy measurable.
    sounding.reset();
    sounding.setPadChokeGroup(0, 0);
    sounding.setPadGain(0, 1.0f);
    sounding.setPadGateMode(0, false);
    sounding.setPadEnvelope(0, 0.0f, 0.0f, 1.0f, 8.0f);
    juce::AudioBuffer<float> polyOutput(2, 64);
    juce::MidiBuffer polyMidi;
    for (int velocity = 1; velocity <= 32; ++velocity)
        polyMidi.addEvent(juce::MidiMessage::noteOn(1, 36, (juce::uint8)velocity), 0);
    polyOutput.clear();
    sounding.renderMidi(polyOutput, polyMidi);
    const float expected32 = 0.5f * (32.0f * 33.0f / 2.0f) / 127.0f;
    if (!near(polyOutput.getSample(0, 10), expected32, 0.02f))
    { fixture.deleteFile(); return 18; }

    sounding.reset();
    polyMidi.clear();
    for (int velocity = 1; velocity <= 33; ++velocity)
        polyMidi.addEvent(juce::MidiMessage::noteOn(1, 36, (juce::uint8)velocity), 0);
    polyOutput.clear();
    sounding.renderMidi(polyOutput, polyMidi);
    const float expectedStolen = 0.5f * ((33.0f * 34.0f / 2.0f) - 1.0f) / 127.0f;
    if (!near(polyOutput.getSample(0, 10), expectedStolen, 0.02f))
    { fixture.deleteFile(); return 19; }
    // Replacing a kit while a voice is sounding must not invalidate its audio.
    sounding.reset();
    sounding.setPadGain(0, 1.0f);
    sounding.setPadChokeGroup(0, 0);
    sounding.setPadEnvelope(0, 0.0f, 0.0f, 1.0f, 8.0f);
    juce::AudioBuffer<float> beforeSwap(2, 128);
    beforeSwap.clear();
    sounding.renderMidi(beforeSwap, hit);
    if (!near(beforeSwap.getSample(0, 100), 0.5f, 0.003f))
    { fixture.deleteFile(); return 20; }

    // Importing an empty kit replaces pad audio but active voices retain a shared buffer.
    const auto manifest = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("liberty-empty-kit", ".xml");
    {
        juce::XmlElement emptyKit("LibertyDrumKit");
        emptyKit.setAttribute("version", 1);
        if (!emptyKit.writeTo(manifest)) { fixture.deleteFile(); return 21; }
    }
    if (!sounding.importPortableKit(manifest))
    { manifest.deleteFile(); fixture.deleteFile(); return 22; }
    manifest.deleteFile();
    if (sounding.hasSample(0)) { fixture.deleteFile(); return 23; }
    juce::AudioBuffer<float> afterSwap(2, 128);
    afterSwap.clear();
    sounding.renderMidi(afterSwap, juce::MidiBuffer{});
    if (!near(afterSwap.getSample(0, 50), 0.5f, 0.003f))
    { fixture.deleteFile(); return 24; }
    // Invalid kit imports must not replace a functioning kit or silence playback.
    LibertyDrumSampler protectedKit;
    protectedKit.prepare(48000.0);
    if (!protectedKit.loadPad(0, fixture)) { fixture.deleteFile(); return 25; }
    protectedKit.setPadGain(0, 0.75f);
    const auto badManifest = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("liberty-invalid-kit", ".xml");
    {
        juce::XmlElement invalidKit("LibertyDrumKit");
        auto* pad = invalidKit.createNewChildElement("Pad");
        pad->setAttribute("index", 0);
        pad->setAttribute("file", "missing-sample-does-not-exist.wav");
        if (!invalidKit.writeTo(badManifest)) { fixture.deleteFile(); return 26; }
    }
    if (protectedKit.importPortableKit(badManifest))
    { badManifest.deleteFile(); fixture.deleteFile(); return 27; }
    badManifest.deleteFile();
    if (!protectedKit.hasSample(0) || !near(protectedKit.getPad(0).gain.load(), 0.75f))
    { fixture.deleteFile(); return 28; }
    juce::AudioBuffer<float> retained(2, 128);
    retained.clear();
    protectedKit.renderMidi(retained, hit);
    if (!near(retained.getSample(0, 80), 0.375f, 0.003f))
    { fixture.deleteFile(); return 29; }

    // A malformed document must be rejected without touching the live kit.
    const auto malformed = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("liberty-malformed-kit", ".xml");
    if (!malformed.replaceWithText("<LibertyDrumKit><Pad"))
    { fixture.deleteFile(); return 30; }
    if (protectedKit.importPortableKit(malformed))
    { malformed.deleteFile(); fixture.deleteFile(); return 31; }
    malformed.deleteFile();
    if (!protectedKit.hasSample(0) || !near(protectedKit.getPad(0).gain.load(), 0.75f))
    { fixture.deleteFile(); return 32; }
    // Duplicate pad indices must be rejected atomically.
    const auto duplicate = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("liberty-duplicate-kit", ".xml");
    {
        juce::XmlElement duplicateKit("LibertyDrumKit");
        for (int n = 0; n < 2; ++n)
        {
            auto* pad = duplicateKit.createNewChildElement("Pad");
            pad->setAttribute("index", 0);
            pad->setAttribute("file", fixture.getFullPathName());
        }
        if (!duplicateKit.writeTo(duplicate)) { fixture.deleteFile(); return 33; }
    }
    if (protectedKit.importPortableKit(duplicate))
    { duplicate.deleteFile(); fixture.deleteFile(); return 34; }
    duplicate.deleteFile();
    if (!protectedKit.hasSample(0) || !near(protectedKit.getPad(0).gain.load(), 0.75f))
    { fixture.deleteFile(); return 35; }
    // Reject malformed pad identifiers without altering the loaded kit.
    for (const auto& invalidIndex : { juce::String("abc"), juce::String("-1"),
                                      juce::String("16"), juce::String("0x1") })
    {
        const auto invalidManifest = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getNonexistentChildFile("liberty-bad-index", ".xml");
        juce::XmlElement invalidKit("LibertyDrumKit");
        auto* pad = invalidKit.createNewChildElement("Pad");
        pad->setAttribute("index", invalidIndex);
        pad->setAttribute("file", fixture.getFullPathName());
        if (!invalidKit.writeTo(invalidManifest)) { fixture.deleteFile(); return 36; }
        const bool imported = protectedKit.importPortableKit(invalidManifest);
        invalidManifest.deleteFile();
        if (imported || !protectedKit.hasSample(0)
            || !near(protectedKit.getPad(0).gain.load(), 0.75f))
        { fixture.deleteFile(); return 37; }
    }
    // Non-finite envelope and gain parameters must not poison the live kit.
    for (const auto& invalidValue : { juce::String("nan"), juce::String("inf"),
                                       juce::String("-inf") })
    {
        const auto invalidManifest = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getNonexistentChildFile("liberty-nonfinite-kit", ".xml");
        juce::XmlElement invalidKit("LibertyDrumKit");
        auto* pad = invalidKit.createNewChildElement("Pad");
        pad->setAttribute("index", 0);
        pad->setAttribute("gain", invalidValue);
        if (!invalidKit.writeTo(invalidManifest)) { fixture.deleteFile(); return 38; }
        const bool imported = protectedKit.importPortableKit(invalidManifest);
        invalidManifest.deleteFile();
        if (imported || !protectedKit.hasSample(0)
            || !near(protectedKit.getPad(0).gain.load(), 0.75f))
        { fixture.deleteFile(); return 39; }
    }
    fixture.deleteFile();

    std::cout << "Drum Sampler smoke tests passed\n";
    return 0;
}
