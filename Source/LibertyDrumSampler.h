#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>
#include <array>
#include <memory>
#include <vector>
#include <cmath>
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
        std::atomic<int> chokeGroup { 0 };
        std::atomic<float> chokeFadeMs { 8.0f };
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
    void processAuditions() noexcept
    {
        const auto triggered = pendingTriggers.exchange(0, std::memory_order_acquire);
        for (int i = 0; i < padCount; ++i)
            if ((triggered & (1u << i)) != 0)
                noteOn(36 + i, pendingVelocity[(size_t)i].load(std::memory_order_relaxed));
    }
    void reset() noexcept { for (auto& voice : voices) voice.active = false; }
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
            node->setAttribute("file", pad.sourcePath);
            node->setAttribute("gain", (double)pad.gain.load());
            node->setAttribute("pan", (double)pad.pan.load());
            node->setAttribute("pitch", (double)pad.pitchSemitones.load());
            node->setAttribute("start", (double)pad.startFraction.load());
            node->setAttribute("end", (double)pad.endFraction.load());
            node->setAttribute("chokeGroup", pad.chokeGroup.load());
            node->setAttribute("chokeFadeMs", (double)pad.chokeFadeMs.load());
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
            setPadGain(i, (float)node->getDoubleAttribute("gain", 1.0));
            setPadPan(i, (float)node->getDoubleAttribute("pan", 0.0));
            setPadPitch(i, (float)node->getDoubleAttribute("pitch", 0.0));
            setPadTrim(i, (float)node->getDoubleAttribute("start", 0.0),
                          (float)node->getDoubleAttribute("end", 1.0));
            setPadChokeGroup(i, node->getIntAttribute("chokeGroup", 0));
            setPadChokeFade(i, (float)node->getDoubleAttribute("chokeFadeMs", 8.0));
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
            if (path.isEmpty()) continue;
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
        for (auto* node : xml->getChildIterator())
        {
            if (!node->hasTagName("Pad")) continue;
            const int index = node->getIntAttribute("index", -1);
            if (index < 0 || index >= padCount) return false;
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
            dst.chokeGroup.store(src.chokeGroup.load());
            dst.chokeFadeMs.store(src.chokeFadeMs.load());
        }
        // Active voices keep their immutable sample buffers until they finish.
        return true;
    }
    bool hasSample(int index) const noexcept
    {
        return index >= 0 && index < padCount && std::atomic_load(&pads[(size_t)index].audio) != nullptr;
    }
    void noteOn(int note, float velocity) noexcept
    {
        if (velocity <= 0.0f) return;
        for (int i = 0; i < padCount; ++i)
            if (pads[(size_t)i].midiNote == note)
            {
                const int group = pads[(size_t)i].chokeGroup.load();
                if (group > 0)
                    for (auto& activeVoice : voices)
                        if (activeVoice.active && activeVoice.chokeGroup == group)
                        {
                            // A short release avoids clicks from abrupt hi-hat choking.
                            const int requested = juce::jmax(1, (int)(outputRate * pads[(size_t)i].chokeFadeMs.load() / 1000.0));
                            if (activeVoice.releaseSamplesRemaining == 0 || requested < activeVoice.releaseSamplesRemaining)
                            {
                                activeVoice.releaseSamplesRemaining = requested;
                                activeVoice.releaseSamplesTotal = requested;
                            }
                        }
                auto audio = std::atomic_load(&pads[(size_t)i].audio);
                if (audio == nullptr || audio->getNumSamples() == 0) return;
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
                          pads[(size_t)i].chokeGroup.load(), 0, 0};
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
            else if (message.isAllSoundOff() || message.isAllNotesOff())
                reset();
            cursor = juce::jmax(cursor, offset);
        }
        if (cursor < output.getNumSamples())
            render(output, cursor, output.getNumSamples() - cursor);
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
        for (int n = juce::jmax(0, start); n < end; ++n)
            for (auto& voice : voices)
            {
                if (!voice.active) continue;
                const int frame = (int)voice.position;
                if (voice.audio == nullptr || frame >= (int)(voice.audio->getNumSamples() * voice.endFraction)) { voice.active = false; voice.audio.reset(); continue; }
                const float fraction = (float)(voice.position - frame);
                const int next = juce::jmin(frame + 1, voice.audio->getNumSamples() - 1);
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
                    output.addSample(channel, n, (a + (b - a) * fraction) * voice.velocity * voice.gain * panGain * releaseGain);
                }
                if (voice.releaseSamplesRemaining > 0 && --voice.releaseSamplesRemaining == 0)
                {
                    voice.active = false;
                    voice.audio.reset();
                    continue;
                }
                voice.position += (voice.sourceRate / outputRate)
                    * std::pow(2.0, (double)voice.pitchSemitones / 12.0);
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
    };
    uint64_t voiceSequence = 0;
    juce::AudioFormatManager formats;
    std::array<Pad, padCount> pads;
    std::array<Voice, 32> voices{};
    std::atomic<unsigned int> pendingTriggers { 0 };
    std::array<std::atomic<float>, padCount> pendingVelocity {};
    double outputRate = 44100.0;
};


