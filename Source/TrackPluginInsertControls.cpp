#define private public
#include "MainComponent.h"
#undef private
#include "PluginHost.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <atomic>
#include <map>
#include <memory>

int getLibertyTrackRowHeight() noexcept;

namespace
{
constexpr int rulerH = 32;
constexpr int instrumentTrack = AudioEngine::maxAudioTracks + 1;
constexpr int visibleInsertRows = AudioEngine::maxAudioTracks + 1;

class TrackPluginInsertControls final : public juce::Component,
                                       private juce::Timer
{
public:
    explicit TrackPluginInsertControls(MainComponent& ownerIn) : owner(ownerIn)
    {
        setInterceptsMouseClicks(false, true);

        for (int i = 0; i < visibleInsertRows; ++i)
        {
            auto& insert = insertButtons[(size_t)i];
            auto& remove = removeButtons[(size_t)i];

            insert.setMouseClickGrabsKeyboardFocus(false);
            remove.setMouseClickGrabsKeyboardFocus(false);
            insert.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff242a31));
            insert.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff315f7a));
            insert.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd9e0e8));
            remove.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a2528));
            remove.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffffb4b4));
            remove.setButtonText("×");

            insert.onClick = [this, i]
            {
                auto& host = LibertyPluginHost::instance();
                if (i < AudioEngine::maxAudioTracks)
                {
                    if (host.hasEffectForTrack(i)) host.showEditorForTrack(i);
                }
                else
                {
                    if (host.hasInstrument()) host.showInstrumentEditor();
                }
            };

            remove.onClick = [this, i]
            {
                auto& host = LibertyPluginHost::instance();
                if (i < AudioEngine::maxAudioTracks)
                    host.unloadEffectForTrack(i);
                else
                    host.unloadInstrument();
                refreshTexts();
                owner.repaint();
            };

            addAndMakeVisible(insert);
            addAndMakeVisible(remove);
        }

        owner.addAndMakeVisible(this);
        startTimerHz(8);
    }

    ~TrackPluginInsertControls() override { shutdown(); }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
        setVisible(false);
    }

    void resized() override
    {
        const int rowH = getLibertyTrackRowHeight();
        for (int i = 0; i < visibleInsertRows; ++i)
        {
            const int row = i < AudioEngine::maxAudioTracks ? i : instrumentTrack;
            const int y = 76 + rulerH + row * rowH;
            insertButtons[(size_t)i].setBounds(108, y + 5, 78, 22);
            removeButtons[(size_t)i].setBounds(188, y + 5, 17, 22);
        }
    }

private:
    void refreshTexts()
    {
        auto& host = LibertyPluginHost::instance();
        for (int i = 0; i < AudioEngine::maxAudioTracks; ++i)
        {
            const bool loaded = host.hasEffectForTrack(i);
            auto name = loaded ? host.getEffectName(i) : juce::String("+ FX");
            if (name.length() > 12) name = name.substring(0, 11) + "…";
            insertButtons[(size_t)i].setButtonText(loaded ? "FX " + name : name);
            insertButtons[(size_t)i].setTooltip(loaded ? host.getEffectName(i)
                                                       : "Charge un effet depuis Browser > Plugins");
            removeButtons[(size_t)i].setVisible(loaded);
        }

        const bool instrumentLoaded = host.hasInstrument();
        auto instrumentName = instrumentLoaded ? host.getInstrumentName() : juce::String("+ INST");
        if (instrumentName.length() > 10) instrumentName = instrumentName.substring(0, 9) + "…";
        insertButtons[(size_t)AudioEngine::maxAudioTracks].setButtonText(instrumentName);
        insertButtons[(size_t)AudioEngine::maxAudioTracks].setTooltip(
            instrumentLoaded ? host.getInstrumentName() : "Charge un instrument depuis Browser > Plugins");
        removeButtons[(size_t)AudioEngine::maxAudioTracks].setVisible(instrumentLoaded);
    }

    void timerCallback() override
    {
        if (stopped.load()) return;
        const auto wanted = owner.getLocalBounds();
        if (getBounds() != wanted) setBounds(wanted);
        else resized();
        refreshTexts();
        toFront(false);
    }

    MainComponent& owner;
    std::array<juce::TextButton, visibleInsertRows> insertButtons;
    std::array<juce::TextButton, visibleInsertRows> removeButtons;
    std::atomic<bool> stopped { false };
};

std::map<MainComponent*, std::unique_ptr<TrackPluginInsertControls>> controllers;

class Bootstrap final : private juce::Timer
{
public:
    Bootstrap() { startTimerHz(10); }
    ~Bootstrap() override { shutdown(); }

    void shutdown()
    {
        stopTimer();
        for (auto& item : controllers)
            if (item.second) item.second->shutdown();
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
                        controllers.emplace(main, std::make_unique<TrackPluginInsertControls>(*main));
    }
};

Bootstrap bootstrap;
}

void shutdownLibertyTrackPluginInsertControls()
{
    bootstrap.shutdown();
}
