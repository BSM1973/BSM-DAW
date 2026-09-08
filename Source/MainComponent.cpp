#include "MainComponent.h"
#include <algorithm>
#include <cmath>

class MainComponent::AudioSettingsWindow final : public juce::DocumentWindow
{
public:
    explicit AudioSettingsWindow(AudioEngine& engine)
        : DocumentWindow("BSM DAW - Audio Settings", juce::Colour(0xff15181d), DocumentWindow::closeButton)
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
    startTimerHz(30);
}

MainComponent::~MainComponent() = default;

void MainComponent::paint(juce::Graphics& g)
{
    auto b = getLocalBounds();
    g.fillAll(juce::Colour(0xff0b0d10));
    auto transport = b.removeFromTop(76);
    auto mixer = b.removeFromBottom(210);
    drawTransport(g, transport);
    drawTrackArea(g, b);
    drawMixer(g, mixer);
}

void MainComponent::drawTransport(juce::Graphics& g, juce::Rectangle<int> a)
{
    g.setColour(juce::Colour(0xff15181d)); g.fillRect(a);
    g.setColour(juce::Colour(0xff30353d)); g.drawHorizontalLine(a.getBottom() - 1, 0.0f, (float)getWidth());
    g.setColour(juce::Colours::white); g.setFont(juce::Font(24.0f, juce::Font::bold));
    g.drawText("BSM DAW", 22, 10, 170, 28, juce::Justification::left);

    const char* labels[] = { "|<", "<", "PLAY", ">", "|>" };
    for (int i = 0; i < 5; ++i)
    {
        auto r = juce::Rectangle<int>(215 + i * 62, 38, 56, 28);
        g.setColour(i == 2 && isPlaying ? juce::Colour(0xff2d965e) : juce::Colour(0xff252a31)); g.fillRoundedRectangle(r.toFloat(), 5.0f);
        g.setColour(juce::Colour(0xff454b54)); g.drawRoundedRectangle(r.toFloat(), 5.0f, 1.0f);
        g.setColour(juce::Colours::white); g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(i == 2 && isPlaying ? "STOP" : labels[i], r, juce::Justification::centred);
    }
    g.setFont(juce::Font(14.0f)); g.setColour(juce::Colour(0xffc9cdd3));
    g.drawText("120.00 BPM", 560, 40, 110, 24, juce::Justification::centred);
    g.drawText("4/4", 680, 40, 50, 24, juce::Justification::centred);
    g.drawText(juce::String(playheadSeconds, 3) + " s", 750, 40, 160, 24, juce::Justification::centred);

    auto settingsButton = juce::Rectangle<int>(925, 10, 120, 24);
    auto importButton = juce::Rectangle<int>(1055, 10, 120, 24);
    for (auto r : { settingsButton, importButton })
    {
        g.setColour(juce::Colour(0xff252a31)); g.fillRoundedRectangle(r.toFloat(), 5.0f);
        g.setColour(juce::Colour(0xff454b54)); g.drawRoundedRectangle(r.toFloat(), 5.0f, 1.0f);
    }
    g.setColour(juce::Colours::white); g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.drawText("AUDIO SETTINGS", settingsButton, juce::Justification::centred);
    g.drawText("IMPORT AUDIO", importButton, juce::Justification::centred);
    g.setFont(juce::Font(10.0f));
    const auto deviceStatus = audioEngine.isInitialised() ? audioEngine.getDeviceName() : "AUDIO NOT AVAILABLE";
    g.setColour(audioEngine.isInitialised() ? juce::Colour(0xff72c58e) : juce::Colour(0xffd06b6b));
    g.drawText(deviceStatus, 925, 39, 300, 16, juce::Justification::left, true);
    g.setColour(juce::Colour(0xff858c96));
    g.drawText(juce::String(audioEngine.getSampleRate(), 0) + " Hz  •  " + juce::String(audioEngine.getBufferSize()) + " samples", 925, 55, 250, 16, juce::Justification::left);
    g.setFont(juce::Font(12.0f)); g.drawText("PROJECT  •  Untitled", getWidth() - 230, 40, 205, 24, juce::Justification::right);
}

