#include "OneKnobEffects.h"
#include <cmath>

namespace
{
class OneKnobEditor final : public juce::Component, private juce::Timer
{
public:
    OneKnobEditor(LibertyOneKnobManager& m, int t) : manager(m), track(t)
    {
        knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 78, 24);
        knob.setRange(0.0, 100.0, 1.0);
        knob.setValue(manager.getAmount(track) * 100.0, juce::dontSendNotification);
        knob.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff22c7ff));
        knob.setColour(juce::Slider::thumbColourId, juce::Colours::white);
        knob.onValueChange = [this] { manager.setAmount(track, (float)knob.getValue() / 100.0f); };
        addAndMakeVisible(knob);
        startTimerHz(8);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff11151b));
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(18.0f, juce::Font::bold));
        g.drawText("LIBERTY", 0, 14, getWidth(), 26, juce::Justification::centred);
        g.setColour(juce::Colour(0xff22c7ff));
        g.setFont(juce::Font(15.0f, juce::Font::bold));
        g.drawText(manager.getName(track).toUpperCase(), 0, 42, getWidth(), 24, juce::Justification::centred);
        g.setColour(juce::Colour(0xff89939e));
        g.setFont(juce::Font(11.0f));
        g.drawText("ONE KNOB", 0, getHeight() - 34, getWidth(), 18, juce::Justification::centred);
    }

    void resized() override { knob.setBounds(getLocalBounds().reduced(50, 72)); }

private:
    void timerCallback() override
    {
        const auto wanted = manager.getAmount(track) * 100.0f;
        if (std::abs((float)knob.getValue() - wanted) > 0.01f)
            knob.setValue(wanted, juce::dontSendNotification);
    }

    LibertyOneKnobManager& manager;
    int track;
    juce::Slider knob;
};

class OneKnobWindow final : public juce::DocumentWindow
{
public:
    OneKnobWindow(LibertyOneKnobManager& manager, int track)
        : juce::DocumentWindow("Liberty - " + manager.getName(track), juce::Colour(0xff11151b),
                               juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setResizable(false, false);
        auto* editor = new OneKnobEditor(manager, track);
        editor->setSize(300, 360);
        setContentOwned(editor, true);
        centreWithSize(300, 360);
        setVisible(true);
    }
    void closeButtonPressed() override { setVisible(false); }
};
}

LibertyOneKnobRack::LibertyOneKnobRack() = default;
LibertyOneKnobRack::~LibertyOneKnobRack() = default;

void LibertyOneKnobRack::prepare(double sr, int maximumBlockSize)
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32)juce::jmax(16, maximumBlockSize), 2 };
    chorus.prepare(spec); flanger.prepare(spec); phaser.prepare(spec);
    reset(); updateParameters();
}

void LibertyOneKnobRack::reset()
{
    chorus.reset(); flanger.reset(); phaser.reset(); tremoloPhase = 0.0;
}

void LibertyOneKnobRack::setType(Type newType)
{
    type = newType; reset(); updateParameters();
}

void LibertyOneKnobRack::setAmount(float newAmount) noexcept
{
    amount = juce::jlimit(0.0f, 1.0f, newAmount);
    updateParameters();
}

juce::String LibertyOneKnobRack::getName() const
{
    switch (type)
    {
        case Type::chorus: return "One Knob Chorus";
        case Type::flanger: return "One Knob Flanger";
        case Type::phaser: return "One Knob Phaser";
        case Type::tremolo: return "One Knob Tremolo";
        default: return {};
    }
}

void LibertyOneKnobRack::updateParameters()
{
    const float x = juce::jlimit(0.0f, 1.0f, amount);
    chorus.setRate(0.15f + 1.85f * x);
    chorus.setDepth(0.10f + 0.80f * x);
    chorus.setCentreDelay(7.0f + 8.0f * x);
    chorus.setFeedback(0.02f + 0.18f * x);
    chorus.setMix(0.08f + 0.52f * x);

    flanger.setRate(0.08f + 0.92f * x);
    flanger.setDepth(0.18f + 0.80f * x);
    flanger.setCentreDelay(0.7f + 3.3f * x);
    flanger.setFeedback(0.05f + 0.72f * x);
    flanger.setMix(0.08f + 0.62f * x);

    phaser.setRate(0.08f + 1.25f * x);
    phaser.setDepth(0.18f + 0.80f * x);
    phaser.setCentreFrequency(260.0f + 1500.0f * x);
    phaser.setFeedback(0.03f + 0.68f * x);
    phaser.setMix(0.08f + 0.68f * x);
}

void LibertyOneKnobRack::process(juce::AudioBuffer<float>& buffer)
{
    if (type == Type::none || buffer.getNumSamples() <= 0) return;
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    switch (type)
    {
        case Type::chorus: chorus.process(context); break;
        case Type::flanger: flanger.process(context); break;
        case Type::phaser: phaser.process(context); break;
        case Type::tremolo:
        {
            const float x = amount;
            const double rate = 1.0 + 8.0 * x;
            const float depth = 0.12f + 0.82f * x;
            for (int s = 0; s < buffer.getNumSamples(); ++s)
            {
                const float lfo = 0.5f + 0.5f * std::sin((float)tremoloPhase);
                const float gain = (1.0f - depth) + depth * lfo;
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch) buffer.setSample(ch, s, buffer.getSample(ch, s) * gain);
                tremoloPhase += juce::MathConstants<double>::twoPi * rate / sampleRate;
                if (tremoloPhase >= juce::MathConstants<double>::twoPi) tremoloPhase -= juce::MathConstants<double>::twoPi;
            }
            break;
        }
        default: break;
    }
}

