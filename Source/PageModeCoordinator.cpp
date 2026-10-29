#define private public
#include "MainComponent.h"
#undef private

#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>

bool isLibertyMixConsoleVisible(MainComponent* owner);
bool isLibertyPerformVisible(MainComponent* owner);
void setLibertyPerformVisible(MainComponent* owner, bool shouldShow);
void showLibertyMixConsolePage(MainComponent*, bool);
void ensureLibertyMixConsoleController(MainComponent*);
void ensureLibertyPerformController(MainComponent*);

namespace
{
class PageModeCoordinator;
std::map<MainComponent*, std::unique_ptr<PageModeCoordinator>> coordinators;

class ArrangementLookAndFeel final : public juce::LookAndFeel_V4
{
public:
 void drawButtonBackground(juce::Graphics& g, juce::Button& b,
                           const juce::Colour&, bool hover, bool pressed) override
 {
     auto box = b.getLocalBounds().toFloat().reduced(0.7f);
     const bool selected = b.getToggleState();
     const auto cyan = juce::Colour(0xff58d1ed);
     juce::ColourGradient fill(selected ? juce::Colour(0xff305b78) : juce::Colour(0xff243a52),
                               box.getX(), box.getY(),
                               selected ? juce::Colour(0xff142b43) : juce::Colour(0xff111f32),
                               box.getRight(), box.getBottom(), false);
     g.setGradientFill(fill);
     g.fillRoundedRectangle(box, 6.5f);
     g.setColour((selected ? cyan : juce::Colour(0xff65869f)).withAlpha(hover ? 0.95f : 0.65f));
     g.drawRoundedRectangle(box.reduced(0.5f), 6.0f, 1.0f);
     if (hover || pressed)
     {
         g.setColour(cyan.withAlpha(pressed ? 0.18f : 0.08f));
         g.fillRoundedRectangle(box.reduced(1.0f), 6.0f);
     }
     if (selected)
     {
         g.setColour(cyan);
         g.fillRoundedRectangle(box.getX() + 12.0f, box.getBottom() - 3.0f,
                                box.getWidth() - 24.0f, 2.0f, 1.0f);
     }
 }
 void drawButtonText(juce::Graphics& g, juce::TextButton& b,
                     bool, bool) override
 {
     g.setColour(b.getToggleState() ? juce::Colours::white : juce::Colour(0xffd0e0ee));
     g.setFont(juce::Font(11.0f, juce::Font::bold));
     g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(6, 2),
                      juce::Justification::centred, 1, 0.9f);
 }
};

class PageModeCoordinator final : private juce::Timer
{
public:
    explicit PageModeCoordinator(MainComponent& ownerIn) : owner(ownerIn)
    {
        configureButton(arrangeButton, "ARRANGEMENT");
        arrangeButton.setLookAndFeel(&arrangementLook);
        arrangeButton.setTooltip("Afficher la page Arrangement");
        configureButton(mixButton, "MIXCONSOLE");
        configureButton(performButton, "PERFORMANCE");

        captureLegacyButtons();

        // Activate the existing full pages directly, not through hidden legacy buttons.
        arrangeButton.onClick = [this]
        {
            setLibertyPerformVisible(&owner, false);
            showLibertyMixConsolePage(&owner, false);
            refresh();
        };
        mixButton.onClick = [this]
        {
            setLibertyPerformVisible(&owner, false);
            showLibertyMixConsolePage(&owner, true);
            refresh();
        };
        performButton.onClick = [this]
        {
            showLibertyMixConsolePage(&owner, false);
            setLibertyPerformVisible(&owner, true);
            refresh();
        };

        owner.addAndMakeVisible(arrangeButton);
        owner.addAndMakeVisible(mixButton);
        owner.addAndMakeVisible(performButton);
        startTimerHz(30);
        refresh();
    }

    ~PageModeCoordinator() override { shutdown(); }

    void shutdown()
    {
        stopTimer();
        arrangeButton.setLookAndFeel(nullptr);
        arrangeButton.setVisible(false);
        mixButton.setVisible(false);
        performButton.setVisible(false);
    }

    void setMixVisible(bool shouldShow)
    {
        if (shouldShow) mixButton.triggerClick();
        else arrangeButton.triggerClick();
    }

private:
    void configureButton(juce::TextButton& button, const juce::String& text)
    {
        button.setButtonText(text);
        button.setClickingTogglesState(false);
        button.setMouseClickGrabsKeyboardFocus(false);
        button.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        button.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    }

