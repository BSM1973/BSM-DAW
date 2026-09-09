#include "MainComponent.h"
#include <algorithm>
#include <cmath>

class MainComponent::AudioSettingsWindow final : public juce::DocumentWindow
{
public:
    explicit AudioSettingsWindow(AudioEngine& engine)
        : DocumentWindow("Liberty - Audio Settings", juce::Colour(0xff15181d), DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new juce::AudioDeviceSelectorComponent(engine.getDeviceManager(), 0, 2, 1, 2, false, true, true, false), true);
        setResizable(true, true);
        centreWithSize(620, 500);
        setVisible(false);
    }
    void closeButtonPressed() override { setVisible(false); }
private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSettingsWindow)
};

MainComponent::MainComponent()
{
    setSize(1440, 820);
    audioEngine.initialise();
    setWantsKeyboardFocus(true);
    startTimerHz(30);
}
MainComponent::~MainComponent() = default;

void MainComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    g.fillAll(juce::Colour(0xff0b0d10));
    auto transport = bounds.removeFromTop(76);
    auto mixer = bounds.removeFromBottom(210);
    drawTransport(g, transport);
    drawTrackArea(g, bounds);
    drawMixer(g, mixer);
}

void MainComponent::drawTransport(juce::Graphics& g, juce::Rectangle<int> area)
{
    // LIBERTY UI RULE: every control and every text element gets its own explicit,
    // non-overlapping rectangle. Never paint text underneath an interactive control.
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

    // Musical position has its own dedicated framed rectangle immediately to the right
    // of the 4/4 control, matching its visual treatment and never overlapping another control.
    const auto positionBox = juce::Rectangle<int>(728, 34, 90, 36);
    g.setColour(juce::Colour(0xff252a31));
    g.fillRoundedRectangle(positionBox.toFloat(), 5.0f);
    g.setColour(juce::Colour(0xff454b54));
    g.drawRoundedRectangle(positionBox.toFloat(), 5.0f, 1.0f);
    g.setColour(juce::Colour(0xffc9cdd3));
    g.setFont(juce::Font(14.0f));
    g.drawText(juce::String(measure) + ":" + juce::String(beat), positionBox, juce::Justification::centred);

    auto settingsButton = juce::Rectangle<int>(925, 10, 120, 24);
    auto importButton = juce::Rectangle<int>(1055, 10, 120, 24);
    for (auto r : { settingsButton, importButton }) { g.setColour(juce::Colour(0xff252a31)); g.fillRoundedRectangle(r.toFloat(), 5.0f); g.setColour(juce::Colour(0xff454b54)); g.drawRoundedRectangle(r.toFloat(), 5.0f, 1.0f); }
    g.setColour(juce::Colours::white); g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.drawText("AUDIO SETTINGS", settingsButton, juce::Justification::centred); g.drawText("IMPORT TO TRACK", importButton, juce::Justification::centred);
}

