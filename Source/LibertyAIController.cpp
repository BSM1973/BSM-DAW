#define private public
#include "MainComponent.h"
#undef private

#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <random>

namespace
{
constexpr int panelWidth = 430;
constexpr int topBarHeight = 76;

class LibertyAIPanel final : public juce::Component
{
public:
    explicit LibertyAIPanel(MainComponent& ownerIn) : owner(ownerIn)
    {
        title.setText("LIBERTY AI MUSIC", juce::dontSendNotification);
        title.setColour(juce::Label::textColourId, juce::Colours::white);
        title.setFont(juce::Font(18.0f, juce::Font::bold));
        addAndMakeVisible(title);

        prompt.setMultiLine(true);
        prompt.setReturnKeyStartsNewLine(true);
        prompt.setTextToShowWhenEmpty("Exemple : cree une melodie rock en A mineur sur 8 mesures", juce::Colour(0xff727b86));
        prompt.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff11151a));
        prompt.setColour(juce::TextEditor::textColourId, juce::Colours::white);
        prompt.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff343d47));
        addAndMakeVisible(prompt);

        configureButton(generate, "GENERATE");
        configureButton(chords, "CHORDS");
        configureButton(drums, "DRUMMER");
        configureButton(humanize, "HUMANIZE");
        configureButton(variation, "VARIATION");
        configureButton(clear, "CLEAR MIDI");
        configureButton(close, "CLOSE");

        generate.onClick = [this] { generateMelody(); };
        chords.onClick = [this] { generateChords(); };
        drums.onClick = [this] { generateDrums(); };
        humanize.onClick = [this] { humanizeMidi(); };
        variation.onClick = [this] { createVariation(); };
        clear.onClick = [this] { owner.midiEngine.clear(); owner.updateMidiClipTiming(); owner.repaint(); setStatus("MIDI cleared"); };
        close.onClick = [this] { setVisible(false); };

        keyBox.addItemList({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
        keyBox.setSelectedItemIndex(9, juce::dontSendNotification);
        scaleBox.addItemList({ "MINOR", "MAJOR", "DORIAN", "PENTATONIC MINOR" }, 1);
        scaleBox.setSelectedItemIndex(0, juce::dontSendNotification);
        barsBox.addItemList({ "2 BARS", "4 BARS", "8 BARS", "16 BARS" }, 1);
        barsBox.setSelectedItemIndex(2, juce::dontSendNotification);
        for (auto* box : { &keyBox, &scaleBox, &barsBox }) addAndMakeVisible(*box);

        status.setColour(juce::Label::textColourId, juce::Colour(0xff8edcff));
        status.setFont(juce::Font(11.0f));
        status.setText("Local MIDI AI tools - non destructive workflow", juce::dontSendNotification);
        addAndMakeVisible(status);

        setOpaque(true);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0d1116));
        g.setColour(juce::Colour(0xff29333e));
        g.drawVerticalLine(0, 0.0f, (float)getHeight());
        g.setColour(juce::Colour(0xff151b22));
        g.fillRoundedRectangle(14.0f, 48.0f, (float)getWidth() - 28.0f, 104.0f, 7.0f);
        g.setColour(juce::Colour(0xff77818c));
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText("PROMPT", 20, 54, 100, 18, juce::Justification::centredLeft);
        g.drawText("MUSICAL CONTEXT", 20, 164, 180, 18, juce::Justification::centredLeft);
    }

    void resized() override
    {
        title.setBounds(18, 10, getWidth() - 110, 30);
        close.setBounds(getWidth() - 82, 10, 66, 26);
        prompt.setBounds(20, 74, getWidth() - 40, 68);
        keyBox.setBounds(20, 186, 92, 28);
        scaleBox.setBounds(118, 186, 170, 28);
        barsBox.setBounds(294, 186, 116, 28);
        generate.setBounds(20, 232, getWidth() - 40, 34);
        chords.setBounds(20, 276, 122, 32);
        drums.setBounds(150, 276, 122, 32);
        humanize.setBounds(280, 276, 130, 32);
        variation.setBounds(20, 316, 188, 32);
        clear.setBounds(216, 316, 194, 32);
        status.setBounds(20, 360, getWidth() - 40, 40);
    }

