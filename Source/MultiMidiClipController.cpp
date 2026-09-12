#define private public
#include "MainComponent.h"
#undef private
#include "MidiEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

int getLibertyTrackColourId(int track);
juce::String getLibertyTrackName(int track);
bool isLibertySnapEnabled() noexcept;
double getLibertySnapSeconds(double tempoBpm) noexcept;
double getLibertyTimelinePixelsPerSecond() noexcept;
int getLibertyTrackRowHeight() noexcept;

namespace
{
constexpr int midiTrackIndex = AudioEngine::maxAudioTracks;
constexpr int instrumentTrackIndex = AudioEngine::maxAudioTracks + 1;
constexpr int headerWidth = 210;
constexpr int rulerHeight = 32;
constexpr int midiRow = AudioEngine::maxAudioTracks;
constexpr int instrumentRow = AudioEngine::maxAudioTracks + 1;
constexpr float resizeZone = 12.0f;

struct Clip
{
    int id = 0;
    double startSeconds = 0.0;
    double lengthSeconds = 2.0;
    bool userLength = false;
    std::vector<MidiEngine::NoteEvent> notes;
};

juce::Colour trackColour(int id)
{
    static constexpr std::array<juce::uint32, 9> p {
        0xff31506a, 0xff3b82f6, 0xff22c55e, 0xffeab308, 0xfff97316,
        0xffef4444, 0xffa855f7, 0xffec4899, 0xff14b8a6
    };
    return juce::Colour(p[(size_t)juce::jlimit(0, 8, id)]);
}

class MultiMidiClipController;
MultiMidiClipController* activeController = nullptr;

class MultiMidiClipController final : public juce::Component,
                                      private juce::Timer,
                                      private juce::AudioIODeviceCallback
{
public:
    explicit MultiMidiClipController(MainComponent& o) : owner(o)
    {
        setInterceptsMouseClicks(true, false);
        owner.addAndMakeVisible(this);
        owner.midiClipOverlay.setVisible(false);
        migrateLegacyClip();
        owner.audioEngine.getDeviceManager().addAudioCallback(this);
        activeController = this;
        startTimerHz(30);
    }

    ~MultiMidiClipController() override { shutdown(); }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
        owner.audioEngine.getDeviceManager().removeAudioCallback(this);
        if (activeController == this) activeController = nullptr;
        setVisible(false);
    }

    void setInstrumentGain(float value) noexcept { instrumentGain.store(juce::jlimit(0.0f, 2.0f, value)); }
    float getInstrumentGain() const noexcept { return instrumentGain.load(); }
    void setInstrumentPan(float value) noexcept { instrumentPan.store(juce::jlimit(-1.0f, 1.0f, value)); }
    float getInstrumentPan() const noexcept { return instrumentPan.load(); }

    bool hitTest(int x, int y) override
    {
        const int rowHeight = getLibertyTrackRowHeight();
        const int mt = 76 + rulerHeight + midiRow * rowHeight;
        const int it = 76 + rulerHeight + instrumentRow * rowHeight;
        const int mix = owner.getHeight() - 210;
        return (x >= headerWidth && ((y >= mt && y < mt + rowHeight) || (y >= it && y < it + rowHeight)))
            || (y >= mix && x >= 715 && x <= 970);
    }

    void paint(juce::Graphics& g) override
    {
        drawClips(g, false, midiClips, midiRow, midiTrackIndex);
        drawClips(g, true, instrumentClips, instrumentRow, instrumentTrackIndex);
        drawInstrumentMixer(g);
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        if (isMixerPoint(e.getPosition()))
        {
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
            return;
        }
        bool ins = false;
        const int i = findClipAt(e.getPosition(), ins);
        if (i < 0)
        {
            setMouseCursor(juce::MouseCursor::CrosshairCursor);
            return;
        }
        const double pixelsPerSecond = getLibertyTimelinePixelsPerSecond();
        const auto& c = (ins ? instrumentClips : midiClips)[(size_t)i];
        const float l = headerWidth + (float)(c.startSeconds * pixelsPerSecond);
        const float r = l + (float)(c.lengthSeconds * pixelsPerSecond);
        setMouseCursor(((float)e.x <= l + resizeZone || (float)e.x >= r - resizeZone)
            ? juce::MouseCursor::LeftRightResizeCursor
            : juce::MouseCursor::DraggingHandCursor);
    }

