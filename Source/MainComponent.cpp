#include "MainComponent.h"
#include "LibertyDrumSampler.h"
#include "MidiEditor.h"
#include "LibertyDesignSystem.h"
void closeFloatingStepSequencerWindow();
void closeFloatingDrumSamplerWindow();
void showFloatingDrumSamplerWindow(AudioEngine&);
void resizeLibertyDynamicTrackController(MainComponent*);
void resizeLibertyGridSnapController(MainComponent*);
void clearLibertyAudioClipResizeSource(AudioEngine&, int);

void resizeLibertyMixConsole(MainComponent*);
void resizeLibertyBrowserResizeController(MainComponent*);
void resizeLibertyMultiMidiClipController(MainComponent*);
void ensureLibertyAudioRecordingController(MainComponent*);
bool commitLibertySequencerMidiClip(MainComponent&, const std::vector<MidiEngine::NoteEvent>&, int, double, double);

double getLibertyTimelinePixelsPerSecond() noexcept;
int getLibertyTrackRowHeight() noexcept;
int getLibertyActiveTool();

namespace
{
constexpr int menuNew = 1;
constexpr int menuOpen = 2;
constexpr int menuSave = 3;
constexpr int menuSaveAs = 4;

bool isSupportedAudioFile(const juce::String& path)
{
    const auto extension = juce::File(path).getFileExtension().toLowerCase();
    return extension == ".wav" || extension == ".aif" || extension == ".aiff";
}

bool exportTrackToProjectMedia(const juce::File& projectFile,
                               int trackIndex,
                               const juce::AudioBuffer<float>* buffer,
                               double sampleRate,
                               juce::File& exportedFile)
{
    if (buffer == nullptr || buffer->getNumSamples() <= 0 || buffer->getNumChannels() <= 0 || sampleRate <= 0.0)
        return false;

    auto mediaFolder = projectFile.getSiblingFile(projectFile.getFileNameWithoutExtension() + "_Media");
    if (!mediaFolder.createDirectory().wasOk() && !mediaFolder.isDirectory())
        return false;

    exportedFile = mediaFolder.getChildFile("Audio_" + juce::String(trackIndex + 1) + ".wav");
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::FileOutputStream> outputStream(exportedFile.createOutputStream());
    if (outputStream == nullptr)
        return false;

    std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(outputStream.get(), sampleRate,
                                                                                static_cast<unsigned int>(buffer->getNumChannels()),
                                                                                24, {}, 0));
    if (writer == nullptr)
        return false;
    outputStream.release();
    return writer->writeFromAudioSampleBuffer(*buffer, 0, buffer->getNumSamples());
}
}

MainComponent::MainComponent()
{
    // Extra height is intentional: six 96+ px track rows plus the mixer must fit
    // without controls colliding or being pushed under the mixer.
    setSize(1440, 980);
    waveformMin.resize((size_t) audioEngine.getAudioTrackCount());
    waveformMax.resize((size_t) audioEngine.getAudioTrackCount());
    trackSourceFiles.resize((size_t) audioEngine.getAudioTrackCount());
    pendingAudioFileNames.resize((size_t) audioEngine.getAudioTrackCount());
    pendingAudioLengths.resize((size_t) audioEngine.getAudioTrackCount());
    pendingAudioStartSeconds.resize((size_t) audioEngine.getAudioTrackCount());
    pendingAudioWarpStates.resize((size_t) audioEngine.getAudioTrackCount());
    instrumentStepSequencers.resize((size_t) dynamicInstrumentTrackCount);
    midiStepSequencers.resize((size_t) dynamicMidiTrackCount);
    audioEngine.initialise();
    auto drumSampler = std::make_shared<LibertyDrumSampler>();
    drumSampler->prepare(audioEngine.getDeviceManager().getCurrentAudioDevice() != nullptr
        ? audioEngine.getDeviceManager().getCurrentAudioDevice()->getCurrentSampleRate() : 44100.0);
    audioEngine.setDrumSampler(std::move(drumSampler));
    initializeProjectTracking(); // Project actions now live in the Fichier menu.
    setWantsKeyboardFocus(true);
    startTimerHz(30);
}

MainComponent::~MainComponent() { closeFloatingDrumSamplerWindow(); closeFloatingStepSequencerWindow(); }

void MainComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    // Liberty Pro design system: shared canvas across transport, arrange and dock.
    g.fillAll(LibertyUI::Colour::canvas);
    auto transport = bounds.removeFromTop(transportHeight);
    auto mixer = bounds.removeFromBottom(mixerHeight);
    drawTransport(g, transport);
    drawTrackArea(g, bounds);
    // Visual dock boundary only; leave editor and tab hit areas unchanged.
    g.setColour(juce::Colour(0xff070b10));
    g.fillRect(mixer.getX(), mixer.getY(), mixer.getWidth(), 2);
    g.setColour(juce::Colour(0xff344452));
    g.fillRect(mixer.getX(), mixer.getY() + 2, mixer.getWidth(), 1);
    g.setColour(juce::Colour(0xff54d6dc).withAlpha(0.75f));
    g.fillRect(mixer.getX() + 8, mixer.getY() + 2,
               juce::jmax(0, juce::jmin(144, mixer.getWidth() - 16)), 1);
    const int instrumentFirst = getAudioTrackCount() + getMidiTrackCount();
    const bool showStepSequencer = selectedTrack >= instrumentFirst && selectedTrack < instrumentFirst + getInstrumentTrackCount();
    const bool isMidiDockTrack = selectedTrack >= getAudioTrackCount() && selectedTrack < instrumentFirst;
    const auto editorArea = (isMidiDockTrack || showStepSequencer) ? mixer.withTrimmedTop(36) : mixer;
    if (dockStepSequencerMode && isMidiDockTrack)
        drawMidiStepSequencer(g, editorArea);
    else if (showStepSequencer && dockStepSequencerMode)
        drawStepSequencerDock(g, editorArea);
    else
        drawMixer(g, editorArea);

    // Shared editor navigation for MIDI and instrument tracks.
    const bool isMidi = selectedTrack >= getAudioTrackCount()
                     && selectedTrack < instrumentFirst;
    if (isMidi || showStepSequencer)
    {
        auto tabs = juce::Rectangle<int>(8, mixer.getY() + 4, 244, 28);
        const auto piano = tabs.removeFromLeft(118);
        const auto seq = juce::Rectangle<int>(piano.getRight() + 8, piano.getY(), 118, piano.getHeight());
        const auto drawTab = [&](juce::Rectangle<int> rect, const juce::String& label, bool active)
        {
            g.setColour(active ? juce::Colour(0xff23535d) : juce::Colour(0xff202c37));
            g.fillRoundedRectangle(rect.toFloat(), 5.0f);
            g.setColour(juce::Colour(0xffe8eff7).withAlpha(active ? 0.11f : 0.045f));
            g.fillRoundedRectangle(rect.toFloat().withHeight(6.0f), 4.0f);
            // Distinct inactive outlines improve dock navigation on graphite surfaces.
            g.setColour(active ? juce::Colour(0xff54d6dc) : juce::Colour(0xff48606d));
            g.drawRoundedRectangle(rect.toFloat().reduced(0.5f), 5.0f, active ? 1.5f : 1.0f);
            if (active)
                g.fillRoundedRectangle((float)rect.getX() + 8.0f, (float)rect.getBottom() - 3.0f,
                                       (float)juce::jmax(1, rect.getWidth() - 16), 2.0f, 1.0f);
            // Obsidian dock tabs: selected editor reads first, inactive tabs remain clear.
            g.setColour(active ? juce::Colour(0xffeffcfd) : juce::Colour(0xffb9ccd6));
            g.setFont(juce::Font(12.0f, juce::Font::bold));
            g.drawText(label, rect, juce::Justification::centred);
        };
        drawTab(piano, "PIANO ROLL", !dockStepSequencerMode);
        drawTab(seq, "STEP SEQ", dockStepSequencerMode && showStepSequencer);
        // Keep Drum Sampler directly beside STEP SEQ, matching the editor tabs.
        drawTab(juce::Rectangle<int>(seq.getRight() + 8, seq.getY(), 148, seq.getHeight()), "DRUM SAMPLER", false);
        if (showStepSequencer && dockStepSequencerMode)
        {
            const auto detach = juce::Rectangle<int>(416, mixer.getY() + 4, 106, 28);
            g.setColour(juce::Colour(0xff202c37));
            g.fillRoundedRectangle(detach.toFloat(), 5.0f);
            // Match secondary dock actions to the Obsidian inactive-tab palette.
            g.setColour(juce::Colour(0xff48606d));
            g.drawRoundedRectangle(detach.toFloat().reduced(0.5f), 5.0f, 1.0f);
            g.setColour(juce::Colour(0xffc2d8e2));
            g.setFont(juce::Font(11.0f, juce::Font::bold));
            g.drawText(juce::String::fromUTF8("D\xC3\x89TACHER"), detach, juce::Justification::centred);
        }
    }
}

namespace
{
class FloatingStepSequencerContent final : public juce::Component, private juce::Timer
{
public:
    explicit FloatingStepSequencerContent(MainComponent& main) : owner(main)
    {
        startTimerHz(15);
    }
    void paint(juce::Graphics& g) override
    {
        owner.paintFloatingStepSequencer(g, getLocalBounds());
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        owner.clickFloatingStepSequencer(event.getPosition(), getLocalBounds());
        repaint();
    }
private:
    void timerCallback() override { repaint(); }
    MainComponent& owner;
};

class FloatingStepSequencerWindow final : public juce::DocumentWindow
{
public:
    explicit FloatingStepSequencerWindow(MainComponent& main)
        : juce::DocumentWindow("Liberty - Step Sequencer", juce::Colour(0xff101821),
                               juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new FloatingStepSequencerContent(main), true);
        setResizable(true, true);
        setResizeLimits(760, 220, 2200, 700);
        if (lastFloatingStepSequencerBounds.isEmpty())
            centreWithSize(1200, 320);
        else
            setBounds(lastFloatingStepSequencerBounds);
        setVisible(true);
    }
    void closeButtonPressed() override { setVisible(false); }
    void moved() override { rememberBounds(); }
    void resized() override
    {
        juce::DocumentWindow::resized();
        rememberBounds();
    }
private:
    void rememberBounds()
    {
        if (getWidth() >= 760 && getHeight() >= 220)
            lastFloatingStepSequencerBounds = getBounds();
    }
public:
    static juce::Rectangle<int> lastFloatingStepSequencerBounds;
};
juce::Rectangle<int> FloatingStepSequencerWindow::lastFloatingStepSequencerBounds;
std::unique_ptr<FloatingStepSequencerWindow> floatingStepSequencerWindow;
}

void closeFloatingStepSequencerWindow() { floatingStepSequencerWindow.reset(); }
class FloatingDrumSamplerWindow final : public juce::DocumentWindow
{
public:
    explicit FloatingDrumSamplerWindow(LibertyDrumSampler& sampler)
        : juce::DocumentWindow("Liberty - Drum Sampler", juce::Colour(0xff121c27),
                               juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new LibertyDrumSamplerPanel(sampler), true);
        setResizable(true, true);
        setResizeLimits(740, 760, 2200, 1600);
        centreWithSize(1050, 950);
    }
    void closeButtonPressed() override { setVisible(false); }
};
std::unique_ptr<FloatingDrumSamplerWindow> floatingDrumSamplerWindow;


void closeFloatingDrumSamplerWindow() { floatingDrumSamplerWindow.reset(); }
void showFloatingDrumSamplerWindow(AudioEngine& engine)
{
    auto sampler = engine.getDrumSampler();
    if (!sampler) return;
    if (!floatingDrumSamplerWindow)
        floatingDrumSamplerWindow = std::make_unique<FloatingDrumSamplerWindow>(*sampler);
    floatingDrumSamplerWindow->setVisible(true);
    floatingDrumSamplerWindow->toFront(true);
}

void MainComponent::showFloatingStepSequencer()
{
    if (!floatingStepSequencerWindow)
        floatingStepSequencerWindow = std::make_unique<FloatingStepSequencerWindow>(*this);
    floatingStepSequencerWindow->setVisible(true);
    floatingStepSequencerWindow->toFront(true);
}

