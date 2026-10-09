#include "MainComponent.h"
void resizeLibertyDynamicTrackController(MainComponent*);
void resizeLibertyGridSnapController(MainComponent*);
void clearLibertyAudioClipResizeSource(AudioEngine&, int);

void resizeLibertyMixConsole(MainComponent*);
void resizeLibertyBrowserResizeController(MainComponent*);
void resizeLibertyMultiMidiClipController(MainComponent*);
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
    const int instrumentFirst = getAudioTrackCount() + getMidiTrackCount();
    const bool showStepSequencer = selectedTrack >= instrumentFirst && selectedTrack < instrumentFirst + getInstrumentTrackCount();
    if (showStepSequencer)
        drawStepSequencerDock(g, mixer);
    else
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
                        g.setColour(juce::Colour(0xff265c65));
                        g.fillRoundedRectangle(clip.toFloat(), 5.0f);
                        g.setColour(juce::Colour(0xff75d6db));
                        g.drawRoundedRectangle(clip.toFloat(), 5.0f, 1.2f);
                        g.setColour(juce::Colours::white);
                        g.setFont(juce::Font(11.0f, juce::Font::bold));
                        g.drawText(patternClip.name, clip.reduced(7, 2), juce::Justification::centredLeft, true);
                    }
                }
            }
        }
        g.setColour(juce::Colour(0xff2c323a)); g.drawHorizontalLine(row.getBottom()-1, 0.0f, (float)getWidth());
    }

    const float playheadX = headerW + (float)(playheadSeconds * pixelsPerSecond);
    if (playheadX >= headerW && playheadX <= (float)getWidth())
    { g.setColour(juce::Colours::white); g.drawLine(playheadX,(float)ruler.getY(),playheadX,(float)area.getBottom(),2.0f); }
}

