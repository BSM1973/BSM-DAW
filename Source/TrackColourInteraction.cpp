#define private public
#include "MainComponent.h"
#undef private

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <map>
#include <memory>

namespace
{
constexpr int colourDefault = 0;
constexpr std::array<juce::uint32, 9> palette {
    0xff31506a,
    0xff3b82f6,
    0xff22c55e,
    0xffeab308,
    0xfff97316,
    0xffef4444,
    0xffa855f7,
    0xffec4899,
    0xff14b8a6
};

constexpr const char* names[] = {
    "Défaut",
    "Bleu",
    "Vert",
    "Jaune",
    "Orange",
    "Rouge",
    "Violet",
    "Rose",
    "Turquoise"
};

std::array<int, AudioEngine::maxAudioTracks> colourIds {};

juce::Colour colourForId(int id)
{
    if (id <= 0 || id >= static_cast<int>(palette.size()))
        return juce::Colour(palette[0]);
    return juce::Colour(palette[(size_t)id]);
}

class TrackColourController final : public juce::Component,
                                    private juce::MouseListener,
                                    private juce::Timer
{
public:
    explicit TrackColourController(MainComponent& ownerIn) : owner(ownerIn)
    {
        setInterceptsMouseClicks(false, false);
        owner.addAndMakeVisible(this);
        owner.addMouseListener(this, true);
        startTimerHz(10);
    }

    ~TrackColourController() override
    {
        stopTimer();
        owner.removeMouseListener(this);
        setVisible(false);
    }

    void paint(juce::Graphics& g) override
    {
        constexpr int headerW = 210;
        constexpr int rulerH = 32;
        constexpr int rowH = 70;
        constexpr float pixelsPerSecond = 80.0f;

        for (int i = 0; i < AudioEngine::maxAudioTracks; ++i)
        {
            const int id = colourIds[(size_t)i];
            if (id == colourDefault)
                continue;

            const auto colour = colourForId(id);
            const int rowY = 76 + rulerH + i * rowH;
            const auto header = juce::Rectangle<int>(0, rowY, headerW, rowH);

            g.setColour(colour.withAlpha(0.24f));
            g.fillRect(header);
            g.setColour(colour.withAlpha(0.95f));
            g.fillRect(header.getX(), header.getY(), 5, header.getHeight());
            g.fillRect(header.getX(), header.getY(), header.getWidth(), 3);

            if (owner.audioEngine.hasAudioFile(i))
            {
                const int clipX = headerW + static_cast<int>(std::round(owner.audioEngine.getTrackStartSeconds(i) * pixelsPerSecond));
                const int clipW = juce::jmax(1, static_cast<int>(std::round(owner.audioEngine.getAudioFileLengthSeconds(i) * pixelsPerSecond)));
                auto clip = juce::Rectangle<int>(clipX, rowY + 4, clipW, rowH - 8);
                g.setColour(colour.withAlpha(0.20f));
                g.fillRoundedRectangle(clip.toFloat(), 5.0f);
                g.setColour(colour.withAlpha(0.95f));
                g.drawRoundedRectangle(clip.toFloat(), 5.0f, 2.0f);
            }

            const int mixerTop = owner.getHeight() - 210;
            auto strip = juce::Rectangle<int>(220 + i * 125, mixerTop + 12, 116, 188);
            g.setColour(colour.withAlpha(0.12f));
            g.fillRoundedRectangle(strip.toFloat(), 5.0f);
            g.setColour(colour.withAlpha(0.95f));
            g.fillRoundedRectangle((float)strip.getX(), (float)strip.getY(), (float)strip.getWidth(), 5.0f, 2.0f);
            g.drawRoundedRectangle(strip.toFloat(), 5.0f, 1.5f);
        }
    }

private:
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (!event.mods.isPopupMenu())
            return;

        const auto e = event.getEventRelativeTo(&owner);
        const int y = e.getPosition().y - 76 - 32;
        if (y < 0)
            return;

        const int track = y / 70;
        if (track < 0 || track >= AudioEngine::maxAudioTracks)
            return;

        owner.selectedTrack = track;
        owner.repaint();

        juce::PopupMenu menu;
        for (int id = 0; id < static_cast<int>(palette.size()); ++id)
            menu.addItem(id + 1, names[id], true, colourIds[(size_t)track] == id);

        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(event.getScreenPosition(), { 1, 1 })),
            [this, track](int result)
            {
                if (result <= 0)
                    return;
                colourIds[(size_t)track] = result - 1;
                repaint();
                owner.repaint();
            });
    }

    void timerCallback() override
    {
        setBounds(owner.getLocalBounds());
        toFront(false);
        repaint();
    }

    MainComponent& owner;
};

class TrackColourBootstrap final : private juce::Timer
{
public:
    TrackColourBootstrap() { startTimerHz(10); }
    ~TrackColourBootstrap() override { stopTimer(); controllers.clear(); }

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
    if (track < 0 || track >= AudioEngine::maxAudioTracks)
        return 0;
    return colourIds[(size_t)track];
}

void setLibertyTrackColourId(int track, int colourId)
{
    if (track < 0 || track >= AudioEngine::maxAudioTracks)
        return;
    colourIds[(size_t)track] = juce::jlimit(0, static_cast<int>(palette.size()) - 1, colourId);
}

void resetLibertyTrackColours()
{
    colourIds.fill(0);
}