void MainComponent::handleStepSequencerClick(juce::Point<int> p, juce::Rectangle<int> area)
{
    const int audioTrackCount = getAudioTrackCount();
    const int midiTrackCount = getMidiTrackCount();
    const int instrumentTrackCount = getInstrumentTrackCount();
    // Step Sequencer hit-test for the selected Instrument row.
    {
        if (p.y >= area.getY())
        {
            const int logicalRow = selectedTrack;
            const int instrumentFirst = audioTrackCount + midiTrackCount;
            if (logicalRow >= instrumentFirst && logicalRow < instrumentFirst + instrumentTrackCount)
            {
                const int instrumentIndex = logicalRow - instrumentFirst;
                if (auto* pattern = getInstrumentStepSequencer(instrumentIndex))
                {
                    auto panel = area;
                    const int dockInset = 8;
                    panel = panel.reduced(dockInset, 6);
                    const auto stretchedControl = [&](int x, int w, int y, int h, int designWidth)
                    {
                        const int availableWidth = juce::jmax(1, panel.getWidth());
                        const int left = panel.getX() + (int) std::round((double)(x - panel.getX()) * availableWidth / designWidth);
                        const int right = panel.getX() + (int) std::round((double)(x - panel.getX() + w) * availableWidth / designWidth);
                        return juce::Rectangle<int>(left, y, juce::jmax(1, right - left), h);
                    };
                    const int titleWidth = 132;
                    auto onOff = juce::Rectangle<int>(panel.getX(), panel.getY(), titleWidth - 8 - 55, 30);
                    auto seqPlay = juce::Rectangle<int>(onOff.getRight(), onOff.getY(), 55, 30);
                    if (seqPlay.contains(p))
                    {
                        if (audioEngine.isStepPreviewPlaying()) audioEngine.setStepPreview(instrumentIndex, false);
                        else if (!audioEngine.isPlaying()) audioEngine.setStepPreview(instrumentIndex, true);
                        repaint(); return;
                    }
                    if (onOff.contains(p))
                    {
                        pattern->enabled = !pattern->enabled;
                        publishInstrumentStepSequencer(instrumentIndex);
                        repaint();
                        return;
                    }
                    const int available = juce::jmax(0, panel.getWidth() - titleWidth);
                    const int stepW = juce::jmax(12, juce::jmin(42, available / 16));
                    const int firstStep = juce::jlimit(0, 3, stepSequencerPage) * 16;
                    for (int s = 0; s < 16; ++s)
                    {
                        const int absoluteStep = firstStep + s;
                        auto pad = juce::Rectangle<int>(panel.getX() + titleWidth + (s * available) / 16, panel.getY(), juce::jmax(1, ((s + 1) * available) / 16 - (s * available) / 16 - 4), 30);
                        if (pad.contains(p) && absoluteStep < pattern->stepCount)
                        {
                            stepSequencerSelectedStep = absoluteStep;
                            pattern->steps[(size_t)absoluteStep].enabled = !pattern->steps[(size_t)absoluteStep].enabled;
                            publishInstrumentStepSequencer(instrumentIndex); repaint(); return;
                        }
                    }
                    const int controlsY = panel.getY() + 42;
                    auto hit = [&](int x, int w) { return stretchedControl(x, w, controlsY, 26, 550 + 34 * juce::jmax(1, (pattern->stepCount + 15) / 16)).contains(p); };
                    int cx = panel.getX();
                    const int counts[] = {16,32,64};
                    for (int n = 0; n < 3; ++n) { if (hit(cx,30)) { pattern->stepCount=counts[n]; stepSequencerPage=juce::jmin(stepSequencerPage,(counts[n]-1)/16); publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } cx+=(n == 2 ? 42 : 34); }
                    const std::int64_t rates[] = { MidiEngine::ticksPerQuarterNote, MidiEngine::ticksPerQuarterNote/2, MidiEngine::ticksPerQuarterNote/4, MidiEngine::ticksPerQuarterNote/8, MidiEngine::ticksPerQuarterNote/16 };
                    for (int r=0;r<5;++r) { if(hit(cx,38)) { pattern->stepTicks=rates[r]; publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } cx+=42; }
                    for (int m=0;m<3;++m) { if(hit(cx,34)) { pattern->rateModifier=(std::uint8_t)m; publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } cx+=38; }
                    if (hit(cx,44)) { pattern->direction=(LibertyStepSequencer::Direction)(((int)pattern->direction+1)%4); publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } cx+=48;
                    if (hit(cx,62)) { pattern->swing = pattern->swing >= 0.50f ? 0.0f : pattern->swing + 0.10f; publishInstrumentStepSequencer(instrumentIndex); repaint(); return; }
                    cx += 68;
                    const int pages = juce::jmax(1,(pattern->stepCount+15)/16);
                    for(int page=0;page<pages;++page) { if(hit(cx,30)) { stepSequencerPage=page; repaint(); return; } cx+=34; }
                    const int selectedStep = juce::jlimit(0, pattern->stepCount - 1, stepSequencerSelectedStep);
                    auto& editStep = pattern->steps[(size_t)selectedStep];
                    const int editY = controlsY + 36;
                    auto editHit = [&](int x, int w) { return stretchedControl(x, w, editY, 26, 780).contains(p); };
                    int ex = panel.getX() + 56;
                    if (editHit(ex,64)) { editStep.pitch = (std::uint8_t)(editStep.pitch >= 84 ? 36 : editStep.pitch + 1); publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } ex += 68;
                    if (editHit(ex,54)) { editStep.velocity = (std::uint8_t)(editStep.velocity >= 127 ? 20 : juce::jmin(127, (int)editStep.velocity + 10)); publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } ex += 58;
                    if (editHit(ex,66)) { editStep.gate = editStep.gate >= 1.0f ? 0.10f : juce::jmin(1.0f, editStep.gate + 0.10f); publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } ex += 70;
                    if (editHit(ex,66)) { editStep.probability = (std::uint8_t)(editStep.probability >= 100 ? 10 : juce::jmin(100, (int)editStep.probability + 10)); publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } ex += 70;
                    if (editHit(ex,62)) { editStep.ratchet = (std::uint8_t)(editStep.ratchet >= 8 ? 1 : editStep.ratchet + 1); publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } ex += 66;
                    if (editHit(ex,38)) { editStep.accent = !editStep.accent; publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } ex += 42;
                    if (editHit(ex,48)) { editStep.octave = editStep.octave >= 2 ? -2 : editStep.octave + 1; publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } ex += 52;
                    if (editHit(ex,38)) { editStep.tie = !editStep.tie; publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } ex += 42;
                    if (editHit(ex,42)) { editStep.channel = (std::uint8_t)(editStep.channel >= 16 ? 1 : editStep.channel + 1); publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } ex += 46;
                    if (editHit(ex,70)) { editStep.microTiming = editStep.microTiming >= 0.50f ? -0.50f : editStep.microTiming + 0.10f; publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } ex += 74;
                    if (editHit(ex,76)) { editStep.chord=(LibertyStepSequencer::Chord)(((int)editStep.chord+1)%5); publishInstrumentStepSequencer(instrumentIndex); repaint(); return; }
                    const int actionY = editY + 36;
                    auto actionHit = [&](int x, int w) { return stretchedControl(x, w, actionY, 26, 1260).contains(p); };
                    int ax = panel.getX();
                    if (actionHit(ax,48)) { for (int s=0;s<pattern->stepCount;++s) pattern->steps[(size_t)s] = {}; publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } ax += 52;
                    if (actionHit(ax,58))
                    {
                        for (int s=0;s<pattern->stepCount;++s)
                        {
                            auto& st=pattern->steps[(size_t)s];
                            const unsigned h=(unsigned)(s*1664525u+1013904223u);
                            st.enabled=(h%100u)<55u; st.velocity=(std::uint8_t)(70u+(h%58u)); st.probability=(std::uint8_t)(70u+(h%31u));
                        }
                        publishInstrumentStepSequencer(instrumentIndex); repaint(); return;
                    }
                    ax += 62;
                    if (actionHit(ax,62)) { std::reverse(pattern->steps.begin(), pattern->steps.begin()+pattern->stepCount); publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } ax += 66;
                    if (actionHit(ax,56))
                    {
                        if(pattern->stepCount>1) std::rotate(pattern->steps.begin(), pattern->steps.begin()+pattern->stepCount-1, pattern->steps.begin()+pattern->stepCount);
                        publishInstrumentStepSequencer(instrumentIndex); repaint(); return;
                    }
                    ax += 60;
                    if (actionHit(ax,72))
                    {
                        const int half=pattern->stepCount/2;
                        if(half>0) for(int s=0;s<half && s+half<pattern->stepCount;++s) pattern->steps[(size_t)(s+half)]=pattern->steps[(size_t)s];
                        publishInstrumentStepSequencer(instrumentIndex); repaint(); return;
                    }
                    ax += 80;
                    if (actionHit(ax,52)) { if (commitInstrumentStepSequencerToMidiClip(instrumentIndex, juce::jlimit(0, juce::jmax(0, getMidiTrackCount() - 1), stepSequencerMidiTarget))) repaint(); return; } ax += 56;
                    if (actionHit(ax,58)) { draggingStepSequencerPattern=true; draggedStepSequencerInstrument=instrumentIndex; repaint(); return; } ax += 62;
                    if (actionHit(ax,58)) { pattern->root=(std::uint8_t)((pattern->root+1)%12); publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } ax += 62;
                    if (actionHit(ax,82)) { pattern->scale=(LibertyStepSequencer::Scale)(((int)pattern->scale+1)%4); publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } ax += 86;
                    if (actionHit(ax,66)) { pattern->transpose=pattern->transpose>=12 ? -12 : pattern->transpose+1; publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } ax += 70;
                    if (actionHit(ax,52)) { pattern->octaveShift=pattern->octaveShift>=4 ? -4 : pattern->octaveShift+1; publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } ax += 56;
                    if (actionHit(ax,60)) { pattern->humanize=pattern->humanize>=1.0f ? 0.0f : juce::jmin(1.0f,pattern->humanize+0.10f); publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } ax += 64;
                    if (actionHit(ax,68))
                    {
                        pattern->euclideanPulses = pattern->euclideanPulses >= pattern->stepCount ? 1 : pattern->euclideanPulses + 1;
                        const int pulses = juce::jlimit(1, pattern->stepCount, pattern->euclideanPulses);
                        for (int s=0; s<pattern->stepCount; ++s)
                        {
                            int rotated=(s-pattern->euclideanRotation)%pattern->stepCount; if(rotated<0) rotated+=pattern->stepCount;
                            pattern->steps[(size_t)s].enabled = ((rotated * pulses) % pattern->stepCount) < pulses;
                        }
                        publishInstrumentStepSequencer(instrumentIndex); repaint(); return;
                    }
                    ax += 72;
                    if (actionHit(ax,70))
                    {
                        pattern->euclideanRotation = (pattern->euclideanRotation + 1) % juce::jmax(1,pattern->stepCount);
                        const int pulses=juce::jlimit(1,pattern->stepCount,pattern->euclideanPulses);
                        for(int s=0;s<pattern->stepCount;++s)
                        {
                            int rotated=(s-pattern->euclideanRotation)%pattern->stepCount; if(rotated<0) rotated+=pattern->stepCount;
                            pattern->steps[(size_t)s].enabled=((rotated*pulses)%pattern->stepCount)<pulses;
                        }
                        publishInstrumentStepSequencer(instrumentIndex); repaint(); return;
                    } ax += 74;
                    if (actionHit(ax,66)) { pattern->cycleSteps = pattern->cycleSteps >= pattern->stepCount ? 1 : pattern->cycleSteps + 1; publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } ax += 70;
                    if (actionHit(ax,42)) { pattern->velocityLaneSteps=pattern->velocityLaneSteps>=pattern->cycleSteps?1:pattern->velocityLaneSteps+1; publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } ax += 46;
                    if (actionHit(ax,42)) { pattern->gateLaneSteps=pattern->gateLaneSteps>=pattern->cycleSteps?1:pattern->gateLaneSteps+1; publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } ax += 46;
                    if (actionHit(ax,42)) { pattern->probabilityLaneSteps=pattern->probabilityLaneSteps>=pattern->cycleSteps?1:pattern->probabilityLaneSteps+1; publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } ax += 46;
                    if (actionHit(ax,42)) { pattern->ratchetLaneSteps=pattern->ratchetLaneSteps>=pattern->cycleSteps?1:pattern->ratchetLaneSteps+1; publishInstrumentStepSequencer(instrumentIndex); repaint(); return; }
                    ax += 46;
                    const auto createClipButton = juce::Rectangle<int>(panel.getX(), actionY + 30, 156, 23);
                    if (createClipButton.contains(p))
                    {
                        createInstrumentPatternClip(instrumentIndex);
                        return;
                    }
                    if ((size_t)instrumentIndex < instrumentStepSequencers.size())
                    {
                        for (int bankIndex=0; bankIndex<8; ++bankIndex)
                        {
                            const int gap = 5;
                            const int width = juce::jmax(1, (panel.getWidth() - 7 * gap) / 8);
                            const auto patRect = juce::Rectangle<int>(panel.getX() + bankIndex * (width + gap), panel.getBottom() - 30, width, 28);
                            if (patRect.contains(p))
                            {
                                instrumentStepSequencers[(size_t)instrumentIndex].activePattern = bankIndex;
                                stepSequencerPage = 0; stepSequencerSelectedStep = 0;
                                publishInstrumentStepSequencer(instrumentIndex); repaint(); return;
                            }
                            ax += 48;
                        }
                    }
                }
            }
        }
    }

}

void MainComponent::drawMidiStepSequencer(juce::Graphics& g, juce::Rectangle<int> area)
{
    // Obsidian editor surface matches the graphite hierarchy of the floating dock.
    juce::ColourGradient surface(juce::Colour(0xff1b2833), (float)area.getX(), (float)area.getY(),
                                 juce::Colour(0xff0b0f14), (float)area.getX(), (float)area.getBottom(), false);
    g.setGradientFill(surface);
    g.fillRect(area);
    if (area.getWidth() < 360 || area.getHeight() < 220)
    {
        // Obsidian resize hint remains legible in compact floating windows.
        g.setColour(juce::Colour(0xffc2d8e2));
        g.setFont(juce::Font(13.0f));
        g.drawFittedText("Agrandissez la fenêtre pour afficher le Step Sequencer MIDI",
                         area.reduced(12), juce::Justification::centred, 2);
        return;
    }
    const int midiIndex = selectedTrack - getAudioTrackCount();
    auto* pattern = getMidiStepSequencer(midiIndex);
    if (!pattern) return;
    const int left = area.getX() + 12, width = juce::jmax(1, area.getWidth() - 24);
    // Obsidian dock title echoes the selected-pattern turquoise hierarchy.
    g.setColour(juce::Colour(0xffb8e8ec));
    g.setFont(juce::Font(16.0f, juce::Font::bold));
    g.drawText("STEP SEQ MIDI " + juce::String(midiIndex + 1), left, area.getY() + 8, 210, 28, juce::Justification::left);
    // Quiet Obsidian header seam separates the title from pattern banks.
    g.setColour(juce::Colour(0xff48606d).withAlpha(0.50f));
    g.fillRect(left, area.getY() + 41, width, 1);
    auto& bank = midiStepSequencers[(size_t)midiIndex];
    for (int i = 0; i < 8; ++i)
    {
        const int bankStart = i * width / 8;
        const int bankEnd = (i + 1) * width / 8;
        auto r = juce::Rectangle<int>(left + bankStart, area.getY() + 48,
                                      juce::jmax(1, bankEnd - bankStart - 5), 29);
        const auto& bankPattern = bank.patterns[(size_t) i];
        const int bankCycleSteps = juce::jlimit(1, LibertyStepSequencer::maxSteps,
                                               bankPattern.cycleSteps);
        const bool hasNotes = std::any_of(bankPattern.steps.begin(),
                                          bankPattern.steps.begin() + bankCycleSteps,
                                          [](const auto& step) { return step.enabled; });
        // Obsidian bank surfaces match inactive MIDI page navigation.
        g.setColour(i == bank.activePattern ? juce::Colour(0xff23535d) : juce::Colour(0xff1b2833));
        g.fillRoundedRectangle(r.toFloat(), 5.0f);
        // Every pattern bank has a defined edge; selection keeps the turquoise focus.
        g.setColour(i == bank.activePattern ? juce::Colour(0xff54d6dc).withAlpha(0.85f)
                                            : juce::Colour(0xff48606d).withAlpha(0.85f));
        g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 5.0f, 1.0f);
        if (hasNotes)
        {
            g.setColour(juce::Colour(0xff54d6dc));
            g.fillEllipse((float) r.getRight() - 9.0f, (float) r.getY() + 4.0f, 5.0f, 5.0f);
        }
        g.setColour(i == bank.activePattern ? juce::Colour(0xffeffcfd) : juce::Colour(0xffb9ccd6));
        g.setFont(juce::Font(r.getWidth() < 42 ? 10.0f : 12.0f, juce::Font::bold));
        g.drawFittedText("P" + juce::String(i + 1), r.reduced(2, 0),
                         juce::Justification::centred, 1);
    }
    const int steps = 16;
    const int page = juce::jlimit(0, 3, stepSequencerPage);
    const int firstStep = page * 16;
    const int gap = 4, availableCellsWidth = width - (steps - 1) * gap;
    for (int i = 0; i < steps; ++i)
    {
        const int cellStart = (i * availableCellsWidth) / steps;
        const int cellEnd = ((i + 1) * availableCellsWidth) / steps;
        const int cellWidth = cellEnd - cellStart;
        auto r = juce::Rectangle<int>(left + cellStart + i * gap, area.getY() + 95, cellWidth, 52);
        const bool active = pattern->steps[(size_t)(firstStep + i)].enabled;
        const bool inCycle = firstStep + i < pattern->cycleSteps;
        // Three visual levels: playable notes, empty steps and steps outside the cycle.
        g.setColour(!inCycle ? juce::Colour(0xff18222c)
                             : active ? juce::Colour(0xff54d6dc)
                                      : juce::Colour(0xff304452));
        g.fillRoundedRectangle(r.toFloat(), 4.0f);
        if (i % 4 == 0)
        {
            // Group downbeats remain legible, but out-of-cycle cells recede.
            // Obsidian downbeat outline: focused in-cycle, quiet outside the pattern.
            g.setColour(inCycle ? juce::Colour(0xff54d6dc).withAlpha(0.85f)
                                : juce::Colour(0xff48606d).withAlpha(0.40f));
            g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 4.0f, 1.0f);
        }
        if (i > 0 && i % 4 == 0)
        {
            // Bar-group markers recede when the following group is outside the cycle.
            // Obsidian rhythmic group markers: clearer in-cycle, subdued beyond cycle.
            g.setColour(inCycle ? juce::Colour(0xff91bbc3).withAlpha(0.70f)
                                : juce::Colour(0xff48606d).withAlpha(0.45f));
            const float markerX = (float) r.getX() - 2.0f;
            g.drawVerticalLine((int) markerX, (float) r.getY() + 5.0f,
                               (float) r.getBottom() - 5.0f);
        }
        // Downbeat numbers read first on empty cells; active notes retain dark ink.
        // Out-of-cycle labels follow the subdued Obsidian secondary-text palette.
        g.setColour(!inCycle ? juce::Colour(0xff667d8d)
                             : active ? juce::Colour(0xff10242b)
                                      : (i % 4 == 0) ? juce::Colour(0xffeffcfd)
                                                     : juce::Colour(0xffdce6ee));
        g.setFont(juce::Font(cellWidth < 25 ? 10.0f : 12.0f, juce::Font::bold));
        g.drawFittedText(juce::String(firstStep + i + 1), r.reduced(1, 0),
                         juce::Justification::centred, 1);
    }
    g.setColour(juce::Colour(0xffb5c3d1));
    g.setFont(juce::Font(12.0f));
    const auto exportButton = juce::Rectangle<int>(left, area.getY() + 161, juce::jmin(180, width), 29);
    const int playableCycleSteps = juce::jlimit(1, LibertyStepSequencer::maxSteps, pattern->cycleSteps);
    const bool hasPlayableNotes = std::any_of(pattern->steps.begin(),
        pattern->steps.begin() + playableCycleSteps,
        [](const auto& step) { return step.enabled; });
    // Empty-pattern action shares the subdued graphite surface of inactive banks.
    g.setColour(hasPlayableNotes ? juce::Colour(0xff23535d) : juce::Colour(0xff1b2833));
    g.fillRoundedRectangle(exportButton.toFloat(), 5.0f);
    // Obsidian clip action: an available pattern is accented, an empty one recedes.
    g.setColour(hasPlayableNotes ? juce::Colour(0xff54d6dc).withAlpha(0.85f)
                                 : juce::Colour(0xff48606d).withAlpha(0.70f));
    g.drawRoundedRectangle(exportButton.toFloat().reduced(0.5f), 5.0f, 1.0f);
    // Empty-pattern status stays readable without competing with the enabled action.
    g.setColour(hasPlayableNotes ? juce::Colour(0xffeffcfd) : juce::Colour(0xffb9ccd6));
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText(hasPlayableNotes ? "CRÉER CLIP MIDI" : "PATTERN VIDE",
               exportButton, juce::Justification::centred);
    for (int pageIndex = 0; pageIndex < 4; ++pageIndex)
    {
        const auto pageRect = juce::Rectangle<int>(left + 190 + pageIndex * ((width - 190) / 4), area.getY() + 161, juce::jmax(1, (width - 190) / 4 - 4), 29);
        // Inactive page surfaces match the Obsidian MIDI editor graphite.
        g.setColour(pageIndex == page ? juce::Colour(0xff23535d) : juce::Colour(0xff1b2833));
        g.fillRoundedRectangle(pageRect.toFloat(), 5.0f);
        // Obsidian page navigation: inactive pages remain outlined and legible.
        g.setColour(pageIndex == page ? juce::Colour(0xff54d6dc).withAlpha(0.85f)
                                       : juce::Colour(0xff48606d).withAlpha(0.85f));
        g.drawRoundedRectangle(pageRect.toFloat().reduced(0.5f), 5.0f, 1.0f);
        g.setColour(pageIndex == page ? juce::Colour(0xffeffcfd) : juce::Colour(0xffb9ccd6));
        const bool compactPageButton = pageRect.getWidth() < 60;
        g.drawFittedText(compactPageButton ? "P" + juce::String(pageIndex + 1)
                                          : juce::String(pageIndex * 16 + 1) + "-" + juce::String((pageIndex + 1) * 16),
                         pageRect.reduced(2, 0), juce::Justification::centred, 1);
    }
    g.setColour(juce::Colour(0xffb5c3d1));
    g.setFont(juce::Font(12.0f));
    const int safeCycleSteps = juce::jlimit(1, LibertyStepSequencer::maxSteps,
                                           pattern->cycleSteps);
    const int activeSteps = (int) std::count_if(
        pattern->steps.begin(), pattern->steps.begin() + safeCycleSteps,
        [](const auto& step) { return step.enabled; });
    const auto infoY = area.getY() + 197;
    const auto patternCaption = "P" + juce::String(bank.activePattern + 1);
    g.setColour(juce::Colour(0xff54d6dc));
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText(patternCaption, left, infoY, 28, 20, juce::Justification::left);
    // Secondary pattern metadata stays legible against the Obsidian dock.
    g.setColour(juce::Colour(0xffc2d8e2));
    g.setFont(juce::Font(12.0f));
    g.drawFittedText(juce::String(safeCycleSteps) + " pas  |  "
                         + juce::String(activeSteps) + " notes  |  Piano Roll indépendant",
                     left + 28, infoY, juce::jmax(1, width - 28), 20,
                     juce::Justification::centredLeft, 1);
}

