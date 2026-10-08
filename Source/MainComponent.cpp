#include "MainComponent.h"
void resizeLibertyDynamicTrackController(MainComponent*);
void resizeLibertyGridSnapController(MainComponent*);
void clearLibertyAudioClipResizeSource(AudioEngine&, int);

void resizeLibertyMixConsole(MainComponent*);
void resizeLibertyBrowserResizeController(MainComponent*);
void resizeLibertyMultiMidiClipController(MainComponent*);

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
    audioEngine.initialise();
    projectButton = std::make_unique<ProjectButton>(this);
    setWantsKeyboardFocus(true);
    startTimerHz(30);
}

MainComponent::~MainComponent() = default;

void MainComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    g.fillAll(juce::Colour(0xff0b0d10));
    auto transport = bounds.removeFromTop(transportHeight);
    auto mixer = bounds.removeFromBottom(mixerHeight);
    drawTransport(g, transport);
    drawTrackArea(g, bounds);
    drawMixer(g, mixer);
}

void MainComponent::drawTransport(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(juce::Colour(0xff15181d)); g.fillRect(area);
    g.setColour(juce::Colour(0xff30353d)); g.drawHorizontalLine(area.getBottom() - 1, 0.0f, (float)getWidth());

    const auto logoArea = juce::Rectangle<int>(0, 0, 182, 66);
    juce::ignoreUnused(logoArea);

    g.setColour(juce::Colours::white);
    juce::Path emblem;
    emblem.addEllipse(10.0f, 10.0f, 44.0f, 44.0f);
    g.strokePath(emblem, juce::PathStrokeType(2.0f));
    juce::Path wing;
    wing.startNewSubPath(16.0f, 43.0f);
    wing.cubicTo(23.0f, 34.0f, 27.0f, 20.0f, 34.0f, 14.0f);
    wing.cubicTo(36.0f, 27.0f, 41.0f, 35.0f, 50.0f, 38.0f);
    g.strokePath(wing, juce::PathStrokeType(2.6f));
    juce::Path wave;
    wave.startNewSubPath(14.0f, 39.0f);
    wave.cubicTo(21.0f, 45.0f, 29.0f, 45.0f, 36.0f, 39.0f);
    wave.cubicTo(41.0f, 34.0f, 47.0f, 33.0f, 52.0f, 35.0f);
    g.strokePath(wave, juce::PathStrokeType(2.0f));

    const juce::ColourGradient audioBarsGradient(
        juce::Colour(0xff72d8f5), 18.0f, 18.0f,
        juce::Colour(0xff4f82ff), 40.0f, 40.0f, false);
    g.setGradientFill(audioBarsGradient);
    g.fillRoundedRectangle(18.0f, 25.0f, 3.5f, 14.0f, 1.5f);
    g.fillRoundedRectangle(24.0f, 21.0f, 3.5f, 18.0f, 1.5f);
    g.fillRoundedRectangle(30.0f, 18.0f, 3.5f, 21.0f, 1.5f);
    g.fillRoundedRectangle(36.0f, 23.0f, 3.5f, 16.0f, 1.5f);

    auto libertyFont = juce::Font("Brush Script MT", 50.0f, juce::Font::plain);
    libertyFont.setPreferredFallbackFamilies({ "Snell Roundhand", "Apple Chancery", "URW Chancery L", "Cursive" });
    g.setColour(juce::Colours::white);
    g.setFont(libertyFont);
    g.drawText("Liberty", 58, 5, 120, 56, juce::Justification::left);

    const char* labels[] = { "|<", "<", "PLAY", ">", "|>" };
    for (int i = 0; i < 5; ++i)
    {
        auto r = juce::Rectangle<int>(215 + i * 62, 38, 56, 28);
        g.setColour(i == 2 && isPlaying ? juce::Colour(0xff2d965e) : juce::Colour(0xff252a31)); g.fillRoundedRectangle(r.toFloat(), 5.0f);
        g.setColour(juce::Colour(0xff454b54)); g.drawRoundedRectangle(r.toFloat(), 5.0f, 1.0f);
        g.setColour(juce::Colours::white); g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(i == 2 && isPlaying ? "STOP" : labels[i], r, juce::Justification::centred);
    }

    const double secondsPerBeat = 60.0 / juce::jmax(1.0, tempoBpm) * (4.0 / (double) juce::jmax(1, timeSignatureDenominator));
    const double beatsPerMeasure = (double) juce::jmax(1, timeSignatureNumerator);
    const double secondsPerMeasure = secondsPerBeat * beatsPerMeasure;
    const auto safeTime = juce::jmax(0.0, playheadSeconds);
    const auto measure = static_cast<long long>(std::floor(safeTime / secondsPerMeasure)) + 1;
    const auto beat = static_cast<int>(std::floor(std::fmod(safeTime, secondsPerMeasure) / secondsPerBeat)) + 1;

    const auto positionBox = juce::Rectangle<int>(740, 34, 90, 36);
    g.setColour(juce::Colour(0xff252a31));
    g.fillRoundedRectangle(positionBox.toFloat(), 5.0f);
    g.setColour(juce::Colour(0xff454b54));
    g.drawRoundedRectangle(positionBox.toFloat(), 5.0f, 1.0f);
    g.setColour(juce::Colour(0xffc9cdd3));
    g.setFont(juce::Font(14.0f));
    g.drawText(juce::String(measure) + ":" + juce::String(beat), positionBox, juce::Justification::centred);

    auto settingsButton = juce::Rectangle<int>(925, 10, 120, 24);
    g.setColour(juce::Colour(0xff252a31)); g.fillRoundedRectangle(settingsButton.toFloat(), 5.0f);
    g.setColour(juce::Colour(0xff454b54)); g.drawRoundedRectangle(settingsButton.toFloat(), 5.0f, 1.0f);
    g.setColour(juce::Colours::white); g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.drawText("AUDIO SETTINGS", settingsButton, juce::Justification::centred);
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
    g.setColour(juce::Colour(0xff12151a)); g.fillRect(ruler);
    g.setColour(juce::Colour(0xff20242b)); g.fillRect(rows.withWidth(headerW));
    g.setColour(juce::Colour(0xff111419)); g.fillRect(rows.withTrimmedLeft(headerW));

    g.setColour(juce::Colour(0xff353b44));
    for (int measureIndex = 0; measureIndex < 100; ++measureIndex)
    {
        const int x = headerW + (int)std::round(measureIndex * pixelsPerMeasure);
        if (x >= getWidth()) break;
        g.drawVerticalLine(x, (float)ruler.getY(), (float)rows.getBottom());
    }
    g.setColour(juce::Colour(0xff777f89)); g.setFont(juce::Font(11.0f));
    for (int i = 0; i < 100; ++i)
    {
        const int x = headerW + (int)std::round(i * pixelsPerMeasure);
        if (x >= getWidth()) break;
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
        g.setColour(logicalRow % 2 ? juce::Colour(0xff14171c) : juce::Colour(0xff171a1f)); g.fillRect(row);
        auto header = row.withWidth(headerW);
        bool selected = false;
        juce::String title;
        if (logicalRow < audioCount)
        {
            const int i = logicalRow;
            selected = selectedTrack == i;
            title = "Audio " + juce::String(i + 1);
            g.setColour(selected ? juce::Colour(0xff263746) : juce::Colour(0xff1e232a)); g.fillRect(header);
            auto clip = row.withTrimmedLeft(headerW + 20).reduced(4);
            if (audioEngine.hasAudioFile(i))
            {
                clip.setWidth(juce::jmax(1, (int)std::round(audioEngine.getAudioFileLengthSeconds(i) * pixelsPerSecond)));
                clip.setX(headerW + (int)std::round(audioEngine.getTrackStartSeconds(i) * pixelsPerSecond));
                g.setColour(selected ? juce::Colour(0xff31506a) : juce::Colour(0xff294459)); g.fillRoundedRectangle(clip.toFloat(), 5.0f);
                g.setColour(juce::Colour(0xff709fc5)); g.drawRoundedRectangle(clip.toFloat(), 5.0f, 1.0f);
                if ((size_t)i < waveformMin.size() && !waveformMin[(size_t)i].empty())
                {
                    const auto centreY=clip.getCentreY(); const auto amplitude=juce::jmax(1.0f,clip.getHeight()*0.42f);
                    const int points=(int)waveformMin[(size_t)i].size(); juce::Path waveform;
                    for(int p=0;p<points;++p){const auto x=clip.getX()+4.0f+(clip.getWidth()-8.0f)*(float)p/(float)juce::jmax(1,points-1);const auto y=(float)centreY-waveformMax[(size_t)i][(size_t)p]*amplitude;if(p==0)waveform.startNewSubPath(x,y);else waveform.lineTo(x,y);}
                    for(int p=points-1;p>=0;--p){const auto x=clip.getX()+4.0f+(clip.getWidth()-8.0f)*(float)p/(float)juce::jmax(1,points-1);waveform.lineTo(x,(float)centreY-waveformMin[(size_t)i][(size_t)p]*amplitude);}
                    waveform.closeSubPath(); g.setColour(juce::Colour(0xff9fc7e8)); g.fillPath(waveform);
                }
                g.setColour(juce::Colours::white); g.setFont(juce::Font(11.0f)); g.drawText(audioEngine.getAudioFileName(i), clip.reduced(10), juce::Justification::centredLeft, true);
            }
        }
        else if (logicalRow < audioCount + midiCount)
        {
            const int i = logicalRow - audioCount;
            title = "MIDI " + juce::String(i + 1);
            selected = selectedTrack == audioCount + i;
            g.setColour(selected ? juce::Colour(0xff263746) : juce::Colour(0xff1e232a)); g.fillRect(header);
        }
        else
        {
            const int i = logicalRow - audioCount - midiCount;
            title = "Instrument " + juce::String(i + 1);
            selected = selectedTrack == audioCount + midiCount + i;
            g.setColour(selected ? juce::Colour(0xff263746) : juce::Colour(0xff1e232a)); g.fillRect(header);
            if (selected)
            {
                if (auto* pattern = getInstrumentStepSequencer(i))
                {
                    auto panel = row.withTrimmedLeft(headerW + 8).reduced(2, 8);
                    const int titleWidth = 122;
                    auto onOff = juce::Rectangle<int>(panel.getX(), panel.getY(), titleWidth - 6, 22);
                    g.setColour(pattern->enabled ? juce::Colour(0xff2d965e) : juce::Colour(0xff252a31));
                    g.fillRoundedRectangle(onOff.toFloat(), 4.0f);
                    g.setColour(juce::Colours::white); g.setFont(juce::Font(10.0f, juce::Font::bold));
                    g.drawText(pattern->enabled ? "STEP SEQ ON" : "STEP SEQ OFF", onOff, juce::Justification::centred);
                    const int available = juce::jmax(0, panel.getWidth() - titleWidth);
                    const int stepW = juce::jmax(12, juce::jmin(42, available / 16));
                    const int firstStep = juce::jlimit(0, 3, stepSequencerPage) * 16;
                    for (int s = 0; s < 16; ++s)
                    {
                        const int absoluteStep = firstStep + s;
                        auto pad = juce::Rectangle<int>(panel.getX() + titleWidth + s * stepW, panel.getY(), stepW - 3, 22);
                        const bool availableStep = absoluteStep < pattern->stepCount;
                        const bool active = availableStep && pattern->steps[(size_t)absoluteStep].enabled;
                        g.setColour(active ? juce::Colour(0xff4f82ff) : (availableStep ? juce::Colour(0xff252a31) : juce::Colour(0xff15181d)));
                        g.fillRoundedRectangle(pad.toFloat(), 3.0f);
                        g.setColour((s % 4) == 0 ? juce::Colour(0xff93a9c5) : juce::Colour(0xff454b54));
                        g.drawRoundedRectangle(pad.toFloat(), 3.0f, 1.0f);
                        g.setColour(availableStep ? juce::Colours::white : juce::Colour(0xff555a61)); g.setFont(juce::Font(9.0f));
                        g.drawText(juce::String(absoluteStep + 1), pad, juce::Justification::centred);
                    }
                    const int controlsY = panel.getY() + 27;
                    auto drawControl = [&](juce::String text, int x, int w, bool active)
                    {
                        auto r = juce::Rectangle<int>(x, controlsY, w, 18);
                        g.setColour(active ? juce::Colour(0xff2d6f9f) : juce::Colour(0xff252a31)); g.fillRoundedRectangle(r.toFloat(), 3.0f);
                        g.setColour(juce::Colour(0xff454b54)); g.drawRoundedRectangle(r.toFloat(), 3.0f, 1.0f);
                        g.setColour(juce::Colours::white); g.setFont(juce::Font(9.0f)); g.drawText(text, r, juce::Justification::centred);
                    };
                    int cx = panel.getX();
                    drawControl("16", cx, 30, pattern->stepCount == 16); cx += 34;
                    drawControl("32", cx, 30, pattern->stepCount == 32); cx += 34;
                    drawControl("64", cx, 30, pattern->stepCount == 64); cx += 42;
                    const std::int64_t rates[] = { MidiEngine::ticksPerQuarterNote, MidiEngine::ticksPerQuarterNote/2, MidiEngine::ticksPerQuarterNote/4, MidiEngine::ticksPerQuarterNote/8 };
                    const char* rateNames[] = { "1/4", "1/8", "1/16", "1/32" };
                    for (int r = 0; r < 4; ++r) { drawControl(rateNames[r], cx, 38, pattern->stepTicks == rates[r]); cx += 42; }
                    drawControl("SW " + juce::String((int)std::round(pattern->swing * 100.0f)) + "%", cx, 62, pattern->swing > 0.0f); cx += 68;
                    const int pages = juce::jmax(1, (pattern->stepCount + 15) / 16);
                    for (int page = 0; page < pages; ++page) { drawControl("P" + juce::String(page + 1), cx, 30, stepSequencerPage == page); cx += 34; }
                    const int selectedStep = juce::jlimit(0, pattern->stepCount - 1, stepSequencerSelectedStep);
                    const auto& editStep = pattern->steps[(size_t)selectedStep];
                    const int editY = controlsY + 22;
                    auto drawEdit = [&](juce::String text, int x, int w, bool active)
                    {
                        auto r = juce::Rectangle<int>(x, editY, w, 18);
                        g.setColour(active ? juce::Colour(0xff654ca3) : juce::Colour(0xff20242b)); g.fillRoundedRectangle(r.toFloat(), 3.0f);
                        g.setColour(juce::Colour(0xff454b54)); g.drawRoundedRectangle(r.toFloat(), 3.0f, 1.0f);
                        g.setColour(juce::Colours::white); g.setFont(juce::Font(8.5f)); g.drawText(text, r, juce::Justification::centred);
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
                    const int actionY = editY + 22;
                    auto drawAction = [&](juce::String text, int x, int w)
                    {
                        auto r = juce::Rectangle<int>(x, actionY, w, 18);
                        g.setColour(juce::Colour(0xff29313a)); g.fillRoundedRectangle(r.toFloat(), 3.0f);
                        g.setColour(juce::Colour(0xff59636f)); g.drawRoundedRectangle(r.toFloat(), 3.0f, 1.0f);
                        g.setColour(juce::Colours::white); g.setFont(juce::Font(8.5f, juce::Font::bold)); g.drawText(text, r, juce::Justification::centred);
                    };
                    int ax = panel.getX();
                    drawAction("CLEAR", ax, 48); ax += 52;
                    drawAction("RANDOM", ax, 58); ax += 62;
                    drawAction("REVERSE", ax, 62); ax += 66;
                    drawAction("ROTATE", ax, 56); ax += 60;
                    drawAction("DUPLICATE", ax, 72); ax += 80;
                    static constexpr const char* rootNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
                    static constexpr const char* scaleNames[] = {"OFF","MAJOR","MINOR","PENTA"};
                    drawAction("ROOT " + juce::String(rootNames[juce::jlimit(0,11,(int)pattern->root)]), ax, 58); ax += 62;
                    drawAction("SCALE " + juce::String(scaleNames[juce::jlimit(0,3,(int)pattern->scale)]), ax, 82); ax += 86;
                    drawAction("TRANS " + juce::String(pattern->transpose), ax, 66); ax += 70;
                    drawAction("HUM " + juce::String((int)std::round(pattern->humanize * 100.0f)) + "%", ax, 60); ax += 64;
                    drawAction("EUC " + juce::String(pattern->euclideanPulses) + "/" + juce::String(pattern->stepCount), ax, 68); ax += 72;
                    drawAction("EUC ROT " + juce::String(pattern->euclideanRotation), ax, 70); ax += 74;
                    drawAction("CYCLE " + juce::String(pattern->cycleSteps), ax, 66);
                    ax += 70;
                    if ((size_t)i < instrumentStepSequencers.size())
                    {
                        const auto activePattern = instrumentStepSequencers[(size_t)i].activePattern;
                        for (int bankIndex = 0; bankIndex < 8; ++bankIndex)
                        {
                            drawAction("PAT " + juce::String(bankIndex + 1), ax, 44);
                            if (bankIndex == activePattern)
                            {
                                auto active = juce::Rectangle<int>(ax, actionY, 44, 18);
                                g.setColour(juce::Colour(0xff4f82ff));
                                g.drawRoundedRectangle(active.toFloat(), 3.0f, 2.0f);
                            }
                            ax += 48;
                        }
                    }
                }
            }
        }
        g.setColour(juce::Colours::white); g.setFont(juce::Font(14.0f, juce::Font::bold));
        g.drawText(title, header.getX()+14, header.getY()+8, 180, 22, juce::Justification::left);
        // Draw the timeline grid inside EVERY visible row, including the
        // final clipped row immediately above the mixer.  Previously the only
        // vertical lines were painted before the rows; each row background then
        // painted over them, which is why the last track could appear gridless.
        {
            const juce::Graphics::ScopedSaveState rowGridState(g);
            g.reduceClipRegion(row.withTrimmedLeft(headerW));
            g.setColour(juce::Colour(0xff252b33));
            // Uniform 1/16 Arrange grid on every row, including the final clipped row.
            constexpr int subdivisions = 16;
            const float pixelsPerSubdivision = pixelsPerMeasure / (float) subdivisions;
            for (int subdivisionIndex = 0; subdivisionIndex < 100 * subdivisions; ++subdivisionIndex)
            {
                const int x = headerW + (int) std::round(subdivisionIndex * pixelsPerSubdivision);
                if (x >= row.getRight()) break;
                if (x < headerW) continue;
                const bool measureLine = (subdivisionIndex % subdivisions) == 0;
                g.setColour(measureLine ? juce::Colour(0xff3b424c) : juce::Colour(0xff252b33));
                g.drawVerticalLine(x, (float) row.getY(), (float) row.getBottom());
            }
        }
        g.setColour(juce::Colour(0xff2c323a)); g.drawHorizontalLine(row.getBottom()-1, 0.0f, (float)getWidth());
    }

    const float playheadX = headerW + (float)(playheadSeconds * pixelsPerSecond);
    if (playheadX >= headerW && playheadX <= (float)getWidth())
    { g.setColour(juce::Colours::white); g.drawLine(playheadX,(float)ruler.getY(),playheadX,(float)area.getBottom(),2.0f); }
}
void MainComponent::drawMixer(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(juce::Colour(0xff101318)); g.fillRect(area);

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
        g.setColour(master ? juce::Colour(0xff1b2027)
                           : (isInstrument ? juce::Colour(0xff182128) : juce::Colour(0xff171b20)));
        g.fillRoundedRectangle(c.toFloat(), 5.0f);
        g.setColour(isInstrument ? juce::Colour(0xff31546a) : juce::Colour(0xff343a44));
        g.drawRoundedRectangle(c.toFloat(), 5.0f, 1.0f);

        const juce::String channelName = master ? "MASTER"
                                                : (isAudio ? "Audio " + juce::String(sourceIndex + 1)
                                                           : "Instrument " + juce::String(sourceIndex + 1));
        g.setColour(juce::Colours::white);
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
            g.setColour(muted ? juce::Colour(0xff9b4545) : juce::Colour(0xff252a31)); g.fillRoundedRectangle(mute.toFloat(), 4.0f);
            g.setColour(solo ? juce::Colour(0xff8b7a32) : juce::Colour(0xff252a31)); g.fillRoundedRectangle(soloButton.toFloat(), 4.0f);
            g.setColour(juce::Colour(0xff454b54)); g.drawRoundedRectangle(mute.toFloat(), 4.0f, 1.0f); g.drawRoundedRectangle(soloButton.toFloat(), 4.0f, 1.0f);
            g.setColour(juce::Colours::white); g.setFont(juce::Font(9.0f, juce::Font::bold)); g.drawText("M", mute, juce::Justification::centred); g.drawText("S", soloButton, juce::Justification::centred);
        }
        else if (isInstrument)
        {
            auto mute = juce::Rectangle<int>(c.getX() + 8, c.getY() + 32, 44, 20);
            auto soloButton = juce::Rectangle<int>(c.getX() + 58, c.getY() + 32, 44, 20);
            const bool muted = audioEngine.isInstrumentTrackMuted(sourceIndex);
            const bool solo = audioEngine.isInstrumentTrackSolo(sourceIndex);
            g.setColour(muted ? juce::Colour(0xff9b4545) : juce::Colour(0xff252a31)); g.fillRoundedRectangle(mute.toFloat(), 4.0f);
            g.setColour(solo ? juce::Colour(0xff8b7a32) : juce::Colour(0xff252a31)); g.fillRoundedRectangle(soloButton.toFloat(), 4.0f);
            g.setColour(juce::Colour(0xff454b54)); g.drawRoundedRectangle(mute.toFloat(), 4.0f, 1.0f); g.drawRoundedRectangle(soloButton.toFloat(), 4.0f, 1.0f);
            g.setColour(juce::Colour(0xff9fc7e8)); g.setFont(juce::Font(9.0f, juce::Font::bold)); g.drawText("M", mute, juce::Justification::centred); g.drawText("S", soloButton, juce::Justification::centred);
        }

        const int faderTop = c.getY() + 58, faderBottom = c.getBottom() - 45;
        auto fader = juce::Rectangle<float>((float)c.getCentreX() - 7.0f, (float)faderTop, 14.0f, (float)(faderBottom - faderTop));
        g.setColour(juce::Colour(0xff090b0e)); g.fillRoundedRectangle(fader, 3.0f);

        const float gain = master ? audioEngine.getMasterGain() : (isAudio ? audioEngine.getTrackGain(sourceIndex) : audioEngine.getInstrumentTrackGain(sourceIndex));
        const auto normalized = juce::jlimit(0.0f, 1.0f, gain * 0.5f);
        const auto knobY = fader.getBottom() - normalized * fader.getHeight();
        g.setColour(juce::Colour(0xffd6d9de)); g.fillRoundedRectangle(fader.getX() - 2.0f, knobY - 6.0f, fader.getWidth() + 4.0f, 12.0f, 3.0f);

        const auto db = 20.0f * std::log10(juce::jmax(0.000001f, gain));
        g.setColour(juce::Colour(0xff858c96)); g.setFont(juce::Font(10.0f));
        g.drawText(db < -59.9f ? "-inf dB" : juce::String(db, 1) + " dB", c.getX(), c.getBottom() - 38, c.getWidth(), 16, juce::Justification::centred);
        g.drawText(master ? "MASTER" : (isAudio ? "PAN " + juce::String(audioEngine.getTrackPan(sourceIndex), 2) : "PAN " + juce::String(audioEngine.getInstrumentTrackPan(sourceIndex), 2)),
                   c.getX(), c.getBottom() - 22, c.getWidth(), 16, juce::Justification::centred);
    }
}

