#define private public
#include "MainComponent.h"
#undef private

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <map>
#include <memory>

double getLibertyTimelinePixelsPerSecond() noexcept;
int getLibertyTrackRowHeight() noexcept;

namespace
{
constexpr int colourDefault = 0;
constexpr int totalTracks = AudioEngine::maxAudioTracks + 2;
constexpr int midiTrackIndex = AudioEngine::maxAudioTracks;
constexpr int instrumentTrackIndex = AudioEngine::maxAudioTracks + 1;
constexpr int headerW = 210;
constexpr int rulerH = 32;
constexpr std::array<juce::uint32, 9> palette {
    0xff31506a, 0xff3b82f6, 0xff22c55e, 0xffeab308, 0xfff97316,
    0xffef4444, 0xffa855f7, 0xffec4899, 0xff14b8a6
};
constexpr const char* colourNames[] = {
    "Defaut", "Bleu", "Vert", "Jaune", "Orange", "Rouge", "Violet", "Rose", "Turquoise"
};

std::array<int, totalTracks> colourIds {};
std::array<juce::String, totalTracks> trackNames {};

juce::String defaultTrackName(int track)
{
    if (track >= 0 && track < AudioEngine::maxAudioTracks) return "Audio " + juce::String(track + 1);
    if (track == midiTrackIndex) return "MIDI 1";
    if (track == instrumentTrackIndex) return "Instrument 1";
    return {};
}

juce::String effectiveTrackName(int track)
{
    if (track < 0 || track >= totalTracks) return {};
    return trackNames[(size_t)track].isNotEmpty() ? trackNames[(size_t)track] : defaultTrackName(track);
}

juce::Colour colourForId(int id)
{
    if (id <= 0 || id >= (int)palette.size()) return juce::Colour(palette[0]);
    return juce::Colour(palette[(size_t)id]);
}

class TrackColourController final : public juce::Component, private juce::Timer
{
public:
    explicit TrackColourController(MainComponent& ownerIn) : owner(ownerIn), listener(*this)
    {
        setInterceptsMouseClicks(false, true);
        owner.addAndMakeVisible(this);
        owner.addMouseListener(&listener, true);

        for (int i = 0; i < totalTracks; ++i)
        {
            auto& mute = muteButtons[(size_t)i];
            auto& solo = soloButtons[(size_t)i];
            mute.setButtonText("M"); solo.setButtonText("S");
            mute.setClickingTogglesState(false); solo.setClickingTogglesState(false);
            mute.setMouseClickGrabsKeyboardFocus(false); solo.setMouseClickGrabsKeyboardFocus(false);
            mute.onClick = [this, i] { setMute(i, !getMute(i)); syncButtons(); owner.repaint(); };
            solo.onClick = [this, i] { setSolo(i, !getSolo(i)); syncButtons(); owner.repaint(); };
            addAndMakeVisible(mute); addAndMakeVisible(solo);
        }

        renameEditor.setVisible(false);
        renameEditor.setSelectAllWhenFocused(true);
        renameEditor.setReturnKeyStartsNewLine(false);
        renameEditor.setEscapeAndReturnKeysConsumed(false);
        renameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff161a20));
        renameEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
        renameEditor.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff63c7e8));
        renameEditor.onReturnKey = [this] { finishRename(true); };
        renameEditor.onEscapeKey = [this] { finishRename(false); };
        renameEditor.onFocusLost = [this] { if (editingTrack >= 0) finishRename(true); };
        addAndMakeVisible(renameEditor);
        renameEditor.setVisible(false);
        startTimerHz(8);
    }

    ~TrackColourController() override { shutdown(); }

    void shutdown()
    {
        if (shutDown) return;
        shutDown = true;
        stopTimer();
        renameEditor.onReturnKey = nullptr;
        renameEditor.onEscapeKey = nullptr;
        renameEditor.onFocusLost = nullptr;
        owner.removeMouseListener(&listener);
        setVisible(false);
    }

    void paint(juce::Graphics& g) override
    {
        const double pixelsPerSecond = getLibertyTimelinePixelsPerSecond();
        const int rowH = getLibertyTrackRowHeight();
        for (int i = 0; i < totalTracks; ++i)
        {
            const int id = colourIds[(size_t)i];
            const auto colour = colourForId(id);
            const int rowY = 76 + rulerH + i * rowH;
            const auto header = juce::Rectangle<int>(0, rowY, headerW, rowH);

            if (id != colourDefault)
            {
                g.setColour(colour.withAlpha(0.24f)); g.fillRect(header);
                g.setColour(colour.withAlpha(0.95f));
                g.fillRect(header.getX(), header.getY(), 5, header.getHeight());
                g.fillRect(header.getX(), header.getY(), header.getWidth(), 3);

                if (i < AudioEngine::maxAudioTracks && owner.audioEngine.hasAudioFile(i))
                {
                    const int clipX = headerW + (int)std::round(owner.audioEngine.getTrackStartSeconds(i) * pixelsPerSecond);
                    const int clipW = juce::jmax(1, (int)std::round(owner.audioEngine.getAudioFileLengthSeconds(i) * pixelsPerSecond));
                    auto clip = juce::Rectangle<int>(clipX, rowY + 4, clipW, rowH - 8);
                    g.setColour(colour.withAlpha(0.20f)); g.fillRoundedRectangle(clip.toFloat(), 5.0f);
                    g.setColour(colour.withAlpha(0.95f)); g.drawRoundedRectangle(clip.toFloat(), 5.0f, 2.0f);
                }
                else if (i == midiTrackIndex && !owner.midiEngine.getNotesCopy().empty())
                {
                    const int clipX = headerW + (int)std::round(owner.midiClipStartSeconds * pixelsPerSecond);
                    const int clipW = juce::jmax(80, (int)std::round(owner.midiClipLengthSeconds * pixelsPerSecond));
                    auto clip = juce::Rectangle<int>(clipX, rowY + 4, clipW, rowH - 8);
                    g.setColour(colour.withAlpha(0.18f)); g.fillRoundedRectangle(clip.toFloat(), 5.0f);
                    g.setColour(colour.withAlpha(0.95f)); g.drawRoundedRectangle(clip.toFloat(), 5.0f, 2.0f);
                }

                if (i < AudioEngine::maxAudioTracks)
                {
                    const int mixerTop = owner.getHeight() - 210;
                    auto strip = juce::Rectangle<int>(220 + i * 125, mixerTop + 12, 116, 188);
                    g.setColour(colour.withAlpha(0.12f)); g.fillRoundedRectangle(strip.toFloat(), 5.0f);
                    g.setColour(colour.withAlpha(0.95f));
                    g.fillRoundedRectangle((float)strip.getX(), (float)strip.getY(), (float)strip.getWidth(), 5.0f, 2.0f);
                    g.drawRoundedRectangle(strip.toFloat(), 5.0f, 1.5f);
                }
            }

            if (editingTrack != i)
            {
                auto titleArea = juce::Rectangle<int>(8, rowY + 6, 96, 24);
                g.setColour(juce::Colour(0xff1e232a)); g.fillRect(titleArea);
                if (id != colourDefault) { g.setColour(colour.withAlpha(0.20f)); g.fillRect(titleArea); }
                g.setColour(juce::Colours::white); g.setFont(juce::Font(13.0f, juce::Font::bold));
                g.drawText(effectiveTrackName(i), titleArea.reduced(4, 0), juce::Justification::centredLeft, true);
            }

            if (i < AudioEngine::maxAudioTracks)
            {
                const int mixerTop = owner.getHeight() - 210;
                auto mixerTitle = juce::Rectangle<int>(224 + i * 125, mixerTop + 18, 108, 20);
                g.setColour(juce::Colour(0xff171b20)); g.fillRect(mixerTitle);
                if (id != colourDefault) { g.setColour(colour.withAlpha(0.16f)); g.fillRect(mixerTitle); }
                g.setColour(juce::Colours::white); g.setFont(juce::Font(11.0f, juce::Font::bold));
                g.drawText(effectiveTrackName(i), mixerTitle, juce::Justification::centred, true);
            }
        }
    }

