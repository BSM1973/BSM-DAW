#define private public
#include "MainComponent.h"
#undef private

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <thread>
#include <vector>

namespace
{
juce::TextButton* findDirectButton(juce::Component* parent, const juce::String& text)
{
    if (parent == nullptr) return nullptr;
    for (int i = 0; i < parent->getNumChildComponents(); ++i)
        if (auto* button = dynamic_cast<juce::TextButton*>(parent->getChildComponent(i)))
            if (button->getButtonText() == text)
                return button;
    return nullptr;
}

juce::Component* findBrowserPanel(MainComponent& owner)
{
    std::function<juce::Component*(juce::Component*)> findRecursive;
    findRecursive = [&findRecursive](juce::Component* parent) -> juce::Component*
    {
        if (parent == nullptr) return nullptr;
        if (findDirectButton(parent, "PLUGINS") != nullptr && findDirectButton(parent, "FILES") != nullptr)
            return parent;
        for (int i = 0; i < parent->getNumChildComponents(); ++i)
            if (auto* found = findRecursive(parent->getChildComponent(i)))
                return found;
        return nullptr;
    };
    return findRecursive(&owner);
}

bool isAudioSample(const juce::File& file)
{
    const auto ext = file.getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac" || ext == ".mp3";
}

juce::File spliceSettingsFolder()
{
    auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                      .getChildFile("BSM").getChildFile("Liberty");
    folder.createDirectory();
    return folder;
}

juce::File spliceRootSettingsFile()
{
    return spliceSettingsFolder().getChildFile("SpliceLibraryPath.txt");
}

class SampleRow final : public juce::Component
{
public:
    void setFile(const juce::File& newFile, const juce::File& root)
    {
        file = newFile;
        rootFolder = root;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        if (isMouseOver())
        {
            g.setColour(juce::Colour(0xff1f2c35));
            g.fillRect(getLocalBounds());
        }

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(file.getFileNameWithoutExtension(), 8, 4, getWidth() - 16, 18,
                   juce::Justification::centredLeft, true);

        auto relative = file.getParentDirectory().getRelativePathFrom(rootFolder);
        if (relative == ".") relative.clear();
        g.setColour(juce::Colour(0xff8f98a3));
        g.setFont(juce::Font(8.5f));
        g.drawText(relative, 8, 23, getWidth() - 16, 14,
                   juce::Justification::centredLeft, true);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        dragStarted = false;
        dragOrigin = event.getPosition();
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (dragStarted || !file.existsAsFile()) return;
        if (event.getDistanceFromDragStart() < 5) return;
        dragStarted = true;
        juce::StringArray files;
        files.add(file.getFullPathName());
        juce::DragAndDropContainer::performExternalDragDropOfFiles(files, false, this);
    }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        if (file.existsAsFile()) file.revealToUser();
    }

private:
    juce::File file, rootFolder;
    juce::Point<int> dragOrigin;
    bool dragStarted = false;
};