private:
    void configureButton(juce::TextButton& b, const juce::String& text)
    {
        b.setButtonText(text);
        b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff202832));
        b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff315f7a));
        b.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        addAndMakeVisible(b);
    }

    int bars() const
    {
        static constexpr std::array<int, 4> values { 2, 4, 8, 16 };
        return values[(size_t)juce::jlimit(0, 3, barsBox.getSelectedItemIndex())];
    }

    int rootPitchClass() const { return juce::jlimit(0, 11, keyBox.getSelectedItemIndex()); }

    std::vector<int> scaleIntervals() const
    {
        switch (scaleBox.getSelectedItemIndex())
        {
            case 1: return { 0, 2, 4, 5, 7, 9, 11 };
            case 2: return { 0, 2, 3, 5, 7, 9, 10 };
            case 3: return { 0, 3, 5, 7, 10 };
            default: return { 0, 2, 3, 5, 7, 8, 10 };
        }
    }

    int pitchForDegree(int degree, int octave = 4) const
    {
        const auto scale = scaleIntervals();
        const int safeDegree = juce::jmax(0, degree);
        return juce::jlimit(0, 127, 12 * (octave + 1) + rootPitchClass()
                            + scale[(size_t)(safeDegree % (int)scale.size())]
                            + 12 * (safeDegree / (int)scale.size()));
    }

    void prepareMidi()
    {
        owner.selectMidiTrack();
        owner.midiEngine.clear();
        owner.midiClipStartSeconds = juce::jmax(0.0, owner.playheadSeconds);
        owner.midiClipLengthUserDefined = false;
    }

    void finishMidi(const juce::String& message)
    {
        owner.updateMidiClipTiming();
        owner.repaint();
        setStatus(message);
    }

    void generateMelody()
    {
        prepareMidi();
        std::mt19937 rng((unsigned int)juce::Time::getMillisecondCounter());
        std::uniform_int_distribution<int> degree(0, (int)scaleIntervals().size() - 1);
        std::uniform_int_distribution<int> vel(76, 112);
        const auto measure = MidiEngine::ticksPerMeasure(owner.timeSignatureNumerator, owner.timeSignatureDenominator);
        const auto step = MidiEngine::ticksPerQuarterNote / 2;
        const auto total = (std::int64_t)bars() * measure;
        for (std::int64_t tick = 0; tick < total; tick += step)
        {
            if ((rng() % 100) < 22) continue;
            int d = degree(rng);
            int octave = ((rng() % 100) < 18) ? 5 : 4;
            owner.midiEngine.addNote(tick, step * (((rng() % 100) < 25) ? 2 : 1), pitchForDegree(d, octave), vel(rng));
        }
        finishMidi("AI melody generated - editable MIDI");
    }

    void generateChords()
    {
        prepareMidi();
        const auto measure = MidiEngine::ticksPerMeasure(owner.timeSignatureNumerator, owner.timeSignatureDenominator);
        const std::array<int, 4> minorProgression { 0, 5, 3, 6 };
        const std::array<int, 4> majorProgression { 0, 4, 5, 3 };
        const auto& progression = scaleBox.getSelectedItemIndex() == 1 ? majorProgression : minorProgression;
        for (int bar = 0; bar < bars(); ++bar)
        {
            const int degree = progression[(size_t)(bar % 4)];
            const auto start = (std::int64_t)bar * measure;
            owner.midiEngine.addNote(start, measure, pitchForDegree(degree, 3), 92);
            owner.midiEngine.addNote(start, measure, pitchForDegree(degree + 2, 3), 84);
            owner.midiEngine.addNote(start, measure, pitchForDegree(degree + 4, 3), 88);
        }
        finishMidi("AI chord progression generated");
    }

    void generateDrums()
    {
        prepareMidi();
        const auto measure = MidiEngine::ticksPerMeasure(owner.timeSignatureNumerator, owner.timeSignatureDenominator);
        const auto eighth = MidiEngine::ticksPerQuarterNote / 2;
        for (int bar = 0; bar < bars(); ++bar)
        {
            const auto base = (std::int64_t)bar * measure;
            for (int i = 0; i < 8; ++i)
                owner.midiEngine.addNote(base + i * eighth, eighth / 2, 42, i % 2 ? 78 : 92, 10);
            owner.midiEngine.addNote(base, eighth, 36, 112, 10);
            owner.midiEngine.addNote(base + 2 * eighth, eighth, 38, 108, 10);
            owner.midiEngine.addNote(base + 4 * eighth, eighth, 36, 114, 10);
            owner.midiEngine.addNote(base + 6 * eighth, eighth, 38, 110, 10);
            if (bar % 4 == 3)
            {
                owner.midiEngine.addNote(base + 7 * eighth, eighth / 4, 45, 102, 10);
                owner.midiEngine.addNote(base + 7 * eighth + eighth / 3, eighth / 4, 47, 108, 10);
            }
        }
        finishMidi("AI drummer groove generated");
    }

    void humanizeMidi()
    {
        const auto notes = owner.midiEngine.getNotesCopy();
        if (notes.empty()) { setStatus("No MIDI notes to humanize"); return; }
        owner.midiEngine.clear();
        std::mt19937 rng((unsigned int)juce::Time::getMillisecondCounter());
        std::uniform_int_distribution<int> timing(-18, 18), velocity(-9, 9);
        for (const auto& n : notes)
            owner.midiEngine.addNote(juce::jmax<std::int64_t>(0, n.startTick + timing(rng)), n.lengthTicks,
                                     n.pitch, juce::jlimit(1, 127, (int)n.velocity + velocity(rng)), n.channel);
        finishMidi("MIDI humanized");
    }

    void createVariation()
    {
        const auto notes = owner.midiEngine.getNotesCopy();
        if (notes.empty()) { generateMelody(); return; }
        owner.midiEngine.clear();
        std::mt19937 rng((unsigned int)juce::Time::getMillisecondCounter());
        for (const auto& n : notes)
        {
            if ((rng() % 100) < 12) continue;
            int pitch = n.pitch;
            if (n.channel != 10 && (rng() % 100) < 28)
                pitch = juce::jlimit(0, 127, pitch + (((rng() % 2) == 0) ? 2 : -2));
            owner.midiEngine.addNote(n.startTick, n.lengthTicks, pitch,
                                     juce::jlimit(1, 127, (int)n.velocity + (int)(rng() % 11) - 5), n.channel);
        }
        finishMidi("AI variation created");
    }

    void setStatus(const juce::String& text) { status.setText(text, juce::dontSendNotification); }

    MainComponent& owner;
    juce::Label title, status;
    juce::TextEditor prompt;
    juce::ComboBox keyBox, scaleBox, barsBox;
    juce::TextButton generate, chords, drums, humanize, variation, clear, close;
};