void MainComponent::clickMidiStepSequencer(juce::Point<int> point, juce::Rectangle<int> area)
{
    if (!area.contains(point) || area.getWidth() < 360 || area.getHeight() < 220)
        return;
    const int midiIndex = selectedTrack - getAudioTrackCount();
    auto* pattern = getMidiStepSequencer(midiIndex);
    if (!pattern) return;
    const int left = area.getX() + 12, width = juce::jmax(1, area.getWidth() - 24);
    const auto exportButton = juce::Rectangle<int>(left, area.getY() + 161, juce::jmin(180, width), 29);
    if (exportButton.contains(point))
    {
        // Never create a timeline clip from an empty or disabled bank.
        // In particular, inactive notes outside the cycle must not be
        // mistaken for a playable Pattern.
        const int playableCycleSteps = juce::jlimit(1, LibertyStepSequencer::maxSteps,
                                                   pattern->cycleSteps);
        const bool hasPlayableNotes = std::any_of(
            pattern->steps.begin(), pattern->steps.begin() + playableCycleSteps,
            [](const auto& step) { return step.enabled; });
        if (!hasPlayableNotes)
        {
            repaint();
            return;
        }
        // Render from a temporary normalized copy: exporting must not mutate
        // the saved Pattern or mark the project dirty just by clicking Export.
        auto exportPattern = *pattern;
        exportPattern.enabled = true;
        const auto notes = LibertyStepSequencer::render(exportPattern);
        if (!notes.empty())
        {
            const double lengthSeconds = MidiEngine::tickToSeconds(
                LibertyStepSequencer::getCycleLengthTicks(exportPattern), tempoBpm);
            commitLibertySequencerMidiClip(*this, notes, midiIndex,
                                           playheadSeconds, juce::jmax(0.01, lengthSeconds));
        }
        repaint();
        return;
    }
    for (int pageIndex = 0; pageIndex < 4; ++pageIndex)
    {
        const auto pageRect = juce::Rectangle<int>(left + 190 + pageIndex * ((width - 190) / 4), area.getY() + 161, juce::jmax(1, (width - 190) / 4 - 4), 29);
        if (pageRect.contains(point))
        {
            // Page navigation is visual only: merely inspecting steps 49-64
            // must not silently expand the musical length of the Pattern.
            stepSequencerPage = pageIndex;
            repaint();
            return;
        }
    }
    auto& bank = midiStepSequencers[(size_t)midiIndex];
    for (int i = 0; i < 8; ++i)
    {
        const int bankStart = i * width / 8;
        const int bankEnd = (i + 1) * width / 8;
        auto r = juce::Rectangle<int>(left + bankStart, area.getY() + 48,
                                      juce::jmax(1, bankEnd - bankStart - 5), 29);
        if (r.contains(point))
        {
            bank.activePattern = i;
            stepSequencerPage = 0;
            stepSequencerSelectedStep = 0;
            repaint();
            return;
        }
    }
    const int steps = 16;
    const int page = juce::jlimit(0, 3, stepSequencerPage);
    const int firstStep = page * 16;
    const int gap = 4, availableCellsWidth = width - (steps - 1) * gap;
    for (int i = 0; i < steps; ++i)
    {
        const int cellStart = (i * availableCellsWidth) / steps;
        const int cellEnd = ((i + 1) * availableCellsWidth) / steps;
        const int cellWidth = cellEnd - cellStart;
        auto r = juce::Rectangle<int>(left + cellStart + i * gap, area.getY() + 95, cellWidth, 52);
        if (r.contains(point))
        {
            auto& step = pattern->steps[(size_t)(firstStep + i)];
            step.enabled = !step.enabled;
            if (step.enabled)
            {
                // Only enabling a note extends the musical cycle. Disabling a
                // step must never lengthen an otherwise shorter Pattern.
                pattern->stepCount = juce::jmax(pattern->stepCount, firstStep + i + 1);
                pattern->cycleSteps = juce::jmax(pattern->cycleSteps, pattern->stepCount);
            }
            // A bank with no enabled notes should not be exported as an
            // active Pattern; preserve its length for later editing.
            const int cycleSteps = juce::jlimit(1, LibertyStepSequencer::maxSteps,
                                               pattern->cycleSteps);
            pattern->enabled = std::any_of(pattern->steps.begin(),
                                           pattern->steps.begin() + cycleSteps,
                                           [](const auto& candidate) { return candidate.enabled; });
            repaint();
            return;
        }
    }
}

void MainComponent::clickFloatingStepSequencer(juce::Point<int> point, juce::Rectangle<int> area)
{
    if (selectedTrack >= getAudioTrackCount() && selectedTrack < getAudioTrackCount() + getMidiTrackCount())
        clickMidiStepSequencer(point, area);
    else
        handleStepSequencerClick(point, area);
}

// Liberty Pro / Obsidian: transport palette is intentionally local to the
// drawing layer. Keep all existing hit rectangles and transport behaviour.
namespace LibertyProTransport
{
constexpr juce::uint32 surface = 0xff18212b;
constexpr juce::uint32 outline = 0xff344452;
constexpr juce::uint32 button = 0xff25323e;
constexpr juce::uint32 accent = 0xff54d6dc;
constexpr juce::uint32 text = 0xffe8eff7;
constexpr juce::uint32 muted = 0xffb5c3d1;
}

void MainComponent::drawTransport(juce::Graphics& g, juce::Rectangle<int> area)
{
    juce::ColourGradient transportSurface(LibertyUI::Colour::surface, (float)area.getX(), (float)area.getY(),
                                          LibertyUI::Colour::canvas, (float)area.getX(), (float)area.getBottom(), false);
    g.setGradientFill(transportSurface);
    g.fillRect(area);
    g.setColour(juce::Colour(LibertyProTransport::outline)); g.drawHorizontalLine(area.getBottom() - 1, 0.0f, (float)getWidth());
    g.setColour(juce::Colour(0xff54d6dc).withAlpha(0.45f));
    g.fillRect(area.getX(), area.getBottom() - 2, juce::jmin(182, area.getWidth()), 1);

    // LIBERTY PRO 2.0: a modern, custom vector brand panel replacing the old
    // scripted logo. Everything is drawn inside the existing 182 x 66 region.
    const auto brandPanel = juce::Rectangle<float>(8.0f, 8.0f, 178.0f, 60.0f);
    g.setColour(LibertyUI::Colour::surface);
    g.fillRoundedRectangle(brandPanel, 10.0f);
    g.setColour(LibertyUI::Colour::divider);
    g.drawRoundedRectangle(brandPanel.reduced(0.5f), 10.0f, 1.0f);
    g.setColour(LibertyUI::Colour::accent);
    g.fillRoundedRectangle(15.0f, 17.0f, 3.0f, 42.0f, 1.5f);

    // Signal emblem has a reserved 34px region and never touches the wordmark.
    juce::Path signal;
    signal.startNewSubPath(26.0f, 41.0f);
    signal.lineTo(30.0f, 41.0f);
    signal.lineTo(34.0f, 30.0f);
    signal.lineTo(39.0f, 48.0f);
    signal.lineTo(44.0f, 35.0f);
    signal.lineTo(49.0f, 41.0f);
    signal.lineTo(54.0f, 41.0f);
    g.setColour(LibertyUI::Colour::accent);
    g.strokePath(signal, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    g.setColour(LibertyUI::Colour::textPrimary);
    g.setFont(juce::Font(23.0f, juce::Font::bold));
    g.drawFittedText("LIBERTY", juce::Rectangle<int>(62, 17, 116, 29),
                     juce::Justification::centredLeft, 1);
    g.setColour(LibertyUI::Colour::textMuted);
    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.drawFittedText("PRO  /  DIGITAL AUDIO", juce::Rectangle<int>(63, 47, 115, 13),
                     juce::Justification::centredLeft, 1);

    // Unified transport deck: five transport controls and the existing
    // interactive BPM/signature component share one Obsidian panel.
    // The controls retain their exact bounds and callbacks.
    const auto commandDeck = juce::Rectangle<float>(205.0f, 31.0f, 528.0f, 42.0f);
    g.setColour(LibertyUI::Colour::canvas);
    g.fillRoundedRectangle(commandDeck, 11.0f);
    g.setColour(LibertyUI::Colour::divider);
    g.drawRoundedRectangle(commandDeck.reduced(0.5f), 11.0f, 1.0f);
    // Obsidian deck: a restrained top-edge highlight and a quiet lower
    // cyan guide reinforce the command group without covering controls.
    g.setColour(juce::Colour(0xff526978).withAlpha(0.65f));
    g.drawHorizontalLine(32, 218.0f, 720.0f);
    g.setColour(LibertyUI::Colour::accent.withAlpha(0.62f));
    g.fillRoundedRectangle(276.0f, 70.0f, 380.0f, 2.0f, 1.0f);

    // Liberty Pro transport: high-contrast segmented controls with no geometry changes.
    // Hit targets remain exactly 56 x 28 at the existing coordinates.
    const char* labels[] = { "|<", "<", "PLAY", ">", "|>" };
    for (int i = 0; i < 5; ++i)
    {
        const auto r = juce::Rectangle<int>(215 + i * 62, 38, 56, 28);
        const bool primary = (i == 2);
        const bool playing = primary && isPlaying;
        const auto face = primary ? (playing ? juce::Colour(0xff177f83) : juce::Colour(0xff24535d))
                                  : LibertyUI::Colour::surface;
        g.setColour(face);
        g.fillRoundedRectangle(r.toFloat(), 7.0f);
        g.setColour(primary ? LibertyUI::Colour::accent : LibertyUI::Colour::divider);
        g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 7.0f, primary ? 1.7f : 1.0f);

        if (primary)
        {
            // A visible accent rail is contained inside PLAY/STOP's own bounds.
            g.setColour(LibertyUI::Colour::accent);
            g.fillRoundedRectangle((float)r.getX() + 9.0f, (float)r.getBottom() - 4.0f,
                                   (float)r.getWidth() - 18.0f, 2.0f, 1.0f);
        }

        const auto textArea = r.reduced(4, 3);
        g.setColour(primary ? LibertyUI::Colour::textPrimary : LibertyUI::Colour::textCyan);
        g.setFont(juce::Font(primary ? 12.0f : 11.0f, juce::Font::bold));
        g.drawFittedText(playing ? "STOP" : labels[i], textArea, juce::Justification::centred, 1);
    }

    const double secondsPerBeat = 60.0 / juce::jmax(1.0, tempoBpm) * (4.0 / (double) juce::jmax(1, timeSignatureDenominator));
    const double beatsPerMeasure = (double) juce::jmax(1, timeSignatureNumerator);
    const double secondsPerMeasure = secondsPerBeat * beatsPerMeasure;
    const auto safeTime = juce::jmax(0.0, playheadSeconds);
    const auto measure = static_cast<long long>(std::floor(safeTime / secondsPerMeasure)) + 1;
    const auto beat = static_cast<int>(std::floor(std::fmod(safeTime, secondsPerMeasure) / secondsPerBeat)) + 1;

    // Readouts use dedicated, non-overlapping regions. Preserve existing hit targets.
    const auto positionBox = juce::Rectangle<int>(740, 34, 90, 36);
    g.setColour(LibertyUI::Colour::surface);
    g.fillRoundedRectangle(positionBox.toFloat(), 7.0f);
    g.setColour(LibertyUI::Colour::accent);
    g.drawRoundedRectangle(positionBox.toFloat().reduced(0.5f), 7.0f, 1.3f);
    g.fillRoundedRectangle((float)positionBox.getX() + 10.0f, (float)positionBox.getBottom() - 4.0f,
                           (float)positionBox.getWidth() - 20.0f, 2.0f, 1.0f);
    g.setColour(LibertyUI::Colour::textPrimary);
    g.setFont(juce::Font(16.0f, juce::Font::bold));
    g.drawFittedText(juce::String(measure) + ":" + juce::String(beat),
                     positionBox.reduced(5, 5), juce::Justification::centred, 1);

    const auto musicInfo = juce::Rectangle<int>(836, 34, 82, 36);
    g.setColour(LibertyUI::Colour::surface);
    g.fillRoundedRectangle(musicInfo.toFloat(), 7.0f);
    g.setColour(LibertyUI::Colour::divider);
    g.drawRoundedRectangle(musicInfo.toFloat().reduced(0.5f), 7.0f, 1.0f);
    // The caption and value have separate vertical rectangles.
    g.setColour(LibertyUI::Colour::textMuted);
    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.drawFittedText("BPM / SIGN", juce::Rectangle<int>(840, 37, 74, 12),
                     juce::Justification::centred, 1);
    g.setColour(LibertyUI::Colour::textPrimary);
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawFittedText(juce::String(tempoBpm, 0) + "  " + juce::String(timeSignatureNumerator)
                     + "/" + juce::String(timeSignatureDenominator),
                     juce::Rectangle<int>(840, 51, 74, 16), juce::Justification::centred, 1);

    // Audio settings is already a real top-bar control elsewhere in Liberty.
    // Do not paint a duplicate over that control: it hides the real UI and
    // creates a mismatch between visual layout and hit testing.

}