void MainComponent::drawTrackArea(juce::Graphics& g, juce::Rectangle<int> area)
{
    constexpr int headerW = 210, rulerH = 32, rowH = 70;
    constexpr float pixelsPerSecond = 80.0f;
    const double secondsPerBeat = 60.0 / juce::jmax(1.0, tempoBpm) * (4.0 / (double) juce::jmax(1, timeSignatureDenominator));
    const double secondsPerMeasure = secondsPerBeat * (double) juce::jmax(1, timeSignatureNumerator);
    const float pixelsPerMeasure = static_cast<float>(secondsPerMeasure * pixelsPerSecond);

    auto ruler = area.removeFromTop(rulerH); auto rows = area;
    g.setColour(juce::Colour(0xff12151a)); g.fillRect(ruler); g.setColour(juce::Colour(0xff20242b)); g.fillRect(rows.withWidth(headerW)); g.setColour(juce::Colour(0xff111419)); g.fillRect(rows.withTrimmedLeft(headerW));

    // LIBERTY TIMELINE RULE: ruler measures and playhead use the exact same time-to-pixel mapping.
    // This prevents any visual offset between the musical grid and the transport position.
    g.setColour(juce::Colour(0xff353b44));
    for (int measureIndex = 0; measureIndex < 100; ++measureIndex)
    {
        const int x = headerW + static_cast<int>(std::round(measureIndex * pixelsPerMeasure));
        if (x >= getWidth()) break;
        g.drawVerticalLine(x, (float)ruler.getY(), (float)rows.getBottom());
    }

    g.setColour(juce::Colour(0xff777f89)); g.setFont(juce::Font(11.0f));
    for (int i = 0; i < 100; ++i)
    {
        const int x = headerW + static_cast<int>(std::round(i * pixelsPerMeasure));
        if (x >= getWidth()) break;
        g.drawText(juce::String(i + 1), x + 6, ruler.getY() + 7, 35, 18, juce::Justification::left);
    }

    for (int i = 0; i < AudioEngine::maxAudioTracks; ++i)
    {
        auto row = rows.removeFromTop(rowH); g.setColour(i % 2 ? juce::Colour(0xff14171c) : juce::Colour(0xff171a1f)); g.fillRect(row);
        auto header = row.removeFromLeft(headerW); g.setColour(i == selectedTrack ? juce::Colour(0xff263746) : juce::Colour(0xff1e232a)); g.fillRect(header);
        g.setColour(juce::Colours::white); g.setFont(juce::Font(14.0f, juce::Font::bold)); g.drawText("Audio " + juce::String(i + 1), header.getX() + 14, header.getY() + 8, 150, 22, juce::Justification::left);
        g.setColour(i == selectedTrack ? juce::Colour(0xff9fc7e8) : juce::Colour(0xff747b85)); g.setFont(juce::Font(10.0f)); g.drawText(audioEngine.hasAudioFile(i) ? audioEngine.getAudioFileName(i) : "EMPTY AUDIO TRACK", header.getX() + 14, header.getY() + 36, 182, 16, juce::Justification::left, true);
        auto clip = row.withTrimmedLeft(20).reduced(4);
        if (audioEngine.hasAudioFile(i))
        {
            const auto desiredWidth = juce::jmax(1, static_cast<int>(std::round(audioEngine.getAudioFileLengthSeconds(i) * pixelsPerSecond)));
            clip.setWidth(desiredWidth);
            clip.setX(headerW + static_cast<int>(std::round(audioEngine.getTrackStartSeconds(i) * pixelsPerSecond)));
            g.setColour(i == selectedTrack ? juce::Colour(0xff31506a) : juce::Colour(0xff294459)); g.fillRoundedRectangle(clip.toFloat(), 5.0f); g.setColour(juce::Colour(0xff709fc5)); g.drawRoundedRectangle(clip.toFloat(), 5.0f, 1.0f);
            if (!waveformMin[(size_t)i].empty())
            {
                const auto centreY = clip.getCentreY(); const auto amplitude = juce::jmax(1.0f, clip.getHeight() * 0.42f); const auto points = static_cast<int>(waveformMin[(size_t)i].size()); juce::Path waveform; waveform.preallocateSpace(points * 4);
                for (int p = 0; p < points; ++p) { const auto x = clip.getX() + 4.0f + (clip.getWidth() - 8.0f) * (float)p / (float)juce::jmax(1, points - 1); const auto y = (float)centreY - waveformMax[(size_t)i][(size_t)p] * amplitude; if (p == 0) waveform.startNewSubPath(x, y); else waveform.lineTo(x, y); }
                for (int p = points - 1; p >= 0; --p) { const auto x = clip.getX() + 4.0f + (clip.getWidth() - 8.0f) * (float)p / (float)juce::jmax(1, points - 1); waveform.lineTo(x, (float)centreY - waveformMin[(size_t)i][(size_t)p] * amplitude); }
                waveform.closeSubPath(); g.setColour(juce::Colour(0xff9fc7e8)); g.fillPath(waveform);
            }
            g.setColour(juce::Colours::white); g.setFont(juce::Font(11.0f)); g.drawText(audioEngine.getAudioFileName(i), clip.reduced(10), juce::Justification::centredLeft, true);
            if (i == selectedTrack && clip.getWidth() >= 110) { g.setColour(juce::Colour(0xffb9d9f0)); g.setFont(juce::Font(9.0f)); g.drawText("DRAG TO MOVE", clip.getX() + 8, clip.getBottom() - 16, 90, 12, juce::Justification::left); }
        }
        else
        {
            g.setColour(juce::Colour(0xff242a31)); g.fillRoundedRectangle(clip.toFloat(), 5.0f); g.setColour(juce::Colour(0xff505862)); g.drawRoundedRectangle(clip.toFloat(), 5.0f, 1.0f); g.setColour(juce::Colour(0xff707780)); g.setFont(juce::Font(11.0f)); g.drawText("Select this track, then IMPORT AUDIO", clip, juce::Justification::centred);
        }
    }
    auto midiRow = rows.removeFromTop(rowH); auto instrumentRow = rows.removeFromTop(rowH); for (auto row : { midiRow, instrumentRow }) { g.setColour(juce::Colour(0xff14171c)); g.fillRect(row); }
    g.setColour(juce::Colour(0xff1e232a)); g.fillRect(midiRow.removeFromLeft(headerW)); g.fillRect(instrumentRow.removeFromLeft(headerW));
    g.setColour(juce::Colours::white); g.setFont(juce::Font(14.0f, juce::Font::bold)); g.drawText("MIDI 1", 14, midiRow.getY() + 8, 150, 22, juce::Justification::left); g.drawText("Instrument 1", 14, instrumentRow.getY() + 8, 150, 22, juce::Justification::left);
    g.setColour(juce::Colour(0xff747b85)); g.setFont(juce::Font(10.0f)); g.drawText("MIDI", 14, midiRow.getY() + 36, 150, 16, juce::Justification::left); g.drawText("INSTRUMENT", 14, instrumentRow.getY() + 36, 150, 16, juce::Justification::left);
    const float playheadX = headerW + (float)playheadSeconds * pixelsPerSecond; if (playheadX >= headerW && playheadX <= (float)getWidth()) { g.setColour(juce::Colours::white); g.drawLine(playheadX, (float)ruler.getY(), playheadX, (float)area.getBottom(), 2.0f); }
}