class SplicePanel final : public juce::Component,
                          private juce::ListBoxModel
{
public:
    SplicePanel()
        : sampleList("Splice Samples", this)
    {
        setOpaque(true);
        loadRootFolder();

        libraryButton.setButtonText("MES SAMPLES");
        soundsButton.setButtonText("SONS");
        loginButton.setButtonText("CONNEXION");
        rescanButton.setButtonText("RÉANALYSER");
        folderButton.setButtonText("DOSSIER");
        desktopButton.setButtonText("DESKTOP");

        for (auto* button : { &libraryButton, &soundsButton, &loginButton,
                              &rescanButton, &folderButton, &desktopButton })
        {
            button->setMouseClickGrabsKeyboardFocus(false);
            button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252a31));
            button->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            addAndMakeVisible(*button);
        }

        libraryButton.onClick = [this] { showLibrary(); };
        soundsButton.onClick = [this]
        {
            showWeb();
            browser.goToURL("https://splice.com/sounds");
        };
        loginButton.onClick = [this]
        {
            showWeb();
            browser.goToURL("https://splice.com/login");
        };
        rescanButton.onClick = [this] { startScan(); };
        folderButton.onClick = [this] { chooseSpliceFolder(); };
        desktopButton.onClick = []
        {
           #if JUCE_MAC
            const juce::File app("/Applications/Splice.app");
            if (app.exists())
                app.startAsProcess();
            else
                juce::URL("https://splice.com/tools/desktop").launchInDefaultBrowser();
           #else
            juce::URL("https://splice.com/tools/desktop").launchInDefaultBrowser();
           #endif
        };

        searchBox.setTextToShowWhenEmpty("Rechercher dans les samples Splice téléchargés", juce::Colour(0xff69727c));
        searchBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff171c22));
        searchBox.setColour(juce::TextEditor::textColourId, juce::Colours::white);
        searchBox.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff303842));
        searchBox.onTextChange = [this] { rebuildFilter(); };
        addAndMakeVisible(searchBox);

        statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9aa3ad));
        statusLabel.setFont(juce::Font(9.0f));
        statusLabel.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(statusLabel);

        sampleList.setRowHeight(42);
        sampleList.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff111419));
        sampleList.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff303842));
        addAndMakeVisible(sampleList);

        addAndMakeVisible(browser);
        browser.goToURL("https://splice.com/sounds");

        showLibrary();
        startScan();
    }

    ~SplicePanel() override
    {
        stopped.store(true);
        if (scanThread.joinable()) scanThread.join();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff101318));
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(14.0f, juce::Font::bold));
        g.drawText("SPLICE", 10, 4, 100, 22, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff8f98a3));
        g.setFont(juce::Font(9.0f));
        g.drawText(libraryMode ? "Bibliothèque locale téléchargée - glisser les fichiers audio dans Liberty"
                               : "Splice web - achats et accès au compte",
                   74, 6, getWidth() - 84, 18, juce::Justification::centredRight, true);
    }

    void resized() override
    {
        const int margin = 8;
        const int gap = 4;
        const int usable = juce::jmax(240, getWidth() - margin * 2);
        const int w1 = (usable - gap * 2) / 3;

        // Give the longest localized actions enough room at the minimum Browser width.
        const int rescanWidth = juce::jmin(juce::jmax(w1, 92), usable - gap * 2 - 120);
        const int remainingActions = usable - rescanWidth - gap * 2;
        const int folderWidth = remainingActions / 2;

        const int loginWidth = juce::jmin(juce::jmax(w1, 86), usable - gap * 2 - 120);
        const int remainingModes = usable - loginWidth - gap * 2;
        const int libraryWidth = remainingModes / 2;

        libraryButton.setBounds(margin, 30, libraryWidth, 26);
        soundsButton.setBounds(margin + libraryWidth + gap, 30, remainingModes - libraryWidth, 26);
        loginButton.setBounds(margin + remainingModes + gap * 2, 30, loginWidth, 26);

        rescanButton.setBounds(margin, 60, rescanWidth, 24);
        folderButton.setBounds(margin + rescanWidth + gap, 60, folderWidth, 24);
        desktopButton.setBounds(margin + rescanWidth + gap + folderWidth + gap, 60,
                                remainingActions - folderWidth, 24);

        for (auto* button : { &libraryButton, &soundsButton, &loginButton,
                              &rescanButton, &folderButton, &desktopButton })
            button->setTooltip(button->getButtonText());

        searchBox.setBounds(margin, 90, usable, 26);
        statusLabel.setBounds(margin, 119, usable, 20);
        sampleList.setBounds(margin, 142, usable, juce::jmax(40, getHeight() - 150));
        browser.setBounds(6, 90, getWidth() - 12, juce::jmax(40, getHeight() - 96));
    }

