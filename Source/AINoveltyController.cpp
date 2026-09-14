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
bool renderLibertyAIActiveInstrumentToAudio(MainComponent& owner, juce::String& resultMessage);

namespace
{
class AILab final : public juce::Component
{
public:
    explicit AILab(MainComponent& ownerIn) : owner(ownerIn)
    {
        title.setText("AI LAB", juce::dontSendNotification);
        title.setColour(juce::Label::textColourId, juce::Colour(0xff8edcff));
        title.setFont(juce::Font(12.0f, juce::Font::bold));
        addAndMakeVisible(title);

        addButton(renderAudio, "RENDER TO AUDIO", [this] { doRender(); });
        addButton(arpeggiate, "ARPEGGIATE", [this] { doArpeggiate(); });
        addButton(counter, "COUNTER MELODY", [this] { doCounterMelody(); });
        addButton(groove, "GROOVE MUTATOR", [this] { doGrooveMutator(); });
        addButton(fill, "SMART FILL", [this] { doSmartFill(); });
        addButton(voicing, "SMART VOICING", [this] { doSmartVoicing(); });
        addButton(energyUp, "ENERGY UP", [this] { doEnergy(1); });
        addButton(energyDown, "ENERGY DOWN", [this] { doEnergy(-1); });
        addButton(pack, "VARIATION PACK", [this] { doVariationPack(); });

        status.setColour(juce::Label::textColourId, juce::Colour(0xffb8c3cf));
        status.setFont(juce::Font(10.0f));
        status.setJustificationType(juce::Justification::topLeft);
        status.setText("Creative AI tools use the active MIDI/Instrument clip.", juce::dontSendNotification);
        addAndMakeVisible(status);
        setOpaque(true);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff11161c));
        g.setColour(juce::Colour(0xff2b3540));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 6.0f, 1.0f);
    }

    void resized() override
    {
        title.setBounds(10, 6, getWidth() - 20, 20);
        const int gap = 6;
        const int x0 = 10;
        const int w = (getWidth() - 20 - gap * 2) / 3;
        const int h = 28;
        int y = 32;
        renderAudio.setBounds(x0, y, w * 3 + gap * 2, 32);
        y += 40;
        arpeggiate.setBounds(x0, y, w, h);
        counter.setBounds(x0 + w + gap, y, w, h);
        groove.setBounds(x0 + 2 * (w + gap), y, w, h);
        y += h + gap;
        fill.setBounds(x0, y, w, h);
        voicing.setBounds(x0 + w + gap, y, w, h);
        pack.setBounds(x0 + 2 * (w + gap), y, w, h);
        y += h + gap;
        energyUp.setBounds(x0, y, (getWidth() - 20 - gap) / 2, h);
        energyDown.setBounds(x0 + (getWidth() - 20 - gap) / 2 + gap, y, (getWidth() - 20 - gap) / 2, h);
        status.setBounds(10, y + h + 8, getWidth() - 20, juce::jmax(30, getHeight() - (y + h + 16)));
    }

