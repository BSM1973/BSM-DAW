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
                 "Affichage", "Naviguer", "IA", "Options", "Aide" };
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
                menu.addItem(6501, "Assistant musical IA");
                menu.addItem(6502, "Composition IA");
                menu.addItem(6503, "Arrangement IA");
                menu.addItem(6504, "Analyse musicale IA");
                break;
            case 7:
                menu.addItem(7001, juce::String::fromUTF8("R\xC3\xA9glages audio et MIDI..."));
                break;
            case 8:
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
class LibertyApplication final:public juce::JUCEApplication{public:const juce::String getApplicationName()override{return"Liberty";}const juce::String getApplicationVersion()override{return"0.1.0";}bool moreThanOneInstanceAllowed()override{return true;}void initialise(const juce::String&cmd)override{juce::StringArray a;a.addTokens(cmd,true);a.removeEmptyStrings();if(a.size()>=3&&a[0]=="--liberty-scan-vst3"){bool ok=LibertyPluginHost::runSingleVST3ScanHelper(a[1],juce::File(a[2]));setApplicationReturnValue(ok?0:2);quit();return;}mainWindow=std::make_unique<MainWindow>(getApplicationName());}void shutdown()override{if(mainWindow){shutdownLibertySpliceBrowserController();shutdownLibertyToolPaletteController();shutdownLibertyLanguageController();shutdownLibertyAudioSettingsRelocator();shutdownLibertyAIComposer2Controller();shutdownLibertyAISongIntelligenceController();shutdownLibertyAIArrangerController();shutdownLibertyAINoveltyController();shutdownLibertyAIAdvancedController();shutdownLibertyAIController();shutdownLibertyUILayerCoordinator();shutdownLibertyBrowserResizeController();shutdownLibertyUISymbolCleaner();shutdownLibertyPageModeCoordinator();shutdownLibertyPluginDragDropController();shutdownLibertyBrowserController();shutdownLibertyPerformController();shutdownLibertyMixConsoleController();shutdownLibertyTrackPluginInsertControls();shutdownLibertyMetronomeController();shutdownLibertyAudioClipWarpView();shutdownLibertyZoomController();shutdownLibertyTrackHeaderMixControls();shutdownLibertyGridSnapController();shutdownLibertyMultiMidiClipController();shutdownLibertyTrackColourInteraction();shutdownLibertyAudioRecordingController();shutdownLibertyMidiKeyboardRouter();shutdownLibertyMidiGroupDragInteraction();shutdownLibertyMidiNoteSelectionInteraction();shutdownLibertyMidiEditor();mainWindow.reset();LibertyPluginHost::instance().shutdown();std::_Exit(0);}}void systemRequestedQuit()override{if(mainWindow)mainWindow->requestClose();else quit();}void anotherInstanceStarted(const juce::String&)override{}private:
// First-run project chooser: custom-painted, scalable and blocking the editor
// until the user explicitly chooses a session template.
class LibertyWelcomeScreen final : public juce::Component
{
public:
    explicit LibertyWelcomeScreen(std::function<void(int,int)> chosen) : onChoose(std::move(chosen))
    {
        setInterceptsMouseClicks(true, true);
        setWantsKeyboardFocus(true);
        const juce::String names[] = { "SESSION VIDE", "2 PISTES AUDIO", "2 INSTRUMENTS", "2 AUDIO + 2 INSTRUMENTS", "8 AUDIO + 8 INSTRUMENTS" };
        const juce::String descriptions[] = {
            "Une session vierge. Construisez votre projet librement.",
            "Deux pistes audio prêtes pour enregistrer et mixer.",
            "Deux pistes d'instruments virtuels et leurs éditeurs.",
            "Deux pistes audio et deux instruments virtuels.",
            "Huit pistes audio et huit instruments virtuels."
        };
        for (int i = 0; i < 5; ++i)
        {
            auto button = std::make_unique<juce::TextButton>();
            button->setButtonText(names[i]);
            button->setTooltip(descriptions[i]);
            button->setMouseClickGrabsKeyboardFocus(false);
            button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff244653));
            button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff347483));
            button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffbff));
            button->onClick = [this,i] { if (onChoose) onChoose(i == 1 || i == 3 ? 2 : i == 4 ? 8 : 0, i == 2 || i == 3 ? 2 : i == 4 ? 8 : 0); };
            addAndMakeVisible(*button);
            buttons.push_back(std::move(button));
        }
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff09121c));
        const auto w = (float)getWidth(), h = (float)getHeight();
        juce::ColourGradient glow(juce::Colour(0xff183f50), w * .5f, h * .24f,
                                  juce::Colour(0xff09121c), w * .5f, h * .85f, true);
        g.setGradientFill(glow);
        g.fillRect(getLocalBounds());
        auto panel = getPanel();
        g.setColour(juce::Colour(0xff111f2c));
        g.fillRoundedRectangle(panel.toFloat(), 22.0f);
        g.setColour(juce::Colour(0xff395e70));
        g.drawRoundedRectangle(panel.toFloat().reduced(.6f), 22.0f, 1.2f);
        g.setColour(juce::Colour(0xff56d7e1));
        g.fillRoundedRectangle((float)panel.getX() + 36, (float)panel.getY() + 35, 5.0f, 45.0f, 2.0f);
        g.setColour(juce::Colour(0xfff1faff));
        g.setFont(juce::Font(30.0f, juce::Font::bold));
        g.drawText("LIBERTY", panel.getX()+54, panel.getY()+30, panel.getWidth()-90, 42, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff7acbd6));
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText("VOTRE STUDIO. VOTRE LIBERTÉ.", panel.getX()+39, panel.getY()+86, panel.getWidth()-78, 24, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xffe0edf3));
        g.setFont(juce::Font(22.0f, juce::Font::bold));
        g.drawText("Créer une nouvelle session", panel.getX()+39, panel.getY()+130, panel.getWidth()-78, 36, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff9db6c4));
        g.setFont(juce::Font(13.0f));
        g.drawText("Choisissez votre point de départ", panel.getX()+40, panel.getY()+170, panel.getWidth()-80, 26, juce::Justification::centredLeft);
        const juce::String titles[] = { "SESSION VIDE", "2 PISTES AUDIO", "2 INSTRUMENTS", "2 AUDIO + 2 INSTRUMENTS", "8 AUDIO + 8 INSTRUMENTS" };
        const juce::String descriptions[] = {
            "Aucune piste. Un espace entièrement libre.",
            "2 pistes audio pour enregistrer et mixer.",
            "2 pistes d'instruments virtuels pour composer.",
            "2 pistes audio et 2 instruments virtuels.",
            "8 pistes audio et 8 instruments virtuels."
        };
        for (int i=0; i<5; ++i)
        {
            auto card = cardBounds(i);
            g.setColour(juce::Colour(0xff1a2c39));
            g.fillRoundedRectangle(card.toFloat(), 12.0f);
            g.setColour(juce::Colour(0xff365364));
            g.drawRoundedRectangle(card.toFloat().reduced(.5f), 12.0f, 1.0f);
            g.setColour(juce::Colour(0xff58d3dd));
            g.fillRoundedRectangle((float)card.getX()+18, (float)card.getY()+18, 28.0f, 4.0f, 2.0f);
            g.setColour(juce::Colour(0xfff2faff));
            g.setFont(juce::Font(16.0f, juce::Font::bold));
            g.drawFittedText(titles[i], card.reduced(18).withHeight(64).withTrimmedTop(15), juce::Justification::centredLeft, 2);
            g.setColour(juce::Colour(0xffa4bcc9));
            g.setFont(juce::Font(12.0f));
            g.drawFittedText(descriptions[i], juce::Rectangle<int>(card.getX()+18,card.getY()+74,card.getWidth()-36,55),juce::Justification::topLeft,3);
        }
        g.setColour(juce::Colour(0xff6f91a2));
        g.setFont(juce::Font(11.0f));
        g.drawText("LIBERTY  |  STATION AUDIONUMÉRIQUE",panel.getX()+36,panel.getBottom()-28,panel.getWidth()-72,20,juce::Justification::centred);
    }
    void resized() override
    {
        for (int i=0;i<5;++i)
        {
            auto card=cardBounds(i);
            buttons[(size_t)i]->setBounds(card.getX()+14,card.getBottom()-48,card.getWidth()-28,36);
        }
    }