private:
    class OwnerMouseListener final : public juce::MouseListener
    {
    public:
        explicit OwnerMouseListener(TrackColourController& c) : controller(c) {}
        void mouseDown(const juce::MouseEvent& event) override
        {
            const auto relative = event.getEventRelativeTo(&controller.owner);
            if (event.mods.isPopupMenu()) { controller.showColourMenu(relative); return; }
            if (event.getNumberOfClicks() >= 2) controller.beginRename(relative.getPosition());
        }
    private:
        TrackColourController& controller;
    };

    bool getMute(int track) const
    {
        if (track < AudioEngine::maxAudioTracks) return owner.audioEngine.isTrackMuted(track);
        if (track == midiTrackIndex) return owner.audioEngine.isMidiTrackMuted();
        return owner.audioEngine.isInstrumentTrackMuted();
    }

    bool getSolo(int track) const
    {
        if (track < AudioEngine::maxAudioTracks) return owner.audioEngine.isTrackSolo(track);
        if (track == midiTrackIndex) return owner.audioEngine.isMidiTrackSolo();
        return owner.audioEngine.isInstrumentTrackSolo();
    }

    void setMute(int track, bool value)
    {
        if (track < AudioEngine::maxAudioTracks) owner.audioEngine.setTrackMuted(track, value);
        else if (track == midiTrackIndex) owner.audioEngine.setMidiTrackMuted(value);
        else owner.audioEngine.setInstrumentTrackMuted(value);
    }

    void setSolo(int track, bool value)
    {
        if (track < AudioEngine::maxAudioTracks) owner.audioEngine.setTrackSolo(track, value);
        else if (track == midiTrackIndex) owner.audioEngine.setMidiTrackSolo(value);
        else owner.audioEngine.setInstrumentTrackSolo(value);
    }

    void beginRename(juce::Point<int> point)
    {
        if (point.x < 8 || point.x > 104) return;
        const int rowH = getLibertyTrackRowHeight();
        const int y = point.y - 76 - rulerH;
        if (y < 0) return;
        const int track = y / rowH;
        const int localY = y % rowH;
        if (track < 0 || track >= totalTracks || localY < 4 || localY > 32) return;

        if (editingTrack >= 0) finishRename(true);
        editingTrack = track;
        renameEditor.setText(effectiveTrackName(track), false);
        renameEditor.setBounds(8, 76 + rulerH + track * rowH + 6, 96, 24);
        renameEditor.setVisible(true);
        renameEditor.toFront(true);
        renameEditor.grabKeyboardFocus();
        renameEditor.selectAll();
        repaint();
    }

    void finishRename(bool commit)
    {
        if (editingTrack < 0) return;
        const int track = editingTrack;
        editingTrack = -1;
        if (commit)
        {
            auto text = renameEditor.getText().trim();
            if (text.isEmpty()) text = defaultTrackName(track);
            if (text.length() > 32) text = text.substring(0, 32);
            trackNames[(size_t)track] = text;
        }
        renameEditor.setVisible(false);
        owner.grabKeyboardFocus();
        repaint(); owner.repaint();
    }

    void showColourMenu(const juce::MouseEvent& event)
    {
        const auto p = event.getPosition();
        if (p.x < 0 || p.x >= headerW) return;
        const int rowH = getLibertyTrackRowHeight();
        const int y = p.y - 76 - rulerH;
        if (y < 0) return;
        const int track = y / rowH;
        if (track < 0 || track >= totalTracks) return;

        if (track < AudioEngine::maxAudioTracks) owner.selectedTrack = track;
        else if (track == midiTrackIndex) owner.selectMidiTrack();
        owner.repaint();

        juce::PopupMenu menu;
        for (int id = 0; id < (int)palette.size(); ++id)
            menu.addItem(id + 1, colourNames[id], true, colourIds[(size_t)track] == id);

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(event.getScreenPosition(), { 1, 1 })),
            [this, track](int result)
            {
                if (result <= 0 || shutDown) return;
                colourIds[(size_t)track] = result - 1;
                repaint(); owner.repaint();
            });
    }

    void syncButtons()
    {
        for (int i = 0; i < totalTracks; ++i)
        {
            auto& mute = muteButtons[(size_t)i]; auto& solo = soloButtons[(size_t)i];
            mute.setColour(juce::TextButton::buttonColourId, getMute(i) ? juce::Colour(0xff9b4545) : juce::Colour(0xff252a31));
            solo.setColour(juce::TextButton::buttonColourId, getSolo(i) ? juce::Colour(0xff8b7a32) : juce::Colour(0xff252a31));
            mute.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            solo.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        }
    }

    void resized() override
    {
        const int rowH = getLibertyTrackRowHeight();
        for (int i = 0; i < totalTracks; ++i)
        {
            const int rowY = 76 + rulerH + i * rowH;
            const int buttonY = rowY + rowH - 29;
            muteButtons[(size_t)i].setBounds(160, buttonY + 2, 21, 20);
            soloButtons[(size_t)i].setBounds(184, buttonY + 2, 21, 20);
        }
        if (editingTrack >= 0)
            renameEditor.setBounds(8, 76 + rulerH + editingTrack * rowH + 6, 96, 24);
    }

    void timerCallback() override
    {
        if (shutDown) return;

        const auto wantedBounds = owner.getLocalBounds();
        if (getBounds() != wantedBounds)
            setBounds(wantedBounds);
        else
            resized();

        syncButtons();
        if (editingTrack >= 0)
            renameEditor.toFront(true);
    }

    MainComponent& owner;
    OwnerMouseListener listener;
    std::array<juce::TextButton, totalTracks> muteButtons;
    std::array<juce::TextButton, totalTracks> soloButtons;
    juce::TextEditor renameEditor;
    int editingTrack = -1;
    bool shutDown = false;
};

