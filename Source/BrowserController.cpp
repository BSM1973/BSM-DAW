#define private public
#include "MainComponent.h"
#undef private

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <atomic>
#include <map>
#include <memory>

namespace
{
constexpr int browserWidth = 320;
constexpr int topBarHeight = 76;

class BrowserPanel final : public juce::Component,
                           private juce::Timer
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
        fileTree.onDoubleClick = [this](const juce::File& file) { openFileOrDirectory(file); };
        addAndMakeVisible(fileTree);

        filesButton.setButtonText("FILES");
        audioButton.setButtonText("AUDIO");
        midiButton.setButtonText("MIDI");
        presetsButton.setButtonText("PRESETS");
        for (auto* b : { &filesButton, &audioButton, &midiButton, &presetsButton })
        {
            b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b2027));
            b->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff315f7a));
            b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffc9cdd3));
            b->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
            addAndMakeVisible(*b);
        }

        filesButton.onClick = [this] { setCategory(Category::files); };
        audioButton.onClick = [this] { setCategory(Category::audio); };
        midiButton.onClick = [this] { setCategory(Category::midi); };
        presetsButton.onClick = [this] { setCategory(Category::presets); };

        homeButton.setButtonText("HOME");
        homeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252a31));
        homeButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        homeButton.onClick = [this] { setRoot(juce::File::getSpecialLocation(juce::File::userHomeDirectory)); };
        addAndMakeVisible(homeButton);

        setVisible(false);
        owner.addAndMakeVisible(this);
        setRoot(juce::File::getSpecialLocation(juce::File::userHomeDirectory));
        setCategory(Category::files);
        startTimerHz(20);
    }

    ~BrowserPanel() override { shutdown(); }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
        fileTree.onDoubleClick = nullptr;
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
        g.setColour(juce::Colour(0xff858c96));
        g.setFont(juce::Font(9.5f));
        g.drawText(rootDirectory.getFullPathName(), 14, 78, getWidth() - 28, 18, juce::Justification::centredLeft, true);
        g.setColour(juce::Colour(0xff20252c));
        g.fillRect(0, 100, getWidth(), 1);
        g.setColour(juce::Colour(0xff8f98a3));
        g.setFont(juce::Font(9.0f));
        g.drawText(categoryHint(), 14, getHeight() - 29, getWidth() - 28, 18, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        closeButton.setBounds(getWidth() - 38, 8, 28, 26);
        filesButton.setBounds(12, 42, 66, 26);
        audioButton.setBounds(82, 42, 66, 26);
        midiButton.setBounds(152, 42, 66, 26);
        presetsButton.setBounds(222, 42, 82, 26);
        homeButton.setBounds(12, 105, 70, 24);
        fileTree.setBounds(10, 136, getWidth() - 20, juce::jmax(40, getHeight() - 174));
    }

private:
    enum class Category { files, audio, midi, presets };

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
        repaint();
    }

    bool fileMatchesCategory(const juce::File& file) const
    {
        if (file.isDirectory() || category == Category::files) return true;
        const auto ext = file.getFileExtension().toLowerCase();
        if (category == Category::audio) return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".mp3" || ext == ".flac";
        if (category == Category::midi) return ext == ".mid" || ext == ".midi";
        return ext == ".xml" || ext == ".fxp" || ext == ".vstpreset" || ext == ".aupreset";
    }

    juce::String categoryHint() const
    {
        switch (category)
        {
            case Category::audio: return "Double-clic sur WAV/AIFF pour charger sur la piste Audio sélectionnée";
            case Category::midi: return "Fichiers MIDI";
            case Category::presets: return "Presets XML / FXP / VSTPreset / AUPreset";
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
    }

    MainComponent& owner;
    juce::TimeSliceThread thread { "Liberty Browser" };
    juce::WildcardFileFilter filter;
    juce::DirectoryContentsList directoryList;
    juce::FileTreeComponent fileTree;
    juce::TextButton toggleButton, closeButton, filesButton, audioButton, midiButton, presetsButton, homeButton;
    juce::File rootDirectory;
    Category category = Category::files;
    std::atomic<bool> stopped { false };
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