    void mouseExit(const juce::MouseEvent&) override { setMouseCursor(juce::MouseCursor::NormalCursor); }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (handleMixerMouse(e.getPosition())) return;
        bool ins = false;
        const int i = findClipAt(e.getPosition(), ins);
        if (i < 0)
        {
            if (isPointInTrackRow(e.getPosition(), false)) owner.selectedTrack = -1;
            if (isPointInTrackRow(e.getPosition(), true)) owner.selectedTrack = -2;
            owner.repaint();
            return;
        }
        selectClip(ins, i);
        auto& c = (ins ? instrumentClips : midiClips)[(size_t)i];
        dragInstrument = ins;
        dragIndex = i;
        dragStartX = (float)e.x;
        dragStartSeconds = c.startSeconds;
        dragStartLength = c.lengthSeconds;
        const double pixelsPerSecond = getLibertyTimelinePixelsPerSecond();
        const float l = headerWidth + (float)(c.startSeconds * pixelsPerSecond);
        const float r = l + (float)(c.lengthSeconds * pixelsPerSecond);
        dragMode = (float)e.x <= l + resizeZone ? 2 : ((float)e.x >= r - resizeZone ? 3 : 1);
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (e.x < headerWidth) return;
        const bool ins = isPointInTrackRow(e.getPosition(), true);
        if (!ins && !isPointInTrackRow(e.getPosition(), false)) return;
        bool hitInstrument = false;
        int i = findClipAt(e.getPosition(), hitInstrument);
        if (i < 0)
        {
            auto& v = ins ? instrumentClips : midiClips;
            Clip c;
            c.id = nextClipId++;
            const auto m = secondsPerMeasure();
            const auto raw = juce::jmax(0.0, ((double)e.x - headerWidth) / getLibertyTimelinePixelsPerSecond());
            c.startSeconds = snapPosition(raw);
            c.lengthSeconds = m;
            v.push_back(std::move(c));
            i = (int)v.size() - 1;
        }
        selectClip(ins, i);
        openLibertyMidiEditor(owner);
        syncPlayback();
        repaint();
        owner.repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (mixerDragMode)
        {
            dragMixer(e.getPosition());
            return;
        }
        if (dragIndex < 0 || !dragMode) return;
        auto& v = dragInstrument ? instrumentClips : midiClips;
        if (dragIndex >= (int)v.size()) return;
        auto& c = v[(size_t)dragIndex];
        const auto step = getLibertySnapSeconds(owner.tempoBpm);
        const auto minLen = step > 0.0 ? step : 0.01;
        const auto d = ((double)e.x - dragStartX) / getLibertyTimelinePixelsPerSecond();
        if (dragMode == 1)
            c.startSeconds = snapPosition(dragStartSeconds + d);
        else if (dragMode == 2)
        {
            const auto right = dragStartSeconds + dragStartLength;
            const auto raw = juce::jlimit(0.0, right - minLen, dragStartSeconds + d);
            auto ns = snapPosition(raw);
            ns = juce::jlimit(0.0, right - minLen, ns);
            c.startSeconds = ns;
            c.lengthSeconds = juce::jmax(minLen, right - c.startSeconds);
            c.userLength = true;
        }
        else
        {
            auto raw = juce::jmax(minLen, dragStartLength + d);
            if (step > 0.0) raw = std::round(raw / step) * step;
            c.lengthSeconds = juce::jmax(minLen, raw);
            c.userLength = true;
        }
        if (isActiveClip(dragInstrument, dragIndex))
        {
            owner.midiClipStartSeconds = c.startSeconds;
            owner.midiClipLengthSeconds = c.lengthSeconds;
            owner.midiClipLengthUserDefined = c.userLength;
        }
        syncPlayback();
        repaint();
        owner.repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        dragIndex = -1;
        dragMode = 0;
        mixerDragMode = 0;
    }