// First visual prototype. Owned by the message thread; audio integration is next.
// Read-only waveform overview for the selected drum pad.
class LibertyDrumWaveform final : public juce::Component
{
public:
    explicit LibertyDrumWaveform(LibertyDrumSampler& engine) : sampler(engine) {}
    void selectPad(int index) { selectedPad = index; repaint(); }
    std::function<void()> onTrimChanged;
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (!sampler.hasSample(selectedPad) || getWidth() <= 0) return;
        const auto& pad = sampler.getPad(selectedPad);
        const float x = juce::jlimit(0.0f, 1.0f, (float)event.x / (float)getWidth());
        const float start = pad.startFraction.load();
        const float end = pad.endFraction.load();
        draggingStart = std::abs(x - start) <= std::abs(x - end);
        mouseDrag(event);
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!sampler.hasSample(selectedPad) || getWidth() <= 0) return;
        const auto& pad = sampler.getPad(selectedPad);
        const float x = juce::jlimit(0.0f, 1.0f, (float)event.x / (float)getWidth());
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
        g.setColour(juce::Colour(0xff0a111a));
        g.fillRoundedRectangle(bounds, 5.0f);
        const auto& pad = sampler.getPad(selectedPad);
        const auto audio = std::atomic_load(&pad.audio);
        if (audio == nullptr || audio->getNumSamples() == 0)
        {
            g.setColour(juce::Colours::grey);
            g.drawFittedText("CHARGER UN SAMPLE POUR VOIR SA FORME D'ONDE",
                             getLocalBounds(), juce::Justification::centred, 1);
            return;
        }
        const int width = getWidth();
        const float mid = getHeight() * 0.5f;
        const float amplitude = juce::jmax(1.0f, mid - 5.0f);
        const int frames = audio->getNumSamples();
        g.setColour(juce::Colour(0xff4fc4b5));
        for (int x = 0; x < width; ++x)
        {
            const int begin = (int)((int64_t)x * frames / juce::jmax(1, width));
            const int finish = juce::jmin(frames, (int)((int64_t)(x + 1) * frames / juce::jmax(1, width)));
            float peak = 0.0f;
            for (int n = begin; n < finish; ++n)
                for (int ch = 0; ch < audio->getNumChannels(); ++ch)
                    peak = juce::jmax(peak, std::abs(audio->getSample(ch, n)));
            const float height = juce::jmin(1.0f, peak) * amplitude;
            g.drawVerticalLine(x, mid - height, mid + height);
        }
        const float start = pad.startFraction.load();
        const float end = pad.endFraction.load();
        g.setColour(juce::Colour(0x880a111a));
        g.fillRect(0.0f, 0.0f, bounds.getWidth() * start, bounds.getHeight());
        g.fillRect(bounds.getWidth() * end, 0.0f, bounds.getWidth() * (1.0f - end), bounds.getHeight());
        g.setColour(juce::Colour(0xffffb454));
        g.drawVerticalLine((int)(bounds.getWidth() * start), 0.0f, bounds.getHeight());
        g.drawVerticalLine(juce::jmin(width - 1, (int)(bounds.getWidth() * end)), 0.0f, bounds.getHeight());
    }
private:
    LibertyDrumSampler& sampler;
    int selectedPad = 0;
    bool draggingStart = true;
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
            button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff253c4a));
            button.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            button.onClick = [this, i] { selectedPad = i; updateControls(); waveform.selectPad(i); sampler.auditionPad(i, 1.0f); };
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
        addAndMakeVisible(loadButton);
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
        addAndMakeVisible(importButton);
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
        gainLabel.setText("GAIN", juce::dontSendNotification);
        panLabel.setText("PAN", juce::dontSendNotification);
        gainLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        panLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(gainLabel);
        addAndMakeVisible(panLabel);
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
        const int cellWidth = juce::jmax(1, (getWidth() - margin * 5) / 4);
        const int cellHeight = juce::jmax(1, (getHeight() - 324 - margin * 5) / 4);
        for (int i = 0; i < LibertyDrumSampler::padCount; ++i)
            pads[(size_t)i].setBounds(margin + (i % 4) * (cellWidth + margin),
                                     40 + margin + (i / 4) * (cellHeight + margin), cellWidth, cellHeight);
        loadButton.setBounds(margin, getHeight() - 34, juce::jmin(180, getWidth() - 2 * margin), 25);
        exportButton.setBounds(margin + 190, getHeight() - 34, 150, 25);
        importButton.setBounds(margin + 350, getHeight() - 34, 150, 25);
        const int controlTop = getHeight() - 196;
        waveform.setBounds(margin, getHeight() - 284, getWidth() - margin * 2, 76);
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
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff121c27));
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(18.0f, juce::Font::bold));
        g.drawText("LIBERTY DRUM SAMPLER", 12, 7, getWidth() - 24, 26, juce::Justification::centredLeft);
    }
private:
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
        gainSlider.setValue(pad.gain.load(), juce::dontSendNotification);
        panSlider.setValue(pad.pan.load(), juce::dontSendNotification);
        pitchSlider.setValue(pad.pitchSemitones.load(), juce::dontSendNotification);
        startSlider.setValue(pad.startFraction.load() * 100.0f, juce::dontSendNotification);
        endSlider.setValue(pad.endFraction.load() * 100.0f, juce::dontSendNotification);
        chokeSelector.setSelectedId(pad.chokeGroup.load() + 1, juce::dontSendNotification);
    }
    void refreshLabels()
    {
        for (int i = 0; i < LibertyDrumSampler::padCount; ++i)
        {
            pads[(size_t)i].setButtonText(sampler.hasSample(i) ? sampler.getPad(i).name
                                                               : "PAD " + juce::String(i + 1));
        }
    }
    LibertyDrumSampler& sampler;
    std::array<juce::TextButton, LibertyDrumSampler::padCount> pads;
    juce::TextButton loadButton, exportButton, importButton;
    LibertyDrumWaveform waveform;
    juce::Slider gainSlider, panSlider, pitchSlider, startSlider, endSlider;
    juce::Label gainLabel, panLabel, pitchLabel, startLabel, endLabel, chokeLabel;
    juce::ComboBox chokeSelector;
    int selectedPad = 0;
    std::unique_ptr<juce::FileChooser> chooser, kitChooser;
};
