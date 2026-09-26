#define private public
#include "MainComponent.h"
#undef private
#include "PluginHost.h"
#include "OneKnobEffects.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <thread>

int getLibertyPreferredBrowserWidth(MainComponent* owner);

namespace
{
constexpr int topBarHeight = 76;

class PluginTreeItem final : public juce::TreeViewItem
{
public:
    enum class Kind { root, manufacturer, plugin };

    PluginTreeItem(Kind kindIn,
                   juce::String labelIn,
                   const juce::PluginDescription* descriptionIn = nullptr,
                   std::function<void(const juce::PluginDescription&)> activateIn = {},
                   std::function<bool(const juce::PluginDescription&)> isFavouriteIn = {},
                   std::function<void(const juce::PluginDescription&)> toggleFavouriteIn = {})
        : kind(kindIn), label(std::move(labelIn)), activate(std::move(activateIn)),
          isFavourite(std::move(isFavouriteIn)), toggleFavourite(std::move(toggleFavouriteIn))
    {
        if (descriptionIn != nullptr) description = *descriptionIn;
    }

    bool mightContainSubItems() override { return kind != Kind::plugin; }
    bool canBeSelected() const override { return kind == Kind::plugin; }
    int getItemHeight() const override { return kind == Kind::manufacturer ? 28 : (kind == Kind::plugin ? 38 : 20); }
    juce::String getUniqueName() const override { return kind == Kind::plugin ? description.createIdentifierString() : label; }

    void paintItem(juce::Graphics& g, int width, int height) override
    {
        if (kind == Kind::root) return;
        if (kind == Kind::manufacturer)
        {
            const bool fav = label == "FAVORIS";
            g.setColour(fav ? juce::Colour(0xff2a2415) : juce::Colour(0xff1b222a));
            g.fillRect(0, 0, width, height);
            g.setColour(fav ? juce::Colour(0xffffc857) : juce::Colour(0xff72d8f5));
            g.setFont(juce::Font(11.0f, juce::Font::bold));
            g.drawText(label, 4, 0, width - 8, height, juce::Justification::centredLeft, true);
            return;
        }

        if (isSelected())
        {
            g.setColour(juce::Colour(0xff244f63));
            g.fillRect(0, 0, width, height);
        }

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(description.name, 4, 3, width - 8, 17, juce::Justification::centredLeft, true);
        g.setColour(juce::Colour(0xff8f98a3));
        g.setFont(juce::Font(9.0f));
        g.drawText(description.pluginFormatName + "  " + (description.isInstrument ? "INSTRUMENT" : "FX"),
                   4, 20, width - 8, 14, juce::Justification::centredLeft, true);
    }

    void itemClicked(const juce::MouseEvent& event) override
    {
        if (kind == Kind::plugin && event.mods.isRightButtonDown() && toggleFavourite)
            toggleFavourite(description);
    }

    void itemDoubleClicked(const juce::MouseEvent&) override
    {
        if (kind == Kind::manufacturer) { setOpen(!isOpen()); return; }
        if (kind == Kind::plugin && activate) activate(description);
    }

