#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"
#include "MidiEditor.h"
#include "PluginHost.h"

// MIDI note editing shortcuts are routed through MainWindow, just like the
// existing project Save/Open shortcuts. This avoids relying on focus delivery
// to a child editor component on macOS.
bool handleLibertyMidiNoteSelectionKeyPress(const juce::KeyPress& key);
bool handleLibertyMidiQuantizeKeyPress(const juce::KeyPress& key);
void shutdownLibertyMidiKeyboardRouter();
void shutdownLibertyMidiGroupDragInteraction();
void shutdownLibertyAudioRecordingController();
void shutdownLibertyTrackColourInteraction();
void shutdownLibertyMultiMidiClipController();
void shutdownLibertyGridSnapController();
void shutdownLibertyTrackHeaderMixControls();
void shutdownLibertyZoomController();
void shutdownLibertyAudioClipWarpView();
void shutdownLibertyMetronomeController();
void shutdownLibertyBrowserController();
void shutdownLibertyTrackPluginInsertControls();

class LibertyApplication final : public juce::JUCEApplication
{
public:
    LibertyApplication() = default;
    const juce::String getApplicationName() override { return "Liberty"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override
    {
        juce::StringArray args;
        args.addTokens(commandLine, true);
        args.removeEmptyStrings();

        if (args.size() >= 3 && args[0] == "--liberty-scan-vst3")
        {
            const juce::File resultFile(args[2]);
            const bool ok = LibertyPluginHost::runSingleVST3ScanHelper(args[1], resultFile);
            setApplicationReturnValue(ok ? 0 : 2);
            quit();
            return;
        }

        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        // A hidden VST3 scanner child never creates the main window or the
        // browser/controllers, so only tear the full app down when it exists.
        if (mainWindow != nullptr)
        {
            // First stop every UI/controller object which holds a MainComponent reference.
            shutdownLibertyBrowserController();
            shutdownLibertyTrackPluginInsertControls();
            shutdownLibertyMetronomeController();
            shutdownLibertyAudioClipWarpView();
            shutdownLibertyZoomController();
            shutdownLibertyTrackHeaderMixControls();
            shutdownLibertyGridSnapController();
            shutdownLibertyMultiMidiClipController();
            shutdownLibertyTrackColourInteraction();
            shutdownLibertyAudioRecordingController();
            shutdownLibertyMidiKeyboardRouter();
            shutdownLibertyMidiGroupDragInteraction();
            shutdownLibertyMidiNoteSelectionInteraction();
            shutdownLibertyMidiEditor();

            // Destroy MainComponent next. This stops AudioEngine and its realtime callback
            // before any hosted AU/VST3 instance is released. Some plugins crash if their
            // releaseResources/destructor runs while the host audio callback is still alive.
            mainWindow.reset();

            // Hosted plugin editors/processors are now destroyed with no audio callback racing them.
            LibertyPluginHost::instance().shutdown();
        }
    }

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
            if (handleLibertyMidiNoteSelectionKeyPress(key)) return true;
            if (handleLibertyMidiQuantizeKeyPress(key)) return true;
            if (auto* content = dynamic_cast<MainComponent*>(getContentComponent())) if (content->keyPressed(key)) return true;
            return true;
        }
        bool keyPressed(const juce::KeyPress& key) override
        {
            if (handleLibertyMidiNoteSelectionKeyPress(key)) return true;
            if (handleLibertyMidiQuantizeKeyPress(key)) return true;
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