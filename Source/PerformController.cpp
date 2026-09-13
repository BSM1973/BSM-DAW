#define private public
#include "MainComponent.h"
#undef private

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <atomic>
#include <map>
#include <memory>

bool isLibertyMixConsoleVisible(MainComponent* owner);
void setLibertyMixConsoleVisible(MainComponent* owner, bool shouldShow);
int getLibertyTrackColourId(int track);
juce::String getLibertyTrackName(int track);

namespace
{
constexpr int transportHeight = 76;
constexpr int audioTracks = AudioEngine::maxAudioTracks;
constexpr int midiTrack = AudioEngine::maxAudioTracks;
constexpr int instrumentTrack = AudioEngine::maxAudioTracks + 1;
constexpr int performTracks = AudioEngine::maxAudioTracks + 2;
constexpr int sceneCount = 8;

juce::Colour trackColour(int id)
{
    static constexpr std::array<juce::uint32, 9> colours {
        0xff31506a, 0xff3b82f6, 0xff22c55e, 0xffeab308, 0xfff97316,
        0xffef4444, 0xffa855f7, 0xffec4899, 0xff14b8a6
    };
    return juce::Colour(colours[(size_t)juce::jlimit(0, 8, id)]);
}

bool isSupportedAudioFile(const juce::File& file)
{
    const auto ext = file.getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aif" || ext == ".aiff";
}

class PerformView final : public juce::Component,
                          public juce::FileDragAndDropTarget,
                          private juce::Timer
{
public:
    explicit PerformView(MainComponent& ownerIn) : owner(ownerIn)
    {
        setOpaque(true);
        setInterceptsMouseClicks(true, true);
        setAlwaysOnTop(true);

        for (int track = 0; track < performTracks; ++track)
        {
            auto& stop = stopTrackButtons[(size_t)track];
            stop.setButtonText("STOP");
            stop.setMouseClickGrabsKeyboardFocus(false);
            stop.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252b32));
            stop.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            stop.onClick = [this, track]
            {
                activeTrackScene[(size_t)track] = -1;
                if (!anyClipActive()) owner.audioEngine.setPlaying(false);
                refreshClipLabels();
                repaint();
            };
            addAndMakeVisible(stop);

            for (int scene = 0; scene < sceneCount; ++scene)
            {
                auto& cell = clipButtons[(size_t)track][(size_t)scene];
                cell.setMouseClickGrabsKeyboardFocus(false);
                cell.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a1f25));
                cell.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffb8c0c9));
                cell.onClick = [this, track, scene] { launchClip(track, scene); };
                addAndMakeVisible(cell);
            }
        }

        for (int scene = 0; scene < sceneCount; ++scene)
        {
            auto& launch = sceneButtons[(size_t)scene];
            launch.setButtonText("SCENE " + juce::String(scene + 1));
            launch.setMouseClickGrabsKeyboardFocus(false);
            launch.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff27313a));
            launch.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            launch.onClick = [this, scene] { launchScene(scene); };
            addAndMakeVisible(launch);
        }

        stopAllButton.setButtonText("STOP ALL");
        stopAllButton.setMouseClickGrabsKeyboardFocus(false);
        stopAllButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6f3030));
        stopAllButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        stopAllButton.onClick = [this]
        {
            activeTrackScene.fill(-1);
            owner.audioEngine.setPlaying(false);
            refreshClipLabels();
            repaint();
        };
        addAndMakeVisible(stopAllButton);

        activeTrackScene.fill(-1);
        setVisible(false);
        owner.addAndMakeVisible(this);
        startTimerHz(20);
    }

    ~PerformView() override { stopTimer(); }

    void setPerformVisible(bool shouldShow)
    {
        performVisible = shouldShow;
        dragTrack = dragScene = -1;
        setVisible(shouldShow);
        if (shouldShow)
        {
            setBounds(0, transportHeight, owner.getWidth(), juce::jmax(1, owner.getHeight() - transportHeight));
            refreshClipLabels();
            resized();
            toFront(false);
            repaint();
        }
    }

    bool isPerformVisible() const noexcept { return performVisible; }

    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        if (!performVisible || files.isEmpty()) return false;
        for (const auto& path : files)
            if (isSupportedAudioFile(juce::File(path))) return true;
        return false;
    }

    void fileDragEnter(const juce::StringArray&, int x, int y) override
    {
        updateDropTarget({ x, y });
    }

    void fileDragMove(const juce::StringArray&, int x, int y) override
    {
        updateDropTarget({ x, y });
    }

    void fileDragExit(const juce::StringArray&) override
    {
        dragTrack = dragScene = -1;
        refreshClipLabels();
        repaint();
    }

    void filesDropped(const juce::StringArray& files, int x, int y) override
    {
        updateDropTarget({ x, y });
        if (dragTrack < 0 || dragTrack >= audioTracks || dragScene < 0 || dragScene >= sceneCount)
        {
            dragTrack = dragScene = -1;
            refreshClipLabels();
            repaint();
            return;
        }

        juce::File selected;
        for (const auto& path : files)
        {
            const juce::File candidate(path);
            if (isSupportedAudioFile(candidate)) { selected = candidate; break; }
        }
        if (!selected.existsAsFile()) return;

        performAudioFiles[(size_t)dragTrack][(size_t)dragScene] = selected;
        activeTrackScene[(size_t)dragTrack] = -1;
        dragTrack = dragScene = -1;
        refreshClipLabels();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0c1014));
        g.setColour(juce::Colour(0xff161c22));
        g.fillRect(0, 0, getWidth(), 64);
        g.setColour(juce::Colour(0xff303944));
        g.drawHorizontalLine(63, 0.0f, (float)getWidth());

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(21.0f, juce::Font::bold));
        g.drawText("PERFORM", 22, 10, 150, 28, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff8f98a3));
        g.setFont(juce::Font(10.0f));
        g.drawText("SESSION   CLIPS   SCENES   LIVE LAUNCH", 170, 16, 320, 18, juce::Justification::centredLeft);

        for (int track = 0; track < performTracks; ++track)
        {
            const auto header = trackHeaders[(size_t)track];
            const auto colour = colourForTrack(track);
            g.setColour(juce::Colour(0xff151a20));
            g.fillRoundedRectangle(header.toFloat(), 5.0f);
            g.setColour(colour);
            g.fillRoundedRectangle((float)header.getX(), (float)header.getY(), (float)header.getWidth(), 5.0f, 2.5f);
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(11.5f, juce::Font::bold));
            g.drawText(trackName(track), header.reduced(6, 6), juce::Justification::centredTop, true);
            g.setColour(juce::Colour(0xff7e8791));
            g.setFont(juce::Font(8.0f, juce::Font::bold));
            g.drawText(trackType(track), header.getX() + 5, header.getY() + 28, header.getWidth() - 10, 14, juce::Justification::centred, true);
        }

        for (int scene = 0; scene < sceneCount; ++scene)
        {
            const auto y = sceneRows[(size_t)scene].getY();
            g.setColour(scene % 2 == 0 ? juce::Colour(0xff11161b) : juce::Colour(0xff0f1419));
            g.fillRect(0, y, getWidth(), sceneRows[(size_t)scene].getHeight());
            g.setColour(juce::Colour(0xff29313a));
            g.drawHorizontalLine(sceneRows[(size_t)scene].getBottom() - 1, 0.0f, (float)getWidth());
        }

        if (dragTrack >= 0 && dragScene >= 0)
        {
            const auto r = clipButtons[(size_t)dragTrack][(size_t)dragScene].getBounds().expanded(2);
            g.setColour(juce::Colour(0xff69d4ff));
            g.drawRoundedRectangle(r.toFloat(), 5.0f, 3.0f);
            g.setFont(juce::Font(9.0f, juce::Font::bold));
            g.drawText("DROP AUDIO HERE", r, juce::Justification::centred);
        }
    }

    void resized() override
    {
        const int leftMargin = 18;
        const int sceneLaunchW = 92;
        const int gridLeft = leftMargin;
        const int gridRight = getWidth() - sceneLaunchW - 28;
        const int gap = 6;
        const int available = juce::jmax(performTracks * 110, gridRight - gridLeft);
        const int columnW = juce::jlimit(110, 220, (available - (performTracks - 1) * gap) / performTracks);
        const int headerTop = 72;
        const int headerH = 62;
        const int rowsTop = 142;
        const int footerH = 54;
        const int rowsAvailable = juce::jmax(320, getHeight() - rowsTop - footerH - 16);
        const int rowH = juce::jlimit(44, 86, rowsAvailable / sceneCount);

        for (int track = 0; track < performTracks; ++track)
        {
            const int x = gridLeft + track * (columnW + gap);
            trackHeaders[(size_t)track] = { x, headerTop, columnW, headerH };
            stopTrackButtons[(size_t)track].setBounds(x + 8, headerTop + 40, columnW - 16, 18);
            for (int scene = 0; scene < sceneCount; ++scene)
            {
                const int y = rowsTop + scene * rowH;
                clipButtons[(size_t)track][(size_t)scene].setBounds(x + 2, y + 4, columnW - 4, rowH - 8);
            }
        }

        for (int scene = 0; scene < sceneCount; ++scene)
        {
            const int y = rowsTop + scene * rowH;
            sceneRows[(size_t)scene] = { 0, y, getWidth(), rowH };
            sceneButtons[(size_t)scene].setBounds(getWidth() - sceneLaunchW - 18, y + 4, sceneLaunchW, rowH - 8);
        }
        stopAllButton.setBounds(getWidth() - 140, getHeight() - 44, 122, 30);
    }

