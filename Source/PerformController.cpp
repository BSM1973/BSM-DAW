#define private public
#include "MainComponent.h"
#undef private

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "PerformSessionTiming.h"
#include "StepSequencer.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <algorithm>
#include <cstdint>
#include <cmath>

bool isLibertyMixConsoleVisible(MainComponent* owner);
void setLibertyMixConsoleVisible(MainComponent* owner, bool shouldShow);
int getLibertyTrackColourId(int track);
juce::String getLibertyTrackName(int track);

namespace
{
constexpr int transportHeight = 76;
constexpr int sceneCount = 8;

juce::Colour trackColour(int id)
{
    static constexpr std::array<juce::uint32, 9> colours {
        0xff31506a, 0xff3b82f6, 0xff22c55e, 0xffeab308, 0xfff97316,
        0xffef4444, 0xffa855f7, 0xffec4899, 0xff14b8a6
    };
    return juce::Colour(colours[(size_t)juce::jlimit(0, 8, id)]);
}

bool isSupportedAudioFile(const juce::File& file)
{
    const auto ext = file.getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aif" || ext == ".aiff";
}

class PerformAudioPlayer final : private juce::AudioIODeviceCallback
{
public:
    explicit PerformAudioPlayer(MainComponent& ownerIn) : owner(ownerIn)
    {
        formats.registerBasicFormats();
        tracks.resize((size_t)owner.getTotalArrangeTrackCount());
        // AudioEngine is the sole owner of the hardware output callback.
        callbackAttached = false;
    }

    ~PerformAudioPlayer() override
    {
        if (callbackAttached)
        {
            owner.audioEngine.getDeviceManager().removeAudioCallback(this);
            callbackAttached = false;
        }
        const juce::ScopedLock sl(lock);
        stopCaptureInternal();
    }

    void syncTrackCount()
    {
        const juce::ScopedLock sl(lock);
        tracks.resize((size_t)juce::jmax(0,owner.getTotalArrangeTrackCount()));
    }

    void setEnabled(bool shouldEnable)
    {
        syncTrackCount();
        const juce::ScopedLock sl(lock);
        enabled.store(shouldEnable,std::memory_order_release);
        if(!shouldEnable)
        {
            for(auto& track:tracks)
            {
                track.activeScene=-1;
                track.queuedScene=-2;
                track.panicNext=true;
            }
            sessionSamples=0;
            owner.audioEngine.notifyInstrumentChanged();
        }
    }

    bool prepareAudioClip(int track, int scene, const juce::File& file, juce::String& error)
    {
        if(track<0 || track>=owner.getAudioTrackCount() || scene<0 || scene>=sceneCount
            || !file.existsAsFile()) {error="Fichier audio introuvable.";return false;}
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if(!reader || reader->lengthInSamples<=0 || reader->lengthInSamples>0x7fffffff)
        {error="Fichier audio non pris en charge.";return false;}
        const int channels=juce::jlimit(1,2,(int)reader->numChannels);
        const int n=(int)reader->lengthInSamples;
        juce::AudioBuffer<float> source(channels,n);
        source.clear();
        if(!reader->read(&source,0,n,0,true,true))
        {error="Lecture audio impossible.";return false;}
        const double deviceRate=juce::jmax(1.0,owner.audioEngine.getSampleRate());
        const double sourceRate=juce::jmax(1.0,reader->sampleRate);
        const int target=(int)juce::jlimit(1.0,(double)0x7fffffff,
            std::round((double)n*deviceRate/sourceRate));
        auto buffer=std::make_shared<juce::AudioBuffer<float>>(channels,target);
        buffer->clear();
        if(std::abs(sourceRate-deviceRate)<0.5)
            for(int ch=0;ch<channels;++ch)buffer->copyFrom(ch,0,source,ch,0,target);
        else
            for(int ch=0;ch<channels;++ch)
            {
                juce::LagrangeInterpolator converter;
                converter.process(sourceRate/deviceRate,source.getReadPointer(ch),
                                  buffer->getWritePointer(ch),target);
            }
        syncTrackCount();
        const juce::ScopedLock sl(lock);
        if(track>=(int)tracks.size())return false;
        auto& state=tracks[(size_t)track];
        state.slots[(size_t)scene].audio=std::move(buffer);
        state.slots[(size_t)scene].notes.clear();
        state.slots[(size_t)scene].midiLengthTicks=0;
        state.slots[(size_t)scene].valid=true;
        if(state.activeScene==scene) {state.activeScene=-1;state.panicNext=true;}
        return true;
    }

    bool prepareMidiClip(int track, int scene,
                         const std::vector<MidiEngine::NoteEvent>& notes,
                         std::int64_t lengthTicks)
    {
        if(track<owner.getAudioTrackCount() || track>=owner.getTotalArrangeTrackCount()
            || scene<0 || scene>=sceneCount || lengthTicks<=0)return false;
        syncTrackCount();
        const juce::ScopedLock sl(lock);
        if(track>=(int)tracks.size())return false;
        auto& state=tracks[(size_t)track];
        auto& slot=state.slots[(size_t)scene];
        slot.audio.reset();
        slot.notes=notes;
        slot.midiLengthTicks=lengthTicks;
        slot.valid=true;
        if(state.activeScene==scene){state.activeScene=-1;state.panicNext=true;}
        return true;
    }

    bool hasClip(int track,int scene) const
    {
        const juce::ScopedLock sl(lock);
        return track>=0 && track<(int)tracks.size() && scene>=0 && scene<sceneCount
            && tracks[(size_t)track].slots[(size_t)scene].valid;
    }

    void queueScene(int track,int scene,int quantize,double bpm,int numerator,int denominator)
    {
        const juce::ScopedLock sl(lock);
        if(track<0 || track>=(int)tracks.size() || scene>=sceneCount || scene< -1)return;
        auto& state=tracks[(size_t)track];
        if(scene>=0 && !state.slots[(size_t)scene].valid)return;
        const double rate=juce::jmax(1.0,owner.audioEngine.getSampleRate());
        const auto q=LibertyPerformTiming::quantumSamples(quantize,bpm,numerator,denominator,rate);
        sessionTempo.store(juce::jmax(1.0,bpm),std::memory_order_release);
        state.queuedAt=LibertyPerformTiming::nextBoundary(sessionSamples,q);
        state.queuedScene=scene;
    }

    void stopTrack(int track)
    {
        const juce::ScopedLock sl(lock);
        if(track<0 || track>=(int)tracks.size())return;
        auto& s=tracks[(size_t)track];s.activeScene=-1;s.queuedScene=-2;s.panicNext=true;
    }
    void stopAll()
    {
        const juce::ScopedLock sl(lock);
        for(auto& s:tracks){s.activeScene=-1;s.queuedScene=-2;s.panicNext=true;}
    }
    void resetSession()
    {
        const juce::ScopedLock sl(lock);
        for(auto& s:tracks){s=TrackState{};}
        sessionSamples=lastBlockStart=0;
    }
    bool isTrackPlaying(int track,int scene=-1) const
    {
        const juce::ScopedLock sl(lock);
        return track>=0 && track<(int)tracks.size()
            && tracks[(size_t)track].activeScene>=0
            && (scene<0 || tracks[(size_t)track].activeScene==scene);
    }
    bool isTrackQueued(int track,int scene) const
    {
        const juce::ScopedLock sl(lock);
        return track>=0 && track<(int)tracks.size()
            && tracks[(size_t)track].queuedScene==scene;
    }
    void setLoopEnabled(bool value) noexcept { loopEnabled.store(value,std::memory_order_release); }
    void setTempoBpm(double bpm) noexcept
    {sessionTempo.store(juce::jmax(1.0,bpm),std::memory_order_release);}

