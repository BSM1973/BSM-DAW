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

bool commitLibertyAIGeneratedClip(MainComponent& owner, bool instrumentTrack);

namespace
{
constexpr int panelWidth = 460;
constexpr int topBarHeight = 76;

juce::TextButton* findDirectButton(MainComponent& owner, const juce::String& text)
{
    for (int i = 0; i < owner.getNumChildComponents(); ++i)
        if (auto* button = dynamic_cast<juce::TextButton*>(owner.getChildComponent(i)))
            if (button->getButtonText() == text)
                return button;
    return nullptr;
}

juce::Component* findBrowserPanel(MainComponent& owner)
{
    for (int i = 0; i < owner.getNumChildComponents(); ++i)
    {
        auto* child = owner.getChildComponent(i);
        if (child == nullptr) continue;
        bool hasFiles = false, hasPlugins = false;
        for (int j = 0; j < child->getNumChildComponents(); ++j)
            if (auto* button = dynamic_cast<juce::TextButton*>(child->getChildComponent(j)))
            {
                hasFiles = hasFiles || button->getButtonText() == "FILES";
                hasPlugins = hasPlugins || button->getButtonText() == "PLUGINS";
            }
        if (hasFiles && hasPlugins) return child;
    }
    return nullptr;
}

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
        prompt.setTextToShowWhenEmpty("Exemple : accords metal en A mineur sur 8 mesures", juce::Colour(0xff727b86));
        prompt.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff11151a));
        prompt.setColour(juce::TextEditor::textColourId, juce::Colours::white);
        prompt.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff343d47));
        addAndMakeVisible(prompt);

        configureButton(generate, "GENERATE");
        configureButton(chords, "CHORDS");
        configureButton(drums, "DRUMMER");
        configureButton(bass, "BASS");
        configureButton(riff, "GUITAR RIFF");
        configureButton(solo, "SOLO");
        configureButton(humanize, "HUMANIZE");
        configureButton(variation, "VARIATION");
        configureButton(clear, "CLEAR MIDI");
        configureButton(close, "CLOSE");

        targetBox.addItem("INSTRUMENT", 1);
        targetBox.addItem("MIDI", 2);
        targetBox.setSelectedId(1, juce::dontSendNotification);
        addAndMakeVisible(targetBox);

        generate.onClick = [this] { generateFromPrompt(); };
        chords.onClick = [this] { applyPromptSettings(); generateChords(); };
        drums.onClick = [this] { applyPromptSettings(); generateDrums(); };
        bass.onClick = [this] { applyPromptSettings(); generateBass(); };
        riff.onClick = [this] { applyPromptSettings(); generateRiff(); };
        solo.onClick = [this] { applyPromptSettings(); generateSolo(); };
        humanize.onClick = [this] { humanizeMidi(); };
        variation.onClick = [this] { createVariation(); };
        clear.onClick = [this]
        {
            owner.midiEngine.clear();
            owner.updateMidiClipTiming();
            owner.repaint();
            setStatus("MIDI cleared");
        };
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
        status.setJustificationType(juce::Justification::topLeft);
        status.setText("Target: INSTRUMENT - generated clips stay in Arrange and feed the loaded AU/VST3", juce::dontSendNotification);
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
        g.drawText("GENERATORS", 20, 306, 180, 18, juce::Justification::centredLeft);
    }

    void resized() override
    {
        title.setBounds(18, 10, getWidth() - 110, 30);
        close.setBounds(getWidth() - 82, 10, 66, 26);
        prompt.setBounds(20, 74, getWidth() - 40, 68);
        keyBox.setBounds(20, 186, 92, 28);
        scaleBox.setBounds(118, 186, 176, 28);
        barsBox.setBounds(300, 186, 140, 28);
        targetBox.setBounds(20, 224, 150, 30);
        generate.setBounds(178, 224, getWidth() - 198, 36);

        const int w = (getWidth() - 52) / 3;
        chords.setBounds(20, 328, w, 32);
        drums.setBounds(26 + w, 328, w, 32);
        bass.setBounds(32 + w * 2, 328, w, 32);
        riff.setBounds(20, 368, w, 32);
        solo.setBounds(26 + w, 368, w, 32);
        variation.setBounds(32 + w * 2, 368, w, 32);
        humanize.setBounds(20, 408, (getWidth() - 46) / 2, 32);
        clear.setBounds(26 + (getWidth() - 46) / 2, 408, (getWidth() - 46) / 2, 32);
        status.setBounds(20, 452, getWidth() - 40, juce::jmax(42, getHeight() - 466));
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

    bool targetInstrument() const noexcept { return targetBox.getSelectedId() != 2; }

    juce::String normalisedPrompt() const
    {
        auto text = prompt.getText().toLowerCase();
        text = text.replaceCharacters("àâäéèêëîïôöùûüç", "aaaeeeeiioouuuc");
        return " " + text + " ";
    }

    bool promptContains(const juce::String& token) const
    {
        return normalisedPrompt().contains(token.toLowerCase());
    }

    void applyPromptSettings()
    {
        const auto text = normalisedPrompt();
        struct KeyToken { const char* token; int index; };
        const std::array<KeyToken, 24> keyTokens {{
            { " c# ", 1 }, { " db ", 1 }, { " d# ", 3 }, { " eb ", 3 },
            { " f# ", 6 }, { " gb ", 6 }, { " g# ", 8 }, { " ab ", 8 },
            { " a# ", 10 }, { " bb ", 10 },
            { " key c ", 0 }, { " tonalite c ", 0 }, { " en c ", 0 },
            { " key d ", 2 }, { " tonalite d ", 2 }, { " en d ", 2 },
            { " key e ", 4 }, { " tonalite e ", 4 }, { " en e ", 4 },
            { " key f ", 5 }, { " en f ", 5 }, { " key g ", 7 },
            { " en g ", 7 }, { " en a ", 9 }
        }};
        for (const auto& k : keyTokens)
            if (text.contains(k.token)) { keyBox.setSelectedItemIndex(k.index, juce::dontSendNotification); break; }
        if (text.contains(" a minor ") || text.contains(" a mineur ") || text.contains(" am ")) keyBox.setSelectedItemIndex(9, juce::dontSendNotification);
        if (text.contains(" b minor ") || text.contains(" b mineur ") || text.contains(" bm ")) keyBox.setSelectedItemIndex(11, juce::dontSendNotification);
        if (text.contains(" pentatonic") || text.contains(" pentatonique")) scaleBox.setSelectedItemIndex(3, juce::dontSendNotification);
        else if (text.contains(" dorian") || text.contains(" dorien")) scaleBox.setSelectedItemIndex(2, juce::dontSendNotification);
        else if (text.contains(" major") || text.contains(" majeur")) scaleBox.setSelectedItemIndex(1, juce::dontSendNotification);
        else if (text.contains(" minor") || text.contains(" mineur")) scaleBox.setSelectedItemIndex(0, juce::dontSendNotification);
        if (text.contains(" 16 bar") || text.contains(" 16 mesure")) barsBox.setSelectedItemIndex(3, juce::dontSendNotification);
        else if (text.contains(" 8 bar") || text.contains(" 8 mesure")) barsBox.setSelectedItemIndex(2, juce::dontSendNotification);
        else if (text.contains(" 4 bar") || text.contains(" 4 mesure")) barsBox.setSelectedItemIndex(1, juce::dontSendNotification);
        else if (text.contains(" 2 bar") || text.contains(" 2 mesure")) barsBox.setSelectedItemIndex(0, juce::dontSendNotification);
        if (text.contains(" piste midi") || text.contains(" midi track")) targetBox.setSelectedId(2, juce::dontSendNotification);
        if (text.contains(" instrument") || text.contains(" synth") || text.contains(" piano") || text.contains(" guitare") || text.contains(" guitar"))
            targetBox.setSelectedId(1, juce::dontSendNotification);
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

    std::array<int, 4> progression() const
    {
        const auto text = normalisedPrompt();
        if (text.contains(" blues")) return { 0, 3, 0, 4 };
        if (text.contains(" metal") || text.contains(" hard rock")) return { 0, 5, 3, 6 };
        if (scaleBox.getSelectedItemIndex() == 1) return { 0, 4, 5, 3 };
        return { 0, 5, 3, 6 };
    }

    void prepareMidi()
    {
        owner.midiEngine.clear();
        owner.midiEngine.clearUndoHistory();
        owner.midiClipStartSeconds = juce::jmax(0.0, owner.playheadSeconds);
        owner.midiClipLengthUserDefined = true;
        const double beat = 60.0 / juce::jmax(1.0, owner.tempoBpm)
                          * (4.0 / (double)juce::jmax(1, owner.timeSignatureDenominator));
        owner.midiClipLengthSeconds = beat * (double)juce::jmax(1, owner.timeSignatureNumerator) * (double)bars();
    }

    void finishMidi(const juce::String& message)
    {
        owner.updateMidiClipTiming();
        const bool instrument = targetInstrument();
        const auto count = owner.midiEngine.getNumNotes();
        const bool committed = commitLibertyAIGeneratedClip(owner, instrument);
        if (!committed)
        {
            setStatus("AI generated " + juce::String((int)count) + " notes but clip commit failed");
            return;
        }
        owner.repaint();
        setStatus(message + " - " + juce::String((int)count) + " notes - target " + (instrument ? "INSTRUMENT" : "MIDI"));
    }

    void generateFromPrompt()
    {
        applyPromptSettings();
        const auto text = normalisedPrompt();
        if (text.contains(" chord") || text.contains(" accord")) { generateChords(); return; }
        if (text.contains(" drum") || text.contains(" batterie") || text.contains(" beat")) { generateDrums(); return; }
        if (text.contains(" bass") || text.contains(" basse")) { generateBass(); return; }
        if (text.contains(" riff") || text.contains(" guitar") || text.contains(" guitare")) { generateRiff(); return; }
        if (text.contains(" solo") || text.contains(" lead")) { generateSolo(); return; }
        generateMelody();
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
        const bool sparse = promptContains("ambient") || promptContains("slow");
        for (std::int64_t tick = 0; tick < total; tick += step)
        {
            if ((rng() % 100) < (sparse ? 48 : 20)) continue;
            const int d = degree(rng);
            const int octave = ((rng() % 100) < 16) ? 5 : 4;
            owner.midiEngine.addNote(tick, step * (((rng() % 100) < 25) ? 2 : 1), pitchForDegree(d, octave), vel(rng));
        }
        finishMidi("AI melody generated");
    }

    void generateChords()
    {
        prepareMidi();
        const auto measure = MidiEngine::ticksPerMeasure(owner.timeSignatureNumerator, owner.timeSignatureDenominator);
        const auto prog = progression();
        const bool power = promptContains("metal") || promptContains("power chord");
        for (int bar = 0; bar < bars(); ++bar)
        {
            const int degree = prog[(size_t)(bar % 4)];
            const auto start = (std::int64_t)bar * measure;
            owner.midiEngine.addNote(start, measure, pitchForDegree(degree, 3), 96);
            if (power)
            {
                owner.midiEngine.addNote(start, measure, pitchForDegree(degree + 4, 3), 90);
                owner.midiEngine.addNote(start, measure, pitchForDegree(degree, 4), 86);
            }
            else
            {
                owner.midiEngine.addNote(start, measure, pitchForDegree(degree + 2, 3), 84);
                owner.midiEngine.addNote(start, measure, pitchForDegree(degree + 4, 3), 88);
            }
        }
        finishMidi(power ? "AI power-chord progression generated" : "AI chord progression generated");
    }

    void generateDrums()
    {
        prepareMidi();
        const auto measure = MidiEngine::ticksPerMeasure(owner.timeSignatureNumerator, owner.timeSignatureDenominator);
        const auto eighth = MidiEngine::ticksPerQuarterNote / 2;
        const bool metal = promptContains("metal") || promptContains("double kick");
        for (int bar = 0; bar < bars(); ++bar)
        {
            const auto base = (std::int64_t)bar * measure;
            for (int i = 0; i < 8; ++i)
                owner.midiEngine.addNote(base + i * eighth, eighth / 2, metal ? 51 : 42, i % 2 ? 78 : 94, 10);
            owner.midiEngine.addNote(base, eighth / 2, 36, 116, 10);
            owner.midiEngine.addNote(base + 2 * eighth, eighth / 2, 38, 110, 10);
            owner.midiEngine.addNote(base + 4 * eighth, eighth / 2, 36, 116, 10);
            owner.midiEngine.addNote(base + 6 * eighth, eighth / 2, 38, 110, 10);
            if (metal)
                for (int i = 1; i < 8; i += 2)
                    owner.midiEngine.addNote(base + i * eighth, eighth / 3, 36, 102, 10);
            if (bar % 4 == 3)
            {
                owner.midiEngine.addNote(base + 7 * eighth, eighth / 4, 45, 102, 10);
                owner.midiEngine.addNote(base + 7 * eighth + eighth / 3, eighth / 4, 47, 108, 10);
            }
        }
        finishMidi(metal ? "AI metal drummer generated" : "AI drummer groove generated");
    }

    void generateBass()
    {
        prepareMidi();
        const auto measure = MidiEngine::ticksPerMeasure(owner.timeSignatureNumerator, owner.timeSignatureDenominator);
        const auto quarter = MidiEngine::ticksPerQuarterNote;
        const auto prog = progression();
        const bool busy = promptContains("funk") || promptContains("busy") || promptContains("active");
        for (int bar = 0; bar < bars(); ++bar)
        {
            const int degree = prog[(size_t)(bar % 4)];
            const auto base = (std::int64_t)bar * measure;
            const int root = pitchForDegree(degree, 2);
            const int fifth = pitchForDegree(degree + 4, 2);
            for (int beat = 0; beat < juce::jmax(1, owner.timeSignatureNumerator); ++beat)
            {
                const auto start = base + (std::int64_t)beat * quarter;
                owner.midiEngine.addNote(start, busy ? quarter / 2 : quarter, beat % 2 ? fifth : root, beat == 0 ? 110 : 94);
                if (busy) owner.midiEngine.addNote(start + quarter / 2, quarter / 2, root, 86);
            }
        }
        finishMidi("AI bass line generated");
    }

    void generateRiff()
    {
        prepareMidi();
        const auto measure = MidiEngine::ticksPerMeasure(owner.timeSignatureNumerator, owner.timeSignatureDenominator);
        const auto sixteenth = MidiEngine::ticksPerQuarterNote / 4;
        const bool metal = promptContains("metal") || promptContains("heavy") || promptContains("hard rock");
        for (int bar = 0; bar < bars(); ++bar)
        {
            const auto base = (std::int64_t)bar * measure;
            for (int i = 0; i < 16; ++i)
            {
                if (!metal && (i % 4 == 3)) continue;
                int degree = 0;
                if (i == 6 || i == 14) degree = 3;
                else if (i == 10) degree = 5;
                owner.midiEngine.addNote(base + (std::int64_t)i * sixteenth,
                                         metal ? sixteenth : sixteenth * 2,
                                         pitchForDegree(degree, 2),
                                         (i % 4 == 0) ? 116 : 92);
            }
        }
        finishMidi(metal ? "AI heavy guitar riff generated" : "AI guitar riff generated");
    }

    void generateSolo()
    {
        prepareMidi();
        std::mt19937 rng((unsigned int)juce::Time::getMillisecondCounter());
        const auto measure = MidiEngine::ticksPerMeasure(owner.timeSignatureNumerator, owner.timeSignatureDenominator);
        const auto step = MidiEngine::ticksPerQuarterNote / 2;
        const auto total = (std::int64_t)bars() * measure;
        const auto scale = scaleIntervals();
        int degree = 0;
        for (std::int64_t tick = 0; tick < total; tick += step)
        {
            int movement = (int)(rng() % 3) - 1;
            degree = juce::jlimit(0, (int)scale.size() * 2 - 1, degree + movement);
            if ((rng() % 100) < 12) continue;
            owner.midiEngine.addNote(tick, ((rng() % 100) < 28) ? step * 2 : step,
                                     pitchForDegree(degree, 4), 88 + (int)(rng() % 28));
        }
        finishMidi("AI solo generated");
    }

    void humanizeMidi()
    {
        const auto notes = owner.midiEngine.getNotesCopy();
        if (notes.empty()) { setStatus("No active clip notes to humanize"); return; }
        owner.midiEngine.clear();
        std::mt19937 rng((unsigned int)juce::Time::getMillisecondCounter());
        std::uniform_int_distribution<int> timing(-18, 18), velocity(-9, 9);
        for (const auto& n : notes)
            owner.midiEngine.addNote(juce::jmax<std::int64_t>(0, n.startTick + timing(rng)), n.lengthTicks,
                                     n.pitch, juce::jlimit(1, 127, (int)n.velocity + velocity(rng)), n.channel);
        owner.updateMidiClipTiming();
        owner.repaint();
        setStatus("Active clip humanized");
    }

    void createVariation()
    {
        const auto notes = owner.midiEngine.getNotesCopy();
        if (notes.empty()) { applyPromptSettings(); generateMelody(); return; }
        owner.midiEngine.clear();
        std::mt19937 rng((unsigned int)juce::Time::getMillisecondCounter());
        for (const auto& n : notes)
        {
            if ((rng() % 100) < 10) continue;
            int pitch = n.pitch;
            if (n.channel != 10 && (rng() % 100) < 24)
                pitch = juce::jlimit(0, 127, pitch + (((rng() % 2) == 0) ? 2 : -2));
            owner.midiEngine.addNote(n.startTick, n.lengthTicks, pitch,
                                     juce::jlimit(1, 127, (int)n.velocity + (int)(rng() % 11) - 5), n.channel);
        }
        owner.updateMidiClipTiming();
        owner.repaint();
        setStatus("AI variation applied to active clip");
    }

    void setStatus(const juce::String& text) { status.setText(text, juce::dontSendNotification); }

    MainComponent& owner;
    juce::Label title, status;
    juce::TextEditor prompt;
    juce::ComboBox keyBox, scaleBox, barsBox, targetBox;
    juce::TextButton generate, chords, drums, bass, riff, solo, humanize, variation, clear, close;
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

        int buttonX = 975;
        if (auto* browser = findDirectButton(owner, "BROWSER"))
            if (browser->getWidth() > 0)
                buttonX = juce::jmin(buttonX, browser->getX() - 82);
        buttonX = juce::jmax(8, buttonX);
        aiButton.setBounds(buttonX, 8, 74, 26);

        int rightEdge = owner.getWidth();
        if (auto* browserPanel = findBrowserPanel(owner))
            if (browserPanel->isVisible() && browserPanel->getWidth() > 0)
                rightEdge = browserPanel->getX();

        const int available = juce::jmax(300, rightEdge - 220);
        const int width = juce::jmin(panelWidth, available);
        panel.setBounds(juce::jmax(0, rightEdge - width), topBarHeight, width, juce::jmax(1, owner.getHeight() - topBarHeight));

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