private:
    juce::String trackName(int track) const
    {
        if (track < audioTracks) return getLibertyTrackName(track);
        if (track == midiTrack) return getLibertyTrackName(midiTrack);
        return getLibertyTrackName(instrumentTrack);
    }

    juce::String trackType(int track) const
    {
        if (track < audioTracks) return "AUDIO";
        if (track == midiTrack) return "MIDI";
        return "INSTRUMENT";
    }

    juce::Colour colourForTrack(int track) const
    {
        return trackColour(getLibertyTrackColourId(track));
    }

    bool hasDroppedAudio(int track, int scene) const
    {
        return track >= 0 && track < audioTracks && scene >= 0 && scene < sceneCount
            && performAudioFiles[(size_t)track][(size_t)scene].existsAsFile();
    }

    bool slotHasClip(int track, int scene) const
    {
        if (track < audioTracks)
            return hasDroppedAudio(track, scene) || (scene == 0 && owner.audioEngine.hasAudioFile(track));
        if (scene != 0) return false;
        return !owner.midiEngine.getNotesCopy().empty();
    }

    juce::String slotName(int track, int scene) const
    {
        if (!slotHasClip(track, scene)) return "EMPTY";
        if (track < audioTracks)
        {
            juce::String name = hasDroppedAudio(track, scene)
                ? performAudioFiles[(size_t)track][(size_t)scene].getFileName()
                : owner.audioEngine.getAudioFileName(track);
            if (name.length() > 22) name = name.substring(0, 19) + "...";
            return name;
        }
        if (track == midiTrack) return "MIDI CLIP";
        return "INSTRUMENT CLIP";
    }

    double slotStartSeconds(int track, int scene)
    {
        if (track < audioTracks)
        {
            if (hasDroppedAudio(track, scene))
            {
                juce::String error;
                if (!owner.audioEngine.loadAudioFileIntoTrack(track, performAudioFiles[(size_t)track][(size_t)scene], error))
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                           "Liberty - PERFORM",
                                                           error.isNotEmpty() ? error : "Audio file could not be loaded.",
                                                           "OK");
                    return -1.0;
                }
                owner.audioEngine.setTrackStartSeconds(track, 0.0);
                owner.rebuildWaveformCache(track);
                owner.trackSourceFiles[(size_t)track] = performAudioFiles[(size_t)track][(size_t)scene];
                return 0.0;
            }
            return owner.audioEngine.getTrackStartSeconds(track);
        }
        return owner.midiClipStartSeconds;
    }

    void refreshClipLabels()
    {
        for (int track = 0; track < performTracks; ++track)
        {
            const auto colour = colourForTrack(track);
            for (int scene = 0; scene < sceneCount; ++scene)
            {
                auto& button = clipButtons[(size_t)track][(size_t)scene];
                const bool hasClip = slotHasClip(track, scene);
                const bool active = activeTrackScene[(size_t)track] == scene;
                const bool target = dragTrack == track && dragScene == scene;
                button.setButtonText(target ? "DROP AUDIO" : slotName(track, scene));
                button.setEnabled(hasClip || (track < audioTracks));
                button.setColour(juce::TextButton::buttonColourId,
                                 target ? juce::Colour(0xff245b70)
                                        : (active ? colour.brighter(0.25f)
                                                  : (hasClip ? colour.withAlpha(0.70f) : juce::Colour(0xff181d22))));
                button.setColour(juce::TextButton::textColourOffId,
                                 (hasClip || target) ? juce::Colours::white : juce::Colour(0xff68717b));
            }
        }
    }

    void updateDropTarget(juce::Point<int> point)
    {
        int nextTrack = -1;
        int nextScene = -1;
        for (int track = 0; track < audioTracks; ++track)
        {
            for (int scene = 0; scene < sceneCount; ++scene)
            {
                if (clipButtons[(size_t)track][(size_t)scene].getBounds().contains(point))
                {
                    nextTrack = track;
                    nextScene = scene;
                    break;
                }
            }
            if (nextTrack >= 0) break;
        }
        if (nextTrack != dragTrack || nextScene != dragScene)
        {
            dragTrack = nextTrack;
            dragScene = nextScene;
            refreshClipLabels();
            repaint();
        }
    }

    void launchClip(int track, int scene)
    {
        if (!slotHasClip(track, scene)) return;
        const double start = slotStartSeconds(track, scene);
        if (start < 0.0) return;

        activeTrackScene[(size_t)track] = scene;
        if (track < audioTracks) owner.selectedTrack = track;
        else if (track == midiTrack) owner.selectMidiTrack();
        else owner.selectedTrack = instrumentTrack;

        owner.playheadSeconds = start;
        owner.audioEngine.setCurrentTimeSeconds(start);
        owner.audioEngine.setPlaying(true);
        owner.isPlaying = true;
        refreshClipLabels();
        repaint();
        owner.repaint();
    }

    void launchScene(int scene)
    {
        bool found = false;
        double earliest = 0.0;
        for (int track = 0; track < performTracks; ++track)
        {
            if (!slotHasClip(track, scene)) continue;
            const double start = slotStartSeconds(track, scene);
            if (start < 0.0) continue;
            if (!found || start < earliest) earliest = start;
            found = true;
            activeTrackScene[(size_t)track] = scene;
        }
        if (!found) return;

        owner.playheadSeconds = earliest;
        owner.audioEngine.setCurrentTimeSeconds(earliest);
        owner.audioEngine.setPlaying(true);
        owner.isPlaying = true;
        refreshClipLabels();
        repaint();
        owner.repaint();
    }

    bool anyClipActive() const
    {
        for (const auto scene : activeTrackScene) if (scene >= 0) return true;
        return false;
    }

    void timerCallback() override
    {
        if (!performVisible) return;
        const auto wanted = juce::Rectangle<int>(0, transportHeight, owner.getWidth(), juce::jmax(1, owner.getHeight() - transportHeight));
        if (getBounds() != wanted) setBounds(wanted);
        toFront(false);
        refreshClipLabels();
        repaint();
    }

    MainComponent& owner;
    std::array<std::array<juce::TextButton, sceneCount>, performTracks> clipButtons;
    std::array<juce::TextButton, sceneCount> sceneButtons;
    std::array<juce::TextButton, performTracks> stopTrackButtons;
    juce::TextButton stopAllButton;
    std::array<juce::Rectangle<int>, performTracks> trackHeaders;
    std::array<juce::Rectangle<int>, sceneCount> sceneRows;
    std::array<int, performTracks> activeTrackScene;
    std::array<std::array<juce::File, sceneCount>, audioTracks> performAudioFiles;
    int dragTrack = -1;
    int dragScene = -1;
    bool performVisible = false;
};

