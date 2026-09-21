#define private public
#include "MainComponent.h"
#undef private
#include "PluginHost.h"
#include "OneKnobEffects.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>

int getLibertyTrackColourId(int track);
juce::String getLibertyTrackName(int track);
float getLibertyInstrumentGain() noexcept;
void setLibertyInstrumentGain(float value) noexcept;
float getLibertyInstrumentPan() noexcept;
void setLibertyInstrumentPan(float value) noexcept;

namespace
{
constexpr int transportHeight = 76;
constexpr int instrumentTrackIndex = AudioEngine::maxAudioTracks + 1;
constexpr int channelCount = 66; // compatibility capacity: 64 dynamic strips + master
constexpr int instrumentChannel = 64;
constexpr int masterChannel = 65;

juce::Colour trackColour(int id)
{
    static constexpr std::array<juce::uint32, 9> colours {
        0xff31506a, 0xff3b82f6, 0xff22c55e, 0xffeab308, 0xfff97316,
        0xffef4444, 0xffa855f7, 0xffec4899, 0xff14b8a6
    };
    return juce::Colour(colours[(size_t)juce::jlimit(0, 8, id)]);
}

float gainToDb(float gain)
{
    return gain <= 0.00001f ? -60.0f : juce::jlimit(-60.0f, 6.0f, juce::Decibels::gainToDecibels(gain));
}

float dbToGain(float db)
{
    return db <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain(db);
}

class ConsoleLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float, float,
                          const juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearVertical)
        {
            juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, 0.0f, 0.0f, style, slider);
            return;
        }

        const float cx = (float)x + (float)width * 0.5f;
        const auto rail = juce::Rectangle<float>(cx - 3.0f, (float)y + 4.0f, 6.0f, (float)height - 8.0f);
        g.setColour(juce::Colour(0xff090b0e));
        g.fillRoundedRectangle(rail, 3.0f);
        g.setColour(juce::Colour(0xff2f363f));
        g.drawRoundedRectangle(rail, 3.0f, 1.0f);

        const auto knob = juce::Rectangle<float>((float)x + 2.0f, sliderPos - 7.0f, (float)width - 4.0f, 14.0f);
        g.setColour(juce::Colour(0xffd8dde3));
        g.fillRoundedRectangle(knob, 3.0f);
        g.setColour(juce::Colour(0xff6b737e));
        g.drawRoundedRectangle(knob, 3.0f, 1.0f);
        g.setColour(juce::Colour(0xff24292f));
        g.drawHorizontalLine((int)sliderPos, knob.getX() + 4.0f, knob.getRight() - 4.0f);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider&) override
    {
        const auto size = (float)juce::jmin(width, height) - 3.0f;
        const auto cx = (float)x + (float)width * 0.5f;
        const auto cy = (float)y + (float)height * 0.5f;
        const auto radius = size * 0.5f;
        const auto bounds = juce::Rectangle<float>(cx - radius, cy - radius, size, size);
        const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        g.setColour(juce::Colour(0xff252c34));
        g.fillEllipse(bounds);
        g.setColour(juce::Colour(0xff56616d));
        g.drawEllipse(bounds, 1.5f);

        juce::Path pointer;
        pointer.addRoundedRectangle(-1.4f, -radius * 0.72f, 2.8f, radius * 0.66f, 1.2f);
        g.setColour(juce::Colour(0xffffad42));
        g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(cx, cy));
    }
};

