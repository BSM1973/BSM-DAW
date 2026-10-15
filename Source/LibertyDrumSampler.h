#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>
#include <array>
#include <memory>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <atomic>

// Liberty Drum Sampler: first milestone, 16 velocity-sensitive one-shot pads.
// Prepare and load on the message thread, then render on the audio thread.
class LibertyDrumSampler final
{
public:
    static constexpr int padCount = 16;
    struct Pad
    {
        juce::String name;
        juce::String sourcePath;
        std::shared_ptr<const juce::AudioBuffer<float>> audio;
        std::atomic<double> sourceRate { 44100.0 };
        std::atomic<float> gain { 1.0f };
        std::atomic<float> pan { 0.0f };
        std::atomic<float> pitchSemitones { 0.0f };
        std::atomic<float> startFraction { 0.0f };
        std::atomic<float> endFraction { 1.0f };
        std::atomic<bool> loopEnabled { false };
        std::atomic<int> colourArgb { (int)0xff253c4a };
        std::atomic<bool> factoryTone { false };
        std::atomic<int> chokeGroup { 0 };
        std::atomic<float> chokeFadeMs { 8.0f };
        std::atomic<bool> gateMode { false };
        std::atomic<float> attackMs { 0.0f }, decayMs { 0.0f }, sustain { 1.0f }, releaseMs { 8.0f };
        int midiNote = 36;
    };
    LibertyDrumSampler()
    {
        for (int i = 0; i < padCount; ++i)
        {
            pads[(size_t)i].name = "Pad " + juce::String(i + 1);
            pads[(size_t)i].midiNote = 36 + i;
        }
        formats.registerBasicFormats();
    }
    void prepare(double rate) noexcept { outputRate = rate > 0 ? rate : 44100.0; reset(); }
    // UI threads queue auditions; the audio callback owns the voices.
    void auditionPad(int index, float velocity = 1.0f) noexcept
    {
        if (index >= 0 && index < padCount)
        {
            pendingVelocity[(size_t)index].store(juce::jlimit(0.0f, 1.0f, velocity), std::memory_order_relaxed);
            pendingTriggers.fetch_or(static_cast<unsigned int>(1u << index), std::memory_order_release);
        }
    }
    void stopPad(int index) noexcept
    {
        if (index >= 0 && index < padCount)
        {
            pendingTriggers.fetch_and(~(1u << index), std::memory_order_acq_rel);
            pendingStops.fetch_or(1u << index, std::memory_order_release);
        }
    }
    void processAuditions() noexcept
    {
        if (pendingStopAll.exchange(false, std::memory_order_acq_rel))
        {
            pendingTriggers.store(0, std::memory_order_release);
            for (auto& voice : voices)
            {
                voice.active = false;
                voice.audio.reset();
            }
            for (auto& position : playbackPositions)
                position.store(-1.0f, std::memory_order_relaxed);
        }
        const auto stops = pendingStops.exchange(0, std::memory_order_acquire);
        for (auto& voice : voices)
            if (voice.active && (stops & (1u << voice.pad)) != 0)
            {
                voice.active = false;
                voice.audio.reset();
            }
        const auto triggered = pendingTriggers.exchange(0, std::memory_order_acquire);
        for (int i = 0; i < padCount; ++i)
            if ((triggered & (1u << i)) != 0)
                noteOn(36 + i, pendingVelocity[(size_t)i].load(std::memory_order_relaxed), true);
    }
    void reset() noexcept
    {
        for (auto& voice : voices) voice.active = false;
        for (auto& position : playbackPositions) position.store(-1.0f, std::memory_order_relaxed);
    }
    float getPlaybackPosition(int index) const noexcept
    {
        return index >= 0 && index < padCount
            ? playbackPositions[(size_t)index].load(std::memory_order_relaxed) : -1.0f;
    }
    bool loadPad(int index, const juce::File& file)
    {
        if (index < 0 || index >= padCount || !file.existsAsFile()) return false;
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (!reader || reader->lengthInSamples <= 0 || reader->lengthInSamples > 44100LL * 60 * 5)
            return false;
        juce::AudioBuffer<float> loaded(juce::jlimit(1, 2, (int)reader->numChannels),
                                        (int)reader->lengthInSamples);
        if (!reader->read(&loaded, 0, loaded.getNumSamples(), 0, true, true)) return false;
        auto published = std::make_shared<const juce::AudioBuffer<float>>(std::move(loaded));
        std::atomic_store(&pads[(size_t)index].audio, std::move(published));
        pads[(size_t)index].sourceRate.store(reader->sampleRate);
        pads[(size_t)index].name = file.getFileNameWithoutExtension();
        pads[(size_t)index].sourcePath = file.getFullPathName();
        pads[(size_t)index].factoryTone.store(false);
        return true;
    }
    // Self-contained synthesized factory sounds: no external sample files required.
    // Acoustic-style timbres are approximations until licensed recordings are bundled.
    bool loadFactoryKit(int kitIndex)
    {
        if (kitIndex < 0 || kitIndex >= 10) return false;
        static constexpr const char* instruments[16] = {
            "KICK", "SNARE", "CLAP", "CLOSED HAT", "OPEN HAT", "LOW TOM",
            "MID TOM", "HIGH TOM", "RIM", "SHAKER", "PERC 1", "PERC 2",
            "CRASH", "RIDE", "FX", "SUB KICK" };
        const double sr = 44100.0;
        const double pi = juce::MathConstants<double>::pi;
        for (int padIndex = 0; padIndex < padCount; ++padIndex)
        {
            const int category = padIndex;
            const double duration = category == 4 || category == 12 || category == 13 ? 1.0
                : category == 15 ? 0.8 : category >= 5 && category <= 7 ? 0.55 : 0.35;
            const int length = (int)(sr * duration);
            juce::AudioBuffer<float> generated(1, length);
            juce::Random random(0x5a17 + kitIndex * 137 + padIndex * 31);
            double phase = 0.0;
            for (int n = 0; n < length; ++n)
            {
                const double t = n / sr;
                const double progress = t / duration;
                const double noise = random.nextFloat() * 2.0 - 1.0;
                double value = 0.0;
                if (category == 0 || category == 15)
                {
                    const double base = category == 15 ? 44.0 : 52.0 + kitIndex * 2.0;
                    phase += 2.0 * pi * (base + 125.0 * std::exp(-t * 38.0)) / sr;
                    value = std::sin(phase) * std::exp(-t * (category == 15 ? 5.0 : 14.0));
                }
                else if (category == 1 || category == 2 || category == 8)
                {
                    phase += 2.0 * pi * (category == 8 ? 900.0 : 185.0) / sr;
                    const double decay = category == 8 ? 50.0 : (category == 2 ? 24.0 : 17.0);
                    value = (noise * 0.8 + std::sin(phase) * 0.2) * std::exp(-t * decay);
                    if (category == 2) value *= (0.6 + 0.4 * std::sin(2.0 * pi * 28.0 * t));
                }
                else if (category == 5 || category == 6 || category == 7)
                {
                    const double freq = (category == 5 ? 115.0 : category == 6 ? 175.0 : 245.0) * (1.0 + 0.02 * kitIndex);
                    phase += 2.0 * pi * (freq + 90.0 * std::exp(-t * 18.0)) / sr;
                    value = (std::sin(phase) + 0.18 * noise) * std::exp(-t * 11.0);
                }
                else
                {
                    // Closed hats have a short pedal-damped envelope; open
                    // hats ring longer, with genre-specific decay character.
                    const double decay = category == 3 ? (kitIndex == 8 ? 43.0 : 34.0)
                        : category == 4 ? (kitIndex == 3 ? 4.0 : 6.0)
                        : category == 12 || category == 13 ? 5.0 : 29.0;
                    // Differentiated metallic/noisy percussion timbres.
                    phase += 2.0 * pi * (4300.0 + category * 160.0 + kitIndex * 35.0) / sr;
                    value = (0.75 * noise + 0.25 * std::sin(phase)) * std::exp(-t * decay);
                }
                // Per-style timbre: acoustic families emphasize resonant drums;
                // electronic styles emphasize sub, metallic hats and saturation.
                const bool electronic = kitIndex == 0 || kitIndex == 4 || kitIndex == 9;
                const bool heavy = kitIndex == 2 || kitIndex == 3;
                if (category == 0 || category == 15)
                {
                    if (electronic)
                        value += 0.28 * std::sin(phase * 0.5) * std::exp(-t * (kitIndex == 9 ? 3.0 : 8.0));
                    if (heavy)
                        value += 0.16 * noise * std::exp(-t * 110.0);
                    if (kitIndex == 5) value *= 0.78; // Reggae: softer, rounder kick.
                }
                else if (category == 1 || category == 2 || category == 8)
                {
                    if (kitIndex == 3) value += 0.20 * noise * std::exp(-t * 33.0);
                    if (kitIndex == 8) value *= 0.72; // Funk: tight, dry snare.
                    if (kitIndex == 7) value += 0.25 * std::sin(phase * 2.4) * std::exp(-t * 27.0);
                    if (kitIndex == 9 && category == 2) value *= 1.3;
                }
                else if (category == 3 || category == 4 || category == 9)
                {
                    if (kitIndex == 9) value *= 0.65 + 0.35 * std::sin(2.0 * pi * 55.0 * t);
                    if (kitIndex == 4) value += 0.15 * std::sin(phase * 1.7) * std::exp(-t * 20.0);
                }
                else if (kitIndex == 7 && category >= 10 && category <= 13)
                {
                    // Oriental: resonant hand-drum and jingling percussion character.
                    value = 0.55 * value + 0.4 * std::sin(phase * (category == 10 ? 0.27 : 0.53))
                        * std::exp(-t * (category == 10 ? 13.0 : 24.0));
                }
                if (!electronic)
                {
                    // Acoustic-inspired layering: drumhead resonance, snare wires,
                    // and inharmonic cymbal partials, with genre-dependent damping.
                    if (category == 1)
                    {
                        const double body = std::sin(2.0 * pi * (172.0 + 60.0 * std::exp(-t * 24.0)) * t);
                        const double wires = noise * std::exp(-t * (kitIndex == 8 ? 33.0 : 16.0));
                        value = 0.50 * value + 0.28 * body * std::exp(-t * 19.0) + 0.22 * wires;
                    }
                    else if (category >= 5 && category <= 7)
                    {
                        const double fundamental = category == 5 ? 106.0 : category == 6 ? 155.0 : 217.0;
                        value += 0.22 * std::sin(2.0 * pi * fundamental * 1.51 * t)
                            * std::exp(-t * (kitIndex == 3 ? 7.0 : 11.0));
                    }
                    else if (category == 12 || category == 13)
                    {
                        const double metallic = std::sin(2.0 * pi * 1731.0 * t)
                            + 0.6 * std::sin(2.0 * pi * 2879.0 * t)
                            + 0.35 * std::sin(2.0 * pi * 4177.0 * t);
                        value = 0.62 * value + 0.13 * metallic * std::exp(-t * 3.8);
                    }
                }
                if (electronic) value = std::tanh(value * (kitIndex == 4 ? 2.0 : 1.55));
                const double attack = juce::jmin(1.0, t * 2500.0);
                generated.setSample(0, n, (float)juce::jlimit(-1.0, 1.0, value * attack * (0.7 + 0.03 * kitIndex)));
            }
            auto& pad = pads[(size_t)padIndex];
            std::atomic_store(&pad.audio, std::make_shared<const juce::AudioBuffer<float>>(std::move(generated)));
            pad.sourceRate.store(sr);
            pad.name = instruments[padIndex];
            // Instrument labels reflect the selected genre, not only the generic
            // synthesis category. MIDI note assignments remain unchanged.
            if (kitIndex == 7)
            {
                static constexpr const char* orientalNames[16] = {
                    "DOUM", "TEK", "KA", "RIQ CLOSED", "RIQ OPEN", "DARBUKA LOW",
                    "DARBUKA MID", "DARBUKA HIGH", "RIM TEK", "SHAKER", "DOUMBEK",
                    "FRAME DRUM", "ZILLS", "RIQ JINGLE", "PERC FX", "BASS DOUM" };
                pad.name = orientalNames[padIndex];
            }
            else if (kitIndex == 9)
            {
                static constexpr const char* trapNames[16] = {
                    "808 KICK", "TRAP SNARE", "TRAP CLAP", "HAT CLOSED", "HAT OPEN",
                    "808 TOM LOW", "808 TOM MID", "808 TOM HIGH", "RIM", "HAT ROLL",
                    "PERC A", "PERC B", "CRASH", "METAL", "RISER FX", "808 SUB" };
                pad.name = trapNames[padIndex];
            }
            else if (kitIndex == 5)
            {
                static constexpr const char* reggaeNames[16] = {
                    "ONE DROP KICK", "CROSS STICK", "CLAP", "CLOSED HAT", "OPEN HAT",
                    "LOW TOM", "MID TOM", "HIGH TOM", "RIMSHOT", "SHAKER",
                    "BONGO LOW", "BONGO HIGH", "CRASH", "RIDE", "DUB FX", "SUB KICK" };
                pad.name = reggaeNames[padIndex];
            }
            else if (kitIndex == 3 && padIndex == 15) pad.name = "DOUBLE KICK";
            else if (kitIndex == 4 && padIndex == 15) pad.name = "SYNTH KICK";
            pad.sourcePath.clear();
            pad.factoryTone.store(true);
            pad.gain.store(1.0f);
            pad.pan.store(0.0f);
            pad.pitchSemitones.store(0.0f);
            pad.startFraction.store(0.0f);
            pad.endFraction.store(1.0f);
            pad.loopEnabled.store(false);
            // Factory kit loading must not inherit envelopes or choke settings
            // from a previously edited or imported kit.
            pad.chokeGroup.store(padIndex == 3 || padIndex == 4 ? 1 : 0);
            pad.chokeFadeMs.store(padIndex == 3 ? 5.0f : 8.0f);
            pad.gateMode.store(false);
            pad.attackMs.store(0.0f);
            pad.decayMs.store(0.0f);
            pad.sustain.store(1.0f);
            pad.releaseMs.store(8.0f);
            pad.colourArgb.store((int)(padIndex < 3 ? 0xffbb5147u : padIndex < 5 ? 0xffd8a343u
                : padIndex < 8 ? 0xff3b91a8u : padIndex < 12 ? 0xff7c65bbu : 0xff477a67u));
        }
        // Restart each pad's deterministic timbre cycle for the new kit.
        // Atomics avoid a data race with the audio callback.
        for (auto& counter : factoryHitCounters)
            counter.store(0u, std::memory_order_relaxed);
        // Stop voices on the audio thread, not from the UI thread.
        pendingTriggers.store(0, std::memory_order_release);
        pendingStopAll.store(true, std::memory_order_release);
        return true;
    }
    // Serializable kit description; sample audio remains in external WAV/AIFF files.
    std::unique_ptr<juce::XmlElement> createKitXml() const
    {
        auto kit = std::make_unique<juce::XmlElement>("LibertyDrumKit");
        kit->setAttribute("version", 1);
        for (int i = 0; i < padCount; ++i)
        {
            const auto& pad = pads[(size_t)i];
            auto* node = kit->createNewChildElement("Pad");
            node->setAttribute("index", i);
            node->setAttribute("name", pad.name);
            node->setAttribute("file", pad.sourcePath);
            node->setAttribute("gain", (double)pad.gain.load());
            node->setAttribute("pan", (double)pad.pan.load());
            node->setAttribute("pitch", (double)pad.pitchSemitones.load());
            node->setAttribute("start", (double)pad.startFraction.load());
            node->setAttribute("end", (double)pad.endFraction.load());
            node->setAttribute("loopEnabled", pad.loopEnabled.load() ? 1 : 0);
            node->setAttribute("colour", juce::String::toHexString(pad.colourArgb.load()));
            node->setAttribute("chokeGroup", pad.chokeGroup.load());
            node->setAttribute("chokeFadeMs", (double)pad.chokeFadeMs.load());
            node->setAttribute("gateMode", pad.gateMode.load() ? 1 : 0);
            node->setAttribute("attackMs", (double)pad.attackMs.load());
            node->setAttribute("decayMs", (double)pad.decayMs.load());
            node->setAttribute("sustain", (double)pad.sustain.load());
            node->setAttribute("releaseMs", (double)pad.releaseMs.load());
        }
        return kit;
    }
    bool restoreKitXml(const juce::XmlElement& kit)
    {
        if (!kit.hasTagName("LibertyDrumKit")) return false;
        for (auto* node : kit.getChildIterator())
        {
            if (!node->hasTagName("Pad")) continue;
            const int i = node->getIntAttribute("index", -1);
            if (i < 0 || i >= padCount) continue;
            const auto path = node->getStringAttribute("file");
            if (path.isNotEmpty())
                loadPad(i, juce::File(path));
            const auto restoredName = node->getStringAttribute("name");
            if (restoredName.isNotEmpty())
                pads[(size_t)i].name = restoredName.substring(0, 80);
            setPadGain(i, (float)node->getDoubleAttribute("gain", 1.0));
            setPadPan(i, (float)node->getDoubleAttribute("pan", 0.0));
            setPadPitch(i, (float)node->getDoubleAttribute("pitch", 0.0));
            setPadTrim(i, (float)node->getDoubleAttribute("start", 0.0),
                          (float)node->getDoubleAttribute("end", 1.0));
            setPadLoop(i, node->getIntAttribute("loopEnabled", 0) != 0);
            const auto colourText = node->getStringAttribute("colour");
            if (colourText.isNotEmpty() && colourText.length() == 8
                && colourText.containsOnly("0123456789abcdefABCDEF"))
                setPadColour(i, (int)colourText.getHexValue32());
            setPadChokeGroup(i, node->getIntAttribute("chokeGroup", 0));
            setPadChokeFade(i, (float)node->getDoubleAttribute("chokeFadeMs", 8.0));
            setPadGateMode(i, node->getIntAttribute("gateMode", 0) != 0);
            setPadEnvelope(i, (float)node->getDoubleAttribute("attackMs", 0.0),
                           (float)node->getDoubleAttribute("decayMs", 0.0),
                           (float)node->getDoubleAttribute("sustain", 1.0),
                           (float)node->getDoubleAttribute("releaseMs", 8.0));
        }
        return true;
    }
    // Export a portable kit: copy referenced samples next to the kit manifest.
    // The original sample files are never modified.
    bool exportPortableKit(const juce::File& manifestFile)
    {
        if (manifestFile.getFileExtension().toLowerCase() != ".xml") return false;
        const auto folder = manifestFile.getParentDirectory();
        if (!folder.createDirectory()) return false;
        const auto samplesFolder = folder.getChildFile(manifestFile.getFileNameWithoutExtension() + "_samples");
        if (!samplesFolder.createDirectory()) return false;
        auto kit = createKitXml();
        std::vector<std::pair<juce::File, juce::File>> staged;
        auto rollback = [&]
        {
            for (const auto& files : staged) files.first.deleteFile();
        };
        for (auto* node : kit->getChildIterator())
        {
            if (!node->hasTagName("Pad")) continue;
            const int index = node->getIntAttribute("index", -1);
            if (index < 0 || index >= padCount) continue;
            const auto path = pads[(size_t)index].sourcePath;
            if (path.isEmpty())
            {
                // Factory kits contain decoded audio in memory, not source files.
                // Materialize that audio as a portable WAV alongside the manifest.
                const auto audio = std::atomic_load(&pads[(size_t)index].audio);
                if (audio == nullptr) continue;
                const auto destination = samplesFolder.getChildFile("pad_"
                    + juce::String(index + 1).paddedLeft('0', 2) + ".wav");
                const auto temp = destination.getSiblingFile(destination.getFileName() + ".tmp");
                if (temp.existsAsFile()) temp.deleteFile();
                auto stream = std::unique_ptr<juce::FileOutputStream>(temp.createOutputStream());
                if (!stream || !stream->openedOk()) { rollback(); return false; }
                juce::WavAudioFormat wav;
                const auto rate = pads[(size_t)index].sourceRate.load();
                auto writer = std::unique_ptr<juce::AudioFormatWriter>(wav.createWriterFor(
                    stream.get(), rate > 0.0 ? rate : 44100.0,
                    (unsigned int)audio->getNumChannels(), 24, {}, 0));
                if (!writer) { stream.reset(); temp.deleteFile(); rollback(); return false; }
                stream.release(); // Writer owns the output stream.
                const bool written = writer->writeFromAudioSampleBuffer(*audio, 0, audio->getNumSamples());
                writer.reset();
                if (!written) { temp.deleteFile(); rollback(); return false; }
                staged.emplace_back(temp, destination);
                node->setAttribute("file", destination.getRelativePathFrom(folder));
                continue;
            }
            const juce::File original(path);
            if (!original.existsAsFile()) { rollback(); return false; }
            const auto destination = samplesFolder.getChildFile("pad_"
                + juce::String(index + 1).paddedLeft('0', 2) + original.getFileExtension());
            if (original != destination)
            {
                const auto temp = destination.getSiblingFile(destination.getFileName() + ".tmp");
                if (temp.existsAsFile()) temp.deleteFile();
                if (!original.copyFileTo(temp)) { rollback(); return false; }
                staged.emplace_back(temp, destination);
            }
            node->setAttribute("file", destination.getRelativePathFrom(folder));
        }
        // Do not publish the manifest until all samples have been copied.
        for (const auto& files : staged)
        {
            if (!files.first.replaceFileIn(files.second))
            {
                rollback();
                return false;
            }
        }
        juce::TemporaryFile temporaryManifest(manifestFile);
        if (!kit->writeTo(temporaryManifest.getFile())) return false;
        return temporaryManifest.overwriteTargetFileWithTemporary();
    }
    bool importPortableKit(const juce::File& manifestFile)
    {
        const auto xml = juce::XmlDocument::parse(manifestFile);
        if (xml == nullptr || !xml->hasTagName("LibertyDrumKit")) return false;
        // Resolve relative paths against the kit manifest, not the current working directory.
        for (auto* node : xml->getChildIterator())
        {
            if (!node->hasTagName("Pad")) continue;
            const auto path = node->getStringAttribute("file");
            if (path.isNotEmpty() && !juce::File::isAbsolutePath(path))
                node->setAttribute("file", manifestFile.getParentDirectory().getChildFile(path).getFullPathName());
        }
        // Validate and decode the entire kit before changing any live pad.
        LibertyDrumSampler candidate;
        candidate.prepare(outputRate);
        std::array<bool, padCount> seenPads {};
        for (auto* node : xml->getChildIterator())
        {
            if (!node->hasTagName("Pad")) continue;
            // A missing or non-numeric index must never default to pad zero.
            const auto indexText = node->getStringAttribute("index");
            if (indexText.isEmpty() || indexText.length() > 2
                || !indexText.containsOnly("0123456789"))
                return false;
            const int index = node->getIntAttribute("index", -1);
            // Duplicate entries are ambiguous and must not silently overwrite
            // previously validated samples or pad parameters.
            if (index < 0 || index >= padCount || seenPads[(size_t)index])
                return false;
            seenPads[(size_t)index] = true;
            // Reject non-finite numeric settings before publishing any pad.
            // NaN/Infinity can otherwise propagate into pitch, envelope or output gain.
            for (const auto* attribute : { "gain", "pan", "pitch", "start", "end",
                                           "chokeFadeMs", "attackMs", "decayMs",
                                           "sustain", "releaseMs" })
            {
                if (!node->hasAttribute(attribute)) continue;
                const auto value = node->getStringAttribute(attribute).trim();
                if (value.isEmpty()) return false;
                const auto utf8 = value.toRawUTF8();
                char* end = nullptr;
                const double parsed = std::strtod(utf8, &end);
                // Reject partially parsed numbers (e.g. "12abc"), not just NaN.
                if (end == utf8 || *end != '\0' || !std::isfinite(parsed)
                    || parsed > std::numeric_limits<float>::max()
                    || parsed < -std::numeric_limits<float>::max())
                    return false;
                const juce::String key(attribute);
                // Reject out-of-range settings rather than silently clamping a
                // damaged manifest to a different, unintended sound.
                if ((key == "gain" && (parsed < 0.0 || parsed > 2.0))
                    || (key == "pan" && (parsed < -1.0 || parsed > 1.0))
                    || (key == "pitch" && (parsed < -24.0 || parsed > 24.0))
                    || (key == "start" && (parsed < 0.0 || parsed > 0.99))
                    || (key == "end" && (parsed < 0.01 || parsed > 1.0))
                    || (key == "sustain" && (parsed < 0.0 || parsed > 1.0))
                    || ((key == "attackMs" || key == "decayMs")
                        && (parsed < 0.0 || parsed > 2000.0))
                    || (key == "releaseMs" && (parsed < 1.0 || parsed > 5000.0))
                    || (key == "chokeFadeMs" && (parsed < 1.0 || parsed > 100.0)))
                    return false;
            }
            // Validate trim endpoints together: each is individually in range,
            // but the playback window must still span at least one percent.
            const double trimStart = node->getDoubleAttribute("start", 0.0);
            const double trimEnd = node->getDoubleAttribute("end", 1.0);
            if (trimEnd - trimStart < 0.01 - 1.0e-7)
                return false;
            for (const auto* attribute : { "chokeGroup", "gateMode" })
            {
                if (!node->hasAttribute(attribute)) continue;
                const auto value = node->getStringAttribute(attribute).trim();
                // Both settings are single-digit enumerations: avoid overflow
                // and accidental acceptance of huge numeric strings.
                if (value.length() != 1 || !value.containsOnly("0123456789"))
                    return false;
                const int parsed = value.getIntValue();
                if ((juce::String(attribute) == "chokeGroup" && parsed > 8)
                    || (juce::String(attribute) == "gateMode" && parsed > 1))
                    return false;
            }
            const auto path = node->getStringAttribute("file");
            if (path.isNotEmpty() && !candidate.loadPad(index, juce::File(path)))
                return false;
        }
        if (!candidate.restoreKitXml(*xml)) return false;
        for (int i = 0; i < padCount; ++i)
        {
            auto& dst = pads[(size_t)i];
            const auto& src = candidate.pads[(size_t)i];
            std::atomic_store(&dst.audio, std::atomic_load(&src.audio));
            dst.name = src.name;
            dst.sourcePath = src.sourcePath;
            dst.sourceRate.store(src.sourceRate.load());
            dst.gain.store(src.gain.load());
            dst.pan.store(src.pan.load());
            dst.pitchSemitones.store(src.pitchSemitones.load());
            dst.startFraction.store(src.startFraction.load());
            dst.endFraction.store(src.endFraction.load());
            dst.loopEnabled.store(src.loopEnabled.load());
            dst.colourArgb.store(src.colourArgb.load());
            dst.factoryTone.store(src.factoryTone.load());
            dst.chokeGroup.store(src.chokeGroup.load());
            dst.chokeFadeMs.store(src.chokeFadeMs.load());
            dst.gateMode.store(src.gateMode.load());
            dst.attackMs.store(src.attackMs.load());
            dst.decayMs.store(src.decayMs.load());
            dst.sustain.store(src.sustain.load());
            dst.releaseMs.store(src.releaseMs.load());
        }
        // Existing voices retain their decoded audio and their original envelope.
        // Mark notes as released before switching kits so a later Note Off
        // cannot inadvertently release a newly-triggered voice.
        for (auto& voice : voices)
            if (voice.active)
                voice.noteReleased = true;
        return true;
    }
    bool hasSample(int index) const noexcept
    {
        return index >= 0 && index < padCount && std::atomic_load(&pads[(size_t)index].audio) != nullptr;
    }
    void noteOff(int note) noexcept
    {
        // Pair overlapping note-ons with note-offs in trigger order.
        // Voices which have finished or have been stolen cannot consume a note-off.
        Voice* oldestHeld = nullptr;
        for (auto& voice : voices)
            if (voice.active && voice.midiNote == note && voice.gateMode
                && !voice.noteReleased
                && (oldestHeld == nullptr || voice.sequence < oldestHeld->sequence))
                oldestHeld = &voice;

        if (oldestHeld == nullptr) return;
        oldestHeld->noteReleased = true;
        const int release = juce::jmax(1, (int)(outputRate * oldestHeld->releaseMs / 1000.0));
        if (oldestHeld->releaseSamplesRemaining == 0
            || release < oldestHeld->releaseSamplesRemaining)
        {
            const float currentLevel = oldestHeld->releaseSamplesRemaining == 0
                ? envelopeAtAge(*oldestHeld, outputRate)
                : oldestHeld->releaseStartLevel
                    * (float)oldestHeld->releaseSamplesRemaining
                    / juce::jmax(1, oldestHeld->releaseSamplesTotal);
            oldestHeld->releaseStartLevel = currentLevel;
            oldestHeld->releaseSamplesRemaining = release;
            oldestHeld->releaseSamplesTotal = release;
        }
    }
    void noteOn(int note, float velocity, bool restartPad = false) noexcept
    {
        if (velocity <= 0.0f) return;
        for (int i = 0; i < padCount; ++i)
            if (pads[(size_t)i].midiNote == note
                || (i == 4 && note == 52 && pads[(size_t)i].factoryTone.load()))
            {
                // An empty pad must not silence other pads in its choke group.
                auto audio = std::atomic_load(&pads[(size_t)i].audio);
                if (audio == nullptr || audio->getNumSamples() == 0) return;
                const int group = pads[(size_t)i].chokeGroup.load();
                if (group > 0)
                    for (auto& activeVoice : voices)
                        if (activeVoice.active && activeVoice.chokeGroup == group)
                        {
                            // Factory hats behave like a pedal: a closed hat
                            // chokes an open hat, but an open hat must not
                            // prematurely silence a preceding closed hit.
                            const bool factoryHatTrigger = pads[(size_t)i].factoryTone.load()
                                && (i == 3 || i == 4);
                            if (factoryHatTrigger && i == 4 && activeVoice.pad == 3)
                                continue;
                            // Closing a semi-open hat uses a slightly longer
                            // pedal transition than closing a fully open hat.
                            const float fadeMs = factoryHatTrigger && i == 3
                                && activeVoice.semiOpenHat ? 7.0f
                                : pads[(size_t)i].chokeFadeMs.load();
                            const int requested = juce::jmax(1,
                                (int)(outputRate * fadeMs / 1000.0));
                            if (activeVoice.releaseSamplesRemaining == 0
                                || requested < activeVoice.releaseSamplesRemaining)
                            {
                                // Preserve the instantaneous gain when shortening a release.
                                const float currentLevel = activeVoice.releaseSamplesRemaining == 0
                                    ? envelopeAtAge(activeVoice, outputRate)
                                    : activeVoice.releaseStartLevel
                                        * (float)activeVoice.releaseSamplesRemaining
                                        / juce::jmax(1, activeVoice.releaseSamplesTotal);
                                activeVoice.releaseStartLevel = currentLevel;
                                activeVoice.releaseSamplesRemaining = requested;
                                activeVoice.releaseSamplesTotal = requested;
                            }
                        }
                // Retriggering a pad restarts its sample instead of stacking
                // another long loop over the previous playback.
                for (auto& previous : voices)
                    if (restartPad && previous.active && previous.pad == i)
                    {
                        previous.active = false;
                        previous.audio.reset();
                    }
                // Prefer an idle voice; when polyphony is exhausted, steal the
                // oldest active voice instead of repeatedly cutting voice zero.
                auto* voice = &voices[0];
                bool foundIdle = false;
                for (auto& candidate : voices)
                    if (!candidate.active)
                    {
                        voice = &candidate;
                        foundIdle = true;
                        break;
                    }
                if (!foundIdle)
                    for (auto& candidate : voices)
                        if (candidate.sequence < voice->sequence)
                            voice = &candidate;
                const int frames = audio->getNumSamples();
                const float start = pads[(size_t)i].startFraction.load();
                *voice = {true, i, (double)juce::jlimit(0, frames - 1, (int)(start * (frames - 1))),
                          juce::jlimit(0.0f, 1.0f, velocity), std::move(audio), pads[(size_t)i].sourceRate.load(),
                          pads[(size_t)i].endFraction.load(), pads[(size_t)i].gain.load(),
                          pads[(size_t)i].pan.load(), pads[(size_t)i].pitchSemitones.load(), ++voiceSequence,
                          pads[(size_t)i].chokeGroup.load(), 0, 0, 1.0f, note, pads[(size_t)i].gateMode.load(),
                          false, 0, pads[(size_t)i].attackMs.load(), pads[(size_t)i].decayMs.load(),
                          pads[(size_t)i].sustain.load(), pads[(size_t)i].releaseMs.load()};
                voice->factoryTone = pads[(size_t)i].factoryTone.load();
                // MIDI 52 adds a semi-open factory hi-hat articulation
                // without consuming or remapping any of the 16 pads.
                voice->semiOpenHat = (i == 4 && note == 52 && voice->factoryTone);
                // Deterministic four-step timbre cycle, independent for each pad.
                // Keep imported samples bit-identical to the original playback path.
                if (voice->factoryTone)
                {
                    const unsigned int step = factoryHitCounters[(size_t)i].fetch_add(1u, std::memory_order_relaxed) % 4u;
                    static constexpr float variations[4] = { 0.0f, -0.07f, 0.045f, -0.025f };
                    voice->toneVariation = variations[step];
                    // Subtle pitch offsets (cents), captured per hit so a
                    // sounding voice never changes pitch mid-note.
                    static constexpr float pitchCents[4] = { 0.0f, -3.0f, 2.0f, -1.5f };
                    voice->pitchVariationSemitones = pitchCents[step] / 100.0f;
                }
                return;
            }
    }
    // Render sample-accurate MIDI events within the host's audio block.
    // Call this from the audio thread; pad loading must be stopped while rendering.
    void renderMidi(juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi) noexcept
    {
        processAuditions();
        int cursor = 0;
        for (const auto metadata : midi)
        {
            const int offset = juce::jlimit(0, output.getNumSamples(), metadata.samplePosition);
            if (offset > cursor)
                render(output, cursor, offset - cursor);
            const auto message = metadata.getMessage();
            if (message.isNoteOn())
                noteOn(message.getNoteNumber(), message.getFloatVelocity());
            else if (message.isNoteOff())
                noteOff(message.getNoteNumber());
            else if (message.isAllSoundOff() || message.isAllNotesOff())
                reset();
            cursor = juce::jmax(cursor, offset);
        }
        if (cursor < output.getNumSamples())
            render(output, cursor, output.getNumSamples() - cursor);
    }
    void setPadEnvelope(int index, float attack, float decay, float sustainLevel, float release) noexcept
    {
        if (index < 0 || index >= padCount) return;
        auto& pad = pads[(size_t)index];
        pad.attackMs.store(juce::jlimit(0.0f, 2000.0f, attack));
        pad.decayMs.store(juce::jlimit(0.0f, 2000.0f, decay));
        pad.sustain.store(juce::jlimit(0.0f, 1.0f, sustainLevel));
        pad.releaseMs.store(juce::jlimit(1.0f, 5000.0f, release));
    }
    void setPadGateMode(int index, bool enabled) noexcept
    {
        if (index >= 0 && index < padCount)
            pads[(size_t)index].gateMode.store(enabled);
    }
    void setPadChokeFade(int index, float milliseconds) noexcept
    {
        if (index >= 0 && index < padCount)
            pads[(size_t)index].chokeFadeMs.store(juce::jlimit(1.0f, 100.0f, milliseconds));
    }
    void setPadChokeGroup(int index, int group) noexcept
    {
        if (index >= 0 && index < padCount)
            pads[(size_t)index].chokeGroup.store(juce::jlimit(0, 8, group));
    }
    void setPadGain(int index, float gain) noexcept
    {
        if (index >= 0 && index < padCount)
            pads[(size_t)index].gain.store(juce::jlimit(0.0f, 2.0f, gain));
    }
    void setPadPan(int index, float pan) noexcept
    {
        if (index >= 0 && index < padCount)
            pads[(size_t)index].pan.store(juce::jlimit(-1.0f, 1.0f, pan));
    }
    void setPadPitch(int index, float semitones) noexcept
    {
        if (index >= 0 && index < padCount)
            pads[(size_t)index].pitchSemitones.store(juce::jlimit(-24.0f, 24.0f, semitones));
    }
    void setPadColour(int index, int argb) noexcept
    {
        if (index >= 0 && index < padCount)
            pads[(size_t)index].colourArgb.store((int)(0xff000000u | ((unsigned int)argb & 0x00ffffffu)));
    }
    void setPadLoop(int index, bool enabled) noexcept
    {
        if (index >= 0 && index < padCount) pads[(size_t)index].loopEnabled.store(enabled);
    }
    void setPadTrim(int index, float start, float end) noexcept
    {
        if (index < 0 || index >= padCount) return;
        const float safeStart = juce::jlimit(0.0f, 0.99f, start);
        const float safeEnd = juce::jlimit(safeStart + 0.01f, 1.0f, end);
        pads[(size_t)index].startFraction.store(safeStart);
        pads[(size_t)index].endFraction.store(safeEnd);
    }
    void render(juce::AudioBuffer<float>& output, int start, int count) noexcept
    {
        if (output.getNumChannels() < 1 || count <= 0) return;
        const int end = juce::jmin(output.getNumSamples(), start + count);
        for (auto& position : playbackPositions) position.store(-1.0f, std::memory_order_relaxed);
        for (int n = juce::jmax(0, start); n < end; ++n)
            for (auto& voice : voices)
            {
                if (!voice.active) continue;
                // Read current pad controls while the voice is playing: moving a
                // slider must not require retriggering a long sample or loop.
                const auto& livePad = pads[(size_t)voice.pad];
                voice.gain = livePad.gain.load(std::memory_order_relaxed);
                voice.pan = livePad.pan.load(std::memory_order_relaxed);
                voice.pitchSemitones = livePad.pitchSemitones.load(std::memory_order_relaxed);
                voice.endFraction = livePad.endFraction.load(std::memory_order_relaxed);
                voice.attackMs = livePad.attackMs.load(std::memory_order_relaxed);
                voice.decayMs = livePad.decayMs.load(std::memory_order_relaxed);
                voice.sustain = livePad.sustain.load(std::memory_order_relaxed);
                voice.releaseMs = livePad.releaseMs.load(std::memory_order_relaxed);
                // Wrap the current voice inside the live trim markers, without retriggering.
                if (voice.audio != nullptr && livePad.loopEnabled.load(std::memory_order_relaxed))
                {
                    const int frames = voice.audio->getNumSamples();
                    const double loopStart = juce::jlimit(0.0, (double)frames - 1.0,
                        (double)livePad.startFraction.load(std::memory_order_relaxed) * frames);
                    const double loopEnd = juce::jlimit(loopStart + 1.0, (double)frames,
                        (double)voice.endFraction * frames);
                    if (voice.position >= loopEnd)
                        voice.position = loopStart + std::fmod(voice.position - loopStart, loopEnd - loopStart);
                }
                const int frame = (int)voice.position;
                if (voice.audio != nullptr && voice.audio->getNumSamples() > 0)
                    playbackPositions[(size_t)voice.pad].store(
                        (float)(voice.position / voice.audio->getNumSamples()), std::memory_order_relaxed);
                if (voice.audio == nullptr || frame >= (int)(voice.audio->getNumSamples() * voice.endFraction)) { voice.active = false; voice.audio.reset(); continue; }
                const float fraction = (float)(voice.position - frame);
                const int next = juce::jmin(frame + 1, voice.audio->getNumSamples() - 1);
                const float envelope = voice.releaseSamplesRemaining > 0
                    ? voice.releaseStartLevel
                    : envelopeAtAge(voice, outputRate);
                for (int channel = 0; channel < output.getNumChannels(); ++channel)
                {
                    const int source = juce::jmin(channel, voice.audio->getNumChannels() - 1);
                    const float a = voice.audio->getSample(source, frame);
                    const float b = voice.audio->getSample(source, next);
                    const float panGain = channel == 0 ? juce::jmin(1.0f, 1.0f - voice.pan)
                                                       : juce::jmin(1.0f, 1.0f + voice.pan);
                    const float releaseGain = voice.releaseSamplesRemaining > 0
                        ? juce::jlimit(0.0f, 1.0f, (float)voice.releaseSamplesRemaining / juce::jmax(1.0f, (float)voice.releaseSamplesTotal))
                        : 1.0f;
                    const float rawSample = a + (b - a) * fraction;
                    // Velocity-sensitive one-pole tone shaping: gentle hits
                    // have fewer highs, strong hits retain their transients.
                    // Filter state belongs to the voice (no cross-pad bleed).
                    const int filterChannel = juce::jmin(channel, 1);
                    // Low drums retain their fundamental on soft hits; hats and
                    // cymbals respond more strongly in the high frequencies.
                    const int instrument = voice.pad;
                    const float minimumBrightness = (instrument == 0 || instrument == 15) ? 0.40f
                        : (instrument >= 5 && instrument <= 7) ? 0.28f
                        : (instrument == 3 || instrument == 4 || instrument == 12 || instrument == 13) ? 0.045f
                        : 0.12f;
                    const float velocityCurve = (instrument == 3 || instrument == 4
                        || instrument == 12 || instrument == 13)
                        ? voice.velocity * voice.velocity * voice.velocity
                        : voice.velocity * voice.velocity;
                    const float brightness = juce::jlimit(0.015f, 1.0f,
                        minimumBrightness + (1.0f - minimumBrightness) * velocityCurve
                        + (voice.factoryTone ? voice.toneVariation : 0.0f));
                    auto& filtered = voice.toneState[(size_t)filterChannel];
                    filtered += brightness * (rawSample - filtered);
                    // Three synthetic velocity layers: soft favors the filtered
                    // body, medium blends body/attack, hard retains more attack.
                    // Interpolation avoids abrupt timbral jumps at boundaries.
                    const float layerBlend = juce::jlimit(0.0f, 1.0f,
                        (voice.velocity - 0.30f) / 0.60f);
                    const float attackAmount = 0.10f + 0.40f * layerBlend;
                    const float layered = filtered * (1.0f - attackAmount)
                        + rawSample * attackAmount;
                    // Brief attack emphasis on stronger synthesized hits.
                    // Exponential decay prevents the extra transient from
                    // affecting the body and tail of the drum.
                    const float transientWeight = voice.factoryTone
                        ? juce::jmax(0.0f, (voice.velocity - 0.45f) / 0.55f)
                            * (float)std::exp(-(double)voice.ageSamples
                                / juce::jmax(1.0, outputRate * 0.009))
                        : 0.0f;
                    const float shapedSample = voice.factoryTone
                        ? layered + 0.16f * transientWeight * (rawSample - filtered)
                        : rawSample;
                    // Softer factory cymbal and hat hits decay sooner. The
                    // envelope is per voice, preserving independent overlaps.
                    const bool factoryMetal = voice.factoryTone
                        && (instrument == 3 || instrument == 4
                            || instrument == 12 || instrument == 13);
                    const double decaySeconds = instrument == 3 ? 0.055
                        : instrument == 4 ? 0.28 : instrument == 12 ? 0.65 : 0.85;
                    const float softDamping = factoryMetal
                        ? (float)std::exp(-(double)voice.ageSamples
                            * (1.0 - (double)voice.velocity)
                            / juce::jmax(1.0, outputRate * decaySeconds))
                        : 1.0f;
                    const float articulationDamping = voice.semiOpenHat
                        ? (float)std::exp(-(double)voice.ageSamples
                            / juce::jmax(1.0, outputRate * 0.115))
                        : 1.0f;
                    output.addSample(channel, n, shapedSample * softDamping * articulationDamping * voice.velocity * voice.gain * panGain * releaseGain * envelope);
                }
                if (voice.releaseSamplesRemaining > 0 && --voice.releaseSamplesRemaining == 0)
                {
                    voice.active = false;
                    voice.audio.reset();
                    continue;
                }
                ++voice.ageSamples;
                voice.position += (voice.sourceRate / outputRate)
                    * std::pow(2.0, ((double)voice.pitchSemitones
                        + (double)voice.pitchVariationSemitones) / 12.0);
            }
    }
    const Pad& getPad(int index) const noexcept { return pads[(size_t)juce::jlimit(0, padCount-1, index)]; }
private:
    struct Voice
    {
        bool active = false;
        int pad = 0;
        double position = 0;
        float velocity = 1;
        std::shared_ptr<const juce::AudioBuffer<float>> audio;
        double sourceRate = 44100.0;
        float endFraction = 1.0f;
        float gain = 1.0f;
        float pan = 0.0f;
        float pitchSemitones = 0.0f;
        uint64_t sequence = 0;
        int chokeGroup = 0;
        int releaseSamplesRemaining = 0;
        int releaseSamplesTotal = 0;
        float releaseStartLevel = 1.0f;
        int midiNote = -1;
        bool gateMode = false;
        bool noteReleased = false;
        int64_t ageSamples = 0;
        float attackMs = 0.0f, decayMs = 0.0f, sustain = 1.0f, releaseMs = 8.0f;
        std::array<float, 2> toneState {};
        bool factoryTone = false;
        bool semiOpenHat = false;
        float toneVariation = 0.0f;
        float pitchVariationSemitones = 0.0f;
    };
    static float envelopeAtAge(const Voice& voice, double rate) noexcept
    {
        const double attack = rate * voice.attackMs / 1000.0;
        const double decay = rate * voice.decayMs / 1000.0;
        if (attack > 0.0 && voice.ageSamples < attack)
            return (float)(voice.ageSamples / attack);
        if (decay > 0.0 && voice.ageSamples < attack + decay)
            return 1.0f - (1.0f - voice.sustain)
                * (float)((voice.ageSamples - attack) / decay);
        return voice.sustain;
    }
    uint64_t voiceSequence = 0;
    juce::AudioFormatManager formats;
    std::array<Pad, padCount> pads;
    std::array<Voice, 32> voices{};
    std::array<std::atomic<unsigned int>, padCount> factoryHitCounters {};
    std::atomic<unsigned int> pendingTriggers { 0 };
    std::atomic<unsigned int> pendingStops { 0 };
    std::atomic<bool> pendingStopAll { false };
    std::array<std::atomic<float>, padCount> pendingVelocity {};
    std::array<std::atomic<float>, padCount> playbackPositions {};
    double outputRate = 44100.0;
};