void MainComponent::drawTrackArea(juce::Graphics& g, juce::Rectangle<int> area)
{
    constexpr int headerW = 210, rulerH = 32, rowH = 78;
    constexpr float pixelsPerSecond = 80.0f;
    auto ruler = area.removeFromTop(rulerH); auto rows = area;
    g.setColour(juce::Colour(0xff12151a)); g.fillRect(ruler);
    g.setColour(juce::Colour(0xff20242b)); g.fillRect(rows.withWidth(headerW));
    g.setColour(juce::Colour(0xff111419)); g.fillRect(rows.withTrimmedLeft(headerW));
    g.setColour(juce::Colour(0xff353b44));
    for (int x = headerW; x < getWidth(); x += 120) g.drawVerticalLine(x, (float)ruler.getY(), (float)rows.getBottom());
    g.setColour(juce::Colour(0xff777f89)); g.setFont(juce::Font(11.0f));
    for (int i = 0; i < 12; ++i) g.drawText(juce::String(i + 1), headerW + i * 120 + 6, ruler.getY() + 7, 35, 18, juce::Justification::left);

    const char* names[] = { "Audio 1", "Audio 2", "MIDI 1", "Instrument 1" };
    const char* types[] = { "AUDIO", "AUDIO", "MIDI", "INSTRUMENT" };
    for (int i = 0; i < 4; ++i)
    {
        auto row = rows.removeFromTop(rowH);
        g.setColour(i % 2 ? juce::Colour(0xff14171c) : juce::Colour(0xff171a1f)); g.fillRect(row);
        auto h = row.removeFromLeft(headerW); g.setColour(juce::Colour(0xff1e232a)); g.fillRect(h);
        g.setColour(juce::Colours::white); g.setFont(juce::Font(14.0f, juce::Font::bold)); g.drawText(names[i], h.getX() + 14, h.getY() + 10, 150, 22, juce::Justification::left);
        g.setColour(juce::Colour(0xff747b85)); g.setFont(juce::Font(10.0f)); g.drawText(types[i], h.getX() + 14, h.getY() + 37, 150, 16, juce::Justification::left);
        auto clip = row.withTrimmedLeft(20 + i * 130).withWidth(250 + i * 35).reduced(4);
        if (i == 0 && audioEngine.hasAudioFile())
        {
            const auto desiredWidth = static_cast<int>(std::ceil(audioEngine.getAudioFileLengthSeconds() * pixelsPerSecond)) + 8;
            clip.setWidth(juce::jlimit(242, 1000, desiredWidth));
        }
        g.setColour(juce::Colour(0xff31506a)); g.fillRoundedRectangle(clip.toFloat(), 5.0f);
        g.setColour(juce::Colour(0xff709fc5)); g.drawRoundedRectangle(clip.toFloat(), 5.0f, 1.0f);
        if (i == 0 && audioEngine.hasAudioFile() && !waveformMin.empty())
        {
            const auto centreY = clip.getCentreY(); const auto amplitude = juce::jmax(1.0f, clip.getHeight() * 0.42f);
            juce::Path waveform; const auto points = static_cast<int>(waveformMin.size()); waveform.preallocateSpace(points * 4);
            for (int p = 0; p < points; ++p)
            {
                const auto x = clip.getX() + 4.0f + (clip.getWidth() - 8.0f) * (float)p / (float)(points - 1);
                const auto y = (float)centreY - waveformMax[(size_t)p] * amplitude;
                if (p == 0) waveform.startNewSubPath(x, y); else waveform.lineTo(x, y);
            }
            for (int p = points - 1; p >= 0; --p)
            {
                const auto x = clip.getX() + 4.0f + (clip.getWidth() - 8.0f) * (float)p / (float)(points - 1);
                waveform.lineTo(x, (float)centreY - waveformMin[(size_t)p] * amplitude);
            }
            waveform.closeSubPath(); g.setColour(juce::Colour(0xff9fc7e8)); g.fillPath(waveform);
        }
        g.setColour(juce::Colours::white); g.setFont(juce::Font(11.0f));
        g.drawText((i == 0 && audioEngine.hasAudioFile()) ? audioEngine.getAudioFileName() : (i == 2 ? "MIDI Region" : "Audio Clip"), clip.reduced(10), juce::Justification::centredLeft, true);
    }
    const float px = headerW + (float)playheadSeconds * pixelsPerSecond;
    if (px >= headerW && px <= (float)getWidth()) { g.setColour(juce::Colours::white); g.drawLine(px, (float)ruler.getY(), px, (float)area.getBottom(), 2.0f); }
}