private:
    int getNumRows() override
    {
        return (int)filteredIndices.size();
    }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (selected)
        {
            g.setColour(juce::Colour(0xff244f63));
            g.fillRect(0, 0, width, height);
        }
    }

    juce::Component* refreshComponentForRow(int row, bool, juce::Component* existing) override
    {
        auto* component = dynamic_cast<SampleRow*>(existing);
        if (component == nullptr)
        {
            delete existing;
            component = new SampleRow();
        }

        if (row >= 0 && row < (int)filteredIndices.size())
        {
            const int sourceIndex = filteredIndices[(size_t)row];
            if (sourceIndex >= 0 && sourceIndex < (int)samples.size())
                component->setFile(samples[(size_t)sourceIndex], spliceRoot);
        }
        return component;
    }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override
    {
        if (row < 0 || row >= (int)filteredIndices.size()) return;
        const int sourceIndex = filteredIndices[(size_t)row];
        if (sourceIndex >= 0 && sourceIndex < (int)samples.size())
            samples[(size_t)sourceIndex].revealToUser();
    }

    void loadRootFolder()
    {
        const auto settings = spliceRootSettingsFile();
        if (settings.existsAsFile())
        {
            const juce::File saved(settings.loadFileAsString().trim());
            if (saved.isDirectory()) spliceRoot = saved;
        }

        if (!spliceRoot.isDirectory())
            spliceRoot = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Splice");
    }

    void saveRootFolder()
    {
        spliceRootSettingsFile().replaceWithText(spliceRoot.getFullPathName());
    }

    void chooseSpliceFolder()
    {
        chooser = std::make_unique<juce::FileChooser>("Choisir le dossier de samples Splice", spliceRoot, "*");
        chooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectDirectories,
                             [this](const juce::FileChooser& fc)
                             {
                                 const auto result = fc.getResult();
                                 if (result.isDirectory())
                                 {
                                     spliceRoot = result;
                                     saveRootFolder();
                                     startScan();
                                 }
                             });
    }

    void showLibrary()
    {
        libraryMode = true;
        browser.setVisible(false);
        searchBox.setVisible(true);
        statusLabel.setVisible(true);
        sampleList.setVisible(true);
        rescanButton.setEnabled(true);
        folderButton.setEnabled(true);
        libraryButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff315f7a));
        soundsButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252a31));
        loginButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252a31));
        repaint();
    }

    void showWeb()
    {
        libraryMode = false;
        sampleList.setVisible(false);
        searchBox.setVisible(false);
        statusLabel.setVisible(false);
        browser.setVisible(true);
        libraryButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252a31));
        soundsButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff315f7a));
        repaint();
    }

    void rebuildFilter()
    {
        filteredIndices.clear();
        const auto query = searchBox.getText().trim().toLowerCase();
        for (int i = 0; i < (int)samples.size(); ++i)
        {
            if (query.isEmpty()
                || samples[(size_t)i].getFileName().toLowerCase().contains(query)
                || samples[(size_t)i].getFullPathName().toLowerCase().contains(query))
                filteredIndices.push_back(i);
        }
        sampleList.updateContent();
        sampleList.repaint();
        updateStatus();
    }

    void updateStatus()
    {
        if (scanning.load())
        {
            statusLabel.setText("Analyse : " + spliceRoot.getFullPathName(), juce::dontSendNotification);
            return;
        }

        if (!spliceRoot.isDirectory())
        {
            statusLabel.setText("Dossier Splice introuvable. Utilise DOSSIER pour le sélectionner.", juce::dontSendNotification);
            return;
        }

        statusLabel.setText(juce::String(filteredIndices.size()) + " sample(s) - glisser vers ARRANGE / PERFORM",
                            juce::dontSendNotification);
    }

    void startScan()
    {
        if (scanning.exchange(true)) return;
        if (scanThread.joinable()) scanThread.join();
        updateStatus();

        const auto root = spliceRoot;
        const auto safeThis = juce::Component::SafePointer<SplicePanel>(this);
        scanThread = std::thread([safeThis, root]
        {
            std::vector<juce::File> found;
            if (root.isDirectory())
            {
                for (const auto entry : juce::RangedDirectoryIterator(root, true, "*", juce::File::findFiles))
                {
                    if (safeThis == nullptr || safeThis->stopped.load()) return;
                    const auto file = entry.getFile();
                    if (isAudioSample(file)) found.push_back(file);
                }
            }

            std::sort(found.begin(), found.end(), [](const juce::File& a, const juce::File& b)
            {
                return a.getFileName().compareNatural(b.getFileName(), false) < 0;
            });

            juce::MessageManager::callAsync([safeThis, found = std::move(found)]() mutable
            {
                if (safeThis == nullptr) return;
                safeThis->samples = std::move(found);
                safeThis->scanning.store(false);
                safeThis->rebuildFilter();
            });
        });
    }

    juce::TextButton libraryButton, soundsButton, loginButton, rescanButton, folderButton, desktopButton;
    juce::TextEditor searchBox;
    juce::Label statusLabel;
    juce::ListBox sampleList;
    juce::WebBrowserComponent browser;
    juce::File spliceRoot;
    std::vector<juce::File> samples;
    std::vector<int> filteredIndices;
    std::unique_ptr<juce::FileChooser> chooser;
    std::thread scanThread;
    std::atomic<bool> scanning { false };
    std::atomic<bool> stopped { false };
    bool libraryMode = true;
};

class SpliceBrowserController final : private juce::Timer, private juce::MouseListener
{
public:
    explicit SpliceBrowserController(MainComponent& ownerIn) : owner(ownerIn)
    {
        juce::Desktop::getInstance().addGlobalMouseListener(this);
        startTimerHz(15);
    }