void MainComponent::drawMixer(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(juce::Colour(0xff101318)); g.fillRect(area);
    for (int i = 0; i < AudioEngine::maxAudioTracks + 1; ++i)
    {
        auto c = juce::Rectangle<int>(220 + i * 125, area.getY() + 12, 116, area.getHeight() - 22); const bool master = i == AudioEngine::maxAudioTracks;
        g.setColour(master ? juce::Colour(0xff1b2027) : juce::Colour(0xff171b20)); g.fillRoundedRectangle(c.toFloat(), 5.0f); g.setColour(juce::Colour(0xff343a44)); g.drawRoundedRectangle(c.toFloat(), 5.0f, 1.0f);
        const bool muted = !master && audioEngine.isTrackMuted(i); const bool solo = !master && audioEngine.isTrackSolo(i); g.setColour(juce::Colours::white); g.setFont(juce::Font(12.0f, juce::Font::bold)); g.drawText(master ? "MASTER" : "Audio " + juce::String(i + 1), c.getX(), c.getY() + 8, c.getWidth(), 20, juce::Justification::centred);
        if (!master) { auto mute = juce::Rectangle<int>(c.getX() + 8, c.getY() + 32, 44, 20); auto soloButton = juce::Rectangle<int>(c.getX() + 58, c.getY() + 32, 44, 20); g.setColour(muted ? juce::Colour(0xff9b4545) : juce::Colour(0xff252a31)); g.fillRoundedRectangle(mute.toFloat(), 4.0f); g.setColour(solo ? juce::Colour(0xff8b7a32) : juce::Colour(0xff252a31)); g.fillRoundedRectangle(soloButton.toFloat(), 4.0f); g.setColour(juce::Colour(0xff454b54)); g.drawRoundedRectangle(mute.toFloat(), 4.0f, 1.0f); g.drawRoundedRectangle(soloButton.toFloat(), 4.0f, 1.0f); g.setColour(juce::Colours::white); g.setFont(juce::Font(9.0f, juce::Font::bold)); g.drawText("M", mute, juce::Justification::centred); g.drawText("S", soloButton, juce::Justification::centred); }
        const int faderTop = c.getY() + 58, faderBottom = c.getBottom() - 45; auto fader = juce::Rectangle<float>((float)c.getCentreX() - 7.0f, (float)faderTop, 14.0f, (float)(faderBottom - faderTop)); g.setColour(juce::Colour(0xff090b0e)); g.fillRoundedRectangle(fader, 3.0f);
        const float gain = master ? audioEngine.getMasterGain() : audioEngine.getTrackGain(i); const auto normalized = juce::jlimit(0.0f, 1.0f, gain * 0.5f); const auto knobY = fader.getBottom() - normalized * fader.getHeight(); g.setColour(juce::Colour(0xffd6d9de)); g.fillRoundedRectangle(fader.getX() - 2.0f, knobY - 6.0f, fader.getWidth() + 4.0f, 12.0f, 3.0f);
        const auto db = 20.0f * std::log10(juce::jmax(0.000001f, gain)); g.setColour(juce::Colour(0xff858c96)); g.setFont(juce::Font(10.0f)); g.drawText(db < -59.9f ? "-inf dB" : juce::String(db, 1) + " dB", c.getX(), c.getBottom() - 38, c.getWidth(), 16, juce::Justification::centred); g.drawText(master ? "MASTER" : "PAN " + juce::String(audioEngine.getTrackPan(i), 2), c.getX(), c.getBottom() - 22, c.getWidth(), 16, juce::Justification::centred);
    }
}

