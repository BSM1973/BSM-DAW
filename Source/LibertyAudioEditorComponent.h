#pragma once
#include <JuceHeader.h>
#include "AudioEngine.h"
#include "LibertyAudioWaveformView.h"
#include "LibertyAudioEditorHeader.h"
#include <functional>
#include <vector>
#include <algorithm>
#include <cmath>

// Reusable audio editor component, decoupled from MainComponent.
// The shared waveform child owns the waveform and marker rendering.
class LibertyAudioEditorComponent final : public juce::Component, private juce::Timer
{
public:
    LibertyAudioEditorComponent(AudioEngine& engine, int trackIndex,
        std::function<double(double)> snapToMusicalGrid,
        std::function<void(double, double, bool)> updateLoop,
        std::function<void()> openSnapSettings,
        std::function<void()> openTimeSettings,
        std::function<void()> openTransientSettings,
        std::function<std::vector<double>()> readTransients,
        std::function<void(double)> removeTransient,
        std::function<double()> readDockZoom,
        std::function<void(double)> writeDockZoom,
        std::function<int()> readTimeFormat = {})
        : audio(engine), track(trackIndex), musicalSnap(std::move(snapToMusicalGrid)),
          publishLoop(std::move(updateLoop)),
          snapSettings(std::move(openSnapSettings)),
          timeSettings(std::move(openTimeSettings)),
          transientSettings(std::move(openTransientSettings)),
          transientMarkers(std::move(readTransients)),
          eraseTransient(std::move(removeTransient)),
          getDockZoom(std::move(readDockZoom)), setDockZoom(std::move(writeDockZoom)),
          getTimeFormat(std::move(readTimeFormat))
    {
        zoom = getDockZoom ? getDockZoom() : 1.0;
        setSize(980, 550);
        warp.setButtonText("WARP");
        warp.setToggleState(audio.isTrackWarpEnabled(track), juce::dontSendNotification);
        warp.onClick = [this] { audio.setTrackWarpEnabled(track, warp.getToggleState()); };
        addAndMakeVisible(warp);
        const juce::StringArray modes { "Warp Ultra Best", "Warp Drums", "Warp Vocal",
            "Warp Granulaire", "Warp Pitch", "Warp Texture" };
        mode.addItemList(modes, 1);
        mode.setSelectedId(audio.getTrackWarpMode(track) + 1, juce::dontSendNotification);
        mode.onChange = [this] { audio.setTrackWarpMode(track, mode.getSelectedId() - 1); };
        addAndMakeVisible(mode);
        pitch.setSliderStyle(juce::Slider::LinearHorizontal);
        pitch.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 24);
        pitch.setRange(-24.0, 24.0, 0.1);
        pitch.setValue(audio.getTrackClipPitch(track), juce::dontSendNotification);
        pitch.onValueChange = [this] { audio.setTrackClipPitch(track, pitch.getValue()); };
        addAndMakeVisible(pitch);
        formants.setSliderStyle(juce::Slider::LinearHorizontal);
        formants.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 24);
        formants.setRange(-12.0, 12.0, 0.1);
        formants.setValue(audio.getTrackWarpFormant(track), juce::dontSendNotification);
        formants.onValueChange = [this] { audio.setTrackWarpFormant(track, formants.getValue()); };
        addAndMakeVisible(formants);
        zoomIn.setButtonText("+");
        zoomOut.setButtonText("-");
        snap.setButtonText("SNAP");
        snap.setClickingTogglesState(true);
        snap.setToggleState(true, juce::dontSendNotification);
        loop.setButtonText("BOUCLE");
        loop.setClickingTogglesState(true);
        loop.onClick = [this] { syncLoop(); repaint(); };
        addAndMakeVisible(loop);
        selection.setButtonText("SELECTION");
        selection.setClickingTogglesState(true);
        addAndMakeVisible(selection);
        snapOptions.setButtonText("GRILLE...");
        timeOptions.setButtonText("TEMPS...");
        transientOptions.setButtonText("TRANSITOIRES...");
        snapOptions.onClick = [this] { if (snapSettings) snapSettings(); };
        timeOptions.onClick = [this] { if (timeSettings) timeSettings(); };
        transientOptions.onClick = [this] { if (transientSettings) transientSettings(); };
        addAndMakeVisible(snapOptions);
        addAndMakeVisible(timeOptions);
        addAndMakeVisible(transientOptions);
        resetMarkers.setButtonText("REINIT. MARQUEURS");
        zoomIn.onClick = [this] { zoom = juce::jmin(64.0, zoom * 2.0); if (setDockZoom) setDockZoom(zoom); repaint(); };
        zoomOut.onClick = [this] { zoom = juce::jmax(1.0, zoom * 0.5); if (setDockZoom) setDockZoom(zoom); repaint(); };
        resetMarkers.onClick = [this] { audio.resetTrackWarpMarkers(track); repaint(); };
        addAndMakeVisible(zoomIn);
        addAndMakeVisible(zoomOut);
        addAndMakeVisible(snap);
        addAndMakeVisible(resetMarkers);
        waveform.getBuffer = [this] { return audio.getAudioBufferSnapshot(track); };
        waveform.getDuration = [this] { return audio.getAudioFileLengthSeconds(track); };
        waveform.getSourceDuration = [this]
        {
            const auto pcm = audio.getAudioBufferSnapshot(track);
            const double rate = audio.getAudioBufferSampleRate(track);
            return pcm && rate > 0.0 ? pcm->getNumSamples() / rate
                                     : audio.getAudioFileLengthSeconds(track);
        };
        waveform.getWarpPairs = [this]
        {
            std::vector<std::pair<double, double>> pairs;
            if (!audio.isTrackWarpEnabled(track)) return pairs;
            const int count = audio.getTrackWarpMarkerCount(track);
            pairs.reserve((size_t)count);
            for (int index = 0; index < count; ++index)
                pairs.emplace_back(audio.getTrackWarpMarkerSourceSeconds(track, index),
                                   audio.getTrackWarpMarkerTargetSeconds(track, index));
            return pairs;
        };
        waveform.getMarkers = [this]
        {
            std::vector<LibertyAudioWaveformView::Marker> markers;
            if (transientMarkers)
                for (double t : transientMarkers()) markers.push_back({ t, true });
            for (int index = 0; index < audio.getTrackWarpMarkerCount(track); ++index)
                markers.push_back({ audio.getTrackWarpMarkerTargetSeconds(track, index), false });
            return markers;
        };
        waveform.onPress = [this](double, const juce::MouseEvent& e) { mouseDown(e.getEventRelativeTo(this)); };
        waveform.onMove = [this](double, const juce::MouseEvent& e) { mouseDrag(e.getEventRelativeTo(this)); };
        waveform.onRelease = [this](double, const juce::MouseEvent& e) { mouseUp(e.getEventRelativeTo(this)); };
        addAndMakeVisible(waveform);
        startTimerHz(12);
    }
    void resized() override
    {
        warp.setBounds(24, 52, 100, 32);
        mode.setBounds(145, 52, 220, 32);
        pitch.setBounds(90, 110, getWidth() - 120, 32);
        formants.setBounds(90, 157, getWidth() - 120, 32);
        zoomOut.setBounds(24, 200, 36, 28);
        zoomIn.setBounds(65, 200, 36, 28);
        snap.setBounds(116, 200, 90, 28);
        resetMarkers.setBounds(220, 200, 180, 28);
        selection.setBounds(410, 200, 120, 28);
        loop.setBounds(540, 200, 110, 28);
        snapOptions.setBounds(660, 200, 100, 28);
        timeOptions.setBounds(770, 200, 100, 28);
        transientOptions.setBounds(24, 235, 160, 28);
        waveform.setBounds(waveformBounds());
    }
    juce::Rectangle<int> waveformBounds() const
    {
        auto bounds = getLocalBounds().reduced(24);
        bounds.removeFromTop(270);
        return bounds;
    }
    // Shared time/viewport transform for rendering and mouse interactions.
    double secondsAt(int x) const
    {
        const LibertyAudioWaveformView::Viewport viewport {
            waveformBounds(), audio.getAudioFileLengthSeconds(track), zoom, 0.0 };
        double seconds = viewport.secondsAt(x);
        if (snap.getToggleState() && musicalSnap)
            seconds = musicalSnap(seconds);
        return seconds;
    }
    int markerPixel(double seconds) const
    {
        const LibertyAudioWaveformView::Viewport viewport {
            waveformBounds(), audio.getAudioFileLengthSeconds(track), zoom, 0.0 };
        return viewport.pixelAt(seconds);
    }
    LibertyAudioWaveformView::WarpEditActions warpActions()
    {
        return {
            [this](double source, double target)
            { return audio.addTrackWarpMarker(track, source, target); },
            [this](int index, double target)
            { audio.moveTrackWarpMarker(track, index, target); },
            [this](int index)
            { audio.removeTrackWarpMarker(track, index); }
        };
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        const auto toolbar = getLocalBounds().reduced(14, 12);
        const bool compact = toolbar.getWidth() < 600;
        const int barWidth = juce::jmin(280, juce::jmax(0, toolbar.getWidth()));
        const auto bar = juce::Rectangle<int>(toolbar.getRight() - barWidth,
            toolbar.getY() + (compact ? 44 : 22), barWidth, 21);
        if (bar.contains(event.getPosition()))
        {
            if (event.mods.isRightButtonDown()) return;
            const int slot = juce::jlimit(0, 3,
                (event.x - bar.getX()) * 4 / juce::jmax(1, bar.getWidth()));
            const double duration = audio.getAudioFileLengthSeconds(track);
            if (slot == 0) { selectionBegin = 0.0; selectionEnd = duration; }
            else if (slot == 1) { selectionBegin = -1.0; selectionEnd = -1.0; }
            else if (slot == 2)
            {
                selectionBegin = juce::jlimit(0.0, duration,
                    selectionEnd >= 0.0 ? selectionEnd : 0.0);
                if (selectionEnd < 0.0) selectionEnd = duration;
            }
            else
            {
                selectionEnd = juce::jlimit(0.0, duration,
                    selectionBegin >= 0.0 ? selectionBegin : duration);
                if (selectionBegin < 0.0) selectionBegin = 0.0;
            }
            syncLoop();
            waveform.repaint();
            repaint();
            return;
        }
        const auto formatButton = juce::Rectangle<int>(
            toolbar.getX(), toolbar.getY() + (compact ? 70 : 46),
            juce::jmin(126, juce::jmax(0, toolbar.getWidth())), 20);
        if (formatButton.contains(event.getPosition()))
        {
            if (!event.mods.isRightButtonDown() && timeSettings) timeSettings();
            return;
        }
        const auto bounds = waveformBounds();
        if (!bounds.contains(event.getPosition())) return;
        const double seconds = secondsAt(event.x);
        draggingMarker = -1;
        if (event.mods.isRightButtonDown() && transientMarkers && eraseTransient)
        {
            const auto markers = transientMarkers();
            const double duration = juce::jmax(0.001, audio.getAudioFileLengthSeconds(track));
            for (const double onset : markers)
            {
                const int x = markerPixel(onset);
                if (std::abs(x - event.x) <= 7)
                {
                    eraseTransient(onset);
                    repaint();
                    return;
                }
            }
        }
        if (selection.getToggleState())
        {
            selecting = true;
            selectionBegin = seconds;
            selectionEnd = seconds;
            repaint();
            return;
        }
        const int count = audio.getTrackWarpMarkerCount(track);
        std::vector<LibertyAudioWaveformView::Marker> candidates;
        for (int i = 1; i < count - 1; ++i)
            candidates.push_back({ audio.getTrackWarpMarkerTargetSeconds(track, i), false });
        const LibertyAudioWaveformView::Viewport viewport {
            waveformBounds(), audio.getAudioFileLengthSeconds(track), zoom, 0.0 };
        const int nearest = viewport.nearestMarker(seconds, candidates, 8);
        if (nearest >= 0) draggingMarker = nearest + 1;
        if (event.mods.isRightButtonDown())
        {
            if (draggingMarker > 0) warpActions().remove(draggingMarker);
            draggingMarker = -1;
        }
        else if (draggingMarker < 0)
        {
            warpActions().add(seconds, seconds);
        }
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (selecting)
        {
            selectionEnd = secondsAt(event.x);
            repaint();
            return;
        }
        if (draggingMarker > 0)
        {
            warpActions().drag(draggingMarker, secondsAt(event.x));
            repaint();
        }
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (selecting) syncLoop();
        draggingMarker = -1;
        selecting = false;
    }
    void syncLoop()
    {
        if (publishLoop)
            publishLoop(selectionBegin, selectionEnd, loop.getToggleState());
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff101e2d));
        LibertyAudioEditorHeader::paint(g, getLocalBounds(),
            "Piste " + juce::String(track + 1), zoom);
        LibertyAudioEditorHeader::paintSelectionButtons(g, getLocalBounds().reduced(14, 12));
        const auto inner = getLocalBounds().reduced(14, 12);
        const bool compact = inner.getWidth() < 600;
        const auto formatButton = juce::Rectangle<int>(
            inner.getX(), inner.getY() + (compact ? 70 : 46),
            juce::jmin(126, juce::jmax(0, inner.getWidth())), 20);
        LibertyAudioEditorHeader::paintTimeFormat(g, formatButton,
            getTimeFormat ? getTimeFormat() : 3);
        const auto warpButton = juce::Rectangle<int>(
            formatButton.getRight() + 6, formatButton.getY(),
            juce::jmin(112, juce::jmax(0, inner.getRight() - formatButton.getRight() - 6)), 20);
        LibertyAudioEditorHeader::paintWarpToggle(g, warpButton,
            audio.isTrackWarpEnabled(track));
        g.setFont(juce::Font(13.0f));
        g.drawText("PITCH", 24, 110, 66, 32, juce::Justification::centredLeft);
        g.drawText("FORMANTS", 24, 157, 75, 32, juce::Justification::centredLeft);
        // The shared waveform child owns waveform, selection and marker rendering.
    }
private:
    void timerCallback() override { waveform.zoom = zoom;
        waveform.selectionStart = selectionBegin;
        waveform.selectionEnd = selectionEnd;
        waveform.loopSelection = loop.getToggleState();
        waveform.repaint(); }
    LibertyAudioWaveformView waveform;
    AudioEngine& audio;
    int track;
    juce::ToggleButton warp;
    juce::ComboBox mode;
    juce::Slider pitch, formants;
    juce::TextButton zoomIn, zoomOut, resetMarkers;
    juce::TextButton snapOptions, timeOptions, transientOptions;
    std::function<void(double, double, bool)> publishLoop;
    std::function<void()> snapSettings, timeSettings, transientSettings;
    std::function<std::vector<double>()> transientMarkers;
    std::function<void(double)> eraseTransient;
    std::function<double()> getDockZoom;
    std::function<void(double)> setDockZoom;
    std::function<int()> getTimeFormat;
    juce::ToggleButton snap, loop, selection;
    std::function<double(double)> musicalSnap;
    double selectionBegin = -1.0, selectionEnd = -1.0;
    bool selecting = false;
    double zoom = 1.0;
    int draggingMarker = -1;
};
