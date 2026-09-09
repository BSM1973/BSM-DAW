#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"
#include "MidiEditor.h"

class LibertyApplication final : public juce::JUCEApplication
{
public:
    LibertyApplication() = default;
    const juce::String getApplicationName() override { return "Liberty"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void initialise(const juce::String&) override { mainWindow = std::make_unique<MainWindow>(getApplicationName()); }
    void shutdown() override { shutdownLibertyMidiEditor(); mainWindow.reset(); }
    void systemRequestedQuit() override { if (mainWindow != nullptr) mainWindow->requestClose(); else quit(); }
    void anotherInstanceStarted(const juce::String&) override {}
private:
    class MainWindow final : public juce::DocumentWindow, private juce::KeyListener
    {
    public:
        explicit MainWindow(juce::String name) : DocumentWindow(std::move(name), juce::Colours::black, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true); setContentOwned(new MainComponent(), true); centreWithSize(getWidth(), getHeight()); setResizable(true, true); addKeyListener(this); setVisible(true);
            if (auto* content = dynamic_cast<MainComponent*>(getContentComponent())) content->grabKeyboardFocus();
        }
        ~MainWindow() override { removeKeyListener(this); }
        bool keyPressed(const juce::KeyPress& key, juce::Component*) override
        {
            if (auto* content = dynamic_cast<MainComponent*>(getContentComponent())) if (content->keyPressed(key)) return true;
            return true;
        }
        bool keyPressed(const juce::KeyPress& key) override
        {
            if (auto* content = dynamic_cast<MainComponent*>(getContentComponent())) if (content->keyPressed(key)) return true;
            return DocumentWindow::keyPressed(key);
        }
        void requestClose()
        {
            if (auto* content = dynamic_cast<MainComponent*>(getContentComponent()))
                content->requestClose([this](bool canClose) { if (canClose) juce::JUCEApplication::getInstance()->quit(); });
            else juce::JUCEApplication::getInstance()->quit();
        }
        void closeButtonPressed() override { requestClose(); }
    };
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(LibertyApplication)