// First visual prototype. Owned by the message thread; audio integration is next.
// Read-only waveform overview for the selected drum pad.
class LibertyDrumWaveform final : public juce::Component, private juce::Timer
{
public:
    explicit LibertyDrumWaveform(LibertyDrumSampler& engine) : sampler(engine) { startTimerHz(30); }
    void selectPad(int index) { selectedPad = index; viewStart = 0.0f; viewSpan = 1.0f; repaint(); }
    std::function<void()> onTrimChanged;
    void timerCallback() override { repaint(); }
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        if (!sampler.hasSample(selectedPad) || getWidth() <= 0 || wheel.deltaY == 0.0f) return;
        const float cursor = juce::jlimit(0.0f, 1.0f, (float)event.x / (float)getWidth());
        const float anchor = viewStart + cursor * viewSpan;
        const float factor = std::pow(1.2f, -wheel.deltaY * 4.0f);
        viewSpan = juce::jlimit(0.005f, 1.0f, viewSpan * factor);
        viewStart = juce::jlimit(0.0f, 1.0f - viewSpan, anchor - cursor * viewSpan);
        repaint();
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (!sampler.hasSample(selectedPad) || getWidth() <= 0) return;
        const auto& pad = sampler.getPad(selectedPad);
        const float x = juce::jlimit(0.0f, 1.0f, viewStart + (float)event.x / (float)getWidth() * viewSpan);
        const float start = pad.startFraction.load();
        const float end = pad.endFraction.load();
        draggingStart = std::abs(x - start) <= std::abs(x - end);
        mouseDrag(event);
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!sampler.hasSample(selectedPad) || getWidth() <= 0) return;
        const auto& pad = sampler.getPad(selectedPad);
        const float x = juce::jlimit(0.0f, 1.0f, viewStart + (float)event.x / (float)getWidth() * viewSpan);
        if (draggingStart)
            sampler.setPadTrim(selectedPad, juce::jmin(x, pad.endFraction.load() - 0.01f),
                               pad.endFraction.load());
        else
            sampler.setPadTrim(selectedPad, pad.startFraction.load(),
                               juce::jmax(x, pad.startFraction.load() + 0.01f));
        if (onTrimChanged) onTrimChanged();
        repaint();
    }
    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        juce::ColourGradient surface(juce::Colour(0xff18212b), 0.0f, 0.0f,
                                      juce::Colour(0xff0b0f14), 0.0f, bounds.getBottom(), false);
        g.setGradientFill(surface);
        g.fillRoundedRectangle(bounds, 5.0f);
        g.setColour(juce::Colour(0xff344452));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 5.0f, 1.0f);
        // Subdued centre reference stays visible without competing with the waveform.
        g.setColour(juce::Colour(0xff728394).withAlpha(0.28f));
        g.drawHorizontalLine(getHeight() / 2, 4.0f, (float)juce::jmax(4, getWidth() - 4));
        const auto& pad = sampler.getPad(selectedPad);
        const auto audio = std::atomic_load(&pad.audio);
        if (audio == nullptr || audio->getNumSamples() == 0)
        {
            g.setColour(juce::Colour(0xffb5c3d1));
            g.drawFittedText("CHARGER UN SAMPLE POUR VOIR SA FORME D'ONDE",
                             getLocalBounds(), juce::Justification::centred, 1);
            return;
        }
        const int width = getWidth();
        const float mid = getHeight() * 0.5f;
        const float amplitude = juce::jmax(1.0f, mid - 5.0f);
        const int frames = audio->getNumSamples();
        g.setColour(juce::Colour(0xff54d6dc));
        for (int x = 0; x < width; ++x)
        {
            const int begin = juce::jlimit(0, frames, (int)((viewStart + (float)x / juce::jmax(1, width) * viewSpan) * frames));
            const int finish = juce::jlimit(begin, frames, (int)std::ceil((viewStart + (float)(x + 1) / juce::jmax(1, width) * viewSpan) * frames));
            float peak = 0.0f;
            for (int n = begin; n < finish; ++n)
                for (int ch = 0; ch < audio->getNumChannels(); ++ch)
                    peak = juce::jmax(peak, std::abs(audio->getSample(ch, n)));
            const float height = juce::jmin(1.0f, peak) * amplitude;
            g.drawVerticalLine(x, mid - height, mid + height);
        }
        const float start = pad.startFraction.load();
        const float end = pad.endFraction.load();
        g.setColour(juce::Colour(0x990b0f14));
        const float startX = (start - viewStart) / viewSpan * bounds.getWidth();
        const float endX = (end - viewStart) / viewSpan * bounds.getWidth();
        g.fillRect(0.0f, 0.0f, juce::jlimit(0.0f, bounds.getWidth(), startX), bounds.getHeight());
        const float endShade = juce::jlimit(0.0f, bounds.getWidth(), endX);
        g.fillRect(endShade, 0.0f, bounds.getWidth() - endShade, bounds.getHeight());
        g.setColour(juce::Colour(0xffe4a85e));
        if (startX >= 0.0f && startX < bounds.getWidth())
            g.drawVerticalLine((int)startX, 0.0f, bounds.getHeight());
        if (endX >= 0.0f && endX < bounds.getWidth())
            g.drawVerticalLine((int)endX, 0.0f, bounds.getHeight());
        // The playhead uses the same zoom/scroll coordinates as the waveform.
        const float playback = sampler.getPlaybackPosition(selectedPad);
        const float playX = (playback - viewStart) / viewSpan * bounds.getWidth();
        if (playback >= 0.0f && playX >= 0.0f && playX < bounds.getWidth())
        {
            g.setColour(juce::Colour(0xffe8eff7));
            g.drawVerticalLine((int)playX, 0.0f, bounds.getHeight());
        }
    }