    void appendMidiForInstrument(int instrument,juce::MidiBuffer& midi,int numSamples)
    {
        if(!enabled.load(std::memory_order_acquire) || numSamples<=0)return;
        if(!lock.tryEnter())return;
        const int audioTracks=owner.getAudioTrackCount();
        const int midiTracks=owner.getMidiTrackCount();
        const int instrumentTracks=owner.getInstrumentTrackCount();
        const double bpm=sessionTempo.load(std::memory_order_acquire);
        const double rate=juce::jmax(1.0,owner.audioEngine.getSampleRate());
        const std::int64_t blockStart=lastBlockStart;
        const bool repeat=loopEnabled.load(std::memory_order_acquire);

        for(int logical=audioTracks;logical<(int)tracks.size();++logical)
        {
            if(LibertyPerformTiming::instrumentForLogicalTrack(
                logical,audioTracks,midiTracks,instrumentTracks)!=instrument)continue;
            const auto& state=tracks[(size_t)logical];
            if(state.panicOffset>=0)
                for(int channel=1;channel<=16;++channel)
                {
                    midi.addEvent(juce::MidiMessage::allNotesOff(channel),state.panicOffset);
                    midi.addEvent(juce::MidiMessage::allSoundOff(channel),state.panicOffset);
                }
            const auto emit=[&](int scene,std::int64_t startedAt,int from,int to)
            {
                if(scene<0 || from>=to)return;
                const auto& slot=state.slots[(size_t)scene];
                if(!slot.valid || slot.midiLengthTicks<=0)return;
                const auto cycle=juce::jmax<std::int64_t>(1,
                    LibertyPerformTiming::ticksToSamples(slot.midiLengthTicks,bpm,rate));
                const auto phaseStart=blockStart+from;
                const auto phaseEnd=blockStart+to;
                const auto relative=phaseStart-startedAt;
                const auto first=juce::jmax<std::int64_t>(0,relative>0?relative/cycle-1:0);
                const auto last=repeat?
                    juce::jmax<std::int64_t>(0,(phaseEnd-startedAt)/cycle+1):0;
                for(std::int64_t iteration=first;iteration<=last;++iteration)
                {
                    const std::int64_t base=startedAt+iteration*cycle;
                    if(base>=phaseEnd)break;
                    for(const auto& note:slot.notes)
                    {
                        if(note.muted || note.lengthTicks<=0 || note.startTick<0)continue;
                        const auto start=base+LibertyPerformTiming::ticksToSamples(note.startTick,bpm,rate);
                        const auto end=juce::jmin(base+cycle,
                            base+LibertyPerformTiming::ticksToSamples(
                                note.startTick+note.lengthTicks,bpm,rate));
                        const int channel=juce::jlimit(1,16,(int)note.channel);
                        const int pitch=juce::jlimit(0,127,(int)note.pitch);
                        const float velocity=juce::jlimit(0.f,1.f,(float)note.velocity/127.f);
                        if(start>=phaseStart && start<phaseEnd)
                            midi.addEvent(juce::MidiMessage::noteOn(channel,pitch,velocity),
                                juce::jlimit(0,numSamples-1,(int)(start-blockStart)));
                        if(end>=phaseStart && end<phaseEnd && end>start)
                            midi.addEvent(juce::MidiMessage::noteOff(channel,pitch),
                                juce::jlimit(0,numSamples-1,(int)(end-blockStart)));
                    }
                    if(!repeat)break;
                }
            };
            if(state.transitionOffset>=0)
            {
                emit(state.previousScene,state.previousStartedAt,0,state.transitionOffset);
                emit(state.activeScene,state.startedAt,state.transitionOffset,numSamples);
            }
            else
                emit(state.activeScene,state.startedAt,0,numSamples);
        }
        lock.exit();
    }

    bool startCapture(juce::String& error)
    {
        const juce::ScopedLock sl(lock);
        if (captureActive) return true;

        const double rate = owner.audioEngine.getSampleRate();
        if (rate <= 0.0)
        {
            error = "No audio device is available.";
            return false;
        }

        auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("BSM").getChildFile("Liberty").getChildFile("Perform Captures");
        if (!folder.createDirectory().wasOk() && !folder.isDirectory())
        {
            error = "The PERFORM capture folder could not be created.";
            return false;
        }

        captureFile = folder.getNonexistentChildFile("Perform Capture " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S"), ".wav", false);
        std::unique_ptr<juce::FileOutputStream> stream(captureFile.createOutputStream());
        if (stream == nullptr)
        {
            error = "The PERFORM capture file could not be created.";
            return false;
        }

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(stream.get(), rate, 2, 24, {}, 0));
        if (writer == nullptr)
        {
            error = "The PERFORM capture writer could not be created.";
            return false;
        }
        stream.release();

        captureThread = std::make_unique<juce::TimeSliceThread>("Liberty PERFORM Capture");
        captureThread->startThread();
        captureWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(writer.release(), *captureThread, 65536);
        captureActive = true;
        return true;
    }

    juce::File stopCapture()
    {
        const juce::ScopedLock sl(lock);
        return stopCaptureInternal();
    }

    bool isCapturing() const noexcept { return captureActive.load(std::memory_order_relaxed); }

    void renderAudioBlock(float* const* outputs, int numOutputs, int numSamples)
    {
        audioDeviceIOCallbackWithContext(nullptr, 0, outputs, numOutputs, numSamples, {});
    }

private:
    struct Slot
    {
        std::shared_ptr<juce::AudioBuffer<float>> audio;
        std::vector<MidiEngine::NoteEvent> notes;
        std::int64_t midiLengthTicks=0;
        bool valid=false;
    };
    struct TrackState
    {
        std::array<Slot,sceneCount> slots;
        int activeScene=-1;
        int queuedScene=-2; // -2 means no pending action, -1 means stop.
        std::int64_t queuedAt=0;
        std::int64_t startedAt=0;
        int previousScene=-1;
        std::int64_t previousStartedAt=0;
        int transitionOffset=-1;
        int panicOffset=-1;
        bool panicNext=false;
    };