void MainComponent::drawTrackArea(juce::Graphics& g, juce::Rectangle<int> area)
{
    constexpr int headerW = trackHeaderWidth, rulerH = trackRulerHeight;
    const int audioCount = audioEngine.getAudioTrackCount();
    const int midiCount = getMidiTrackCount();
    const int instrumentCount = getInstrumentTrackCount();
    const int rowH = getLibertyTrackRowHeight();
    const int scrollRows = getTrackScrollRows();
    const double pixelsPerSecond = getLibertyTimelinePixelsPerSecond();
    const double secondsPerBeat = 60.0 / juce::jmax(1.0, tempoBpm) * (4.0 / (double) juce::jmax(1, timeSignatureDenominator));
    const double secondsPerMeasure = secondsPerBeat * (double) juce::jmax(1, timeSignatureNumerator);
    const float pixelsPerMeasure = (float)(secondsPerMeasure * pixelsPerSecond);

    auto ruler = area.removeFromTop(rulerH);
    auto rows = area;
    // Deeper Obsidian ruler surface improves label and playhead contrast.
    g.setColour(juce::Colour(0xff141e28)); g.fillRect(ruler);
    // Distinct graphite corner anchors the track headers against the ruler.
    g.setColour(juce::Colour(0xff1d2a35));
    g.fillRect(ruler.withWidth(headerW));
    // Continue the header/timeline seam through the ruler corner.
    g.setColour(juce::Colour(0xff40515f));
    g.drawVerticalLine(headerW - 1, (float)ruler.getY(), (float)ruler.getBottom());
    g.setColour(juce::Colour(0xff202c37)); g.fillRect(rows.withWidth(headerW));
    g.setColour(juce::Colour(0xff0b0f14)); g.fillRect(rows.withTrimmedLeft(headerW));
    // Thin boundary keeps the time ruler distinct from the track canvas.
    g.setColour(juce::Colour(0xff344452));
    g.drawHorizontalLine(ruler.getBottom() - 1, (float)area.getX(), (float)area.getRight());

    // Four-bar anchors stand out while intermediate measure lines recede.
    for (int measureIndex = 0; measureIndex < 100; ++measureIndex)
    {
        const int x = headerW + (int)std::round(measureIndex * pixelsPerMeasure);
        if (x >= getWidth()) break;
        const bool fourBarAnchor = (measureIndex % 4) == 0;
        g.setColour(fourBarAnchor ? juce::Colour(0xff607787)
                                  : juce::Colour(0xff2b3946));
        g.drawVerticalLine(x, (float)ruler.getY(), (float)rows.getBottom());
        // Short ruler-only accent: clear four-bar grouping without extra
        // vertical weight over clips and waveforms.
        if (fourBarAnchor && x >= headerW && x < ruler.getRight())
        {
            g.setColour(juce::Colour(0xff6db8c5).withAlpha(0.72f));
            g.fillRect(x, ruler.getBottom() - 5, 2, 4);
        }
    }
    // Measure numbers are clearer at bar-group boundaries, subdued elsewhere.
    g.setFont(juce::Font(11.0f));
    for (int i = 0; i < 100; ++i)
    {
        const int x = headerW + (int)std::round(i * pixelsPerMeasure);
        if (x >= getWidth()) break;
        const bool groupStart = (i % 4) == 0;
        g.setColour(groupStart ? juce::Colour(0xffe8f3f5)
                               : juce::Colour(0xff8499aa));
        g.setFont(juce::Font(groupStart ? 12.0f : 11.0f,
                             groupStart ? juce::Font::bold : juce::Font::plain));
        g.drawText(juce::String(i + 1), x + 6, ruler.getY() + 7, 35, 18, juce::Justification::left);
    }

    const int totalRows = audioCount + midiCount + instrumentCount;
    // Draw the final partially visible row too, but clip it strictly to the
    // arranger viewport. This makes the grid meet the mixer with no dead strip.
    const int visibleRows = juce::jmax(1, (rows.getHeight() + rowH - 1) / rowH);
    for (int logicalRow = scrollRows; logicalRow < totalRows && logicalRow < scrollRows + visibleRows; ++logicalRow)
    {
        const int visibleIndex = logicalRow - scrollRows;
        auto row = juce::Rectangle<int>(rows.getX(), rows.getY() + visibleIndex * rowH, rows.getWidth(), rowH);
        if (row.getY() >= rows.getBottom()) break;
        row = row.getIntersection(rows);
        // Restrained graphite zebra lanes keep large sessions readable.
        // Selection remains distinct without overpowering clip colours.
        const bool activeLane = logicalRow == selectedTrack;
        g.setColour(activeLane ? juce::Colour(0xff1b303a)
                     : (logicalRow % 2 == 0 ? juce::Colour(0xff151f29)
                                            : juce::Colour(0xff111b24)));
        g.fillRect(row);
        auto header = row.withWidth(headerW);
        bool selected = false;
        juce::String title;
        if (logicalRow < audioCount)
        {
            const int i = logicalRow;
            selected = selectedTrack == i;
            title = "Audio " + juce::String(i + 1);
            g.setColour(selected ? juce::Colour(0xff23434c) : juce::Colour(0xff1b2833)); g.fillRect(header);
            auto clip = row.withTrimmedLeft(headerW + 20).reduced(4);
            if (audioEngine.hasAudioFile(i))
            {
                clip.setWidth(juce::jmax(1, (int)std::round(audioEngine.getAudioFileLengthSeconds(i) * pixelsPerSecond)));
                clip.setX(headerW + (int)std::round(audioEngine.getTrackStartSeconds(i) * pixelsPerSecond));
                g.setColour(selected ? juce::Colour(0xff315e73) : juce::Colour(0xff24475b)); g.fillRoundedRectangle(clip.toFloat(), 5.0f);
                g.setColour(selected ? juce::Colour(0xff89d6e5)
                                     : juce::Colour(0xff5598b3));
                // Inset selection rim stays inside the clip and preserves hit geometry.
                if (selected && clip.getWidth() > 8 && clip.getHeight() > 8)
                {
                    g.setColour(juce::Colour(0xffb4eaf1).withAlpha(0.22f));
                    g.drawRoundedRectangle(clip.toFloat().reduced(2.0f), 4.0f, 1.0f);
                    g.setColour(juce::Colour(0xff89d6e5));
                }
                g.drawRoundedRectangle(clip.toFloat().reduced(0.5f), 5.0f, selected ? 1.5f : 1.0f);
                if ((size_t)i < waveformMin.size() && !waveformMin[(size_t)i].empty())
                {
                    const auto centreY=clip.getCentreY(); const auto amplitude=juce::jmax(1.0f,clip.getHeight()*0.42f);
                    const int points=(int)waveformMin[(size_t)i].size(); juce::Path waveform;
                    for(int p=0;p<points;++p){const auto x=clip.getX()+4.0f+(clip.getWidth()-8.0f)*(float)p/(float)juce::jmax(1,points-1);const auto y=(float)centreY-waveformMax[(size_t)i][(size_t)p]*amplitude;if(p==0)waveform.startNewSubPath(x,y);else waveform.lineTo(x,y);}
                    for(int p=points-1;p>=0;--p){const auto x=clip.getX()+4.0f+(clip.getWidth()-8.0f)*(float)p/(float)juce::jmax(1,points-1);waveform.lineTo(x,(float)centreY-waveformMin[(size_t)i][(size_t)p]*amplitude);}
                    waveform.closeSubPath();
                    // A soft underlay adds depth without altering waveform geometry.
                    g.setColour(juce::Colour(0xff0c2432).withAlpha(0.30f));
                    g.fillPath(waveform);
                    g.setColour(selected ? juce::Colour(0xffc3f1f6)
                                         : juce::Colour(0xff92c5d5));
                    g.fillPath(waveform);
                }
                // File names follow the same selected/inactive hierarchy as track headers.
                g.setColour(selected ? juce::Colour(0xfff5fcfd)
                                     : juce::Colour(0xffd1e2ec));
                g.setFont(juce::Font(selected ? 11.5f : 11.0f,
                                     selected ? juce::Font::bold : juce::Font::plain));
                g.drawText(audioEngine.getAudioFileName(i), clip.reduced(10), juce::Justification::centredLeft, true);
            }
        }
        else if (logicalRow < audioCount + midiCount)
        {
            const int i = logicalRow - audioCount;
            title = "MIDI " + juce::String(i + 1);
            selected = selectedTrack == audioCount + i;
            g.setColour(selected ? juce::Colour(0xff23434c) : juce::Colour(0xff1b2833)); g.fillRect(header);
        }
        else
        {
            const int i = logicalRow - audioCount - midiCount;
            title = "Instrument " + juce::String(i + 1);
            selected = selectedTrack == audioCount + midiCount + i;
            g.setColour(selected ? juce::Colour(0xff23434c) : juce::Colour(0xff1b2833)); g.fillRect(header);

        }
        // Category accents are painted once in the clipped header pass below.
        // Avoid a redundant rail beneath the Obsidian header surface.
        // Obsidian Pro: category accent and subtle selected-track surface.
        const juce::Colour categoryAccent = logicalRow < audioCount ? juce::Colour(0xff55b9d8)
                                           : logicalRow < audioCount + midiCount ? juce::Colour(0xffae91ed)
                                                                                 : juce::Colour(0xffe0ae65);
        {
            const juce::Graphics::ScopedSaveState headerState(g);
            g.reduceClipRegion(header);
            g.setColour(selected ? juce::Colour(0xff29414b)
                                 : (logicalRow % 2 == 0 ? juce::Colour(0xff1b2935)
                                                        : juce::Colour(0xff192631)));
            g.fillRect(header);
            if (selected)
            {
                // Restrained category tint and a crisp inset edge keep
                // the active header legible without obscuring its controls.
                g.setColour(categoryAccent.withAlpha(0.14f));
                g.fillRect(header.withTrimmedLeft(10));
                g.setColour(categoryAccent.withAlpha(0.52f));
                g.drawHorizontalLine(header.getY(), (float) header.getX() + 10.0f,
                                     (float) header.getRight() - 1.0f);
            }
            // Fine two-tone category rail: bright core, soft neighbouring edge.
            g.setColour(categoryAccent.withAlpha(selected ? 0.22f : 0.12f));
            g.fillRect(header.getX() + 4, header.getY() + 7, 5,
                       juce::jmax(0, header.getHeight() - 14));
            g.setColour(categoryAccent.withAlpha(selected ? 0.98f : 0.72f));
            g.fillRect(header.getX() + 5, header.getY() + 7, 2,
                       juce::jmax(0, header.getHeight() - 14));
            g.setColour(juce::Colour(0xff344653));
            g.drawVerticalLine(header.getRight() - 1, (float) header.getY(), (float) header.getBottom());
        }
        g.setColour(juce::Colour(0xffe8eff7)); g.setFont(juce::Font(14.0f, juce::Font::bold));
        if (selected)
        {
            g.setColour(juce::Colour(0xff54d6dc));
            g.fillRect(header.getX(), header.getY(), 3, header.getHeight());
        }
        // Selected track labels carry stronger contrast than inactive headers.
        g.setColour(selected ? juce::Colour(0xfff4fbfc)
                             : juce::Colour(0xffc0ceda));
        g.setFont(juce::Font(selected ? 14.0f : 13.5f,
                             selected ? juce::Font::bold : juce::Font::plain));
        g.drawFittedText(title, header.getX() + 18, header.getY() + 8, juce::jmax(0, header.getWidth() - 28), juce::jmax(0, juce::jmin(24, header.getHeight() - 8)), juce::Justification::centredLeft, 1, 0.85f);
        // Obsidian Pro track strip: real M/S switches and live gain for Audio
        // and Instrument lanes. The audio ARM/MON components occupy the first
        // row and remain independent of these painted, hit-tested controls.
        if (logicalRow < audioCount || logicalRow >= audioCount + midiCount)
        {
            const bool audioLane = logicalRow < audioCount;
            const int channel = audioLane ? logicalRow : logicalRow - audioCount - midiCount;
            const bool muted = audioLane ? audioEngine.isTrackMuted(channel) : audioEngine.isInstrumentTrackMuted(channel);
            const bool soloed = audioLane ? audioEngine.isTrackSolo(channel) : audioEngine.isInstrumentTrackSolo(channel);
            const float gain = audioLane ? audioEngine.getTrackGain(channel) : audioEngine.getInstrumentTrackGain(channel);
            const int controlsY = header.getY() + 36;
            if (!audioLane)
            {
                const bool armed = armedInstrumentTrack == channel;
                const auto recRect = juce::Rectangle<int>(header.getX() + 108, header.getY() + 7, 46, 24);
                g.setColour(armed ? juce::Colour(0xff873340) : juce::Colour(0xff202c37));
                g.fillRoundedRectangle(recRect.toFloat(), 5.0f);
                g.setColour(armed ? juce::Colour(0xffff8994) : juce::Colour(0xff405360));
                g.drawRoundedRectangle(recRect.toFloat().reduced(0.5f), 5.0f, 1.0f);
                g.setColour(armed ? juce::Colours::white : juce::Colour(0xffffcbd0));
                g.setFont(juce::Font(10.0f, juce::Font::bold));
                g.drawText("REC", recRect, juce::Justification::centred);
                const bool monitoring = monitoredInstrumentTrack == channel;
                const auto monRect = juce::Rectangle<int>(header.getX() + 158, header.getY() + 7, 48, 24);
                g.setColour(monitoring ? juce::Colour(0xff24535d) : juce::Colour(0xff202c37));
                g.fillRoundedRectangle(monRect.toFloat(), 5.0f);
                g.setColour(monitoring ? juce::Colour(0xff54d6dc) : juce::Colour(0xff405360));
                g.drawRoundedRectangle(monRect.toFloat().reduced(0.5f), 5.0f, 1.0f);
                g.setColour(monitoring ? juce::Colours::white : juce::Colour(0xffd6e5ee));
                g.drawText("MON", monRect, juce::Justification::centred);
            }
            auto drawStripButton = [&](juce::Rectangle<int> r, const juce::String& label, bool active, juce::Colour tint)
            {
                g.setColour(active ? tint.withAlpha(0.38f) : juce::Colour(0xff202c37));
                g.fillRoundedRectangle(r.toFloat(), 5.0f);
                g.setColour(active ? tint : juce::Colour(0xff405360));
                g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 5.0f, 1.0f);
                g.setColour(active ? juce::Colours::white : juce::Colour(0xffd6e5ee));
                g.setFont(juce::Font(10.0f, juce::Font::bold));
                g.drawText(label, r, juce::Justification::centred);
            };
            drawStripButton({header.getX() + 18, controlsY, 32, 24}, "M", muted, juce::Colour(0xffe16a75));
            drawStripButton({header.getX() + 55, controlsY, 32, 24}, "S", soloed, juce::Colour(0xffe2b65c));
            const auto gainBar = juce::Rectangle<int>(header.getX() + 94, controlsY + 7, 98, 10);
            g.setColour(juce::Colour(0xff101b24)); g.fillRoundedRectangle(gainBar.toFloat(), 4.0f);
            g.setColour(juce::Colour(0xff54d6dc));
            g.fillRoundedRectangle(gainBar.withWidth(juce::jlimit(0, gainBar.getWidth(), (int)std::round(gain * 0.5f * gainBar.getWidth()))).toFloat(), 4.0f);
            g.setColour(juce::Colour(0xffa9beca)); g.setFont(juce::Font(9.0f));
            g.drawText("VOL", header.getX() + 94, controlsY - 7, 42, 12, juce::Justification::centredLeft);
        }
        // Draw the timeline grid inside EVERY visible row, including the
        // final clipped row immediately above the mixer.  Previously the only
        // vertical lines were painted before the rows; each row background then
        // painted over them, which is why the last track could appear gridless.
        {
            const juce::Graphics::ScopedSaveState rowGridState(g);
            g.reduceClipRegion(row.withTrimmedLeft(headerW));
            g.setColour(juce::Colour(0xff202c37));
            // Uniform 1/16 Arrange grid on every row, including the final clipped row.
            constexpr int subdivisions = 16;
            const float pixelsPerSubdivision = pixelsPerMeasure / (float) subdivisions;
            for (int subdivisionIndex = 0; subdivisionIndex < 100 * subdivisions; ++subdivisionIndex)
            {
                const int x = headerW + (int) std::round(subdivisionIndex * pixelsPerSubdivision);
                if (x >= row.getRight()) break;
                if (x < headerW) continue;
                const bool measureLine = (subdivisionIndex % subdivisions) == 0;
                const bool beatLine = (subdivisionIndex % 4) == 0;
                // Visual hierarchy only: bar > quarter-note beat > sixteenth subdivision.
                g.setColour(measureLine ? (selected ? juce::Colour(0xff66818e)
                                                    : juce::Colour(0xff526777))
                                        : beatLine ? (selected ? juce::Colour(0xff3b5360)
                                                               : juce::Colour(0xff30414e))
                                                   : (selected ? juce::Colour(0xff253743)
                                                               : juce::Colour(0xff202c37)));
                g.drawVerticalLine(x, (float) row.getY(), (float) row.getBottom());
            }
        }
        if (logicalRow >= audioCount + midiCount)
        {
            const int instrumentIndex = logicalRow - audioCount - midiCount;
            if (instrumentIndex >= 0 && instrumentIndex < (int) instrumentStepSequencers.size())
            {
                const auto& bank = instrumentStepSequencers[(size_t) instrumentIndex];
                for (const auto& patternClip : bank.timelineClips)
                {
                    if (patternClip.lengthSeconds <= 0.0) continue;
                    const int clipX = headerW + (int) std::round(patternClip.startSeconds * pixelsPerSecond);
                    const int clipWidth = juce::jmax(1, (int) std::round(patternClip.lengthSeconds * pixelsPerSecond));
                    auto clip = juce::Rectangle<int>(clipX, row.getY() + 5, clipWidth, juce::jmax(1, row.getHeight() - 10))
                                    .getIntersection(row.withTrimmedLeft(headerW));
                    if (!clip.isEmpty())
                    {
                        g.setColour(selected ? juce::Colour(0xff645034)
                                             : juce::Colour(0xff463b2c));
                        g.fillRoundedRectangle(clip.toFloat(), 5.0f);
                        g.setColour(selected ? juce::Colour(0xffe0ae65)
                                             : juce::Colour(0xffae8954));
                        if (selected && clip.getWidth() > 8 && clip.getHeight() > 8)
                        {
                            g.setColour(juce::Colour(0xfff3d7a6).withAlpha(0.20f));
                            g.drawRoundedRectangle(clip.toFloat().reduced(2.0f), 4.0f, 1.0f);
                            g.setColour(juce::Colour(0xffe0ae65));
                        }
                        g.drawRoundedRectangle(clip.toFloat().reduced(0.5f), 5.0f, selected ? 1.5f : 1.0f);
                        g.setColour(selected ? juce::Colour(0xfff8f2e8)
                                             : juce::Colour(0xffe2d4bf));
                        g.setFont(juce::Font(selected ? 11.5f : 11.0f, juce::Font::bold));
                        g.drawText(patternClip.name, clip.reduced(7, 2), juce::Justification::centredLeft, true);
                    }
                }
            }
        }
        // Stronger dividers only where the next track changes category.
        const int nextRow = logicalRow + 1;
        const bool categoryBoundary = (nextRow == audioCount && midiCount + instrumentCount > 0)
                                   || (nextRow == audioCount + midiCount && instrumentCount > 0);
        // Crisp, low-contrast lane dividers; category changes get a stronger cue.
        g.setColour(categoryBoundary ? juce::Colour(0xff526475)
                                     : selected ? juce::Colour(0xff3e5963)
                                                : juce::Colour(0xff263440));
        g.drawHorizontalLine(row.getBottom() - 1, (float) row.getX(), (float) row.getRight());
        // Header-only highlight creates a refined inset edge on the selected track.
        if (selected && row.getHeight() > 2)
        {
            g.setColour(juce::Colour(0xff42606a).withAlpha(0.42f));
            g.drawHorizontalLine(row.getBottom() - 2, (float) row.getX() + 10.0f,
                                 (float) juce::jmin(row.getRight(), headerW - 2));
        }
    }

    // Two-tone divider gives the header/canvas junction subtle depth
    // without changing the actual track-header width or hit regions.
    g.setColour(juce::Colour(0xff09121a).withAlpha(0.72f));
    g.drawVerticalLine(headerW, (float)rows.getY(), (float)rows.getBottom());
    g.setColour(juce::Colour(0xff536777).withAlpha(0.88f));
    g.drawVerticalLine(headerW - 1, (float)rows.getY(), (float)rows.getBottom());

    const float playheadX = headerW + (float)(playheadSeconds * pixelsPerSecond);
    if (playheadX >= headerW && playheadX <= (float)getWidth())
    {
        // Subtle halo separates the transport cursor from dense clip waveforms.
        g.setColour(juce::Colour(0xffe4a85e).withAlpha(0.18f));
        g.drawLine(playheadX, (float)ruler.getY(), playheadX, (float)area.getBottom(), 5.0f);
        g.setColour(juce::Colour(0xfff2bb73));
        g.drawLine(playheadX, (float)ruler.getY(), playheadX, (float)area.getBottom(), 1.5f);
        // Small ruler-only locator; the transport position and hit areas are unchanged.
        juce::Path playheadMarker;
        playheadMarker.addTriangle(playheadX - 5.0f, (float)ruler.getY() + 2.0f,
                                   playheadX + 5.0f, (float)ruler.getY() + 2.0f,
                                   playheadX, (float)ruler.getY() + 10.0f);
        g.fillPath(playheadMarker);
    }
}