class TrackColourBootstrap final : private juce::Timer
{
public:
    TrackColourBootstrap() { startTimerHz(10); }
    ~TrackColourBootstrap() override { shutdown(); }

    void shutdown()
    {
        stopTimer();
        for (auto& entry : controllers)
            if (entry.second != nullptr) entry.second->shutdown();
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
                        controllers.emplace(main, std::make_unique<TrackColourController>(*main));
    }

    std::map<MainComponent*, std::unique_ptr<TrackColourController>> controllers;
};

TrackColourBootstrap trackColourBootstrap;
}

int getLibertyTrackColourId(int track)
{
    if (track < 0 || track >= totalTracks) return 0;
    return colourIds[(size_t)track];
}

void setLibertyTrackColourId(int track, int colourId)
{
    if (track < 0 || track >= totalTracks) return;
    colourIds[(size_t)track] = juce::jlimit(0, (int)palette.size() - 1, colourId);
}

void resetLibertyTrackColours() { colourIds.fill(0); }

juce::String getLibertyTrackName(int track) { return effectiveTrackName(track); }

void setLibertyTrackName(int track, const juce::String& name)
{
    if (track < 0 || track >= totalTracks) return;
    auto clean = name.trim();
    if (clean.length() > 32) clean = clean.substring(0, 32);
    trackNames[(size_t)track] = clean;
}

void resetLibertyTrackNames()
{
    for (auto& name : trackNames) name.clear();
}

void shutdownLibertyTrackColourInteraction()
{
    trackColourBootstrap.shutdown();
}
