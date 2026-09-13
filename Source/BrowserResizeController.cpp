#define private public
#include "MainComponent.h"
#undef private

#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <map>
#include <memory>

namespace
{
constexpr int topBarHeight = 76;
constexpr int minBrowserWidth = 260;
constexpr int maxBrowserWidth = 720;

juce::Component* findBrowserPanel(MainComponent& owner)
{
    for (int i = 0; i < owner.getNumChildComponents(); ++i)
    {
        auto* child = owner.getChildComponent(i);
        if (child == nullptr) continue;
        bool hasFiles = false, hasPlugins = false;
        for (int j = 0; j < child->getNumChildComponents(); ++j)
        {
            if (auto* b = dynamic_cast<juce::TextButton*>(child->getChildComponent(j)))
            {
                hasFiles = hasFiles || b->getButtonText() == "FILES";
                hasPlugins = hasPlugins || b->getButtonText() == "PLUGINS";
            }
        }
        if (hasFiles && hasPlugins) return child;
    }
    return nullptr;
}

juce::File widthSettingsFile()
{
    auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                      .getChildFile("BSM").getChildFile("Liberty");
    folder.createDirectory();
    return folder.getChildFile("BrowserWidth.txt");
}

class BrowserResizeController;

class ResizeHandle final : public juce::Component
{
public:
    explicit ResizeHandle(BrowserResizeController& controllerIn) : controller(controllerIn)
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        setAlwaysOnTop(true);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0x22000000));
        g.setColour(juce::Colour(0xff69d4ff));
        g.fillRoundedRectangle((float)getWidth() * 0.5f - 1.0f, 8.0f, 2.0f,
                               (float)juce::jmax(0, getHeight() - 16), 1.0f);
    }

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    BrowserResizeController& controller;
    int startWidth = 320;
    int startScreenX = 0;
};

class BrowserResizeController final : private juce::Timer,
                                     private juce::ComponentListener
{
public:
    explicit BrowserResizeController(MainComponent& ownerIn)
        : owner(ownerIn), handle(*this)
    {
        if (const auto file = widthSettingsFile(); file.existsAsFile())
            preferredWidth = juce::jlimit(minBrowserWidth, maxBrowserWidth, file.loadFileAsString().getIntValue());

        handle.setVisible(false);
        owner.addAndMakeVisible(handle);
        startTimerHz(30);
    }

    ~BrowserResizeController() override { shutdown(); }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
        if (browserPanel != nullptr) browserPanel->removeComponentListener(this);
        handle.setVisible(false);
        browserPanel = nullptr;
    }

    int getPreferredWidth() const noexcept { return preferredWidth; }

    void setPreferredWidth(int width)
    {
        preferredWidth = juce::jlimit(minBrowserWidth,
                                      juce::jmin(maxBrowserWidth, juce::jmax(minBrowserWidth, owner.getWidth() - 220)),
                                      width);
        applyBounds();
    }

    void saveWidth()
    {
        widthSettingsFile().replaceWithText(juce::String(preferredWidth));
    }

private:
    friend class ResizeHandle;

    void attachIfPossible()
    {
        if (browserPanel != nullptr) return;
        browserPanel = findBrowserPanel(owner);
        if (browserPanel == nullptr) return;
        browserPanel->addComponentListener(this);
        applyBounds();
    }

    void applyBounds()
    {
        if (browserPanel == nullptr || !browserPanel->isVisible())
        {
            handle.setVisible(false);
            return;
        }

        const int width = juce::jlimit(minBrowserWidth,
                                       juce::jmin(maxBrowserWidth, juce::jmax(minBrowserWidth, owner.getWidth() - 220)),
                                       preferredWidth);
        const auto wanted = juce::Rectangle<int>(juce::jmax(0, owner.getWidth() - width),
                                                 topBarHeight,
                                                 juce::jmin(width, owner.getWidth()),
                                                 juce::jmax(1, owner.getHeight() - topBarHeight));
        if (browserPanel->getBounds() != wanted)
        {
            const juce::ScopedValueSetter<bool> guard(applying, true);
            browserPanel->setBounds(wanted);
        }

        handle.setBounds(wanted.getX() - 5, wanted.getY(), 10, wanted.getHeight());
        handle.setVisible(true);
        handle.toFront(false);
    }

    void componentMovedOrResized(juce::Component&, bool, bool) override
    {
        if (!applying) applyBounds();
    }

    void componentVisibilityChanged(juce::Component&) override
    {
        applyBounds();
    }

    void timerCallback() override
    {
        if (stopped.load()) return;
        attachIfPossible();
        applyBounds();
    }

    MainComponent& owner;
    ResizeHandle handle;
    juce::Component* browserPanel = nullptr;
    int preferredWidth = 320;
    bool applying = false;
    std::atomic<bool> stopped { false };
};

void ResizeHandle::mouseDown(const juce::MouseEvent& event)
{
    startWidth = controller.getPreferredWidth();
    startScreenX = event.getScreenPosition().x;
}

void ResizeHandle::mouseDrag(const juce::MouseEvent& event)
{
    const int delta = startScreenX - event.getScreenPosition().x;
    controller.setPreferredWidth(startWidth + delta);
}

void ResizeHandle::mouseUp(const juce::MouseEvent&)
{
    controller.saveWidth();
}

std::map<MainComponent*, std::unique_ptr<BrowserResizeController>> controllers;

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
                        controllers.emplace(main, std::make_unique<BrowserResizeController>(*main));
    }
};

Bootstrap bootstrap;
}

void shutdownLibertyBrowserResizeController()
{
    bootstrap.shutdown();
}