void MainComponent::drawStepSequencerDock(juce::Graphics& g, juce::Rectangle<int> area)
{
    juce::ColourGradient dockGradient(juce::Colour(0xff1d2b3d), (float)area.getX(), (float)area.getY(),
                                      juce::Colour(0xff0c1420), (float)area.getX(), (float)area.getBottom(), false);
    g.setGradientFill(dockGradient);
    g.fillRect(area);
    g.setColour(juce::Colour(0xff4d789e));
    g.fillRect(area.getX(), area.getY(), area.getWidth(), 2);
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
                    g.setColour(pattern->enabled ? juce::Colour(0xff177c70) : juce::Colour(0xff303d4d));
                    g.fillRoundedRectangle(onOff.toFloat(), 7.0f);
                    if (pattern->enabled)
                    {
                        g.setColour(juce::Colour(0x4465f5c9));
                        g.fillRoundedRectangle((float)onOff.getX() + 4.0f, (float)onOff.getY() + 3.0f,
                                               (float)juce::jmax(1, onOff.getWidth() - 8), 4.0f, 2.0f);
                    }
                    g.setColour(pattern->enabled ? juce::Colour(0xff5de0b9) : juce::Colour(0xff607187));
                    g.drawRoundedRectangle(onOff.toFloat(), 7.0f, 1.5f);
                    g.setColour(juce::Colours::white); g.setFont(juce::Font(10.0f, juce::Font::bold));
                    g.drawText(pattern->enabled ? "STEP SEQ ON" : "STEP SEQ OFF", onOff, juce::Justification::centred);
                    const auto seqPlay = juce::Rectangle<int>(onOff.getRight(), onOff.getY(), 55, onOff.getHeight());
                    const bool previewActive = audioEngine.isStepPreviewPlaying() && audioEngine.getStepPreviewTrack() == instrumentIndex;
                    g.setColour(previewActive ? juce::Colour(0xff286c9a) : juce::Colour(0xff273e55));
                    g.fillRoundedRectangle(seqPlay.toFloat(), 7.0f);
                    g.setColour(previewActive ? juce::Colour(0xff83d8ff) : juce::Colour(0xff638aa9));
                    g.drawRoundedRectangle(seqPlay.toFloat(), 7.0f, 1.2f);
                    g.setColour(juce::Colours::white);
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
                        g.setColour(active ? juce::Colour(0xff438cff) : (availableStep ? juce::Colour(0xff28394d) : juce::Colour(0xff111823)));
                        g.fillRoundedRectangle(pad.toFloat(), 6.0f);
                        if (active)
                        {
                            g.setColour(juce::Colour(0x334db5ff));
                            g.fillRoundedRectangle((float)pad.getX() + 2.0f, (float)pad.getY() + 2.0f,
                                                   (float)juce::jmax(1, pad.getWidth() - 4), 5.0f, 2.0f);
                        }
                        if (s % 4 == 0)
                        {
                            g.setColour(active ? juce::Colour(0xffd1e9ff) : juce::Colour(0xff6689af));
                            g.fillRoundedRectangle((float)pad.getX() + 2.0f, (float)pad.getY() + 4.0f,
                                                   2.0f, (float)juce::jmax(1, pad.getHeight() - 8), 1.0f);
                        }
                        g.setColour((s % 4) == 0 ? juce::Colour(0xff93a9c5) : juce::Colour(0xff496078));
                        g.drawRoundedRectangle(pad.toFloat(), 6.0f, 1.0f);
                        if (active)
                        {
                            g.setColour(juce::Colour(0xff9ac8ff));
                            g.fillRoundedRectangle((float)pad.getX() + 5.0f, (float)pad.getBottom() - 4.0f,
                                                   (float)juce::jmax(1, pad.getWidth() - 10), 2.0f, 1.0f);
                        }
                        g.setColour(active ? juce::Colours::white : (availableStep ? juce::Colour(0xffc3d6e9) : juce::Colour(0xff647184)));
                        g.setFont(juce::Font(10.5f, juce::Font::bold));
                        g.drawText(juce::String(absoluteStep + 1), pad, juce::Justification::centred);
                    }
                    const int controlsY = panel.getY() + 42;
                    g.setColour(juce::Colour(0xff35495f));
                    g.fillRect(panel.getX(), controlsY - 7, panel.getWidth(), 1);
                    g.setColour(juce::Colour(0xff243448));
                    g.fillRect(panel.getX(), controlsY + 30, panel.getWidth(), 1);
                    g.fillRect(panel.getX(), controlsY + 66, panel.getWidth(), 1);
                    auto drawControl = [&](juce::String text, int x, int w, bool active)
                    {
                        auto r = stretchedControl(x, w, controlsY, 26, 550 + 34 * juce::jmax(1, (pattern->stepCount + 15) / 16));
                        g.setColour(active ? juce::Colour(0xff2a639a) : juce::Colour(0xff263343)); g.fillRoundedRectangle(r.toFloat(), 6.0f);
                        if (active) { g.setColour(juce::Colour(0xff84c1ff)); g.fillRoundedRectangle((float)r.getX() + 5.0f, (float)r.getBottom() - 3.0f, (float)juce::jmax(1, r.getWidth() - 10), 2.0f, 1.0f); }
                        g.setColour(active ? juce::Colour(0xff9acaff) : juce::Colour(0xff50677e));
                        g.drawRoundedRectangle(r.toFloat(), 6.0f, active ? 1.5f : 1.0f);
                        g.setColour(active ? juce::Colours::white : juce::Colour(0xffcad8e7));
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
                        g.setColour(active ? juce::Colour(0xff654ca3) : juce::Colour(0xff273143)); g.fillRoundedRectangle(r.toFloat(), 6.0f);
                        if (active) { g.setColour(juce::Colour(0xffc4adff)); g.fillRoundedRectangle((float)r.getX() + 5.0f, (float)r.getBottom() - 3.0f, (float)juce::jmax(1, r.getWidth() - 10), 2.0f, 1.0f); }
                        g.setColour(active ? juce::Colour(0xffc2aaff) : juce::Colour(0xff53647d));
                        g.drawRoundedRectangle(r.toFloat(), 6.0f, active ? 1.5f : 1.0f);
                        g.setColour(active ? juce::Colour(0xfff8f0ff) : juce::Colour(0xffd5dcec));
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
                        g.setColour(juce::Colour(0xff304155)); g.fillRoundedRectangle(r.toFloat(), 6.0f);
                        g.setColour(juce::Colour(0xff344c62));
                        g.fillRoundedRectangle((float)r.getX() + 2.0f, (float)r.getY() + 2.0f,
                                               (float)juce::jmax(1, r.getWidth() - 4), 2.0f, 1.0f);
                        g.setColour(juce::Colour(0xff58718b)); g.drawRoundedRectangle(r.toFloat(), 6.0f, 1.0f);
                        g.setColour(juce::Colour(0xff172433));
                        g.fillRoundedRectangle((float)r.getX() + 6.0f, (float)r.getBottom() - 3.0f,
                                               (float)juce::jmax(1, r.getWidth() - 12), 1.0f, 0.5f);
                        g.setColour(juce::Colour(0xffe7f0fa));
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
                    g.setColour(juce::Colour(0xff255a69));
                    g.fillRoundedRectangle(createClipButton.toFloat(), 5.0f);
                    g.setColour(juce::Colour(0xff6dd6db));
                    g.drawRoundedRectangle(createClipButton.toFloat(), 5.0f, 1.2f);
                    g.setFont(juce::Font(10.0f, juce::Font::bold));
                    g.setColour(juce::Colours::white);
                    g.drawText("CREATE PATTERN CLIP", createClipButton, juce::Justification::centred);
                    if ((size_t)instrumentIndex < instrumentStepSequencers.size())
                    {
                        const auto activePattern = instrumentStepSequencers[(size_t)instrumentIndex].activePattern;
                        for (int bankIndex = 0; bankIndex < 8; ++bankIndex)
                        {
                            const auto patRect = wideRow(bankIndex, 8, panel.getBottom() - 30, 28);
                            ax = patRect.getX();
                            const bool selectedPattern = bankIndex == activePattern;
                            g.setColour(selectedPattern ? juce::Colour(0xff294c79) : juce::Colour(0xff263445));
                            g.fillRoundedRectangle(patRect.toFloat(), 6.0f);
                            if (selectedPattern)
                            {
                                g.setColour(juce::Colour(0x444e9cff));
                                g.fillRoundedRectangle((float)patRect.getX() + 3.0f, (float)patRect.getY() + 2.0f,
                                                       (float)juce::jmax(1, patRect.getWidth() - 6), 5.0f, 2.0f);
                            }
                            g.setColour(selectedPattern ? juce::Colour(0xff79b4ff) : juce::Colour(0xff43576e));
                            g.drawRoundedRectangle(patRect.toFloat(), 6.0f, selectedPattern ? 1.5f : 1.0f);
                            g.setColour(selectedPattern ? juce::Colour(0xfff2f8ff) : juce::Colour(0xffb7c7d9));
                            g.setFont(juce::Font(11.0f, juce::Font::bold));
                            g.drawText("PAT " + juce::String(bankIndex + 1), patRect, juce::Justification::centred);
                            if (bankIndex == activePattern)
                            {
                                auto active = patRect;
                                g.setColour(juce::Colour(0xff9acbff));
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

void MainComponent::mouseDoubleClick(const juce::MouseEvent& event)
{
    const auto p = event.getPosition();
    if (p.x < trackHeaderWidth || p.y < getArrangeTop() || p.y >= getMixerTop()) return;
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
        juce::AlertWindow renameDialog("Rename Pattern Clip", "Enter a name for this clip:",
                                        juce::AlertWindow::NoIcon);
        renameDialog.addTextEditor("name", clip.name, "Clip name:");
        renameDialog.addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        renameDialog.addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
        if (renameDialog.runModalLoop() == 1)
        {
            const auto name = renameDialog.getTextEditorContents("name").trim();
            if (name.isNotEmpty())
            {
                clip.name = name.substring(0, 64);
                repaint();
            }
        }
        return;
    }
}

void MainComponent::mouseDown(const juce::MouseEvent& event)
{
    const auto p = event.getPosition();
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

    // Step Sequencer hit-test for the selected Instrument row.
    {
        if (p.y >= getMixerTop())
        {
            const int logicalRow = selectedTrack;
            const int instrumentFirst = audioTrackCount + midiTrackCount;
            if (logicalRow >= instrumentFirst && logicalRow < instrumentFirst + instrumentTrackCount)
            {
                const int instrumentIndex = logicalRow - instrumentFirst;
                if (auto* pattern = getInstrumentStepSequencer(instrumentIndex))
                {
                    auto panel = juce::Rectangle<int>(0, getMixerTop(), getWidth(), juce::jmax(1, mixerHeight));
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
                const double beat = 60.0 / juce::jmax(1.0, tempoBpm);
                const double sixteenth = beat / 4.0;
                const double target = juce::jmax(0.0, dragStartSeconds + deltaSeconds);
                clips[(size_t) draggedPatternClip].startSeconds = std::round(target / sixteenth) * sixteenth;
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
    if (draggedPatternInstrument >= 0)
        publishInstrumentArrangementClips(draggedPatternInstrument);
    draggedPatternInstrument = -1;
    draggedPatternClip = -1;
    draggingClip = false;
    draggedTrack = -1;
}