private:
    juce::Rectangle<int> getPanel() const
    {
        const int width=juce::jmin(1000, juce::jmax(540,getWidth()-48));
        const int height=juce::jmin(720,juce::jmax(540,getHeight()-48));
        return juce::Rectangle<int>(width,height).withCentre(getLocalBounds().getCentre());
    }
    juce::Rectangle<int> cardBounds(int i) const
    {
        auto p=getPanel();
        const int gap=16, margin=38;
        const int cw=(p.getWidth()-2*margin-2*gap)/3;
        return {p.getX()+margin+(i%3)*(cw+gap),p.getY()+212+(i/3)*218,cw,200};
    }
    std::function<void(int,int)> onChoose;
    std::vector<std::unique_ptr<juce::TextButton>> buttons;
};
class LibertyMainContent final : public juce::Component
{
public:
    LibertyMainContent()
    {
        editor = std::make_unique<MainComponent>();
        menuModel = std::make_unique<LibertyMainMenu>(*editor);
        menuBar = std::make_unique<juce::MenuBarComponent>(menuModel.get());
        addAndMakeVisible(*menuBar);
        addAndMakeVisible(*editor);
        setSize(editor->getWidth(), editor->getHeight() + 30);
        welcome = std::make_unique<LibertyWelcomeScreen>([this](int audio, int instruments)
        {
            if (!editor->configureStartupTracks(audio, instruments))
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Liberty", "Impossible de préparer la session.", "OK");
                return;
            }
            welcome->setVisible(false);
            // Defer destruction: this callback runs inside a welcome button's
            // onClick, so deleting the welcome component here is unsafe.
            juce::Component::SafePointer<LibertyMainContent> safeThis(this);
            juce::MessageManager::callAsync([safeThis]
            {
                if (safeThis == nullptr) return;
                safeThis->welcome.reset();
                safeThis->editor->grabKeyboardFocus();
            });
        });
        addAndMakeVisible(*welcome);
        welcome->toFront(false);
    }
    MainComponent& getEditor() const { return *editor; }
    void resized() override
    {
        constexpr int menuHeight = 30;
        menuBar->setBounds(0, 0, getWidth(), menuHeight);
        editor->setBounds(0, menuHeight, getWidth(),
                          juce::jmax(0, getHeight() - menuHeight));
        if (welcome != nullptr) { welcome->setBounds(getLocalBounds()); welcome->toFront(false); }
    }