private:
    LibertyDrumSampler& sampler;
    int selectedPad = 0;
    bool draggingStart = true;
    float viewStart = 0.0f;
    float viewSpan = 1.0f;
};

class LibertyDrumSamplerPanel final : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit LibertyDrumSamplerPanel(LibertyDrumSampler& engine) : sampler(engine), waveform(engine)
    {
        for (int i = 0; i < LibertyDrumSampler::padCount; ++i)
        {
            auto& button = pads[(size_t)i];
            button.setButtonText("PAD " + juce::String(i + 1));
            button.setColour(juce::TextButton::buttonColourId, juce::Colour((juce::uint32)sampler.getPad(i).colourArgb.load()));
            button.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe8eff7));
            button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff23535d));
            button.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
            button.onClick = [this, i] { selectedPad = i; updateControls(); waveform.selectPad(i); sampler.auditionPad(i, 1.0f); repaint(); };
            addAndMakeVisible(button);
        }
        loadButton.setButtonText("CHARGER SAMPLE");
        loadButton.onClick = [this]
        {
            const int padIndex = selectedPad;
            chooser = std::make_unique<juce::FileChooser>("Charger un sample de batterie", juce::File{}, "*.wav;*.aif;*.aiff");
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [safe = juce::Component::SafePointer<LibertyDrumSamplerPanel>(this), padIndex](const juce::FileChooser& dialog)
                {
                    if (safe == nullptr) return;
                    if (safe->sampler.loadPad(padIndex, dialog.getResult()))
                        safe->refreshLabels();
                });
        };
        loadButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff25323e));
        loadButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff344452));
        loadButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd1dce7));
        loadButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible(loadButton);
        colourButton.setButtonText("COULEUR PAD");
        colourButton.onClick = [this]
        {
            const int padIndex = selectedPad;
            const std::array<juce::uint32, 16> palette {{
                0xff253c4a, 0xffe74c3c, 0xffe67e22, 0xfff1c40f,
                0xff2ecc71, 0xff16a085, 0xff3498db, 0xff2980b9,
                0xff9b59b6, 0xffe84393, 0xff95a5a6, 0xffecf0f1,
                0xff7f8c8d, 0xff1abc9c, 0xffd35400, 0xffc0392b
            }};
            juce::PopupMenu menu;
            const char* names[] = { "BLEU ARDOISE", "ROUGE", "ORANGE", "JAUNE",
                "VERT", "TURQUOISE", "BLEU", "BLEU PROFOND", "VIOLET", "ROSE",
                "GRIS CLAIR", "BLANC", "GRIS", "MENTHE", "CUIVRE", "BORDEAUX" };
            for (int i = 0; i < (int)palette.size(); ++i)
                menu.addItem(i + 1, names[i], true,
                    (juce::uint32)sampler.getPad(padIndex).colourArgb.load() == palette[(size_t)i]);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&colourButton),
                [safe = juce::Component::SafePointer<LibertyDrumSamplerPanel>(this), padIndex, palette](int choice)
                {
                    if (safe == nullptr || choice < 1 || choice > (int)palette.size()) return;
                    safe->sampler.setPadColour(padIndex, (int)palette[(size_t)(choice - 1)]);
                    safe->refreshPadColours();
                });
        };
        colourButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff25323e));
        colourButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff344452));
        colourButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd1dce7));
        colourButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible(colourButton);
        stopButton.setButtonText("STOP PAD");
        stopButton.onClick = [this] { sampler.stopPad(selectedPad); };
        stopButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3b3038));
        stopButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff59404b));
        stopButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xfff0dce3));
        addAndMakeVisible(stopButton);
        loopButton.setButtonText("LOOP START / END");
        loopButton.setClickingTogglesState(true);
        loopButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff25323e));
        loopButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff23535d));
        loopButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd1dce7));
        loopButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        loopButton.onClick = [this] { sampler.setPadLoop(selectedPad, loopButton.getToggleState()); };
        addAndMakeVisible(loopButton);
        exportButton.setButtonText("EXPORTER KIT");
        exportButton.onClick = [this]
        {
            kitChooser = std::make_unique<juce::FileChooser>("Exporter le kit Liberty",
                juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("LibertyDrumKit.xml"),
                "*.xml");
            kitChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                       | juce::FileBrowserComponent::canSelectFiles
                                       | juce::FileBrowserComponent::warnAboutOverwriting,
                [safe = juce::Component::SafePointer<LibertyDrumSamplerPanel>(this)](const juce::FileChooser& chooser)
                {
                    if (safe == nullptr) return;
                    const auto target = chooser.getResult();
                    if (target != juce::File{} && !safe->sampler.exportPortableKit(target))
                        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                            "Export du kit", "Impossible de copier les samples ou de sauvegarder le kit.");
                });
        };
        exportButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff25323e));
        exportButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff344452));
        exportButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd1dce7));
        exportButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible(exportButton);
        importButton.setButtonText("IMPORTER KIT");
        importButton.onClick = [this]
        {
            kitChooser = std::make_unique<juce::FileChooser>("Importer un kit Liberty", juce::File{}, "*.xml");
            kitChooser->launchAsync(juce::FileBrowserComponent::openMode
                                       | juce::FileBrowserComponent::canSelectFiles,
                [safe = juce::Component::SafePointer<LibertyDrumSamplerPanel>(this)](const juce::FileChooser& chooser)
                {
                    if (safe == nullptr) return;
                    const auto target = chooser.getResult();
                    if (target != juce::File{} && safe->sampler.importPortableKit(target))
                    {
                        safe->refreshLabels();
                        safe->updateControls();
                        safe->waveform.repaint();
                    }
                    else if (target != juce::File{})
                        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                            "Import du kit", "Fichier de kit invalide.");
                });
        };
        importButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff25323e));
        importButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff344452));
        importButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd1dce7));
        importButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible(importButton);
        factorySelector.addItem("FACTORY KITS", 1);
        const char* factoryNames[] = { "HIP-HOP", "ACOUSTIC", "ROCK", "METAL", "ELECTRO",
            "REGGAE", "POP", "ORIENTAL", "FUNK", "TRAP" };
        for (int i = 0; i < 10; ++i) factorySelector.addItem(factoryNames[i], i + 2);
        factorySelector.setSelectedId(1, juce::dontSendNotification);
        factorySelector.onChange = [this]
        {
            const int kitIndex = factorySelector.getSelectedId() - 2;
            if (kitIndex >= 0 && sampler.loadFactoryKit(kitIndex))
            {
                selectedPad = 0;
                refreshLabels();
                updateControls();
                waveform.selectPad(selectedPad);
                waveform.repaint();
            }
        };
        addAndMakeVisible(factorySelector);
        addAndMakeVisible(waveform);
        waveform.onTrimChanged = [this] { updateControls(); };
        gainSlider.setRange(0.0, 2.0, 0.01);
        gainSlider.setValue(1.0, juce::dontSendNotification);
        gainSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 22);
        gainSlider.onValueChange = [this] { sampler.setPadGain(selectedPad, (float)gainSlider.getValue()); };
        addAndMakeVisible(gainSlider);
        panSlider.setRange(-1.0, 1.0, 0.01);
        panSlider.setValue(0.0, juce::dontSendNotification);
        panSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 22);
        panSlider.onValueChange = [this] { sampler.setPadPan(selectedPad, (float)panSlider.getValue()); };
        addAndMakeVisible(panSlider);
        pitchSlider.setRange(-24.0, 24.0, 1.0);
        pitchSlider.setValue(0.0, juce::dontSendNotification);
        pitchSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 22);
        pitchSlider.onValueChange = [this] { sampler.setPadPitch(selectedPad, (float)pitchSlider.getValue()); };
        addAndMakeVisible(pitchSlider);
        pitchLabel.setText("PITCH", juce::dontSendNotification);
        pitchLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(pitchLabel);
        startSlider.setRange(0.0, 99.0, 1.0);
        endSlider.setRange(1.0, 100.0, 1.0);
        startSlider.setValue(0.0, juce::dontSendNotification);
        endSlider.setValue(100.0, juce::dontSendNotification);
        for (auto* slider : { &startSlider, &endSlider })
        {
            slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 22);
            addAndMakeVisible(*slider);
        }
        startSlider.onValueChange = [this] { applyTrim(); };
        endSlider.onValueChange = [this] { applyTrim(); };
        startLabel.setText("START", juce::dontSendNotification);
        endLabel.setText("END", juce::dontSendNotification);
        for (auto* label : { &startLabel, &endLabel })
        {
            label->setColour(juce::Label::textColourId, juce::Colours::white);
            addAndMakeVisible(*label);
        }
        chokeLabel.setText("CHOKE", juce::dontSendNotification);
        chokeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(chokeLabel);
        chokeSelector.addItem("OFF", 1);
        for (int group = 1; group <= 8; ++group)
            chokeSelector.addItem("GROUPE " + juce::String(group), group + 1);
        chokeSelector.onChange = [this]
        {
            sampler.setPadChokeGroup(selectedPad, chokeSelector.getSelectedId() - 1);
        };
        addAndMakeVisible(chokeSelector);
        chokeFadeLabel.setText("FADE ms", juce::dontSendNotification);
        chokeFadeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(chokeFadeLabel);
        chokeFadeSlider.setRange(1.0, 100.0, 1.0);
        chokeFadeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 22);
        chokeFadeSlider.onValueChange = [this] { sampler.setPadChokeFade(selectedPad, (float)chokeFadeSlider.getValue()); };
        addAndMakeVisible(chokeFadeSlider);
        playModeLabel.setText("MODE", juce::dontSendNotification);
        playModeLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(playModeLabel);
        playModeSelector.addItem("ONE SHOT", 1);
        playModeSelector.addItem("GATE", 2);
        playModeSelector.onChange = [this] { sampler.setPadGateMode(selectedPad, playModeSelector.getSelectedId() == 2); };
        addAndMakeVisible(playModeSelector);
        const char* names[] = { "ATTACK", "DECAY", "SUSTAIN", "RELEASE" };
        const double maximums[] = { 2000.0, 2000.0, 100.0, 5000.0 };
        const double defaults[] = { 0.0, 0.0, 100.0, 8.0 };
        for (int k = 0; k < 4; ++k)
        {
            adsrLabels[(size_t)k].setText(names[k], juce::dontSendNotification);
            adsrLabels[(size_t)k].setColour(juce::Label::textColourId, juce::Colours::white);
            addAndMakeVisible(adsrLabels[(size_t)k]);
            auto& slider = adsrSliders[(size_t)k];
            slider.setRange(k == 3 ? 1.0 : 0.0, maximums[k], k == 2 ? 1.0 : 1.0);
            slider.setValue(defaults[k], juce::dontSendNotification);
            slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 22);
            slider.onValueChange = [this] { applyEnvelope(); };
            addAndMakeVisible(slider);
        }
        gainLabel.setText("GAIN", juce::dontSendNotification);
        panLabel.setText("PAN", juce::dontSendNotification);
        gainLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        panLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(gainLabel);
        addAndMakeVisible(panLabel);
        // Shared Obsidian control palette, without changing slider values or callbacks.
        for (auto* slider : { &gainSlider, &panSlider, &pitchSlider, &startSlider,
                              &endSlider, &chokeFadeSlider })
        {
            slider->setColour(juce::Slider::backgroundColourId, juce::Colour(0xff25323e));
            slider->setColour(juce::Slider::trackColourId, juce::Colour(0xff54d6dc));
            slider->setColour(juce::Slider::thumbColourId, juce::Colour(0xffe8eff7));
            slider->setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffe8eff7));
            slider->setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff111922));
            slider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff344452));
        }
        for (auto& slider : adsrSliders)
        {
            slider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff25323e));
            slider.setColour(juce::Slider::trackColourId, juce::Colour(0xff54d6dc));
            slider.setColour(juce::Slider::thumbColourId, juce::Colour(0xffe8eff7));
            slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffe8eff7));
            slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff111922));
            slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff344452));
        }
        for (auto* selector : { &factorySelector, &chokeSelector, &playModeSelector })
        {
            selector->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff25323e));
            selector->setColour(juce::ComboBox::textColourId, juce::Colour(0xffe8eff7));
            selector->setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff344452));
            selector->setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff54d6dc));
        }
        // Action colours are configured beside each button above, so their
        // individual Obsidian states are not overwritten by a shared palette.
        refreshLabels();
        updateControls();
    }
    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        for (const auto& path : files)
        {
            const auto extension = juce::File(path).getFileExtension().toLowerCase();
            if (extension == ".wav" || extension == ".aif" || extension == ".aiff")
                return true;
        }
        return false;
    }
    void filesDropped(const juce::StringArray& files, int x, int y) override
    {
        int destination = selectedPad;
        for (int i = 0; i < LibertyDrumSampler::padCount; ++i)
            if (pads[(size_t)i].getBounds().contains(x, y))
            {
                destination = i;
                break;
            }
        for (const auto& path : files)
        {
            const juce::File file(path);
            const auto extension = file.getFileExtension().toLowerCase();
            if (extension != ".wav" && extension != ".aif" && extension != ".aiff")
                continue;
            if (sampler.loadPad(destination, file))
            {
                selectedPad = destination;
                updateControls();
                refreshLabels();
                waveform.selectPad(selectedPad);
                repaint();
            }
            break;
        }
    }
    void resized() override
    {
        const int margin = 12;
        // Reserve a fixed lower section for the waveform, kit actions and ADSR.
        // Derive the four pad rows from the actual space above that section.
        const int actionsTop = getHeight() - 550;
        factorySelector.setBounds(juce::jmax(margin, getWidth() - 226), 8, 214, 28);
        const int padTop = 40 + margin;
        const int padBottom = actionsTop - margin;
        const int cellWidth = juce::jmax(1, (getWidth() - margin * 5) / 4);
        const int padAreaHeight = juce::jmax(0, padBottom - padTop);
        const int cellHeight = juce::jmax(0, (padAreaHeight - margin * 3) / 4);
        for (int i = 0; i < LibertyDrumSampler::padCount; ++i)
            pads[(size_t)i].setBounds(margin + (i % 4) * (cellWidth + margin),
                                     padTop + (i / 4) * (cellHeight + margin), cellWidth, cellHeight);
        // Actions live above the waveform, never on top of the last ADSR row.
        // Keep kit actions inside the panel even when the window is narrower.
        // The waveform begins 40 px below this row.
        const int actionGap = 8;
        const int availableWidth = juce::jmax(0, getWidth() - margin * 2);
        const int actionWidth = juce::jmax(0, (availableWidth - actionGap * 3) / 4);
        const int lastActionWidth = juce::jmax(0, availableWidth - 3 * (actionWidth + actionGap));
        loadButton.setBounds(margin, actionsTop, actionWidth, 28);
        stopButton.setBounds(margin + actionWidth + actionGap, actionsTop, actionWidth, 28);
        exportButton.setBounds(margin + 2 * (actionWidth + actionGap), actionsTop, actionWidth, 28);
        importButton.setBounds(margin + 3 * (actionWidth + actionGap), actionsTop, lastActionWidth, 28);
        const int controlTop = getHeight() - 382;
        waveform.setBounds(margin, getHeight() - 470, getWidth() - margin * 2, 76);
        // Three distinct rows: kit actions (-550..-522), pad actions
        // (-516..-490), waveform (-470..-394), then GAIN (-382).
        // Never place interactive buttons inside the waveform component.
        const int loopActionsY = actionsTop + 34;
        const int padActionGap = 8;
        const int loopWidth = juce::jmin(190, juce::jmax(0, (availableWidth - padActionGap) / 2));
        const int colourWidth = juce::jmin(160, juce::jmax(0, availableWidth - padActionGap - loopWidth));
        const int padActionLeft = margin + (availableWidth - loopWidth - padActionGap - colourWidth) / 2;
        loopButton.setBounds(padActionLeft, loopActionsY, loopWidth, 26);
        colourButton.setBounds(padActionLeft + loopWidth + padActionGap, loopActionsY, colourWidth, 26);
        gainLabel.setBounds(margin, controlTop, 48, 25);
        gainSlider.setBounds(margin + 48, controlTop, juce::jmax(80, getWidth() - margin * 2 - 48), 25);
        panLabel.setBounds(margin, controlTop + 31, 48, 25);
        panSlider.setBounds(margin + 48, controlTop + 31, juce::jmax(80, getWidth() - margin * 2 - 48), 25);
        pitchLabel.setBounds(margin, controlTop + 62, 48, 25);
        pitchSlider.setBounds(margin + 48, controlTop + 62, juce::jmax(80, getWidth() - margin * 2 - 48), 25);
        startLabel.setBounds(margin, controlTop + 93, 48, 25);
        startSlider.setBounds(margin + 48, controlTop + 93, juce::jmax(80, getWidth() - margin * 2 - 48), 25);
        endLabel.setBounds(margin, controlTop + 124, 48, 25);
        endSlider.setBounds(margin + 48, controlTop + 124, juce::jmax(80, getWidth() - margin * 2 - 48), 25);
        chokeLabel.setBounds(margin, controlTop + 155, 56, 25);
        chokeSelector.setBounds(margin + 56, controlTop + 155, 160, 25);
        chokeFadeLabel.setBounds(margin, controlTop + 186, 72, 25);
        chokeFadeSlider.setBounds(margin + 72, controlTop + 186, juce::jmax(80, getWidth() - margin * 2 - 72), 25);
        playModeLabel.setBounds(margin, controlTop + 217, 56, 25);
        playModeSelector.setBounds(margin + 56, controlTop + 217, 160, 25);
        for (int k = 0; k < 4; ++k)
        {
            adsrLabels[(size_t)k].setBounds(margin, controlTop + 248 + 31 * k, 72, 25);
            adsrSliders[(size_t)k].setBounds(margin + 72, controlTop + 248 + 31 * k,
                juce::jmax(80, getWidth() - margin * 2 - 72), 25);
        }
    }
    void paint(juce::Graphics& g) override
    {
        juce::ColourGradient background(juce::Colour(0xff202c37), 0.0f, 0.0f,
                                         juce::Colour(0xff0b0f14), 0.0f, (float)getHeight(), false);
        g.setGradientFill(background);
        g.fillAll();
        // Draw depth behind the existing TextButtons without changing hitboxes.
        for (int i = 0; i < LibertyDrumSampler::padCount; ++i)
        {
            const auto bounds = pads[(size_t)i].getBounds().toFloat();
            if (bounds.isEmpty()) continue;
            const bool active = i == selectedPad;
            const auto padColour = juce::Colour((juce::uint32)sampler.getPad(i).colourArgb.load());
            g.setColour(juce::Colour(0xff070b10));
            g.fillRoundedRectangle(bounds.expanded(3.0f), 8.0f);
            // Selected pads receive a crisp turquoise edge; custom pad colours remain visible.
            g.setColour(active ? juce::Colour(0xff54d6dc).withAlpha(0.92f) : padColour.withAlpha(0.42f));
            g.drawRoundedRectangle(bounds.expanded(active ? 3.0f : 2.0f), 8.0f, active ? 2.0f : 1.0f);
            if (active)
            {
                g.setColour(juce::Colour(0xff54d6dc).withAlpha(0.18f));
                g.drawRoundedRectangle(bounds.expanded(5.0f), 10.0f, 1.0f);
            }
        }
        // Restrained Obsidian header: graphite baseline and short turquoise accent.
        g.setColour(juce::Colour(0xff344452));
        g.fillRect(12, 36, juce::jmax(0, getWidth() - 24), 1);
        g.setColour(juce::Colour(0xff54d6dc).withAlpha(0.72f));
        g.fillRect(12, 36, juce::jmax(0, juce::jmin(104, getWidth() - 24)), 1);
        g.setColour(juce::Colour(0xffe8eff7));
        g.setFont(juce::Font(18.0f, juce::Font::bold));
        g.drawText("LIBERTY DRUM SAMPLER", 12, 7, getWidth() - 250, 26, juce::Justification::centredLeft);
        const int actionsTop = getHeight() - 550;
        const int innerWidth = juce::jmax(0, getWidth() - 24);
        // Section dividers sit in existing non-interactive gaps.
        g.setColour(juce::Colour(0xff344452));
        g.fillRect(12, actionsTop - 7, innerWidth, 1);
        g.fillRect(12, getHeight() - 390, innerWidth, 1);
        g.setColour(juce::Colour(0xff54d6dc).withAlpha(0.58f));
        g.fillRect(12, actionsTop - 7, juce::jmin(72, innerWidth), 1);
        // Violet distinguishes waveform editing from the kit action strip.
        g.setColour(juce::Colour(0xffc4adff).withAlpha(0.32f));
        g.fillRect(12, getHeight() - 390, juce::jmin(72, innerWidth), 1);
        // Keep section labels away from pads and editable controls.
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.setColour(juce::Colour(0xffb5c3d1));
        g.drawText("PADS  /  16 VOICES", 12, 39, juce::jmax(0, getWidth() - 250), 12,
                   juce::Justification::centredLeft);
        g.drawText("SAMPLE  /  WAVEFORM", 12, getHeight() - 486, innerWidth, 13,
                   juce::Justification::centredLeft);
    }