    const juce::PluginDescription* getPluginDescription() const noexcept
    {
        return kind == Kind::plugin ? &description : nullptr;
    }

private:
    Kind kind;
    juce::String label;
    juce::PluginDescription description;
    std::function<void(const juce::PluginDescription&)> activate;
    std::function<bool(const juce::PluginDescription&)> isFavourite;
    std::function<void(const juce::PluginDescription&)> toggleFavourite;
};

class BrowserPanel final : public juce::Component,
                           private juce::Timer,
                           private juce::FileBrowserListener
{
public:
    explicit BrowserPanel(MainComponent& ownerIn)
        : owner(ownerIn), filter("*", "*", "All files"), directoryList(&filter, thread), fileTree(directoryList)
    {
        setAlwaysOnTop(true);
        thread.startThread();
        loadFavourites();

        toggleButton.setButtonText("BROWSER");
        toggleButton.setAlwaysOnTop(true);
        toggleButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252a31));
        toggleButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff315f7a));
        toggleButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        toggleButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        toggleButton.setClickingTogglesState(true);
        toggleButton.setToggleState(false, juce::dontSendNotification);
        toggleButton.onClick = [this] { setBrowserOpen(toggleButton.getToggleState()); };
        owner.addAndMakeVisible(toggleButton);

        closeButton.setButtonText("CLOSE");
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

        pluginTree.setRootItemVisible(false);
        pluginTree.setMultiSelectEnabled(false);
        pluginTree.setColour(juce::TreeView::backgroundColourId, juce::Colour(0xff111419));
        pluginTree.setColour(juce::TreeView::linesColourId, juce::Colour(0xff303842));
        pluginTree.setColour(juce::TreeView::dragAndDropIndicatorColourId, juce::Colour(0xff72d8f5));
        addAndMakeVisible(pluginTree);

        spliceTitle.setText("SPLICE", juce::dontSendNotification);
        spliceTitle.setColour(juce::Label::textColourId, juce::Colours::white);
        spliceTitle.setFont(juce::Font(14.0f, juce::Font::bold));
        addAndMakeVisible(spliceTitle);
        spliceInfo.setText("Splice Sounds - charge le plugin officiel AU/VST3 dans Liberty pour l'interface complète.", juce::dontSendNotification);
        spliceInfo.setColour(juce::Label::textColourId, juce::Colour(0xff9aa3ad));
        spliceInfo.setFont(juce::Font(10.0f));
        spliceInfo.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(spliceInfo);
        spliceOpenPluginButton.setButtonText("OUVRIR SPLICE SOUNDS");
        spliceOpenPluginButton.onClick = [this] { openSpliceSounds(); };
        addAndMakeVisible(spliceOpenPluginButton);
        spliceScanButton.setButtonText("SCAN SPLICE");
        spliceScanButton.onClick = [this] { scanPlugins(); };
        addAndMakeVisible(spliceScanButton);

        filesButton.setButtonText("FILES");
        audioButton.setButtonText("AUDIO");
        midiButton.setButtonText("MIDI");
        presetsButton.setButtonText("PRESETS");
        pluginsButton.setButtonText("PLUGINS");
        spliceButton.setButtonText("SPLICE");
        for (auto* b : { &filesButton, &audioButton, &midiButton, &presetsButton, &pluginsButton, &spliceButton })
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
        spliceButton.onClick = [this] { setCategory(Category::splice); };

        homeButton.setButtonText("HOME");
        homeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252a31));
        homeButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        homeButton.onClick = [this] { setRoot(juce::File::getSpecialLocation(juce::File::userHomeDirectory)); };
        addAndMakeVisible(homeButton);

        scanPluginsButton.setButtonText("SCAN AU + VST3");
        blacklistButton.setButtonText("BLACKLIST");
        clearBlacklistButton.setButtonText("CLEAR BL");
        favouritePluginButton.setButtonText("FAV");
        loadPluginButton.setButtonText("LOAD");
        openPluginButton.setButtonText("OPEN UI");
        unloadPluginButton.setButtonText("UNLOAD");
        for (auto* b : { &scanPluginsButton, &blacklistButton, &clearBlacklistButton, &favouritePluginButton,
                         &loadPluginButton, &openPluginButton, &unloadPluginButton })
        {
            b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252a31));
            b->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            addAndMakeVisible(*b);
        }

        scanPluginsButton.onClick = [this] { scanPlugins(); };
        blacklistButton.onClick = [this] { showBlacklist(); };
        clearBlacklistButton.onClick = [this] { clearBlacklist(); };
        favouritePluginButton.onClick = [this] { toggleSelectedFavourite(); };
        loadPluginButton.onClick = [this] { loadSelectedPlugin(); };
        openPluginButton.onClick = [this] { openLoadedPluginEditor(); };
        unloadPluginButton.onClick = [this] { unloadPlugin(); };

        chorusButton.setButtonText("1K CHORUS");
        flangerButton.setButtonText("1K FLANGER");
        phaserButton.setButtonText("1K PHASER");
        tremoloButton.setButtonText("1K TREMOLO");
        reverbButton.setButtonText("1K REVERB");
        delayButton.setButtonText("1K DELAY");
        driveButton.setButtonText("1K DRIVE");
        compressorButton.setButtonText("1K COMP");
        saturationButton.setButtonText("1K SAT");
        widthButton.setButtonText("1K WIDTH");
        filterButton.setButtonText("1K FILTER");
        doublerButton.setButtonText("1K DOUBLER");
        exciterButton.setButtonText("1K EXCITER"); deEsserButton.setButtonText("1K DE-ESS");
        gateButton.setButtonText("1K GATE"); bassBoostButton.setButtonText("1K BASS");
        airButton.setButtonText("1K AIR"); punchButton.setButtonText("1K PUNCH"); softClipButton.setButtonText("1K CLIP");
        clearOneKnobButton.setButtonText("1K OFF");
        for (auto* b : { &chorusButton, &flangerButton, &phaserButton, &tremoloButton, &reverbButton, &delayButton, &driveButton, &compressorButton, &saturationButton, &widthButton, &filterButton, &doublerButton, &exciterButton, &deEsserButton, &gateButton, &bassBoostButton, &airButton, &punchButton, &softClipButton, &clearOneKnobButton })
        {
            b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff16303b));
            b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xff72d8f5));
            addAndMakeVisible(*b);
        }
        chorusButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::chorus); };
        flangerButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::flanger); };
        phaserButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::phaser); };
        tremoloButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::tremolo); };
        reverbButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::reverb); };
        delayButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::delay); };
        driveButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::drive); };
        compressorButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::compressor); };
        saturationButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::saturation); };
        widthButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::stereoWidth); };
        filterButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::filter); };
        doublerButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::doubler); };
        exciterButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::exciter); };
        deEsserButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::deEsser); };
        gateButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::gate); };
        bassBoostButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::bassBoost); };
        airButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::air); };
        punchButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::punch); };
        softClipButton.onClick = [this] { loadOneKnob(LibertyOneKnobRack::Type::softClip); };
        clearOneKnobButton.onClick = [this] { clearOneKnob(); };

        pluginStatus.setColour(juce::Label::textColourId, juce::Colour(0xffaab2bc));
        pluginStatus.setFont(juce::Font(10.0f));
        pluginStatus.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(pluginStatus);

        setVisible(false);
        owner.addAndMakeVisible(this);
        setRoot(juce::File::getSpecialLocation(juce::File::userHomeDirectory));
        refreshPlugins();
        setCategory(Category::files);
        startTimerHz(10);
    }

    ~BrowserPanel() override { shutdown(); }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
        if (scanThread.joinable()) scanThread.join();
        pluginTree.setRootItem(nullptr);
        pluginRoot.reset();
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
        g.setColour(juce::Colour(0xff20252c));
        g.fillRect(0, 100, getWidth(), 1);
        g.setColour(juce::Colour(0xff8f98a3));
        g.setFont(juce::Font(9.0f));
        g.drawText(categoryHint(), 14, getHeight() - 29, getWidth() - 28, 18, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        closeButton.setBounds(getWidth() - 72, 8, 62, 26);
        const int tabGap = 3;
        const int tabWidth = juce::jmax(42, (juce::jmax(300, getWidth() - 16) - tabGap * 5) / 6);
        int tabX = 8;
        for (auto* button : { &filesButton, &audioButton, &midiButton, &presetsButton, &pluginsButton, &spliceButton })
        {
            button->setBounds(tabX, 42, tabWidth, 26);
            tabX += tabWidth + tabGap;
        }

        homeButton.setBounds(12, 105, 70, 24);
        fileTree.setBounds(10, 136, getWidth() - 20, juce::jmax(40, getHeight() - 174));

        scanPluginsButton.setBounds(10, 108, getWidth() - 20, 28);
        blacklistButton.setBounds(10, 140, juce::jmax(90, (getWidth() - 24) / 2), 26);
        clearBlacklistButton.setBounds(14 + (getWidth() - 24) / 2, 140, juce::jmax(90, (getWidth() - 24) / 2), 26);
        const int oneKnobY = 172;
        const int oneKnobW = juce::jmax(54, (getWidth() - 26) / 4);
        juce::TextButton* oneKnobButtons[] = { &chorusButton, &flangerButton, &phaserButton, &tremoloButton,
            &reverbButton, &delayButton, &driveButton, &compressorButton, &saturationButton, &widthButton,
            &filterButton, &doublerButton, &exciterButton, &deEsserButton, &gateButton, &bassBoostButton, &airButton, &punchButton, &softClipButton, &clearOneKnobButton };
        for (int i = 0; i < 20; ++i)
            oneKnobButtons[i]->setBounds(10 + (i % 4) * (oneKnobW + 2), oneKnobY + (i / 4) * 30, oneKnobW, 28);
        pluginTree.setBounds(10, 354, getWidth() - 20, juce::jmax(40, getHeight() - 456));
        const int controlsY = getHeight() - 94;
        const int w = juce::jmax(48, (getWidth() - 28) / 4);
        favouritePluginButton.setBounds(10, controlsY, w, 28);
        loadPluginButton.setBounds(14 + w, controlsY, w, 28);
        openPluginButton.setBounds(18 + w * 2, controlsY, w, 28);
        unloadPluginButton.setBounds(22 + w * 3, controlsY, w, 28);
        pluginStatus.setBounds(10, controlsY + 31, getWidth() - 20, 28);
        spliceTitle.setBounds(12, 100, getWidth() - 24, 28);
        spliceInfo.setBounds(12, 128, getWidth() - 24, 52);
        const int spliceButtonW = juce::jmax(80, (getWidth() - 28) / 2);
        spliceOpenPluginButton.setBounds(12, 188, spliceButtonW, 32);
        spliceScanButton.setBounds(16 + spliceButtonW, 188, getWidth() - 28 - spliceButtonW, 32);
    }

