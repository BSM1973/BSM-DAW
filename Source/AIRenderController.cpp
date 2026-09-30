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

void silenceInstrumentState(LibertyPluginHost& host, int instrumentTrack, int blockSize, bool offline = false)
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

    const double sampleRate = owner.audioEngine.getSampleRate();
    if (sampleRate <= 0.0)
    {
        resultMessage = "Aucun peripherique audio disponible pour le rendu AI.";
        return false;
    }

    // Render must never share live monitoring/playback with the generated audio.
    owner.audioEngine.setPlaying(false);
    owner.isPlaying = false;

    if (!host.beginOfflineInstrumentRender(instrumentTrack))
    {
        resultMessage = "L'instrument est deja utilise par un rendu offline.";
        return false;
    }
    struct OfflineRenderGuard
    {
        LibertyPluginHost& host;
        int track;
        ~OfflineRenderGuard() { host.endOfflineInstrumentRender(track); }
    } offlineGuard { host, instrumentTrack };

    const float instrumentGain = owner.audioEngine.getInstrumentTrackGain(instrumentTrack);
    const float instrumentPan = owner.audioEngine.getInstrumentTrackPan(instrumentTrack);
    const int blockSize = juce::jmax(64, owner.audioEngine.getBufferSize() > 0 ? owner.audioEngine.getBufferSize() : 512);
    const double clipDuration = juce::jmax(0.25, owner.midiClipLengthSeconds);
    const double tailSeconds = 2.0;
    const double totalSamplesExact = (clipDuration + tailSeconds) * sampleRate;
    if (!std::isfinite(clipDuration) || !std::isfinite(totalSamplesExact)
        || totalSamplesExact <= 0.0
        || totalSamplesExact > (double) std::numeric_limits<std::int64_t>::max())
    {
        resultMessage = "Le clip Instrument est trop long pour etre rendu.";
        return false;
    }
    const std::int64_t totalSamples = juce::jmax<std::int64_t>(1, (std::int64_t)std::ceil(totalSamplesExact));
    const double clipEndSamplesExact = clipDuration * sampleRate;
    if (!std::isfinite(clipEndSamplesExact)
        || clipEndSamplesExact < 0.0
        || clipEndSamplesExact > (double) std::numeric_limits<std::int64_t>::max())
    {
        resultMessage = "La duree du clip Instrument est invalide.";
        return false;
    }
    const std::int64_t clipEndSample = (std::int64_t)std::llround(clipEndSamplesExact);

    // Clear any hanging note/sustain left by realtime playback before rendering.
    silenceInstrumentState(host, instrumentTrack, blockSize, true);

    auto tempFile = makeRenderFile().getSiblingFile("AI Render Temp " + juce::Uuid().toString() + ".wav");
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> tempStream(tempFile.createOutputStream());
    if (tempStream == nullptr)
    {
        silenceInstrumentState(host, instrumentTrack, blockSize, true);
        tempFile.deleteFile();
        resultMessage = "Impossible de creer le fichier temporaire AI Render.";
        return false;
    }
    std::unique_ptr<juce::AudioFormatWriter> tempWriter(wav.createWriterFor(tempStream.get(), sampleRate, 2, 32, {}, 0));
    if (tempWriter == nullptr)
    {
        silenceInstrumentState(host, instrumentTrack, blockSize, true);
        tempFile.deleteFile();
        resultMessage = "Impossible de creer le writer temporaire AI Render.";
        return false;
    }
    tempStream.release();

    float peak = 0.0f;
    std::int64_t writePos = 0;
    while (writePos < totalSamples)
    {
        const int num = (int)juce::jmin<std::int64_t>(blockSize, totalSamples - writePos);
        juce::AudioBuffer<float> block(2, num);
        block.clear();
        juce::MidiBuffer midi;

        for (const auto& n : notes)
        {
            if (n.startTick < 0 || n.lengthTicks <= 0
                || n.startTick > std::numeric_limits<std::int64_t>::max() - n.lengthTicks)
                continue;
            const auto noteEndTick = n.startTick + n.lengthTicks;
            const auto startSeconds = MidiEngine::tickToSeconds(n.startTick, owner.tempoBpm);
            const auto endSeconds = MidiEngine::tickToSeconds(noteEndTick, owner.tempoBpm);
            const double startSampleExact = startSeconds * sampleRate;
            const double endSampleExact = endSeconds * sampleRate;
            if (!std::isfinite(startSampleExact) || !std::isfinite(endSampleExact)
                || startSampleExact < 0.0 || endSampleExact < 0.0
                || startSampleExact > (double) std::numeric_limits<std::int64_t>::max()
                || endSampleExact > (double) std::numeric_limits<std::int64_t>::max())
                continue;
            const auto startSample = (std::int64_t)std::llround(startSampleExact);
            const auto endSample = (std::int64_t)std::llround(endSampleExact);
            const int midiChannel = juce::jlimit(1, 16, (int)n.channel);
            // The extra render window after clipDuration is tail-only: it may drain
            // synth releases and FX, but must never start notes hidden beyond the
            // right edge of a resized Instrument clip.
            if (startSample < clipEndSample && startSample >= writePos && startSample < writePos + num)
                midi.addEvent(juce::MidiMessage::noteOn(midiChannel, (int)n.pitch, (juce::uint8)n.velocity), (int)(startSample - writePos));
            if (startSample < clipEndSample && endSample < clipEndSample
                && endSample >= writePos && endSample < writePos + num)
                midi.addEvent(juce::MidiMessage::noteOff(midiChannel, (int)n.pitch), (int)(endSample - writePos));
        }

        if (clipEndSample >= writePos && clipEndSample < writePos + num)
            for (int channel = 1; channel <= 16; ++channel)
            {
                midi.addEvent(juce::MidiMessage::allNotesOff(channel), (int)(clipEndSample - writePos));
                midi.addEvent(juce::MidiMessage::controllerEvent(channel, 64, 0), (int)(clipEndSample - writePos));
            }

        float* channels[] = { block.getWritePointer(0), block.getWritePointer(1) };
        if (!host.processOfflineInstrument(instrumentTrack, channels, 2, num, midi, instrumentGain, instrumentPan))
        {
            tempWriter.reset();
            silenceInstrumentState(host, instrumentTrack, blockSize, true);
            tempFile.deleteFile();
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

        for (int ch = 0; ch < block.getNumChannels(); ++ch)
            peak = juce::jmax(peak, block.getMagnitude(ch, 0, num));
        if (!tempWriter->writeFromAudioSampleBuffer(block, 0, num))
        {
            tempWriter.reset();
            silenceInstrumentState(host, instrumentTrack, blockSize, true);
            tempFile.deleteFile();
            resultMessage = "Echec de l'ecriture du rendu audio temporaire.";
            return false;
        }
        writePos += num;
    }
    tempWriter.reset();

    // Safety ceiling: preserve dynamics and attenuate only if the complete render
    // exceeded -1 dBFS. The first pass stayed on disk, so long bounces do not need
    // a full stereo buffer in RAM.
    constexpr float safePeak = 0.8912509f; // -1 dBFS
    const float renderGain = peak > safePeak && std::isfinite(peak) ? safePeak / peak : 1.0f;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> tempReader(formats.createReaderFor(tempFile));
    if (tempReader == nullptr)
    {
        silenceInstrumentState(host, instrumentTrack, blockSize, true);
        tempFile.deleteFile();
        resultMessage = "Impossible de relire le rendu audio temporaire.";
        return false;
    }

    auto file = makeRenderFile();
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
    {
        tempReader.reset();
        silenceInstrumentState(host, instrumentTrack, blockSize, true);
        file.deleteFile();
        tempFile.deleteFile();
        resultMessage = "Impossible de creer le fichier AI Render.";
        return false;
    }

    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), sampleRate, 2, 24, {}, 0));
    if (writer == nullptr)
    {
        tempReader.reset();
        silenceInstrumentState(host, instrumentTrack, blockSize, true);
        file.deleteFile();
        tempFile.deleteFile();
        resultMessage = "Impossible de creer le writer WAV.";
        return false;
    }
    stream.release();
    juce::AudioBuffer<float> finalBlock(2, blockSize);
    std::int64_t readPos = 0;
    while (readPos < tempReader->lengthInSamples)
    {
        const int num = (int)juce::jmin<std::int64_t>(blockSize, tempReader->lengthInSamples - readPos);
        finalBlock.clear();
        if (!tempReader->read(&finalBlock, 0, num, readPos, true, true))
        {
            writer.reset();
            tempReader.reset();
            silenceInstrumentState(host, instrumentTrack, blockSize, true);
            file.deleteFile();
            tempFile.deleteFile();
            resultMessage = "Echec de la relecture du rendu audio temporaire.";
            return false;
        }
        if (renderGain < 1.0f)
            finalBlock.applyGain(0, num, renderGain);
        if (!writer->writeFromAudioSampleBuffer(finalBlock, 0, num))
        {
            writer.reset();
            tempReader.reset();
            silenceInstrumentState(host, instrumentTrack, blockSize, true);
            file.deleteFile();
            tempFile.deleteFile();
            resultMessage = "Echec de l'ecriture du rendu audio.";
            return false;
        }
        readPos += num;
    }
    writer.reset();
    tempReader.reset();
    tempFile.deleteFile();

    // Critical anti-feedback step: terminate every synth voice after offline render.
    silenceInstrumentState(host, instrumentTrack, blockSize, true);

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