    juce::File stopCaptureInternal()
    {
        captureActive.store(false, std::memory_order_relaxed);
        captureWriter.reset();
        if (captureThread != nullptr)
        {
            captureThread->stopThread(2000);
            captureThread.reset();
        }
        return captureFile;
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override
    {
        const int block = device != nullptr ? juce::jmax(32, device->getCurrentBufferSizeSamples()) : 512;
        captureMix.setSize(2, block, false, false, true);
    }

    void audioDeviceStopped() override
    {
        const juce::ScopedLock sl(lock);
        for (auto& track : tracks) { track.activeScene = -1; track.queuedScene = -2; track.panicNext = true; }
        sessionSamples = lastBlockStart = 0;
        if (captureActive.load(std::memory_order_relaxed))
            stopCaptureInternal();
    }

    void audioDeviceIOCallbackWithContext(const float* const*, int,
                                          float* const* outputs,int channels,int samples,
                                          const juce::AudioIODeviceCallbackContext&) override
    {
        if(!enabled.load(std::memory_order_acquire) || samples<=0 || !lock.tryEnter())return;
        const auto blockStart=sessionSamples;
        lastBlockStart=blockStart;
        const double rate=juce::jmax(1.0,owner.audioEngine.getSampleRate());
        const double bpm=sessionTempo.load(std::memory_order_acquire);
        const bool repeat=loopEnabled.load(std::memory_order_acquire);
        const int captureSamples=juce::jmin(samples,captureMix.getNumSamples());
        if(captureSamples>0)captureMix.clear(0,captureSamples);

        for(int logical=0;logical<(int)tracks.size();++logical)
        {
            auto& state=tracks[(size_t)logical];
            state.panicOffset=state.panicNext?0:-1;
            state.panicNext=false;
            state.previousScene=state.activeScene;
            state.previousStartedAt=state.startedAt;
            state.transitionOffset=-1;
            const bool isAudio=logical<owner.getAudioTrackCount();
            const bool muted=isAudio?owner.audioEngine.isTrackMuted(logical):false;
            const bool soloed=isAudio?owner.audioEngine.isTrackSolo(logical):false;
            const bool audible=!muted && (!isAudio || !owner.audioEngine.isAnyTrackSolo() || soloed);
            const float gain=isAudio?owner.audioEngine.getTrackGain(logical):1.0f;
            const float pan=isAudio?owner.audioEngine.getTrackPan(logical):0.0f;
            const float left=gain*(pan>0.0f?1.0f-pan:1.0f);
            const float right=gain*(pan<0.0f?1.0f+pan:1.0f);

            for(int i=0;i<samples;++i)
            {
                const auto absolute=blockStart+i;
                if(state.queuedScene!=-2 && absolute>=state.queuedAt)
                {
                    state.activeScene=state.queuedScene;
                    state.startedAt=state.queuedAt;
                    state.transitionOffset=i;
                    state.queuedScene=-2;
                    state.panicOffset=i;
                }
                if(state.activeScene<0)continue;
                auto& slot=state.slots[(size_t)state.activeScene];
                const auto relative=absolute-state.startedAt;
                if(relative<0)continue;
                const auto length=slot.audio?
                    (std::int64_t)slot.audio->getNumSamples():
                    LibertyPerformTiming::ticksToSamples(slot.midiLengthTicks,bpm,rate);
                if(length<=0)
                {state.activeScene=-1;state.panicOffset=i;state.transitionOffset=i;continue;}
                if(!repeat && relative>=length)
                {state.activeScene=-1;state.panicOffset=i;state.transitionOffset=i;continue;}
                if(!isAudio || slot.audio==nullptr || !audible)continue;
                const int index=(int)(relative%length);
                const int sourceChannels=slot.audio->getNumChannels();
                const float l=slot.audio->getSample(0,index)*left;
                const float r=slot.audio->getSample(sourceChannels>1?1:0,index)*right;
                if(channels>0 && outputs[0])outputs[0][i]+=l;
                if(channels>1 && outputs[1])outputs[1][i]+=r;
                if(captureActive.load(std::memory_order_relaxed) && i<captureSamples)
                {
                    captureMix.addSample(0,i,l);
                    captureMix.addSample(1,i,r);
                }
            }
        }
        sessionSamples+=samples;
        if(captureActive.load(std::memory_order_relaxed)
           && captureWriter!=nullptr && captureSamples>0)
        {
            captureMix.applyGain(0,captureSamples,owner.audioEngine.getMasterGain());
            captureWriter->write(captureMix.getArrayOfReadPointers(),captureSamples);
        }
        lock.exit();
    }

    MainComponent& owner;
    juce::AudioFormatManager formats;
    std::vector<TrackState> tracks;
    std::int64_t sessionSamples=0,lastBlockStart=0;
    std::atomic<bool> loopEnabled { true };
    std::atomic<double> sessionTempo { 120.0 };
    mutable juce::CriticalSection lock;
    std::atomic<bool> enabled { false };
    bool callbackAttached = false;
    juce::AudioBuffer<float> captureMix;
    std::unique_ptr<juce::TimeSliceThread> captureThread;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> captureWriter;
    juce::File captureFile;
    std::atomic<bool> captureActive { false };
};

class PerformView final : public juce::Component,
                          public juce::FileDragAndDropTarget,
                          private juce::Timer
{
public:
    explicit PerformView(MainComponent& ownerIn)
        : owner(ownerIn), player(ownerIn)
    {
        setOpaque(true);
        setInterceptsMouseClicks(true, true);
        // PERFORM is a normal page, not a floating overlay. The Browser
        // and its resize handle own the always-on-top layer.
        // Giving this full-screen page the same priority hides the handle
        // and makes its blue grip appear to flicker during page updates.
        clipButtons.resize((size_t)trackCount());
        stopTrackButtons.resize((size_t)trackCount());
        trackHeaders.resize((size_t)trackCount());
        activeTrackScene.assign((size_t)trackCount(), -1);
        performAudioFiles.resize((size_t)audioTrackCount());
        performMidiClips.resize((size_t)trackCount());

        for (int track = 0; track < trackCount(); ++track)
        {
            auto& stop = stopTrackButtons[(size_t)track];
            stop.setButtonText("STOP");
            stop.setMouseClickGrabsKeyboardFocus(false);
            stop.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252b32));
            stop.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            stop.onClick = [this, track]
            {
                player.stopTrack(track);
                if (track >= 0 && track < (int)activeTrackScene.size())
                    activeTrackScene[(size_t)track] = -1;
                refreshClipLabels();
                repaint();
            };
            addAndMakeVisible(stop);

            for (int scene = 0; scene < sceneCount; ++scene)
            {
                auto& cell = clipButtons[(size_t)track][(size_t)scene];
                cell.setMouseClickGrabsKeyboardFocus(false);
                cell.onClick = [this, track, scene] { launchClip(track, scene); };
                addAndMakeVisible(cell);
            }
        }