private:
    template <typename Fn>
    void addButton(juce::TextButton& button, const juce::String& text, Fn&& fn)
    {
        button.setButtonText(text);
        button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff202832));
        button.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        button.onClick = std::forward<Fn>(fn);
        addAndMakeVisible(button);
    }

    std::vector<MidiEngine::NoteEvent> activeNotes() const { return owner.midiEngine.getNotesCopy(); }

    void replaceActive(const std::vector<MidiEngine::NoteEvent>& notes, const juce::String& message)
    {
        if (notes.empty()) { setStatus("No active notes."); return; }
        owner.midiEngine.clear();
        for (const auto& n : notes)
            owner.midiEngine.addNote(n.startTick, n.lengthTicks, n.pitch, n.velocity, n.channel);
        owner.updateMidiClipTiming();
        owner.repaint();
        setStatus(message + " - " + juce::String((int)notes.size()) + " notes");
    }

    void doRender()
    {
        juce::String message;
        const bool ok = renderLibertyAIActiveInstrumentToAudio(owner, message);
        setStatus((ok ? "DONE: " : "ERROR: ") + message);
    }

    void doArpeggiate()
    {
        const auto source = activeNotes();
        if (source.empty()) { setStatus("No active clip to arpeggiate."); return; }
        std::vector<int> pitches;
        for (const auto& n : source)
            if (n.channel != 10) pitches.push_back((int)n.pitch);
        std::sort(pitches.begin(), pitches.end());
        pitches.erase(std::unique(pitches.begin(), pitches.end()), pitches.end());
        if (pitches.empty()) { setStatus("Arpeggiate is for melodic clips."); return; }

        const auto step = MidiEngine::ticksPerQuarterNote / 2;
        auto total = owner.midiEngine.getLengthTicks();
        if (total <= 0) total = MidiEngine::ticksPerQuarterNote * 4;
        std::vector<MidiEngine::NoteEvent> out;
        int index = 0;
        bool up = true;
        for (std::int64_t tick = 0; tick < total; tick += step)
        {
            MidiEngine::NoteEvent n;
            n.startTick = tick;
            n.lengthTicks = (std::int64_t)(step * 0.82);
            n.pitch = (std::uint8_t)pitches[(size_t)index];
            n.velocity = (std::uint8_t)(index == 0 ? 108 : 92);
            n.channel = 1;
            out.push_back(n);
            if (pitches.size() > 1)
            {
                if (up && index >= (int)pitches.size() - 1) { up = false; --index; }
                else if (!up && index <= 0) { up = true; ++index; }
                else index += up ? 1 : -1;
            }
        }
        replaceActive(out, "AI arpeggio applied");
    }

    void doCounterMelody()
    {
        const auto source = activeNotes();
        if (source.empty()) { setStatus("No active clip for counter melody."); return; }
        std::vector<MidiEngine::NoteEvent> counterNotes;
        int emitted = 0;
        for (size_t i = 0; i < source.size(); ++i)
        {
            const auto& s = source[i];
            if (s.channel == 10 || (i % 2) != 0) continue;
            MidiEngine::NoteEvent n = s;
            n.startTick += MidiEngine::ticksPerQuarterNote / 2;
            n.lengthTicks = juce::jmax<std::int64_t>(MidiEngine::ticksPerQuarterNote / 3, s.lengthTicks / 2);
            const int direction = (emitted % 4 < 2) ? 7 : -5;
            n.pitch = (std::uint8_t)juce::jlimit(36, 108, (int)s.pitch + direction);
            n.velocity = (std::uint8_t)juce::jlimit(50, 100, (int)s.velocity - 12);
            counterNotes.push_back(n);
            ++emitted;
        }
        if (counterNotes.empty()) { setStatus("Counter melody needs melodic notes."); return; }

        owner.midiEngine.clear();
        for (const auto& n : counterNotes)
            owner.midiEngine.addNote(n.startTick, n.lengthTicks, n.pitch, n.velocity, n.channel);
        owner.midiClipStartSeconds += owner.midiClipLengthSeconds;
        owner.updateMidiClipTiming();
        if (commitLibertyAIGeneratedClip(owner, true))
            setStatus("New counter-melody Instrument clip created");
        else
            setStatus("Counter-melody clip commit failed");
    }

    void doGrooveMutator()
    {
        auto notes = activeNotes();
        if (notes.empty()) { setStatus("No active clip for groove mutation."); return; }
        const auto eighth = MidiEngine::ticksPerQuarterNote / 2;
        for (auto& n : notes)
        {
            const auto slot = n.startTick / eighth;
            if ((slot % 2) == 1)
                n.startTick += 55;
            n.velocity = (std::uint8_t)juce::jlimit(1, 127, (int)n.velocity + ((slot % 4 == 0) ? 8 : -3));
        }
        replaceActive(notes, "AI groove mutated");
    }

    void doSmartFill()
    {
        auto notes = activeNotes();
        if (notes.empty()) { setStatus("No active clip for Smart Fill."); return; }
        const auto measure = MidiEngine::ticksPerMeasure(owner.timeSignatureNumerator, owner.timeSignatureDenominator);
        const bool drums = std::any_of(notes.begin(), notes.end(), [](const auto& n) { return n.channel == 10; });
        const auto total = juce::jmax<std::int64_t>(measure, owner.midiEngine.getLengthTicks());
        for (std::int64_t barEnd = measure * 4; barEnd <= total; barEnd += measure * 4)
        {
            const auto start = barEnd - MidiEngine::ticksPerQuarterNote;
            if (drums)
            {
                const int drumPitches[] = { 45, 47, 48, 50 };
                for (int i = 0; i < 4; ++i)
                {
                    MidiEngine::NoteEvent n;
                    n.startTick = start + i * (MidiEngine::ticksPerQuarterNote / 4);
                    n.lengthTicks = MidiEngine::ticksPerQuarterNote / 6;
                    n.pitch = (std::uint8_t)drumPitches[i];
                    n.velocity = (std::uint8_t)(96 + i * 7);
                    n.channel = 10;
                    notes.push_back(n);
                }
            }
            else
            {
                auto last = *std::max_element(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.startTick < b.startTick; });
                for (int i = 0; i < 4; ++i)
                {
                    auto n = last;
                    n.startTick = start + i * (MidiEngine::ticksPerQuarterNote / 4);
                    n.lengthTicks = MidiEngine::ticksPerQuarterNote / 5;
                    n.pitch = (std::uint8_t)juce::jlimit(0, 127, (int)last.pitch + i);
                    n.velocity = (std::uint8_t)juce::jlimit(1, 127, 88 + i * 6);
                    notes.push_back(n);
                }
            }
        }
        replaceActive(notes, "AI Smart Fill added");
    }

    void doSmartVoicing()
    {
        auto notes = activeNotes();
        if (notes.empty()) { setStatus("No active clip for voicing."); return; }
        std::map<std::int64_t, std::vector<size_t>> groups;
        for (size_t i = 0; i < notes.size(); ++i)
            if (notes[i].channel != 10) groups[notes[i].startTick].push_back(i);
        int changed = 0;
        for (auto& pair : groups)
        {
            auto& ids = pair.second;
            if (ids.size() < 3) continue;
            std::sort(ids.begin(), ids.end(), [&](size_t a, size_t b) { return notes[a].pitch < notes[b].pitch; });
            auto& low = notes[ids.front()];
            low.pitch = (std::uint8_t)juce::jlimit(0, 127, (int)low.pitch + 12);
            if (ids.size() >= 4)
            {
                auto& high = notes[ids.back()];
                high.pitch = (std::uint8_t)juce::jlimit(0, 127, (int)high.pitch - 12);
            }
            ++changed;
        }
        if (changed == 0) { setStatus("Smart Voicing needs chordal material."); return; }
        replaceActive(notes, "AI voicing redistributed");
    }

    void doEnergy(int direction)
    {
        auto notes = activeNotes();
        if (notes.empty()) { setStatus("No active clip for Energy."); return; }
        std::vector<MidiEngine::NoteEvent> out;
        for (size_t i = 0; i < notes.size(); ++i)
        {
            auto n = notes[i];
            n.velocity = (std::uint8_t)juce::jlimit(1, 127, (int)n.velocity + direction * 12);
            if (direction < 0 && (i % 5) == 4) continue;
            out.push_back(n);
            if (direction > 0 && n.channel != 10 && (i % 6) == 0)
            {
                auto octave = n;
                octave.pitch = (std::uint8_t)juce::jlimit(0, 127, (int)n.pitch + 12);
                octave.velocity = (std::uint8_t)juce::jlimit(1, 127, (int)n.velocity - 18);
                out.push_back(octave);
            }
        }
        replaceActive(out, direction > 0 ? "AI energy increased" : "AI energy reduced");
    }

    void doVariationPack()
    {
        const auto original = activeNotes();
        if (original.empty()) { setStatus("No active clip for Variation Pack."); return; }
        const double baseStart = owner.midiClipStartSeconds;
        const double length = juce::jmax(0.25, owner.midiClipLengthSeconds);
        std::mt19937 rng((unsigned int)juce::Time::getMillisecondCounter());

        for (int variant = 0; variant < 3; ++variant)
        {
            std::vector<MidiEngine::NoteEvent> v;
            for (size_t i = 0; i < original.size(); ++i)
            {
                auto n = original[i];
                const int removeChance = 5 + variant * 5;
                if ((int)(rng() % 100) < removeChance) continue;
                if (n.channel != 10 && (int)(rng() % 100) < 14 + variant * 8)
                    n.pitch = (std::uint8_t)juce::jlimit(0, 127, (int)n.pitch + ((rng() % 2) ? 2 : -2));
                n.velocity = (std::uint8_t)juce::jlimit(1, 127, (int)n.velocity + (int)(rng() % 15) - 7);
                v.push_back(n);
            }
            owner.midiEngine.clear();
            for (const auto& n : v)
                owner.midiEngine.addNote(n.startTick, n.lengthTicks, n.pitch, n.velocity, n.channel);
            owner.midiClipStartSeconds = baseStart + length * (variant + 1);
            owner.midiClipLengthSeconds = length;
            owner.midiClipLengthUserDefined = true;
            owner.updateMidiClipTiming();
            commitLibertyAIGeneratedClip(owner, true);
        }
        owner.repaint();
        setStatus("Variation Pack A/B/C created as 3 adjacent Instrument clips");
    }

    void setStatus(const juce::String& text) { status.setText(text, juce::dontSendNotification); }

    MainComponent& owner;
    juce::Label title, status;
    juce::TextButton renderAudio, arpeggiate, counter, groove, fill, voicing, energyUp, energyDown, pack;
};