private:
    struct PlaybackNote
    {
        std::atomic<double> start { 0.0 };
        std::atomic<double> end { 0.0 };
        std::atomic<double> frequency { 440.0 };
        std::atomic<float> amplitude { 0.0f };
    };
    static constexpr size_t maxInstrumentNotes = 512;

    double secondsPerMeasure() const
    {
        const double beat = 60.0 / juce::jmax(1.0, owner.tempoBpm)
            * (4.0 / (double)juce::jmax(1, owner.timeSignatureDenominator));
        return beat * (double)juce::jmax(1, owner.timeSignatureNumerator);
    }

    double snapPosition(double seconds) const
    {
        seconds = juce::jmax(0.0, seconds);
        const auto step = getLibertySnapSeconds(owner.tempoBpm);
        return step > 0.0 ? std::round(seconds / step) * step : seconds;
    }

    bool isPointInTrackRow(juce::Point<int> p, bool ins) const
    {
        const int rowHeight = getLibertyTrackRowHeight();
        const int row = ins ? instrumentRow : midiRow;
        const int top = 76 + rulerHeight + row * rowHeight;
        return p.y >= top && p.y < top + rowHeight;
    }

    int findClipAt(juce::Point<int> p, bool& ins) const
    {
        const int rowHeight = getLibertyTrackRowHeight();
        const double pixelsPerSecond = getLibertyTimelinePixelsPerSecond();
        for (int pass = 0; pass < 2; ++pass)
        {
            ins = pass == 1;
            if (!isPointInTrackRow(p, ins)) continue;
            const auto& v = ins ? instrumentClips : midiClips;
            for (int i = (int)v.size() - 1; i >= 0; --i)
            {
                const auto& c = v[(size_t)i];
                const int x = headerWidth + (int)std::round(c.startSeconds * pixelsPerSecond);
                const int w = juce::jmax(24, (int)std::round(c.lengthSeconds * pixelsPerSecond));
                const int y = 76 + rulerHeight + (ins ? instrumentRow : midiRow) * rowHeight + 4;
                if (juce::Rectangle<int>(x, y, w, rowHeight - 8).contains(p)) return i;
            }
        }
        return -1;
    }

    bool isActiveClip(bool ins, int i) const noexcept { return activeIndex == i && activeInstrument == ins; }

    void syncActiveClip()
    {
        if (activeIndex < 0) return;
        auto& v = activeInstrument ? instrumentClips : midiClips;
        if (activeIndex >= (int)v.size()) return;
        auto& c = v[(size_t)activeIndex];
        c.notes = owner.midiEngine.getNotesCopy();
        if (!c.userLength && !c.notes.empty())
        {
            double end = 0.0;
            for (const auto& n : c.notes)
                end = juce::jmax(end, MidiEngine::tickToSeconds(n.startTick + n.lengthTicks, owner.tempoBpm));
            const auto m = secondsPerMeasure();
            c.lengthSeconds = juce::jmax(m, std::ceil(end / m) * m);
        }
        owner.midiClipStartSeconds = c.startSeconds;
        owner.midiClipLengthSeconds = c.lengthSeconds;
        owner.midiClipLengthUserDefined = c.userLength;
    }

    void selectClip(bool ins, int i)
    {
        syncActiveClip();
        auto& v = ins ? instrumentClips : midiClips;
        if (i < 0 || i >= (int)v.size()) return;
        activeInstrument = ins;
        activeIndex = i;
        auto& c = v[(size_t)i];
        owner.midiEngine.clear();
        owner.midiEngine.clearUndoHistory();
        for (const auto& n : c.notes)
            owner.midiEngine.addNote(n.startTick, n.lengthTicks, n.pitch, n.velocity, n.channel);
        owner.midiEngine.clearNoteSelection();
        owner.midiEngine.clearUndoHistory();
        owner.midiClipStartSeconds = c.startSeconds;
        owner.midiClipLengthSeconds = c.lengthSeconds;
        owner.midiClipLengthUserDefined = c.userLength;
        owner.selectedTrack = ins ? -2 : -1;
        owner.repaint();
    }

    void migrateLegacyClip()
    {
        const auto notes = owner.midiEngine.getNotesCopy();
        if (notes.empty()) return;
        Clip c;
        c.id = nextClipId++;
        c.startSeconds = owner.midiClipStartSeconds;
        c.lengthSeconds = juce::jmax(secondsPerMeasure(), owner.midiClipLengthSeconds);
        c.userLength = owner.midiClipLengthUserDefined;
        c.notes = notes;
        midiClips.push_back(std::move(c));
        activeIndex = 0;
        activeInstrument = false;
    }

    void flattenClips(const std::vector<Clip>& v, std::vector<MidiEngine::NoteEvent>& out) const
    {
        out.clear();
        for (const auto& c : v)
        {
            const auto off = MidiEngine::secondsToTick(c.startSeconds, owner.tempoBpm);
            const auto len = MidiEngine::secondsToTick(c.lengthSeconds, owner.tempoBpm);
            for (const auto& n : c.notes)
            {
                if (n.startTick >= len) continue;
                auto a = n;
                a.startTick += off;
                out.push_back(a);
            }
        }
    }

    void syncPlayback()
    {
        syncActiveClip();
        std::vector<MidiEngine::NoteEvent> mn;
        flattenClips(midiClips, mn);
        double end = 0.0;
        for (const auto& c : midiClips) end = juce::jmax(end, c.startSeconds + c.lengthSeconds);
        for (const auto& c : instrumentClips) end = juce::jmax(end, c.startSeconds + c.lengthSeconds);
        owner.audioEngine.setMidiNotes(mn, 0.0, end, owner.tempoBpm);
        owner.audioEngine.setProjectExtraLengthSeconds(end);

        std::vector<MidiEngine::NoteEvent> in;
        flattenClips(instrumentClips, in);
        const auto count = juce::jmin((size_t)maxInstrumentNotes, in.size());
        for (size_t i = 0; i < count; ++i)
        {
            const auto& n = in[i];
            instrumentPlayback[i].start.store(MidiEngine::tickToSeconds(n.startTick, owner.tempoBpm));
            instrumentPlayback[i].end.store(MidiEngine::tickToSeconds(n.startTick + n.lengthTicks, owner.tempoBpm));
            instrumentPlayback[i].frequency.store(440.0 * std::pow(2.0, (static_cast<int>(n.pitch) - 69) / 12.0));
            instrumentPlayback[i].amplitude.store(0.045f * ((float)n.velocity / 127.0f));
        }
        instrumentNoteCount.store(count, std::memory_order_release);
    }

    void drawClips(juce::Graphics& g, bool ins, const std::vector<Clip>& v, int row, int colourIndex)
    {
        const int rowHeight = getLibertyTrackRowHeight();
        const double pixelsPerSecond = getLibertyTimelinePixelsPerSecond();
        const int y = 76 + rulerHeight + row * rowHeight;
        const auto colour = trackColour(getLibertyTrackColourId(colourIndex));
        for (int i = 0; i < (int)v.size(); ++i)
        {
            const auto& c = v[(size_t)i];
            const int x = headerWidth + (int)std::round(c.startSeconds * pixelsPerSecond);
            const int w = juce::jmax(24, (int)std::round(c.lengthSeconds * pixelsPerSecond));
            const auto r = juce::Rectangle<int>(x, y + 4, w, rowHeight - 8);
            const bool sel = isActiveClip(ins, i);
            g.setColour(colour.withAlpha(sel ? 0.76f : 0.55f));
            g.fillRoundedRectangle(r.toFloat(), 5.0f);
            g.setColour(sel ? juce::Colours::white : colour.brighter(0.35f));
            g.drawRoundedRectangle(r.toFloat(), 5.0f, sel ? 2.0f : 1.0f);
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(10.0f, juce::Font::bold));
            g.drawText(ins ? "INSTRUMENT MIDI" : "MIDI CLIP", r.reduced(8, 4), juce::Justification::topLeft, true);
            const auto len = MidiEngine::secondsToTick(c.lengthSeconds, owner.tempoBpm);
            for (const auto& n : c.notes)
            {
                if (n.startTick >= len) continue;
                const float nx = (float)r.getX() + (float)(MidiEngine::tickToSeconds(n.startTick, owner.tempoBpm) * pixelsPerSecond);
                const float nw = juce::jmax(2.0f, (float)(MidiEngine::tickToSeconds(n.lengthTicks, owner.tempoBpm) * pixelsPerSecond));
                const float ny = (float)r.getY() + 10.0f + ((127.0f - n.pitch) / 127.0f) * (float)juce::jmax(1, r.getHeight() - 20);
                if (nx >= r.getRight()) continue;
                g.setColour(juce::Colours::white.withAlpha(0.72f));
                g.fillRoundedRectangle(nx, ny, juce::jmin(nw, (float)r.getRight() - nx), 3.0f, 1.5f);
            }
        }
    }

    bool isMixerPoint(juce::Point<int> p) const
    {
        return p.y >= owner.getHeight() - 210 && p.x >= 715 && p.x <= 970;
    }

    void drawInstrumentMixer(juce::Graphics& g)
    {
        const int top = owner.getHeight() - 210;
        g.setColour(juce::Colour(0xff101318));
        g.fillRect(715, top, 260, 210);
        drawMixerStrip(g, juce::Rectangle<int>(720, top + 12, 116, 188), false);
        drawMixerStrip(g, juce::Rectangle<int>(845, top + 12, 116, 188), true);
    }

    void drawMixerStrip(juce::Graphics& g, juce::Rectangle<int> c, bool master)
    {
        const auto colour = trackColour(getLibertyTrackColourId(instrumentTrackIndex));
        g.setColour(master ? juce::Colour(0xff1b2027) : juce::Colour(0xff171b20));
        g.fillRoundedRectangle(c.toFloat(), 5.0f);
        if (!master)
        {
            g.setColour(colour.withAlpha(0.14f));
            g.fillRoundedRectangle(c.toFloat(), 5.0f);
        }
        g.setColour(juce::Colour(0xff343a44));
        g.drawRoundedRectangle(c.toFloat(), 5.0f, 1.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(master ? "MASTER" : getLibertyTrackName(instrumentTrackIndex), c.getX(), c.getY() + 8, c.getWidth(), 20, juce::Justification::centred, true);

        if (!master)
        {
            const auto m = juce::Rectangle<int>(c.getX() + 8, c.getY() + 32, 44, 20);
            const auto s = juce::Rectangle<int>(c.getX() + 58, c.getY() + 32, 44, 20);
            g.setColour(owner.audioEngine.isInstrumentTrackMuted() ? juce::Colour(0xff9b4545) : juce::Colour(0xff252a31));
            g.fillRoundedRectangle(m.toFloat(), 4.0f);
            g.setColour(owner.audioEngine.isInstrumentTrackSolo() ? juce::Colour(0xff8b7a32) : juce::Colour(0xff252a31));
            g.fillRoundedRectangle(s.toFloat(), 4.0f);
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(9.0f, juce::Font::bold));
            g.drawText("M", m, juce::Justification::centred);
            g.drawText("S", s, juce::Justification::centred);
        }

        const int ft = c.getY() + 58;
        const int fb = c.getBottom() - 45;
        const auto f = juce::Rectangle<float>((float)c.getCentreX() - 7.0f, (float)ft, 14.0f, (float)(fb - ft));
        g.setColour(juce::Colour(0xff090b0e));
        g.fillRoundedRectangle(f, 3.0f);
        const float gain = master ? owner.audioEngine.getMasterGain() : instrumentGain.load();
        const float norm = juce::jlimit(0.0f, 1.0f, gain * 0.5f);
        const float ky = f.getBottom() - norm * f.getHeight();
        g.setColour(juce::Colour(0xffd6d9de));
        g.fillRoundedRectangle(f.getX() - 2.0f, ky - 6.0f, f.getWidth() + 4.0f, 12.0f, 3.0f);
        const auto db = 20.0f * std::log10(juce::jmax(0.000001f, gain));
        g.setColour(juce::Colour(0xff858c96));
        g.setFont(juce::Font(10.0f));
        g.drawText(db < -59.9f ? "-inf dB" : juce::String(db, 1) + " dB", c.getX(), c.getBottom() - 38, c.getWidth(), 16, juce::Justification::centred);
        g.drawText(master ? "MASTER" : "PAN " + juce::String(instrumentPan.load(), 2), c.getX(), c.getBottom() - 22, c.getWidth(), 16, juce::Justification::centred);
    }

    bool handleMixerMouse(juce::Point<int> p)
    {
        if (!isMixerPoint(p)) return false;
        const int top = owner.getHeight() - 210;
        const auto inst = juce::Rectangle<int>(720, top + 12, 116, 188);
        const auto master = juce::Rectangle<int>(845, top + 12, 116, 188);
        if (inst.contains(p))
        {
            const auto m = juce::Rectangle<int>(inst.getX() + 8, inst.getY() + 32, 44, 20);
            const auto s = juce::Rectangle<int>(inst.getX() + 58, inst.getY() + 32, 44, 20);
            if (m.contains(p))
            {
                owner.audioEngine.setInstrumentTrackMuted(!owner.audioEngine.isInstrumentTrackMuted());
                repaint();
                return true;
            }
            if (s.contains(p))
            {
                owner.audioEngine.setInstrumentTrackSolo(!owner.audioEngine.isInstrumentTrackSolo());
                repaint();
                return true;
            }
            if (p.y >= inst.getY() + 58 && p.y <= inst.getBottom() - 45)
            {
                mixerDragMode = 1;
                dragMixer(p);
                return true;
            }
            if (p.y >= inst.getBottom() - 28)
            {
                mixerDragMode = 2;
                dragMixer(p);
                return true;
            }
        }
        if (master.contains(p) && p.y >= master.getY() + 58 && p.y <= master.getBottom() - 45)
        {
            mixerDragMode = 3;
            dragMixer(p);
            return true;
        }
        return true;
    }

    void dragMixer(juce::Point<int> p)
    {
        const int top = owner.getHeight() - 210;
        const auto c = mixerDragMode == 3 ? juce::Rectangle<int>(845, top + 12, 116, 188)
                                         : juce::Rectangle<int>(720, top + 12, 116, 188);
        if (mixerDragMode == 1 || mixerDragMode == 3)
        {
            const int ft = c.getY() + 58;
            const int fb = c.getBottom() - 45;
            const float n = juce::jlimit(0.0f, 1.0f, (float)(fb - p.y) / (float)juce::jmax(1, fb - ft));
            if (mixerDragMode == 3) owner.audioEngine.setMasterGain(n * 2.0f);
            else instrumentGain.store(n * 2.0f);
        }
        else if (mixerDragMode == 2)
            instrumentPan.store(juce::jlimit(-1.0f, 1.0f, ((float)p.x - (float)c.getCentreX()) / 45.0f));
        repaint();
    }

    void timerCallback() override
    {
        if (stopped.load()) return;
        owner.midiClipOverlay.setVisible(false);
        const auto bounds = owner.getLocalBounds();
        if (getBounds() != bounds) setBounds(bounds);
        handleProjectPersistence();
        syncPlayback();
        repaint();
    }

    void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const*, int,
                                           float* const* outputs, int numOutputs,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch]) juce::FloatVectorOperations::clear(outputs[ch], numSamples);

        if (!owner.audioEngine.isPlaying() || owner.audioEngine.isInstrumentTrackMuted()) return;
        if (owner.audioEngine.isAnyTrackSolo() && !owner.audioEngine.isInstrumentTrackSolo()) return;
        const auto rate = owner.audioEngine.getSampleRate();
        if (rate <= 0.0) return;
        const auto pos = owner.audioEngine.transportSamples.load();
        const auto count = instrumentNoteCount.load(std::memory_order_acquire);
        const float gain = instrumentGain.load();
        const float pan = instrumentPan.load();
        const float lg = gain * (pan > 0.0f ? 1.0f - pan : 1.0f);
        const float rg = gain * (pan < 0.0f ? 1.0f + pan : 1.0f);
        constexpr double tp = 6.28318530717958647692;
        for (int s = 0; s < numSamples; ++s)
        {
            const double t = (double)(pos + s) / rate;
            float v = 0.0f;
            for (size_t i = 0; i < count; ++i)
            {
                const double st = instrumentPlayback[i].start.load();
                const double en = instrumentPlayback[i].end.load();
                if (t < st || t >= en) continue;
                const double nt = t - st;
                const double dur = en - st;
                float env = 1.0f;
                if (nt < 0.005) env = (float)(nt / 0.005);
                if (dur - nt < 0.010) env = juce::jmin(env, (float)((dur - nt) / 0.010));
                v += (float)(std::sin(tp * instrumentPlayback[i].frequency.load() * nt)
                    * (double)(instrumentPlayback[i].amplitude.load() * env));
            }
            const float master = owner.audioEngine.getMasterGain();
            if (numOutputs > 0 && outputs[0]) outputs[0][s] += v * lg * master;
            if (numOutputs > 1 && outputs[1]) outputs[1][s] += v * rg * master;
        }
    }

    juce::String serialiseClips() const
    {
        juce::XmlElement root("MultiMidiClips");
        root.setAttribute("version", 1);
        auto add = [&](const std::vector<Clip>& v, const char* track)
        {
            for (const auto& c : v)
            {
                auto* ce = root.createNewChildElement("Clip");
                ce->setAttribute("track", track);
                ce->setAttribute("id", c.id);
                ce->setAttribute("start", c.startSeconds);
                ce->setAttribute("length", c.lengthSeconds);
                ce->setAttribute("userLength", c.userLength);
                for (const auto& n : c.notes)
                {
                    auto* ne = ce->createNewChildElement("Note");
                    ne->setAttribute("startTick", (double)n.startTick);
                    ne->setAttribute("lengthTicks", (double)n.lengthTicks);
                    ne->setAttribute("pitch", (int)n.pitch);
                    ne->setAttribute("velocity", (int)n.velocity);
                    ne->setAttribute("channel", (int)n.channel);
                }
            }
        };
        add(midiClips, "midi");
        add(instrumentClips, "instrument");
        return root.toString();
    }

    void loadClipsFromXml(const juce::XmlElement& root)
    {
        midiClips.clear();
        instrumentClips.clear();
        activeIndex = -1;
        forEachXmlChildElementWithTagName(root, ce, "Clip")
        {
            Clip c;
            c.id = ce->getIntAttribute("id", nextClipId++);
            c.startSeconds = ce->getDoubleAttribute("start", 0.0);
            c.lengthSeconds = ce->getDoubleAttribute("length", secondsPerMeasure());
            c.userLength = ce->getBoolAttribute("userLength", false);
            nextClipId = juce::jmax(nextClipId, c.id + 1);
            forEachXmlChildElementWithTagName(*ce, ne, "Note")
            {
                MidiEngine::NoteEvent n;
                n.startTick = (std::int64_t)ne->getDoubleAttribute("startTick", 0.0);
                n.lengthTicks = (std::int64_t)ne->getDoubleAttribute("lengthTicks", MidiEngine::ticksPerQuarterNote);
                n.pitch = (std::uint8_t)juce::jlimit(0, 127, ne->getIntAttribute("pitch", 60));
                n.velocity = (std::uint8_t)juce::jlimit(1, 127, ne->getIntAttribute("velocity", 100));
                n.channel = (std::uint8_t)juce::jlimit(1, 16, ne->getIntAttribute("channel", 1));
                c.notes.push_back(n);
            }
            if (ce->getStringAttribute("track") == "instrument") instrumentClips.push_back(std::move(c));
            else midiClips.push_back(std::move(c));
        }
        if (!midiClips.empty()) selectClip(false, 0);
        else if (!instrumentClips.empty()) selectClip(true, 0);
    }

    void handleProjectPersistence()
    {
        const auto path = owner.currentProjectFile.getFullPathName();
        if (path != lastProjectPath)
        {
            lastProjectPath = path;
            if (owner.currentProjectFile.existsAsFile())
                if (auto xml = juce::parseXML(owner.currentProjectFile))
                {
                    if (auto* state = xml->getChildByName("MultiMidiClips"))
                    {
                        loadClipsFromXml(*state);
                        owner.markProjectClean();
                    }
                    else
                    {
                        midiClips.clear(); instrumentClips.clear(); activeIndex = -1; migrateLegacyClip();
                    }
                }
        }
        if (!owner.currentProjectFile.existsAsFile() || owner.hasUnsavedChanges()) return;
        auto xml = juce::parseXML(owner.currentProjectFile);
        if (xml == nullptr) return;
        const auto wanted = serialiseClips();
        auto* existing = xml->getChildByName("MultiMidiClips");
        if (existing != nullptr && existing->toString() == wanted) return;
        if (existing != nullptr) xml->removeChildElement(existing, true);
        auto state = juce::parseXML(wanted);
        if (state != nullptr) xml->addChildElement(state.release());
        auto out = owner.currentProjectFile.createOutputStream();
        if (out != nullptr)
        {
            out->setPosition(0);
            out->truncate();
            out->writeText(xml->toString(), false, false, "UTF-8");
            out->flush();
        }
    }

    MainComponent& owner;
    std::vector<Clip> midiClips, instrumentClips;
    int nextClipId = 1, activeIndex = -1;
    bool activeInstrument = false;
    int dragIndex = -1;
    bool dragInstrument = false;
    int dragMode = 0;
    float dragStartX = 0.0f;
    double dragStartSeconds = 0.0, dragStartLength = 0.0;
    int mixerDragMode = 0;
    std::atomic<float> instrumentGain { 1.0f }, instrumentPan { 0.0f };
    std::array<PlaybackNote, maxInstrumentNotes> instrumentPlayback;
    std::atomic<size_t> instrumentNoteCount { 0 };
    std::atomic<bool> stopped { false };
    juce::String lastProjectPath;
};