void MainComponent::drawStepSequencerDock(juce::Graphics& g, juce::Rectangle<int> area)
{
    juce::ColourGradient dockGradient(juce::Colour(0xff202c37), (float)area.getX(), (float)area.getY(),
                                      juce::Colour(0xff0b0f14), (float)area.getX(), (float)area.getBottom(), false);
    g.setGradientFill(dockGradient);
    g.fillRect(area);
    g.setColour(juce::Colour(0xff070b10));
    g.fillRect(area.getX(), area.getY(), area.getWidth(), 2);
    g.setColour(juce::Colour(0xff344452));
    g.fillRect(area.getX(), area.getY() + 2, area.getWidth(), 1);
    g.setColour(juce::Colour(0xff54d6dc).withAlpha(0.8f));
    g.fillRect(area.getX() + 8, area.getY() + 2,
               juce::jmax(0, juce::jmin(144, area.getWidth() - 16)), 1);
    const int instrumentFirst = getAudioTrackCount() + getMidiTrackCount();
    const int instrumentIndex = selectedTrack - instrumentFirst;
    if (instrumentIndex < 0 || instrumentIndex >= getInstrumentTrackCount()) return;
    if (auto* pattern = getInstrumentStepSequencer(instrumentIndex))
    {
                    auto panel = area;
                    const int dockInset = 8;
                    panel = panel.reduced(dockInset, 6);
                    const auto wideRow = [&](int index, int count, int y, int height)
                    {
                        const int gap = 5;
                        const int width = juce::jmax(1, (panel.getWidth() - (count - 1) * gap) / count);
                        return juce::Rectangle<int>(panel.getX() + index * (width + gap), y, width, height);
                    };
                    const auto stretchedControl = [&](int x, int w, int y, int h, int designWidth)
                    {
                        const int availableWidth = juce::jmax(1, panel.getWidth());
                        const int left = panel.getX() + (int) std::round((double)(x - panel.getX()) * availableWidth / designWidth);
                        const int right = panel.getX() + (int) std::round((double)(x - panel.getX() + w) * availableWidth / designWidth);
                        return juce::Rectangle<int>(left, y, juce::jmax(1, right - left), h);
                    };
                    const int titleWidth = 132;
                    auto onOff = juce::Rectangle<int>(panel.getX(), panel.getY(), titleWidth - 8 - 55, 30);
                    g.setColour(pattern->enabled ? juce::Colour(0xff23535d) : juce::Colour(0xff25323e));
                    g.fillRoundedRectangle(onOff.toFloat(), 7.0f);
                    g.setColour(juce::Colour(0xffe8eff7).withAlpha(pattern->enabled ? 0.12f : 0.045f));
                    g.fillRoundedRectangle(onOff.toFloat().withHeight(4.0f), 3.0f);
                    if (pattern->enabled)
                    {
                        g.setColour(juce::Colour(0x4454d6dc));
                        g.fillRoundedRectangle((float)onOff.getX() + 4.0f, (float)onOff.getY() + 3.0f,
                                               (float)juce::jmax(1, onOff.getWidth() - 8), 4.0f, 2.0f);
                    }
                    g.setColour(pattern->enabled ? juce::Colour(0xff54d6dc) : juce::Colour(0xff667d8d));
                    g.drawRoundedRectangle(onOff.toFloat(), 7.0f, 1.5f);
                    // Obsidian enabled-state text remains clear without overpowering the dock.
                    g.setColour(pattern->enabled ? juce::Colour(0xffeffcfd) : juce::Colour(0xffaabfcb));
                    g.setFont(juce::Font(10.0f, juce::Font::bold));
                    g.drawText(pattern->enabled ? "STEP SEQ ON" : "STEP SEQ OFF", onOff, juce::Justification::centred);
                    const auto seqPlay = juce::Rectangle<int>(onOff.getRight(), onOff.getY(), 55, onOff.getHeight());
                    const bool previewActive = audioEngine.isStepPreviewPlaying() && audioEngine.getStepPreviewTrack() == instrumentIndex;
                    g.setColour(previewActive ? juce::Colour(0xff23535d) : juce::Colour(0xff25323e));
                    g.fillRoundedRectangle(seqPlay.toFloat(), 7.0f);
                    g.setColour(juce::Colour(0xffe8eff7).withAlpha(previewActive ? 0.12f : 0.045f));
                    g.fillRoundedRectangle(seqPlay.toFloat().withHeight(4.0f), 3.0f);
                    g.setColour(previewActive ? juce::Colour(0xff54d6dc) : juce::Colour(0xff667d8d));
                    g.drawRoundedRectangle(seqPlay.toFloat(), 7.0f, 1.2f);
                    if (previewActive)
                    {
                        g.setColour(juce::Colour(0xff54d6dc).withAlpha(0.85f));
                        g.fillRoundedRectangle((float)seqPlay.getX() + 7.0f, (float)seqPlay.getBottom() - 4.0f,
                                               (float)juce::jmax(1, seqPlay.getWidth() - 14), 2.0f, 1.0f);
                    }
                    // Obsidian preview transport: active STOP is crisp, idle PLAY stays secondary.
                    g.setColour(previewActive ? juce::Colour(0xffeffcfd) : juce::Colour(0xffb9ccd6));
                    g.setFont(juce::Font(9.0f, juce::Font::bold));
                    g.drawText(previewActive ? "STOP" : "PLAY", seqPlay, juce::Justification::centred);
                    const int available = juce::jmax(0, panel.getWidth() - titleWidth);
                    const int stepW = juce::jmax(1, available / 16);
                    const int firstStep = juce::jlimit(0, 3, stepSequencerPage) * 16;
                    for (int s = 0; s < 16; ++s)
                    {
                        const int absoluteStep = firstStep + s;
                        auto pad = juce::Rectangle<int>(panel.getX() + titleWidth + (s * available) / 16, panel.getY(), juce::jmax(1, ((s + 1) * available) / 16 - (s * available) / 16 - 4), 30);
                        const bool availableStep = absoluteStep < pattern->stepCount;
                        const bool active = availableStep && pattern->steps[(size_t)absoluteStep].enabled;
                        // Obsidian step hierarchy: active, available and out-of-pattern cells.
                        g.setColour(active ? juce::Colour(0xff286674)
                                           : (availableStep ? juce::Colour(0xff304452)
                                                            : juce::Colour(0xff18222c)));
                        g.fillRoundedRectangle(pad.toFloat(), 6.0f);
                        if (availableStep)
                        {
                            g.setColour(juce::Colour(0xffe8eff7).withAlpha(active ? 0.12f : 0.04f));
                            g.fillRoundedRectangle(pad.toFloat().withHeight(3.0f), 2.0f);
                        }
                        if (active)
                        {
                            g.setColour(juce::Colour(0x4454d6dc));
                            g.fillRoundedRectangle((float)pad.getX() + 2.0f, (float)pad.getY() + 2.0f,
                                                   (float)juce::jmax(1, pad.getWidth() - 4), 5.0f, 2.0f);
                        }
                        if (s % 4 == 0)
                        {
                            g.setColour(active ? juce::Colour(0xffe8eff7) : juce::Colour(0xff667d8d));
                            g.fillRoundedRectangle((float)pad.getX() + 2.0f, (float)pad.getY() + 4.0f,
                                                   2.0f, (float)juce::jmax(1, pad.getHeight() - 8), 1.0f);
                        }
                        // Four-step accents follow the playable range; inactive cells recede.
                        g.setColour(!availableStep ? juce::Colour(0xff344452)
                                                   : (s % 4) == 0 ? juce::Colour(0xff91bbc3)
                                                                  : juce::Colour(0xff48606d));
                        g.drawRoundedRectangle(pad.toFloat(), 6.0f, 1.0f);
                        if (active)
                        {
                            g.setColour(juce::Colour(0xff54d6dc));
                            g.fillRoundedRectangle((float)pad.getX() + 5.0f, (float)pad.getBottom() - 4.0f,
                                                   (float)juce::jmax(1, pad.getWidth() - 10), 2.0f, 1.0f);
                        }
                        // Obsidian step numbers: notes, group downbeats and out-of-range steps.
                        g.setColour(!availableStep ? juce::Colour(0xff667d8d)
                                                   : active ? juce::Colour(0xffeffcfd)
                                                            : (s % 4) == 0 ? juce::Colour(0xffdceef1)
                                                                           : juce::Colour(0xffb9ccd6));
                        g.setFont(juce::Font(10.5f, juce::Font::bold));
                        g.drawText(juce::String(absoluteStep + 1), pad, juce::Justification::centred);
                    }
                    const int controlsY = panel.getY() + 42;
                    g.setColour(juce::Colour(0xff344452));
                    g.fillRect(panel.getX(), controlsY - 7, panel.getWidth(), 1);
                    g.setColour(juce::Colour(0xff54d6dc).withAlpha(0.35f));
                    g.fillRect(panel.getX(), controlsY - 7,
                               juce::jmax(0, juce::jmin(96, panel.getWidth())), 1);
                    g.setColour(juce::Colour(0xff344452).withAlpha(0.75f));
                    g.fillRect(panel.getX(), controlsY + 30, panel.getWidth(), 1);
                    g.setColour(juce::Colour(0xffc4adff).withAlpha(0.28f));
                    g.fillRect(panel.getX(), controlsY + 30,
                               juce::jmax(0, juce::jmin(72, panel.getWidth())), 1);
                    g.setColour(juce::Colour(0xff344452).withAlpha(0.75f));
                    g.fillRect(panel.getX(), controlsY + 66, panel.getWidth(), 1);
                    // Subtle graphite end-cap completes the editor group without competing with its violet header.
                    g.setColour(juce::Colour(0xff728394).withAlpha(0.22f));
                    g.fillRect(panel.getX(), controlsY + 66,
                               juce::jmax(0, juce::jmin(48, panel.getWidth())), 1);
                    auto drawControl = [&](juce::String text, int x, int w, bool active)
                    {
                        auto r = stretchedControl(x, w, controlsY, 26, 550 + 34 * juce::jmax(1, (pattern->stepCount + 15) / 16));
                        g.setColour(active ? juce::Colour(0xff23535d) : juce::Colour(0xff25323e)); g.fillRoundedRectangle(r.toFloat(), 6.0f);
                        g.setColour(juce::Colour(0xffe8eff7).withAlpha(active ? 0.11f : 0.045f));
                        g.fillRoundedRectangle(r.toFloat().withHeight(4.0f), 3.0f);
                        if (active) { g.setColour(juce::Colour(0xff54d6dc)); g.fillRoundedRectangle((float)r.getX() + 5.0f, (float)r.getBottom() - 3.0f, (float)juce::jmax(1, r.getWidth() - 10), 2.0f, 1.0f); }
                        // Obsidian rhythm controls: clear selection edge and restrained idle border.
                        g.setColour(active ? juce::Colour(0xff54d6dc) : juce::Colour(0xff48606d));
                        g.drawRoundedRectangle(r.toFloat(), 6.0f, active ? 1.5f : 1.0f);
                        // Obsidian rhythm controls: selected settings read brighter than idle ones.
                        g.setColour(active ? juce::Colour(0xffeffcfd) : juce::Colour(0xffb9ccd6));
                        g.setFont(juce::Font(9.0f, juce::Font::bold));
                        g.drawText(text, r, juce::Justification::centred);
                    };
                    int cx = panel.getX();
                    drawControl("16", cx, 30, pattern->stepCount == 16); cx += 34;
                    drawControl("32", cx, 30, pattern->stepCount == 32); cx += 34;
                    drawControl("64", cx, 30, pattern->stepCount == 64); cx += 42;
                    const std::int64_t rates[] = { MidiEngine::ticksPerQuarterNote, MidiEngine::ticksPerQuarterNote/2, MidiEngine::ticksPerQuarterNote/4, MidiEngine::ticksPerQuarterNote/8, MidiEngine::ticksPerQuarterNote/16 };
                    const char* rateNames[] = { "1/4", "1/8", "1/16", "1/32", "1/64" };
                    for (int r = 0; r < 5; ++r) { drawControl(rateNames[r], cx, 38, pattern->stepTicks == rates[r]); cx += 42; }
                    const char* modifierNames[] = { "STR", "TRI", "DOT" };
                    for (int m=0;m<3;++m) { drawControl(modifierNames[m], cx, 34, pattern->rateModifier == m); cx += 38; }
                    static constexpr const char* directionNames[] = { "FWD", "REV", "PING", "RND" };
                    drawControl(directionNames[juce::jlimit(0,3,(int)pattern->direction)], cx, 44, pattern->direction != LibertyStepSequencer::Direction::Forward); cx += 48;
                    drawControl("SW " + juce::String((int)std::round(pattern->swing * 100.0f)) + "%", cx, 62, pattern->swing > 0.0f); cx += 68;
                    const int pages = juce::jmax(1, (pattern->stepCount + 15) / 16);
                    for (int page = 0; page < pages; ++page) { drawControl("P" + juce::String(page + 1), cx, 30, stepSequencerPage == page); cx += 34; }
                    const int selectedStep = juce::jlimit(0, pattern->stepCount - 1, stepSequencerSelectedStep);
                    const auto& editStep = pattern->steps[(size_t)selectedStep];
                    const int editY = controlsY + 36;
                    auto drawEdit = [&](juce::String text, int x, int w, bool active)
                    {
                        auto r = stretchedControl(x, w, editY, 26, 780);
                        g.setColour(active ? juce::Colour(0xff594780) : juce::Colour(0xff273143)); g.fillRoundedRectangle(r.toFloat(), 6.0f);
                        g.setColour(juce::Colour(0xffe8eff7).withAlpha(active ? 0.12f : 0.045f));
                        g.fillRoundedRectangle(r.toFloat().withHeight(4.0f), 3.0f);
                        if (active) { g.setColour(juce::Colour(0xffc4adff)); g.fillRoundedRectangle((float)r.getX() + 5.0f, (float)r.getBottom() - 3.0f, (float)juce::jmax(1, r.getWidth() - 10), 2.0f, 1.0f); }
                        // Obsidian note editing: violet selection with a legible slate idle edge.
                        g.setColour(active ? juce::Colour(0xffc2aaff) : juce::Colour(0xff68788f));
                        g.drawRoundedRectangle(r.toFloat(), 6.0f, active ? 1.5f : 1.0f);
                        // Obsidian note editor: brighter violet-white focus, quieter idle labels.
                        g.setColour(active ? juce::Colour(0xfff8f0ff) : juce::Colour(0xffbfc9dc));
                        g.setFont(juce::Font(9.0f, juce::Font::bold));
                        g.drawText(text, r, juce::Justification::centred);
                    };
                    int ex = panel.getX();
                    drawEdit("STEP " + juce::String(selectedStep + 1), ex, 52, true); ex += 56;
                    drawEdit("NOTE " + juce::String((int)editStep.pitch + editStep.octave * 12), ex, 64, false); ex += 68;
                    drawEdit("VEL " + juce::String((int)editStep.velocity), ex, 54, false); ex += 58;
                    drawEdit("GATE " + juce::String((int)std::round(editStep.gate * 100.0f)) + "%", ex, 66, false); ex += 70;
                    drawEdit("PROB " + juce::String((int)editStep.probability) + "%", ex, 66, false); ex += 70;
                    drawEdit("RATCH x" + juce::String((int)editStep.ratchet), ex, 62, editStep.ratchet > 1); ex += 66;
                    drawEdit("ACC", ex, 38, editStep.accent); ex += 42;
                    drawEdit("OCT " + juce::String(editStep.octave), ex, 48, editStep.octave != 0); ex += 52;
                    drawEdit("TIE", ex, 38, editStep.tie); ex += 42;
                    drawEdit("CH " + juce::String((int)editStep.channel), ex, 42, editStep.channel != 1); ex += 46;
                    drawEdit("MICRO " + juce::String((int)std::round(editStep.microTiming * 100.0f)) + "%", ex, 70, editStep.microTiming != 0.0f); ex += 74;
                    static constexpr const char* chordNames[]={"OFF","MAJ","MIN","POWER","7TH"};
                    drawEdit("CHORD " + juce::String(chordNames[juce::jlimit(0,4,(int)editStep.chord)]), ex, 76, editStep.chord != LibertyStepSequencer::Chord::Off);
                    const int actionY = editY + 36;
                    auto drawAction = [&](juce::String text, int x, int w)
                    {
                        auto r = stretchedControl(x, w, actionY, 26, 1260);
                        g.setColour(juce::Colour(0xff25323e)); g.fillRoundedRectangle(r.toFloat(), 6.0f);
                        g.setColour(juce::Colour(0xff3b5362));
                        g.fillRoundedRectangle((float)r.getX() + 2.0f, (float)r.getY() + 2.0f,
                                               (float)juce::jmax(1, r.getWidth() - 4), 2.0f, 1.0f);
                        // Obsidian action row: readable graphite edges without a selection accent.
                        g.setColour(juce::Colour(0xff48606d)); g.drawRoundedRectangle(r.toFloat(), 6.0f, 1.0f);
                        g.setColour(juce::Colour(0xff172433));
                        g.fillRoundedRectangle((float)r.getX() + 6.0f, (float)r.getBottom() - 3.0f,
                                               (float)juce::jmax(1, r.getWidth() - 12), 1.0f, 0.5f);
                        g.setColour(juce::Colour(0xffc2d8e2));
                        g.setFont(juce::Font(9.0f, juce::Font::bold));
                        g.drawText(text, r, juce::Justification::centred);
                    };
                    int ax = panel.getX();
                    drawAction("CLEAR", ax, 48); ax += 52;
                    drawAction("RANDOM", ax, 58); ax += 62;
                    drawAction("REVERSE", ax, 62); ax += 66;
                    drawAction("ROTATE", ax, 56); ax += 60;
                    drawAction("DUPLICATE", ax, 72); ax += 80;
                    drawAction("SEND MIDI", ax, 52); ax += 56;
                    drawAction("DRAG CLIP", ax, 58); ax += 62;
                    static constexpr const char* rootNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
                    static constexpr const char* scaleNames[] = {"OFF","MAJOR","MINOR","PENTA"};
                    drawAction("ROOT " + juce::String(rootNames[juce::jlimit(0,11,(int)pattern->root)]), ax, 58); ax += 62;
                    drawAction("SCALE " + juce::String(scaleNames[juce::jlimit(0,3,(int)pattern->scale)]), ax, 82); ax += 86;
                    drawAction("TRANS " + juce::String(pattern->transpose), ax, 66); ax += 70;
                    drawAction("OCT " + juce::String(pattern->octaveShift), ax, 52); ax += 56;
                    drawAction("HUM " + juce::String((int)std::round(pattern->humanize * 100.0f)) + "%", ax, 60); ax += 64;
                    
                    drawAction("EUC " + juce::String(pattern->euclideanPulses) + "/" + juce::String(pattern->stepCount), ax, 68); ax += 72;
                    drawAction("EUC ROT " + juce::String(pattern->euclideanRotation), ax, 70); ax += 74;
                    drawAction("CYCLE " + juce::String(pattern->cycleSteps), ax, 66); ax += 70;
                    drawAction("VL " + juce::String(pattern->velocityLaneSteps), ax, 42); ax += 46;
                    drawAction("GL " + juce::String(pattern->gateLaneSteps), ax, 42); ax += 46;
                    drawAction("PL " + juce::String(pattern->probabilityLaneSteps), ax, 42); ax += 46;
                    drawAction("RL " + juce::String(pattern->ratchetLaneSteps), ax, 42);
                    ax += 46;
                    // Dedicated instrument-lane clip creation, separate from MIDI export.
                    const auto createClipButton = juce::Rectangle<int>(panel.getX(), actionY + 30, 156, 23);
                    g.setColour(juce::Colour(0xff23535d));
                    g.fillRoundedRectangle(createClipButton.toFloat(), 5.0f);
                    g.setColour(juce::Colour(0xffe8eff7).withAlpha(0.12f));
                    g.fillRoundedRectangle(createClipButton.toFloat().withHeight(4.0f), 3.0f);
                    g.setColour(juce::Colour(0xff54d6dc));
                    g.drawRoundedRectangle(createClipButton.toFloat(), 5.0f, 1.2f);
                    g.fillRoundedRectangle((float)createClipButton.getX() + 8.0f,
                                           (float)createClipButton.getBottom() - 3.0f,
                                           (float)juce::jmax(1, createClipButton.getWidth() - 16), 2.0f, 1.0f);
                    g.setFont(juce::Font(10.0f, juce::Font::bold));
                    // Obsidian clip creation follows the MIDI clip action hierarchy.
                    g.setColour(juce::Colour(0xffeffcfd));
                    g.drawText("CREATE PATTERN CLIP", createClipButton, juce::Justification::centred);
                    if ((size_t)instrumentIndex < instrumentStepSequencers.size())
                    {
                        const auto activePattern = instrumentStepSequencers[(size_t)instrumentIndex].activePattern;
                        for (int bankIndex = 0; bankIndex < 8; ++bankIndex)
                        {
                            const auto patRect = wideRow(bankIndex, 8, panel.getBottom() - 30, 28);
                            ax = patRect.getX();
                            const bool selectedPattern = bankIndex == activePattern;
                            g.setColour(selectedPattern ? juce::Colour(0xff23535d) : juce::Colour(0xff25323e));
                            g.fillRoundedRectangle(patRect.toFloat(), 6.0f);
                            if (selectedPattern)
                            {
                                g.setColour(juce::Colour(0x4454d6dc));
                                g.fillRoundedRectangle((float)patRect.getX() + 3.0f, (float)patRect.getY() + 2.0f,
                                                       (float)juce::jmax(1, patRect.getWidth() - 6), 5.0f, 2.0f);
                            }
                            // Obsidian pattern banks match the MIDI P1-P8 focus hierarchy.
                            g.setColour(selectedPattern ? juce::Colour(0xff54d6dc) : juce::Colour(0xff48606d));
                            g.drawRoundedRectangle(patRect.toFloat(), 6.0f, selectedPattern ? 1.5f : 1.0f);
                            g.setColour(selectedPattern ? juce::Colour(0xffeffcfd) : juce::Colour(0xffb9ccd6));
                            g.setFont(juce::Font(11.0f, juce::Font::bold));
                            g.drawText("PAT " + juce::String(bankIndex + 1), patRect, juce::Justification::centred);
                            if (bankIndex == activePattern)
                            {
                                auto active = patRect;
                                g.setColour(juce::Colour(0xffb6f2f2));
                                g.fillRoundedRectangle((float)active.getX() + 9.0f, (float)active.getBottom() - 4.0f,
                                                       (float)juce::jmax(1, active.getWidth() - 18), 2.0f, 1.0f);
                            }
                            ax += 48;
                        }
                    }
                
    }
}
void MainComponent::drawMixer(juce::Graphics& g, juce::Rectangle<int> area)
{
    // Obsidian mixer surface matches the arranger without altering strip geometry.
    g.setColour(juce::Colour(0xff101821)); g.fillRect(area);

    const int audioTracks = getAudioTrackCount();
    const int instrumentTracks = getInstrumentTrackCount();
    const int channelCount = audioTracks + instrumentTracks;

    for (int channel = 0; channel < channelCount + 1; ++channel)
    {
        const bool master = channel == channelCount;
        const bool isAudio = channel < audioTracks;
        const bool isInstrument = !master && !isAudio;
        const int sourceIndex = isAudio ? channel : (channel - audioTracks);

        auto c = juce::Rectangle<int>(220 + channel * 125, area.getY() + 12, 116, area.getHeight() - 22);
        // Group dividers occupy the existing inter-strip gutter only.
        if ((isInstrument && sourceIndex == 0) || master)
        {
            g.setColour(master ? juce::Colour(0xff54d6dc).withAlpha(0.48f)
                               : juce::Colour(0xff427887).withAlpha(0.60f));
            const int separatorX = c.getX() - 5;
            g.drawVerticalLine(separatorX, (float)c.getY() + 9.0f, (float)c.getBottom() - 9.0f);
        }
        g.setColour(master ? juce::Colour(0xff26313d)
                           : (isInstrument ? juce::Colour(0xff1d303b) : juce::Colour(0xff1c2732)));
        g.fillRoundedRectangle(c.toFloat(), 5.0f);
        g.setColour(master ? juce::Colour(0xff607989)
                           : (isInstrument ? juce::Colour(0xff427887)
                                           : juce::Colour(0xff405361)));
        g.drawRoundedRectangle(c.toFloat(), 5.0f, 1.0f);
        // A restrained turquoise header accent distinguishes the master bus.
        if (master)
        {
            g.setColour(juce::Colour(0xff54d6dc));
            g.fillRoundedRectangle(juce::Rectangle<float>((float)c.getX() + 12.0f, (float)c.getY() + 3.0f,
                                                         (float)c.getWidth() - 24.0f, 2.0f), 1.0f);
        }

        const juce::String channelName = master ? "MASTER"
                                                : (isAudio ? "Audio " + juce::String(sourceIndex + 1)
                                                           : "Instrument " + juce::String(sourceIndex + 1));
        // Mixer strip titles follow the Obsidian channel hierarchy.
        g.setColour(master ? juce::Colour(0xfff0fafb)
                           : (isInstrument ? juce::Colour(0xffb8e8ec)
                                           : juce::Colour(0xffd1e0e9)));
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText(channelName, c.getX(), c.getY() + 8, c.getWidth(), 20, juce::Justification::centred);

        // Audio channels keep their existing live mute/solo/gain/pan controls.
        // Instrument strips are shown in the Arrange mini mixer now; their
        // dedicated audio controls can be wired when per-instrument mixer state
        // is introduced, without incorrectly controlling an audio track.
        if (isAudio)
        {
            const bool muted = audioEngine.isTrackMuted(sourceIndex);
            const bool solo = audioEngine.isTrackSolo(sourceIndex);
            auto mute = juce::Rectangle<int>(c.getX() + 8, c.getY() + 32, 44, 20);
            auto soloButton = juce::Rectangle<int>(c.getX() + 58, c.getY() + 32, 44, 20);
            g.setColour(muted ? juce::Colour(0xffb15358) : juce::Colour(0xff253540)); g.fillRoundedRectangle(mute.toFloat(), 4.0f);
            g.setColour(solo ? juce::Colour(0xffa18a3e) : juce::Colour(0xff253540)); g.fillRoundedRectangle(soloButton.toFloat(), 4.0f);
            g.setColour(muted ? juce::Colour(0xffd57c80) : juce::Colour(0xff49606d));
            g.drawRoundedRectangle(mute.toFloat(), 4.0f, 1.0f);
            g.setColour(solo ? juce::Colour(0xffcbb35e) : juce::Colour(0xff49606d));
            g.drawRoundedRectangle(soloButton.toFloat(), 4.0f, 1.0f);
            g.setFont(juce::Font(9.0f, juce::Font::bold));
            g.setColour(muted ? juce::Colour(0xfffdf4f4) : juce::Colour(0xffb9ceda));
            g.drawText("M", mute, juce::Justification::centred);
            g.setColour(solo ? juce::Colour(0xfffff7db) : juce::Colour(0xffb9ceda));
            g.drawText("S", soloButton, juce::Justification::centred);
        }
        else if (isInstrument)
        {
            auto mute = juce::Rectangle<int>(c.getX() + 8, c.getY() + 32, 44, 20);
            auto soloButton = juce::Rectangle<int>(c.getX() + 58, c.getY() + 32, 44, 20);
            const bool muted = audioEngine.isInstrumentTrackMuted(sourceIndex);
            const bool solo = audioEngine.isInstrumentTrackSolo(sourceIndex);
            g.setColour(muted ? juce::Colour(0xffb15358) : juce::Colour(0xff253540)); g.fillRoundedRectangle(mute.toFloat(), 4.0f);
            g.setColour(solo ? juce::Colour(0xffa18a3e) : juce::Colour(0xff253540)); g.fillRoundedRectangle(soloButton.toFloat(), 4.0f);
            g.setColour(muted ? juce::Colour(0xffd57c80) : juce::Colour(0xff49606d));
            g.drawRoundedRectangle(mute.toFloat(), 4.0f, 1.0f);
            g.setColour(solo ? juce::Colour(0xffcbb35e) : juce::Colour(0xff49606d));
            g.drawRoundedRectangle(soloButton.toFloat(), 4.0f, 1.0f);
            g.setFont(juce::Font(9.0f, juce::Font::bold));
            g.setColour(muted ? juce::Colour(0xfffdf4f4) : juce::Colour(0xffb9ceda));
            g.drawText("M", mute, juce::Justification::centred);
            g.setColour(solo ? juce::Colour(0xfffff7db) : juce::Colour(0xffb9ceda));
            g.drawText("S", soloButton, juce::Justification::centred);
        }

        const int faderTop = c.getY() + 58, faderBottom = c.getBottom() - 45;
        auto fader = juce::Rectangle<float>((float)c.getCentreX() - 7.0f, (float)faderTop, 14.0f, (float)(faderBottom - faderTop));
        // Deep graphite fader rail complements the Obsidian mixer strips.
        g.setColour(juce::Colour(0xff0c1720)); g.fillRoundedRectangle(fader, 3.0f);

        const float gain = master ? audioEngine.getMasterGain() : (isAudio ? audioEngine.getTrackGain(sourceIndex) : audioEngine.getInstrumentTrackGain(sourceIndex));
        const auto normalized = juce::jlimit(0.0f, 1.0f, gain * 0.5f);
        const auto knobY = fader.getBottom() - normalized * fader.getHeight();
        // Metallic Obsidian fader cap; preserve the existing handle geometry.
        g.setColour(master ? juce::Colour(0xffc6e7ed) : juce::Colour(0xffb8c9d3));
        g.fillRoundedRectangle(fader.getX() - 2.0f, knobY - 6.0f, fader.getWidth() + 4.0f, 12.0f, 3.0f);
        g.setColour(master ? juce::Colour(0xfff0fbfd) : juce::Colour(0xffe2edf2));
        g.drawHorizontalLine(juce::roundToInt(knobY - 2.0f), fader.getX() + 1.0f, fader.getRight() - 1.0f);

        const auto db = 20.0f * std::log10(juce::jmax(0.000001f, gain));
        // Obsidian mixer readouts: prioritize gain, keep pan as secondary metadata.
        g.setFont(juce::Font(10.0f));
        g.setColour(master ? juce::Colour(0xffd9f3f5) : juce::Colour(0xffc2d8e2));
        g.drawText(db < -59.9f ? "-inf dB" : juce::String(db, 1) + " dB", c.getX(), c.getBottom() - 38, c.getWidth(), 16, juce::Justification::centred);
        g.setColour(master ? juce::Colour(0xff91bbc3) : juce::Colour(0xff829eae));
        g.drawText(master ? "MASTER" : (isAudio ? "PAN " + juce::String(audioEngine.getTrackPan(sourceIndex), 2) : "PAN " + juce::String(audioEngine.getInstrumentTrackPan(sourceIndex), 2)),
                   c.getX(), c.getBottom() - 22, c.getWidth(), 16, juce::Justification::centred);
    }
}

