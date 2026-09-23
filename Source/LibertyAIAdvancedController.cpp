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
#include <vector>

bool commitLibertyAIGeneratedClip(MainComponent& owner, bool instrumentTrack);

namespace
{
juce::Component* findAIPanel(MainComponent& owner)
{
    for (int i = 0; i < owner.getNumChildComponents(); ++i)
    {
        auto* child = owner.getChildComponent(i);
        if (child == nullptr) continue;
        for (int j = 0; j < child->getNumChildComponents(); ++j)
            if (auto* label = dynamic_cast<juce::Label*>(child->getChildComponent(j)))
                if (label->getText() == "LIBERTY AI MUSIC")
                    return child;
    }
    return nullptr;
}

juce::Label* findStatusLabel(juce::Component& panel)
{
    for (int i = 0; i < panel.getNumChildComponents(); ++i)
        if (auto* label = dynamic_cast<juce::Label*>(panel.getChildComponent(i)))
            if (label->getText().containsIgnoreCase("Target:") || label->getText().containsIgnoreCase("generated"))
                return label;
    return nullptr;
}

class AdvancedAIControls final : public juce::Component
{
public:
    AdvancedAIControls(MainComponent& ownerIn, juce::Component& panelIn)
        : owner(ownerIn), panel(panelIn)
    {
        setup(harmonize, "HARMONIZE");
        setup(chordTrack, "CHORD TRACK");
        setup(varA, "VAR A");
        setup(varB, "VAR B");
        setup(varC, "VAR C");

        harmonize.onClick = [this] { makeHarmony(); };
        chordTrack.onClick = [this] { makeChordTrack(); };
        varA.onClick = [this] { makeVariation(1); };
        varB.onClick = [this] { makeVariation(2); };
        varC.onClick = [this] { makeVariation(3); };

        panel.addAndMakeVisible(*this);
        setBounds(20, 452, juce::jmax(280, panel.getWidth() - 40), 76);
        resized();
        moveStatusBelow();
    }

    void resized() override
    {
        const int w = juce::jmax(80, (getWidth() - 12) / 2);
        harmonize.setBounds(0, 0, w, 30);
        chordTrack.setBounds(w + 12, 0, getWidth() - w - 12, 30);
        const int vw = juce::jmax(60, (getWidth() - 12) / 3);
        varA.setBounds(0, 40, vw, 30);
        varB.setBounds(vw + 6, 40, vw, 30);
        varC.setBounds((vw + 6) * 2, 40, getWidth() - (vw + 6) * 2, 30);
    }