class AINoveltyController final : private juce::Timer
{
public:
    explicit AINoveltyController(MainComponent& ownerIn) : owner(ownerIn) { startTimerHz(8); }
    ~AINoveltyController() override { shutdown(); }
    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
        lab.reset();
    }

private:
    juce::Component* findAIPanel()
    {
        for (int i = 0; i < owner.getNumChildComponents(); ++i)
        {
            auto* c = owner.getChildComponent(i);
            if (c == nullptr) continue;
            for (int j = 0; j < c->getNumChildComponents(); ++j)
                if (auto* label = dynamic_cast<juce::Label*>(c->getChildComponent(j)))
                    if (label->getText() == "LIBERTY AI MUSIC") return c;
        }
        return nullptr;
    }

    void timerCallback() override
    {
        if (stopped.load()) return;
        auto* panel = findAIPanel();
        if (panel == nullptr) return;
        if (lab == nullptr || lab->getParentComponent() != panel)
        {
            lab.reset();
            lab = std::make_unique<AILab>(owner);
            panel->addAndMakeVisible(*lab);
        }

        // Reserve the lower part of the AI panel for AI LAB and keep legacy status above it.
        for (int i = 0; i < panel->getNumChildComponents(); ++i)
            if (auto* label = dynamic_cast<juce::Label*>(panel->getChildComponent(i)))
                if (label->getText() != "LIBERTY AI MUSIC" && label != nullptr && label != findOwnStatus())
                    if (label->getY() >= 440) label->setBounds(20, 450, panel->getWidth() - 40, 44);

        lab->setBounds(10, 502, juce::jmax(1, panel->getWidth() - 20), juce::jmax(260, panel->getHeight() - 512));
        lab->toFront(false);
    }

    juce::Label* findOwnStatus() const { return nullptr; }

    MainComponent& owner;
    std::unique_ptr<AILab> lab;
    std::atomic<bool> stopped { false };
};

std::map<MainComponent*, std::unique_ptr<AINoveltyController>> controllers;

class Bootstrap final : private juce::Timer
{
public:
    Bootstrap() { startTimerHz(5); }
    ~Bootstrap() override { shutdown(); }
    void shutdown()
    {
        stopTimer();
        for (auto& p : controllers) if (p.second) p.second->shutdown();
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
                        controllers.emplace(main, std::make_unique<AINoveltyController>(*main));
    }
};

Bootstrap bootstrap;
}

void shutdownLibertyAINoveltyController()
{
    bootstrap.shutdown();
}
