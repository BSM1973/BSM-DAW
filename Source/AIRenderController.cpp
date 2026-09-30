#define private public
#include "MainComponent.h"
#undef private
#include "PluginHost.h"
#include "OneKnobEffects.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <memory>

int getLibertyActiveInstrumentClipLane(MainComponent& owner);

namespace
{
juce::File makeRenderFile()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("BSM").getChildFile("Liberty").getChildFile("AI Renders");
    dir.createDirectory();
    return dir.getNonexistentChildFile("AI Render " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S"), ".wav", false);
}

void silenceInstrumentState(LibertyPluginHost& host, int instrumentTrack, int blockSize)
{
    juce::AudioBuffer<float> discard(2, blockSize);
    discard.clear();
    juce::MidiBuffer panic;
    for (int channel = 1; channel <= 16; ++channel)
    {
        panic.addEvent(juce::MidiMessage::allNotesOff(channel), 0);
        panic.addEvent(juce::MidiMessage::allSoundOff(channel), 0);
        panic.addEvent(juce::MidiMessage::controllerEvent(channel, 64, 0), 0); // sustain off
    }
    float* channels[] = { discard.getWritePointer(0), discard.getWritePointer(1) };
    host.processInstrumentForTrack(instrumentTrack, channels, 2, blockSize, panic);

    // Drain residual synth/reverb state into a buffer that is never sent to the outputs.
    for (int i = 0; i < 8; ++i)
    {
        discard.clear();
        juce::MidiBuffer empty;
        host.processInstrumentForTrack(instrumentTrack, channels, 2, blockSize, empty);
    }

    // Offline bounce also advances every external Instrument insert. Clear only
    // their DSP history; plugin parameters, slot assignments and One Knob amount
    // remain untouched for the next realtime playback.
    host.resetInstrumentEffectState(instrumentTrack);
    auto& oneKnob = LibertyOneKnobManager::instance();
    for (int slot = 0; slot < LibertyPluginHost::effectSlotsPerTrack; ++slot)
        oneKnob.resetEffectState(100000 + instrumentTrack * 8 + slot);
}
}