class PerformController final : private juce::Timer
{
public:
    explicit PerformController(MainComponent& ownerIn) : owner(ownerIn), view(ownerIn)
    {
        performButton.setButtonText("PERFORM");
        performButton.setMouseClickGrabsKeyboardFocus(false);
        performButton.setClickingTogglesState(false);
        performButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        performButton.onClick = [this]
        {
            const bool next = !performVisible;
            if (next) setLibertyMixConsoleVisible(&owner, false);
            setPerformVisible(next);
        };
        owner.addAndMakeVisible(performButton);

        // While PERFORM is active these two buttons deliberately cover the page
        // buttons owned by MixConsoleController. This prevents ARRANGE from being
        // painted as active while PERFORM is the current workspace.
        arrangeOverlay.setButtonText("ARRANGE");
        mixOverlay.setButtonText("MIXCONSOLE");
        for (auto* b : { &arrangeOverlay, &mixOverlay })
        {
            b->setMouseClickGrabsKeyboardFocus(false);
            b->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252a31));
            b->setVisible(false);
            owner.addAndMakeVisible(*b);
        }
        arrangeOverlay.onClick = [this]
        {
            setPerformVisible(false);
            setLibertyMixConsoleVisible(&owner, false);
        };
        mixOverlay.onClick = [this]
        {
            setPerformVisible(false);
            setLibertyMixConsoleVisible(&owner, true);
        };

        startTimerHz(30);
    }

    ~PerformController() override { shutdown(); }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
        view.setPerformVisible(false);
        performButton.setVisible(false);
        arrangeOverlay.setVisible(false);
        mixOverlay.setVisible(false);
    }

    void setPerformVisible(bool shouldShow)
    {
        performVisible = shouldShow;
        view.setPerformVisible(shouldShow);
        arrangeOverlay.setVisible(shouldShow);
        mixOverlay.setVisible(shouldShow);
        refreshButtons();
        if (shouldShow) view.toFront(false);
        arrangeOverlay.toFront(false);
        mixOverlay.toFront(false);
        performButton.toFront(false);
        owner.repaint();
    }

    bool isVisible() const noexcept { return performVisible; }