private:
    enum class Category { files, audio, midi, presets, plugins, splice };

    static juce::File favouritesFile()
    {
        auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("BSM").getChildFile("Liberty");
        folder.createDirectory();
        return folder.getChildFile("PluginFavorites.txt");
    }

    static juce::String favouriteKey(const juce::PluginDescription& d)
    {
        auto id = d.createIdentifierString();
        return id.isNotEmpty() ? id : d.pluginFormatName + "|" + d.manufacturerName + "|" + d.name;
    }

    void loadFavourites()
    {
        favouriteKeys.clear();
        if (auto file = favouritesFile(); file.existsAsFile()) file.readLines(favouriteKeys);
        favouriteKeys.trim(); favouriteKeys.removeEmptyStrings(); favouriteKeys.removeDuplicates(false);
    }

    void saveFavourites() { favouritesFile().replaceWithText(favouriteKeys.joinIntoString("\n")); }
    bool isFavouritePlugin(const juce::PluginDescription& d) const { return favouriteKeys.contains(favouriteKey(d)); }

    void toggleFavourite(const juce::PluginDescription& d)
    {
        const auto key = favouriteKey(d);
        const int index = favouriteKeys.indexOf(key);
        if (index >= 0) favouriteKeys.remove(index); else favouriteKeys.add(key);
        saveFavourites(); rebuildPluginTree();
        pluginStatus.setText(index >= 0 ? "Retiré des favoris" : "Ajouté aux favoris", juce::dontSendNotification);
    }

    void toggleSelectedFavourite() { if (const auto* d = selectedPluginDescription()) toggleFavourite(*d); }

    void selectionChanged() override {}
    void fileClicked(const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked(const juce::File& file) override { openFileOrDirectory(file); }
    void browserRootChanged(const juce::File&) override {}

    void setBrowserOpen(bool shouldOpen)
    {
        browserOpen = shouldOpen;
        setVisible(browserOpen);
        if (browserOpen) toFront(false);
        adjustHostWindow(browserOpen);
        owner.repaint();
    }

    void adjustHostWindow(bool opening)
    {
        auto* window = owner.findParentComponentOfClass<juce::DocumentWindow>();
        if (window == nullptr) return;
        auto bounds = window->getBounds();
        const int width = getLibertyPreferredBrowserWidth(&owner);
        if (opening)
        {
            if (hostExpanded) return;
            originalWindowBounds = bounds;
            auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(bounds);
            if (display != nullptr)
            {
                const auto usable = display->userArea;
                int wantedRight = bounds.getRight() + width;
                int newX = bounds.getX();
                if (wantedRight > usable.getRight()) newX = juce::jmax(usable.getX(), bounds.getX() - (wantedRight - usable.getRight()));
                window->setBounds(newX, bounds.getY(), juce::jmin(usable.getWidth(), bounds.getWidth() + width), bounds.getHeight());
            }
            else window->setSize(bounds.getWidth() + width, bounds.getHeight());
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
    }

    void setCategory(Category newCategory)
    {
        category = newCategory;
        filesButton.setToggleState(category == Category::files, juce::dontSendNotification);
        audioButton.setToggleState(category == Category::audio, juce::dontSendNotification);
        midiButton.setToggleState(category == Category::midi, juce::dontSendNotification);
        presetsButton.setToggleState(category == Category::presets, juce::dontSendNotification);
        pluginsButton.setToggleState(category == Category::plugins, juce::dontSendNotification);
        spliceButton.setToggleState(category == Category::splice, juce::dontSendNotification);
        const bool pluginMode = category == Category::plugins;
        const bool spliceMode = category == Category::splice;
        fileTree.setVisible(!pluginMode && !spliceMode); homeButton.setVisible(!pluginMode && !spliceMode);
        spliceTitle.setVisible(spliceMode); spliceInfo.setVisible(spliceMode); spliceOpenPluginButton.setVisible(spliceMode); spliceScanButton.setVisible(spliceMode);
        pluginTree.setVisible(pluginMode); scanPluginsButton.setVisible(pluginMode);
        blacklistButton.setVisible(pluginMode); clearBlacklistButton.setVisible(pluginMode);
        favouritePluginButton.setVisible(pluginMode); loadPluginButton.setVisible(pluginMode);
        openPluginButton.setVisible(pluginMode); unloadPluginButton.setVisible(pluginMode);
        pluginStatus.setVisible(pluginMode);
        chorusButton.setVisible(pluginMode); flangerButton.setVisible(pluginMode);
        phaserButton.setVisible(pluginMode); tremoloButton.setVisible(pluginMode);
        reverbButton.setVisible(pluginMode); delayButton.setVisible(pluginMode); driveButton.setVisible(pluginMode);
        compressorButton.setVisible(pluginMode); saturationButton.setVisible(pluginMode); widthButton.setVisible(pluginMode);
        filterButton.setVisible(pluginMode); doublerButton.setVisible(pluginMode); exciterButton.setVisible(pluginMode);
        deEsserButton.setVisible(pluginMode); gateButton.setVisible(pluginMode); bassBoostButton.setVisible(pluginMode);
        airButton.setVisible(pluginMode); punchButton.setVisible(pluginMode); softClipButton.setVisible(pluginMode); clearOneKnobButton.setVisible(pluginMode);
        if (pluginMode && !scanning.load()) refreshPlugins();
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
            case Category::audio: return "WAV / AIFF / MP3 / FLAC";
            case Category::midi: return "Fichiers MIDI";
            case Category::presets: return "Presets";
            case Category::plugins: return "Favoris ou clic droit - glisser-déposer vers une piste";
            default: return rootDirectory.getFullPathName();
        }
    }

    void openFileOrDirectory(const juce::File& file)
    {
        if (file == juce::File{} || !file.exists()) return;
        if (file.isDirectory()) { setRoot(file); return; }
        if (!fileMatchesCategory(file)) return;
        const auto ext = file.getFileExtension().toLowerCase();
        if (ext != ".wav" && ext != ".aif" && ext != ".aiff") return;
        const int track = selectedAudioTrack();
        if (track < 0)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Browser",
                                                   "Sélectionne une piste Audio pour charger ce fichier.", "OK");
            return;
        }
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

    PluginTreeItem* makePluginItem(const juce::PluginDescription& d)
    {
        return new PluginTreeItem(PluginTreeItem::Kind::plugin, d.name, &d,
            [this](const juce::PluginDescription& x) { loadPluginDescription(x); },
            [this](const juce::PluginDescription& x) { return isFavouritePlugin(x); },
            [this](const juce::PluginDescription& x) { toggleFavourite(x); });
    }

    void rebuildPluginTree()
    {
        pluginTree.setRootItem(nullptr);
        pluginRoot = std::make_unique<PluginTreeItem>(PluginTreeItem::Kind::root, "ROOT");
        std::sort(pluginDescriptions.begin(), pluginDescriptions.end(), [](const auto& a, const auto& b)
        {
            auto am = a.manufacturerName.trim(); if (am.isEmpty()) am = "Other";
            auto bm = b.manufacturerName.trim(); if (bm.isEmpty()) bm = "Other";
            const int c = am.compareNatural(bm, false);
            return c != 0 ? c < 0 : a.name.compareNatural(b.name, false) < 0;
        });

        auto* fav = new PluginTreeItem(PluginTreeItem::Kind::manufacturer, "FAVORIS");
        pluginRoot->addSubItem(fav);
        for (const auto& d : pluginDescriptions) if (isFavouritePlugin(d)) fav->addSubItem(makePluginItem(d));

        juce::String current;
        PluginTreeItem* group = nullptr;
        for (const auto& d : pluginDescriptions)
        {
            auto m = d.manufacturerName.trim(); if (m.isEmpty()) m = "Other";
            if (m != current)
            {
                current = m;
                group = new PluginTreeItem(PluginTreeItem::Kind::manufacturer, m);
                pluginRoot->addSubItem(group);
            }
            if (group != nullptr) group->addSubItem(makePluginItem(d));
        }
        pluginTree.setRootItem(pluginRoot.get());
        pluginRoot->setOpen(true); fav->setOpen(true); pluginTree.repaint();
    }

    void refreshPlugins()
    {
        pluginDescriptions = LibertyPluginHost::instance().getPluginDescriptions();
        rebuildPluginTree(); refreshPluginStatus();
    }

    const juce::PluginDescription* selectedPluginDescription() const
    {
        if (auto* selected = pluginTree.getSelectedItem(0))
            if (auto* item = dynamic_cast<PluginTreeItem*>(selected)) return item->getPluginDescription();
        return nullptr;
    }

    void setScanStatus(const juce::String& text) { const juce::ScopedLock lock(scanStatusLock); scanStatusText = text; }
    juce::String getScanStatus() const { const juce::ScopedLock lock(scanStatusLock); return scanStatusText; }

    void scanPlugins()
    {
        if (scanning.exchange(true)) return;
        if (scanThread.joinable()) scanThread.join();
        scanFinishedPending.store(false);
        for (auto* b : { &scanPluginsButton, &blacklistButton, &clearBlacklistButton, &favouritePluginButton,
                         &loadPluginButton, &openPluginButton, &unloadPluginButton }) b->setEnabled(false);
        setScanStatus("Préparation du scan AU + VST3");
        scanThread = std::thread([this]
        {
            LibertyPluginHost::instance().scanInstalledPlugins([this](const juce::String& formatName, const juce::String& pluginName, float progress)
            {
                if (stopped.load()) return;
                const int percent = juce::jlimit(0, 100, juce::roundToInt(progress * 100.0f));
                setScanStatus(pluginName.isEmpty() ? "Finalisation du scan" : "SCAN " + formatName + "  " + juce::String(percent) + "%  " + pluginName);
            });
            if (!stopped.load()) { scanning.store(false); scanFinishedPending.store(true); }
        });
    }

    void showBlacklist()
    {
        auto& host = LibertyPluginHost::instance();
        auto entries = host.getBlacklistedPlugins();
        juce::String text = entries.isEmpty() ? "Aucun plugin blacklisté." : entries.joinIntoString("\n");
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Liberty - Blacklist", text, "OK");
    }

    void clearBlacklist() { LibertyPluginHost::instance().clearBlacklist(); refreshPluginStatus(); }

    void loadPluginDescription(const juce::PluginDescription& d)
    {
        if (scanning.load()) return;
        juce::String error;
        bool ok = false;
        auto& host = LibertyPluginHost::instance();
        if (d.isInstrument)
        {
            const int instrumentLane = selectedInstrumentTrack();
            if (instrumentLane < 0)
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Instrument",
                                                       "Sélectionne une piste Instrument pour charger cet instrument.", "OK");
                return;
            }
            ok = host.loadInstrumentForTrack(instrumentLane, d, error);
            if (ok) host.showInstrumentEditorForTrack(instrumentLane);
        }
        else
        {
            const int track = selectedAudioTrack();
            if (track < 0)
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Effet",
                                                       "Sélectionne une piste Audio pour charger cet effet.", "OK");
                return;
            }
            ok = host.loadEffectForTrack(track, d, error);
            if (ok) host.showEditorForTrack(track);
        }
        if (!ok) juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Plugin", error, "OK");
        refreshPluginStatus(); owner.repaint();
    }

    int selectedAudioTrack() const
    {
        const int track = owner.selectedTrack;
        return (track >= 0 && track < owner.getAudioTrackCount()) ? track : -1;
    }

    int selectedInstrumentTrack() const
    {
        const int firstInstrument = owner.getAudioTrackCount() + owner.getMidiTrackCount();
        const int track = owner.selectedTrack;
        return (track >= firstInstrument && track < firstInstrument + owner.getInstrumentTrackCount()) ? track - firstInstrument : -1;
    }

    void loadOneKnob(LibertyOneKnobRack::Type type)
    {
        const int track = selectedAudioTrack();
        if (track < 0)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - One Knob",
                                                   "Sélectionne une piste Audio pour charger ce One Knob.", "OK");
            return;
        }
        auto& manager = LibertyOneKnobManager::instance();
        manager.setEffect(track, type);
        manager.showEditor(track);
        refreshPluginStatus();
        owner.repaint();
    }

    void clearOneKnob()
    {
        const int track = selectedAudioTrack();
        if (track < 0)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - One Knob",
                                                   "Sélectionne une piste Audio pour retirer le One Knob.", "OK");
            return;
        }
        LibertyOneKnobManager::instance().clearEffect(track);
        refreshPluginStatus();
        owner.repaint();
    }

    void openSpliceSounds()
    {
        refreshPlugins();
        const juce::PluginDescription* splice = nullptr;
        for (const auto& d : pluginDescriptions)
        {
            const auto name = d.name.trim();
            if (name.equalsIgnoreCase("Splice Sounds")
                || (name.containsIgnoreCase("Splice") && name.containsIgnoreCase("Sounds")))
            {
                splice = &d;
                if (d.isInstrument) break;
            }
        }

        if (splice == nullptr)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Splice Sounds",
                "Splice Sounds n'est pas encore présent dans la liste des plugins Liberty. Installe le plugin officiel puis lance SCAN AU + VST3.", "OK");
            return;
        }

        if (!splice->isInstrument)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Splice Sounds",
                "Liberty a trouvé un plugin Splice, mais pas l'instrument Splice Sounds. Vérifie que Splice Sounds est installé et rescanné.", "OK");
            return;
        }

        const int lane = selectedInstrumentTrack();
        if (lane < 0)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Splice Sounds",
                "Sélectionne d'abord une piste Instrument : Splice Sounds est un plugin instrument.", "OK");
            return;
        }

        loadPluginDescription(*splice);
    }

    void loadSelectedPlugin() { if (const auto* d = selectedPluginDescription()) loadPluginDescription(*d); }
    void openLoadedPluginEditor()
    {
        if (const auto* d = selectedPluginDescription(); d != nullptr && d->isInstrument)
        {
            const int lane = selectedInstrumentTrack();
            if (lane < 0)
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Instrument",
                                                       "Sélectionne une piste Instrument pour ouvrir son interface.", "OK");
                return;
            }
            LibertyPluginHost::instance().showInstrumentEditorForTrack(lane);
            return;
        }
        const int track = selectedAudioTrack();
        if (track < 0)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Effet",
                                                   "Sélectionne une piste Audio pour ouvrir son effet.", "OK");
            return;
        }
        LibertyPluginHost::instance().showEditorForTrack(track);
    }
    void unloadPlugin()
    {
        if (const auto* d = selectedPluginDescription(); d != nullptr && d->isInstrument)
        {
            const int lane = selectedInstrumentTrack();
            if (lane < 0)
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Instrument",
                                                       "Sélectionne une piste Instrument pour retirer son instrument.", "OK");
                return;
            }
            LibertyPluginHost::instance().unloadInstrumentForTrack(lane);
        }
        else
        {
            const int track = selectedAudioTrack();
            if (track < 0)
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Effet",
                                                       "Sélectionne une piste Audio pour retirer son effet.", "OK");
                return;
            }
            LibertyPluginHost::instance().unloadEffectForTrack(track);
        }
        refreshPluginStatus(); owner.repaint();
    }

    void refreshPluginStatus()
    {
        if (scanning.load()) { pluginStatus.setText(getScanStatus(), juce::dontSendNotification); return; }
        auto& host = LibertyPluginHost::instance();
        const int logical = owner.selectedTrack;
        const int audioCount = owner.getAudioTrackCount();
        const int firstInstrument = audioCount + owner.getMidiTrackCount();
        juce::String text;
        if (logical >= 0 && logical < audioCount)
        {
            auto& oneKnob = LibertyOneKnobManager::instance();
            if (oneKnob.hasEffect(logical)) text << "A" << (logical + 1) << ": " << oneKnob.getName(logical) << "   ";
            if (host.hasEffectForTrack(logical)) text << "FX: " << host.getEffectName(logical) << "   ";
        }
        else if (logical >= firstInstrument && logical < firstInstrument + owner.getInstrumentTrackCount())
        {
            const int instrumentLane = logical - firstInstrument;
            if (host.hasInstrumentForTrack(instrumentLane)) text << "INST: " << host.getInstrumentNameForTrack(instrumentLane);
        }
        if (text.isEmpty()) text = juce::String(pluginDescriptions.size()) + " plugins  " + juce::String(favouriteKeys.size()) + " favoris";
        pluginStatus.setText(text, juce::dontSendNotification);
    }

    void timerCallback() override
    {
        if (stopped.load()) return;
        const int width = juce::jlimit(260, juce::jmax(260, owner.getWidth() - 220), getLibertyPreferredBrowserWidth(&owner));
        const int panelX = juce::jmax(0, owner.getWidth() - width);
        if (browserOpen)
        {
            const auto wanted = juce::Rectangle<int>(panelX, topBarHeight, juce::jmin(width, owner.getWidth()), juce::jmax(1, owner.getHeight() - topBarHeight));
            if (getBounds() != wanted) setBounds(wanted);
            if (!isVisible()) setVisible(true);
        }

        const int buttonX = browserOpen ? juce::jmax(8, panelX - 104) : juce::jmax(8, owner.getWidth() - 112);
        const auto buttonBounds = juce::Rectangle<int>(buttonX, 8, 96, 26);
        if (toggleButton.getBounds() != buttonBounds) toggleButton.setBounds(buttonBounds);

        if (category == Category::plugins)
        {
            if (scanning.load()) pluginStatus.setText(getScanStatus(), juce::dontSendNotification);
            else if (scanFinishedPending.exchange(false))
            {
                if (scanThread.joinable()) scanThread.join();
                refreshPlugins();
                for (auto* b : { &scanPluginsButton, &blacklistButton, &clearBlacklistButton, &favouritePluginButton,
                                 &loadPluginButton, &openPluginButton, &unloadPluginButton }) b->setEnabled(true);
            }
            else refreshPluginStatus();
        }
    }

    MainComponent& owner;
    juce::TimeSliceThread thread { "Liberty Browser" };
    juce::WildcardFileFilter filter;
    juce::DirectoryContentsList directoryList;
    juce::FileTreeComponent fileTree;
    juce::TreeView pluginTree;
    std::unique_ptr<PluginTreeItem> pluginRoot;
    juce::Array<juce::PluginDescription> pluginDescriptions;
    juce::StringArray favouriteKeys;
    juce::TextButton toggleButton, closeButton, filesButton, audioButton, midiButton, presetsButton, pluginsButton, spliceButton, homeButton;
    juce::TextButton scanPluginsButton, blacklistButton, clearBlacklistButton, favouritePluginButton, loadPluginButton, openPluginButton, unloadPluginButton;
    juce::TextButton chorusButton, flangerButton, phaserButton, tremoloButton, reverbButton, delayButton, driveButton, compressorButton, saturationButton, widthButton, filterButton, doublerButton, exciterButton, deEsserButton, gateButton, bassBoostButton, airButton, punchButton, softClipButton, clearOneKnobButton;
    juce::Label pluginStatus, spliceTitle, spliceInfo;
    juce::TextButton spliceOpenPluginButton, spliceScanButton;
    juce::File rootDirectory;
    Category category = Category::files;
    std::atomic<bool> stopped { false }, scanning { false }, scanFinishedPending { false };
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
    void shutdown()
    {
        stopTimer();
        for (auto& item : browsers) if (item.second) item.second->shutdown();
        browsers.clear();
    }
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

void shutdownLibertyBrowserController()
{
    bootstrap.shutdown();
}
