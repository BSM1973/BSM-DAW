#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <memory>
#include <vector>
#include <cmath>

// Liberty Drum Sampler: first milestone, 16 velocity-sensitive one-shot pads.
// Prepare and load on the message thread, then render on the audio thread.
class LibertyDrumSampler final
{
public:
    static constexpr int padCount = 16;
    struct Pad
    {
        juce::String name;
        juce::AudioBuffer<float> audio;
        double sourceRate = 44100.0;
        float gain = 1.0f;
        float pan = 0.0f;
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
        pads[(size_t)index].audio = std::move(loaded);
        pads[(size_t)index].sourceRate = reader->sampleRate;
        pads[(size_t)index].name = file.getFileNameWithoutExtension();
        return true;
    }
    bool hasSample(int index) const noexcept
    {
        return index >= 0 && index < padCount && pads[(size_t)index].audio.getNumSamples() > 0;
    }
    void noteOn(int note, float velocity) noexcept
    {
        if (velocity <= 0.0f) return;
        for (int i = 0; i < padCount; ++i)
            if (pads[(size_t)i].midiNote == note && pads[(size_t)i].audio.getNumSamples() > 0)
            {
                auto* voice = &voices[0];
                for (auto& candidate : voices) if (!candidate.active) { voice = &candidate; break; }
                *voice = {true, i, 0.0, juce::jlimit(0.0f, 1.0f, velocity)};
                return;
            }
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
                if (frame >= pad.audio.getNumSamples()) { voice.active = false; continue; }
                const float fraction = (float)(voice.position - frame);
                const int next = juce::jmin(frame + 1, pad.audio.getNumSamples() - 1);
                for (int channel = 0; channel < output.getNumChannels(); ++channel)
                {
                    const int source = juce::jmin(channel, pad.audio.getNumChannels() - 1);
                    const float a = pad.audio.getSample(source, frame);
                    const float b = pad.audio.getSample(source, next);
                    const float panGain = channel == 0 ? juce::jmin(1.0f, 1.0f - pad.pan)
                                                       : juce::jmin(1.0f, 1.0f + pad.pan);
                    output.addSample(channel, n, (a + (b - a) * fraction) * voice.velocity * pad.gain * panGain);
                }
                voice.position += pad.sourceRate / outputRate;
            }
    }
    const Pad& getPad(int index) const noexcept { return pads[(size_t)juce::jlimit(0, padCount-1, index)]; }
private:
    struct Voice { bool active = false; int pad = 0; double position = 0; float velocity = 1; };
    juce::AudioFormatManager formats;
    std::array<Pad, padCount> pads;
    std::array<Voice, 32> voices{};
    double outputRate = 44100.0;
};


// First visual prototype. Owned by the message thread; audio integration is next.
class LibertyDrumSamplerPanel final : public juce::Component
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
            button.onClick = [this, i] { selectedPad = i; sampler.noteOn(36 + i, 1.0f); };
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
        refreshLabels();
    }
    void resized() override
    {
        const int margin = 12;
        const int cellWidth = juce::jmax(1, (getWidth() - margin * 5) / 4);
        const int cellHeight = juce::jmax(1, (getHeight() - 78 - margin * 5) / 4);
        for (int i = 0; i < LibertyDrumSampler::padCount; ++i)
            pads[(size_t)i].setBounds(margin + (i % 4) * (cellWidth + margin),
                                     40 + margin + (i / 4) * (cellHeight + margin), cellWidth, cellHeight);
        loadButton.setBounds(margin, getHeight() - 32, juce::jmin(180, getWidth() - 2 * margin), 25);
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff121c27));
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(18.0f, juce::Font::bold));
        g.drawText("LIBERTY DRUM SAMPLER", 12, 7, getWidth() - 24, 26, juce::Justification::centredLeft);
    }
private:
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
    int selectedPad = 0;
    std::unique_ptr<juce::FileChooser> chooser;
};