private:
    void refreshButtons()
    {
        performButton.setColour(juce::TextButton::buttonColourId,
                                performVisible ? juce::Colour(0xff315f7a) : juce::Colour(0xff252a31));
        arrangeOverlay.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252a31));
        mixOverlay.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252a31));
    }

    void timerCallback() override
    {
        if (stopped.load()) return;
        arrangeOverlay.setBounds(1055, 8, 88, 26);
        mixOverlay.setBounds(1147, 8, 112, 26);
        performButton.setBounds(1263, 8, 94, 26);
        refreshButtons();
        if (performVisible)
        {
            arrangeOverlay.toFront(false);
            mixOverlay.toFront(false);
        }
        performButton.toFront(false);
    }

    MainComponent& owner;
    PerformView view;
    juce::TextButton arrangeOverlay, mixOverlay, performButton;
    std::atomic<bool> stopped { false };
    bool performVisible = false;
};

std::map<MainComponent*, std::unique_ptr<PerformController>> controllers;

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
                        controllers.emplace(main, std::make_unique<PerformController>(*main));
    }
};

Bootstrap bootstrap;
}

bool isLibertyPerformVisible(MainComponent* owner)
{
    if (owner == nullptr) return false;
    const auto it = controllers.find(owner);
    return it != controllers.end() && it->second && it->second->isVisible();
}

void setLibertyPerformVisible(MainComponent* owner, bool shouldShow)
{
    if (owner == nullptr) return;
    const auto it = controllers.find(owner);
    if (it == controllers.end() || !it->second) return;
    it->second->setPerformVisible(shouldShow);
}

void shutdownLibertyPerformController()
{
    bootstrap.shutdown();
}