        for (int scene = 0; scene < sceneCount; ++scene)
        {
            auto& launch = sceneButtons[(size_t)scene];
            launch.setButtonText("SCENE " + juce::String(scene + 1));
            launch.setMouseClickGrabsKeyboardFocus(false);
            launch.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff27313a));
            launch.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            launch.onClick = [this, scene] { launchScene(scene); };
            addAndMakeVisible(launch);
        }

        stopAllButton.setButtonText("STOP ALL");
        stopAllButton.setMouseClickGrabsKeyboardFocus(false);
        stopAllButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6f3030));
        stopAllButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        stopAllButton.onClick = [this]
        {
            player.stopAll();
            std::fill(activeTrackScene.begin(), activeTrackScene.end(), -1);
            refreshClipLabels();
            repaint();
        };
        addAndMakeVisible(stopAllButton);

        recordArrangeButton.setButtonText("RECORD TO ARRANGE");
        recordArrangeButton.setMouseClickGrabsKeyboardFocus(false);
        recordArrangeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff31404d));
        recordArrangeButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        recordArrangeButton.onClick = [this] { toggleCaptureToArrange(); };
        addAndMakeVisible(recordArrangeButton);
        quantizeSelector.addItem(juce::String::fromUTF8("IMMÉDIAT"),1);
        quantizeSelector.addItem("1 TEMPS",2);
        quantizeSelector.addItem("1 MESURE",3);
        quantizeSelector.setSelectedId(3,juce::dontSendNotification);
        quantizeSelector.setTooltip(juce::String::fromUTF8("Quantification du lancement des clips et des scènes."));
        addAndMakeVisible(quantizeSelector);
        loopButton.setButtonText("BOUCLE : ON");
        loopButton.setClickingTogglesState(true);
        loopButton.setToggleState(true,juce::dontSendNotification);
        loopButton.onClick=[this]
        {
            const bool on=loopButton.getToggleState();
            loopButton.setButtonText(on?"BOUCLE : ON":"BOUCLE : OFF");
            player.setLoopEnabled(on);
            ++sessionRevision;
        };
        addAndMakeVisible(loopButton);
        std::fill(activeTrackScene.begin(), activeTrackScene.end(), -1);
        setVisible(false);
        owner.addAndMakeVisible(this);
        startTimerHz(20);
    }

    ~PerformView() override
    {
        stopTimer();
        if (player.isCapturing()) player.stopCapture();
        player.setEnabled(false);
    }

    void setPerformVisible(bool shouldShow)
    {
        performVisible = shouldShow;
        dragTrack = dragScene = -1;
        setVisible(shouldShow);
        player.setEnabled(shouldShow);

        if (shouldShow)
        {
            syncTrackContainers();
            // ARRANGE transport is deliberately stopped. PERFORM owns independent audio.
            owner.audioEngine.setPlaying(false);
            owner.audioEngine.setStepPreview(-1,false);
            owner.isPlaying = false;
            player.setTempoBpm(owner.getTempoBpm());
            setBounds(0, transportHeight, owner.getWidth(), juce::jmax(1, owner.getHeight() - transportHeight));
            resized();
            refreshClipLabels();
            toFront(false);
            repaint();
        }
        else
        {
            player.stopAll();
            std::fill(activeTrackScene.begin(), activeTrackScene.end(), -1);
            if (player.isCapturing()) finishCaptureToArrange();
        }
    }

    void renderAudioBlock(float* const* outputs, int numOutputs, int numSamples)
    {
        player.renderAudioBlock(outputs, numOutputs, numSamples);
    }

    bool isPerformVisible() const noexcept { return performVisible; }
    void appendMidiForInstrument(int lane,juce::MidiBuffer& midi,int samples)
    {if(performVisible)player.appendMidiForInstrument(lane,midi,samples);}
    std::uint64_t getSessionRevision() const noexcept{return sessionRevision;}

    void clearSession()
    {
        player.stopAll();
        player.resetSession();
        for(auto& bank:performAudioFiles)bank={};
        for(auto& bank:performMidiClips)bank={};
        quantizeSelector.setSelectedId(3,juce::dontSendNotification);
        loopButton.setToggleState(true,juce::dontSendNotification);
        loopButton.setButtonText("BOUCLE : ON");
        player.setLoopEnabled(true);
        ++sessionRevision;
        refreshClipLabels();
    }

    void saveSession(juce::XmlElement& project,const juce::File& projectFile) const
    {
        auto* session=project.createNewChildElement("PerformanceSession");
        session->setAttribute("quantization",quantizeSelector.getSelectedId());
        session->setAttribute("loop",loopButton.getToggleState());
        for(int track=0;track<trackCount();++track)
            for(int scene=0;scene<sceneCount;++scene)
            {
                if(!slotHasClip(track,scene))continue;
                auto* clip=session->createNewChildElement("Clip");
                clip->setAttribute("track",track);
                clip->setAttribute("scene",scene);
                if(track<audioTrackCount())
                {
                    clip->setAttribute("kind","audio");
                    clip->setAttribute("source",performAudioFiles[(size_t)track][(size_t)scene]
                        .getRelativePathFrom(projectFile.getParentDirectory()));
                }
                else
                {
                    clip->setAttribute("kind","midi");
                    const auto& data=performMidiClips[(size_t)track][(size_t)scene];
                    clip->setAttribute("lengthTicks",juce::String(data.lengthTicks));
                    for(const auto& n:data.notes)
                    {
                        auto* e=clip->createNewChildElement("Note");
                        e->setAttribute("start",juce::String(n.startTick));
                        e->setAttribute("length",juce::String(n.lengthTicks));
                        e->setAttribute("pitch",(int)n.pitch);
                        e->setAttribute("velocity",(int)n.velocity);
                        e->setAttribute("channel",(int)n.channel);
                        e->setAttribute("muted",n.muted);
                    }
                }
            }
    }

    void loadSession(const juce::XmlElement& project,const juce::File& file)
    {
        clearSession();
        const auto* session=project.getChildByName("PerformanceSession");
        if(!session)return;
        quantizeSelector.setSelectedId(juce::jlimit(1,3,
            session->getIntAttribute("quantization",3)),juce::dontSendNotification);
        const bool repeat=session->getBoolAttribute("loop",true);
        loopButton.setToggleState(repeat,juce::dontSendNotification);
        loopButton.setButtonText(repeat?"BOUCLE : ON":"BOUCLE : OFF");
        player.setLoopEnabled(repeat);
        for(const auto* clip:session->getChildIterator())
        {
            if(!clip->hasTagName("Clip"))continue;
            const int track=clip->getIntAttribute("track",-1);
            const int scene=clip->getIntAttribute("scene",-1);
            if(track<0 || track>=trackCount() || scene<0 || scene>=sceneCount)continue;
            if(clip->getStringAttribute("kind")=="audio" && track<audioTrackCount())
            {
                const auto source=clip->getStringAttribute("source");
                const auto path=juce::File::isAbsolutePath(source)?juce::File(source)
                    :file.getParentDirectory().getChildFile(source);
                juce::String error;
                if(path.existsAsFile() && player.prepareAudioClip(track,scene,path,error))
                    performAudioFiles[(size_t)track][(size_t)scene]=path;
            }
            else if(clip->getStringAttribute("kind")=="midi" && track>=audioTrackCount())
            {
                SessionMidiData data;
                data.valid=true;
                data.lengthTicks=juce::jmax<std::int64_t>(1,
                    clip->getStringAttribute("lengthTicks","3840").getLargeIntValue());
                for(const auto* e:clip->getChildIterator())
                {
                    if(!e->hasTagName("Note") || data.notes.size()>=8192)continue;
                    MidiEngine::NoteEvent note;
                    note.startTick=e->getStringAttribute("start").getLargeIntValue();
                    note.lengthTicks=e->getStringAttribute("length").getLargeIntValue();
                    if(note.startTick<0 || note.lengthTicks<=0 ||
                        note.lengthTicks>data.lengthTicks ||
                        note.startTick>data.lengthTicks-note.lengthTicks)continue;
                    note.pitch=(std::uint8_t)juce::jlimit(0,127,e->getIntAttribute("pitch",60));
                    note.velocity=(std::uint8_t)juce::jlimit(1,127,e->getIntAttribute("velocity",100));
                    note.channel=(std::uint8_t)juce::jlimit(1,16,e->getIntAttribute("channel",1));
                    note.muted=e->getBoolAttribute("muted",false);
                    data.notes.push_back(note);
                }
                assignMidiClip(track,scene,data);
            }
        }
        ++sessionRevision;
        refreshClipLabels();
    }

    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        if(!performVisible)return false;
        for(const auto& path:files)
            if(isSupportedAudioFile(juce::File(path)) || isSupportedMidiFile(juce::File(path)))return true;
        return false;
    }
    void fileDragEnter(const juce::StringArray& files,int x,int y) override
    {updateDropTarget({x,y},files);}
    void fileDragMove(const juce::StringArray& files,int x,int y) override
    {updateDropTarget({x,y},files);}
    void fileDragExit(const juce::StringArray&) override
    {dragTrack=dragScene=-1;refreshClipLabels();repaint();}
    void filesDropped(const juce::StringArray& files,int x,int y) override
    {
        updateDropTarget({x,y},files);
        if(dragTrack<0 || dragScene<0)return;
        const bool audioTarget=dragTrack<audioTrackCount();
        juce::File chosen;
        for(const auto& path:files)
        {
            const juce::File file(path);
            if(audioTarget?isSupportedAudioFile(file):isSupportedMidiFile(file))
            {chosen=file;break;}
        }
        if(!chosen.existsAsFile())return;
        if(audioTarget)
        {
            juce::String error;
            if(!player.prepareAudioClip(dragTrack,dragScene,chosen,error))
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Liberty - PERFORMANCE",error,"OK");
                dragTrack=dragScene=-1;return;
            }
            performAudioFiles[(size_t)dragTrack][(size_t)dragScene]=chosen;
        }
        else
        {
            SessionMidiData data;
            juce::String error;
            if(!parseMidiFile(chosen,data,error)
                || !assignMidiClip(dragTrack,dragScene,data))
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Liberty - PERFORMANCE",error.isNotEmpty()?error:
                    "Impossible de charger ce clip MIDI.","OK");
                dragTrack=dragScene=-1;return;
            }
        }
        ++sessionRevision;
        dragTrack=dragScene=-1;refreshClipLabels();repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0c1014));
        g.setColour(juce::Colour(0xff161c22));
        g.fillRect(0, 0, getWidth(), 64);
        g.setColour(juce::Colour(0xff303944));
        g.drawHorizontalLine(63, 0.0f, (float)getWidth());

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(21.0f, juce::Font::bold));
        g.drawText("PERFORM", 22, 10, 150, 28, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff8f98a3));
        g.setFont(juce::Font(10.0f));
        g.drawFittedText(juce::String::fromUTF8("SESSION   CLIPS   SCÈNES   LIVE"),170,16,
            juce::jmax(1,quantizeSelector.getX()-176),18,
            juce::Justification::centredLeft,1);

        const int paintedTracks = juce::jmin(trackCount(), (int)trackHeaders.size());
        for (int track = 0; track < paintedTracks; ++track)
        {
            const auto header = trackHeaders[(size_t)track];
            const auto colour = colourForTrack(track);
            g.setColour(juce::Colour(0xff151a20));
            g.fillRoundedRectangle(header.toFloat(), 5.0f);
            g.setColour(colour);
            g.fillRoundedRectangle((float)header.getX(), (float)header.getY(), (float)header.getWidth(), 5.0f, 2.5f);
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(11.5f, juce::Font::bold));
            g.drawText(trackName(track), header.reduced(6, 6), juce::Justification::centredTop, true);
            g.setColour(juce::Colour(0xff7e8791));
            g.setFont(juce::Font(8.0f, juce::Font::bold));
            g.drawText(trackType(track), header.getX() + 5, header.getY() + 28, header.getWidth() - 10, 14, juce::Justification::centred, true);
        }

        for (int scene = 0; scene < sceneCount; ++scene)
        {
            const auto y = sceneRows[(size_t)scene].getY();
            g.setColour(scene % 2 == 0 ? juce::Colour(0xff11161b) : juce::Colour(0xff0f1419));
            g.fillRect(0, y, getWidth(), sceneRows[(size_t)scene].getHeight());
            g.setColour(juce::Colour(0xff29313a));
            g.drawHorizontalLine(sceneRows[(size_t)scene].getBottom() - 1, 0.0f, (float)getWidth());
        }

        if (dragTrack >= 0 && dragTrack < (int)clipButtons.size() && dragScene >= 0 && dragScene < sceneCount)
        {
            const auto r = clipButtons[(size_t)dragTrack][(size_t)dragScene].getBounds().expanded(2);
            g.setColour(juce::Colour(0xff69d4ff));
            g.drawRoundedRectangle(r.toFloat(), 5.0f, 3.0f);
            g.setFont(juce::Font(9.0f, juce::Font::bold));
            g.drawText("DROP AUDIO HERE", r, juce::Justification::centred);
        }
    }

    void resized() override
    {
        syncTrackContainers();
        const int visibleTracks = juce::jmin(trackCount(), (int)trackHeaders.size());
        const int controlsX=juce::jmin(500,juce::jmax(180,getWidth()-330));
        quantizeSelector.setBounds(controlsX,13,172,28);
        loopButton.setBounds(controlsX+180,13,135,28);
        const int leftMargin = 18;
        const int sceneLaunchW = 92;
        const int gridLeft = leftMargin;
        const int gridRight = getWidth() - sceneLaunchW - 28;
        const int gap = 6;
        const int available = juce::jmax(visibleTracks * 110, gridRight - gridLeft);
        const int columnW = visibleTracks > 0 ? juce::jlimit(110, 220, (available - (visibleTracks - 1) * gap) / visibleTracks) : 110;
        const int headerTop = 72;
        const int headerH = 62;
        const int rowsTop = 142;
        const int footerH = 54;
        const int rowsAvailable = juce::jmax(320, getHeight() - rowsTop - footerH - 16);
        const int rowH = juce::jlimit(44, 86, rowsAvailable / sceneCount);

        for (int track = 0; track < visibleTracks; ++track)
        {
            const int x = gridLeft + track * (columnW + gap);
            trackHeaders[(size_t)track] = { x, headerTop, columnW, headerH };
            stopTrackButtons[(size_t)track].setBounds(x + 8, headerTop + 40, columnW - 16, 18);
            for (int scene = 0; scene < sceneCount; ++scene)
            {
                const int y = rowsTop + scene * rowH;
                clipButtons[(size_t)track][(size_t)scene].setBounds(x + 2, y + 4, columnW - 4, rowH - 8);
            }
        }

        for (int scene = 0; scene < sceneCount; ++scene)
        {
            const int y = rowsTop + scene * rowH;
            sceneRows[(size_t)scene] = { 0, y, getWidth(), rowH };
            sceneButtons[(size_t)scene].setBounds(getWidth() - sceneLaunchW - 18, y + 4, sceneLaunchW, rowH - 8);
        }
        recordArrangeButton.setBounds(18, getHeight() - 44, 170, 30);
        stopAllButton.setBounds(getWidth() - 140, getHeight() - 44, 122, 30);
    }