void MainComponent::resized() { resizeLibertyMixConsole(this); resizeLibertyDynamicTrackController(this); resizeLibertyGridSnapController(this); resizeLibertyBrowserResizeController(this); resizeLibertyMultiMidiClipController(this); repaint(); }

void MainComponent::timerCallback()
{
    const auto newPlayhead = audioEngine.getCurrentTimeSeconds();
    const bool newPlaying = audioEngine.isPlaying();
    const bool changed = newPlaying != isPlaying || (newPlaying && std::abs(newPlayhead - playheadSeconds) > 0.0001);
    playheadSeconds = newPlayhead;
    isPlaying = newPlaying;
    if (changed)
        repaint(0, transportHeight, getWidth(), juce::jmax(0, getHeight() - transportHeight));
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

void MainComponent::mouseDown(const juce::MouseEvent& event)
{
    const auto p = event.getPosition();
    if (handleMixerMouse(event)) return;

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

    // Step Sequencer hit-test for the selected Instrument row.
    {
        const int rowOffset = p.y - getArrangeTop();
        if (rowOffset >= 0 && p.y < getMixerTop())
        {
            const int logicalRow = getTrackScrollRows() + rowOffset / rowH;
            const int instrumentFirst = audioTrackCount + midiTrackCount;
            if (logicalRow >= instrumentFirst && logicalRow < instrumentFirst + instrumentTrackCount
                && selectedTrack == logicalRow)
            {
                const int instrumentIndex = logicalRow - instrumentFirst;
                if (auto* pattern = getInstrumentStepSequencer(instrumentIndex))
                {
                    const int rowY = getArrangeTop() + (logicalRow - getTrackScrollRows()) * rowH;
                    auto panel = juce::Rectangle<int>(headerW + 8, rowY + 8, getWidth() - headerW - 16, juce::jmax(1, rowH - 16));
                    const int titleWidth = 122;
                    auto onOff = juce::Rectangle<int>(panel.getX(), panel.getY(), titleWidth - 6, 22);
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
                        auto pad = juce::Rectangle<int>(panel.getX() + titleWidth + s * stepW, panel.getY(), stepW - 3, 22);
                        if (pad.contains(p) && absoluteStep < pattern->stepCount)
                        {
                            stepSequencerSelectedStep = absoluteStep;
                            pattern->steps[(size_t)absoluteStep].enabled = !pattern->steps[(size_t)absoluteStep].enabled;
                            publishInstrumentStepSequencer(instrumentIndex); repaint(); return;
                        }
                    }
                    const int controlsY = panel.getY() + 27;
                    auto hit = [&](int x, int w) { return juce::Rectangle<int>(x, controlsY, w, 18).contains(p); };
                    int cx = panel.getX();
                    const int counts[] = {16,32,64};
                    for (int n = 0; n < 3; ++n) { if (hit(cx,30)) { pattern->stepCount=counts[n]; stepSequencerPage=juce::jmin(stepSequencerPage,(counts[n]-1)/16); publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } cx+=34; }
                    cx += 8;
                    const std::int64_t rates[] = { MidiEngine::ticksPerQuarterNote, MidiEngine::ticksPerQuarterNote/2, MidiEngine::ticksPerQuarterNote/4, MidiEngine::ticksPerQuarterNote/8 };
                    for (int r=0;r<4;++r) { if(hit(cx,38)) { pattern->stepTicks=rates[r]; publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } cx+=42; }
                    if (hit(cx,62)) { pattern->swing = pattern->swing >= 0.50f ? 0.0f : pattern->swing + 0.10f; publishInstrumentStepSequencer(instrumentIndex); repaint(); return; }
                    cx += 68;
                    const int pages = juce::jmax(1,(pattern->stepCount+15)/16);
                    for(int page=0;page<pages;++page) { if(hit(cx,30)) { stepSequencerPage=page; repaint(); return; } cx+=34; }
                    const int selectedStep = juce::jlimit(0, pattern->stepCount - 1, stepSequencerSelectedStep);
                    auto& editStep = pattern->steps[(size_t)selectedStep];
                    const int editY = controlsY + 22;
                    auto editHit = [&](int x, int w) { return juce::Rectangle<int>(x, editY, w, 18).contains(p); };
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
                    const int actionY = editY + 22;
                    auto actionHit = [&](int x, int w) { return juce::Rectangle<int>(x, actionY, w, 18).contains(p); };
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
                    if (actionHit(ax,58)) { pattern->root=(std::uint8_t)((pattern->root+1)%12); publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } ax += 62;
                    if (actionHit(ax,82)) { pattern->scale=(LibertyStepSequencer::Scale)(((int)pattern->scale+1)%4); publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } ax += 86;
                    if (actionHit(ax,66)) { pattern->transpose=pattern->transpose>=12 ? -12 : pattern->transpose+1; publishInstrumentStepSequencer(instrumentIndex); repaint(); return; } ax += 70;
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
                    if (actionHit(ax,66)) { pattern->cycleSteps = pattern->cycleSteps >= pattern->stepCount ? 1 : pattern->cycleSteps + 1; publishInstrumentStepSequencer(instrumentIndex); repaint(); return; }
                    ax += 70;
                    if ((size_t)instrumentIndex < instrumentStepSequencers.size())
                    {
                        for (int bankIndex=0; bankIndex<8; ++bankIndex)
                        {
                            if (actionHit(ax,44))
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

    const int track = getAudioTrackAtPosition(p);
    if (track >= 0)
    {
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
            selectedTrack = logicalRow;
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
    if (draggingClip && draggedTrack >= 0)
    {
        const double pixelsPerSecond = getLibertyTimelinePixelsPerSecond();
        const double deltaSeconds = ((double)event.position.x - (double)dragStartMouseX) / pixelsPerSecond;
        audioEngine.setTrackStartSeconds(draggedTrack, juce::jmax(0.0, dragStartSeconds + deltaSeconds)); repaint(); return;
    }
    handleMixerMouse(event);
}