void MainComponent::resized() { resizeLibertyMixConsole(this); resizeLibertyDynamicTrackController(this); resizeLibertyGridSnapController(this); resizeLibertyBrowserResizeController(this); resizeLibertyMultiMidiClipController(this); repaint(); }

void MainComponent::timerCallback()
{
    // Attach the real REC controller directly from the live MainComponent.
    // Do not depend on desktop window discovery or an extra drawn placeholder.
    ensureLibertyAudioRecordingController(this);
    const auto newPlayhead = audioEngine.getCurrentTimeSeconds();
    const bool newPlaying = audioEngine.isPlaying();
    const bool changed = newPlaying != isPlaying || (newPlaying && std::abs(newPlayhead - playheadSeconds) > 0.0001);
    playheadSeconds = newPlayhead;
    isPlaying = newPlaying;
    if (changed)
        repaint(0, transportHeight, getWidth(), juce::jmax(0, getHeight() - transportHeight));
}

void showLibertyAIMusicPanel(MainComponent&);
void showLibertyAIComposer2(MainComponent&);
void showLibertyAIArranger(MainComponent&);
void showLibertyAISongBrain(MainComponent&);

void MainComponent::performMainMenuAction(int commandId)
{
    switch (commandId)
    {
        case 1001: newProject(); break;
        case 1002: openProject(); break;
        case 1003: saveProject(); break;
        case 1004: saveProjectAs(); break;
        case 4001:
            isPlaying = !audioEngine.isPlaying();
            audioEngine.setPlaying(isPlaying);
            repaint();
            break;
        case 4002:
            isPlaying = false;
            audioEngine.setPlaying(false);
            repaint();
            break;
        case 6501: showLibertyAIMusicPanel(*this); break;
        case 6502: showLibertyAIMusicPanel(*this); showLibertyAIComposer2(*this); break;
        case 6503: showLibertyAIMusicPanel(*this); showLibertyAIArranger(*this); break;
        case 6504: showLibertyAISongBrain(*this); break;
        case 7001: openAudioSettings(); break;
        case 8001:
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                "Liberty",
                "Liberty DAW - BSM Studio Music");
            break;
        default: break;
    }
}