private:
    void syncTrackContainers()
    {
        const auto wantedTracks = (size_t)juce::jmax(0, trackCount());
        const auto wantedAudio = (size_t)juce::jmax(0, audioTrackCount());
        const auto oldTracks = clipButtons.size();
        if (clipButtons.size() != wantedTracks) clipButtons.resize(wantedTracks);
        if (stopTrackButtons.size() != wantedTracks) stopTrackButtons.resize(wantedTracks);
        if (trackHeaders.size() != wantedTracks) trackHeaders.resize(wantedTracks);
        if (activeTrackScene.size() != wantedTracks) activeTrackScene.resize(wantedTracks, -1);
        if (performAudioFiles.size() != wantedAudio) performAudioFiles.resize(wantedAudio);
        if (performMidiClips.size() != wantedTracks) performMidiClips.resize(wantedTracks);
        player.syncTrackCount();
        for (size_t t = oldTracks; t < wantedTracks; ++t)
        {
            auto& stop = stopTrackButtons[t];
            stop.setButtonText("STOP");
            stop.setMouseClickGrabsKeyboardFocus(false);
            stop.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252b32));
            stop.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            stop.onClick = [this, t]
            {
                const int track = (int)t;
                player.stopTrack(track);
                if (track < (int)activeTrackScene.size()) activeTrackScene[t] = -1;
                refreshClipLabels();
                repaint();
            };
            addAndMakeVisible(stop);
            for (int scene = 0; scene < sceneCount; ++scene)
            {
                auto& cell = clipButtons[t][(size_t)scene];
                cell.setMouseClickGrabsKeyboardFocus(false);
                cell.onClick = [this, t, scene] { launchClip((int)t, scene); };
                addAndMakeVisible(cell);
            }
        }
        if (dragTrack < -1 || dragTrack >= (int)wantedTracks || dragScene < -1 || dragScene >= sceneCount) dragTrack = dragScene = -1;
    }

    int audioTrackCount() const noexcept { return owner.getAudioTrackCount(); }
    int trackCount() const noexcept { return owner.getTotalArrangeTrackCount(); }
    int firstMidiTrack() const noexcept { return audioTrackCount(); }
    int firstInstrumentTrack() const noexcept { return audioTrackCount() + owner.getMidiTrackCount(); }

    juce::String trackName(int track) const
    {
        const auto name = getLibertyTrackName(track);
        if (name.isNotEmpty()) return name;
        if (track < firstMidiTrack()) return "Audio " + juce::String(track + 1);
        if (track < firstInstrumentTrack()) return "MIDI " + juce::String(track - firstMidiTrack() + 1);
        return "Instrument " + juce::String(track - firstInstrumentTrack() + 1);
    }

    juce::String trackType(int track) const
    {
        if (track < firstMidiTrack()) return "AUDIO";
        if (track < firstInstrumentTrack()) return "MIDI";
        return "INSTRUMENT";
    }

    juce::Colour colourForTrack(int track) const
    {
        return trackColour(getLibertyTrackColourId(track));
    }

    struct SessionMidiData {
        bool valid=false;
        std::int64_t lengthTicks=3840;
        std::vector<MidiEngine::NoteEvent> notes;
        juce::File source;
    };
    static bool isSupportedMidiFile(const juce::File& file) {
        const auto ext=file.getFileExtension().toLowerCase();
        return ext==".mid" || ext==".midi";
    }
    static bool parseMidiFile(const juce::File& file,SessionMidiData& data,juce::String& error) {
        auto input=file.createInputStream();
        juce::MidiFile midi;
        if(input==nullptr || !midi.readFrom(*input) || midi.getTimeFormat()<=0) {
            error="Le fichier MIDI doit utiliser une résolution PPQ valide.";return false;
        }
        const double ppq=(double)midi.getTimeFormat();
        data={};
        std::int64_t longest=0;
        for(int track=0;track<midi.getNumTracks();++track) {
            auto* sequence=midi.getTrack(track);
            if(sequence==nullptr)continue;
            sequence->updateMatchedPairs();
            for(int i=0;i<sequence->getNumEvents();++i) {
                const auto* event=sequence->getEventPointer(i);
                if(event==nullptr || !event->message.isNoteOn()
                    || event->noteOffObject==nullptr || data.notes.size()>=8192)continue;
                const double first=event->message.getTimeStamp()*960.0/ppq;
                const double last=event->noteOffObject->message.getTimeStamp()*960.0/ppq;
                if(!std::isfinite(first) || !std::isfinite(last) || first<0.0 || last<=first)continue;
                MidiEngine::NoteEvent note;
                note.startTick=(std::int64_t)std::llround(first);
                note.lengthTicks=juce::jmax<std::int64_t>(1,(std::int64_t)std::llround(last)-note.startTick);
                note.pitch=(std::uint8_t)event->message.getNoteNumber();
                note.velocity=(std::uint8_t)juce::jlimit(1,127,(int)std::lround(event->message.getVelocity()*127.f));
                note.channel=(std::uint8_t)event->message.getChannel();
                longest=juce::jmax(longest,note.startTick+note.lengthTicks);
                data.notes.push_back(note);
            }
        }
        data.lengthTicks=juce::jmax<std::int64_t>(3840,((longest+3839)/3840)*3840);
        data.source=file;data.valid=true;return true;
    }
    bool assignMidiClip(int track,int scene,const SessionMidiData& data) {
        if(track<audioTrackCount() || track>=(int)performMidiClips.size()
            || scene<0 || scene>=sceneCount || !data.valid)return false;
        if(!player.prepareMidiClip(track,scene,data.notes,data.lengthTicks))return false;
        performMidiClips[(size_t)track][(size_t)scene]=data;
        return true;
    }
    bool createMidiFromTrack(int track,int scene) {
        SessionMidiData data;data.valid=true;
        if(track<firstInstrumentTrack()) {
            if(const auto* engine=owner.getMidiEngine(track-firstMidiTrack()))
                data.notes=engine->getNotesCopy();
        } else if(auto* pattern=owner.getInstrumentStepSequencer(track-firstInstrumentTrack())) {
            data.notes=LibertyStepSequencer::render(*pattern);
            data.lengthTicks=juce::jmax<std::int64_t>(3840,LibertyStepSequencer::getCycleLengthTicks(*pattern));
        }
        std::int64_t last=0;
        for(const auto& note:data.notes)last=juce::jmax(last,note.startTick+note.lengthTicks);
        data.lengthTicks=juce::jmax(data.lengthTicks,((last+3839)/3840)*3840);
        if(!assignMidiClip(track,scene,data))return false;
        ++sessionRevision;return true;
    }
    int launchQuantization() const noexcept {
        return juce::jlimit(0,2,quantizeSelector.getSelectedId()-1);
    }

    bool hasDroppedAudio(int track, int scene) const
    {
        return track >= 0 && track < audioTrackCount() && track < (int)performAudioFiles.size()
            && scene >= 0 && scene < sceneCount
            && performAudioFiles[(size_t)track][(size_t)scene].existsAsFile();
    }

    bool slotHasClip(int track,int scene) const {
        if(scene<0 || scene>=sceneCount)return false;
        if(track<audioTrackCount())return hasDroppedAudio(track,scene);
        return track>=0 && track<(int)performMidiClips.size()
            && performMidiClips[(size_t)track][(size_t)scene].valid;
    }
    juce::String slotName(int track,int scene) const {
        if(!slotHasClip(track,scene))return track<audioTrackCount()?"VIDE":"+ MIDI";
        if(track>=audioTrackCount()) {
            const auto& clip=performMidiClips[(size_t)track][(size_t)scene];
            return clip.source.existsAsFile()?clip.source.getFileNameWithoutExtension():"CLIP MIDI";
        }
        auto name=performAudioFiles[(size_t)track][(size_t)scene].getFileName();
        return name.length()>22?name.substring(0,19)+"...":name;
    }

    void refreshClipLabels()
    {
        syncTrackContainers();
        const int count=juce::jmin(trackCount(),(int)clipButtons.size());
        for(int track=0;track<count;++track)
        {
            const auto colour=colourForTrack(track);
            for(int scene=0;scene<sceneCount;++scene)
            {
                auto& button=clipButtons[(size_t)track][(size_t)scene];
                const bool valid=slotHasClip(track,scene);
                const bool active=player.isTrackPlaying(track,scene);
                const bool queued=player.isTrackQueued(track,scene);
                const bool target=dragTrack==track && dragScene==scene;
                button.setButtonText(target?(track<audioTrackCount()?"DÉPOSER AUDIO":"DÉPOSER MIDI"):
                    (queued?"EN ATTENTE":slotName(track,scene)));
                button.setEnabled(true);
                button.setColour(juce::TextButton::buttonColourId,
                    target?juce::Colour(0xff245b70):
                    active?colour.brighter(0.25f):
                    valid?colour.withAlpha(0.70f):juce::Colour(0xff181d22));
                button.setColour(juce::TextButton::textColourOffId,
                    valid||target?juce::Colours::white:juce::Colour(0xff9ba6af));
                if(track>=firstMidiTrack())
                {
                    const int lane=LibertyPerformTiming::instrumentForLogicalTrack(
                        track,audioTrackCount(),owner.getMidiTrackCount(),owner.getInstrumentTrackCount());
                    button.setTooltip(lane>=0?
                        "MIDI vers instrument "+juce::String(lane+1)+
                        ". Cliquez sur une cellule vide pour créer un clip.":
                        "Chargez une piste instrument pour entendre ce clip MIDI.");
                }
            }
        }
    }

    void updateDropTarget(juce::Point<int> point,const juce::StringArray& files)
    {
        int newTrack=-1,newScene=-1;
        for(int track=0;track<juce::jmin(trackCount(),(int)clipButtons.size());++track)
        {
            bool supportsFile=false;
            for(const auto& path:files)
                if(track<audioTrackCount()?isSupportedAudioFile(juce::File(path)):
                    isSupportedMidiFile(juce::File(path))){supportsFile=true;break;}
            if(!supportsFile)continue;
            for(int scene=0;scene<sceneCount;++scene)
                if(clipButtons[(size_t)track][(size_t)scene].getBounds().contains(point))
                {newTrack=track;newScene=scene;break;}
            if(newTrack>=0)break;
        }
        if(newTrack!=dragTrack || newScene!=dragScene)
        {dragTrack=newTrack;dragScene=newScene;refreshClipLabels();repaint();}
    }

    void launchClip(int track,int scene)
    {
        if(track<0 || track>=trackCount() || scene<0 || scene>=sceneCount)return;
        if(!slotHasClip(track,scene) && (track<audioTrackCount() ||
            !createMidiFromTrack(track,scene)))return;
        player.queueScene(track,scene,launchQuantization(),
            owner.getTempoBpm(),owner.getTimeSignatureNumerator(),
            owner.getTimeSignatureDenominator());
        refreshClipLabels();
    }

    void launchScene(int scene)
    {
        if(scene<0 || scene>=sceneCount)return;
        for(int track=0;track<trackCount();++track)
            player.queueScene(track,slotHasClip(track,scene)?scene:-1,launchQuantization(),
                owner.getTempoBpm(),owner.getTimeSignatureNumerator(),
                owner.getTimeSignatureDenominator());
        refreshClipLabels();
    }

    void toggleCaptureToArrange()
    {
        if (!player.isCapturing())
        {
            juce::String error;
            if (!player.startCapture(error))
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                       "Liberty - PERFORM", error, "OK");
                return;
            }
            recordArrangeButton.setButtonText("STOP RECORDING");
            recordArrangeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff9b4545));
        }
        else
        {
            finishCaptureToArrange();
        }
    }

    void finishCaptureToArrange()
    {
        const auto capture = player.stopCapture();
        recordArrangeButton.setButtonText("RECORD TO ARRANGE");
        recordArrangeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff31404d));
        if (!capture.existsAsFile()) return;

        int targetTrack = -1;
        for (int i = 0; i < owner.getAudioTrackCount(); ++i)
            if (!owner.audioEngine.hasAudioFile(i)) { targetTrack = i; break; }

        if (targetTrack < 0)
        {
            targetTrack = owner.addAudioTrack();
            player.syncTrackCount();
            syncTrackContainers();
        }

        juce::String error;
        if (!owner.audioEngine.loadAudioFileIntoTrack(targetTrack, capture, error))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Liberty - PERFORM", error, "OK");
            return;
        }
        owner.audioEngine.setTrackStartSeconds(targetTrack, 0.0);
        owner.trackSourceFiles[(size_t)targetTrack] = capture;
        owner.rebuildWaveformCache(targetTrack);
        owner.selectedTrack = targetTrack;
        owner.repaint();
    }

    void timerCallback() override
    {
        if (!performVisible) return;
        const auto wanted = juce::Rectangle<int>(0, transportHeight, owner.getWidth(), juce::jmax(1, owner.getHeight() - transportHeight));
        if (getBounds() != wanted) setBounds(wanted);
        player.setTempoBpm(owner.getTempoBpm());
        // Keep z-order stable while PERFORM is running. Repeated toFront()
        // competes with the Browser's always-on-top layer and causes flicker.
        refreshClipLabels();
        repaint();
    }

    MainComponent& owner;
    PerformAudioPlayer player;
    std::deque<std::array<juce::TextButton, sceneCount>> clipButtons;
    std::array<juce::TextButton, sceneCount> sceneButtons;
    std::deque<juce::TextButton> stopTrackButtons;
    juce::TextButton stopAllButton, recordArrangeButton;
    std::vector<juce::Rectangle<int>> trackHeaders;
    std::array<juce::Rectangle<int>, sceneCount> sceneRows;
    std::vector<int> activeTrackScene;
    std::vector<std::array<juce::File, sceneCount>> performAudioFiles;
    std::vector<std::array<SessionMidiData, sceneCount>> performMidiClips;
    juce::ComboBox quantizeSelector;
    juce::TextButton loopButton;
    std::uint64_t sessionRevision=0;
    int dragTrack = -1;
    int dragScene = -1;
    bool performVisible = false;
};

