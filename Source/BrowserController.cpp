#define private public
#include "MainComponent.h"
#undef private
#include "PluginHost.h"
#include "OneKnobEffects.h"
#include "LibertyDesignSystem.h"
#include "PageNavigationLayout.h"
void refreshLibertyMixConsole(MainComponent*);
void resizeLibertyMixConsole(MainComponent*);

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <thread>

int getLibertyPreferredBrowserWidth(MainComponent* owner);
void resizeLibertyBrowserResizeController(MainComponent* owner);

namespace
{
constexpr int topBarHeight = 76;

// File filtering is performed by the directory listing itself, so category
// changes and search results agree with the visible file tree.
class BrowserFileFilter final : public juce::FileFilter
{
public:
    BrowserFileFilter() : juce::FileFilter("Liberty browser files") {}
    int category = 0;
    juce::String query;
    bool isDirectorySuitable(const juce::File&) const override { return true; }
    bool isFileSuitable(const juce::File& file) const override
    {
        if (query.isNotEmpty() && !file.getFileName().containsIgnoreCase(query)) return false;
        const auto ext = file.getFileExtension().toLowerCase();
        if (category == 4) return ext == ".bsmproj";
        if (category == 1) return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".mp3" || ext == ".flac";
        if (category == 2) return ext == ".mid" || ext == ".midi";
        if (category == 3) return ext == ".xml" || ext == ".fxp" || ext == ".vstpreset" || ext == ".aupreset";
        return true;
    }
};

class PluginTreeItem final : public juce::TreeViewItem
{
public:
    enum class Kind { root, manufacturer, plugin };

    PluginTreeItem(Kind kindIn,
                   juce::String labelIn,
                   const juce::PluginDescription* descriptionIn = nullptr,
                   std::function<void(const juce::PluginDescription&)> activateIn = {},
                   std::function<bool(const juce::PluginDescription&)> isFavouriteIn = {},
                   std::function<void(const juce::PluginDescription&)> toggleFavouriteIn = {},
                   bool thumbnailsIn = false)
        : kind(kindIn), label(std::move(labelIn)), activate(std::move(activateIn)),
          isFavourite(std::move(isFavouriteIn)), toggleFavourite(std::move(toggleFavouriteIn)),
          thumbnails(thumbnailsIn)
    {
        if (descriptionIn != nullptr) description = *descriptionIn;
        // Decode the optional thumbnail once per tree item, never during painting.
        if (kind == Kind::plugin && thumbnails)
        {
            const auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                .getChildFile("Liberty").getChildFile("PluginThumbnails");
            const auto file = folder.getChildFile(juce::String::toHexString(
                (juce::int64) description.createIdentifierString().hashCode64()) + ".png");
            if (file.existsAsFile()) thumbnailImage = juce::ImageFileFormat::loadFrom(file);
        }
    }

    bool mightContainSubItems() override { return kind != Kind::plugin; }
    bool canBeSelected() const override { return kind == Kind::plugin; }
    int getItemHeight() const override { return kind == Kind::manufacturer ? 28 : (kind == Kind::plugin ? (thumbnails ? 88 : 38) : 20); }
    juce::String getUniqueName() const override { return kind == Kind::plugin ? description.createIdentifierString() : label; }

