#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
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
        std::shared_ptr<const juce::AudioBuffer<float>> audio;
        std::atomic<double> sourceRate { 44100.0 };
        std::atomic<float> gain { 1.0f };
        std::atomic<float> pan { 0.0f };
        std::atomic<float> pitchSemitones { 0.0f };
        std::atomic<float> startFraction { 0.0f };
        std::atomic<float> endFraction { 1.0f };
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
                auto audio = std::atomic_load(&pads[(size_t)i].audio);
                if (audio == nullptr || audio->getNumSamples() == 0) return;
                auto* voice = &voices[0];
                for (auto& candidate : voices) if (!candidate.active) { voice = &candidate; break; }
                const int frames = audio->getNumSamples();
                const float start = pads[(size_t)i].startFraction.load();
                *voice = {true, i, (double)juce::jlimit(0, frames - 1, (int)(start * (frames - 1))),
                          juce::jlimit(0.0f, 1.0f, velocity), std::move(audio), pads[(size_t)i].sourceRate.load()};
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
                const auto& pad = pads[(size_t)voice.pad];
                const int frame = (int)voice.position;
                if (voice.audio == nullptr || frame >= (int)(voice.audio->getNumSamples() * pad.endFraction.load())) { voice.active = false; voice.audio.reset(); continue; }
                const float fraction = (float)(voice.position - frame);
                const int next = juce::jmin(frame + 1, voice.audio->getNumSamples() - 1);
                for (int channel = 0; channel < output.getNumChannels(); ++channel)
                {
                    const int source = juce::jmin(channel, voice.audio->getNumChannels() - 1);
                    const float a = voice.audio->getSample(source, frame);
                    const float b = voice.audio->getSample(source, next);
                    const float panGain = channel == 0 ? juce::jmin(1.0f, 1.0f - pad.pan.load())
                                                       : juce::jmin(1.0f, 1.0f + pad.pan.load());
                    output.addSample(channel, n, (a + (b - a) * fraction) * voice.velocity * pad.gain.load() * panGain);
                }
                voice.position += (voice.sourceRate / outputRate)
                    * std::pow(2.0, (double)pad.pitchSemitones.load() / 12.0);
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
    };
    juce::AudioFormatManager formats;
    std::array<Pad, padCount> pads;
    std::array<Voice, 32> voices{};
    std::atomic<unsigned int> pendingTriggers { 0 };
    std::array<std::atomic<float>, padCount> pendingVelocity {};
    double outputRate = 44100.0;
};


// First visual prototype. Owned by the message thread; audio integration is next.
class LibertyDrumSamplerPanel final : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit LibertyDrumSamplerPanel(LibertyDrumSampler& engine) : sampler(engine)
    {
        for (int i = 0; i < LibertyDrumSampler::padCount; ++i)
        {
            auto& button = pads[(size_t)i];
            button.setButtonText("PAD " + juce::String(i + 1));
            button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff253c4a));
            button.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            button.onClick = [this, i] { selectedPad = i; updateControls(); sampler.auditionPad(i, 1.0f); };
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
                repaint();
            }
            break;
        }
    }
    void resized() override
    {
        const int margin = 12;
        const int cellWidth = juce::jmax(1, (getWidth() - margin * 5) / 4);
        const int cellHeight = juce::jmax(1, (getHeight() - 181 - margin * 5) / 4);
        for (int i = 0; i < LibertyDrumSampler::padCount; ++i)
            pads[(size_t)i].setBounds(margin + (i % 4) * (cellWidth + margin),
                                     40 + margin + (i / 4) * (cellHeight + margin), cellWidth, cellHeight);
        loadButton.setBounds(margin, getHeight() - 34, juce::jmin(180, getWidth() - 2 * margin), 25);
        const int controlTop = getHeight() - 134;
        gainLabel.setBounds(margin, controlTop, 48, 25);
        gainSlider.setBounds(margin + 48, controlTop, juce::jmax(80, getWidth() - margin * 2 - 48), 25);
        panLabel.setBounds(margin, controlTop + 31, 48, 25);
        panSlider.setBounds(margin + 48, controlTop + 31, juce::jmax(80, getWidth() - margin * 2 - 48), 25);
        pitchLabel.setBounds(margin, controlTop + 62, 48, 25);
        pitchSlider.setBounds(margin + 48, controlTop + 62, juce::jmax(80, getWidth() - margin * 2 - 48), 25);
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff121c27));
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(18.0f, juce::Font::bold));
        g.drawText("LIBERTY DRUM SAMPLER", 12, 7, getWidth() - 24, 26, juce::Justification::centredLeft);
    }
private:
    void updateControls()
    {
        const auto& pad = sampler.getPad(selectedPad);
        gainSlider.setValue(pad.gain.load(), juce::dontSendNotification);
        panSlider.setValue(pad.pan.load(), juce::dontSendNotification);
        pitchSlider.setValue(pad.pitchSemitones.load(), juce::dontSendNotification);
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
    juce::TextButton loadButton;
    juce::Slider gainSlider, panSlider, pitchSlider;
    juce::Label gainLabel, panLabel, pitchLabel;
    int selectedPad = 0;
    std::unique_ptr<juce::FileChooser> chooser;
};
