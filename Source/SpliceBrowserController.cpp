#define private public
#include "MainComponent.h"
#undef private

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <atomic>
#include <map>
#include <memory>

namespace
{
juce::TextButton* findDirectButton(juce::Component* parent, const juce::String& text)
{
    if (parent == nullptr) return nullptr;
    for (int i = 0; i < parent->getNumChildComponents(); ++i)
        if (auto* button = dynamic_cast<juce::TextButton*>(parent->getChildComponent(i)))
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
        if (findDirectButton(child, "PLUGINS") != nullptr && findDirectButton(child, "FILES") != nullptr)
            return child;
    }
    return nullptr;
}

class SplicePanel final : public juce::Component
{
public:
    SplicePanel()
    {
        setOpaque(true);

        soundsButton.setButtonText("SOUNDS");
        loginButton.setButtonText("LOGIN");
        refreshButton.setButtonText("REFRESH");
        desktopButton.setButtonText("DESKTOP");
        for (auto* button : { &soundsButton, &loginButton, &refreshButton, &desktopButton })
        {
            button->setMouseClickGrabsKeyboardFocus(false);
            button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252a31));
            button->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            addAndMakeVisible(*button);
        }

        soundsButton.onClick = [this] { browser.goToURL("https://splice.com/sounds"); };
        loginButton.onClick = [this] { browser.goToURL("https://splice.com/login"); };
        refreshButton.onClick = [this] { browser.refresh(); };
        desktopButton.onClick = []
        {
           #if JUCE_MAC
            const juce::File app("/Applications/Splice.app");
            if (app.exists())
                app.startAsProcess();
            else
                juce::URL("https://splice.com/tools/desktop").launchInDefaultBrowser();
           #else
            juce::URL("https://splice.com/tools/desktop").launchInDefaultBrowser();
           #endif
        };

        addAndMakeVisible(browser);
        browser.goToURL("https://splice.com/sounds");
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff101318));
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(14.0f, juce::Font::bold));
        g.drawText("SPLICE", 10, 4, 100, 22, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff8f98a3));
        g.setFont(juce::Font(9.0f));
        g.drawText("Splice account and Sounds inside Liberty", 74, 6, getWidth() - 84, 18, juce::Justification::centredRight, true);
    }

    void resized() override
    {
        const int y = 30;
        const int w = juce::jmax(48, (getWidth() - 50) / 4);
        soundsButton.setBounds(8, y, w, 26);
        loginButton.setBounds(12 + w, y, w, 26);
        refreshButton.setBounds(16 + w * 2, y, w, 26);
        desktopButton.setBounds(20 + w * 3, y, w, 26);
        browser.setBounds(6, 62, getWidth() - 12, juce::jmax(40, getHeight() - 68));
    }

private:
    juce::TextButton soundsButton, loginButton, refreshButton, desktopButton;
    juce::WebBrowserComponent browser;
};

class SpliceBrowserController final : private juce::Timer, private juce::MouseListener
{
public:
    explicit SpliceBrowserController(MainComponent& ownerIn) : owner(ownerIn)
    {
        juce::Desktop::getInstance().addGlobalMouseListener(this);
        startTimerHz(15);
    }

    ~SpliceBrowserController() override { shutdown(); }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
        juce::Desktop::getInstance().removeGlobalMouseListener(this);
        if (spliceButton != nullptr) spliceButton->setVisible(false);
        if (splicePanel != nullptr) splicePanel->setVisible(false);
    }

private:
    void attachIfPossible()
    {
        if (browserPanel != nullptr) return;
        browserPanel = findBrowserPanel(owner);
        if (browserPanel == nullptr) return;

        spliceButtonOwned = std::make_unique<juce::TextButton>("SPLICE");
        spliceButton = spliceButtonOwned.get();
        spliceButton->setMouseClickGrabsKeyboardFocus(false);
        spliceButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2027));
        spliceButton->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffc9cdd3));
        spliceButton->onClick = [this] { showSplice(true); };
        browserPanel->addAndMakeVisible(*spliceButton);

        splicePanelOwned = std::make_unique<SplicePanel>();
        splicePanel = splicePanelOwned.get();
        splicePanel->setVisible(false);
        browserPanel->addAndMakeVisible(*splicePanel);
    }

    void showSplice(bool shouldShow)
    {
        spliceVisible = shouldShow;
        if (splicePanel != nullptr)
        {
            splicePanel->setVisible(shouldShow);
            if (shouldShow) splicePanel->toFront(false);
        }
        if (spliceButton != nullptr)
            spliceButton->setColour(juce::TextButton::buttonColourId,
                                    shouldShow ? juce::Colour(0xff315f7a) : juce::Colour(0xff1b2027));
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (!spliceVisible || event.eventComponent == nullptr) return;
        auto* button = dynamic_cast<juce::TextButton*>(event.eventComponent);
        if (button == nullptr || button == spliceButton) return;
        const auto text = button->getButtonText();
        if (text == "FILES" || text == "AUDIO" || text == "MIDI" || text == "PRESETS" || text == "PLUGINS")
            showSplice(false);
    }

    void timerCallback() override
    {
        if (stopped.load()) return;
        attachIfPossible();
        if (browserPanel == nullptr || spliceButton == nullptr || splicePanel == nullptr) return;

        auto* files = findDirectButton(browserPanel, "FILES");
        auto* audio = findDirectButton(browserPanel, "AUDIO");
        auto* midi = findDirectButton(browserPanel, "MIDI");
        auto* presets = findDirectButton(browserPanel, "PRESETS");
        auto* plugins = findDirectButton(browserPanel, "PLUGINS");

        if (files && audio && midi && presets && plugins)
        {
            files->setBounds(8, 42, 42, 26);
            audio->setBounds(52, 42, 44, 26);
            midi->setBounds(98, 42, 40, 26);
            presets->setBounds(140, 42, 54, 26);
            plugins->setBounds(196, 42, 56, 26);
            spliceButton->setBounds(254, 42, 58, 26);
        }

        splicePanel->setBounds(0, 72, browserPanel->getWidth(), juce::jmax(40, browserPanel->getHeight() - 72));
        spliceButton->setVisible(browserPanel->isVisible());
        if (spliceVisible)
        {
            splicePanel->setVisible(browserPanel->isVisible());
            if (browserPanel->isVisible()) splicePanel->toFront(false);
            spliceButton->toFront(false);
        }
    }

    MainComponent& owner;
    juce::Component* browserPanel = nullptr;
    std::unique_ptr<juce::TextButton> spliceButtonOwned;
    juce::TextButton* spliceButton = nullptr;
    std::unique_ptr<SplicePanel> splicePanelOwned;
    SplicePanel* splicePanel = nullptr;
    bool spliceVisible = false;
    std::atomic<bool> stopped { false };
};

std::map<MainComponent*, std::unique_ptr<SpliceBrowserController>> controllers;

class Bootstrap final : private juce::Timer
{
public:
    Bootstrap() { startTimerHz(10); }
    ~Bootstrap() override { shutdown(); }

    void shutdown()
    {
        stopTimer();
        for (auto& entry : controllers)
            if (entry.second) entry.second->shutdown();
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
                        controllers.emplace(main, std::make_unique<SpliceBrowserController>(*main));
    }
};

Bootstrap bootstrap;
}

void shutdownLibertySpliceBrowserController()
{
    bootstrap.shutdown();
}