class PerformController final : private juce::Timer
{
public:
    explicit PerformController(MainComponent& ownerIn) : owner(ownerIn), view(ownerIn)
    {
        performButton.setButtonText("PERFORM");
        performButton.setMouseClickGrabsKeyboardFocus(false);
        performButton.setClickingTogglesState(false);
        performButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        performButton.onClick = [this]
        {
            if (!performVisible) setLibertyMixConsoleVisible(&owner, false);
            setPerformVisible(true);
        };
        owner.addAndMakeVisible(performButton);

        arrangeOverlay.setButtonText("ARRANGE");
        mixOverlay.setButtonText("MIXCONSOLE");
        for (auto* b : { &arrangeOverlay, &mixOverlay })
        {
            b->setMouseClickGrabsKeyboardFocus(false);
            b->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252a31));
            b->setVisible(false);
            owner.addAndMakeVisible(*b);
        }
        arrangeOverlay.onClick = [this]
        {
            setPerformVisible(false);
            setLibertyMixConsoleVisible(&owner, false);
        };
        mixOverlay.onClick = [this]
        {
            setPerformVisible(false);
            setLibertyMixConsoleVisible(&owner, true);
        };

        startTimerHz(30);
    }

    ~PerformController() override { shutdown(); }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
        view.setPerformVisible(false);
        performButton.setVisible(false);
        arrangeOverlay.setVisible(false);
        mixOverlay.setVisible(false);
    }

    void setPerformVisible(bool shouldShow)
    {
        if (stopped.load()) return;
        performVisible = shouldShow;
        view.setPerformVisible(shouldShow);
        arrangeOverlay.setVisible(shouldShow);
        mixOverlay.setVisible(shouldShow);
        performButton.setColour(juce::TextButton::buttonColourId,
                                shouldShow ? juce::Colour(0xff315f7a) : juce::Colour(0xff252a31));
        // The Browser is an always-on-top sibling. Raising PERFORM again
        // here can reorder its header and cause a visible flash.
        // PerformView already raises itself once when becoming visible.
        arrangeOverlay.toFront(false);
        mixOverlay.toFront(false);
        performButton.toFront(false);
        owner.repaint();
    }

    void renderAudioBlock(float* const* outputs, int numOutputs, int numSamples)
    {
        if (performVisible) view.renderAudioBlock(outputs, numOutputs, numSamples);
    }
    void appendMidi(int track,juce::MidiBuffer& midi,int samples)
    {if(performVisible)view.appendMidiForInstrument(track,midi,samples);}
    void clearSession(){view.clearSession();}
    void saveSession(juce::XmlElement& xml,const juce::File& file) const
    {view.saveSession(xml,file);}
    void loadSession(const juce::XmlElement& xml,const juce::File& file)
    {view.loadSession(xml,file);}
    std::uint64_t getSessionRevision() const noexcept {return view.getSessionRevision();}

    bool isVisible() const noexcept { return performVisible; }