    void updateBounds()
    {
        setBounds(20, 452, juce::jmax(280, panel.getWidth() - 40), 76);
        moveStatusBelow();
    }

private:
    void setup(juce::TextButton& b, const juce::String& text)
    {
        b.setButtonText(text);
        b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff202832));
        b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff315f7a));
        b.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        addAndMakeVisible(b);
    }

    void moveStatusBelow()
    {
        if (auto* status = findStatusLabel(panel))
            status->setBounds(20, 538, juce::jmax(100, panel.getWidth() - 40), juce::jmax(42, panel.getHeight() - 552));
    }

    void replaceMidi(const std::vector<MidiEngine::NoteEvent>& notes)
    {
        owner.midiEngine.clear();
        owner.midiEngine.clearUndoHistory();
        for (const auto& n : notes)
            owner.midiEngine.addNote(n.startTick, n.lengthTicks, n.pitch, n.velocity, n.channel);
        owner.midiEngine.clearUndoHistory();
        owner.updateMidiClipTiming();
    }

    void makeHarmony()
    {
        const auto source = owner.midiEngine.getNotesCopy();
        if (source.empty()) return;

        auto result = source;
        for (const auto& n : source)
        {
            if (n.channel == 10) continue;
            auto h = n;
            const int interval = ((int)n.pitch % 12 == 1 || (int)n.pitch % 12 == 3 ||
                                  (int)n.pitch % 12 == 6 || (int)n.pitch % 12 == 8 ||
                                  (int)n.pitch % 12 == 10) ? 3 : 4;
            h.pitch = (std::uint8_t)juce::jlimit(0, 127, (int)n.pitch + interval);
            h.velocity = (std::uint8_t)juce::jlimit(1, 127, (int)n.velocity - 12);
            result.push_back(h);
        }

        replaceMidi(result);
        commitLibertyAIGeneratedClip(owner, true);
    }

    void makeChordTrack()
    {
        const auto source = owner.midiEngine.getNotesCopy();
        if (source.empty()) return;

        const auto measure = MidiEngine::ticksPerMeasure(owner.timeSignatureNumerator,
                                                         owner.timeSignatureDenominator);
        std::int64_t maxTick = 0;
        for (const auto& n : source) maxTick = juce::jmax(maxTick, n.startTick + n.lengthTicks);
        const int measures = juce::jmax(1, (int)std::ceil((double)maxTick / (double)measure));

        std::vector<MidiEngine::NoteEvent> chords;
        for (int m = 0; m < measures; ++m)
        {
            std::array<int, 12> counts {};
            const auto a = (std::int64_t)m * measure;
            const auto b = a + measure;
            for (const auto& n : source)
                if (n.channel != 10 && n.startTick < b && n.startTick + n.lengthTicks > a)
                    counts[(size_t)(n.pitch % 12)]++;

            int root = 0;
            for (int pc = 1; pc < 12; ++pc)
                if (counts[(size_t)pc] > counts[(size_t)root]) root = pc;

            const int base = 48 + root;
            for (int semis : { 0, 3, 7 })
            {
                MidiEngine::NoteEvent n;
                n.startTick = a;
                n.lengthTicks = measure;
                n.pitch = (std::uint8_t)juce::jlimit(0, 127, base + semis);
                n.velocity = (std::uint8_t)(semis == 0 ? 92 : 80);
                n.channel = 1;
                chords.push_back(n);
            }
        }

        replaceMidi(chords);
        // Chord Track is deliberately placed on the MIDI track so the source
        // Instrument clip can remain a separate musical part.
        commitLibertyAIGeneratedClip(owner, false);
    }

    void makeVariation(int flavour)
    {
        const auto source = owner.midiEngine.getNotesCopy();
        if (source.empty()) return;

        std::mt19937 rng((unsigned int)juce::Time::getMillisecondCounter() + (unsigned int)flavour * 7919u);
        std::vector<MidiEngine::NoteEvent> out;
        out.reserve(source.size() + source.size() / 3);

        for (const auto& n : source)
        {
            if ((rng() % 100) < (flavour == 1 ? 6 : flavour == 2 ? 12 : 18)) continue;
            auto a = n;
            if (a.channel != 10 && (rng() % 100) < (20 + flavour * 10))
                a.pitch = (std::uint8_t)juce::jlimit(0, 127, (int)a.pitch + ((rng() % 2) ? 2 : -2));
            a.velocity = (std::uint8_t)juce::jlimit(1, 127, (int)a.velocity + (int)(rng() % 13) - 6);
            if (flavour == 3 && a.channel != 10 && (rng() % 100) < 22)
                a.lengthTicks = juce::jmax<std::int64_t>(MidiEngine::ticksPerQuarterNote / 8, a.lengthTicks / 2);
            out.push_back(a);
        }

        replaceMidi(out);
        commitLibertyAIGeneratedClip(owner, true);
    }

    MainComponent& owner;
    juce::Component& panel;
    juce::TextButton harmonize, chordTrack, varA, varB, varC;
};

class Controller final : private juce::Timer
{
public:
    explicit Controller(MainComponent& ownerIn) : owner(ownerIn) { startTimerHz(5); }
    ~Controller() override { shutdown(); }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
        controls.reset();
        panel = nullptr;
    }

private:
    void timerCallback() override
    {
        if (stopped.load()) return;
        auto* found = findAIPanel(owner);
        if (found == nullptr) return;
        if (panel != found || controls == nullptr)
        {
            controls.reset();
            panel = found;
            controls = std::make_unique<AdvancedAIControls>(owner, *panel);
        }
        if (controls) controls->updateBounds();
    }

    MainComponent& owner;
    juce::Component* panel = nullptr;
    std::unique_ptr<AdvancedAIControls> controls;
    std::atomic<bool> stopped { false };
};

std::map<MainComponent*, std::unique_ptr<Controller>> controllers;

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
                        controllers.emplace(main, std::make_unique<Controller>(*main));
    }
};

Bootstrap bootstrap;
}

void shutdownLibertyAIAdvancedController()
{
    bootstrap.shutdown();
}