void MainComponent::resized() { repaint(); }

void MainComponent::timerCallback()
{
    playheadSeconds = audioEngine.getCurrentTimeSeconds();
    isPlaying = audioEngine.isPlaying();
    repaint();
}

void MainComponent::openAudioSettings()
{
    if (audioSettingsWindow == nullptr) audioSettingsWindow = std::make_unique<AudioSettingsWindow>(audioEngine);
    audioSettingsWindow->setVisible(true);
    audioSettingsWindow->toFront(true);
}

void MainComponent::rebuildWaveformCache(int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= AudioEngine::maxAudioTracks) return;
    waveformMin[(size_t)trackIndex].clear(); waveformMax[(size_t)trackIndex].clear();
    const auto* buffer = audioEngine.getAudioBuffer(trackIndex);
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

void MainComponent::openAudioFile()
{
    const int trackToLoad = selectedTrack;
    audioFileChooser = std::make_unique<juce::FileChooser>("Import audio into Audio " + juce::String(trackToLoad + 1), juce::File{}, "*.wav;*.aif;*.aiff");
    audioFileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, trackToLoad](const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult(); if (!file.existsAsFile()) return;
            juce::String error;
            if (!audioEngine.loadAudioFileIntoTrack(trackToLoad, file, error)) { juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Audio Import", error, "OK"); return; }
            selectedTrack = trackToLoad; isPlaying = false; playheadSeconds = 0.0; rebuildWaveformCache(trackToLoad); repaint();
        });
}

int MainComponent::getAudioTrackAtPosition(juce::Point<int> position) const
{
    constexpr int rulerH = 32, rowH = 70;
    const int y = position.y - 76 - rulerH; if (y < 0) return -1;
    const int track = y / rowH; return track >= 0 && track < AudioEngine::maxAudioTracks ? track : -1;
}

bool MainComponent::isPointInsideAudioClip(int trackIndex, juce::Point<int> position) const
{
    if (trackIndex < 0 || trackIndex >= AudioEngine::maxAudioTracks || !audioEngine.hasAudioFile(trackIndex)) return false;
    constexpr int headerW = 210, rulerH = 32, rowH = 70; constexpr float pixelsPerSecond = 80.0f;
    const int rowY = 76 + rulerH + trackIndex * rowH;
    const int x = headerW + static_cast<int>(std::round(audioEngine.getTrackStartSeconds(trackIndex) * pixelsPerSecond));
    const int width = juce::jmax(1, static_cast<int>(std::round(audioEngine.getAudioFileLengthSeconds(trackIndex) * pixelsPerSecond)));
    return juce::Rectangle<int>(x, rowY + 4, width, rowH - 8).contains(position);
}

