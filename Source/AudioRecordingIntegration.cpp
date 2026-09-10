#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>

#define private public
#include "MainComponent.h"
#undef private

namespace
{
class Recorder final : private juce::AudioIODeviceCallback
{
public:
    explicit Recorder(juce::AudioDeviceManager& m) : manager(m) { manager.addAudioCallback(this); }
    ~Recorder() override { stop(); manager.removeAudioCallback(this); }
    bool start()
    {
        if (recording.load()) return false;
        auto* device = manager.getCurrentAudioDevice();
        if (device == nullptr || device->getActiveInputChannels().countNumberOfSetBits() == 0) return false;
        const auto rate = device->getCurrentSampleRate();
        if (rate <= 0.0) return false;
        recordingChannels = juce::jlimit(1, 2, device->getActiveInputChannels().countNumberOfSetBits());
        buffer.setSize(recordingChannels, static_cast<int>(std::ceil(rate * 600.0)), false, true, true);
        buffer.clear(); writePosition.store(0); recordingRate = rate; recording.store(true); return true;
    }
    void stop() { recording.store(false); while (callbackActive.load()) juce::Thread::yield(); }
    bool isRecording() const noexcept { return recording.load(); }
    int getRecordedSamples() const noexcept { return writePosition.load(); }
    bool writeToWav(const juce::File& file) const
    {
        const int samples = getRecordedSamples();
        if (samples <= 0 || recordingChannels <= 0 || recordingRate <= 0.0) return false;
        std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream()); if (stream == nullptr) return false;
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), recordingRate, static_cast<unsigned int>(recordingChannels), 24, {}, 0));
        if (writer == nullptr) return false; stream.release(); return writer->writeFromAudioSampleBuffer(buffer, 0, samples);
    }
private:
    void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext(const float* const* input, int numInput, float* const*, int, int numSamples, const juce::AudioIODeviceCallbackContext&) override
    {
        callbackActive.store(true);
        if (recording.load() && input != nullptr)
        {
            const int current = writePosition.load();
            const int count = juce::jmin(numSamples, juce::jmax(0, buffer.getNumSamples() - current));
            const int channels = juce::jmin(recordingChannels, numInput);
            for (int c = 0; c < channels; ++c) if (input[c] != nullptr) buffer.copyFrom(c, current, input[c], count);
            if (count > 0) writePosition.store(current + count);
            if (count < numSamples) recording.store(false);
        }
        callbackActive.store(false);
    }
    juce::AudioDeviceManager& manager; juce::AudioBuffer<float> buffer;
    std::atomic<bool> recording { false }, callbackActive { false }; std::atomic<int> writePosition { 0 };
    double recordingRate = 0.0; int recordingChannels = 0;
};

class RecordingController final : public juce::Component, private juce::Timer
{
public:
    explicit RecordingController(MainComponent& o) : owner(o), recorder(o.audioEngine.getDeviceManager()) { setInterceptsMouseClicks(true, false); owner.addAndMakeVisible(this); startTimerHz(20); }
    ~RecordingController() override { stopRecording(); }
    void paint(juce::Graphics& g) override
    {
        const auto r = getLocalBounds().reduced(1); g.setColour(recorder.isRecording() ? juce::Colour(0xffa83f46) : juce::Colour(0xff252a31)); g.fillRoundedRectangle(r.toFloat(), 5.0f); g.setColour(juce::Colour(0xff454b54)); g.drawRoundedRectangle(r.toFloat(), 5.0f, 1.0f); g.setColour(juce::Colours::white); g.setFont(juce::Font(11.0f, juce::Font::bold)); g.drawText(recorder.isRecording() ? "STOP REC" : "REC", r, juce::Justification::centred);
    }
    void mouseDown(const juce::MouseEvent&) override { if (recorder.isRecording()) stopRecording(); else startRecording(); }
private:
    void timerCallback() override { setBounds(525, 38, 56, 28); repaint(); }
    void startRecording()
    {
        if (owner.selectedTrack < 0 || owner.selectedTrack >= AudioEngine::maxAudioTracks) { juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Recording", "Select an Audio track before recording.", "OK"); return; }
        auto* device = owner.audioEngine.getDeviceManager().getCurrentAudioDevice();
        if (device == nullptr || device->getActiveInputChannels().countNumberOfSetBits() == 0) { juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Recording", "No audio input is available. Select an input in AUDIO SETTINGS.", "OK"); return; }
        recordingTrack = owner.selectedTrack; recordingStartSeconds = owner.audioEngine.getCurrentTimeSeconds(); savedExtraLength = owner.audioEngine.getProjectExtraLengthSeconds(); owner.audioEngine.setProjectExtraLengthSeconds(juce::jmax(savedExtraLength, recordingStartSeconds + 600.0));
        if (!recorder.start()) return; owner.audioEngine.setPlaying(true); owner.isPlaying = true; owner.repaint();
    }
    void stopRecording()
    {
        if (!recorder.isRecording() && recorder.getRecordedSamples() <= 0) return; recorder.stop(); owner.audioEngine.setPlaying(false); owner.isPlaying = false; owner.audioEngine.setProjectExtraLengthSeconds(savedExtraLength);
        const int samples = recorder.getRecordedSamples(); if (samples <= 0) { owner.repaint(); return; }
        juce::File folder = owner.currentProjectFile.existsAsFile() ? owner.currentProjectFile.getSiblingFile(owner.currentProjectFile.getFileNameWithoutExtension() + "_Media") : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("BSM DAW Recordings"); folder.createDirectory();
        const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S"); const auto file = folder.getNonexistentChildFile("Recording_" + stamp + "_Audio_" + juce::String(recordingTrack + 1), ".wav");
        if (!recorder.writeToWav(file)) { juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Recording", "The recording could not be written to WAV.", "OK"); owner.repaint(); return; }
        juce::String error; if (!owner.audioEngine.loadAudioFileIntoTrack(recordingTrack, file, error)) { juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Recording", error, "OK"); owner.repaint(); return; }
        owner.audioEngine.setTrackStartSeconds(recordingTrack, recordingStartSeconds); owner.trackSourceFiles[static_cast<size_t>(recordingTrack)] = file; owner.selectedTrack = recordingTrack; owner.playheadSeconds = recordingStartSeconds + owner.audioEngine.getAudioFileLengthSeconds(recordingTrack); owner.audioEngine.setCurrentTimeSeconds(owner.playheadSeconds); owner.rebuildWaveformCache(recordingTrack); owner.repaint();
    }
    MainComponent& owner; Recorder recorder; int recordingTrack = -1; double recordingStartSeconds = 0.0; double savedExtraLength = 0.0;
};
class Bootstrap final : private juce::Timer
{
public: Bootstrap() { startTimerHz(20); }
private: void timerCallback() override { auto& desktop = juce::Desktop::getInstance(); for (int i = 0; i < desktop.getNumComponents(); ++i) { auto* w = dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i)); if (w == nullptr) continue; auto* m = dynamic_cast<MainComponent*>(w->getContentComponent()); if (m == nullptr) continue; if (controllers.find(m) == controllers.end()) controllers.emplace(m, std::make_unique<RecordingController>(*m)); } }
    std::map<MainComponent*, std::unique_ptr<RecordingController>> controllers;
};
Bootstrap bootstrap;
}
