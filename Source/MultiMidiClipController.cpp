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

namespace
{
constexpr int midiTrackIndex = AudioEngine::maxAudioTracks;
constexpr int instrumentTrackIndex = AudioEngine::maxAudioTracks + 1;
constexpr int headerWidth = 210;
constexpr int rulerHeight = 32;
constexpr int rowHeight = 70;
constexpr int midiRow = AudioEngine::maxAudioTracks;
constexpr int instrumentRow = AudioEngine::maxAudioTracks + 1;
constexpr float pixelsPerSecond = 80.0f;
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
    static constexpr std::array<juce::uint32, 9> palette {
        0xff31506a, 0xff3b82f6, 0xff22c55e, 0xffeab308, 0xfff97316,
        0xffef4444, 0xffa855f7, 0xffec4899, 0xff14b8a6
    };
    id = juce::jlimit(0, static_cast<int>(palette.size()) - 1, id);
    return juce::Colour(palette[(size_t)id]);
}

class MultiMidiClipController final : public juce::Component,
                                      private juce::Timer,
                                      private juce::AudioIODeviceCallback
{
public:
    explicit MultiMidiClipController(MainComponent& ownerIn) : owner(ownerIn)
    {
        setInterceptsMouseClicks(true, false);
        owner.addAndMakeVisible(this);
        owner.midiClipOverlay.setVisible(false);
        migrateLegacyClip();
        owner.audioEngine.getDeviceManager().addAudioCallback(this);
        startTimerHz(30);
    }

    ~MultiMidiClipController() override
    {
        shutdown();
    }

    void shutdown()
    {
        if (stopped.exchange(true))
            return;
        stopTimer();
        owner.audioEngine.getDeviceManager().removeAudioCallback(this);
        setVisible(false);
    }

    bool hitTest(int x, int y) override
    {
        const int midiTop = 76 + rulerHeight + midiRow * rowHeight;
        const int instrumentTop = 76 + rulerHeight + instrumentRow * rowHeight;
        const int mixerTop = owner.getHeight() - 210;
        const bool clipRows = x >= headerWidth && ((y >= midiTop && y < midiTop + rowHeight) ||
                                                   (y >= instrumentTop && y < instrumentTop + rowHeight));
        const bool instrumentHeaderButtons = y >= instrumentTop && y < instrumentTop + rowHeight && x >= 158 && x <= 208;
        const bool mixer = y >= mixerTop && x >= 715 && x <= 970;
        return clipRows || instrumentHeaderButtons || mixer;
    }

    void paint(juce::Graphics& g) override
    {
        drawClips(g, false, midiClips, midiRow, midiTrackIndex);
        drawClips(g, true, instrumentClips, instrumentRow, instrumentTrackIndex);
        drawInstrumentHeaderButtons(g);
        drawInstrumentMixer(g);
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        if (isMixerPoint(e.getPosition()) || isInstrumentHeaderButtonPoint(e.getPosition()))
        {
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
            return;
        }
        bool instrument = false;
        const int index = findClipAt(e.getPosition(), instrument);
        if (index < 0)
        {
            setMouseCursor(juce::MouseCursor::CrosshairCursor);
            return;
        }
        const auto& clip = (instrument ? instrumentClips : midiClips)[(size_t)index];
        const float x = (float)e.x;
        const float left = headerWidth + (float)(clip.startSeconds * pixelsPerSecond);
        const float right = left + (float)(clip.lengthSeconds * pixelsPerSecond);
        setMouseCursor((x <= left + resizeZone || x >= right - resizeZone)
            ? juce::MouseCursor::LeftRightResizeCursor
            : juce::MouseCursor::DraggingHandCursor);
    }

    void mouseExit(const juce::MouseEvent&) override { setMouseCursor(juce::MouseCursor::NormalCursor); }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (handleMixerMouse(e.getPosition())) return;
        if (handleInstrumentHeaderButtons(e.getPosition())) return;

        bool instrument = false;
        const int index = findClipAt(e.getPosition(), instrument);
        if (index < 0)
        {
            if (isPointInTrackRow(e.getPosition(), false)) owner.selectedTrack = -1;
            if (isPointInTrackRow(e.getPosition(), true)) owner.selectedTrack = -2;
            owner.repaint();
            return;
        }

        selectClip(instrument, index);
        auto& clip = (instrument ? instrumentClips : midiClips)[(size_t)index];
        dragInstrument = instrument;
        dragIndex = index;
        dragStartX = (float)e.x;
        dragStartSeconds = clip.startSeconds;
        dragStartLength = clip.lengthSeconds;
        const float left = headerWidth + (float)(clip.startSeconds * pixelsPerSecond);
        const float right = left + (float)(clip.lengthSeconds * pixelsPerSecond);
        if ((float)e.x <= left + resizeZone) dragMode = 2;
        else if ((float)e.x >= right - resizeZone) dragMode = 3;
        else dragMode = 1;
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (e.x < headerWidth) return;
        const bool instrument = isPointInTrackRow(e.getPosition(), true);
        if (!instrument && !isPointInTrackRow(e.getPosition(), false)) return;

        bool hitInstrument = false;
        int index = findClipAt(e.getPosition(), hitInstrument);
        if (index < 0)
        {
            auto& clips = instrument ? instrumentClips : midiClips;
            Clip clip;
            clip.id = nextClipId++;
            const auto measure = secondsPerMeasure();
            const auto rawSeconds = juce::jmax(0.0, ((double)e.x - headerWidth) / pixelsPerSecond);
            clip.startSeconds = std::round(rawSeconds / measure) * measure;
            clip.lengthSeconds = measure;
            clips.push_back(std::move(clip));
            index = (int)clips.size() - 1;
        }
        selectClip(instrument, index);
        openLibertyMidiEditor(owner);
        syncPlayback();
        repaint();
        owner.repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (mixerDragMode != 0)
        {
            dragMixer(e.getPosition());
            return;
        }
        if (dragIndex < 0 || dragMode == 0) return;
        auto& clips = dragInstrument ? instrumentClips : midiClips;
        if (dragIndex >= (int)clips.size()) return;
        auto& clip = clips[(size_t)dragIndex];
        const auto measure = secondsPerMeasure();
        const auto delta = ((double)e.x - dragStartX) / pixelsPerSecond;
        if (dragMode == 1)
        {
            clip.startSeconds = juce::jmax(0.0, std::round((dragStartSeconds + delta) / measure) * measure);
        }
        else if (dragMode == 2)
        {
            const auto originalRight = dragStartSeconds + dragStartLength;
            const auto newStart = juce::jlimit(0.0, originalRight - measure, dragStartSeconds + delta);
            clip.startSeconds = juce::jmax(0.0, std::round(newStart / measure) * measure);
            clip.lengthSeconds = juce::jmax(measure, originalRight - clip.startSeconds);
            clip.userLength = true;
        }
        else if (dragMode == 3)
        {
            const auto newLength = juce::jmax(measure, dragStartLength + delta);
            clip.lengthSeconds = juce::jmax(measure, std::round(newLength / measure) * measure);
            clip.userLength = true;
        }
        if (isActiveClip(dragInstrument, dragIndex))
        {
            owner.midiClipStartSeconds = clip.startSeconds;
            owner.midiClipLengthSeconds = clip.lengthSeconds;
            owner.midiClipLengthUserDefined = clip.userLength;
        }
        syncPlayback();
        repaint(); owner.repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        dragIndex = -1; dragMode = 0; mixerDragMode = 0;
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
        const double beat = 60.0 / juce::jmax(1.0, owner.tempoBpm) * (4.0 / (double)juce::jmax(1, owner.timeSignatureDenominator));
        return beat * (double)juce::jmax(1, owner.timeSignatureNumerator);
    }

    bool isPointInTrackRow(juce::Point<int> p, bool instrument) const
    {
        const int row = instrument ? instrumentRow : midiRow;
        const int top = 76 + rulerHeight + row * rowHeight;
        return p.y >= top && p.y < top + rowHeight;
    }

    int findClipAt(juce::Point<int> p, bool& instrument) const
    {
        for (int pass = 0; pass < 2; ++pass)
        {
            instrument = pass == 1;
            if (!isPointInTrackRow(p, instrument)) continue;
            const auto& clips = instrument ? instrumentClips : midiClips;
            for (int i = (int)clips.size() - 1; i >= 0; --i)
            {
                const auto& clip = clips[(size_t)i];
                const int x = headerWidth + (int)std::round(clip.startSeconds * pixelsPerSecond);
                const int w = juce::jmax(24, (int)std::round(clip.lengthSeconds * pixelsPerSecond));
                const int y = 76 + rulerHeight + (instrument ? instrumentRow : midiRow) * rowHeight + 4;
                if (juce::Rectangle<int>(x, y, w, rowHeight - 8).contains(p)) return i;
            }
        }
        return -1;
    }

    bool isActiveClip(bool instrument, int index) const noexcept
    {
        return activeIndex == index && activeInstrument == instrument;
    }

    void syncActiveClip()
    {
        if (activeIndex < 0) return;
        auto& clips = activeInstrument ? instrumentClips : midiClips;
        if (activeIndex >= (int)clips.size()) return;
        auto& clip = clips[(size_t)activeIndex];
        clip.notes = owner.midiEngine.getNotesCopy();
        if (!clip.userLength && !clip.notes.empty())
        {
            double noteEnd = 0.0;
            for (const auto& note : clip.notes)
                noteEnd = juce::jmax(noteEnd, MidiEngine::tickToSeconds(note.startTick + note.lengthTicks, owner.tempoBpm));
            const auto measure = secondsPerMeasure();
            clip.lengthSeconds = juce::jmax(measure, std::ceil(noteEnd / measure) * measure);
        }
        owner.midiClipStartSeconds = clip.startSeconds;
        owner.midiClipLengthSeconds = clip.lengthSeconds;
        owner.midiClipLengthUserDefined = clip.userLength;
    }

    void selectClip(bool instrument, int index)
    {
        syncActiveClip();
        auto& clips = instrument ? instrumentClips : midiClips;
        if (index < 0 || index >= (int)clips.size()) return;
        activeInstrument = instrument;
        activeIndex = index;
        auto& clip = clips[(size_t)index];

        owner.midiEngine.clear();
        owner.midiEngine.clearUndoHistory();
        for (const auto& note : clip.notes)
            owner.midiEngine.addNote(note.startTick, note.lengthTicks, note.pitch, note.velocity, note.channel);
        owner.midiEngine.clearNoteSelection();
        owner.midiEngine.clearUndoHistory();
        owner.midiClipStartSeconds = clip.startSeconds;
        owner.midiClipLengthSeconds = clip.lengthSeconds;
        owner.midiClipLengthUserDefined = clip.userLength;
        owner.selectedTrack = instrument ? -2 : -1;
        owner.repaint();
    }

    void migrateLegacyClip()
    {
        const auto notes = owner.midiEngine.getNotesCopy();
        if (notes.empty()) return;
        Clip clip;
        clip.id = nextClipId++;
        clip.startSeconds = owner.midiClipStartSeconds;
        clip.lengthSeconds = juce::jmax(secondsPerMeasure(), owner.midiClipLengthSeconds);
        clip.userLength = owner.midiClipLengthUserDefined;
        clip.notes = notes;
        midiClips.push_back(std::move(clip));
        activeIndex = 0;
        activeInstrument = false;
    }

    void flattenClips(const std::vector<Clip>& clips, std::vector<MidiEngine::NoteEvent>& result) const
    {
        result.clear();
        for (const auto& clip : clips)
        {
            const auto offsetTicks = MidiEngine::secondsToTick(clip.startSeconds, owner.tempoBpm);
            const auto lengthTicks = MidiEngine::secondsToTick(clip.lengthSeconds, owner.tempoBpm);
            for (const auto& note : clip.notes)
            {
                if (note.startTick >= lengthTicks) continue;
                auto absolute = note;
                absolute.startTick += offsetTicks;
                result.push_back(absolute);
            }
        }
    }

    void syncPlayback()
    {
        syncActiveClip();
        std::vector<MidiEngine::NoteEvent> midiNotes;
        flattenClips(midiClips, midiNotes);
        double end = 0.0;
        for (const auto& c : midiClips) end = juce::jmax(end, c.startSeconds + c.lengthSeconds);
        for (const auto& c : instrumentClips) end = juce::jmax(end, c.startSeconds + c.lengthSeconds);
        owner.audioEngine.setMidiNotes(midiNotes, 0.0, end, owner.tempoBpm);
        owner.audioEngine.setProjectExtraLengthSeconds(end);

        std::vector<MidiEngine::NoteEvent> instrumentNotes;
        flattenClips(instrumentClips, instrumentNotes);
        const auto count = juce::jmin((size_t)maxInstrumentNotes, instrumentNotes.size());
        for (size_t i = 0; i < count; ++i)
        {
            const auto& note = instrumentNotes[i];
            const auto start = MidiEngine::tickToSeconds(note.startTick, owner.tempoBpm);
            const auto finish = MidiEngine::tickToSeconds(note.startTick + note.lengthTicks, owner.tempoBpm);
            instrumentPlayback[i].start.store(start, std::memory_order_relaxed);
            instrumentPlayback[i].end.store(finish, std::memory_order_relaxed);
            instrumentPlayback[i].frequency.store(440.0 * std::pow(2.0, (static_cast<int>(note.pitch) - 69) / 12.0), std::memory_order_relaxed);
            instrumentPlayback[i].amplitude.store(0.045f * ((float)note.velocity / 127.0f), std::memory_order_relaxed);
        }
        instrumentNoteCount.store(count, std::memory_order_release);
    }

    void drawClips(juce::Graphics& g, bool instrument, const std::vector<Clip>& clips, int row, int colourIndex)
    {
        const int y = 76 + rulerHeight + row * rowHeight;
        const auto colour = trackColour(getLibertyTrackColourId(colourIndex));
        for (int i = 0; i < (int)clips.size(); ++i)
        {
            const auto& clip = clips[(size_t)i];
            const int x = headerWidth + (int)std::round(clip.startSeconds * pixelsPerSecond);
            const int w = juce::jmax(24, (int)std::round(clip.lengthSeconds * pixelsPerSecond));
            auto r = juce::Rectangle<int>(x, y + 4, w, rowHeight - 8);
            const bool selected = isActiveClip(instrument, i);
            g.setColour(colour.withAlpha(selected ? 0.76f : 0.55f));
            g.fillRoundedRectangle(r.toFloat(), 5.0f);
            g.setColour(selected ? juce::Colours::white : colour.brighter(0.35f));
            g.drawRoundedRectangle(r.toFloat(), 5.0f, selected ? 2.0f : 1.0f);
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(10.0f, juce::Font::bold));
            g.drawText(instrument ? "INSTRUMENT MIDI" : "MIDI CLIP", r.reduced(8, 4), juce::Justification::topLeft, true);

            const auto lengthTicks = MidiEngine::secondsToTick(clip.lengthSeconds, owner.tempoBpm);
            for (const auto& note : clip.notes)
            {
                if (note.startTick >= lengthTicks) continue;
                const float nx = (float)r.getX() + (float)(MidiEngine::tickToSeconds(note.startTick, owner.tempoBpm) * pixelsPerSecond);
                const float nw = juce::jmax(2.0f, (float)(MidiEngine::tickToSeconds(note.lengthTicks, owner.tempoBpm) * pixelsPerSecond));
                const float ny = (float)r.getY() + 10.0f + ((127.0f - note.pitch) / 127.0f) * (float)juce::jmax(1, r.getHeight() - 20);
                if (nx >= r.getRight()) continue;
                g.setColour(juce::Colours::white.withAlpha(0.72f));
                g.fillRoundedRectangle(nx, ny, juce::jmin(nw, (float)r.getRight() - nx), 3.0f, 1.5f);
            }
        }
    }

    bool isInstrumentHeaderButtonPoint(juce::Point<int> p) const
    {
        const int y = 76 + rulerHeight + instrumentRow * rowHeight + 8;
        return juce::Rectangle<int>(160, y, 21, 20).contains(p) || juce::Rectangle<int>(184, y, 21, 20).contains(p);
    }

    void drawInstrumentHeaderButtons(juce::Graphics& g)
    {
        const int y = 76 + rulerHeight + instrumentRow * rowHeight + 8;
        auto mute = juce::Rectangle<int>(160, y, 21, 20);
        auto solo = juce::Rectangle<int>(184, y, 21, 20);
        g.setColour(instrumentMuted ? juce::Colour(0xff9b4545) : juce::Colour(0xff252a31)); g.fillRoundedRectangle(mute.toFloat(), 4.0f);
        g.setColour(instrumentSolo ? juce::Colour(0xff8b7a32) : juce::Colour(0xff252a31)); g.fillRoundedRectangle(solo.toFloat(), 4.0f);
        g.setColour(juce::Colours::white); g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.drawText("M", mute, juce::Justification::centred); g.drawText("S", solo, juce::Justification::centred);
    }

    bool handleInstrumentHeaderButtons(juce::Point<int> p)
    {
        const int y = 76 + rulerHeight + instrumentRow * rowHeight + 8;
        if (juce::Rectangle<int>(160, y, 21, 20).contains(p)) { instrumentMuted = !instrumentMuted; repaint(); return true; }
        if (juce::Rectangle<int>(184, y, 21, 20).contains(p)) { instrumentSolo = !instrumentSolo; repaint(); return true; }
        return false;
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
        g.setColour(master ? juce::Colour(0xff1b2027) : juce::Colour(0xff171b20)); g.fillRoundedRectangle(c.toFloat(), 5.0f);
        if (!master) { g.setColour(colour.withAlpha(0.14f)); g.fillRoundedRectangle(c.toFloat(), 5.0f); }
        g.setColour(juce::Colour(0xff343a44)); g.drawRoundedRectangle(c.toFloat(), 5.0f, 1.0f);
        g.setColour(juce::Colours::white); g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(master ? "MASTER" : getLibertyTrackName(instrumentTrackIndex), c.getX(), c.getY() + 8, c.getWidth(), 20, juce::Justification::centred, true);
        if (!master)
        {
            auto m = juce::Rectangle<int>(c.getX()+8,c.getY()+32,44,20); auto s = juce::Rectangle<int>(c.getX()+58,c.getY()+32,44,20);
            g.setColour(instrumentMuted ? juce::Colour(0xff9b4545) : juce::Colour(0xff252a31)); g.fillRoundedRectangle(m.toFloat(),4.0f);
            g.setColour(instrumentSolo ? juce::Colour(0xff8b7a32) : juce::Colour(0xff252a31)); g.fillRoundedRectangle(s.toFloat(),4.0f);
            g.setColour(juce::Colours::white); g.setFont(juce::Font(9.0f, juce::Font::bold)); g.drawText("M",m,juce::Justification::centred); g.drawText("S",s,juce::Justification::centred);
        }
        const int faderTop = c.getY()+58, faderBottom = c.getBottom()-45;
        auto fader = juce::Rectangle<float>((float)c.getCentreX()-7.0f,(float)faderTop,14.0f,(float)(faderBottom-faderTop));
        g.setColour(juce::Colour(0xff090b0e)); g.fillRoundedRectangle(fader,3.0f);
        const float gain = master ? owner.audioEngine.getMasterGain() : instrumentGain.load();
        const float norm = juce::jlimit(0.0f,1.0f,gain*0.5f); const float ky = fader.getBottom()-norm*fader.getHeight();
        g.setColour(juce::Colour(0xffd6d9de)); g.fillRoundedRectangle(fader.getX()-2.0f,ky-6.0f,fader.getWidth()+4.0f,12.0f,3.0f);
        const auto db = 20.0f*std::log10(juce::jmax(0.000001f,gain)); g.setColour(juce::Colour(0xff858c96)); g.setFont(juce::Font(10.0f));
        g.drawText(db < -59.9f ? "-inf dB" : juce::String(db,1)+" dB",c.getX(),c.getBottom()-38,c.getWidth(),16,juce::Justification::centred);
        g.drawText(master ? "MASTER" : "PAN " + juce::String(instrumentPan.load(),2),c.getX(),c.getBottom()-22,c.getWidth(),16,juce::Justification::centred);
    }

    bool handleMixerMouse(juce::Point<int> p)
    {
        if (!isMixerPoint(p)) return false;
        const int top = owner.getHeight()-210;
        auto inst = juce::Rectangle<int>(720,top+12,116,188);
        auto master = juce::Rectangle<int>(845,top+12,116,188);
        if (inst.contains(p))
        {
            auto m=juce::Rectangle<int>(inst.getX()+8,inst.getY()+32,44,20); auto s=juce::Rectangle<int>(inst.getX()+58,inst.getY()+32,44,20);
            if (m.contains(p)) { instrumentMuted=!instrumentMuted; repaint(); return true; }
            if (s.contains(p)) { instrumentSolo=!instrumentSolo; repaint(); return true; }
            if (p.y>=inst.getY()+58 && p.y<=inst.getBottom()-45) { mixerDragMode=1; dragMixer(p); return true; }
            if (p.y>=inst.getBottom()-28) { mixerDragMode=2; dragMixer(p); return true; }
        }
        if (master.contains(p) && p.y>=master.getY()+58 && p.y<=master.getBottom()-45) { mixerDragMode=3; dragMixer(p); return true; }
        return true;
    }

    void dragMixer(juce::Point<int> p)
    {
        const int top=owner.getHeight()-210;
        auto c = mixerDragMode==3 ? juce::Rectangle<int>(845,top+12,116,188) : juce::Rectangle<int>(720,top+12,116,188);
        if (mixerDragMode==1 || mixerDragMode==3)
        {
            const int ft=c.getY()+58, fb=c.getBottom()-45; const float n=juce::jlimit(0.0f,1.0f,(float)(fb-p.y)/(float)juce::jmax(1,fb-ft));
            if (mixerDragMode==3) owner.audioEngine.setMasterGain(n*2.0f); else instrumentGain.store(n*2.0f);
        }
        else if (mixerDragMode==2)
            instrumentPan.store(juce::jlimit(-1.0f,1.0f,((float)p.x-(float)c.getCentreX())/45.0f));
        repaint();
    }

    void timerCallback() override
    {
        if (stopped.load()) return;
        owner.midiClipOverlay.setVisible(false);
        setBounds(owner.getLocalBounds());
        toFront(false);
        handleProjectPersistence();
        syncPlayback();
        repaint();
    }

    void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputs, int numOutputs, int numSamples, const juce::AudioIODeviceCallbackContext&) override
    {
        for (int ch=0; ch<numOutputs; ++ch) if (outputs[ch]) juce::FloatVectorOperations::clear(outputs[ch],numSamples);
        if (!owner.audioEngine.isPlaying() || instrumentMuted) return;
        if (owner.audioEngine.isAnyTrackSolo() && !instrumentSolo) return;
        const auto rate=owner.audioEngine.getSampleRate(); if (rate<=0.0) return;
        const auto position=owner.audioEngine.transportSamples.load(std::memory_order_relaxed);
        const auto count=instrumentNoteCount.load(std::memory_order_acquire);
        const float gain=instrumentGain.load(); const float pan=instrumentPan.load();
        const float lg=gain*(pan>0.0f?1.0f-pan:1.0f); const float rg=gain*(pan<0.0f?1.0f+pan:1.0f);
        constexpr double twoPi=6.28318530717958647692;
        for (int s=0;s<numSamples;++s)
        {
            const double t=(double)(position+s)/rate; float v=0.0f;
            for (size_t i=0;i<count;++i)
            {
                const double st=instrumentPlayback[i].start.load(); const double en=instrumentPlayback[i].end.load(); if(t<st||t>=en) continue;
                const double nt=t-st; const double dur=en-st; float env=1.0f; if(nt<0.005) env=(float)(nt/0.005); if(dur-nt<0.010) env=juce::jmin(env,(float)((dur-nt)/0.010));
                v+=(float)(std::sin(twoPi*instrumentPlayback[i].frequency.load()*nt)*(double)(instrumentPlayback[i].amplitude.load()*env));
            }
            const float master=owner.audioEngine.getMasterGain();
            if(numOutputs>0&&outputs[0]) outputs[0][s]+=v*lg*master;
            if(numOutputs>1&&outputs[1]) outputs[1][s]+=v*rg*master;
        }
    }

    juce::String serialiseClips() const
    {
        juce::XmlElement root("MultiMidiClips"); root.setAttribute("version",1);
        auto add=[&](const std::vector<Clip>& clips,const char* track)
        {
            for(const auto& c:clips)
            {
                auto* ce=root.createNewChildElement("Clip"); ce->setAttribute("track",track); ce->setAttribute("id",c.id); ce->setAttribute("start",c.startSeconds); ce->setAttribute("length",c.lengthSeconds); ce->setAttribute("userLength",c.userLength);
                for(const auto& n:c.notes){auto* ne=ce->createNewChildElement("Note"); ne->setAttribute("startTick",(double)n.startTick); ne->setAttribute("lengthTicks",(double)n.lengthTicks); ne->setAttribute("pitch",(int)n.pitch); ne->setAttribute("velocity",(int)n.velocity); ne->setAttribute("channel",(int)n.channel);}
            }
        };
        add(midiClips,"midi"); add(instrumentClips,"instrument"); return root.toString();
    }

    void loadClipsFromXml(const juce::XmlElement& root)
    {
        midiClips.clear(); instrumentClips.clear(); activeIndex=-1;
        forEachXmlChildElementWithTagName(root, ce, "Clip")
        {
            Clip c; c.id=ce->getIntAttribute("id",nextClipId++); c.startSeconds=ce->getDoubleAttribute("start",0.0); c.lengthSeconds=ce->getDoubleAttribute("length",secondsPerMeasure()); c.userLength=ce->getBoolAttribute("userLength",false); nextClipId=juce::jmax(nextClipId,c.id+1);
            forEachXmlChildElementWithTagName(*ce, ne, "Note") { MidiEngine::NoteEvent n; n.startTick=(std::int64_t)ne->getDoubleAttribute("startTick",0); n.lengthTicks=(std::int64_t)ne->getDoubleAttribute("lengthTicks",MidiEngine::ticksPerQuarterNote); n.pitch=(std::uint8_t)juce::jlimit(0,127,ne->getIntAttribute("pitch",60)); n.velocity=(std::uint8_t)juce::jlimit(1,127,ne->getIntAttribute("velocity",100)); n.channel=(std::uint8_t)juce::jlimit(1,16,ne->getIntAttribute("channel",1)); c.notes.push_back(n); }
            if(ce->getStringAttribute("track")=="instrument") instrumentClips.push_back(std::move(c)); else midiClips.push_back(std::move(c));
        }
        if(!midiClips.empty()) selectClip(false,0); else if(!instrumentClips.empty()) selectClip(true,0);
    }

    void handleProjectPersistence()
    {
        const auto path=owner.currentProjectFile.getFullPathName();
        if(path!=lastProjectPath)
        {
            lastProjectPath=path;
            if(owner.currentProjectFile.existsAsFile())
            {
                if(auto xml=juce::parseXML(owner.currentProjectFile))
                {
                    if(auto* state=xml->getChildByName("MultiMidiClips")) { loadClipsFromXml(*state); owner.markProjectClean(); }
                    else { midiClips.clear(); instrumentClips.clear(); activeIndex=-1; migrateLegacyClip(); }
                }
            }
        }
        if(!owner.currentProjectFile.existsAsFile() || owner.hasUnsavedChanges()) return;
        auto xml=juce::parseXML(owner.currentProjectFile); if(xml==nullptr) return;
        const auto wanted=serialiseClips(); const auto* existing=xml->getChildByName("MultiMidiClips"); if(existing!=nullptr && existing->toString()==wanted) return;
        if(existing!=nullptr) xml->removeChildElement(existing,true);
        auto state=juce::parseXML(wanted); if(state!=nullptr) xml->addChildElement(state.release());
        auto out=owner.currentProjectFile.createOutputStream(); if(out!=nullptr){out->setPosition(0); out->truncate(); out->writeText(xml->toString(),false,false,"UTF-8"); out->flush();}
    }

    MainComponent& owner;
    std::vector<Clip> midiClips;
    std::vector<Clip> instrumentClips;
    int nextClipId=1;
    int activeIndex=-1;
    bool activeInstrument=false;
    int dragIndex=-1;
    bool dragInstrument=false;
    int dragMode=0;
    float dragStartX=0.0f;
    double dragStartSeconds=0.0;
    double dragStartLength=0.0;
    int mixerDragMode=0;
    bool instrumentMuted=false;
    bool instrumentSolo=false;
    std::atomic<float> instrumentGain {1.0f};
    std::atomic<float> instrumentPan {0.0f};
    std::array<PlaybackNote,maxInstrumentNotes> instrumentPlayback;
    std::atomic<size_t> instrumentNoteCount {0};
    std::atomic<bool> stopped {false};
    juce::String lastProjectPath;
};

class Bootstrap final : private juce::Timer
{
public:
    Bootstrap(){startTimerHz(10);}
    ~Bootstrap() override { shutdown(); }
    void shutdown(){stopTimer(); for(auto& pair:controllers) pair.second->shutdown(); controllers.clear();}
private:
    void timerCallback() override
    {
        auto& desktop=juce::Desktop::getInstance();
        for(int i=0;i<desktop.getNumComponents();++i)
            if(auto* w=dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i)))
                if(auto* main=dynamic_cast<MainComponent*>(w->getContentComponent()))
                    if(controllers.find(main)==controllers.end()) controllers.emplace(main,std::make_unique<MultiMidiClipController>(*main));
    }
    std::map<MainComponent*,std::unique_ptr<MultiMidiClipController>> controllers;
};

Bootstrap bootstrap;
}

void shutdownLibertyMultiMidiClipController()
{
    bootstrap.shutdown();
}