private:
    void timerCallback() override
    {
        if (stopped.load()) return;
        arrangeOverlay.setBounds(1055, 8, 88, 26);
        mixOverlay.setBounds(1147, 8, 112, 26);
        performButton.setBounds(1263, 8, 94, 26);
        // Page controls are raised on mode activation, not on every timer
        // tick. Constantly changing sibling z-order makes the Browser header
        // and its resize handle flash while PERFORM is visible.
    }

    MainComponent& owner;
    PerformView view;
    juce::TextButton arrangeOverlay, mixOverlay, performButton;
    std::atomic<bool> stopped { false };
    bool performVisible = false;
};

std::map<MainComponent*, std::unique_ptr<PerformController>> controllers;

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
                    {
                        controllers.emplace(main, std::make_unique<PerformController>(*main));
                        stopTimer();
                        return;
                    }
    }
};

Bootstrap bootstrap;
}

void renderLibertyPerformAudio(AudioEngine* engine, float* const* outputs, int numOutputs, int numSamples)
{
    if (engine == nullptr) return;
    for (auto& entry : controllers)
        if (entry.first != nullptr && &entry.first->audioEngine == engine && entry.second)
        {
            entry.second->renderAudioBlock(outputs, numOutputs, numSamples);
            return;
        }
}

void appendLibertyPerformMidi(AudioEngine* engine,int instrument,juce::MidiBuffer& midi,int samples)
{
    if(engine==nullptr)return;
    for(auto& entry:controllers)
        if(entry.first!=nullptr && &entry.first->audioEngine==engine && entry.second)
        {entry.second->appendMidi(instrument,midi,samples);return;}
}