void MainComponent::drawMixer(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(juce::Colour(0xff101318)); g.fillRect(area);
    const char* names[] = { "Audio 1", "Audio 2", "MIDI 1", "Instrument", "MASTER" };
    for (int i = 0; i < 5; ++i)
    {
        auto c = juce::Rectangle<int>(220 + i * 125, area.getY() + 12, 116, area.getHeight() - 22);
        g.setColour(i == 4 ? juce::Colour(0xff1b2027) : juce::Colour(0xff171b20)); g.fillRoundedRectangle(c.toFloat(), 5.0f);
        g.setColour(juce::Colour(0xff343a44)); g.drawRoundedRectangle(c.toFloat(), 5.0f, 1.0f);
        g.setColour(juce::Colours::white); g.setFont(juce::Font(12.0f, juce::Font::bold)); g.drawText(names[i], c.getX(), c.getY() + 8, c.getWidth(), 20, juce::Justification::centred);
        if (i < 4)
        {
            auto mute = juce::Rectangle<int>(c.getX() + 10, c.getY() + 32, 42, 20);
            g.setColour(i == 0 && audioEngine.isTrackMuted() ? juce::Colour(0xff9b4545) : juce::Colour(0xff252a31)); g.fillRoundedRectangle(mute.toFloat(), 4.0f);
            g.setColour(juce::Colour(0xff454b54)); g.drawRoundedRectangle(mute.toFloat(), 4.0f, 1.0f);
            g.setColour(juce::Colours::white); g.setFont(juce::Font(9.0f, juce::Font::bold)); g.drawText("M", mute, juce::Justification::centred);
        }
        auto fader = juce::Rectangle<float>((float)c.getCentreX() - 7.0f, (float)c.getY() + 58.0f, 14.0f, 90.0f);
        g.setColour(juce::Colour(0xff090b0e)); g.fillRoundedRectangle(fader, 3.0f);
        float normalized = 0.5f;
        if (i == 0) normalized = audioEngine.getTrackGain() * 0.5f;
        if (i == 4) normalized = audioEngine.getMasterGain() * 0.5f;
        normalized = juce::jlimit(0.0f, 1.0f, normalized);
        const auto knobY = fader.getBottom() - normalized * fader.getHeight();
        g.setColour(juce::Colour(0xffd6d9de)); g.fillRoundedRectangle(fader.getX() - 2.0f, knobY - 6.0f, fader.getWidth() + 4.0f, 12.0f, 3.0f);
        g.setColour(juce::Colour(0xff858c96)); g.setFont(juce::Font(10.0f));
        if (i == 0) g.drawText(juce::String(20.0f * std::log10(juce::jmax(0.0001f, audioEngine.getTrackGain())), 1) + " dB", c.getX(), c.getBottom() - 38, c.getWidth(), 16, juce::Justification::centred);
        else if (i == 4) g.drawText(juce::String(20.0f * std::log10(juce::jmax(0.0001f, audioEngine.getMasterGain())), 1) + " dB", c.getX(), c.getBottom() - 38, c.getWidth(), 16, juce::Justification::centred);
        else g.drawText("-6.0 dB", c.getX(), c.getBottom() - 38, c.getWidth(), 16, juce::Justification::centred);
        g.drawText(i == 0 ? ("PAN " + juce::String(audioEngine.getTrackPan(), 2)) : "PAN 0.00", c.getX(), c.getBottom() - 22, c.getWidth(), 16, juce::Justification::centred);
    }
}

void MainComponent::resized() {}