class Bootstrap final : private juce::Timer
{
public:
    Bootstrap() { startTimerHz(10); }
    ~Bootstrap() override { shutdown(); }
    void shutdown()
    {
        stopTimer();
        for (auto& p : controllers) p.second->shutdown();
        controllers.clear();
    }
private:
    void timerCallback() override
    {
        auto& d = juce::Desktop::getInstance();
        for (int i = 0; i < d.getNumComponents(); ++i)
            if (auto* w = dynamic_cast<juce::DocumentWindow*>(d.getComponent(i)))
                if (auto* m = dynamic_cast<MainComponent*>(w->getContentComponent()))
                    if (controllers.find(m) == controllers.end())
                        controllers.emplace(m, std::make_unique<MultiMidiClipController>(*m));
    }
    std::map<MainComponent*, std::unique_ptr<MultiMidiClipController>> controllers;
};

Bootstrap bootstrap;
}

float getLibertyInstrumentGain() noexcept
{
    return activeController != nullptr ? activeController->getInstrumentGain() : 1.0f;
}

void setLibertyInstrumentGain(float value) noexcept
{
    if (activeController != nullptr) activeController->setInstrumentGain(value);
}

float getLibertyInstrumentPan() noexcept
{
    return activeController != nullptr ? activeController->getInstrumentPan() : 0.0f;
}

void setLibertyInstrumentPan(float value) noexcept
{
    if (activeController != nullptr) activeController->setInstrumentPan(value);
}

void shutdownLibertyMultiMidiClipController()
{
    bootstrap.shutdown();
}