private:
    void refreshPadColours()
    {
        for (int i = 0; i < LibertyDrumSampler::padCount; ++i)
            pads[(size_t)i].setColour(juce::TextButton::buttonColourId,
                juce::Colour((juce::uint32)sampler.getPad(i).colourArgb.load()));
    }
    void applyEnvelope()
    {
        sampler.setPadEnvelope(selectedPad, (float)adsrSliders[0].getValue(),
            (float)adsrSliders[1].getValue(), (float)adsrSliders[2].getValue() / 100.0f,
            (float)adsrSliders[3].getValue());
    }
    void applyTrim()
    {
        auto start = (float)startSlider.getValue() / 100.0f;
        auto end = (float)endSlider.getValue() / 100.0f;
        if (end < start + 0.01f)
        {
            end = juce::jmin(1.0f, start + 0.01f);
            endSlider.setValue(end * 100.0f, juce::dontSendNotification);
        }
        sampler.setPadTrim(selectedPad, start, end);
        waveform.repaint();
    }
    void updateControls()
    {
        const auto& pad = sampler.getPad(selectedPad);
        loopButton.setToggleState(pad.loopEnabled.load(), juce::dontSendNotification);
        gainSlider.setValue(pad.gain.load(), juce::dontSendNotification);
        panSlider.setValue(pad.pan.load(), juce::dontSendNotification);
        pitchSlider.setValue(pad.pitchSemitones.load(), juce::dontSendNotification);
        startSlider.setValue(pad.startFraction.load() * 100.0f, juce::dontSendNotification);
        endSlider.setValue(pad.endFraction.load() * 100.0f, juce::dontSendNotification);
        chokeSelector.setSelectedId(pad.chokeGroup.load() + 1, juce::dontSendNotification);
        chokeFadeSlider.setValue(pad.chokeFadeMs.load(), juce::dontSendNotification);
        playModeSelector.setSelectedId(pad.gateMode.load() ? 2 : 1, juce::dontSendNotification);
        const double values[] = { pad.attackMs.load(), pad.decayMs.load(), pad.sustain.load() * 100.0, pad.releaseMs.load() };
        for (int k = 0; k < 4; ++k)
            adsrSliders[(size_t)k].setValue(values[k], juce::dontSendNotification);
    }
    void refreshLabels()
    {
        refreshPadColours();
        for (int i = 0; i < LibertyDrumSampler::padCount; ++i)
        {
            pads[(size_t)i].setButtonText(sampler.hasSample(i) ? sampler.getPad(i).name
                                                               : "PAD " + juce::String(i + 1));
        }
    }
    LibertyDrumSampler& sampler;
    std::array<juce::TextButton, LibertyDrumSampler::padCount> pads;
    juce::TextButton loadButton, stopButton, exportButton, importButton, loopButton, colourButton;
    juce::ComboBox factorySelector;
    LibertyDrumWaveform waveform;
    juce::Slider gainSlider, panSlider, pitchSlider, startSlider, endSlider;
    juce::Label gainLabel, panLabel, pitchLabel, startLabel, endLabel, chokeLabel;
    juce::ComboBox chokeSelector;
    juce::Slider chokeFadeSlider;
    juce::Label chokeFadeLabel;
    juce::ComboBox playModeSelector;
    juce::Label playModeLabel;
    std::array<juce::Slider, 4> adsrSliders;
    std::array<juce::Label, 4> adsrLabels;
    int selectedPad = 0;
    std::unique_ptr<juce::FileChooser> chooser, kitChooser;
};
