#define private public
#include "MainComponent.h"
#undef private
#include "PluginHost.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>

namespace
{
juce::File makeRenderFile()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("BSM")
                   .getChildFile("Liberty")
                   .getChildFile("AI Renders");
    dir.createDirectory();
    return dir.getNonexistentChildFile("AI Render " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S"), ".wav", false);
}
}

bool renderLibertyAIActiveInstrumentToAudio(MainComponent& owner, juce::String& resultMessage)
{
    auto& host = LibertyPluginHost::instance();
    if (!host.hasInstrument())
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
    for (int i = 0; i < AudioEngine::maxAudioTracks; ++i)
        if (!owner.audioEngine.hasAudioFile(i)) { targetTrack = i; break; }
    if (targetTrack < 0)
    {
        resultMessage = "Aucune piste Audio vide disponible.";
        return false;
    }

    owner.audioEngine.setPlaying(false);
    owner.isPlaying = false;

    const double sampleRate = owner.audioEngine.getSampleRate() > 0.0 ? owner.audioEngine.getSampleRate() : 48000.0;
    const int blockSize = juce::jmax(64, owner.audioEngine.getBufferSize() > 0 ? owner.audioEngine.getBufferSize() : 512);
    const double clipDuration = juce::jmax(0.25, owner.midiClipLengthSeconds);
    const double tailSeconds = 2.0;
    const int totalSamples = juce::jmax(1, (int)std::ceil((clipDuration + tailSeconds) * sampleRate));

    juce::AudioBuffer<float> rendered(2, totalSamples);
    rendered.clear();

    int writePos = 0;
    bool firstBlock = true;
    while (writePos < totalSamples)
    {
        const int num = juce::jmin(blockSize, totalSamples - writePos);
        juce::AudioBuffer<float> block(2, num);
        block.clear();
        juce::MidiBuffer midi;

        if (firstBlock)
        {
            for (int channel = 1; channel <= 16; ++channel)
                midi.addEvent(juce::MidiMessage::allNotesOff(channel), 0);
            firstBlock = false;
        }

        for (const auto& n : notes)
        {
            const auto startSeconds = MidiEngine::tickToSeconds(n.startTick, owner.tempoBpm);
            const auto endSeconds = MidiEngine::tickToSeconds(n.startTick + n.lengthTicks, owner.tempoBpm);
            const auto startSample = (int)std::llround(startSeconds * sampleRate);
            const auto endSample = (int)std::llround(endSeconds * sampleRate);
            if (startSample >= writePos && startSample < writePos + num)
                midi.addEvent(juce::MidiMessage::noteOn((int)n.channel, (int)n.pitch, (juce::uint8)n.velocity), startSample - writePos);
            if (endSample >= writePos && endSample < writePos + num)
                midi.addEvent(juce::MidiMessage::noteOff((int)n.channel, (int)n.pitch), endSample - writePos);
        }

        const int clipEndSample = (int)std::llround(clipDuration * sampleRate);
        if (clipEndSample >= writePos && clipEndSample < writePos + num)
            for (int channel = 1; channel <= 16; ++channel)
                midi.addEvent(juce::MidiMessage::allNotesOff(channel), clipEndSample - writePos);

        float* channels[] = { block.getWritePointer(0), block.getWritePointer(1) };
        if (!host.processInstrument(channels, 2, num, midi))
        {
            resultMessage = "L'instrument charge n'a pas pu etre rendu.";
            return false;
        }

        rendered.copyFrom(0, writePos, block, 0, 0, num);
        rendered.copyFrom(1, writePos, block, 1, 0, num);
        writePos += num;
    }

    auto file = makeRenderFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
    {
        resultMessage = "Impossible de creer le fichier AI Render.";
        return false;
    }

    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), sampleRate, 2, 24, {}, 0));
    if (writer == nullptr)
    {
        resultMessage = "Impossible de creer le writer WAV.";
        return false;
    }
    stream.release();
    if (!writer->writeFromAudioSampleBuffer(rendered, 0, rendered.getNumSamples()))
    {
        resultMessage = "Echec de l'ecriture du rendu audio.";
        return false;
    }
    writer.reset();

    juce::String error;
    if (!owner.audioEngine.loadAudioFileIntoTrack(targetTrack, file, error))
    {
        resultMessage = error.isNotEmpty() ? error : "Le rendu WAV n'a pas pu etre charge dans ARRANGE.";
        return false;
    }

    owner.audioEngine.setTrackStartSeconds(targetTrack, juce::jmax(0.0, owner.midiClipStartSeconds));
    owner.trackSourceFiles[(size_t)targetTrack] = file;
    owner.rebuildWaveformCache(targetTrack);
    owner.selectedTrack = targetTrack;
    owner.repaint();

    resultMessage = "AI Render cree sur Audio " + juce::String(targetTrack + 1) + " - " + file.getFileName();
    return true;
}