    ~SpliceBrowserController() override { shutdown(); }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
        juce::Desktop::getInstance().removeGlobalMouseListener(this);
        if (spliceButton != nullptr)
        {
            spliceButton->removeListener(this);
            spliceButton->setVisible(false);
        }
        if (splicePanel != nullptr) splicePanel->setVisible(false);
        splicePanelOwned.reset();
        spliceButton = nullptr;
    }

private:
    void buttonClicked(juce::Button* button) override
    {
        if (button == spliceButton)
            showSplice(true);
    }

    void attachIfPossible()
    {
        if (browserPanel != nullptr) return;
        browserPanel = findBrowserPanel(owner);
        if (browserPanel == nullptr) return;

        spliceButton = findDirectButton(browserPanel, "SPLICE");
        if (spliceButton == nullptr)
        {
            browserPanel = nullptr;
            return;
        }
        spliceButton->setMouseClickGrabsKeyboardFocus(false);
        spliceButton->addListener(this);

        splicePanelOwned = std::make_unique<SplicePanel>();
        splicePanel = splicePanelOwned.get();
        splicePanel->setVisible(false);
        browserPanel->addAndMakeVisible(*splicePanel);
    }

    void showSplice(bool shouldShow)
    {
        spliceVisible = shouldShow;
        if (splicePanel != nullptr)
        {
            splicePanel->setVisible(shouldShow);
            if (shouldShow) splicePanel->toFront(false);
        }
        if (spliceButton != nullptr)
            spliceButton->setColour(juce::TextButton::buttonColourId,
                                    shouldShow ? juce::Colour(0xff315f7a) : juce::Colour(0xff1b2027));
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (!spliceVisible || event.eventComponent == nullptr) return;
        auto* button = dynamic_cast<juce::TextButton*>(event.eventComponent);
        if (button == nullptr || button == spliceButton) return;
        const auto text = button->getButtonText();
        if (text == "FILES" || text == "AUDIO" || text == "MIDI" || text == "PRESETS" || text == "PLUGINS")
            showSplice(false);
    }

    void timerCallback() override
    {
        if (stopped.load()) return;
        attachIfPossible();
        if (browserPanel == nullptr || spliceButton == nullptr || splicePanel == nullptr) return;

        auto* files = findDirectButton(browserPanel, "FILES");
        auto* audio = findDirectButton(browserPanel, "AUDIO");
        auto* midi = findDirectButton(browserPanel, "MIDI");
        auto* presets = findDirectButton(browserPanel, "PRESETS");
        auto* plugins = findDirectButton(browserPanel, "PLUGINS");

        if (files && audio && midi && presets && plugins)
        {
            const int available = juce::jmax(300, browserPanel->getWidth() - 16);
            const int gap = 3;
            const int each = juce::jmax(42, (available - gap * 5) / 6);
            int x = 8;
            for (auto* b : { files, audio, midi, presets, plugins, spliceButton })
            {
                b->setBounds(x, 42, each, 26);
                x += each + gap;
            }
        }

        splicePanel->setBounds(0, 72, browserPanel->getWidth(), juce::jmax(40, browserPanel->getHeight() - 72));
        spliceButton->setVisible(browserPanel->isVisible());
        if (spliceVisible)
        {
            splicePanel->setVisible(browserPanel->isVisible());
            if (browserPanel->isVisible()) splicePanel->toFront(false);
            spliceButton->toFront(false);
        }
    }

    MainComponent& owner;
    juce::Component* browserPanel = nullptr;
    juce::TextButton* spliceButton = nullptr;
    std::unique_ptr<SplicePanel> splicePanelOwned;
    SplicePanel* splicePanel = nullptr;
    bool spliceVisible = false;
    std::atomic<bool> stopped { false };
};

std::map<MainComponent*, std::unique_ptr<SpliceBrowserController>> controllers;

class Bootstrap final : private juce::Timer
{
public:
    Bootstrap() { startTimerHz(10); }
    ~Bootstrap() override { shutdown(); }

    void shutdown()
    {
        stopTimer();
        for (auto& entry : controllers)
            if (entry.second) entry.second->shutdown();
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
                        controllers.emplace(main, std::make_unique<SpliceBrowserController>(*main));
    }
};

Bootstrap bootstrap;
}

void shutdownLibertySpliceBrowserController()
{
    bootstrap.shutdown();
}