    void captureLegacyButtons()
    {
        for (int i = 0; i < owner.getNumChildComponents(); ++i)
        {
            auto* button = dynamic_cast<juce::TextButton*>(owner.getChildComponent(i));
            if (button == nullptr) continue;
            if (button == &arrangeButton || button == &mixButton || button == &performButton) continue;

            const auto text = button->getButtonText();
            if (text == "ARRANGEMENT" && legacyArrange == nullptr) legacyArrange = button;
            else if (text == "MIXCONSOLE" && legacyMix == nullptr) legacyMix = button;
            else if (text == "PERFORM" && legacyPerform == nullptr) legacyPerform = button;
        }
    }

    void hideEveryLegacyPageButton()
    {
        for (int i = 0; i < owner.getNumChildComponents(); ++i)
        {
            auto* button = dynamic_cast<juce::TextButton*>(owner.getChildComponent(i));
            if (button == nullptr) continue;
            if (button == &arrangeButton || button == &mixButton || button == &performButton) continue;
            const auto text = button->getButtonText();
            if (text == "ARRANGEMENT" || text == "MIXCONSOLE" || text == "PERFORM")
                button->setVisible(false);
        }
    }

    void refresh()
    {
        const bool perform = isLibertyPerformVisible(&owner);
        const bool mix = !perform && isLibertyMixConsoleVisible(&owner);
        const bool arrange = !perform && !mix;

        const auto active = juce::Colour(0xff315f7a);
        const auto inactive = juce::Colour(0xff252a31);
        arrangeButton.setToggleState(arrange, juce::dontSendNotification);
        arrangeButton.setColour(juce::TextButton::buttonColourId, arrange ? active : inactive);
        mixButton.setColour(juce::TextButton::buttonColourId, mix ? active : inactive);
        performButton.setColour(juce::TextButton::buttonColourId, perform ? active : inactive);

        // Navigation starts at the beginning of the transport section, above PLAY.
        // Reserve the next slot for the NAVIGATEUR toggle.
        const int navX = 205;
        arrangeButton.setBounds(navX, 4, 120, 25);
        mixButton.setBounds(navX + 124, 4, 98, 25);
        performButton.setBounds(navX + 226, 4, 92, 25);

        hideEveryLegacyPageButton();
        arrangeButton.setVisible(true);
        mixButton.setVisible(true);
        performButton.setVisible(true);
        arrangeButton.toFront(false);
        mixButton.toFront(false);
        performButton.toFront(false);

        if (perform)
        {
            for (int i = 0; i < owner.getNumChildComponents(); ++i)
            {
                auto* child = owner.getChildComponent(i);
                if (child != nullptr && child->isVisible() && child->isAlwaysOnTop())
                    child->toFront(false);
            }
            arrangeButton.toFront(false);
            mixButton.toFront(false);
            performButton.toFront(false);
        }
    }

    void timerCallback() override { refresh(); }

    MainComponent& owner;
    ArrangementLookAndFeel arrangementLook;
    juce::TextButton arrangeButton, mixButton, performButton;
    juce::TextButton* legacyArrange = nullptr;
    juce::TextButton* legacyMix = nullptr;
    juce::TextButton* legacyPerform = nullptr;
};

class Bootstrap final : private juce::Timer
{
public:
    Bootstrap() { startTimerHz(10); }
    ~Bootstrap() override { shutdown(); }

    void shutdown()
    {
        stopTimer();
        for (auto& entry : coordinators)
            if (entry.second) entry.second->shutdown();
        coordinators.clear();
    }

private:
    void timerCallback() override
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* window = dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i)))
                if (auto* main = dynamic_cast<MainComponent*>(window->getContentComponent()))
                    if (coordinators.find(main) == coordinators.end())
                    {
                        coordinators.emplace(main, std::make_unique<PageModeCoordinator>(*main));
                        stopTimer();
                        return;
                    }
    }
};

Bootstrap bootstrap;
}

void ensureLibertyPageModeCoordinator(MainComponent* owner)
{
    if (owner == nullptr) return;
    if (coordinators.find(owner) == coordinators.end())
        coordinators.emplace(owner, std::make_unique<PageModeCoordinator>(*owner));
}

void setLibertyMixConsoleVisible(MainComponent* owner, bool shouldShow)
{
    if (owner == nullptr) return;
    const auto it = coordinators.find(owner);
    if (it == coordinators.end() || !it->second) return;
    it->second->setMixVisible(shouldShow);
}

void shutdownLibertyPageModeCoordinator()
{
    bootstrap.shutdown();
}