void ensureLibertyPerformController(MainComponent* owner);
void saveLibertyPerformSession(MainComponent* owner,juce::XmlElement& xml,const juce::File& file)
{
    if(owner==nullptr)return;
    ensureLibertyPerformController(owner);
    if(auto it=controllers.find(owner);it!=controllers.end() && it->second)
        it->second->saveSession(xml,file);
}
void loadLibertyPerformSession(MainComponent* owner,const juce::XmlElement& xml,const juce::File& file)
{
    if(owner==nullptr)return;
    ensureLibertyPerformController(owner);
    if(auto it=controllers.find(owner);it!=controllers.end() && it->second)
        it->second->loadSession(xml,file);
}
void clearLibertyPerformSession(MainComponent* owner)
{
    if(auto it=controllers.find(owner);it!=controllers.end() && it->second)
        it->second->clearSession();
}
std::uint64_t getLibertyPerformSessionRevision(const MainComponent* owner)
{
    if(auto it=controllers.find(const_cast<MainComponent*>(owner));it!=controllers.end() && it->second)
        return it->second->getSessionRevision();
    return 0;
}

void ensureLibertyPerformController(MainComponent* owner)
{
    if (owner != nullptr && !controllers.count(owner))
        controllers.emplace(owner, std::make_unique<PerformController>(*owner));
}

bool isLibertyPerformVisible(MainComponent* owner)
{
    if (owner == nullptr) return false;
    const auto it = controllers.find(owner);
    return it != controllers.end() && it->second && it->second->isVisible();
}

void setLibertyPerformVisible(MainComponent* owner, bool shouldShow)
{
    if (owner == nullptr) return;
    ensureLibertyPerformController(owner);
    const auto it = controllers.find(owner);
    if (it == controllers.end() || !it->second) return;
    it->second->setPerformVisible(shouldShow);
}

void shutdownLibertyPerformController()
{
    bootstrap.shutdown();
}