void MainComponent::openAudioSettings()
{
    if (audioSettingsWindow == nullptr) audioSettingsWindow = std::make_unique<AudioSettingsWindow>(audioEngine);
    audioSettingsWindow->setVisible(true);
    audioSettingsWindow->toFront(true);
}

void MainComponent::rebuildWaveformCache(int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= audioEngine.getAudioTrackCount()) return;
    waveformMin[(size_t)trackIndex].clear(); waveformMax[(size_t)trackIndex].clear();
    const auto bufferSnapshot = audioEngine.getAudioBufferSnapshot(trackIndex);
    const auto* buffer = bufferSnapshot.get();
    if (buffer == nullptr || buffer->getNumSamples() <= 0 || buffer->getNumChannels() <= 0) return;
    constexpr int points = 1200;
    auto& minCache = waveformMin[(size_t)trackIndex]; auto& maxCache = waveformMax[(size_t)trackIndex];
    minCache.resize(points, 0.0f); maxCache.resize(points, 0.0f);
    const auto totalSamples = buffer->getNumSamples(); const auto channels = buffer->getNumChannels();
    for (int point = 0; point < points; ++point)
    {
        const auto start = static_cast<int>((static_cast<std::int64_t>(point) * totalSamples) / points);
        const auto end = static_cast<int>((static_cast<std::int64_t>(point + 1) * totalSamples) / points);
        const auto safeEnd = juce::jmax(start + 1, end); float minValue = 0.0f; float maxValue = 0.0f;
        for (int sample = start; sample < safeEnd && sample < totalSamples; ++sample) for (int channel = 0; channel < channels; ++channel)
        { const auto value = buffer->getSample(channel, sample); minValue = std::min(minValue, value); maxValue = std::max(maxValue, value); }
        minCache[(size_t)point] = juce::jlimit(-1.0f, 1.0f, minValue); maxCache[(size_t)point] = juce::jlimit(-1.0f, 1.0f, maxValue);
    }
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& path : files)
        if (isSupportedAudioFile(path))
            return true;
    return false;
}

void MainComponent::filesDropped(const juce::StringArray& files, int x, int y)
{
    const int trackToLoad = getAudioTrackAtPosition({ x, y });
    if (trackToLoad < 0)
        return;

    juce::File file;
    for (const auto& path : files)
    {
        if (isSupportedAudioFile(path))
        {
            file = juce::File(path);
            break;
        }
    }

    if (!file.existsAsFile())
        return;

    juce::String error;
    if (!audioEngine.loadAudioFileIntoTrack(trackToLoad, file, error))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               "Liberty - Audio Import",
                                               error,
                                               "OK");
        return;
    }

    constexpr int headerW = 210;
    const double pixelsPerSecond = getLibertyTimelinePixelsPerSecond();
    const double dropStartSeconds = x >= headerW ? juce::jmax(0.0, (x - headerW) / pixelsPerSecond) : 0.0;

    trackSourceFiles[(size_t)trackToLoad] = file;
    pendingAudioFileNames[(size_t)trackToLoad].clear();
    pendingAudioLengths[(size_t)trackToLoad] = 0.0;
    pendingAudioStartSeconds[(size_t)trackToLoad] = 0.0;
    pendingAudioWarpStates[(size_t)trackToLoad] = {};
    audioEngine.setTrackStartSeconds(trackToLoad, dropStartSeconds);
    audioEngine.setPlaying(false);
    selectedTrack = trackToLoad;
    isPlaying = false;
    rebuildWaveformCache(trackToLoad);
    repaint();
}

int MainComponent::getAudioTrackAtPosition(juce::Point<int> position) const
{
    constexpr int rulerH = trackRulerHeight;
    const int rowH = getLibertyTrackRowHeight();
    const int y = position.y - getArrangeTop(); if (y < 0 || position.y >= getMixerTop()) return -1;
    const int logicalRow = getTrackScrollRows() + y / rowH;
    return logicalRow >= 0 && logicalRow < audioEngine.getAudioTrackCount() ? logicalRow : -1;
}

bool MainComponent::isPointInsideAudioClip(int trackIndex, juce::Point<int> position) const
{
    if (trackIndex < 0 || trackIndex >= audioEngine.getAudioTrackCount() || !audioEngine.hasAudioFile(trackIndex)) return false;
    constexpr int headerW = trackHeaderWidth, rulerH = trackRulerHeight;
    const int rowH = getLibertyTrackRowHeight();
    const double pixelsPerSecond = getLibertyTimelinePixelsPerSecond();
    const int rowY = getArrangeTop() + (trackIndex - getTrackScrollRows()) * rowH;
    const int x = headerW + static_cast<int>(std::round(audioEngine.getTrackStartSeconds(trackIndex) * pixelsPerSecond));
    const int width = juce::jmax(1, static_cast<int>(std::round(audioEngine.getAudioFileLengthSeconds(trackIndex) * pixelsPerSecond)));
    return juce::Rectangle<int>(x, rowY + 4, width, rowH - 8).contains(position);
}

bool MainComponent::handleMixerMouse(const juce::MouseEvent& event)
{
    const int mixerTop = getMixerTop(); if (event.position.y < mixerTop) return false;
    const int audioTracks = getAudioTrackCount();
    const int instrumentTracks = getInstrumentTrackCount();
    const int channelCount = audioTracks + instrumentTracks;

    for (int channel = 0; channel < channelCount + 1; ++channel)
    {
        auto c = juce::Rectangle<int>(220 + channel * 125, mixerTop + 12, 116, 188);
        if (!c.contains(event.getPosition())) continue;

        const bool master = channel == channelCount;
        const bool isAudio = channel < audioTracks;

        if (!master)
        {
            auto mute = juce::Rectangle<int>(c.getX() + 8, c.getY() + 32, 44, 20);
            auto solo = juce::Rectangle<int>(c.getX() + 58, c.getY() + 32, 44, 20);
            const int sourceIndex = isAudio ? channel : channel - audioTracks;
            if (mute.contains(event.getPosition())) { if (isAudio) audioEngine.setTrackMuted(sourceIndex,!audioEngine.isTrackMuted(sourceIndex)); else audioEngine.setInstrumentTrackMuted(sourceIndex,!audioEngine.isInstrumentTrackMuted(sourceIndex)); repaint(); return true; }
            if (solo.contains(event.getPosition())) { if (isAudio) audioEngine.setTrackSolo(sourceIndex,!audioEngine.isTrackSolo(sourceIndex)); else audioEngine.setInstrumentTrackSolo(sourceIndex,!audioEngine.isInstrumentTrackSolo(sourceIndex)); repaint(); return true; }
        }

        const int faderTop = c.getY() + 58, faderBottom = c.getBottom() - 45;
        if (event.position.y >= faderTop && event.position.y <= faderBottom)
        {
            const float n = juce::jlimit(0.0f, 1.0f, (float)(faderBottom - event.position.y) / (float)juce::jmax(1, faderBottom - faderTop));
            const float gain = n * 2.0f;
            if (master) audioEngine.setMasterGain(gain); else if (isAudio) audioEngine.setTrackGain(channel, gain); else audioEngine.setInstrumentTrackGain(channel - audioTracks, gain);
            repaint();
            return true;
        }

        if (!master && event.position.y >= c.getBottom() - 28)
        {
            const float pan = juce::jlimit(-1.0f, 1.0f, ((float)event.position.x - (float)c.getCentreX()) / 45.0f);
            if (isAudio) audioEngine.setTrackPan(channel, pan); else audioEngine.setInstrumentTrackPan(channel - audioTracks, pan);
            repaint();
            return true;
        }

        // The click belongs to a visible instrument strip, but it has no
        // per-instrument mixer state yet. Consume it so it cannot leak through
        // to arranger hit-testing underneath the fixed mini mixer.
        return true;
    }
    return false;
}

void MainComponent::mouseMove(const juce::MouseEvent& event)
{
    const auto p = event.getPosition();
    bool overPatternClip = false;
    if (p.x >= trackHeaderWidth && p.y >= getArrangeTop() && p.y < getMixerTop())
    {
        const int row = getTrackScrollRows()
                      + (p.y - getArrangeTop()) / juce::jmax(1, getLibertyTrackRowHeight());
        const int instrumentIndex = row - getAudioTrackCount() - getMidiTrackCount();
        if (instrumentIndex >= 0 && instrumentIndex < (int) instrumentStepSequencers.size())
        {
            const double pixelsPerSecond = getLibertyTimelinePixelsPerSecond();
            for (const auto& clip : instrumentStepSequencers[(size_t) instrumentIndex].timelineClips)
            {
                const int left = trackHeaderWidth + (int) std::round(clip.startSeconds * pixelsPerSecond);
                const int width = juce::jmax(1, (int) std::round(clip.lengthSeconds * pixelsPerSecond));
                if (clip.lengthSeconds > 0.0 && p.x >= left && p.x < left + width)
                {
                    overPatternClip = true;
                    break;
                }
            }
        }
    }
    setMouseCursor(overPatternClip ? juce::MouseCursor::DraggingHandCursor
                                   : juce::MouseCursor::NormalCursor);
}