bool MainComponent::handleMixerMouse(const juce::MouseEvent& event)
{
    const int mixerTop = getHeight() - 210; if (event.position.y < mixerTop) return false;
    for (int i = 0; i < AudioEngine::maxAudioTracks + 1; ++i)
    {
        auto c = juce::Rectangle<int>(220 + i * 125, mixerTop + 12, 116, 188); if (!c.contains(event.getPosition())) continue;
        if (i < AudioEngine::maxAudioTracks)
        {
            auto mute = juce::Rectangle<int>(c.getX() + 8, c.getY() + 32, 44, 20); auto solo = juce::Rectangle<int>(c.getX() + 58, c.getY() + 32, 44, 20);
            const bool isMouseDown = event.mouseDownPosition.toInt() == event.getPosition();
            if (isMouseDown && mute.contains(event.getPosition())) { audioEngine.setTrackMuted(i, !audioEngine.isTrackMuted(i)); repaint(); return true; }
            if (isMouseDown && solo.contains(event.getPosition())) { audioEngine.setTrackSolo(i, !audioEngine.isTrackSolo(i)); repaint(); return true; }
        }
        const int faderTop = c.getY() + 58, faderBottom = c.getBottom() - 45;
        if (event.position.y >= faderTop && event.position.y <= faderBottom)
        {
            const float n = juce::jlimit(0.0f, 1.0f, (float)(faderBottom - event.position.y) / (float)juce::jmax(1, faderBottom - faderTop));
            const float gain = n * 2.0f; if (i == AudioEngine::maxAudioTracks) audioEngine.setMasterGain(gain); else audioEngine.setTrackGain(i, gain); repaint(); return true;
        }
        if (i < AudioEngine::maxAudioTracks && event.position.y >= c.getBottom() - 28)
        {
            const float pan = juce::jlimit(-1.0f, 1.0f, ((float)event.position.x - (float)c.getCentreX()) / 45.0f); audioEngine.setTrackPan(i, pan); repaint(); return true;
        }
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
        for (int i = 0; i < AudioEngine::maxAudioTracks; ++i)
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
    if (juce::Rectangle<int>(1055, 10, 120, 24).contains(p)) { openAudioFile(); return; }

    constexpr int headerW = 210, rulerH = 32;
    if (p.y >= 76 && p.y < 76 + rulerH && p.x >= headerW)
    {
        const double rawTime = juce::jmax(0.0, (double)(p.x - headerW) / 80.0);
        const double snappedTime = std::round(rawTime / secondsPerMeasure) * secondsPerMeasure;
        audioEngine.setCurrentTimeSeconds(snappedTime);
        playheadSeconds = snappedTime;
        repaint();
        return;
    }

    const int track = getAudioTrackAtPosition(p);
    if (track >= 0)
    {
        selectedTrack = track;
        if (isPointInsideAudioClip(track, p))
        {
            draggingClip = true;
            draggedTrack = track;
            dragStartMouseX = (float)p.x;
            dragStartSeconds = audioEngine.getTrackStartSeconds(track);
        }
        repaint();
        return;
    }

    if (p.y >= 76 && p.y < getHeight() - 210 && p.x >= headerW)
    {
        const double rawTime = juce::jmax(0.0, (double)(p.x - headerW) / 80.0);
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
        constexpr float pixelsPerSecond = 80.0f;
        const double deltaSeconds = ((double)event.position.x - (double)dragStartMouseX) / pixelsPerSecond;
        audioEngine.setTrackStartSeconds(draggedTrack, juce::jmax(0.0, dragStartSeconds + deltaSeconds)); repaint(); return;
    }
    handleMixerMouse(event);
}