LibertyOneKnobManager& LibertyOneKnobManager::instance()
{
    static LibertyOneKnobManager manager;
    return manager;
}

LibertyOneKnobManager::LibertyOneKnobManager() = default;

void LibertyOneKnobManager::prepare(double sampleRate, int maximumBlockSize)
{
    const juce::ScopedLock scoped(lock);
    for (auto& rack : racks) rack.prepare(sampleRate, maximumBlockSize);
}

void LibertyOneKnobManager::setEffect(int trackIndex, LibertyOneKnobRack::Type type)
{
    if (!validTrack(trackIndex)) return;
    const juce::ScopedLock scoped(lock);
    racks[(size_t)trackIndex].setType(type);
    if (editors[(size_t)trackIndex]) editors[(size_t)trackIndex].reset();
}

void LibertyOneKnobManager::clearEffect(int trackIndex)
{
    setEffect(trackIndex, LibertyOneKnobRack::Type::none);
}

LibertyOneKnobRack::Type LibertyOneKnobManager::getEffect(int trackIndex) const
{
    if (!validTrack(trackIndex)) return LibertyOneKnobRack::Type::none;
    const juce::ScopedLock scoped(lock);
    return racks[(size_t)trackIndex].getType();
}

void LibertyOneKnobManager::setAmount(int trackIndex, float amount)
{
    if (!validTrack(trackIndex)) return;
    const juce::ScopedLock scoped(lock);
    racks[(size_t)trackIndex].setAmount(amount);
}

float LibertyOneKnobManager::getAmount(int trackIndex) const
{
    if (!validTrack(trackIndex)) return 0.0f;
    const juce::ScopedLock scoped(lock);
    return racks[(size_t)trackIndex].getAmount();
}

juce::String LibertyOneKnobManager::getName(int trackIndex) const
{
    if (!validTrack(trackIndex)) return {};
    const juce::ScopedLock scoped(lock);
    return racks[(size_t)trackIndex].getName();
}

bool LibertyOneKnobManager::hasEffect(int trackIndex) const
{
    return getEffect(trackIndex) != LibertyOneKnobRack::Type::none;
}

void LibertyOneKnobManager::process(int trackIndex, juce::AudioBuffer<float>& buffer)
{
    if (!validTrack(trackIndex)) return;
    if (!lock.tryEnter()) return;
    racks[(size_t)trackIndex].process(buffer);
    lock.exit();
}

void LibertyOneKnobManager::beginAudioTrackBlock(int trackIndex,
                                                    float* const* outputChannelData,
                                                    int numOutputChannels,
                                                    int numSamples)
{
    if (!validTrack(trackIndex) || !hasEffect(trackIndex) || numSamples <= 0) return;
    if (!lock.tryEnter()) return;
    const int channels = juce::jlimit(1, 2, numOutputChannels);
    auto& baseline = baselines[(size_t)trackIndex];
    baseline.setSize(channels, numSamples, false, false, true);
    for (int ch = 0; ch < channels; ++ch)
    {
        if (outputChannelData[ch] != nullptr)
            baseline.copyFrom(ch, 0, outputChannelData[ch], numSamples);
        else
            baseline.clear(ch, 0, numSamples);
    }
    lock.exit();
}

void LibertyOneKnobManager::endAudioTrackBlock(int trackIndex,
                                                  float* const* outputChannelData,
                                                  int numOutputChannels,
                                                  int numSamples)
{
    if (!validTrack(trackIndex) || !hasEffect(trackIndex) || numSamples <= 0) return;
    if (!lock.tryEnter()) return;

    const int channels = juce::jlimit(1, 2, numOutputChannels);
    auto& baseline = baselines[(size_t)trackIndex];
    auto& work = workBuffers[(size_t)trackIndex];
    if (baseline.getNumSamples() != numSamples || baseline.getNumChannels() < channels)
    {
        lock.exit();
        return;
    }

    work.setSize(channels, numSamples, false, false, true);
    work.clear();
    for (int ch = 0; ch < channels; ++ch)
    {
        if (outputChannelData[ch] == nullptr) continue;
        work.copyFrom(ch, 0, outputChannelData[ch], numSamples);
        work.addFrom(ch, 0, baseline, ch, 0, numSamples, -1.0f);
    }

    racks[(size_t)trackIndex].process(work);

    for (int ch = 0; ch < channels; ++ch)
    {
        if (outputChannelData[ch] == nullptr) continue;
        juce::FloatVectorOperations::copy(outputChannelData[ch], baseline.getReadPointer(ch), numSamples);
        juce::FloatVectorOperations::add(outputChannelData[ch], work.getReadPointer(ch), numSamples);
    }
    lock.exit();
}

void LibertyOneKnobManager::showEditor(int trackIndex)
{
    if (!validTrack(trackIndex) || !hasEffect(trackIndex)) return;
    const juce::ScopedLock scoped(lock);
    auto& window = editors[(size_t)trackIndex];
    if (!window) window = std::make_unique<OneKnobWindow>(*this, trackIndex);
    else { window->setVisible(true); window->toFront(true); }
}
