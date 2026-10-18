#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdlib>
#include "MainComponent.h"
#include "MidiEditor.h"
#include "PluginHost.h"
bool handleLibertyMidiNoteSelectionKeyPress(const juce::KeyPress& key); bool handleLibertyMidiQuantizeKeyPress(const juce::KeyPress& key);
void shutdownLibertyMidiKeyboardRouter(); void shutdownLibertyMidiGroupDragInteraction(); void shutdownLibertyAudioRecordingController(); void shutdownLibertyTrackColourInteraction(); void shutdownLibertyMultiMidiClipController(); void shutdownLibertyGridSnapController(); void shutdownLibertyTrackHeaderMixControls(); void shutdownLibertyZoomController(); void shutdownLibertyAudioClipWarpView(); void shutdownLibertyMetronomeController(); void shutdownLibertyBrowserController(); void shutdownLibertyBrowserResizeController(); void shutdownLibertyUILayerCoordinator(); void shutdownLibertyAudioSettingsRelocator(); void shutdownLibertyLanguageController(); void shutdownLibertyToolPaletteController(); void shutdownLibertyAIAdvancedController(); void shutdownLibertyAINoveltyController(); void shutdownLibertyAIArrangerController(); void shutdownLibertyAISongIntelligenceController(); void shutdownLibertyAIComposer2Controller(); void shutdownLibertyAIController(); void shutdownLibertyTrackPluginInsertControls(); void shutdownLibertyPluginDragDropController(); void shutdownLibertyMixConsoleController(); void shutdownLibertyPerformController(); void shutdownLibertyPageModeCoordinator(); void shutdownLibertyUISymbolCleaner(); void shutdownLibertySpliceBrowserController();
class LibertyMainMenu final : public juce::MenuBarModel
{
public:
    explicit LibertyMainMenu(MainComponent& component) : owner(component) {}
    juce::StringArray getMenuBarNames() override
    {
        return { "Fichier", juce::String::fromUTF8("\xC3\x89" "dition"),
                 juce::String::fromUTF8("Cr\xC3\xA9" "er"), "Lecture",
                 "Affichage", "Naviguer", "Options", "Aide" };
    }
    juce::PopupMenu getMenuForIndex(int index, const juce::String&) override
    {
        juce::PopupMenu menu;
        switch (index)
        {
            case 0:
                menu.addItem(1001, "Nouveau projet");
                menu.addItem(1002, "Ouvrir un projet...");
                menu.addSeparator();
                menu.addItem(1003, "Enregistrer");
                menu.addItem(1004, "Enregistrer sous...");
                break;
            case 1:
                menu.addItem(2001, "Annuler", false, false);
                menu.addItem(2002, juce::String::fromUTF8("R\xC3\xA9tablir"), false, false);
                break;
            case 2:
                menu.addItem(3001, juce::String::fromUTF8("Cr\xC3\xA9" "er une piste audio"), false, false);
                menu.addItem(3002, juce::String::fromUTF8("Cr\xC3\xA9" "er une piste instrument"), false, false);
                break;
            case 3:
                menu.addItem(4001, owner.isAudioPlaying() ? "Pause" : "Lecture");
                menu.addItem(4002, "Stop");
                break;
            case 4:
                menu.addItem(5001, "Mixeur", false, false);
                menu.addItem(5002, juce::String::fromUTF8("S\xC3\xA9quenceur"), false, false);
                break;
            case 5:
                menu.addItem(6001, "Aller au d\xC3\xA9" "but", false, false);
                break;
            case 6:
                menu.addItem(7001, juce::String::fromUTF8("R\xC3\xA9glages audio..."));
                break;
            case 7:
                menu.addItem(8001, juce::String::fromUTF8("\xC3\x80 propos de Liberty"));
                break;
            default: break;
        }
        return menu;
    }
    void menuItemSelected(int commandId, int) override
    {
        owner.performMainMenuAction(commandId);
    }
private:
    MainComponent& owner;
};
class LibertyApplication final:public juce::JUCEApplication{public:const juce::String getApplicationName()override{return"Liberty";}const juce::String getApplicationVersion()override{return"0.1.0";}bool moreThanOneInstanceAllowed()override{return true;}void initialise(const juce::String&cmd)override{juce::StringArray a;a.addTokens(cmd,true);a.removeEmptyStrings();if(a.size()>=3&&a[0]=="--liberty-scan-vst3"){bool ok=LibertyPluginHost::runSingleVST3ScanHelper(a[1],juce::File(a[2]));setApplicationReturnValue(ok?0:2);quit();return;}mainWindow=std::make_unique<MainWindow>(getApplicationName());}void shutdown()override{if(mainWindow){shutdownLibertySpliceBrowserController();shutdownLibertyToolPaletteController();shutdownLibertyLanguageController();shutdownLibertyAudioSettingsRelocator();shutdownLibertyAIComposer2Controller();shutdownLibertyAISongIntelligenceController();shutdownLibertyAIArrangerController();shutdownLibertyAINoveltyController();shutdownLibertyAIAdvancedController();shutdownLibertyAIController();shutdownLibertyUILayerCoordinator();shutdownLibertyBrowserResizeController();shutdownLibertyUISymbolCleaner();shutdownLibertyPageModeCoordinator();shutdownLibertyPluginDragDropController();shutdownLibertyBrowserController();shutdownLibertyPerformController();shutdownLibertyMixConsoleController();shutdownLibertyTrackPluginInsertControls();shutdownLibertyMetronomeController();shutdownLibertyAudioClipWarpView();shutdownLibertyZoomController();shutdownLibertyTrackHeaderMixControls();shutdownLibertyGridSnapController();shutdownLibertyMultiMidiClipController();shutdownLibertyTrackColourInteraction();shutdownLibertyAudioRecordingController();shutdownLibertyMidiKeyboardRouter();shutdownLibertyMidiGroupDragInteraction();shutdownLibertyMidiNoteSelectionInteraction();shutdownLibertyMidiEditor();mainWindow.reset();LibertyPluginHost::instance().shutdown();std::_Exit(0);}}void systemRequestedQuit()override{if(mainWindow)mainWindow->requestClose();else quit();}void anotherInstanceStarted(const juce::String&)override{}private:class MainWindow final:public juce::DocumentWindow,private juce::KeyListener{public:explicit MainWindow(juce::String n):DocumentWindow(std::move(n),juce::Colours::black,DocumentWindow::allButtons){setUsingNativeTitleBar(true);setContentOwned(new MainComponent(),true);
#if JUCE_MAC
if(auto* c=dynamic_cast<MainComponent*>(getContentComponent())) {
    mainMenu = std::make_unique<LibertyMainMenu>(*c);
    juce::MenuBarModel::setMacMainMenu(mainMenu.get());
}
#else
if(auto* c=dynamic_cast<MainComponent*>(getContentComponent())) {
    mainMenu = std::make_unique<LibertyMainMenu>(*c);
    menuBar = std::make_unique<juce::MenuBarComponent>(mainMenu.get());
    addAndMakeVisible(*menuBar);
    menuBar->setBounds(0, 0, getWidth(), 24);
}
#endif
centreWithSize(getWidth(),getHeight());setResizable(true,true);addKeyListener(this);setVisible(true);if(auto*c=dynamic_cast<MainComponent*>(getContentComponent()))c->grabKeyboardFocus();}~MainWindow()override{
#if JUCE_MAC
juce::MenuBarModel::setMacMainMenu(nullptr);
#endif
menuBar.reset();
mainMenu.reset();
removeKeyListener(this);
}bool keyPressed(const juce::KeyPress&k,juce::Component*)override{if(handleLibertyMidiNoteSelectionKeyPress(k)||handleLibertyMidiQuantizeKeyPress(k))return true;if(auto*c=dynamic_cast<MainComponent*>(getContentComponent()))if(c->keyPressed(k))return true;return true;}bool keyPressed(const juce::KeyPress&k)override{if(handleLibertyMidiNoteSelectionKeyPress(k)||handleLibertyMidiQuantizeKeyPress(k))return true;if(auto*c=dynamic_cast<MainComponent*>(getContentComponent()))if(c->keyPressed(k))return true;return DocumentWindow::keyPressed(k);}void requestClose(){if(auto*c=dynamic_cast<MainComponent*>(getContentComponent()))c->requestClose([this](bool ok){if(ok)juce::JUCEApplication::getInstance()->quit();});else juce::JUCEApplication::getInstance()->quit();}void closeButtonPressed()override{requestClose();}
private:
std::unique_ptr<LibertyMainMenu> mainMenu;
std::unique_ptr<juce::MenuBarComponent> menuBar;
};std::unique_ptr<MainWindow>mainWindow;};
START_JUCE_APPLICATION(LibertyApplication)