bool renderLibertyAIActiveInstrumentToAudio(MainComponent& owner, juce::String& resultMessage)
{
    auto& host = LibertyPluginHost::instance();
    const int instrumentTrack = getLibertyActiveInstrumentClipLane(owner);
    if (instrumentTrack < 0)
    {
        resultMessage = "Selectionne d'abord un clip Instrument actif a rendre.";
        return false;
    }
    if (!host.hasInstrumentForTrack(instrumentTrack))
    {
        resultMessage = "Charge d'abord un instrument AU/VST3 sur la piste Instrument.";
        return false;
    }

    const auto notes = owner.midiEngine.getNotesCopy();
    if (notes.empty())
    {
        resultMessage = "Aucun clip Instrument actif a rendre.";
        return false;
    }

    int targetTrack = -1;
    for (int i = 0; i < owner.getAudioTrackCount(); ++i)
        if (!owner.audioEngine.hasAudioFile(i)) { targetTrack = i; break; }
    if (targetTrack < 0)
    {
        resultMessage = "Aucune piste Audio vide disponible.";
        return false;
    }

    // Render must never share live monitoring/playback with the generated audio.
    owner.audioEngine.setPlaying(false);
    owner.isPlaying = false;

    const float instrumentGain = owner.audioEngine.getInstrumentTrackGain(instrumentTrack);
    const float instrumentPan = owner.audioEngine.getInstrumentTrackPan(instrumentTrack);
    const double sampleRate = owner.audioEngine.getSampleRate() > 0.0 ? owner.audioEngine.getSampleRate() : 48000.0;
    const int blockSize = juce::jmax(64, owner.audioEngine.getBufferSize() > 0 ? owner.audioEngine.getBufferSize() : 512);
    const double clipDuration = juce::jmax(0.25, owner.midiClipLengthSeconds);
    const double tailSeconds = 2.0;
    const int totalSamples = juce::jmax(1, (int)std::ceil((clipDuration + tailSeconds) * sampleRate));

    // Clear any hanging note/sustain left by realtime playback before rendering.
    silenceInstrumentState(host, instrumentTrack, blockSize);

    juce::AudioBuffer<float> rendered(2, totalSamples);
    rendered.clear();

    int writePos = 0;
    while (writePos < totalSamples)
    {
        const int num = juce::jmin(blockSize, totalSamples - writePos);
        juce::AudioBuffer<float> block(2, num);
        block.clear();
        juce::MidiBuffer midi;

        for (const auto& n : notes)
        {
            const auto startSeconds = MidiEngine::tickToSeconds(n.startTick, owner.tempoBpm);
            const auto endSeconds = MidiEngine::tickToSeconds(n.startTick + n.lengthTicks, owner.tempoBpm);
            const auto startSample = (int)std::llround(startSeconds * sampleRate);
            const auto endSample = (int)std::llround(endSeconds * sampleRate);
            const int midiChannel = juce::jlimit(1, 16, (int)n.channel);
            // The extra render window after clipDuration is tail-only: it may drain
            // synth releases and FX, but must never start notes hidden beyond the
            // right edge of a resized Instrument clip.
            const int clipEndSample = (int)std::llround(clipDuration * sampleRate);
            if (startSample < clipEndSample && startSample >= writePos && startSample < writePos + num)
                midi.addEvent(juce::MidiMessage::noteOn(midiChannel, (int)n.pitch, (juce::uint8)n.velocity), startSample - writePos);
            if (startSample < clipEndSample && endSample < clipEndSample
                && endSample >= writePos && endSample < writePos + num)
                midi.addEvent(juce::MidiMessage::noteOff(midiChannel, (int)n.pitch), endSample - writePos);
        }

        const int clipEndSample = (int)std::llround(clipDuration * sampleRate);
        if (clipEndSample >= writePos && clipEndSample < writePos + num)
            for (int channel = 1; channel <= 16; ++channel)
            {
                midi.addEvent(juce::MidiMessage::allNotesOff(channel), clipEndSample - writePos);
                midi.addEvent(juce::MidiMessage::controllerEvent(channel, 64, 0), clipEndSample - writePos);
            }

        float* channels[] = { block.getWritePointer(0), block.getWritePointer(1) };
        if (!host.processInstrumentForTrack(instrumentTrack, channels, 2, num, midi, instrumentGain, instrumentPan))
        {
            silenceInstrumentState(host, instrumentTrack, blockSize);
            resultMessage = "L'instrument charge n'a pas pu etre rendu.";
            return false;
        }

        // Match realtime Instrument processing exactly: FX plugin then One Knob,
        // slot by slot from FX1 through FX8 on the active Instrument lane.
        auto& oneKnob = LibertyOneKnobManager::instance();
        for (int slot = 0; slot < LibertyPluginHost::effectSlotsPerTrack; ++slot)
        {
            host.processInstrumentEffectSlot(instrumentTrack, slot, block);
            oneKnob.process(100000 + instrumentTrack * 8 + slot, block);
        }

        // Reject invalid plugin output before it can reach a file or the speakers.
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = block.getWritePointer(ch);
            for (int s = 0; s < num; ++s)
                if (!std::isfinite(data[s])) data[s] = 0.0f;
        }

        rendered.copyFrom(0, writePos, block, 0, 0, num);
        rendered.copyFrom(1, writePos, block, 1, 0, num);
        writePos += num;
    }

    // Safety ceiling: a synth/plugin that runs away during offline rendering must
    // never create a full-scale feedback-like WAV. Preserve dynamics, attenuate only.
    float peak = 0.0f;
    for (int ch = 0; ch < rendered.getNumChannels(); ++ch)
        peak = juce::jmax(peak, rendered.getMagnitude(ch, 0, rendered.getNumSamples()));
    constexpr float safePeak = 0.8912509f; // -1 dBFS
    if (peak > safePeak && std::isfinite(peak))
        rendered.applyGain(safePeak / peak);

    auto file = makeRenderFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
    {
        silenceInstrumentState(host, instrumentTrack, blockSize);
        file.deleteFile();
        resultMessage = "Impossible de creer le fichier AI Render.";
        return false;
    }

    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), sampleRate, 2, 24, {}, 0));
    if (writer == nullptr)
    {
        silenceInstrumentState(host, instrumentTrack, blockSize);
        file.deleteFile();
        resultMessage = "Impossible de creer le writer WAV.";
        return false;
    }
    stream.release();
    if (!writer->writeFromAudioSampleBuffer(rendered, 0, rendered.getNumSamples()))
    {
        writer.reset();
        silenceInstrumentState(host, instrumentTrack, blockSize);
        file.deleteFile();
        resultMessage = "Echec de l'ecriture du rendu audio.";
        return false;
    }
    writer.reset();

    // Critical anti-feedback step: terminate every synth voice after offline render.
    silenceInstrumentState(host, instrumentTrack, blockSize);

    juce::String error;
    if (!owner.audioEngine.loadAudioFileIntoTrack(targetTrack, file, error))
    {
        file.deleteFile();
        resultMessage = error.isNotEmpty() ? error : "Le rendu WAV n'a pas pu etre charge dans ARRANGE.";
        return false;
    }

    owner.audioEngine.setTrackStartSeconds(targetTrack, juce::jmax(0.0, owner.midiClipStartSeconds));
    // A reusable empty Audio lane may still carry old mix settings. The rendered
    // WAV already contains the Instrument gain/pan, so replay it at unity/centre
    // and unmuted. Preserve Solo because it is part of the user's mix selection.
    owner.audioEngine.setTrackGain(targetTrack, 1.0f);
    owner.audioEngine.setTrackPan(targetTrack, 0.0f);
    owner.audioEngine.setTrackMuted(targetTrack, false);
    owner.trackSourceFiles[(size_t)targetTrack] = file;
    owner.rebuildWaveformCache(targetTrack);

    // The source Instrument is automatically muted after bounce so pressing Play
    // cannot double the live synth with its rendered copy. The MIDI clip is retained.
    owner.audioEngine.setInstrumentTrackMuted(instrumentTrack, true);
    owner.selectedTrack = targetTrack;
    owner.repaint();

    resultMessage = "AI Render securise sur Audio " + juce::String(targetTrack + 1)
                  + " - source Instrument mutee - " + file.getFileName();
    return true;
}