class LibertyAIController final : private juce::Timer
{
public:
    explicit LibertyAIController(MainComponent& ownerIn) : owner(ownerIn), panel(ownerIn)
    {
        aiButton.setButtonText("AI MUSIC");
        aiButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff263342));
        aiButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff315f7a));
        aiButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        aiButton.setClickingTogglesState(true);
        aiButton.onClick = [this]
        {
            panel.setVisible(aiButton.getToggleState());
            if (panel.isVisible()) panel.toFront(false);
        };
        owner.addAndMakeVisible(aiButton);
        owner.addChildComponent(panel);
        startTimerHz(10);
    }

    ~LibertyAIController() override { shutdown(); }
    void shutdown() { if (stopped.exchange(true)) return; stopTimer(); panel.setVisible(false); aiButton.setVisible(false); }

private:
    void timerCallback() override
    {
        if (stopped.load()) return;
        aiButton.setBounds(1361, 8, 72, 26);
        const int width = juce::jmin(panelWidth, juce::jmax(300, owner.getWidth() - 240));
        panel.setBounds(juce::jmax(0, owner.getWidth() - width), topBarHeight, width, juce::jmax(1, owner.getHeight() - topBarHeight));
        if (panel.isVisible()) panel.toFront(false);
        aiButton.toFront(false);
    }

    MainComponent& owner;
    LibertyAIPanel panel;
    juce::TextButton aiButton;
    std::atomic<bool> stopped { false };
};

std::map<MainComponent*, std::unique_ptr<LibertyAIController>> controllers;

class Bootstrap final : private juce::Timer
{
public:
    Bootstrap() { startTimerHz(5); }
    ~Bootstrap() override { shutdown(); }
    void shutdown()
    {
        stopTimer();
        for (auto& item : controllers) if (item.second) item.second->shutdown();
        controllers.clear();
    }
private:
    void timerCallback() override
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* window = dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i)))
                if (auto* main = dynamic_cast<MainComponent*>(window->getContentComponent()))
                    if (controllers.find(main) == controllers.end())
                        controllers.emplace(main, std::make_unique<LibertyAIController>(*main));
    }
};

Bootstrap bootstrap;
}

void shutdownLibertyAIController()
{
    bootstrap.shutdown();
}