class MixConsoleView final : public juce::Component,
                             private juce::Timer
{
public:
    explicit MixConsoleView(MainComponent& ownerIn) : owner(ownerIn)
    {
        setOpaque(true);
        setInterceptsMouseClicks(true, true);

        for (int ch = 0; ch < channelCount; ++ch)
        {
            auto& fader = faders[(size_t)ch];
            fader.setSliderStyle(juce::Slider::LinearVertical);
            fader.setRange(-60.0, 6.0, 0.1);
            fader.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 20);
            fader.setNumDecimalPlacesToDisplay(1);
            fader.setTextValueSuffix(" dB");
            fader.setLookAndFeel(&lookAndFeel);
            fader.setMouseClickGrabsKeyboardFocus(false);
            fader.onValueChange = [this, ch]
            {
                if (syncing) return;
                const float gain = dbToGain((float)faders[(size_t)ch].getValue());
                if (ch < AudioEngine::maxAudioTracks)
                    owner.audioEngine.setTrackGain(ch, gain);
                else if (ch == instrumentChannel)
                    setLibertyInstrumentGain(gain);
                else
                    owner.audioEngine.setMasterGain(gain);
                repaint();
            };
            addAndMakeVisible(fader);

            if (ch != masterChannel)
            {
                auto& pan = pans[(size_t)ch];
                pan.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                pan.setRange(-1.0, 1.0, 0.01);
                pan.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                pan.setDoubleClickReturnValue(true, 0.0);
                pan.setLookAndFeel(&lookAndFeel);
                pan.setMouseClickGrabsKeyboardFocus(false);
                pan.onValueChange = [this, ch]
                {
                    if (syncing) return;
                    const float value = (float)pans[(size_t)ch].getValue();
                    if (ch < AudioEngine::maxAudioTracks)
                        owner.audioEngine.setTrackPan(ch, value);
                    else
                        setLibertyInstrumentPan(value);
                    repaint();
                };
                addAndMakeVisible(pan);

                auto& mute = muteButtons[(size_t)ch];
                auto& solo = soloButtons[(size_t)ch];
                mute.setButtonText("M");
                solo.setButtonText("S");
                for (auto* b : { &mute, &solo })
                {
                    b->setMouseClickGrabsKeyboardFocus(false);
                    b->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
                    addAndMakeVisible(*b);
                }
                mute.onClick = [this, ch]
                {
                    if (ch < AudioEngine::maxAudioTracks)
                        owner.audioEngine.setTrackMuted(ch, !owner.audioEngine.isTrackMuted(ch));
                    else
                        owner.audioEngine.setInstrumentTrackMuted(!owner.audioEngine.isInstrumentTrackMuted());
                    syncButtons();
                    repaint();
                };
                solo.onClick = [this, ch]
                {
                    if (ch < AudioEngine::maxAudioTracks)
                        owner.audioEngine.setTrackSolo(ch, !owner.audioEngine.isTrackSolo(ch));
                    else
                        owner.audioEngine.setInstrumentTrackSolo(!owner.audioEngine.isInstrumentTrackSolo());
                    syncButtons();
                    repaint();
                };
            }

            auto& plugin = pluginButtons[(size_t)ch];
            plugin.setMouseClickGrabsKeyboardFocus(false);
            plugin.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff171c22));
            plugin.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd4d8de));
            plugin.onClick = [this, ch]
            {
                auto& host = LibertyPluginHost::instance();
                auto& oneKnob = LibertyOneKnobManager::instance();
                const int oneKnobSlot = ch == instrumentChannel ? 100000 : ch;
                if (ch != masterChannel && oneKnob.hasEffect(oneKnobSlot))
                    oneKnob.showEditor(oneKnobSlot);
                else if (ch < AudioEngine::maxAudioTracks && host.hasEffectForTrack(ch))
                    host.showEditorForTrack(ch);
                else if (ch == instrumentChannel && host.hasInstrument())
                    host.showInstrumentEditor();
            };
            addAndMakeVisible(plugin);

            auto& unload = unloadButtons[(size_t)ch];
            unload.setButtonText("×");
            unload.setMouseClickGrabsKeyboardFocus(false);
            unload.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff342126));
            unload.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            unload.onClick = [this, ch]
            {
                auto& host = LibertyPluginHost::instance();
                auto& oneKnob = LibertyOneKnobManager::instance();
                if (ch < AudioEngine::maxAudioTracks)
                {
                    if (oneKnob.hasEffect(ch)) oneKnob.clearEffect(ch);
                    else host.unloadEffectForTrack(ch);
                }
                else if (ch == instrumentChannel)
                {
                    const int slot = 100000;
                    if (oneKnob.hasEffect(slot)) oneKnob.clearEffect(slot);
                    else host.unloadInstrument();
                }
                refreshPluginLabels();
                repaint();
            };
            addAndMakeVisible(unload);
        }

        startTimerHz(24);
        setVisible(false);
        owner.addAndMakeVisible(this);
    }

    ~MixConsoleView() override
    {
        stopTimer();
        for (auto& fader : faders) fader.setLookAndFeel(nullptr);
        for (int ch = 0; ch < masterChannel; ++ch) pans[(size_t)ch].setLookAndFeel(nullptr);
    }

    void setConsoleVisible(bool shouldShow)
    {
        consoleVisible = shouldShow;
        setVisible(shouldShow);
        if (shouldShow)
        {
            setBounds(0, transportHeight, owner.getWidth(), juce::jmax(1, owner.getHeight() - transportHeight));
            toFront(false);
            resized();
            syncFromEngine();
            repaint();
        }
    }

    bool isConsoleVisible() const noexcept { return consoleVisible; }

    int getDropTrackForOwnerPoint(juce::Point<int> ownerPoint, bool instrumentPlugin) const
    {
        if (!consoleVisible || ownerPoint.y < transportHeight) return -1;
        const auto local = ownerPoint - juce::Point<int>(0, transportHeight);
        for (int ch = 0; ch < channelCount; ++ch)
        {
            if (!stripBounds[(size_t)ch].contains(local)) continue;
            if (instrumentPlugin)
                return ch == instrumentChannel ? instrumentTrackIndex : -1;
            return ch < AudioEngine::maxAudioTracks ? ch : -1;
        }
        return -1;
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0d1014));

        g.setColour(juce::Colour(0xff171c22));
        g.fillRect(0, 0, getWidth(), 54);
        g.setColour(juce::Colour(0xff303740));
        g.drawHorizontalLine(53, 0.0f, (float)getWidth());
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(20.0f, juce::Font::bold));
        g.drawText("MIXCONSOLE", 24, 10, 190, 28, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff8f98a3));
        g.setFont(juce::Font(10.0f));
        g.drawText("FADERS • PAN • MUTE/SOLO • METERS • INSERTS • MASTER", 215, 15, 430, 20, juce::Justification::centredLeft);

        for (int ch = 0; ch < channelCount; ++ch)
            drawStrip(g, ch, stripBounds[(size_t)ch]);
    }

    void resized() override
    {
        const int availableWidth = juce::jmax(720, getWidth() - 48);
        const int activeChannels = juce::jlimit(2, channelCount, owner.getAudioTrackCount() + owner.getInstrumentTrackCount() + 1);
        const int stripW = juce::jlimit(118, 188, availableWidth / activeChannels);
        const int totalW = stripW * channelCount;
        const int startX = juce::jmax(12, (getWidth() - totalW) / 2);
        const int top = 66;
        const int bottomMargin = 18;
        const int stripH = juce::jmax(480, getHeight() - top - bottomMargin);

        for (int ch = 0; ch < channelCount; ++ch)
        {
            const auto strip = juce::Rectangle<int>(startX + ch * stripW, top, stripW - 4, stripH);
            stripBounds[(size_t)ch] = strip;
            const int cx = strip.getCentreX();

            pluginButtons[(size_t)ch].setBounds(strip.getX() + 8, strip.getY() + 88, strip.getWidth() - 44, 28);
            unloadButtons[(size_t)ch].setBounds(strip.getRight() - 32, strip.getY() + 88, 24, 28);
            const bool hasUnload = ch != masterChannel;
            unloadButtons[(size_t)ch].setVisible(hasUnload);

            if (ch != masterChannel)
            {
                pans[(size_t)ch].setBounds(cx - 22, strip.getY() + 142, 44, 44);
                muteButtons[(size_t)ch].setBounds(cx - 46, strip.getY() + 200, 40, 26);
                soloButtons[(size_t)ch].setBounds(cx + 6, strip.getY() + 200, 40, 26);
            }

            const int faderTop = strip.getY() + 270;
            const int faderBottom = strip.getBottom() - 66;
            faders[(size_t)ch].setBounds(cx - 34, faderTop, 68, juce::jmax(150, faderBottom - faderTop));
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        for (int ch = 0; ch < channelCount; ++ch)
        {
            if (!stripBounds[(size_t)ch].contains(event.getPosition())) continue;
            if (ch < AudioEngine::maxAudioTracks)
                owner.selectedTrack = ch;
            else if (ch == instrumentChannel)
                owner.selectedTrack = instrumentTrackIndex;
            selectedConsoleChannel = ch;
            repaint();
            owner.repaint();
            return;
        }
    }

private:
    juce::String channelName(int ch) const
    {
        if (ch < AudioEngine::maxAudioTracks) return getLibertyTrackName(ch);
        if (ch == instrumentChannel) return getLibertyTrackName(instrumentTrackIndex);
        return "MASTER";
    }

    juce::Colour channelColour(int ch) const
    {
        if (ch < AudioEngine::maxAudioTracks) return trackColour(getLibertyTrackColourId(ch));
        if (ch == instrumentChannel) return trackColour(getLibertyTrackColourId(instrumentTrackIndex));
        return juce::Colour(0xffd8a94d);
    }

    bool isChannelSelected(int ch) const
    {
        if (selectedConsoleChannel == ch) return true;
        if (ch < AudioEngine::maxAudioTracks) return owner.selectedTrack == ch;
        if (ch == instrumentChannel) return owner.selectedTrack == instrumentTrackIndex;
        return false;
    }

    void drawStrip(juce::Graphics& g, int ch, juce::Rectangle<int> r)
    {
        const bool selected = isChannelSelected(ch);
        const auto colour = channelColour(ch);

        g.setColour(selected ? juce::Colour(0xff202b34) : juce::Colour(0xff15191f));
        g.fillRoundedRectangle(r.toFloat(), 6.0f);
        g.setColour(selected ? colour.brighter(0.30f) : juce::Colour(0xff343b45));
        g.drawRoundedRectangle(r.toFloat(), 6.0f, selected ? 2.0f : 1.0f);

        g.setColour(colour);
        g.fillRoundedRectangle((float)r.getX(), (float)r.getY(), (float)r.getWidth(), 6.0f, 3.0f);

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText(channelName(ch), r.getX() + 8, r.getY() + 14, r.getWidth() - 16, 20, juce::Justification::centred, true);

        g.setColour(juce::Colour(0xff8f98a3));
        g.setFont(juce::Font(8.5f, juce::Font::bold));
        const auto type = ch < AudioEngine::maxAudioTracks ? "AUDIO CHANNEL"
                         : (ch == instrumentChannel ? "VIRTUAL INSTRUMENT" : "STEREO MASTER");
        g.drawText(type, r.getX() + 5, r.getY() + 36, r.getWidth() - 10, 15, juce::Justification::centred, true);

        g.setColour(juce::Colour(0xff0f1317));
        g.fillRoundedRectangle((float)r.getX() + 8.0f, (float)r.getY() + 56.0f, (float)r.getWidth() - 16.0f, 24.0f, 3.0f);
        g.setColour(juce::Colour(0xffaab2bc));
        g.setFont(juce::Font(8.5f));
        const auto route = ch == masterChannel ? "OUT 1–2" : "ROUTE  →  MASTER";
        g.drawText(route, r.getX() + 10, r.getY() + 58, r.getWidth() - 20, 20, juce::Justification::centred, true);

        if (ch != masterChannel)
        {
            g.setColour(juce::Colour(0xff8f98a3));
            g.setFont(juce::Font(8.0f, juce::Font::bold));
            g.drawText("INSERT", r.getX() + 9, r.getY() + 83, r.getWidth() - 18, 12, juce::Justification::centredLeft);
            g.drawText("PAN", r.getX(), r.getY() + 126, r.getWidth(), 14, juce::Justification::centred);
        }
        else
        {
            g.setColour(juce::Colour(0xff8f98a3));
            g.setFont(juce::Font(8.0f, juce::Font::bold));
            g.drawText("MASTER BUS", r.getX(), r.getY() + 92, r.getWidth(), 18, juce::Justification::centred);
        }

        drawMeter(g, ch, r);

        g.setColour(juce::Colour(0xff747d88));
        g.setFont(juce::Font(8.0f));
        g.drawText("+6", r.getRight() - 30, r.getY() + 270, 22, 12, juce::Justification::right);
        g.drawText("0", r.getRight() - 30, r.getY() + 315, 22, 12, juce::Justification::right);
        g.drawText("-18", r.getRight() - 30, r.getY() + 400, 22, 12, juce::Justification::right);
        g.drawText("-60", r.getRight() - 30, r.getBottom() - 82, 22, 12, juce::Justification::right);
    }

    void drawMeter(juce::Graphics& g, int ch, juce::Rectangle<int> r)
    {
        const int top = r.getY() + 270;
        const int bottom = r.getBottom() - 86;
        if (bottom <= top) return;

        const auto meter = juce::Rectangle<float>((float)r.getX() + 10.0f, (float)top, 12.0f, (float)(bottom - top));
        g.setColour(juce::Colour(0xff080a0d));
        g.fillRoundedRectangle(meter, 2.0f);
        g.setColour(juce::Colour(0xff2c333b));
        g.drawRoundedRectangle(meter, 2.0f, 1.0f);

        const float level = juce::jlimit(0.0f, 1.2f, meterLevels[(size_t)ch]);
        const float db = level <= 0.00001f ? -60.0f : juce::jlimit(-60.0f, 6.0f, juce::Decibels::gainToDecibels(level));
        const float norm = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 66.0f);
        auto fill = meter;
        fill.setY(meter.getBottom() - meter.getHeight() * norm);
        fill.setHeight(meter.getHeight() * norm);

        const juce::ColourGradient grad(juce::Colour(0xff36c774), fill.getX(), fill.getBottom(),
                                        juce::Colour(0xffffb347), fill.getX(), fill.getY(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(fill, 2.0f);

        if (meterPeaks[(size_t)ch] > 0.98f)
        {
            g.setColour(juce::Colour(0xffff4e4e));
            g.fillRect((int)meter.getX(), (int)meter.getY(), (int)meter.getWidth(), 3);
        }
    }

    float calculateAudioTrackLevel(int track) const
    {
        if (!owner.audioEngine.isPlaying() || owner.audioEngine.isTrackMuted(track)) return 0.0f;
        const auto* buffer = owner.audioEngine.getAudioBuffer(track);
        const auto rate = owner.audioEngine.getSampleRate();
        if (buffer == nullptr || rate <= 0.0 || buffer->getNumSamples() <= 0) return 0.0f;

        const double localSeconds = owner.audioEngine.getCurrentTimeSeconds() - owner.audioEngine.getTrackStartSeconds(track);
        if (localSeconds < 0.0 || localSeconds >= owner.audioEngine.getAudioFileLengthSeconds(track)) return 0.0f;

        const int centre = juce::jlimit(0, buffer->getNumSamples() - 1, (int)std::llround(localSeconds * rate));
        const int start = juce::jmax(0, centre - 128);
        const int count = juce::jmin(256, buffer->getNumSamples() - start);
        float peak = 0.0f;
        for (int c = 0; c < juce::jmin(2, buffer->getNumChannels()); ++c)
        {
            const float* data = buffer->getReadPointer(c, start);
            for (int i = 0; i < count; ++i)
                peak = juce::jmax(peak, std::abs(data[i]));
        }
        return peak * owner.audioEngine.getTrackGain(track);
    }

    void updateMeters()
    {
        float masterEstimate = 0.0f;
        for (int track = 0; track < AudioEngine::maxAudioTracks; ++track)
        {
            const float raw = calculateAudioTrackLevel(track);
            meterPeaks[(size_t)track] = juce::jmax(raw, meterPeaks[(size_t)track] * 0.90f);
            meterLevels[(size_t)track] = juce::jmax(raw, meterLevels[(size_t)track] * 0.82f);
            masterEstimate += raw;
        }

        // Instrument audio is generated by the hosted plugin. Until the host exposes
        // a dedicated post-plugin meter tap, show silence rather than inventing a level.
        const float instrumentLevel = 0.0f;
        meterPeaks[(size_t)instrumentChannel] = juce::jmax(instrumentLevel, meterPeaks[(size_t)instrumentChannel] * 0.90f);
        meterLevels[(size_t)instrumentChannel] = juce::jmax(instrumentLevel, meterLevels[(size_t)instrumentChannel] * 0.82f);

        masterEstimate = juce::jmin(1.2f, masterEstimate * owner.audioEngine.getMasterGain());
        meterPeaks[(size_t)masterChannel] = juce::jmax(masterEstimate, meterPeaks[(size_t)masterChannel] * 0.90f);
        meterLevels[(size_t)masterChannel] = juce::jmax(masterEstimate, meterLevels[(size_t)masterChannel] * 0.82f);
    }

    void syncButtons()
    {
        for (int ch = 0; ch < masterChannel; ++ch)
        {
            const bool mute = ch < AudioEngine::maxAudioTracks
                ? owner.audioEngine.isTrackMuted(ch)
                : owner.audioEngine.isInstrumentTrackMuted();
            const bool solo = ch < AudioEngine::maxAudioTracks
                ? owner.audioEngine.isTrackSolo(ch)
                : owner.audioEngine.isInstrumentTrackSolo();

            muteButtons[(size_t)ch].setColour(juce::TextButton::buttonColourId,
                mute ? juce::Colour(0xffa44141) : juce::Colour(0xff252b32));
            soloButtons[(size_t)ch].setColour(juce::TextButton::buttonColourId,
                solo ? juce::Colour(0xffa18432) : juce::Colour(0xff252b32));
        }
    }

    void refreshPluginLabels()
    {
        auto& host = LibertyPluginHost::instance();
        auto& oneKnob = LibertyOneKnobManager::instance();
        for (int ch = 0; ch < juce::jmin(owner.getAudioTrackCount(), instrumentChannel); ++ch)
        {
            const bool oneKnobLoaded = oneKnob.hasEffect(ch);
            const bool externalLoaded = host.hasEffectForTrack(ch);
            juce::String label = "INSERT — EMPTY";
            if (oneKnobLoaded) label = oneKnob.getName(ch);
            else if (externalLoaded) label = host.getEffectName(ch);
            pluginButtons[(size_t)ch].setButtonText(label);
            unloadButtons[(size_t)ch].setEnabled(oneKnobLoaded || externalLoaded);
        }

        const int instrumentOneKnobSlot = 100000;
        const bool instrumentFxLoaded = oneKnob.hasEffect(instrumentOneKnobSlot);
        const bool instrumentLoaded = host.hasInstrument();
        juce::String instrumentLabel = instrumentLoaded ? host.getInstrumentName() : "INSTRUMENT — EMPTY";
        if (instrumentFxLoaded)
            instrumentLabel << "  +  " << oneKnob.getName(instrumentOneKnobSlot);
        pluginButtons[(size_t)instrumentChannel].setButtonText(instrumentLabel);
        unloadButtons[(size_t)instrumentChannel].setEnabled(instrumentFxLoaded || instrumentLoaded);
        pluginButtons[(size_t)masterChannel].setButtonText("MASTER OUTPUT");
        pluginButtons[(size_t)masterChannel].setEnabled(false);
        unloadButtons[(size_t)masterChannel].setVisible(false);
    }

    void syncFromEngine()
    {
        syncing = true;
        for (int ch = 0; ch < juce::jmin(owner.getAudioTrackCount(), instrumentChannel); ++ch)
        {
            faders[(size_t)ch].setValue(gainToDb(owner.audioEngine.getTrackGain(ch)), juce::dontSendNotification);
            pans[(size_t)ch].setValue(owner.audioEngine.getTrackPan(ch), juce::dontSendNotification);
        }
        faders[(size_t)instrumentChannel].setValue(gainToDb(getLibertyInstrumentGain()), juce::dontSendNotification);
        pans[(size_t)instrumentChannel].setValue(getLibertyInstrumentPan(), juce::dontSendNotification);
        faders[(size_t)masterChannel].setValue(gainToDb(owner.audioEngine.getMasterGain()), juce::dontSendNotification);
        syncing = false;
        syncButtons();
        refreshPluginLabels();
    }

    void timerCallback() override
    {
        if (!consoleVisible) return;
        const auto wanted = juce::Rectangle<int>(0, transportHeight, owner.getWidth(), juce::jmax(1, owner.getHeight() - transportHeight));
        if (getBounds() != wanted)
            setBounds(wanted);
        syncFromEngine();
        updateMeters();
        repaint();
    }

    MainComponent& owner;
    ConsoleLookAndFeel lookAndFeel;
    std::array<juce::Slider, channelCount> faders;
    std::array<juce::Slider, channelCount> pans;
    std::array<juce::TextButton, channelCount> muteButtons;
    std::array<juce::TextButton, channelCount> soloButtons;
    std::array<juce::TextButton, channelCount> pluginButtons;
    std::array<juce::TextButton, channelCount> unloadButtons;
    std::array<juce::Rectangle<int>, channelCount> stripBounds;
    std::array<float, channelCount> meterLevels {};
    std::array<float, channelCount> meterPeaks {};
    bool syncing = false;
    bool consoleVisible = false;
    int selectedConsoleChannel = -1;
};

class MixConsoleController final : private juce::Timer
{
public:
    explicit MixConsoleController(MainComponent& ownerIn)
        : owner(ownerIn), view(ownerIn)
    {
        arrangeButton.setButtonText("ARRANGE");
        mixButton.setButtonText("MIXCONSOLE");
        for (auto* button : { &arrangeButton, &mixButton })
        {
            button->setClickingTogglesState(false);
            button->setMouseClickGrabsKeyboardFocus(false);
            button->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            owner.addAndMakeVisible(*button);
        }

        arrangeButton.onClick = [this] { showMixConsole(false); };
        mixButton.onClick = [this] { showMixConsole(true); };
        startTimerHz(10);
        refreshPageButtons();
    }

    ~MixConsoleController() override { shutdown(); }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
        view.setConsoleVisible(false);
        arrangeButton.setVisible(false);
        mixButton.setVisible(false);
    }

    void showMixConsole(bool shouldShow)
    {
        mixVisible = shouldShow;
        view.setConsoleVisible(shouldShow);
        refreshPageButtons();
        if (shouldShow) view.toFront(false);
        arrangeButton.toFront(false);
        mixButton.toFront(false);
        owner.repaint();
    }

    bool isVisible() const noexcept { return mixVisible; }

    int getDropTrack(juce::Point<int> ownerPoint, bool instrumentPlugin) const
    {
        return view.getDropTrackForOwnerPoint(ownerPoint, instrumentPlugin);
    }

private:
    void refreshPageButtons()
    {
        arrangeButton.setColour(juce::TextButton::buttonColourId,
                                mixVisible ? juce::Colour(0xff252a31) : juce::Colour(0xff315f7a));
        mixButton.setColour(juce::TextButton::buttonColourId,
                            mixVisible ? juce::Colour(0xff315f7a) : juce::Colour(0xff252a31));
    }

    void timerCallback() override
    {
        if (stopped.load()) return;
        arrangeButton.setBounds(1055, 8, 88, 26);
        mixButton.setBounds(1147, 8, 112, 26);
        arrangeButton.toFront(false);
        mixButton.toFront(false);
    }

    MainComponent& owner;
    MixConsoleView view;
    juce::TextButton arrangeButton, mixButton;
    std::atomic<bool> stopped { false };
    bool mixVisible = false;
};

std::map<MainComponent*, std::unique_ptr<MixConsoleController>> controllers;

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
                        controllers.emplace(main, std::make_unique<MixConsoleController>(*main));
    }
};

Bootstrap bootstrap;
}

bool isLibertyMixConsoleVisible(MainComponent* owner)
{
    if (owner == nullptr) return false;
    const auto it = controllers.find(owner);
    return it != controllers.end() && it->second && it->second->isVisible();
}

int getLibertyMixConsolePluginDropTrack(MainComponent* owner,
                                        juce::Point<int> ownerPoint,
                                        bool instrumentPlugin)
{
    if (owner == nullptr) return -1;
    const auto it = controllers.find(owner);
    if (it == controllers.end() || !it->second) return -1;
    return it->second->getDropTrack(ownerPoint, instrumentPlugin);
}

void shutdownLibertyMixConsoleController()
{
    bootstrap.shutdown();
}