private:
    std::unique_ptr<MainComponent> editor;
    std::unique_ptr<LibertyWelcomeScreen> welcome;
    std::unique_ptr<LibertyMainMenu> menuModel;
    std::unique_ptr<juce::MenuBarComponent> menuBar;
};
class MainWindow final : public juce::DocumentWindow, private juce::KeyListener
{
public:
    explicit MainWindow(juce::String name)
        : juce::DocumentWindow(std::move(name), juce::Colours::black,
                               juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new LibertyMainContent(), true);
        setResizable(true, true);
        // Start maximised within the usable desktop area (macOS menu bar
        // and Dock remain available), like double-clicking the title bar.
        // Do not use kiosk mode or native fullscreen / a separate Space.
        if (const auto* display = juce::Desktop::getInstance()
                                      .getDisplays().getPrimaryDisplay())
            setBounds(display->userArea);
        addKeyListener(this);
        setVisible(true);
        editor().grabKeyboardFocus();
    }
    ~MainWindow() override { removeKeyListener(this); }
    bool keyPressed(const juce::KeyPress& key, juce::Component*) override
    {
        if (handleLibertyMidiNoteSelectionKeyPress(key)
            || handleLibertyMidiQuantizeKeyPress(key))
            return true;
        return editor().keyPressed(key);
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (handleLibertyMidiNoteSelectionKeyPress(key)
            || handleLibertyMidiQuantizeKeyPress(key))
            return true;
        return editor().keyPressed(key) || juce::DocumentWindow::keyPressed(key);
    }
    void requestClose()
    {
        editor().requestClose([this](bool ok)
        {
            if (ok) juce::JUCEApplication::getInstance()->quit();
        });
    }
    void closeButtonPressed() override { requestClose(); }
private:
    MainComponent& editor() const
    {
        return static_cast<LibertyMainContent*>(getContentComponent())->getEditor();
    }
};
std::unique_ptr<MainWindow>mainWindow;};
START_JUCE_APPLICATION(LibertyApplication)