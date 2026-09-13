#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdlib>
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
void shutdownLibertyPluginDragDropController();
void shutdownLibertyMixConsoleController();
void shutdownLibertyPerformController();
void shutdownLibertyPageModeCoordinator();
void shutdownLibertyUISymbolCleaner();
void shutdownLibertySpliceBrowserController();

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
        if (mainWindow != nullptr)
        {
            // Global listeners/timers must be detached while JUCE Desktop still exists.
            shutdownLibertySpliceBrowserController();
            shutdownLibertyUISymbolCleaner();
            shutdownLibertyPageModeCoordinator();
            shutdownLibertyPluginDragDropController();

            // Stop every UI/controller object which holds a MainComponent reference.
            shutdownLibertyBrowserController();
            shutdownLibertyPerformController();
            shutdownLibertyMixConsoleController();
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

            // Destroy MainComponent so AudioEngine removes its realtime callback.
            mainWindow.reset();

            // Release every hosted AU/VST3 and editor explicitly while JUCE is alive.
            LibertyPluginHost::instance().shutdown();

            // Liberty owns several process-lifetime JUCE controller singletons. Their
            // late C++ static destructors run after JUCE's Desktop/MessageManager teardown
            // and have been the remaining source of the macOS quit crash. Everything
            // meaningful is already explicitly stopped/released above, so terminate now
            // without executing that unsafe late static-destruction phase.
            std::_Exit(0);
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
