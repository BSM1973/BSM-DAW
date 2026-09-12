#define private public
#include "MainComponent.h"
#undef private
#include "PluginHost.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <atomic>
#include <map>
#include <memory>
#include <thread>

namespace
{
constexpr int browserWidth = 320;
constexpr int topBarHeight = 76;

class BrowserPanel final : public juce::Component,
                           private juce::Timer,
                           private juce::FileBrowserListener,
                           private juce::ListBoxModel
{
public:
    explicit BrowserPanel(MainComponent& ownerIn)
        : owner(ownerIn),
          filter("*", "*", "All files"),
          directoryList(&filter, thread),
          fileTree(directoryList)
    {
        thread.startThread();

        toggleButton.setButtonText("BROWSER");
        toggleButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252a31));
        toggleButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff315f7a));
        toggleButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        toggleButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        toggleButton.setClickingTogglesState(true);
        toggleButton.setToggleState(false, juce::dontSendNotification);
        toggleButton.onClick = [this] { setBrowserOpen(toggleButton.getToggleState()); };
        owner.addAndMakeVisible(toggleButton);

        closeButton.setButtonText("×");
        closeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252a31));
        closeButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        closeButton.onClick = [this]
        {
            toggleButton.setToggleState(false, juce::dontSendNotification);
            setBrowserOpen(false);
        };
        addAndMakeVisible(closeButton);

        fileTree.setRootItemVisible(false);
        fileTree.setColour(juce::TreeView::backgroundColourId, juce::Colour(0xff111419));
        fileTree.setColour(juce::TreeView::linesColourId, juce::Colour(0xff3b424c));
        fileTree.setColour(juce::TreeView::dragAndDropIndicatorColourId, juce::Colour(0xff72d8f5));
        fileTree.addListener(this);
        addAndMakeVisible(fileTree);

        filesButton.setButtonText("FILES");
        audioButton.setButtonText("AUDIO");
        midiButton.setButtonText("MIDI");
        presetsButton.setButtonText("PRESETS");
        pluginsButton.setButtonText("PLUGINS");
        for (auto* b : { &filesButton, &audioButton, &midiButton, &presetsButton, &pluginsButton })
        {
            b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2027));
            b->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff315f7a));
            b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffc9cdd3));
            b->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
            b->setClickingTogglesState(false);
            addAndMakeVisible(*b);
        }

        filesButton.onClick = [this] { setCategory(Category::files); };
        audioButton.onClick = [this] { setCategory(Category::audio); };
        midiButton.onClick = [this] { setCategory(Category::midi); };
        presetsButton.onClick = [this] { setCategory(Category::presets); };
        pluginsButton.onClick = [this] { setCategory(Category::plugins); };

        homeButton.setButtonText("HOME");
        homeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252a31));
        homeButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        homeButton.onClick = [this] { setRoot(juce::File::getSpecialLocation(juce::File::userHomeDirectory)); };
        addAndMakeVisible(homeButton);

        pluginList.setModel(this);
        pluginList.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff111419));
        pluginList.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff3b424c));
        pluginList.setRowHeight(38);
        pluginList.setOutlineThickness(1);
        addAndMakeVisible(pluginList);

        scanPluginsButton.setButtonText("SCAN AU + VST3");
        blacklistButton.setButtonText("BLACKLIST");
        clearBlacklistButton.setButtonText("CLEAR BL");
        loadPluginButton.setButtonText("LOAD");
        openPluginButton.setButtonText("OPEN UI");
        unloadPluginButton.setButtonText("UNLOAD");
        for (auto* b : { &scanPluginsButton, &blacklistButton, &clearBlacklistButton, &loadPluginButton, &openPluginButton, &unloadPluginButton })
        {
            b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252a31));
            b->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff315f7a));
            b->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            addAndMakeVisible(*b);
        }
        scanPluginsButton.onClick = [this] { scanPlugins(); };
        blacklistButton.onClick = [this] { showBlacklist(); };
        clearBlacklistButton.onClick = [this] { clearBlacklist(); };
        loadPluginButton.onClick = [this] { loadSelectedPlugin(); };
        openPluginButton.onClick = [this] { openLoadedPluginEditor(); };
        unloadPluginButton.onClick = [this] { unloadPlugin(); };

        pluginStatus.setColour(juce::Label::textColourId, juce::Colour(0xffaab2bc));
        pluginStatus.setFont(juce::Font(10.0f));
        pluginStatus.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(pluginStatus);

        setVisible(false);
        owner.addAndMakeVisible(this);
        setRoot(juce::File::getSpecialLocation(juce::File::userHomeDirectory));
        refreshPlugins();
        setCategory(Category::files);
        startTimerHz(20);
    }

    ~BrowserPanel() override { shutdown(); }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
        if (scanThread.joinable()) scanThread.join();
        pluginList.setModel(nullptr);
        fileTree.removeListener(this);
        thread.stopThread(1500);
        setVisible(false);
        toggleButton.setVisible(false);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff101318));
        g.setColour(juce::Colour(0xff3b424c));
        g.drawVerticalLine(0, 0.0f, (float)getHeight());
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(15.0f, juce::Font::bold));
        g.drawText("BROWSER", 14, 10, 150, 24, juce::Justification::centredLeft);

        if (category != Category::plugins)
        {
            g.setColour(juce::Colour(0xff858c96));
            g.setFont(juce::Font(9.5f));
            g.drawText(rootDirectory.getFullPathName(), 14, 78, getWidth() - 28, 18, juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour(juce::Colour(0xff72d8f5));
            g.setFont(juce::Font(10.0f, juce::Font::bold));
            g.drawText("AUDIO UNIT (.component) + VST3 (.vst3)", 14, 78, getWidth() - 28, 18, juce::Justification::centredLeft, true);
        }

        g.setColour(juce::Colour(0xff20252c));
        g.fillRect(0, 100, getWidth(), 1);
        g.setColour(juce::Colour(0xff8f98a3));
        g.setFont(juce::Font(9.0f));
        g.drawText(categoryHint(), 14, getHeight() - 29, getWidth() - 28, 18, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        closeButton.setBounds(getWidth() - 38, 8, 28, 26);
        filesButton.setBounds(8, 42, 52, 26);
        audioButton.setBounds(62, 42, 52, 26);
        midiButton.setBounds(116, 42, 46, 26);
        presetsButton.setBounds(164, 42, 66, 26);
        pluginsButton.setBounds(232, 42, 80, 26);

        homeButton.setBounds(12, 105, 70, 24);
        fileTree.setBounds(10, 136, getWidth() - 20, juce::jmax(40, getHeight() - 174));

        scanPluginsButton.setBounds(10, 108, getWidth() - 20, 28);
        blacklistButton.setBounds(10, 140, 145, 26);
        clearBlacklistButton.setBounds(159, 140, 151, 26);
        pluginList.setBounds(10, 172, getWidth() - 20, juce::jmax(40, getHeight() - 274));
        const int controlsY = getHeight() - 94;
        loadPluginButton.setBounds(10, controlsY, 72, 28);
        openPluginButton.setBounds(86, controlsY, 72, 28);
        unloadPluginButton.setBounds(162, controlsY, 76, 28);
        pluginStatus.setBounds(10, controlsY + 31, getWidth() - 20, 28);
    }

private:
    enum class Category { files, audio, midi, presets, plugins };

    void selectionChanged() override {}
    void fileClicked(const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked(const juce::File& file) override { openFileOrDirectory(file); }
    void browserRootChanged(const juce::File&) override {}

    int getNumRows() override { return pluginDescriptions.size(); }

    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override
    {
        if (rowNumber < 0 || rowNumber >= pluginDescriptions.size()) return;
        const auto& d = pluginDescriptions.getReference(rowNumber);
        if (rowIsSelected)
        {
            g.setColour(juce::Colour(0xff244f63));
            g.fillRect(0, 0, width, height);
        }
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(d.name, 8, 3, width - 16, 17, juce::Justification::centredLeft, true);
        g.setColour(juce::Colour(0xff8f98a3));
        g.setFont(juce::Font(9.0f));
        const auto kind = d.isInstrument ? "INSTRUMENT" : "FX";
        g.drawText(d.pluginFormatName + " • " + kind + " • " + d.manufacturerName,
                   8, 20, width - 16, 14, juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged(int) override { if (!scanning.load()) refreshPluginStatus(); }
    void listBoxItemDoubleClicked(int, const juce::MouseEvent&) override { if (!scanning.load()) loadSelectedPlugin(); }

    void setBrowserOpen(bool shouldOpen)
    {
        browserOpen = shouldOpen;
        setVisible(browserOpen);
        if (browserOpen) toFront(false);
        toggleButton.toFront(false);
        adjustHostWindow(browserOpen);
        owner.repaint();
    }

    void adjustHostWindow(bool opening)
    {
        auto* window = owner.findParentComponentOfClass<juce::DocumentWindow>();
        if (window == nullptr) return;
        auto bounds = window->getBounds();
        if (opening)
        {
            if (hostExpanded) return;
            originalWindowBounds = bounds;
            auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(bounds);
            if (display != nullptr)
            {
                const auto usable = display->userArea;
                int wantedRight = bounds.getRight() + browserWidth;
                int newX = bounds.getX();
                if (wantedRight > usable.getRight()) newX = juce::jmax(usable.getX(), bounds.getX() - (wantedRight - usable.getRight()));
                const int newWidth = juce::jmin(usable.getWidth(), bounds.getWidth() + browserWidth);
                window->setBounds(newX, bounds.getY(), newWidth, bounds.getHeight());
            }
            else window->setSize(bounds.getWidth() + browserWidth, bounds.getHeight());
            hostExpanded = true;
        }
        else if (hostExpanded)
        {
            if (!originalWindowBounds.isEmpty()) window->setBounds(originalWindowBounds);
            hostExpanded = false;
        }
    }

    void setRoot(const juce::File& directory)
    {
        if (!directory.isDirectory()) return;
        rootDirectory = directory;
        directoryList.setDirectory(rootDirectory, true, true);
        fileTree.refresh();
        repaint();
    }

    void setCategory(Category newCategory)
    {
        category = newCategory;
        filesButton.setToggleState(category == Category::files, juce::dontSendNotification);
        audioButton.setToggleState(category == Category::audio, juce::dontSendNotification);
        midiButton.setToggleState(category == Category::midi, juce::dontSendNotification);
        presetsButton.setToggleState(category == Category::presets, juce::dontSendNotification);
        pluginsButton.setToggleState(category == Category::plugins, juce::dontSendNotification);

        const bool pluginMode = category == Category::plugins;
        fileTree.setVisible(!pluginMode);
        homeButton.setVisible(!pluginMode);
        pluginList.setVisible(pluginMode);
        scanPluginsButton.setVisible(pluginMode);
        blacklistButton.setVisible(pluginMode);
        clearBlacklistButton.setVisible(pluginMode);
        loadPluginButton.setVisible(pluginMode);
        openPluginButton.setVisible(pluginMode);
        unloadPluginButton.setVisible(pluginMode);
        pluginStatus.setVisible(pluginMode);
        if (pluginMode && !scanning.load()) refreshPlugins();
        repaint();
    }

    bool fileMatchesCategory(const juce::File& file) const
    {
        if (file.isDirectory() || category == Category::files) return true;
        const auto ext = file.getFileExtension().toLowerCase();
        if (category == Category::audio) return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".mp3" || ext == ".flac";
        if (category == Category::midi) return ext == ".mid" || ext == ".midi";
        if (category == Category::presets) return ext == ".xml" || ext == ".fxp" || ext == ".vstpreset" || ext == ".aupreset";
        return false;
    }

    juce::String categoryHint() const
    {
        switch (category)
        {
            case Category::audio: return "Double-clic sur WAV/AIFF pour charger sur la piste Audio sélectionnée";
            case Category::midi: return "Fichiers MIDI";
            case Category::presets: return "Presets XML / FXP / VSTPreset / AUPreset";
            case Category::plugins: return "Crash au scan → blacklist automatique au prochain lancement";
            default: return "Navigation fichiers et dossiers";
        }
    }

    void openFileOrDirectory(const juce::File& file)
    {
        if (file == juce::File{} || !file.exists()) return;
        if (file.isDirectory()) { setRoot(file); return; }
        if (!fileMatchesCategory(file)) return;
        const auto ext = file.getFileExtension().toLowerCase();
        const bool audio = ext == ".wav" || ext == ".aif" || ext == ".aiff";
        if (!audio) return;
        int track = owner.selectedTrack;
        if (track < 0 || track >= AudioEngine::maxAudioTracks) track = 0;
        juce::String error;
        if (!owner.audioEngine.loadAudioFileIntoTrack(track, file, error))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Browser", error, "OK");
            return;
        }
        owner.trackSourceFiles[(size_t)track] = file;
        owner.audioEngine.setTrackStartSeconds(track, juce::jmax(0.0, owner.playheadSeconds));
        owner.selectedTrack = track;
        owner.rebuildWaveformCache(track);
        owner.repaint();
    }

    void refreshPlugins()
    {
        pluginDescriptions = LibertyPluginHost::instance().getPluginDescriptions();
        pluginList.updateContent();
        refreshPluginStatus();
    }

    void setScanStatus(const juce::String& text)
    {
        const juce::ScopedLock scoped(scanStatusLock);
        scanStatusText = text;
    }

    juce::String getScanStatus() const
    {
        const juce::ScopedLock scoped(scanStatusLock);
        return scanStatusText;
    }

    void scanPlugins()
    {
        if (scanning.exchange(true)) return;
        if (scanThread.joinable()) scanThread.join();

        scanFinishedPending.store(false);
        scanPluginsButton.setEnabled(false);
        loadPluginButton.setEnabled(false);
        openPluginButton.setEnabled(false);
        unloadPluginButton.setEnabled(false);
        blacklistButton.setEnabled(false);
        clearBlacklistButton.setEnabled(false);
        setScanStatus("Préparation du scan AU + VST3…");
        pluginStatus.setText(getScanStatus(), juce::dontSendNotification);

        scanThread = std::thread([this]
        {
            auto& host = LibertyPluginHost::instance();
            host.scanInstalledPlugins([this](const juce::String& formatName,
                                             const juce::String& pluginName,
                                             float progress)
            {
                if (stopped.load()) return;
                if (pluginName.isEmpty())
                {
                    setScanStatus("Finalisation du scan…");
                    return;
                }
                const int percent = juce::jlimit(0, 100, juce::roundToInt(progress * 100.0f));
                setScanStatus("SCAN " + formatName + "  " + juce::String(percent) + "%  •  " + pluginName);
            });

            if (!stopped.load())
            {
                scanning.store(false);
                scanFinishedPending.store(true);
            }
        });
    }

    void showBlacklist()
    {
        auto& host = LibertyPluginHost::instance();
        const auto entries = host.getBlacklistedPlugins();
        juce::String message;
        message << "Dossier :\n" << host.getBlacklistFolder().getFullPathName() << "\n\n";
        if (entries.isEmpty())
            message << "Aucun plugin blacklisté.";
        else
        {
            message << juce::String(entries.size()) << " plugin(s) blacklisté(s) :\n\n";
            for (const auto& entry : entries) message << entry << "\n";
        }
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Liberty - Blacklist plugins", message, "OK");
    }

    void clearBlacklist()
    {
        LibertyPluginHost::instance().clearBlacklist();
        pluginStatus.setText("Blacklist vidée. Relance SCAN pour retester.", juce::dontSendNotification);
    }

    int selectedPluginRow() const { return pluginList.getSelectedRow(); }

    void loadSelectedPlugin()
    {
        if (scanning.load()) return;
        const int row = selectedPluginRow();
        if (row < 0 || row >= pluginDescriptions.size()) return;
        const auto description = pluginDescriptions.getReference(row);
        auto& host = LibertyPluginHost::instance();
        juce::String error;
        bool ok = false;

        if (description.isInstrument)
        {
            ok = host.loadInstrument(description, error);
            if (ok) host.showInstrumentEditor();
        }
        else
        {
            int track = owner.selectedTrack;
            if (track < 0 || track >= AudioEngine::maxAudioTracks) track = 0;
            ok = host.loadEffectForTrack(track, description, error);
            if (ok) host.showEditorForTrack(track);
        }

        if (!ok)
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Plugin", error, "OK");
        refreshPluginStatus();
    }

    void openLoadedPluginEditor()
    {
        if (scanning.load()) return;
        const int row = selectedPluginRow();
        if (row >= 0 && row < pluginDescriptions.size() && pluginDescriptions.getReference(row).isInstrument)
        {
            LibertyPluginHost::instance().showInstrumentEditor();
            return;
        }
        int track = owner.selectedTrack;
        if (track < 0 || track >= AudioEngine::maxAudioTracks) track = 0;
        LibertyPluginHost::instance().showEditorForTrack(track);
    }

    void unloadPlugin()
    {
        if (scanning.load()) return;
        const int row = selectedPluginRow();
        if (row >= 0 && row < pluginDescriptions.size() && pluginDescriptions.getReference(row).isInstrument)
            LibertyPluginHost::instance().unloadInstrument();
        else
        {
            int track = owner.selectedTrack;
            if (track < 0 || track >= AudioEngine::maxAudioTracks) track = 0;
            LibertyPluginHost::instance().unloadEffectForTrack(track);
        }
        refreshPluginStatus();
    }

    void refreshPluginStatus()
    {
        if (scanning.load())
        {
            pluginStatus.setText(getScanStatus(), juce::dontSendNotification);
            return;
        }
        auto& host = LibertyPluginHost::instance();
        int track = owner.selectedTrack;
        if (track < 0 || track >= AudioEngine::maxAudioTracks) track = 0;
        juce::String text;
        if (host.hasEffectForTrack(track)) text << "A" << (track + 1) << ": " << host.getEffectName(track) << "   ";
        if (host.hasInstrument()) text << "INST: " << host.getInstrumentName();
        if (text.isEmpty())
            text = juce::String(pluginDescriptions.size()) + " plugins • "
                 + juce::String(host.getBlacklistedPlugins().size()) + " blacklistés";
        pluginStatus.setText(text, juce::dontSendNotification);
    }

    void timerCallback() override
    {
        if (stopped.load()) return;
        const int panelX = juce::jmax(0, owner.getWidth() - browserWidth);
        if (browserOpen)
        {
            const auto wanted = juce::Rectangle<int>(panelX, topBarHeight, juce::jmin(browserWidth, owner.getWidth()), juce::jmax(1, owner.getHeight() - topBarHeight));
            if (getBounds() != wanted) setBounds(wanted);
            if (!isVisible()) setVisible(true);
            toFront(false);
        }
        const int buttonX = browserOpen ? juce::jmax(8, panelX - 104) : juce::jmax(8, owner.getWidth() - 112);
        const auto buttonBounds = juce::Rectangle<int>(buttonX, 8, 96, 26);
        if (toggleButton.getBounds() != buttonBounds) toggleButton.setBounds(buttonBounds);
        toggleButton.toFront(false);

        if (category == Category::plugins)
        {
            if (scanning.load())
            {
                pluginStatus.setText(getScanStatus(), juce::dontSendNotification);
            }
            else if (scanFinishedPending.exchange(false))
            {
                if (scanThread.joinable()) scanThread.join();
                refreshPlugins();
                scanPluginsButton.setEnabled(true);
                loadPluginButton.setEnabled(true);
                openPluginButton.setEnabled(true);
                unloadPluginButton.setEnabled(true);
                blacklistButton.setEnabled(true);
                clearBlacklistButton.setEnabled(true);
                pluginStatus.setText(juce::String(pluginDescriptions.size()) + " plugins trouvés • "
                                     + juce::String(LibertyPluginHost::instance().getBlacklistedPlugins().size())
                                     + " blacklistés", juce::dontSendNotification);
            }
            else
            {
                refreshPluginStatus();
            }
        }
    }

    MainComponent& owner;
    juce::TimeSliceThread thread { "Liberty Browser" };
    juce::WildcardFileFilter filter;
    juce::DirectoryContentsList directoryList;
    juce::FileTreeComponent fileTree;
    juce::ListBox pluginList;
    juce::Array<juce::PluginDescription> pluginDescriptions;
    juce::TextButton toggleButton, closeButton, filesButton, audioButton, midiButton, presetsButton, pluginsButton, homeButton;
    juce::TextButton scanPluginsButton, blacklistButton, clearBlacklistButton, loadPluginButton, openPluginButton, unloadPluginButton;
    juce::Label pluginStatus;
    juce::File rootDirectory;
    Category category = Category::files;
    std::atomic<bool> stopped { false };
    std::atomic<bool> scanning { false };
    std::atomic<bool> scanFinishedPending { false };
    mutable juce::CriticalSection scanStatusLock;
    juce::String scanStatusText;
    std::thread scanThread;
    bool browserOpen = false, hostExpanded = false;
    juce::Rectangle<int> originalWindowBounds;
};

std::map<MainComponent*, std::unique_ptr<BrowserPanel>> browsers;
class Bootstrap final : private juce::Timer
{
public:
    Bootstrap() { startTimerHz(10); }
    ~Bootstrap() override { shutdown(); }
    void shutdown() { stopTimer(); for (auto& item : browsers) if (item.second) item.second->shutdown(); browsers.clear(); }
private:
    void timerCallback() override
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* window = dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i)))
                if (auto* main = dynamic_cast<MainComponent*>(window->getContentComponent()))
                    if (browsers.find(main) == browsers.end()) browsers.emplace(main, std::make_unique<BrowserPanel>(*main));
    }
};
Bootstrap bootstrap;
}

void shutdownLibertyBrowserController() { bootstrap.shutdown(); }