    void paintItem(juce::Graphics& g, int width, int height) override
    {
        if (kind == Kind::root) return;
        if (kind == Kind::manufacturer)
        {
            const bool fav = label == "FAVORIS";
            // Obsidian graphite manufacturer headers; preserve the gold-toned favourites surface.
            g.setColour(fav ? juce::Colour(0xff2a2415) : juce::Colour(0xff1b2833));
            g.fillRect(0, 0, width, height);
            // A one-pixel graphite rule separates manufacturer groups without changing layout.
            g.setColour(fav ? juce::Colour(0xff655331) : juce::Colour(0xff40515f));
            g.fillRect(0, height - 1, width, 1);
            // Obsidian manufacturer headings use muted turquoise; favourites keep gold.
            g.setColour(fav ? juce::Colour(0xffffc857) : juce::Colour(0xffb8e8ec));
            g.setFont(juce::Font(11.0f, juce::Font::bold));
            // Keep a dedicated disclosure region and clip long group names.
            const int headingWidth = juce::jmax(0, width - 20);
            if (headingWidth > 0)
                g.drawFittedText(label, 4, 0, headingWidth, height,
                                 juce::Justification::centredLeft, 1);
            return;
        }

        if (isSelected())
        {
            // Obsidian selection uses the same deep turquoise as selected arranger rows.
            g.setColour(juce::Colour(0xff23434c));
            g.fillRect(0, 0, width, height);
            // Obsidian selection marker stays inside the existing plugin row.
            g.setColour(juce::Colour(0xff54d6dc));
            g.fillRect(0, 0, 2, height);
        }

        int textLeft = 4;
        if (thumbnails && width >= 110)
        {
            const juce::Rectangle<int> imageArea(4, 5, 92, height - 10);
            g.setColour(juce::Colour(0xff1b2833));
            g.fillRoundedRectangle(imageArea.toFloat(), 4.0f);
            if (thumbnailImage.isValid()) g.drawImageWithin(thumbnailImage, imageArea.getX(), imageArea.getY(), imageArea.getWidth(), imageArea.getHeight(), juce::RectanglePlacement::centred);
            else
            {
                g.setColour(juce::Colour(0xff8da6b1));
                g.setFont(10.0f);
                g.drawFittedText("IMAGE", imageArea, juce::Justification::centred, 1);
            }
            textLeft = 102;
        }
        // Obsidian soft white keeps plugin names prominent without harsh pure white.
        g.setColour(juce::Colour(0xffeffcfd));
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        if (width > textLeft + 8)
            g.drawFittedText(description.name, textLeft, 3, width - textLeft - 8, 17,
                             juce::Justification::centredLeft, 1);
        // Obsidian secondary metadata stays readable without competing with plugin names.
        g.setColour(juce::Colour(0xffb9ccd6));
        g.setFont(juce::Font(9.0f));
        if (width > textLeft + 8)
            g.drawFittedText(description.pluginFormatName + "  "
                             + (description.isInstrument ? "INSTRUMENT" : "EFFET"),
                             textLeft, 20, width - textLeft - 8, 14,
                             juce::Justification::centredLeft, 1);
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
    bool thumbnails = false;
    juce::Image thumbnailImage;
    juce::String label;
    juce::PluginDescription description;
    std::function<void(const juce::PluginDescription&)> activate;
    std::function<bool(const juce::PluginDescription&)> isFavourite;
    std::function<void(const juce::PluginDescription&)> toggleFavourite;
};

// One Knob controls reserve separate rectangles for their icon and label.
// Never paint icons over JUCE's centred default TextButton text.
class OneKnobButton final : public juce::TextButton
{
public:
    void paintButton(juce::Graphics& g, bool isOver, bool isDown) override
    {
        const auto area = getLocalBounds().toFloat().reduced(0.8f);
        const auto background = getToggleState() ? juce::Colour(0xff286674)
                                                  : juce::Colour(0xff1b2833);
        g.setColour(background.brighter(isDown ? 0.12f : (isOver ? 0.06f : 0.0f)));
        g.fillRoundedRectangle(area, 5.0f);
        g.setColour(juce::Colour(0xff40515f));
        g.drawRoundedRectangle(area, 5.0f, 1.0f);

        const int width = getWidth();
        const int iconSpace = width >= 112 ? 25 : (width >= 82 ? 20 : 0);
        if (iconSpace > 0)
        {
            const float cx = (float)iconSpace * 0.48f;
            const float cy = (float)getHeight() * 0.5f;
            g.setColour(juce::Colour(0xff54d6dc));
            g.drawEllipse(cx - 5.0f, cy - 5.0f, 10.0f, 10.0f, 1.0f);
            juce::Path wave;
            wave.startNewSubPath(cx - 3.5f, cy);
            wave.lineTo(cx - 1.5f, cy - 2.0f);
            wave.lineTo(cx + 0.5f, cy + 2.0f);
            wave.lineTo(cx + 3.5f, cy);
            g.strokePath(wave, juce::PathStrokeType(1.0f));
        }

        // The label is clipped to its own region; the icon cannot overlap it.
        auto textArea = getLocalBounds().withTrimmedLeft(iconSpace + 2).reduced(3, 2);
        g.setColour(juce::Colour(0xffb8e8ec));
        g.setFont(juce::Font(juce::jlimit(9.0f, 12.0f, (float)width / 11.0f), juce::Font::bold));
        g.drawFittedText(getButtonText(), textArea, juce::Justification::centred, 1, 0.75f);
    }
};

class BrowserPanel final : public juce::Component,
                           private juce::Timer,
                           private juce::FileBrowserListener
{
public:
    void ensureToggleVisible()
    {
        toggleButton.setVisible(true);
        toggleButton.toFront(false);
    }
    bool isOpenForMixer() const noexcept {return browserOpen;}

    void openForEffectSlot(bool instrument,int lane,int slot)
    {
        if(slot<0||slot>=LibertyPluginHost::effectSlotsPerTrack ||
           lane<0||(instrument?lane>=owner.getInstrumentTrackCount()
                                :lane>=owner.getAudioTrackCount()))
            return;
        targetIsInstrument=instrument;
        targetEffectLane=lane;
        targetEffectSlot=slot;
        owner.selectedTrack=instrument
            ?owner.getAudioTrackCount()+owner.getMidiTrackCount()+lane:lane;
        setCategory(Category::plugins);
        toggleButton.setToggleState(true,juce::dontSendNotification);
        setBrowserOpen(true);
    }

    explicit BrowserPanel(MainComponent& ownerIn)
        : owner(ownerIn), directoryList(&filter, thread), fileTree(directoryList)
    {
        setAlwaysOnTop(true);
        thread.startThread();
        previewFormats.registerBasicFormats();
        previewPlayer.setSource(&previewTransport);
        owner.audioEngine.getDeviceManager().addAudioCallback(&previewPlayer);
        loadFavourites();

        toggleButton.setButtonText("NAVIGATEUR");
        toggleButton.setAlwaysOnTop(true);
        // Obsidian browser toggle matches category controls in both states.
        toggleButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
        toggleButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff286674));
        toggleButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffb9ccd6));
        toggleButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffeffcfd));
        toggleButton.setClickingTogglesState(true);
        toggleButton.setToggleState(false, juce::dontSendNotification);
        toggleButton.onClick = [this] { setBrowserOpen(toggleButton.getToggleState()); };
        owner.addAndMakeVisible(toggleButton);

        closeButton.setButtonText("FERMER");
        // Obsidian close control uses the same graphite surface and soft-white labels.
        closeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
        closeButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffcfd));
        closeButton.onClick = [this]
        {
            toggleButton.setToggleState(false, juce::dontSendNotification);
            setBrowserOpen(false);
        };
        addAndMakeVisible(closeButton);

        // Obsidian: distinguish selected files without altering file interactions.
        fileTree.setColour(juce::DirectoryContentsDisplayComponent::highlightColourId, juce::Colour(0xff23434c));
        fileTree.setColour(juce::DirectoryContentsDisplayComponent::textColourId, juce::Colour(0xffeffcfd));
        fileTree.setRootItemVisible(false);
        // Obsidian file tree uses the same deep graphite as the plugin browser.
        fileTree.setColour(juce::TreeView::backgroundColourId, juce::Colour(0xff101d2b));
        // Match the plugin tree graphite guides and turquoise drop target.
        fileTree.setColour(juce::TreeView::linesColourId, juce::Colour(0xff40515f));
        fileTree.setColour(juce::TreeView::dragAndDropIndicatorColourId, juce::Colour(0xff54d6dc));
        fileTree.addListener(this);
        addAndMakeVisible(fileTree);

        pluginTree.setRootItemVisible(false);
        pluginTree.setMultiSelectEnabled(false);
        // Keep the plugin list surface continuous with the Obsidian browser panel.
        pluginTree.setColour(juce::TreeView::backgroundColourId, juce::Colour(0xff101821));
        // Obsidian graphite tree guides align with the browser panel dividers.
        pluginTree.setColour(juce::TreeView::linesColourId, juce::Colour(0xff40515f));
        // Obsidian turquoise highlights plugin drag-and-drop targets consistently.
        pluginTree.setColour(juce::TreeView::dragAndDropIndicatorColourId, juce::Colour(0xff54d6dc));
        addAndMakeVisible(pluginTree);
        thumbnailModeButton.setButtonText("MINIATURES");
        thumbnailModeButton.setClickingTogglesState(true);
        thumbnailModeButton.onClick = [this]
        {
            favouritesFile().getSiblingFile("PluginThumbnailMode.txt").replaceWithText(
                thumbnailModeButton.getToggleState() ? "1" : "0");
            rebuildPluginTree();
        };
        thumbnailModeButton.setToggleState(
            favouritesFile().getSiblingFile("PluginThumbnailMode.txt").loadFileAsString().trim() == "1",
            juce::dontSendNotification);
        thumbnailModeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
        thumbnailModeButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff286674));
        thumbnailModeButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffcfd));
        addAndMakeVisible(thumbnailModeButton);
        importThumbnailButton.setButtonText("IMPORTER IMAGE");
        importThumbnailButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
        importThumbnailButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffcfd));
        importThumbnailButton.onClick = [this] { importSelectedThumbnail(); };
        addAndMakeVisible(importThumbnailButton);

        spliceTitle.setText("SPLICE", juce::dontSendNotification);
        // Obsidian primary text keeps the Splice heading consistent with Browser tabs.
        spliceTitle.setColour(juce::Label::textColourId, juce::Colour(0xffeffcfd));
        spliceTitle.setFont(juce::Font(14.0f, juce::Font::bold));
        addAndMakeVisible(spliceTitle);
        spliceInfo.setText("Splice Sounds - charge le plugin officiel AU/VST3 dans Liberty pour l'interface complète.", juce::dontSendNotification);
        // Match Splice help text to Obsidian secondary typography.
        spliceInfo.setColour(juce::Label::textColourId, juce::Colour(0xffb9ccd6));
        spliceInfo.setFont(juce::Font(10.0f));
        spliceInfo.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(spliceInfo);
        // Obsidian primary action for opening the official Splice plugin.
        spliceOpenPluginButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff286674));
        spliceOpenPluginButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffcfd));
        spliceOpenPluginButton.setButtonText("OUVRIR SPLICE SOUNDS");
        spliceOpenPluginButton.onClick = [this] { openSpliceSounds(); };
        addAndMakeVisible(spliceOpenPluginButton);
        // Obsidian secondary Splice action: subdued graphite, readable blue-grey text.
        spliceScanButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
        spliceScanButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffb9ccd6));
        spliceScanButton.setButtonText("SCAN SPLICE");
        spliceScanButton.onClick = [this] { scanPlugins(); };
        addAndMakeVisible(spliceScanButton);

        filesButton.setButtonText("PROJETS");
        audioButton.setButtonText("SAMPLES");
        midiButton.setButtonText("MIDI");
        presetsButton.setButtonText("PRESETS");
        pluginsButton.setButtonText("PLUGINS");
        pluginSearch.setTextToShowWhenEmpty(juce::String::fromUTF8("Rechercher un plugin ou fabricant..."), juce::Colour(0xff8da6b1));
        pluginSearch.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff101821));
        pluginSearch.setColour(juce::TextEditor::textColourId, juce::Colour(0xffeffcfd));
        pluginSearch.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff40515f));
        pluginSearch.onTextChange = [this]
        {
            if (category == Category::plugins) rebuildPluginTree();
            else if (category != Category::splice)
            {
                filter.query = pluginSearch.getText().trim();
                directoryList.refresh();
            }
        };
        pluginSearch.onEscapeKey = [this]
        {
            pluginSearch.clear();
            pluginTree.grabKeyboardFocus();
        };
        addAndMakeVisible(pluginSearch);
        spliceButton.setButtonText("SPLICE");
        for (auto* b : { &filesButton, &audioButton, &midiButton, &presetsButton, &pluginsButton, &spliceButton })
        {
            // Obsidian inactive category surface matches browser manufacturer headers.
            b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
            // Obsidian active category surface matches selected step-sequencer accents.
            b->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff286674));
            // Obsidian muted blue-grey keeps inactive category labels readable.
            b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffb9ccd6));
            // Obsidian soft white completes the active category typography.
            b->setColour(juce::TextButton::textColourOnId, juce::Colour(0xffeffcfd));
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
        // Obsidian HOME control matches the graphite category surfaces.
        homeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
        homeButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffcfd));
        homeButton.onClick = [this]
        {
            if (category == Category::audio)
            {
                const auto library = findInstalledSampleLibrary();
                if (library.isDirectory()) { setRoot(library); return; }
            }
            setRoot(juce::File::getSpecialLocation(juce::File::userHomeDirectory));
        };
        addAndMakeVisible(homeButton);
        chooseLibraryButton.setButtonText("CHOISIR");
        chooseLibraryButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
        chooseLibraryButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffcfd));
        chooseLibraryButton.onClick = [this] { chooseSampleLibrary(); };
        addAndMakeVisible(chooseLibraryButton);
        saveFolderButton.setButtonText("AJOUTER FAVORI");
        openFavouriteButton.setButtonText("OUVRIR FAVORI");
        for (auto* button : { &saveFolderButton, &openFavouriteButton })
        {
            button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
            button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffcfd));
            addAndMakeVisible(*button);
        }
        saveFolderButton.onClick = [this]
        {
            if (!rootDirectory.isDirectory()) return;
            auto favourites = sampleFolderFavourites();
            const auto path = rootDirectory.getFullPathName();
            const int index = favourites.indexOf(path);
            if (index >= 0) favourites.remove(index);
            else favourites.add(path);
            sampleFolderFavouritesFile().replaceWithText(favourites.joinIntoString("\n"));
            updateSampleFolderControls();
        };
        openFavouriteButton.onClick = [this]
        {
            juce::PopupMenu menu;
            const auto favourites = sampleFolderFavourites();
            for (int i = 0; i < favourites.size(); ++i)
            {
                const juce::File folder(favourites[i]);
                menu.addItem(i + 1, folder.getFileName() + (folder.isDirectory() ? "" : " (indisponible)"), folder.isDirectory());
            }
            if (favourites.isEmpty()) menu.addItem(1, "Aucun dossier favori", false);
            juce::Component::SafePointer<BrowserPanel> safe(this);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&openFavouriteButton),
                [safe, favourites](int result)
                {
                    if (safe == nullptr || result < 1 || result > favourites.size()) return;
                    const juce::File folder(favourites[result - 1]);
                    if (folder.isDirectory()) safe->setRoot(folder);
                });
        };
        parentFolderButton.setButtonText("DOSSIER PARENT");
        parentFolderButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
        parentFolderButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffcfd));
        parentFolderButton.onClick = [this]
        {
            const auto parent = rootDirectory.getParentDirectory();
            if (parent.isDirectory() && parent != rootDirectory) setRoot(parent);
        };
        addAndMakeVisible(parentFolderButton);
        previewPlay.setButtonText(juce::String::fromUTF8("ÉCOUTER"));
        previewStop.setButtonText("STOP");
        for (auto* b : { &previewPlay, &previewStop })
        {
            b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
            b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffcfd));
            addAndMakeVisible(*b);
        }
        previewPlay.onClick = [this]
        {
            if (previewReader == nullptr) return;
            if (previewTransport.hasStreamFinished()) previewTransport.setPosition(0.0);
            previewTransport.start();
        };
        previewStop.onClick = [this] { previewTransport.stop(); previewTransport.setPosition(0.0); };
        previewVolume.setSliderStyle(juce::Slider::LinearHorizontal);
        previewVolume.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        previewVolume.setRange(0.0, 1.0, 0.01);
        previewVolume.setValue(0.7, juce::dontSendNotification);
        previewVolume.onValueChange = [this] { previewTransport.setGain((float)previewVolume.getValue()); };
        addAndMakeVisible(previewVolume);
        previewAuto.setButtonText("AUTO");
        previewAuto.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffeffcfd));
        addAndMakeVisible(previewAuto);
        previewInfo.setText("Aucun sample sélectionné", juce::dontSendNotification);
        previewInfo.setColour(juce::Label::textColourId, juce::Colour(0xffeffcfd));
        previewInfo.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(previewInfo);
        samplePackInfo.setColour(juce::Label::textColourId, juce::Colour(0xffb8e8ec));
        samplePackInfo.setFont(juce::Font(10.0f));
        samplePackInfo.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(samplePackInfo);
        browsePacksButton.setButtonText("PACKS");
        browsePacksButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
        browsePacksButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffcfd));
        browsePacksButton.onClick = [this] { showInstalledPacks(); };
        addAndMakeVisible(browsePacksButton);
        musicFilterButton.setButtonText("FILTRES");
        musicFilterButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
        musicFilterButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffcfd));
        musicFilterButton.onClick = [this] { showMusicalFilters(); };
        addAndMakeVisible(musicFilterButton);
        sampleNameSearch.setTextToShowWhenEmpty("RECHERCHER UN SAMPLE...", juce::Colour(0xff8095a8));
        sampleNameSearch.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff101d2b));
        sampleNameSearch.setColour(juce::TextEditor::textColourId, juce::Colour(0xffeff7ff));
        sampleNameSearch.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff38536a));
        sampleNameSearch.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xff54d6dc));
        sampleNameSearch.setColour(juce::TextEditor::highlightColourId, juce::Colour(0xff286674));
        sampleNameSearch.setFont(juce::Font(12.0f));
        sampleNameSearch.onTextChange = [this]
        {
            sampleNameQuery = sampleNameSearch.getText();
            saveSampleFilterPreferences();
            sampleSearchPending = true;
            sampleSearchChangedAt = juce::Time::getMillisecondCounter();
        };
        addAndMakeVisible(sampleNameSearch);
        const auto savedSampleFilters = juce::StringArray::fromLines(sampleFilterPreferenceFile().loadFileAsString());
        if (savedSampleFilters.size() > 0) sampleFilterMask = savedSampleFilters[0].getIntValue() & 31;
        if (savedSampleFilters.size() > 1)
        {
            sampleNameQuery = savedSampleFilters[1];
            sampleNameSearch.setText(sampleNameQuery, juce::dontSendNotification);
        }

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
            // Obsidian plugin actions share the graphite surface and soft-white typography.
            b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
            b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeffcfd));
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
            // Obsidian One Knob action buttons use the shared graphite and cyan palette.
            b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2833));
            b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffb8e8ec));
            b->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff286674));
            b->setColour(juce::TextButton::textColourOnId, juce::Colour(0xffeffcfd));
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

        // Obsidian secondary text aligns plugin status with browser hints.
        pluginStatus.setColour(juce::Label::textColourId, juce::Colour(0xffb9ccd6));
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
        previewTransport.stop();
        owner.audioEngine.getDeviceManager().removeAudioCallback(&previewPlayer);
        previewPlayer.setSource(nullptr);
        previewTransport.setSource(nullptr);
        previewReader.reset();
        if (sampleSearchThread.joinable()) sampleSearchThread.join();
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
        // Obsidian deep graphite aligns the browser with mixer and editor panels.
        g.fillAll(LibertyUI::Colour::panel);
        // Obsidian browser edge matches the graphite dividers throughout Liberty.
        g.setColour(juce::Colour(0xff40515f));
        g.drawVerticalLine(0, 0.0f, (float)getHeight());
        // Obsidian soft-white title matches primary labels across Liberty Pro.
        g.setColour(LibertyUI::Colour::textPrimary);
        g.setFont(juce::Font(15.0f, juce::Font::bold));
        g.drawText("LIBERTY  /  NAVIGATEUR", 14, 10, juce::jmax(0, getWidth() - 96), 24, juce::Justification::centredLeft, true);
        g.setColour(juce::Colour(0xff54d6dc));
        g.fillRoundedRectangle(12.0f, 35.0f, juce::jmax(0.0f, (float)getWidth() - 24.0f), 2.0f, 1.0f);
        // Obsidian separator clearly distinguishes browser controls from the plugin list.
        g.setColour(juce::Colour(0xff40515f));
        g.fillRect(0, 100, getWidth(), 1);
        if (category == Category::audio)
        {
            g.setColour(juce::Colour(0xff89a6b9));
            g.setFont(juce::Font(10.0f, juce::Font::bold));
            g.drawText("BIBLIOTHÈQUE DE SAMPLES", 14, 219, juce::jmax(0, getWidth() - 28), 11, juce::Justification::centredLeft, true);
        }
        if (category == Category::audio)
        {
            g.setColour(juce::Colour(0xff142638));
            g.fillRoundedRectangle(7.0f, 102.0f, (float)juce::jmax(0, getWidth() - 14), 116.0f, 8.0f);
            g.setColour(juce::Colour(0xff324d63));
            g.drawRoundedRectangle(7.0f, 102.0f, (float)juce::jmax(0, getWidth() - 14), 116.0f, 8.0f, 1.0f);
            const int previewTop = juce::jmax(136, getHeight() - 144);
            g.setColour(juce::Colour(0xff142638));
            g.fillRoundedRectangle(7.0f, (float)previewTop - 5.0f, (float)juce::jmax(0, getWidth() - 14), 134.0f, 8.0f);
            g.setColour(juce::Colour(0xff324d63));
            g.drawRoundedRectangle(7.0f, (float)previewTop - 5.0f, (float)juce::jmax(0, getWidth() - 14), 134.0f, 8.0f, 1.0f);
            const auto wave = juce::Rectangle<int>(14, previewTop + 24, juce::jmax(0, getWidth() - 28), 36);
            g.setColour(juce::Colour(0xff0c1723));
            g.fillRoundedRectangle(wave.toFloat(), 4.0f);
            if (previewThumbnail.getTotalLength() > 0.0)
            {
                g.setColour(juce::Colour(0xff54d6dc));
                previewThumbnail.drawChannels(g, wave, 0.0, previewThumbnail.getTotalLength(), 0.85f);
                const auto fraction = juce::jlimit(0.0, 1.0, previewTransport.getCurrentPosition() / previewThumbnail.getTotalLength());
                g.setColour(juce::Colour(0xfff0c879));
                g.drawVerticalLine(wave.getX() + (int)(fraction * wave.getWidth()), (float)wave.getY(), (float)wave.getBottom());
            }
        }
        if (category == Category::plugins)
        {
            // Actual in-app One Knob section header, rather than a mockup-only label.
            g.setColour(juce::Colour(0xff54d6dc));
            g.fillRoundedRectangle(12.0f, 177.0f, 3.0f, 10.0f, 1.5f);
            g.setColour(juce::Colour(0xffb8e8ec));
            g.setFont(juce::Font(11.0f, juce::Font::bold));
            g.drawText("ONE KNOB FX", 22, 172, getWidth() - 34, 20, juce::Justification::centredLeft);
        }
        // Obsidian secondary label improves category hint legibility on deep graphite.
        g.setColour(LibertyUI::Colour::textMuted);
        g.setFont(juce::Font(9.0f));
        g.drawText(categoryHint(), 14, getHeight() - 29, getWidth() - 28, 18, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        closeButton.setBounds(getWidth() - 72, 8, 62, 26);
        const int tabGap = 3;
        const int tabWidth = juce::jmax(0, (juce::jmax(0, getWidth() - 16) - tabGap * 5) / 6);
        int tabX = 8;
        for (auto* button : { &filesButton, &audioButton, &midiButton, &presetsButton, &pluginsButton, &spliceButton })
        {
            button->setBounds(tabX, 42, tabWidth, 26);
            tabX += tabWidth + tabGap;
        }

        homeButton.setBounds(12, 105, 70, 24);
        chooseLibraryButton.setBounds(86, 105, 78, 24);
        parentFolderButton.setBounds(category == Category::audio ? 168 : 86, 105,
            juce::jmax(0, getWidth() - (category == Category::audio ? 180 : 98)), 24);
        const int folderFavWidth = juce::jmax(0, (getWidth() - 24) / 2);
        saveFolderButton.setBounds(10, 136, folderFavWidth, 25);
        openFavouriteButton.setBounds(14 + folderFavWidth, 136, folderFavWidth, 25);
        samplePackInfo.setBounds(14, 165, juce::jmax(0, getWidth() - 190), 22);
        browsePacksButton.setBounds(juce::jmax(10, getWidth() - 160), 165, 70, 22);
        musicFilterButton.setBounds(juce::jmax(10, getWidth() - 86), 165, 76, 22);
        sampleNameSearch.setBounds(14, 190, juce::jmax(0, getWidth() - 28), 24);
        samplePackInfo.setFont(juce::Font(10.0f));
        previewInfo.setFont(juce::Font(11.0f, juce::Font::bold));
        fileTree.setBounds(10, category == Category::audio ? 230 : 136,
            juce::jmax(0, getWidth() - 20),
            juce::jmax(0, getHeight() - (category == Category::audio ? 382 : 248)));
        const int previewY = juce::jmax(136, getHeight() - 144);
        const int previewW = juce::jmax(0, (getWidth() - 24) / 2);
        previewInfo.setBounds(14, previewY, juce::jmax(0, getWidth() - 28), 22);
        previewPlay.setBounds(10, previewY + 65, previewW, 26);
        previewStop.setBounds(14 + previewW, previewY + 65, previewW, 26);
        previewVolume.setBounds(10, previewY + 97, juce::jmax(0, getWidth() - 96), 24);
        previewAuto.setBounds(juce::jmax(10, getWidth() - 78), previewY + 97, 68, 24);

        scanPluginsButton.setBounds(10, 108, getWidth() - 20, 28);
        const int blacklistW = juce::jmax(0, (getWidth() - 24) / 2);
        blacklistButton.setBounds(10, 140, blacklistW, 26);
        clearBlacklistButton.setBounds(14 + blacklistW, 140, blacklistW, 26);
        const int oneKnobY = 192;
        const int oneKnobW = juce::jmax(0, (getWidth() - 26) / 4);
        juce::TextButton* oneKnobButtons[] = { &chorusButton, &flangerButton, &phaserButton, &tremoloButton,
            &reverbButton, &delayButton, &driveButton, &compressorButton, &saturationButton, &widthButton,
            &filterButton, &doublerButton, &exciterButton, &deEsserButton, &gateButton, &bassBoostButton, &airButton, &punchButton, &softClipButton, &clearOneKnobButton };
        for (int i = 0; i < 20; ++i)
            oneKnobButtons[i]->setBounds(10 + (i % 4) * (oneKnobW + 2), oneKnobY + (i / 4) * 30, oneKnobW, 28);
        pluginSearch.setBounds(10, 76, juce::jmax(0, getWidth() - 20), 23);
        const int thumbnailW = juce::jmax(0, (getWidth() - 24) / 2);
        thumbnailModeButton.setBounds(10, 343, thumbnailW, 26);
        importThumbnailButton.setBounds(14 + thumbnailW, 343, thumbnailW, 26);
        pluginTree.setBounds(10, 374, juce::jmax(0, getWidth() - 20), juce::jmax(0, getHeight() - 476));
        const int controlsY = getHeight() - 94;
        const int w = juce::jmax(0, (getWidth() - 28) / 4);
        favouritePluginButton.setBounds(10, controlsY, w, 28);
        loadPluginButton.setBounds(14 + w, controlsY, w, 28);
        openPluginButton.setBounds(18 + w * 2, controlsY, w, 28);
        unloadPluginButton.setBounds(22 + w * 3, controlsY, w, 28);
        pluginStatus.setBounds(10, controlsY + 31, getWidth() - 20, 28);
        spliceTitle.setBounds(12, 100, getWidth() - 24, 28);
        spliceInfo.setBounds(12, 128, getWidth() - 24, 52);
        const int spliceButtonW = juce::jmax(0, (getWidth() - 28) / 2);
        spliceOpenPluginButton.setBounds(12, 188, spliceButtonW, 32);
        spliceScanButton.setBounds(16 + spliceButtonW, 188, juce::jmax(0, getWidth() - 28 - spliceButtonW), 32);
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
    void fileClicked(const juce::File& file, const juce::MouseEvent&) override
    {
        if (category != Category::audio || !file.existsAsFile()) return;
        updateSamplePackInfo(file);
        previewTransport.stop();
        previewTransport.setSource(nullptr);
        previewReader.reset();
        previewThumbnail.clear();
        previewInfo.setText("Aucun sample sélectionné", juce::dontSendNotification);
        auto reader = std::unique_ptr<juce::AudioFormatReader>(previewFormats.createReaderFor(file));
        if (reader == nullptr) return;
        const double rate = reader->sampleRate;
        const double seconds = rate > 0.0 ? (double)reader->lengthInSamples / rate : 0.0;
        const int totalSeconds = juce::jmax(0, (int)seconds);
        previewThumbnail.setSource(new juce::FileInputSource(file));
        previewInfo.setText(file.getFileNameWithoutExtension() + "  |  " + file.getFileExtension().trimCharactersAtStart(".").toUpperCase() + "  |  " + juce::String(totalSeconds / 60)
                            + ":" + juce::String(totalSeconds % 60).paddedLeft('0', 2),
                            juce::dontSendNotification);
        previewInfo.setTooltip(file.getFullPathName() + "  |  " + juce::String(seconds, 2) + " s");
        previewReader = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
        previewTransport.setSource(previewReader.get(), 0, nullptr, rate);
        previewTransport.setGain((float)previewVolume.getValue());
        if (previewAuto.getToggleState()) previewTransport.start();
    }
    void fileDoubleClicked(const juce::File& file) override { openFileOrDirectory(file); }
    void browserRootChanged(const juce::File&) override {}

    void setBrowserOpen(bool shouldOpen)
    {
        if(!shouldOpen)
        {
            previewTransport.stop();
            targetEffectSlot=-1;
            targetEffectLane=-1;
        }
        browserOpen = shouldOpen;
        setVisible(browserOpen);
        if (browserOpen) toFront(false);
        adjustHostWindow(browserOpen);
        // The mixer claims only the left viewport, so the right plugin
        // browser never covers channel strips or floating fader controls.
        resizeLibertyMixConsole(&owner);
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

    static juce::File sampleFolderFavouritesFile()
    {
        return favouritesFile().getSiblingFile("SampleFolderFavourites.txt");
    }

    static juce::StringArray sampleFolderFavourites()
    {
        juce::StringArray paths;
        sampleFolderFavouritesFile().readLines(paths);
        paths.trim(); paths.removeEmptyStrings(); paths.removeDuplicates(false);
        return paths;
    }

    static juce::File sampleFavouriteFile()
    {
        return libraryPreferenceFile().getSiblingFile("SampleFileFavourites.txt");
    }

    void toggleSampleFavourite(const juce::File& file)
    {
        juce::StringArray favourites;
        sampleFavouriteFile().readLines(favourites);
        const auto path = file.getFullPathName();
        if (favourites.contains(path)) favourites.removeString(path);
        else favourites.add(path);
        favourites.removeDuplicates(false);
        sampleFavouriteFile().replaceWithText(favourites.joinIntoString("\n"));
    }

    static juce::File sampleFilterPreferenceFile()
    {
        return libraryPreferenceFile().getSiblingFile("SampleSearchPreferences.txt");
    }

    static juce::StringArray sampleFilterNames()
    {
        return { "Kicks", "Snares", "Hi-Hats", "Loops", "FX" };
    }

    static int sampleCategoryMask(const juce::File& file)
    {
        const auto name = (file.getFileNameWithoutExtension() + " " + file.getParentDirectory().getFileName()).toLowerCase();
        const juce::StringArray tokens = juce::StringArray::fromTokens(name.replaceCharacters("-_.", "   "), " ", "");
        auto has = [&tokens](const juce::StringArray& words)
        {
            for (const auto& word : words)
                if (tokens.contains(word) || (word.length() > 3 && tokens.joinIntoString(" ").contains(word))) return true;
            return false;
        };
        int mask = 0;
        if (has({ "kick", "kicks", "bassdrum", "bd" })) mask |= 1;
        if (has({ "snare", "snares", "snr" })) mask |= 2;
        if (has({ "hihat", "hihats", "hat", "hats", "hh" })) mask |= 4;
        if (has({ "loop", "loops", "groove", "grooves" })) mask |= 8;
        if (has({ "fx", "sfx", "effect", "effects", "impact", "riser" })) mask |= 16;
        return mask;
    }

    void saveSampleFilterPreferences()
    {
        sampleFilterPreferenceFile().replaceWithText(juce::String(sampleFilterMask) + "\n" + sampleNameQuery);
    }

    void showMusicalFilters()
    {
        juce::PopupMenu menu;
        menu.addItem(1, "TOUTES LES CATEGORIES", true, sampleFilterMask == 0);
        menu.addSeparator();
        const auto names = sampleFilterNames();
        for (int i = 0; i < names.size(); ++i)
            menu.addItem(i + 2, names[i], true, (sampleFilterMask & (1 << i)) != 0);
        menu.addSeparator();
        menu.addItem(8, "RECHERCHER", !sampleSearchRunning.load());
        juce::Component::SafePointer<BrowserPanel> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&musicFilterButton), [safe](int result)
        {
            if (safe == nullptr || result == 0) return;
            if (result == 1) safe->sampleFilterMask = 0;
            else if (result >= 2 && result <= 6) safe->sampleFilterMask ^= (1 << (result - 2));
            if (result != 8) safe->saveSampleFilterPreferences();
            if (safe->sampleSearchRunning.load())
            {
                safe->sampleSearchPending = true;
                safe->sampleSearchChangedAt = juce::Time::getMillisecondCounter();
            }
            else safe->showMusicalFilterResults();
        });
    }

    void showMusicalFilterResults()
    {
        const auto library = findInstalledSampleLibrary();
        if (!library.isDirectory())
        {
            samplePackInfo.setText("Aucune bibliotheque de samples disponible.", juce::dontSendNotification);
            return;
        }
        if (sampleSearchRunning.load())
        {
            sampleSearchPending = true;
            sampleSearchChangedAt = juce::Time::getMillisecondCounter();
            return;
        }
        if (sampleSearchThread.joinable()) sampleSearchThread.join();
        sampleSearchRunning.store(true);
        sampleSearchPending = false;
        musicFilterButton.setEnabled(false);
        samplePackInfo.setText("Recherche de samples en cours...", juce::dontSendNotification);
        const int mask = sampleFilterMask;
        const auto query = sampleNameQuery.trim();
        const bool reuseIndex = sampleIndexLibrary == library && sampleIndexComplete;
        const auto cachedIndex = reuseIndex ? sampleIndex : juce::Array<juce::File>();
        juce::Component::SafePointer<BrowserPanel> safe(this);
        sampleSearchThread = std::thread([this, safe, library, mask, query, reuseIndex, cachedIndex]
        {
            juce::Array<juce::File> matches;
            juce::Array<juce::File> indexedFiles = reuseIndex ? cachedIndex : juce::Array<juce::File>();
            int inspected = 0;
            bool reachedLimit = false;
            int counts[5] = { 0, 0, 0, 0, 0 };
            if (!reuseIndex)
            {
                juce::RangedDirectoryIterator iterator(library, true, "*", juce::File::findFiles);
                for (const auto& entry : iterator)
                {
                    if (stopped.load()) return;
                    if (++inspected > 12000) { reachedLimit = true; break; }
                    const auto file = entry.getFile();
                    if (file.hasFileExtension("wav;aif;aiff;mp3;flac")) indexedFiles.add(file);
                }
            }
            else inspected = indexedFiles.size();
            for (const auto& file : indexedFiles)
            {
                if (stopped.load()) return;
                const int categories = sampleCategoryMask(file);
                for (int i = 0; i < 5; ++i) if ((categories & (1 << i)) != 0) ++counts[i];
                if (mask != 0 && (categories & mask) == 0) continue;
                if (query.isNotEmpty() && !file.getFileName().containsIgnoreCase(query)) continue;
                if (matches.size() < 80) matches.add(file);
            }
            if (stopped.load()) return;
            juce::StringArray details;
            juce::AudioFormatManager metadataFormats;
            metadataFormats.registerBasicFormats();
            for (const auto& file : matches)
            {
                juce::String duration = "--:--";
                std::unique_ptr<juce::AudioFormatReader> reader(metadataFormats.createReaderFor(file));
                if (reader != nullptr && reader->sampleRate > 0.0)
                {
                    const int seconds = (int)(reader->lengthInSamples / reader->sampleRate);
                    duration = juce::String(seconds / 60) + ":" + juce::String(seconds % 60).paddedLeft('0', 2);
                }
                details.add(file.getFileExtension().toUpperCase().trimCharactersAtStart(".") + "  |  " + duration);
            }
            const juce::StringArray labels = sampleFilterNames();
            juce::String summary = juce::String(matches.size()) + " resultats / " + juce::String(inspected) + " fichiers | ";
            for (int i = 0; i < 5; ++i)
                summary += labels[i] + ": " + juce::String(counts[i]) + (i == 4 ? "" : "  ");
            juce::MessageManager::callAsync([safe, matches, details, summary, mask, query, library, indexedFiles, reuseIndex, reachedLimit]
            {
                if (safe == nullptr || safe->stopped.load()) return;
                safe->sampleSearchRunning.store(false);
                safe->musicFilterButton.setEnabled(true);
                if (!reuseIndex)
                {
                    safe->sampleIndex = indexedFiles;
                    safe->sampleIndexLibrary = library;
                    safe->sampleIndexComplete = !reachedLimit;
                }
                if (safe->category != Category::audio) return;
                if (safe->sampleSearchPending || safe->sampleFilterMask != mask || safe->sampleNameQuery.trim() != query)
                {
                    safe->sampleSearchPending = true;
                    safe->sampleSearchChangedAt = juce::Time::getMillisecondCounter();
                    return;
                }
                safe->samplePackInfo.setText(summary, juce::dontSendNotification);
                safe->samplePackInfo.setTooltip(summary + " (12 000 fichiers maximum, 80 resultats affiches)");
                juce::StringArray favourites;
                sampleFavouriteFile().readLines(favourites);
                juce::PopupMenu results;
                for (int i = 0; i < matches.size(); ++i)
                {
                    const auto& file = matches.getReference(i);
                    const bool favourite = favourites.contains(file.getFullPathName());
                    results.addItem(i + 1, (favourite ? "[FAV]  " : "") + file.getFileNameWithoutExtension()
                        + "  |  " + details[i] + "  |  " + file.getParentDirectory().getFileName());
                }
                if (matches.isEmpty()) results.addItem(1, "Aucun resultat", false);
                if (!matches.isEmpty())
                {
                    juce::PopupMenu favouriteMenu;
                    for (int i = 0; i < matches.size(); ++i)
                        favouriteMenu.addItem(1001 + i, (favourites.contains(matches.getReference(i).getFullPathName()) ? "RETIRER  " : "AJOUTER  ")
                            + matches.getReference(i).getFileName());
                    results.addSeparator();
                    results.addSubMenu("GERER LES FAVORIS", favouriteMenu);
                }
                results.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&safe->musicFilterButton),
                    [safe, matches](int result)
                    {
                        if (safe == nullptr || result < 1) return;
                        if (result >= 1001 && result < 1001 + matches.size())
                        {
                            safe->toggleSampleFavourite(matches.getReference(result - 1001));
                            safe->samplePackInfo.setTooltip("Favoris de samples mis a jour");
                            return;
                        }
                        if (result > matches.size()) return;
                        const auto file = matches.getReference(result - 1);
                        if (!file.existsAsFile()) return;
                        if (safe->category != Category::audio) return;
                        safe->setRoot(file.getParentDirectory());
                        safe->fileTree.setSelectedFile(file);
                        safe->updateSamplePackInfo(file);
                    });
            });
        });
    }

    void showInstalledPacks()
    {
        const auto library = findInstalledSampleLibrary();
        if (!library.isDirectory()) return;
        juce::PopupMenu menu;
        juce::Array<juce::File> packFolders;
        juce::Array<juce::File> manufacturers;
        library.findChildFiles(manufacturers, juce::File::findDirectories, false);
        std::sort(manufacturers.begin(), manufacturers.end(), [](const auto& a, const auto& b)
        {
            return a.getFileName().compareNatural(b.getFileName(), false) < 0;
        });
        for (const auto& manufacturer : manufacturers)
        {
            juce::Array<juce::File> packs;
            manufacturer.findChildFiles(packs, juce::File::findDirectories, false);
            std::sort(packs.begin(), packs.end(), [](const auto& a, const auto& b)
            {
                return a.getFileName().compareNatural(b.getFileName(), false) < 0;
            });
            juce::PopupMenu submenu;
            for (const auto& pack : packs)
            {
                juce::PopupMenu packMenu;
                packFolders.add(pack);
                packMenu.addItem(packFolders.size(), "OUVRIR LE PACK");
                juce::Array<juce::File> categories;
                pack.findChildFiles(categories, juce::File::findDirectories, false);
                std::sort(categories.begin(), categories.end(), [](const auto& a, const auto& b)
                {
                    return a.getFileName().compareNatural(b.getFileName(), false) < 0;
                });
                if (!categories.isEmpty()) packMenu.addSeparator();
                for (const auto& folder : categories)
                {
                    packFolders.add(folder);
                    packMenu.addItem(packFolders.size(), folder.getFileName());
                }
                submenu.addSubMenu(pack.getFileName(), packMenu);
            }
            if (!packs.isEmpty()) menu.addSubMenu(manufacturer.getFileName(), submenu);
        }
        if (packFolders.isEmpty())
        {
            menu.addItem(1, "Aucun pack trouvé dans la bibliothèque", false);
        }
        juce::Component::SafePointer<BrowserPanel> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&browsePacksButton),
            [safe, packFolders](int result)
            {
                if (safe == nullptr || result < 1 || result > packFolders.size()) return;
                if (packFolders[result - 1].isDirectory()) safe->setRoot(packFolders[result - 1]);
            });
    }

    void updateSamplePackInfo(const juce::File& selected = {})
    {
        const auto library = findInstalledSampleLibrary();
        const auto location = selected.existsAsFile() ? selected.getParentDirectory() : rootDirectory;
        juce::String manufacturer = "Non renseigné";
        juce::String pack = location.getFileName();
        juce::String categoryName = "Autres";
        if (library.isDirectory() && (location == library || location.isAChildOf(library)))
        {
            const auto relative = location.getRelativePathFrom(library).replaceCharacter('\\', '/');
            const auto parts = juce::StringArray::fromTokens(relative, "/", "");
            if (parts.size() >= 1 && location != library) manufacturer = parts[0];
            if (parts.size() >= 2) pack = parts[1];
            if (parts.size() >= 3) categoryName = parts[2];
            if (location == library) pack = "Racine de la bibliothèque";
        }
        const auto summary = "FABRICANT : " + manufacturer + "  |  PACK : " + pack
                           + "  |  CATÉGORIE : " + categoryName;
        samplePackInfo.setText(summary, juce::dontSendNotification);
        samplePackInfo.setTooltip(summary + "\nDossier : " + location.getFullPathName());
    }

    void updateSampleFolderControls()
    {
        const auto path = rootDirectory.getFullPathName();
        const bool saved = sampleFolderFavourites().contains(path);
        saveFolderButton.setButtonText(saved ? "RETIRER FAVORI" : "AJOUTER FAVORI");
        saveFolderButton.setTooltip(path);
        openFavouriteButton.setTooltip("Dossiers favoris de samples");
        parentFolderButton.setTooltip(path);
    }

    static juce::File libraryPreferenceFile()
    {
        return favouritesFile().getSiblingFile("SampleLibraryPath.txt");
    }

    void chooseSampleLibrary()
    {
        auto chooser = std::make_shared<juce::FileChooser>("Choisir le dossier de samples", rootDirectory);
        juce::Component::SafePointer<BrowserPanel> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [safe, chooser](const juce::FileChooser& selected)
            {
                if (safe == nullptr) return;
                const auto folder = selected.getResult();
                if (!folder.isDirectory()) return;
                if (libraryPreferenceFile().replaceWithText(folder.getFullPathName()))
                {
                    safe->sampleIndex.clear();
                    safe->sampleIndexComplete = false;
                    safe->sampleIndexLibrary = juce::File();
                    safe->setRoot(folder);
                }
            });
    }

    void importSelectedThumbnail()
    {
        const auto* selected = selectedPluginDescription();
        if (selected == nullptr)
        {
            pluginStatus.setText("Sélectionnez d'abord un plugin.", juce::dontSendNotification);
            return;
        }
        const auto identifier = selected->createIdentifierString();
        auto chooser = std::make_shared<juce::FileChooser>("Importer une miniature PNG", juce::File(), "*.png");
        juce::Component::SafePointer<BrowserPanel> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe, chooser, identifier](const juce::FileChooser& selectedFile)
            {
                if (safe == nullptr) return;
                const auto source = selectedFile.getResult();
                if (!source.existsAsFile() || !juce::ImageFileFormat::loadFrom(source).isValid())
                {
                    safe->pluginStatus.setText("Image PNG invalide.", juce::dontSendNotification);
                    return;
                }
                const auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("Liberty").getChildFile("PluginThumbnails");
                const auto destination = folder.getChildFile(juce::String::toHexString(
                    (juce::int64) identifier.hashCode64()) + ".png");
                // Stage the new image before replacing an existing thumbnail.
                const auto staged = folder.getNonexistentChildFile("thumbnail-import", ".png", false);
                const bool copied = folder.createDirectory() && source.copyFileTo(staged);
                const bool saved = copied && staged.replaceFileIn(destination);
                if (!saved) staged.deleteFile();
                if (saved)
                {
                    safe->rebuildPluginTree();
                    safe->pluginStatus.setText("Miniature importée.", juce::dontSendNotification);
                }
                else safe->pluginStatus.setText("Impossible de copier la miniature.", juce::dontSendNotification);
            });
    }

    void setRoot(const juce::File& directory)
    {
        if (!directory.isDirectory()) return;
        rootDirectory = directory;
        directoryList.setDirectory(rootDirectory, true, true);
        fileTree.refresh();
        updateSampleFolderControls();
        updateSamplePackInfo();
    }

    static juce::File findInstalledSampleLibrary()
    {
        // Discover an already installed partner library without creating folders,
        // downloading content, or assuming redistribution permissions.
        const auto configured = juce::File(libraryPreferenceFile().loadFileAsString().trim());
        if (configured.isDirectory()) return configured;
        const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
        const juce::File candidates[] = {
            juce::File("/Users/Shared/Liberty Music Software/Sample Libraries"),
            home.getChildFile("Music/Liberty Music Software/Sample Libraries"),
            home.getChildFile("Music/Liberty/Sample Libraries")
        };
        for (const auto& candidate : candidates)
            if (candidate.isDirectory()) return candidate;
        return {};
    }

    void setCategory(Category newCategory)
    {
        // Changing tabs abandons the explicit FX target instead of
        // unexpectedly loading a plugin into a stale slot later.
        if(newCategory!=Category::plugins)
        {
            targetEffectSlot=-1;
            targetEffectLane=-1;
        }
        if (newCategory != Category::audio) previewTransport.stop();
        category = newCategory;
        homeButton.setButtonText(category == Category::audio ? "LIBRARY" : "HOME");
        chooseLibraryButton.setVisible(category == Category::audio);
        saveFolderButton.setVisible(category == Category::audio);
        openFavouriteButton.setVisible(category == Category::audio);
        samplePackInfo.setVisible(category == Category::audio);
        browsePacksButton.setVisible(category == Category::audio);
        musicFilterButton.setVisible(category == Category::audio);
        sampleNameSearch.setVisible(category == Category::audio);
        resized();
        if (category == Category::audio)
        {
            if (sampleNameQuery.isNotEmpty() || sampleFilterMask != 0)
            {
                sampleSearchPending = true;
                sampleSearchChangedAt = juce::Time::getMillisecondCounter();
            }
            const auto library = findInstalledSampleLibrary();
            if (library.isDirectory()) setRoot(library);
        }
        previewPlay.setVisible(category == Category::audio);
        previewStop.setVisible(category == Category::audio);
        previewVolume.setVisible(category == Category::audio);
        previewAuto.setVisible(category == Category::audio);
        previewInfo.setVisible(category == Category::audio);
        filesButton.setToggleState(category == Category::files, juce::dontSendNotification);
        audioButton.setToggleState(category == Category::audio, juce::dontSendNotification);
        midiButton.setToggleState(category == Category::midi, juce::dontSendNotification);
        presetsButton.setToggleState(category == Category::presets, juce::dontSendNotification);
        pluginsButton.setToggleState(category == Category::plugins, juce::dontSendNotification);
        spliceButton.setToggleState(category == Category::splice, juce::dontSendNotification);
        const bool pluginMode = category == Category::plugins;
        const bool spliceMode = category == Category::splice;
        fileTree.setVisible(!pluginMode && !spliceMode);
        homeButton.setVisible(!pluginMode && !spliceMode);
        parentFolderButton.setVisible(!pluginMode && !spliceMode);
        spliceTitle.setVisible(spliceMode); spliceInfo.setVisible(spliceMode); spliceOpenPluginButton.setVisible(spliceMode); spliceScanButton.setVisible(spliceMode);
        if (spliceMode && !scanning.load())
        {
            refreshPlugins();
            const bool found = std::any_of(pluginDescriptions.begin(), pluginDescriptions.end(), [](const auto& d)
            {
                return d.isInstrument && d.name.containsIgnoreCase("Splice") && d.name.containsIgnoreCase("Sounds");
            });
            spliceOpenPluginButton.setEnabled(true);
            spliceInfo.setText(found ? "Splice Sounds détecté - prêt à ouvrir."
                                     : "Splice Sounds non détecté - OUVRIR lancera automatiquement un scan.", juce::dontSendNotification);
        }
        pluginTree.setVisible(pluginMode);
        thumbnailModeButton.setVisible(pluginMode);
        importThumbnailButton.setVisible(pluginMode);
        pluginSearch.setVisible(!spliceMode);
        pluginSearch.setTextToShowWhenEmpty(pluginMode ? "Rechercher un plugin ou fabricant..."
                                                   : "Rechercher un fichier...", juce::Colour(0xff8da6b1));
        filter.category = category == Category::audio ? 1 : category == Category::midi ? 2
                          : category == Category::presets ? 3 : category == Category::files ? 4 : 0;
        filter.query = pluginSearch.getText().trim();
        if (!pluginMode && !spliceMode) directoryList.refresh();
        scanPluginsButton.setVisible(pluginMode);
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
        if (file.isDirectory()) return true;
        const auto ext = file.getFileExtension().toLowerCase();
        if (category == Category::files) return ext == ".bsmproj";
        if (category == Category::audio) return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".mp3" || ext == ".flac";
        if (category == Category::midi) return ext == ".mid" || ext == ".midi";
        if (category == Category::presets) return ext == ".xml" || ext == ".fxp" || ext == ".vstpreset" || ext == ".aupreset";
        return false;
    }

    juce::String categoryHint() const
    {
        switch (category)
        {
            case Category::files: return "Projets Liberty (.bsmproj)";
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
        if (file.hasFileExtension("bsmproj"))
        {
            // Use the existing project-open workflow, which owns unsaved-change
            // checks, rather than loading a file behind the user's back.
            owner.openProject();
            return;
        }
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
        owner.isPlaying = false;
        owner.playheadSeconds = 0.0;
        owner.selectedTrack = track;
        owner.rebuildWaveformCache(track);
        owner.repaint();
    }

    PluginTreeItem* makePluginItem(const juce::PluginDescription& d)
    {
        return new PluginTreeItem(PluginTreeItem::Kind::plugin, d.name, &d,
            [this](const juce::PluginDescription& x) { loadPluginDescription(x); },
            [this](const juce::PluginDescription& x) { return isFavouritePlugin(x); },
            [this](const juce::PluginDescription& x) { toggleFavourite(x); },
            thumbnailModeButton.getToggleState());
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

        const auto query = pluginSearch.getText().trim();
        const auto matches = [&query](const juce::PluginDescription& d)
        {
            return query.isEmpty() || d.name.containsIgnoreCase(query)
                || d.manufacturerName.containsIgnoreCase(query)
                || d.pluginFormatName.containsIgnoreCase(query);
        };
        auto* fav = new PluginTreeItem(PluginTreeItem::Kind::manufacturer, "FAVORIS");
        pluginRoot->addSubItem(fav);
        for (const auto& d : pluginDescriptions)
            if (isFavouritePlugin(d) && matches(d)) fav->addSubItem(makePluginItem(d));

        // Library-style hierarchy: instruments and audio effects are distinct.
        // Keep manufacturers inside each family, and never classify unknown
        // plugins as MIDI effects without reliable plugin metadata.
        auto* instruments = new PluginTreeItem(PluginTreeItem::Kind::manufacturer, "INSTRUMENTS");
        auto* audioEffects = new PluginTreeItem(PluginTreeItem::Kind::manufacturer, "EFFETS AUDIO");
        pluginRoot->addSubItem(instruments);
        pluginRoot->addSubItem(audioEffects);
        for (int family = 0; family < 2; ++family)
        {
            auto* parent = family == 0 ? instruments : audioEffects;
            juce::String current;
            PluginTreeItem* group = nullptr;
            for (const auto& d : pluginDescriptions)
            {
                if (d.isInstrument != (family == 0) || !matches(d)) continue;
                auto manufacturer = d.manufacturerName.trim();
                if (manufacturer.isEmpty()) manufacturer = "AUTRES";
                if (manufacturer != current)
                {
                    current = manufacturer;
                    group = new PluginTreeItem(PluginTreeItem::Kind::manufacturer, manufacturer);
                    parent->addSubItem(group);
                }
                if (group != nullptr) group->addSubItem(makePluginItem(d));
            }
        }
        // During a search, reveal matching manufacturers without requiring
        // the user to expand every nested folder manually.
        if (query.isNotEmpty())
        {
            for (auto* family : { instruments, audioEffects })
                for (int i = 0; i < family->getNumSubItems(); ++i)
                    family->getSubItem(i)->setOpen(true);
        }
        pluginTree.setRootItem(pluginRoot.get());
        pluginRoot->setOpen(true); fav->setOpen(true); instruments->setOpen(true); audioEffects->setOpen(true); pluginTree.repaint();
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
        spliceScanButton.setEnabled(false);
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
        if(targetEffectSlot>=0)
        {
            if(d.isInstrument)
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,"Liberty - Effets",
                    "Sélectionne un effet audio AU ou VST3, pas un instrument.","OK");
                return;
            }
            auto& host=LibertyPluginHost::instance();
            auto& one=LibertyOneKnobManager::instance();
            const int lane=targetEffectLane,slot=targetEffectSlot;
            const bool instrument=targetIsInstrument;
            const int key=instrument?100000+lane*8+slot:lane*8+slot;
            const bool valid=lane>=0&&slot>=0&&slot<LibertyPluginHost::effectSlotsPerTrack&&
                (instrument?lane<owner.getInstrumentTrackCount()
                           :lane<owner.getAudioTrackCount());
            const bool occupied=one.hasEffect(key)||
                (instrument?host.hasEffectForInstrumentTrackSlot(lane,slot)
                           :host.hasEffectForTrackSlot(lane,slot));
            if(!valid||occupied)
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,"Liberty - Effets",
                    "Ce slot n'est plus libre. Rouvre la chaîne d'effets.","OK");
                return;
            }
            juce::String error;
            const bool ok=instrument
                ?host.loadEffectForInstrumentTrackSlot(lane,slot,d,error)
                :host.loadEffectForTrackSlot(lane,slot,d,error);
            if(!ok)
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,"Liberty - Plugin",error,"OK");
                return;
            }
            toggleButton.setToggleState(false,juce::dontSendNotification);
            setBrowserOpen(false);
            refreshLibertyMixConsole(&owner);
            refreshPluginStatus();
            owner.repaint();
            if(instrument)host.showEffectEditorForInstrumentTrackSlot(lane,slot);
            else host.showEditorForTrackSlot(lane,slot);
            return;
        }
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
            const int instrumentLane = selectedInstrumentTrack();
            if (instrumentLane >= 0)
            {
                int slot = -1;
                for (int i = 0; i < LibertyPluginHost::effectSlotsPerTrack; ++i)
                    if (!host.hasEffectForInstrumentTrackSlot(instrumentLane, i)) { slot = i; break; }
                if (slot < 0) { error = "Les 8 slots FX de cette piste Instrument sont occupés."; }
                else
                {
                    ok = host.loadEffectForInstrumentTrackSlot(instrumentLane, slot, d, error);
                    if (ok) host.showEffectEditorForInstrumentTrackSlot(instrumentLane, slot);
                }
            }
            else
            {
                const int track = selectedAudioTrack();
                if (track < 0)
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Effet",
                                                           "Sélectionne une piste Audio ou Instrument pour charger cet effet.", "OK");
                    return;
                }
                int slot = -1;
                for (int i = 0; i < LibertyPluginHost::effectSlotsPerTrack; ++i)
                    if (!host.hasEffectForTrackSlot(track, i)) { slot = i; break; }
                if (slot < 0) { error = "Les 8 slots FX de cette piste Audio sont occupés."; }
                else
                {
                    ok = host.loadEffectForTrackSlot(track, slot, d, error);
                    if (ok) host.showEditorForTrackSlot(track, slot);
                }
            }
        }
        if (!ok) juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Plugin", error, "OK");
        refreshPluginStatus();
        refreshLibertyMixConsole(&owner);
        owner.repaint();
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

    int firstFreeOneKnobInsertSlot() const
    {
        auto& host = LibertyPluginHost::instance();
        auto& one = LibertyOneKnobManager::instance();
        if (const int instrumentLane = selectedInstrumentTrack(); instrumentLane >= 0)
        {
            for (int slot = 0; slot < LibertyPluginHost::effectSlotsPerTrack; ++slot)
                if (!host.hasEffectForInstrumentTrackSlot(instrumentLane, slot)
                    && !one.hasEffect(100000 + instrumentLane * 8 + slot)) return slot;
        }
        else if (const int audioLane = selectedAudioTrack(); audioLane >= 0)
        {
            for (int slot = 0; slot < LibertyPluginHost::effectSlotsPerTrack; ++slot)
                if (!host.hasEffectForTrackSlot(audioLane, slot)
                    && !one.hasEffect(audioLane * 8 + slot)) return slot;
        }
        return -1;
    }

    void loadOneKnob(LibertyOneKnobRack::Type type)
    {
        // When an empty slot invoked the browser, honor that exact slot ID.
        // The ordinary browser continues using the next free insert.
        const int insertSlot = targetEffectSlot>=0?targetEffectSlot:firstFreeOneKnobInsertSlot();
        const int instrumentLane = targetEffectSlot>=0
            ?(targetIsInstrument?targetEffectLane:-1):selectedInstrumentTrack();
        const int audioLane = targetEffectSlot>=0
            ?(targetIsInstrument?-1:targetEffectLane):selectedAudioTrack();
        if (insertSlot < 0 ||
            (instrumentLane>=0 && instrumentLane>=owner.getInstrumentTrackCount()) ||
            (audioLane>=0 && audioLane>=owner.getAudioTrackCount()))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - One Knob",
                                                   (instrumentLane < 0 && audioLane < 0)
                                                       ? "Sélectionne une piste Audio ou Instrument pour charger ce One Knob."
                                                       : "Les 8 inserts de cette piste sont déjà occupés.",
                                                   "OK");
            return;
        }
        const int key = instrumentLane >= 0 ? 100000 + instrumentLane * 8 + insertSlot : audioLane * 8 + insertSlot;
        auto& manager = LibertyOneKnobManager::instance();
        auto& host = LibertyPluginHost::instance();
        if((instrumentLane>=0?host.hasEffectForInstrumentTrackSlot(instrumentLane,insertSlot)
                             :host.hasEffectForTrackSlot(audioLane,insertSlot)) ||
           manager.hasEffect(key))
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,"Liberty - One Knob",
                "Ce slot FX est déjà occupé.","OK");
            return;
        }
        manager.setEffect(key, type);
        if(targetEffectSlot>=0)
        {
            toggleButton.setToggleState(false,juce::dontSendNotification);
            setBrowserOpen(false);
        }
        manager.showEditor(key);
        refreshPluginStatus();
        refreshLibertyMixConsole(&owner);
        owner.repaint();
    }

    void clearOneKnob()
    {
        auto& manager = LibertyOneKnobManager::instance();
        const int instrumentLane = selectedInstrumentTrack();
        const int audioLane = selectedAudioTrack();
        for (int slot = 0; slot < LibertyPluginHost::effectSlotsPerTrack; ++slot)
        {
            const int key = instrumentLane >= 0 ? 100000 + instrumentLane * 8 + slot
                                               : (audioLane >= 0 ? audioLane * 8 + slot : -1);
            if (key >= 0 && manager.hasEffect(key))
            {
                manager.clearEffect(key);
                refreshPluginStatus();
                refreshLibertyMixConsole(&owner);
                owner.repaint();
                return;
            }
        }
        if (instrumentLane < 0 && audioLane < 0)
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - One Knob",
                                                   "Sélectionne une piste Audio ou Instrument pour retirer le One Knob.", "OK");
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
            spliceInfo.setText("Splice Sounds non détecté - lancement du scan AU + VST3...", juce::dontSendNotification);
            scanPlugins();
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
        auto& host = LibertyPluginHost::instance();
        for (int slot = 0; slot < LibertyPluginHost::effectSlotsPerTrack; ++slot)
            if (host.hasEffectForTrackSlot(track, slot)) { host.showEditorForTrackSlot(track, slot); return; }
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
            auto& host = LibertyPluginHost::instance();
            for (int slot = 0; slot < LibertyPluginHost::effectSlotsPerTrack; ++slot)
                if (host.hasEffectForTrackSlot(track, slot)) { host.unloadEffectForTrackSlot(track, slot); break; }
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
            for (int slot = 0; slot < LibertyPluginHost::effectSlotsPerTrack; ++slot)
            {
                const int oneKey = logical * 8 + slot;
                if (host.hasEffectForTrackSlot(logical, slot))
                    text << "FX" << (slot + 1) << ": " << host.getEffectNameForTrackSlot(logical, slot) << "   ";
                else if (oneKnob.hasEffect(oneKey))
                    text << "FX" << (slot + 1) << ": " << oneKnob.getName(oneKey) << "   ";
            }
        }
        else if (logical >= firstInstrument && logical < firstInstrument + owner.getInstrumentTrackCount())
        {
            const int instrumentLane = logical - firstInstrument;
            if (host.hasInstrumentForTrack(instrumentLane)) text << "INST: " << host.getInstrumentNameForTrack(instrumentLane) << "   ";
            auto& oneKnob = LibertyOneKnobManager::instance();
            for (int slot = 0; slot < LibertyPluginHost::effectSlotsPerTrack; ++slot)
            {
                const int oneKey = 100000 + instrumentLane * 8 + slot;
                if (host.hasEffectForInstrumentTrackSlot(instrumentLane, slot))
                    text << "FX" << (slot + 1) << ": " << host.getEffectNameForInstrumentTrackSlot(instrumentLane, slot) << "   ";
                else if (oneKnob.hasEffect(oneKey))
                    text << "FX" << (slot + 1) << ": " << oneKnob.getName(oneKey) << "   ";
            }
        }
        if (text.isEmpty()) text = juce::String(pluginDescriptions.size()) + " plugins  " + juce::String(favouriteKeys.size()) + " favoris";
        pluginStatus.setText(text, juce::dontSendNotification);
    }

    void timerCallback() override
    {
        if (stopped.load()) return;
        if (category == Category::audio && previewTransport.isPlaying()) repaint();
        if (category == Category::audio && sampleSearchPending && !sampleSearchRunning.load()
            && juce::Time::getMillisecondCounter() - sampleSearchChangedAt >= 350)
        {
            sampleSearchPending = false;
            showMusicalFilterResults();
        }
        const int width = juce::jlimit(260, juce::jmax(260, owner.getWidth() - 220), getLibertyPreferredBrowserWidth(&owner));
        const int panelX = juce::jmax(0, owner.getWidth() - width);
        if (browserOpen)
        {
            const auto wanted = juce::Rectangle<int>(panelX, topBarHeight, juce::jmin(width, owner.getWidth()), juce::jmax(1, owner.getHeight() - topBarHeight));
            if (getBounds() != wanted) setBounds(wanted);
            if (!isVisible()) setVisible(true);
        }

        // The browser starts after PERFORMANCE, never on top of it.
        const auto buttonBounds = juce::Rectangle<int>(LibertyPageNavigation::browserX,
                                                     LibertyPageNavigation::top,
                                                     LibertyPageNavigation::browserWidth,
                                                     LibertyPageNavigation::height);
        if (toggleButton.getBounds() != buttonBounds) toggleButton.setBounds(buttonBounds);

        if (category == Category::plugins || category == Category::splice)
        {
            if (scanning.load())
            {
                if (category == Category::plugins) pluginStatus.setText(getScanStatus(), juce::dontSendNotification);
                else spliceInfo.setText(getScanStatus(), juce::dontSendNotification);
            }
            else if (scanFinishedPending.exchange(false))
            {
                if (scanThread.joinable()) scanThread.join();
                refreshPlugins();
                for (auto* b : { &scanPluginsButton, &blacklistButton, &clearBlacklistButton, &favouritePluginButton,
                                 &loadPluginButton, &openPluginButton, &unloadPluginButton }) b->setEnabled(true);
                spliceScanButton.setEnabled(true);
                if (category == Category::splice)
                {
                    const bool found = std::any_of(pluginDescriptions.begin(), pluginDescriptions.end(), [](const auto& d)
                    {
                        return d.isInstrument && d.name.containsIgnoreCase("Splice") && d.name.containsIgnoreCase("Sounds");
                    });
                    spliceInfo.setText(found ? "Splice Sounds détecté - prêt à ouvrir."
                                             : "Splice Sounds non détecté après le scan AU + VST3.", juce::dontSendNotification);
                    spliceOpenPluginButton.setEnabled(found);
                }
            }
            else if (category == Category::plugins) refreshPluginStatus();
        }
    }

    MainComponent& owner;
    juce::TimeSliceThread thread { "Liberty Browser" };
    BrowserFileFilter filter;
    juce::DirectoryContentsList directoryList;
    juce::FileTreeComponent fileTree;
    juce::TreeView pluginTree;
    juce::TextButton thumbnailModeButton, importThumbnailButton, chooseLibraryButton, saveFolderButton, openFavouriteButton;
    juce::TextEditor pluginSearch;
    std::unique_ptr<PluginTreeItem> pluginRoot;
    juce::Array<juce::PluginDescription> pluginDescriptions;
    juce::StringArray favouriteKeys;
    juce::TextButton toggleButton, closeButton, filesButton, audioButton, midiButton, presetsButton, pluginsButton, spliceButton, homeButton, parentFolderButton, previewPlay, previewStop;
    juce::Slider previewVolume;
    juce::ToggleButton previewAuto;
    juce::Label previewInfo, samplePackInfo;
    juce::TextButton browsePacksButton, musicFilterButton;
    juce::TextEditor sampleNameSearch;
    juce::String sampleNameQuery;
    juce::Array<juce::File> sampleIndex;
    juce::File sampleIndexLibrary;
    bool sampleIndexComplete = false;
    int sampleFilterMask = 0;
    bool sampleSearchPending = false;
    juce::uint32 sampleSearchChangedAt = 0;
    juce::AudioFormatManager previewFormats;
    juce::AudioThumbnailCache previewThumbnailCache { 32 };
    juce::AudioThumbnail previewThumbnail { 256, previewFormats, previewThumbnailCache };
    std::unique_ptr<juce::AudioFormatReaderSource> previewReader;
    juce::AudioTransportSource previewTransport;
    juce::AudioSourcePlayer previewPlayer;
    juce::TextButton scanPluginsButton, blacklistButton, clearBlacklistButton, favouritePluginButton, loadPluginButton, openPluginButton, unloadPluginButton;
    OneKnobButton chorusButton, flangerButton, phaserButton, tremoloButton, reverbButton, delayButton, driveButton, compressorButton, saturationButton, widthButton, filterButton, doublerButton, exciterButton, deEsserButton, gateButton, bassBoostButton, airButton, punchButton, softClipButton, clearOneKnobButton;
    juce::Label pluginStatus, spliceTitle, spliceInfo;
    juce::TextButton spliceOpenPluginButton, spliceScanButton;
    juce::File rootDirectory;
    Category category = Category::files;
    std::atomic<bool> stopped { false }, scanning { false }, scanFinishedPending { false };
    mutable juce::CriticalSection scanStatusLock;
    juce::String scanStatusText;
    std::thread scanThread, sampleSearchThread;
    std::atomic<bool> sampleSearchRunning { false };
    bool browserOpen = false, hostExpanded = false;
    bool targetIsInstrument=false;
    int targetEffectLane=-1,targetEffectSlot=-1;
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
                    if (browsers.find(main) == browsers.end()) { browsers.emplace(main, std::make_unique<BrowserPanel>(*main)); resizeLibertyBrowserResizeController(main); stopTimer(); return; }
    }
};

Bootstrap bootstrap;
}

void ensureLibertyBrowserController(MainComponent* main)
{
    if (main == nullptr) return;
    auto it = browsers.find(main);
    if (it == browsers.end())
    {
        it = browsers.emplace(main, std::make_unique<BrowserPanel>(*main)).first;
        resizeLibertyBrowserResizeController(main);
    }
    if (it != browsers.end() && it->second != nullptr)
        it->second->ensureToggleVisible();
}


int getLibertyBrowserReservedWidth(MainComponent* owner)
{
    if(owner==nullptr)return 0;
    const auto it=browsers.find(owner);
    if(it==browsers.end()||!it->second||!it->second->isOpenForMixer())return 0;
    return juce::jlimit(260,juce::jmax(260,owner->getWidth()-220),
                       getLibertyPreferredBrowserWidth(owner));
}

void showLibertyEffectBrowserForSlot(MainComponent* owner,bool instrument,int lane,int slot)
{
    if(owner==nullptr)return;
    ensureLibertyBrowserController(owner);
    const auto it=browsers.find(owner);
    if(it!=browsers.end()&&it->second)
        it->second->openForEffectSlot(instrument,lane,slot);
}

void shutdownLibertyBrowserController()
{
    bootstrap.shutdown();
}