void MainComponent::mouseExit(const juce::MouseEvent&)
{
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void MainComponent::mouseDoubleClick(const juce::MouseEvent& event)
{
    const auto p = event.getPosition();    if (p.x < trackHeaderWidth || p.y < getArrangeTop() || p.y >= getMixerTop()) return;
    const int rowH = getLibertyTrackRowHeight();
    const int logicalRow = getTrackScrollRows() + (p.y - getArrangeTop()) / juce::jmax(1, rowH);
    const int instrumentIndex = logicalRow - getAudioTrackCount() - getMidiTrackCount();
    if (instrumentIndex < 0 || instrumentIndex >= (int) instrumentStepSequencers.size()) return;
    auto& clips = instrumentStepSequencers[(size_t) instrumentIndex].timelineClips;
    const double pixelsPerSecond = getLibertyTimelinePixelsPerSecond();
    for (int i = (int) clips.size() - 1; i >= 0; --i)
    {
        auto& clip = clips[(size_t) i];
        const int left = trackHeaderWidth + (int) std::round(clip.startSeconds * pixelsPerSecond);
        const int width = juce::jmax(1, (int) std::round(clip.lengthSeconds * pixelsPerSecond));
        if (p.x < left || p.x >= left + width) continue;
        auto* renameDialog = new juce::AlertWindow("Rename Pattern Clip", "Enter a name for this clip:",
                                                     juce::AlertWindow::NoIcon);
        renameDialog->addTextEditor("name", clip.name, "Clip name:");
        renameDialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        renameDialog->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
        juce::Component::SafePointer<MainComponent> safeThis(this);
        renameDialog->enterModalState(true, juce::ModalCallbackFunction::create(
            [safeThis, instrumentIndex, i, renameDialog](int result)
            {
                if (result != 1 || safeThis == nullptr) return;
                if (instrumentIndex < 0 || instrumentIndex >= (int) safeThis->instrumentStepSequencers.size()) return;
                auto& savedClips = safeThis->instrumentStepSequencers[(size_t) instrumentIndex].timelineClips;
                if (i < 0 || i >= (int) savedClips.size()) return;
                const auto name = renameDialog->getTextEditorContents("name").trim();
                if (name.isNotEmpty())
                {
                    savedClips[(size_t) i].name = name.substring(0, 64);
                    safeThis->repaint();
                }
            }), true);
        return;
    }
}

void MainComponent::mouseDown(const juce::MouseEvent& event)
{
    const auto p = event.getPosition();

    // Pattern clips take priority over timeline creation and editor shortcuts.
    // A second click is handled by mouseDoubleClick (rename), never by clip creation.
    if (p.x >= trackHeaderWidth && p.y >= getArrangeTop() && p.y < getMixerTop())
    {
        const int row = getTrackScrollRows()
                      + (p.y - getArrangeTop()) / juce::jmax(1, getLibertyTrackRowHeight());
        const int instrumentFirst = getAudioTrackCount() + getMidiTrackCount();
        const int instrumentIndex = row - instrumentFirst;
        if (instrumentIndex >= 0 && instrumentIndex < (int) instrumentStepSequencers.size())
        {
            auto& clips = instrumentStepSequencers[(size_t) instrumentIndex].timelineClips;
            const double pixelsPerSecond = getLibertyTimelinePixelsPerSecond();
            for (int clipIndex = (int) clips.size() - 1; clipIndex >= 0; --clipIndex)
            {
                const auto& clip = clips[(size_t) clipIndex];
                const int left = trackHeaderWidth + (int) std::round(clip.startSeconds * pixelsPerSecond);
                const int width = juce::jmax(1, (int) std::round(clip.lengthSeconds * pixelsPerSecond));
                if (p.x < left || p.x >= left + width) continue;
                selectedTrack = row;
                if (event.getNumberOfClicks() == 1)
                {
                    draggedPatternInstrument = instrumentIndex;
                    draggedPatternClip = clipIndex;
                    dragStartMouseX = (float) p.x;
                    dragStartSeconds = clip.startSeconds;
                }
                else
                {
                    draggedPatternInstrument = -1;
                    draggedPatternClip = -1;
                }
                repaint();
                return;
            }
        }
    }
    if (dockStepSequencerMode && selectedTrack >= getAudioTrackCount() + getMidiTrackCount()
        && selectedTrack < getAudioTrackCount() + getMidiTrackCount() + getInstrumentTrackCount()
        && juce::Rectangle<int>(416, getMixerTop() + 4, 106, 28).contains(p))
    {
        showFloatingStepSequencer();
        return;
    }
    if (juce::Rectangle<int>(260, getMixerTop() + 4, 148, 28).contains(p))
    {
        const int firstInstrument = getAudioTrackCount() + getMidiTrackCount();
        if (selectedTrack >= getAudioTrackCount()
            && selectedTrack < firstInstrument + getInstrumentTrackCount())
        {
            showFloatingDrumSamplerWindow(audioEngine);
            return;
        }
    }
    const int dockInstrumentFirst = getAudioTrackCount() + getMidiTrackCount();
    const bool selectedMidi = selectedTrack >= getAudioTrackCount()
                           && selectedTrack < dockInstrumentFirst;
    const bool selectedInstrument = selectedTrack >= dockInstrumentFirst
                                 && selectedTrack < dockInstrumentFirst + getInstrumentTrackCount();
    if ((selectedMidi || selectedInstrument) && p.y >= getMixerTop() + 4
        && p.y < getMixerTop() + 32)
    {
        const int tabX = 8;
        if (p.x >= tabX && p.x < tabX + 118)
        {
            dockStepSequencerMode = false;
            openLibertyMidiEditor(*this);
            repaint();
            return;
        }
        if (p.x >= tabX + 126 && p.x < tabX + 244)
        {
            // MIDI-track Pattern playback needs its own routing and persistence;
            // do not pretend the instrument sequencer edits the selected MIDI track.
            if (selectedInstrument || selectedMidi) dockStepSequencerMode = true;
            repaint();
            return;
        }
    }

    const int lowerDockInstrumentFirst = getAudioTrackCount() + getMidiTrackCount();
    const bool stepSequencerDockActive = selectedTrack >= lowerDockInstrumentFirst
                                      && selectedTrack < lowerDockInstrumentFirst + getInstrumentTrackCount();
    if (!stepSequencerDockActive && handleMixerMouse(event)) return;

    const auto rewindButton = juce::Rectangle<int>(215, 38, 56, 28);
    const auto previousButton = juce::Rectangle<int>(277, 38, 56, 28);
    const auto playButton = juce::Rectangle<int>(339, 38, 56, 28);
    const auto nextButton = juce::Rectangle<int>(401, 38, 56, 28);
    const auto forwardButton = juce::Rectangle<int>(463, 38, 56, 28);

    const double secondsPerBeat = 60.0 / juce::jmax(1.0, tempoBpm) * (4.0 / (double) juce::jmax(1, timeSignatureDenominator));
    const double secondsPerMeasure = secondsPerBeat * (double) juce::jmax(1, timeSignatureNumerator);

    if (rewindButton.contains(p))
    {
        audioEngine.resetTransport();
        audioEngine.setPlaying(false);
        playheadSeconds = 0.0;
        isPlaying = false;
        repaint();
        return;
    }

    if (previousButton.contains(p))
    {
        const double currentTime = juce::jmax(0.0, audioEngine.getCurrentTimeSeconds());
        const double newTime = juce::jmax(0.0, std::ceil((currentTime - 0.000001) / secondsPerMeasure - 1.0e-9) * secondsPerMeasure - secondsPerMeasure);
        audioEngine.setCurrentTimeSeconds(newTime);
        playheadSeconds = newTime;
        repaint();
        return;
    }

    if (playButton.contains(p))
    {
        isPlaying = !isPlaying;
        audioEngine.setPlaying(isPlaying);
        repaint();
        return;
    }

    if (nextButton.contains(p))
    {
        const double currentTime = juce::jmax(0.0, audioEngine.getCurrentTimeSeconds());
        const double nextMeasure = (std::floor(currentTime / secondsPerMeasure + 1.0e-9) + 1.0) * secondsPerMeasure;
        audioEngine.setCurrentTimeSeconds(nextMeasure);
        playheadSeconds = nextMeasure;
        repaint();
        return;
    }

    if (forwardButton.contains(p))
    {
        double projectEnd = 0.0;
        for (int i = 0; i < audioEngine.getAudioTrackCount(); ++i)
            if (audioEngine.hasAudioFile(i))
                projectEnd = juce::jmax(projectEnd, audioEngine.getTrackStartSeconds(i) + audioEngine.getAudioFileLengthSeconds(i));
        const double snappedEnd = std::ceil(projectEnd / secondsPerMeasure - 1.0e-9) * secondsPerMeasure;
        audioEngine.setCurrentTimeSeconds(snappedEnd);
        playheadSeconds = snappedEnd;
        audioEngine.setPlaying(false);
        isPlaying = false;
        repaint();
        return;
    }

    if (juce::Rectangle<int>(925, 10, 120, 24).contains(p)) { openAudioSettings(); return; }

    constexpr int headerW = 210, rulerH = 32;
    const int audioTrackCount = audioEngine.getAudioTrackCount();
    const int midiTrackCount = getMidiTrackCount();
    const int instrumentTrackCount = getInstrumentTrackCount();
    const int rowH = getLibertyTrackRowHeight();
    const double pixelsPerSecond = getLibertyTimelinePixelsPerSecond();
    if (p.y >= 76 && p.y < 76 + rulerH && p.x >= headerW)
    {
        const double rawTime = juce::jmax(0.0, (double)(p.x - headerW) / pixelsPerSecond);
        const double snappedTime = std::round(rawTime / secondsPerMeasure) * secondsPerMeasure;
        audioEngine.setCurrentTimeSeconds(snappedTime);
        playheadSeconds = snappedTime;
        repaint();
        return;
    }

    const bool instrumentDockSelected = selectedTrack >= getAudioTrackCount() + getMidiTrackCount()
        && selectedTrack < getAudioTrackCount() + getMidiTrackCount() + getInstrumentTrackCount();
    if (p.y >= getMixerTop() && dockStepSequencerMode && instrumentDockSelected)
    {
        handleStepSequencerClick(p, juce::Rectangle<int>(0, getMixerTop() + 36, getWidth(), mixerHeight - 36));
        return;
    }
    const bool midiDockSelected = selectedTrack >= getAudioTrackCount()
        && selectedTrack < getAudioTrackCount() + getMidiTrackCount();
    if (p.y >= getMixerTop() && dockStepSequencerMode && midiDockSelected)
    {
        clickMidiStepSequencer(p, juce::Rectangle<int>(0, getMixerTop() + 36, getWidth(), mixerHeight - 36));
        return;
    }

    const int track = getAudioTrackAtPosition(p);
    if (track >= 0)
    {
        if (selectedTrack != track) dockStepSequencerMode = false;
        selectedTrack = track;
        if (isPointInsideAudioClip(track, p))
        {
            const int tool = getLibertyActiveTool();
            const double clickTime = juce::jmax(0.0, (double)(p.x - headerW) / pixelsPerSecond);

            if (tool == 2) // SPLIT / COUPER
            {
                int newTrack = -1;
                for (int i = 0; i < getAudioTrackCount(); ++i)
                    if (i != track
                        && !audioEngine.hasAudioFile(i)
                        && trackSourceFiles[(size_t)i].getFullPathName().isEmpty())
                    {
                        newTrack = i;
                        break;
                    }
                if (newTrack < 0)
                    newTrack = addAudioTrack();

                juce::String error;
                if (audioEngine.splitAudioTrack(track, clickTime, newTrack, error))
                {
                    trackSourceFiles[(size_t)newTrack] = trackSourceFiles[(size_t)track];
                    pendingAudioFileNames[(size_t)newTrack].clear();
                    pendingAudioLengths[(size_t)newTrack] = 0.0;
                    pendingAudioStartSeconds[(size_t)newTrack] = 0.0;
                    pendingAudioWarpStates[(size_t)newTrack] = {};
                    rebuildWaveformCache(track);
                    rebuildWaveformCache(newTrack);
                    selectedTrack = newTrack;
                }
                else
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                           "Liberty - Split", error, "OK");
                repaint();
                return;
            }

            if (tool == 3) // ERASE / EFFACER
            {
                clearLibertyAudioClipResizeSource(audioEngine, track);
                audioEngine.clearAudioTrack(track);
                isPlaying = false;
                playheadSeconds = 0.0;
                trackSourceFiles[(size_t)track] = juce::File{};
                pendingAudioFileNames[(size_t)track].clear();
                pendingAudioLengths[(size_t)track] = 0.0;
                pendingAudioStartSeconds[(size_t)track] = 0.0;
                pendingAudioWarpStates[(size_t)track] = {};
                waveformMin[(size_t)track].clear();
                waveformMax[(size_t)track].clear();
                draggingClip = false;
                draggedTrack = -1;
                repaint();
                return;
            }

            if (tool == 8) // MUTE / MUET
            {
                audioEngine.setTrackMuted(track, !audioEngine.isTrackMuted(track));
                repaint();
                return;
            }

            // RESIZE and STRETCH are handled by the dedicated edge handles.
            if (tool == 4 || tool == 5)
            {
                draggingClip = false;
                draggedTrack = -1;
                repaint();
                return;
            }

            // SELECT keeps the validated clip move workflow unchanged.
            draggingClip = true;
            draggedTrack = track;
            dragStartMouseX = (float)p.x;
            dragStartSeconds = audioEngine.getTrackStartSeconds(track);
        }
        repaint();
        return;
    }

    // Restore direct track-header M/S and gain hit targets. These match the
    // painted controls above; they must precede generic track selection.
    if (p.x < 210 && p.y >= getArrangeTop() && p.y < getMixerTop())
    {
        const int logicalRow = getTrackScrollRows() + (p.y - getArrangeTop()) / rowH;
        const int instrumentFirst = audioTrackCount + midiTrackCount;
        if (logicalRow >= 0 && (logicalRow < audioTrackCount || logicalRow >= instrumentFirst))
        {
            const bool audioLane = logicalRow < audioTrackCount;
            const int channel = audioLane ? logicalRow : logicalRow - instrumentFirst;
            if (channel >= 0 && channel < (audioLane ? audioTrackCount : instrumentTrackCount))
            {
                const int rowY = getArrangeTop() + (logicalRow - getTrackScrollRows()) * rowH;
                const int controlY = rowY + 36;
                if (!audioLane && juce::Rectangle<int>(108, rowY + 7, 46, 24).contains(p))
                {
                    armedInstrumentTrack = (armedInstrumentTrack == channel ? -1 : channel);
                    selectedTrack = logicalRow;
                    repaint();
                    return;
                }
                if (!audioLane && juce::Rectangle<int>(158, rowY + 7, 48, 24).contains(p))
                {
                    monitoredInstrumentTrack = (monitoredInstrumentTrack == channel ? -1 : channel);
                    audioEngine.setMonitoredInstrumentTrack(monitoredInstrumentTrack);
                    selectedTrack = logicalRow;
                    repaint();
                    return;
                }
                if (juce::Rectangle<int>(18, controlY, 32, 24).contains(p))
                {
                    if (audioLane) audioEngine.setTrackMuted(channel, !audioEngine.isTrackMuted(channel));
                    else audioEngine.setInstrumentTrackMuted(channel, !audioEngine.isInstrumentTrackMuted(channel));
                    repaint(); return;
                }
                if (juce::Rectangle<int>(55, controlY, 32, 24).contains(p))
                {
                    if (audioLane) audioEngine.setTrackSolo(channel, !audioEngine.isTrackSolo(channel));
                    else audioEngine.setInstrumentTrackSolo(channel, !audioEngine.isInstrumentTrackSolo(channel));
                    repaint(); return;
                }
                if (juce::Rectangle<int>(94, controlY + 4, 98, 18).contains(p))
                {
                    const float gain = 2.0f * juce::jlimit(0.0f, 1.0f, (float)(p.x - 94) / 98.0f);
                    if (audioLane) audioEngine.setTrackGain(channel, gain);
                    else audioEngine.setInstrumentTrackGain(channel, gain);
                    repaint(); return;
                }
            }
        }
    }

    // Select every dynamic MIDI and Instrument row, including after vertical scrolling.
    const int rowOffset = p.y - getArrangeTop();
    if (rowOffset >= 0 && p.y < getMixerTop())
    {
        const int logicalRow = getTrackScrollRows() + rowOffset / rowH;
        const int midiFirst = audioTrackCount;
        const int instrumentFirst = midiFirst + midiTrackCount;
        const int totalRows = instrumentFirst + instrumentTrackCount;

        if (logicalRow >= midiFirst && logicalRow < totalRows)
        {
            if (selectedTrack != logicalRow) dockStepSequencerMode = false;
            selectedTrack = logicalRow;
            if (logicalRow >= instrumentFirst && p.x >= headerW)
            {
                const int instrumentIndex = logicalRow - instrumentFirst;
                if (instrumentIndex >= 0 && instrumentIndex < (int) instrumentStepSequencers.size())
                {
                    auto& clips = instrumentStepSequencers[(size_t) instrumentIndex].timelineClips;
                    for (int clipIndex = (int) clips.size() - 1; clipIndex >= 0; --clipIndex)
                    {
                        const auto& clip = clips[(size_t) clipIndex];
                        const int left = headerW + (int) std::round(clip.startSeconds * pixelsPerSecond);
                        const int width = juce::jmax(1, (int) std::round(clip.lengthSeconds * pixelsPerSecond));
                        if (p.x >= left && p.x < left + width)
                        {
                            draggedPatternInstrument = instrumentIndex;
                            draggedPatternClip = clipIndex;
                            dragStartMouseX = (float) p.x;
                            dragStartSeconds = clip.startSeconds;
                            repaint();
                            return;
                        }
                    }
                }
            }
            repaint();
            return;
        }
    }

    if (p.y >= 76 && p.y < getHeight() - 210 && p.x >= headerW)
    {
        const double rawTime = juce::jmax(0.0, (double)(p.x - headerW) / pixelsPerSecond);
        const double snappedTime = std::round(rawTime / secondsPerMeasure) * secondsPerMeasure;
        audioEngine.setCurrentTimeSeconds(snappedTime);
        playheadSeconds = snappedTime;
        repaint();
    }
}

void MainComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (draggingStepSequencerPattern)
    {
        repaint();
        return;
    }
    if (draggedPatternInstrument >= 0 && draggedPatternClip >= 0)
    {
        if (draggedPatternInstrument < (int) instrumentStepSequencers.size())
        {
            auto& clips = instrumentStepSequencers[(size_t) draggedPatternInstrument].timelineClips;
            if (draggedPatternClip < (int) clips.size())
            {
                const double deltaSeconds = ((double) event.position.x - (double) dragStartMouseX)
                                            / getLibertyTimelinePixelsPerSecond();
                const double target = juce::jmax(0.0, dragStartSeconds + deltaSeconds);
                // Snap by integer MIDI ticks so dragging and tempo changes share
                // the same musical coordinate system (1/16 = 240 ticks).
                constexpr auto sixteenthTicks = MidiEngine::ticksPerQuarterNote / 4;
                const auto targetTick = juce::jmax<std::int64_t>(
                    0, MidiEngine::secondsToTick(target, tempoBpm));
                const auto snappedTick = ((targetTick + sixteenthTicks / 2) / sixteenthTicks)
                                         * sixteenthTicks;
                clips[(size_t) draggedPatternClip].startSeconds =
                    MidiEngine::tickToSeconds(snappedTick, tempoBpm);
                repaint();
            }
        }
        return;
    }
    if (draggingClip && draggedTrack >= 0)
    {
        const double pixelsPerSecond = getLibertyTimelinePixelsPerSecond();
        const double deltaSeconds = ((double)event.position.x - (double)dragStartMouseX) / pixelsPerSecond;
        audioEngine.setTrackStartSeconds(draggedTrack, juce::jmax(0.0, dragStartSeconds + deltaSeconds)); repaint(); return;
    }
    handleMixerMouse(event);
}

void MainComponent::mouseUp(const juce::MouseEvent& event)
{
    if (draggingStepSequencerPattern)
    {
        const auto p = event.getPosition();
        // A pattern drop must originate from an actual drag. A plain click,
        // including either click of a double-click, must not create a clip.
        if (event.getDistanceFromDragStart() < 5)
        {
            draggingStepSequencerPattern = false;
            draggedStepSequencerInstrument = -1;
            repaint();
            return;
        }
        const int rowH = getLibertyTrackRowHeight();
        const int rowOffset = p.y - getArrangeTop();
        if (rowOffset >= 0 && p.y < getMixerTop() && p.x >= 210)
        {
            const int logicalRow = getTrackScrollRows() + rowOffset / juce::jmax(1, rowH);
            const int midiLane = logicalRow - getAudioTrackCount();
            const int instrumentLane = midiLane - getMidiTrackCount();
            if ((midiLane >= 0 && midiLane < getMidiTrackCount())
                || (instrumentLane >= 0 && instrumentLane < getInstrumentTrackCount()))
            {
                const double dropTime = juce::jmax(0.0, (double)(p.x - 210) / getLibertyTimelinePixelsPerSecond());
                const double oldPlayhead = playheadSeconds;
                playheadSeconds = dropTime;
                if (midiLane >= 0 && midiLane < getMidiTrackCount())
                    commitInstrumentStepSequencerToMidiClip(draggedStepSequencerInstrument, midiLane);
                else if (instrumentLane == draggedStepSequencerInstrument)
                    createInstrumentPatternClip(instrumentLane);
                playheadSeconds = oldPlayhead;
            }
        }
        draggingStepSequencerPattern = false;
        draggedStepSequencerInstrument = -1;
        repaint();
        return;
    }
    if (draggedPatternInstrument >= 0 && draggedPatternClip >= 0
        && event.getDistanceFromDragStart() >= 3)
        publishInstrumentArrangementClips(draggedPatternInstrument);
    draggedPatternInstrument = -1;
    draggedPatternClip = -1;
    draggingClip = false;
    draggedTrack = -1;
}
