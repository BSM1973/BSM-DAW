#include "LibertyDrumSampler.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <iostream>

static bool near(float actual, float expected, float tolerance = 0.002f)
{
    return std::abs(actual - expected) <= tolerance;
}

static int fail(int code)
{
    std::cerr << "DrumSamplerSmoke failed at check " << code << std::endl;
    return code;
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
    if (kit == nullptr) return fail(1);

    LibertyDrumSampler restored;
    restored.prepare(48000.0);
    if (!restored.restoreKitXml(*kit)) return fail(2);
    const auto& pad = restored.getPad(0);
    if (pad.chokeGroup.load() != 3 || !near(pad.chokeFadeMs.load(), 17.0f)
        || !pad.gateMode.load() || !near(pad.attackMs.load(), 25.0f)
        || !near(pad.decayMs.load(), 120.0f)
        || !near(pad.sustain.load(), 0.35f)
        || !near(pad.releaseMs.load(), 250.0f)) return fail(3);

    // Older manifests without ADSR or gate attributes must restore defaults.
    auto legacy = sampler.createKitXml();
    if (legacy == nullptr) return fail(4);
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
    if (!older.restoreKitXml(*legacy)) return fail(5);
    const auto& defaults = older.getPad(0);
    if (!near(defaults.chokeFadeMs.load(), 8.0f) || defaults.gateMode.load()
        || !near(defaults.attackMs.load(), 0.0f)
        || !near(defaults.decayMs.load(), 0.0f)
        || !near(defaults.sustain.load(), 1.0f)
        || !near(defaults.releaseMs.load(), 8.0f)) return fail(6);

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
            if (!std::isfinite(output.getSample(channel, frame))) return fail(7);

    // Generate a deterministic WAV fixture and test actual playback gain.
    const auto fixture = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("liberty-drum-smoke", ".wav");
    {
        juce::WavAudioFormat format;
        std::unique_ptr<juce::FileOutputStream> stream(fixture.createOutputStream());
        if (!stream) return fail(8);
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.get(), 48000.0, 1, 16, {}, 0));
        if (!writer) return fail(9);
        stream.release();
        juce::AudioBuffer<float> tone(1, 4800);
        for (int i = 0; i < tone.getNumSamples(); ++i)
            tone.setSample(0, i, 0.5f);
        if (!writer->writeFromAudioSampleBuffer(tone, 0, tone.getNumSamples())) return fail(10);
    }
    LibertyDrumSampler sounding;
    sounding.prepare(48000.0);
    if (!sounding.loadPad(0, fixture)) { fixture.deleteFile(); return fail(11); }
    sounding.setPadEnvelope(0, 0.0f, 0.0f, 1.0f, 8.0f);
    juce::AudioBuffer<float> audible(2, 256);
    audible.clear();
    juce::MidiBuffer hit;
    hit.addEvent(juce::MidiMessage::noteOn(1, 36, (juce::uint8)127), 0);
    sounding.renderMidi(audible, hit);
    if (!near(audible.getSample(0, 100), 0.5f, 0.003f)) { fixture.deleteFile(); return fail(12); }

    // Attack should ramp from silence toward the sample's nominal level.
    sounding.reset();
    sounding.setPadEnvelope(0, 4.0f, 0.0f, 1.0f, 8.0f);
    audible.clear();
    sounding.renderMidi(audible, hit);
    if (std::abs(audible.getSample(0, 0)) > 0.003f
        || !(audible.getSample(0, 150) > audible.getSample(0, 40)))
    { fixture.deleteFile(); return fail(13); }

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
    { fixture.deleteFile(); return fail(14); }
    // A second hit in the same choke group must fade the first hit smoothly.
    sounding.reset();
    if (!sounding.loadPad(1, fixture)) { fixture.deleteFile(); return fail(15); }
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
    { fixture.deleteFile(); return fail(16); }

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
    { fixture.deleteFile(); return fail(17); }
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
    { fixture.deleteFile(); return fail(18); }

    sounding.reset();
    polyMidi.clear();
    for (int velocity = 1; velocity <= 33; ++velocity)
        polyMidi.addEvent(juce::MidiMessage::noteOn(1, 36, (juce::uint8)velocity), 0);
    polyOutput.clear();
    sounding.renderMidi(polyOutput, polyMidi);
    const float expectedStolen = 0.5f * ((33.0f * 34.0f / 2.0f) - 1.0f) / 127.0f;
    if (!near(polyOutput.getSample(0, 10), expectedStolen, 0.02f))
    { fixture.deleteFile(); return fail(19); }
    // Replacing a kit while a voice is sounding must not invalidate its audio.
    sounding.reset();
    sounding.setPadGain(0, 1.0f);
    sounding.setPadChokeGroup(0, 0);
    sounding.setPadEnvelope(0, 0.0f, 0.0f, 1.0f, 8.0f);
    juce::AudioBuffer<float> beforeSwap(2, 128);
    beforeSwap.clear();
    sounding.renderMidi(beforeSwap, hit);
    if (!near(beforeSwap.getSample(0, 100), 0.5f, 0.003f))
    { fixture.deleteFile(); return fail(20); }

    // Importing an empty kit replaces pad audio but active voices retain a shared buffer.
    const auto manifest = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("liberty-empty-kit", ".xml");
    {
        juce::XmlElement emptyKit("LibertyDrumKit");
        emptyKit.setAttribute("version", 1);
        if (!emptyKit.writeTo(manifest)) { fixture.deleteFile(); return fail(21); }
    }
    if (!sounding.importPortableKit(manifest))
    { manifest.deleteFile(); fixture.deleteFile(); return fail(22); }
    manifest.deleteFile();
    if (sounding.hasSample(0)) { fixture.deleteFile(); return fail(23); }
    juce::AudioBuffer<float> afterSwap(2, 128);
    afterSwap.clear();
    sounding.renderMidi(afterSwap, juce::MidiBuffer{});
    if (!near(afterSwap.getSample(0, 50), 0.5f, 0.003f))
    { fixture.deleteFile(); return fail(24); }
    // Invalid kit imports must not replace a functioning kit or silence playback.
    LibertyDrumSampler protectedKit;
    protectedKit.prepare(48000.0);
    if (!protectedKit.loadPad(0, fixture)) { fixture.deleteFile(); return fail(25); }
    protectedKit.setPadGain(0, 0.75f);
    const auto badManifest = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("liberty-invalid-kit", ".xml");
    {
        juce::XmlElement invalidKit("LibertyDrumKit");
        auto* pad = invalidKit.createNewChildElement("Pad");
        pad->setAttribute("index", 0);
        pad->setAttribute("file", "missing-sample-does-not-exist.wav");
        if (!invalidKit.writeTo(badManifest)) { fixture.deleteFile(); return fail(26); }
    }
    if (protectedKit.importPortableKit(badManifest))
    { badManifest.deleteFile(); fixture.deleteFile(); return fail(27); }
    badManifest.deleteFile();
    if (!protectedKit.hasSample(0) || !near(protectedKit.getPad(0).gain.load(), 0.75f))
    { fixture.deleteFile(); return fail(28); }
    juce::AudioBuffer<float> retained(2, 128);
    retained.clear();
    protectedKit.renderMidi(retained, hit);
    if (!near(retained.getSample(0, 80), 0.375f, 0.003f))
    { fixture.deleteFile(); return fail(29); }

    // A malformed document must be rejected without touching the live kit.
    const auto malformed = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("liberty-malformed-kit", ".xml");
    if (!malformed.replaceWithText("<LibertyDrumKit><Pad"))
    { fixture.deleteFile(); return fail(30); }
    if (protectedKit.importPortableKit(malformed))
    { malformed.deleteFile(); fixture.deleteFile(); return fail(31); }
    malformed.deleteFile();
    if (!protectedKit.hasSample(0) || !near(protectedKit.getPad(0).gain.load(), 0.75f))
    { fixture.deleteFile(); return fail(32); }
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
        if (!duplicateKit.writeTo(duplicate)) { fixture.deleteFile(); return fail(33); }
    }
    if (protectedKit.importPortableKit(duplicate))
    { duplicate.deleteFile(); fixture.deleteFile(); return fail(34); }
    duplicate.deleteFile();
    if (!protectedKit.hasSample(0) || !near(protectedKit.getPad(0).gain.load(), 0.75f))
    { fixture.deleteFile(); return fail(35); }
    // Reject malformed pad identifiers without altering the loaded kit.
    for (const auto& invalidIndex : { juce::String("abc"), juce::String("-1"),
                                      juce::String("16"), juce::String("0x1"),
                                      juce::String("999999999999999999999999") })
    {
        const auto invalidManifest = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getNonexistentChildFile("liberty-bad-index", ".xml");
        juce::XmlElement invalidKit("LibertyDrumKit");
        auto* pad = invalidKit.createNewChildElement("Pad");
        pad->setAttribute("index", invalidIndex);
        pad->setAttribute("file", fixture.getFullPathName());
        if (!invalidKit.writeTo(invalidManifest)) { fixture.deleteFile(); return fail(36); }
        const bool imported = protectedKit.importPortableKit(invalidManifest);
        invalidManifest.deleteFile();
        if (imported || !protectedKit.hasSample(0)
            || !near(protectedKit.getPad(0).gain.load(), 0.75f))
        { fixture.deleteFile(); return fail(37); }
    }
    // Non-finite envelope and gain parameters must not poison the live kit.
    for (const auto& invalidValue : { juce::String("nan"), juce::String("inf"),
                                       juce::String("-inf"), juce::String("1e100") })
    {
        const auto invalidManifest = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getNonexistentChildFile("liberty-nonfinite-kit", ".xml");
        juce::XmlElement invalidKit("LibertyDrumKit");
        auto* pad = invalidKit.createNewChildElement("Pad");
        pad->setAttribute("index", 0);
        pad->setAttribute("gain", invalidValue);
        if (!invalidKit.writeTo(invalidManifest)) { fixture.deleteFile(); return fail(38); }
        const bool imported = protectedKit.importPortableKit(invalidManifest);
        invalidManifest.deleteFile();
        if (imported || !protectedKit.hasSample(0)
            || !near(protectedKit.getPad(0).gain.load(), 0.75f))
        { fixture.deleteFile(); return fail(39); }
    }
    // Numeric attributes must parse completely, not silently become zero.
    for (const auto& invalidValue : { juce::String("abc"), juce::String("12xyz"),
                                       juce::String("1.2.3") })
    {
        const auto invalidManifest = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getNonexistentChildFile("liberty-malformed-gain", ".xml");
        juce::XmlElement invalidKit("LibertyDrumKit");
        auto* pad = invalidKit.createNewChildElement("Pad");
        pad->setAttribute("index", 0);
        pad->setAttribute("gain", invalidValue);
        if (!invalidKit.writeTo(invalidManifest)) { fixture.deleteFile(); return fail(40); }
        const bool imported = protectedKit.importPortableKit(invalidManifest);
        invalidManifest.deleteFile();
        if (imported || !protectedKit.hasSample(0)
            || !near(protectedKit.getPad(0).gain.load(), 0.75f))
        { fixture.deleteFile(); return fail(41); }
    }
    // Integer-only pad settings must reject invalid and out-of-range values.
    for (const auto& invalid : { std::pair<const char*, const char*> { "chokeGroup", "9" },
                                 { "chokeGroup", "-1" }, { "chokeGroup", "2.5" },
                                 { "gateMode", "2" }, { "gateMode", "true" },
                                 { "chokeGroup", "999999999999999999" },
                                 { "gateMode", "999999999999999999" } })
    {
        const auto invalidManifest = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getNonexistentChildFile("liberty-invalid-pad-mode", ".xml");
        juce::XmlElement invalidKit("LibertyDrumKit");
        auto* pad = invalidKit.createNewChildElement("Pad");
        pad->setAttribute("index", 0);
        pad->setAttribute(invalid.first, invalid.second);
        if (!invalidKit.writeTo(invalidManifest)) { fixture.deleteFile(); return fail(42); }
        const bool imported = protectedKit.importPortableKit(invalidManifest);
        invalidManifest.deleteFile();
        if (imported || !protectedKit.hasSample(0)
            || !near(protectedKit.getPad(0).gain.load(), 0.75f))
        { fixture.deleteFile(); return fail(43); }
    }
    // A valid number outside an audio control's supported range is still invalid.
    for (const auto& invalid : { std::pair<const char*, const char*> { "gain", "3" },
                                 { "gain", "-0.1" }, { "pan", "1.5" },
                                 { "pitch", "25" }, { "start", "1" },
                                 { "end", "0" }, { "sustain", "1.1" },
                                 { "attackMs", "-1" }, { "attackMs", "2001" },
                                 { "decayMs", "2001" }, { "releaseMs", "0" },
                                 { "releaseMs", "5001" }, { "chokeFadeMs", "0" },
                                 { "chokeFadeMs", "101" } })
    {
        const auto invalidManifest = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getNonexistentChildFile("liberty-out-of-range", ".xml");
        juce::XmlElement invalidKit("LibertyDrumKit");
        auto* pad = invalidKit.createNewChildElement("Pad");
        pad->setAttribute("index", 0);
        pad->setAttribute(invalid.first, invalid.second);
        if (!invalidKit.writeTo(invalidManifest)) { fixture.deleteFile(); return fail(44); }
        const bool imported = protectedKit.importPortableKit(invalidManifest);
        invalidManifest.deleteFile();
        if (imported || !protectedKit.hasSample(0)
            || !near(protectedKit.getPad(0).gain.load(), 0.75f))
        { fixture.deleteFile(); return fail(45); }
    }
    // Individually valid trim endpoints must also define a usable interval.
    for (const auto& trim : { std::pair<double, double> { 0.8, 0.4 },
                              { 0.5, 0.5 }, { 0.50, 0.505 } })
    {
        const auto invalidManifest = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getNonexistentChildFile("liberty-invalid-trim", ".xml");
        juce::XmlElement invalidKit("LibertyDrumKit");
        auto* pad = invalidKit.createNewChildElement("Pad");
        pad->setAttribute("index", 0);
        pad->setAttribute("start", trim.first);
        pad->setAttribute("end", trim.second);
        if (!invalidKit.writeTo(invalidManifest)) { fixture.deleteFile(); return fail(46); }
        const bool imported = protectedKit.importPortableKit(invalidManifest);
        invalidManifest.deleteFile();
        if (imported || !protectedKit.hasSample(0)
            || !near(protectedKit.getPad(0).gain.load(), 0.75f))
        { fixture.deleteFile(); return fail(47); }
    }
    // Atomic multi-pad import: a valid first pad must not be committed if
    // a later pad is malformed. Preserve all existing pad settings and audio.
    const auto partial = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("liberty-partial-kit", ".xml");
    {
        juce::XmlElement kit("LibertyDrumKit");
        auto* first = kit.createNewChildElement("Pad");
        first->setAttribute("index", 0);
        first->setAttribute("file", fixture.getFullPathName());
        first->setAttribute("gain", 0.25);
        auto* second = kit.createNewChildElement("Pad");
        second->setAttribute("index", 1);
        second->setAttribute("file", "missing-audio-file.wav");
        if (!kit.writeTo(partial)) { fixture.deleteFile(); return fail(48); }
    }
    const bool partialImported = protectedKit.importPortableKit(partial);
    partial.deleteFile();
    if (partialImported || !protectedKit.hasSample(0)
        || !near(protectedKit.getPad(0).gain.load(), 0.75f))
    { fixture.deleteFile(); return fail(49); }
    juce::AudioBuffer<float> stillWorking(2, 128);
    stillWorking.clear();
    protectedKit.reset();
    protectedKit.renderMidi(stillWorking, hit);
    if (!near(stillWorking.getSample(0, 80), 0.375f, 0.003f))
    { fixture.deleteFile(); return fail(50); }
    // A later pad with an invalid audio setting must also abort the whole
    // import, even if the preceding pad was valid and changed its gain.
    const auto invalidSecond = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("liberty-invalid-second-pad", ".xml");
    {
        juce::XmlElement kit("LibertyDrumKit");
        auto* first = kit.createNewChildElement("Pad");
        first->setAttribute("index", 0);
        first->setAttribute("file", fixture.getFullPathName());
        first->setAttribute("gain", 0.25);
        auto* second = kit.createNewChildElement("Pad");
        second->setAttribute("index", 1);
        second->setAttribute("gain", "not-a-number");
        if (!kit.writeTo(invalidSecond)) { fixture.deleteFile(); return fail(51); }
    }
    const bool invalidSecondImported = protectedKit.importPortableKit(invalidSecond);
    invalidSecond.deleteFile();
    if (invalidSecondImported || !protectedKit.hasSample(0)
        || !near(protectedKit.getPad(0).gain.load(), 0.75f))
    { fixture.deleteFile(); return fail(52); }
    // Portable kit round-trip must preserve sample data and pad controls.
    const auto exportFolder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("liberty-roundtrip", "");
    if (!exportFolder.createDirectory()) { fixture.deleteFile(); return fail(53); }
    const auto exportedManifest = exportFolder.getChildFile("roundtrip.xml");
    LibertyDrumSampler exportSource;
    exportSource.prepare(48000.0);
    if (!exportSource.loadPad(0, fixture)) { exportFolder.deleteRecursively(); fixture.deleteFile(); return fail(54); }
    exportSource.setPadGain(0, 0.8f);
    exportSource.setPadPan(0, -0.25f);
    exportSource.setPadPitch(0, 5.0f);
    exportSource.setPadChokeGroup(0, 2);
    exportSource.setPadChokeFade(0, 18.0f);
    exportSource.setPadGateMode(0, true);
    exportSource.setPadEnvelope(0, 0.0f, 0.0f, 1.0f, 80.0f);
    if (!exportSource.exportPortableKit(exportedManifest)
        || !exportedManifest.existsAsFile())
    { exportFolder.deleteRecursively(); fixture.deleteFile(); return fail(55); }
    LibertyDrumSampler importedRoundtrip;
    importedRoundtrip.prepare(48000.0);
    if (!importedRoundtrip.importPortableKit(exportedManifest)
        || !importedRoundtrip.hasSample(0))
    { exportFolder.deleteRecursively(); fixture.deleteFile(); return fail(56); }
    const auto& roundtripPad = importedRoundtrip.getPad(0);
    if (!near(roundtripPad.gain.load(), 0.8f)
        || !near(roundtripPad.pan.load(), -0.25f)
        || !near(roundtripPad.pitchSemitones.load(), 5.0f)
        || roundtripPad.chokeGroup.load() != 2
        || !near(roundtripPad.chokeFadeMs.load(), 18.0f)
        || !roundtripPad.gateMode.load()
        || !near(roundtripPad.releaseMs.load(), 80.0f))
    { exportFolder.deleteRecursively(); fixture.deleteFile(); return fail(57); }
    importedRoundtrip.reset();
    juce::AudioBuffer<float> roundtripAudio(2, 128);
    roundtripAudio.clear();
    importedRoundtrip.renderMidi(roundtripAudio, hit);
    if (std::abs(roundtripAudio.getSample(0, 80)) < 0.01f)
    { exportFolder.deleteRecursively(); fixture.deleteFile(); return fail(58); }
    // A portable kit must survive moving its entire folder, because the
    // manifest references samples relative to its own location.
    const auto relocatedFolder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("liberty-relocated-kit", "");
    if (!exportFolder.moveFileTo(relocatedFolder))
    { exportFolder.deleteRecursively(); fixture.deleteFile(); return fail(59); }
    const auto relocatedManifest = relocatedFolder.getChildFile("roundtrip.xml");
    LibertyDrumSampler relocatedKit;
    relocatedKit.prepare(48000.0);
    if (!relocatedKit.importPortableKit(relocatedManifest)
        || !relocatedKit.hasSample(0)
        || !near(relocatedKit.getPad(0).gain.load(), 0.8f)
        || !near(relocatedKit.getPad(0).pan.load(), -0.25f))
    { relocatedFolder.deleteRecursively(); fixture.deleteFile(); return fail(60); }
    juce::AudioBuffer<float> relocatedAudio(2, 128);
    relocatedAudio.clear();
    relocatedKit.renderMidi(relocatedAudio, hit);
    if (std::abs(relocatedAudio.getSample(0, 80)) < 0.01f)
    { relocatedFolder.deleteRecursively(); fixture.deleteFile(); return fail(61); }
    relocatedFolder.deleteRecursively();
    // Multiple pads must receive unique sample destinations and round-trip.
    const auto multiFolder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("liberty-multipad-export", "");
    if (!multiFolder.createDirectory()) { fixture.deleteFile(); return fail(62); }
    const auto secondFixture = multiFolder.getChildFile("another-sample.wav");
    if (!fixture.copyFileTo(secondFixture))
    { multiFolder.deleteRecursively(); fixture.deleteFile(); return fail(63); }
    LibertyDrumSampler multiSource;
    multiSource.prepare(48000.0);
    if (!multiSource.loadPad(0, fixture) || !multiSource.loadPad(1, secondFixture))
    { multiFolder.deleteRecursively(); fixture.deleteFile(); return fail(64); }
    multiSource.setPadGain(0, 0.6f);
    multiSource.setPadGain(1, 0.9f);
    const auto multiManifest = multiFolder.getChildFile("multi.xml");
    if (!multiSource.exportPortableKit(multiManifest))
    { multiFolder.deleteRecursively(); fixture.deleteFile(); return fail(65); }
    const auto sampleDir = multiFolder.getChildFile("multi_samples");
    if (!sampleDir.getChildFile("pad_01.wav").existsAsFile()
        || !sampleDir.getChildFile("pad_02.wav").existsAsFile())
    { multiFolder.deleteRecursively(); fixture.deleteFile(); return fail(66); }
    LibertyDrumSampler multiImported;
    multiImported.prepare(48000.0);
    if (!multiImported.importPortableKit(multiManifest)
        || !multiImported.hasSample(0) || !multiImported.hasSample(1)
        || !near(multiImported.getPad(0).gain.load(), 0.6f)
        || !near(multiImported.getPad(1).gain.load(), 0.9f))
    { multiFolder.deleteRecursively(); fixture.deleteFile(); return fail(67); }
    multiFolder.deleteRecursively();
    // A missing source sample must fail export without publishing a manifest.
    const auto missingFolder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("liberty-missing-export", "");
    if (!missingFolder.createDirectory()) { fixture.deleteFile(); return fail(68); }
    const auto transientSample = missingFolder.getChildFile("transient.wav");
    if (!fixture.copyFileTo(transientSample))
    { missingFolder.deleteRecursively(); fixture.deleteFile(); return fail(69); }
    LibertyDrumSampler missingSource;
    missingSource.prepare(48000.0);
    if (!missingSource.loadPad(0, transientSample))
    { missingFolder.deleteRecursively(); fixture.deleteFile(); return fail(70); }
    transientSample.deleteFile();
    const auto missingManifest = missingFolder.getChildFile("should-not-exist.xml");
    if (missingSource.exportPortableKit(missingManifest)
        || missingManifest.existsAsFile())
    { missingFolder.deleteRecursively(); fixture.deleteFile(); return fail(71); }
    missingFolder.deleteRecursively();
    // If the second source disappears, the staged copy for the first pad
    // must be removed and no manifest should be published.
    const auto stagedFolder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("liberty-staged-rollback", "");
    if (!stagedFolder.createDirectory()) { fixture.deleteFile(); return fail(72); }
    const auto disappearing = stagedFolder.getChildFile("disappearing.wav");
    if (!fixture.copyFileTo(disappearing))
    { stagedFolder.deleteRecursively(); fixture.deleteFile(); return fail(73); }
    LibertyDrumSampler stagedSource;
    stagedSource.prepare(48000.0);
    if (!stagedSource.loadPad(0, fixture) || !stagedSource.loadPad(1, disappearing))
    { stagedFolder.deleteRecursively(); fixture.deleteFile(); return fail(74); }
    disappearing.deleteFile();
    const auto stagedManifest = stagedFolder.getChildFile("staged.xml");
    const auto stagedTemp = stagedFolder.getChildFile("staged_samples")
        .getChildFile("pad_01.wav.tmp");
    if (stagedSource.exportPortableKit(stagedManifest)
        || stagedManifest.existsAsFile() || stagedTemp.existsAsFile())
    { stagedFolder.deleteRecursively(); fixture.deleteFile(); return fail(75); }
    stagedFolder.deleteRecursively();
    // Failed re-export must not overwrite an existing, previously valid manifest.
    const auto preserveFolder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("liberty-preserve-export", "");
    if (!preserveFolder.createDirectory()) { fixture.deleteFile(); return fail(76); }
    const auto preserveManifest = preserveFolder.getChildFile("kit.xml");
    LibertyDrumSampler preserveSource;
    preserveSource.prepare(48000.0);
    if (!preserveSource.loadPad(0, fixture)
        || !preserveSource.exportPortableKit(preserveManifest))
    { preserveFolder.deleteRecursively(); fixture.deleteFile(); return fail(77); }
    const auto originalManifest = preserveManifest.loadFileAsString();
    const auto disappearingAgain = preserveFolder.getChildFile("vanishing.wav");
    if (!fixture.copyFileTo(disappearingAgain)
        || !preserveSource.loadPad(1, disappearingAgain))
    { preserveFolder.deleteRecursively(); fixture.deleteFile(); return fail(78); }
    disappearingAgain.deleteFile();
    if (preserveSource.exportPortableKit(preserveManifest)
        || preserveManifest.loadFileAsString() != originalManifest)
    { preserveFolder.deleteRecursively(); fixture.deleteFile(); return fail(79); }
    preserveFolder.deleteRecursively();
    // Repeated pad hits restart long samples without stacking their audio.
    LibertyDrumSampler retrigger;
    retrigger.prepare(48000.0);
    if (!retrigger.loadPad(0, fixture)) { fixture.deleteFile(); return fail(80); }
    retrigger.setPadEnvelope(0, 0.0f, 0.0f, 1.0f, 8.0f);
    juce::AudioBuffer<float> firstHit(2, 128), repeatedHit(2, 128);
    firstHit.clear();
    repeatedHit.clear();
    retrigger.auditionPad(0, 1.0f);
    retrigger.processAuditions();
    retrigger.render(firstHit, 0, 128);
    retrigger.auditionPad(0, 1.0f);
    retrigger.processAuditions();
    retrigger.render(repeatedHit, 0, 128);
    if (!near(firstHit.getSample(0, 80), repeatedHit.getSample(0, 80), 0.003f))
    { fixture.deleteFile(); return fail(81); }
    retrigger.stopPad(0);
    juce::AudioBuffer<float> stoppedHit(2, 128);
    stoppedHit.clear();
    retrigger.renderMidi(stoppedHit, juce::MidiBuffer{});
    if (std::abs(stoppedHit.getSample(0, 80)) > 0.003f)
    { fixture.deleteFile(); return fail(82); }
    // Factory hi-hat MIDI 52 must produce an audible semi-open articulation
    // while the ordinary open-hat pad remains mapped to MIDI 40.
    LibertyDrumSampler factoryHats;
    factoryHats.prepare(48000.0);
    if (!factoryHats.loadFactoryKit(0)) { fixture.deleteFile(); return fail(83); }
    if (factoryHats.getPad(4).midiNote != 40
        || !factoryHats.getPad(4).factoryTone.load())
    { fixture.deleteFile(); return fail(84); }
    juce::AudioBuffer<float> semiHat(2, 2048), openHat(2, 2048);
    juce::MidiBuffer semiNote, openNote;
    semiNote.addEvent(juce::MidiMessage::noteOn(1, 52, (juce::uint8)110), 0);
    openNote.addEvent(juce::MidiMessage::noteOn(1, 40, (juce::uint8)110), 0);
    semiHat.clear();
    factoryHats.renderMidi(semiHat, semiNote);
    factoryHats.reset();
    openHat.clear();
    factoryHats.renderMidi(openHat, openNote);
    double semiEnergy = 0.0, openEnergy = 0.0;
    for (int frame = 0; frame < semiHat.getNumSamples(); ++frame)
    {
        const float semi = semiHat.getSample(0, frame);
        const float open = openHat.getSample(0, frame);
        if (!std::isfinite(semi) || !std::isfinite(open))
        { fixture.deleteFile(); return fail(85); }
        if (frame >= 1024)
        {
            semiEnergy += (double)semi * semi;
            openEnergy += (double)open * open;
        }
    }
    if (!(semiEnergy > 0.0 && openEnergy > semiEnergy))
    { fixture.deleteFile(); return fail(86); }
    // Imported pads must never acquire the factory-only MIDI 52 alias.
    LibertyDrumSampler importedHats;
    importedHats.prepare(48000.0);
    if (!importedHats.loadPad(4, fixture))
    { fixture.deleteFile(); return fail(87); }
    juce::AudioBuffer<float> importedAlias(2, 256);
    importedAlias.clear();
    importedHats.renderMidi(importedAlias, semiNote);
    if (!near(importedAlias.getSample(0, 100), 0.0f, 0.0001f))
    { fixture.deleteFile(); return fail(88); }
    // A closed factory hat must fade an already ringing semi-open hat
    // smoothly. Mute the new closed hit to isolate the outgoing voice.
    factoryHats.reset();
    factoryHats.setPadGain(3, 0.0f);
    juce::AudioBuffer<float> pedalFade(2, 2400);
    pedalFade.clear();
    juce::MidiBuffer pedalMidi;
    pedalMidi.addEvent(juce::MidiMessage::noteOn(1, 52, (juce::uint8)127), 0);
    pedalMidi.addEvent(juce::MidiMessage::noteOn(1, 39, (juce::uint8)127), 800);
    factoryHats.renderMidi(pedalFade, pedalMidi);
    double preFadeEnergy = 0.0, midFadeEnergy = 0.0, tailFadeEnergy = 0.0;
    for (int frame = 0; frame < pedalFade.getNumSamples(); ++frame)
    {
        const double value = pedalFade.getSample(0, frame);
        if (!std::isfinite(value)) { fixture.deleteFile(); return fail(89); }
        if (frame >= 700 && frame < 800) preFadeEnergy += value * value;
        if (frame >= 950 && frame < 1050) midFadeEnergy += value * value;
        if (frame >= 1300 && frame < 1400) tailFadeEnergy += value * value;
    }
    if (!(preFadeEnergy > 0.0 && midFadeEnergy > 0.0
          && tailFadeEnergy < midFadeEnergy * 0.01))
    { fixture.deleteFile(); return fail(90); }
    // Opening the factory hat must not choke a closed-hat voice.
    // Compare the closed-only tail with the same hit followed by a
    // silent open-hat trigger; both should retain the same tail.
    factoryHats.reset();
    factoryHats.setPadGain(3, 1.0f);
    factoryHats.setPadGain(4, 0.0f);
    juce::AudioBuffer<float> closedReference(2, 1200), closedThenOpen(2, 1200);
    juce::MidiBuffer closedNote, openAfterClosed;
    closedNote.addEvent(juce::MidiMessage::noteOn(1, 39, (juce::uint8)127), 0);
    openAfterClosed.addEvent(juce::MidiMessage::noteOn(1, 39, (juce::uint8)127), 0);
    openAfterClosed.addEvent(juce::MidiMessage::noteOn(1, 40, (juce::uint8)127), 100);
    closedReference.clear();
    factoryHats.renderMidi(closedReference, closedNote);
    // Reloading the same factory kit resets its deterministic round-robin
    // counters, so both comparisons use the identical closed-hat variation.
    if (!factoryHats.loadFactoryKit(0))
    { fixture.deleteFile(); return fail(93); }
    factoryHats.setPadGain(4, 0.0f);
    // Kit changes queue a stop-all command, consumed on the next render.
    // Flush it before the comparison so the first MIDI hit is not cleared.
    juce::AudioBuffer<float> kitFlush(2, 64);
    kitFlush.clear();
    factoryHats.renderMidi(kitFlush, juce::MidiBuffer{});
    closedThenOpen.clear();
    factoryHats.renderMidi(closedThenOpen, openAfterClosed);
    double referenceEnergy = 0.0, protectedEnergy = 0.0;
    for (int frame = 120; frame < 500; ++frame)
    {
        const double reference = closedReference.getSample(0, frame);
        const double withOpen = closedThenOpen.getSample(0, frame);
        if (!std::isfinite(reference) || !std::isfinite(withOpen))
        { fixture.deleteFile(); return fail(91); }
        referenceEnergy += reference * reference;
        protectedEnergy += withOpen * withOpen;
    }
    // Round-robin variations can change waveforms, but the closed-hat
    // tail must remain audible after an open-hat trigger.
    if (!(referenceEnergy > 1.0e-10
        && protectedEnergy > referenceEnergy * 0.10))
    { fixture.deleteFile(); return fail(92); }
    // The protected closed hat must also retain measurable energy later
    // in its tail, rather than only immediately after the open trigger.
    double laterReferenceEnergy = 0.0, laterProtectedEnergy = 0.0;
    for (int frame = 500; frame < 850; ++frame)
    {
        const double reference = closedReference.getSample(0, frame);
        const double withOpen = closedThenOpen.getSample(0, frame);
        if (!std::isfinite(reference) || !std::isfinite(withOpen))
        { fixture.deleteFile(); return fail(94); }
        laterReferenceEnergy += reference * reference;
        laterProtectedEnergy += withOpen * withOpen;
    }
    if (!(laterReferenceEnergy > 1.0e-12
        && laterProtectedEnergy > laterReferenceEnergy * 0.10))
    { fixture.deleteFile(); return fail(95); }
    // Switching from a factory kit to a custom WAV pad must revoke the
    // factory-only MIDI 52 semi-open articulation immediately.
    factoryHats.reset();
    if (!factoryHats.loadPad(4, fixture))
    { fixture.deleteFile(); return fail(96); }
    if (factoryHats.getPad(4).factoryTone.load())
    { fixture.deleteFile(); return fail(97); }
    // Previous choke tests deliberately muted this pad; restore its gain
    // before asserting that the replacement WAV remains audible.
    factoryHats.setPadGain(4, 1.0f);
    juce::AudioBuffer<float> replacedAlias(2, 256), replacedNormal(2, 256);
    replacedAlias.clear();
    factoryHats.renderMidi(replacedAlias, semiNote);
    factoryHats.reset();
    replacedNormal.clear();
    factoryHats.renderMidi(replacedNormal, openNote);
    double aliasEnergy = 0.0, normalEnergy = 0.0;
    for (int frame = 0; frame < 256; ++frame)
    {
        const double alias = replacedAlias.getSample(0, frame);
        const double normal = replacedNormal.getSample(0, frame);
        if (!std::isfinite(alias) || !std::isfinite(normal))
        { fixture.deleteFile(); return fail(98); }
        aliasEnergy += alias * alias;
        normalEnergy += normal * normal;
    }
    if (!(aliasEnergy < 1.0e-12 && normalEnergy > 1.0e-10))
    { fixture.deleteFile(); return fail(99); }
    fixture.deleteFile();

    std::cout << "Drum Sampler smoke tests passed\\n";
    return 0;
}