void MainComponent::rebuildWaveformCache()
{
    waveformMin.clear(); waveformMax.clear();
    const auto* buffer = audioEngine.getAudioBuffer();
    if (buffer == nullptr || buffer->getNumSamples() <= 0 || buffer->getNumChannels() <= 0) return;
    constexpr int numPoints = 1200;
    waveformMin.resize(numPoints, 0.0f); waveformMax.resize(numPoints, 0.0f);
    const auto totalSamples = buffer->getNumSamples(); const auto channels = buffer->getNumChannels();
    for (int point = 0; point < numPoints; ++point)
    {
        const auto start = static_cast<int>((static_cast<std::int64_t>(point) * totalSamples) / numPoints);
        const auto end = static_cast<int>((static_cast<std::int64_t>(point + 1) * totalSamples) / numPoints);
        const auto safeEnd = juce::jmax(start + 1, end); float minValue = 0.0f, maxValue = 0.0f;
        for (int sample = start; sample < safeEnd && sample < totalSamples; ++sample)
            for (int channel = 0; channel < channels; ++channel) { const auto value = buffer->getSample(channel, sample); minValue = std::min(minValue, value); maxValue = std::max(maxValue, value); }
        waveformMin[(size_t)point] = juce::jlimit(-1.0f, 1.0f, minValue); waveformMax[(size_t)point] = juce::jlimit(-1.0f, 1.0f, maxValue);
    }
}

void MainComponent::openAudioSettings()
{
    if (audioSettingsWindow == nullptr) audioSettingsWindow = std::make_unique<AudioSettingsWindow>(audioEngine);
    audioSettingsWindow->centreWithSize(620, 500); audioSettingsWindow->setVisible(true); audioSettingsWindow->toFront(true);
}

void MainComponent::openAudioFile()
{
    audioFileChooser = std::make_unique<juce::FileChooser>("Import audio file", juce::File{}, "*.wav;*.aif;*.aiff");
    audioFileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult(); if (!file.existsAsFile()) return;
            juce::String error;
            if (!audioEngine.loadAudioFile(file, error)) { juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "BSM DAW - Audio Import", error); return; }
            isPlaying = false; playheadSeconds = 0.0; rebuildWaveformCache(); repaint();
        });
}

void MainComponent::mouseDown(const juce::MouseEvent& event)
{
    if (event.y >= 38 && event.y <= 66 && event.x >= 215 && event.x <= 271) { isPlaying = false; audioEngine.setPlaying(false); audioEngine.resetTransport(); playheadSeconds = 0.0; repaint(); return; }
    if (event.y >= 38 && event.y <= 66 && event.x >= 339 && event.x <= 395) { isPlaying = !isPlaying; audioEngine.setPlaying(isPlaying); repaint(); return; }
    if (event.x >= 925 && event.x <= 1045 && event.y >= 10 && event.y <= 34) { openAudioSettings(); return; }
    if (event.x >= 1055 && event.x <= 1175 && event.y >= 10 && event.y <= 34) { openAudioFile(); return; }

    constexpr int headerW = 210; constexpr int mixerTop = 610;
    if (audioEngine.hasAudioFile() && event.x >= headerW && event.y >= 76 && event.y < 186)
    {
        audioEngine.setCurrentTimeSeconds(juce::jmax(0.0, (double)(event.x - headerW) / 80.0));
        playheadSeconds = audioEngine.getCurrentTimeSeconds(); repaint(); return;
    }
    if (event.y >= mixerTop + 12 && event.y <= getHeight() - 10)
    {
        for (int i = 0; i < 5; ++i)
        {
            const int x = 220 + i * 125; const int y = mixerTop + 12;
            if (event.x < x || event.x > x + 116) continue;
            if (i == 0 && event.y >= y + 32 && event.y <= y + 52) { audioEngine.setTrackMuted(!audioEngine.isTrackMuted()); repaint(); return; }
            if ((i == 0 || i == 4) && event.y >= y + 58 && event.y <= y + 148)
            {
                const auto normalized = juce::jlimit(0.0f, 1.0f, 1.0f - (float)(event.y - (y + 58)) / 90.0f);
                if (i == 0) audioEngine.setTrackGain(normalized * 2.0f); else audioEngine.setMasterGain(normalized * 2.0f);
                repaint(); return;
            }
            if (i == 0 && event.y >= y + 148 && event.y <= y + 174)
            {
                audioEngine.setTrackPan(juce::jlimit(-1.0f, 1.0f, ((float)event.x - (x + 58.0f)) / 58.0f)); repaint(); return;
            }
        }
    }
}

void MainComponent::timerCallback()
{
    playheadSeconds = audioEngine.getCurrentTimeSeconds(); isPlaying = audioEngine.isPlaying(); repaint();
}
