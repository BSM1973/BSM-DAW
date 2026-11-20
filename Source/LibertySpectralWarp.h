#pragma once
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <vector>

// Offline/pre-render spectral foundation. Never instantiate or call process()
// from the real-time audio callback: FFT, allocation and analysis are expensive.
class LibertySpectralWarp final
{
public:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int analysisHop = fftSize / 4;

    // Constant-ratio phase vocoder with phase accumulation and weighted
    // overlap-add. Output duration is independent of pitch/formant settings.
    // The caller must schedule this on a worker thread and publish a completed
    // immutable audio buffer atomically.
    static juce::AudioBuffer<float> render(const juce::AudioBuffer<float>& input,
                                            double durationRatio,
                                            double pitchSemitones = 0.0,
                                            double formantSemitones = 0.0)
    {
        durationRatio = juce::jlimit(0.25, 4.0, durationRatio);
        pitchSemitones = juce::jlimit(-12.0, 12.0, pitchSemitones);
        formantSemitones = juce::jlimit(-12.0, 12.0, formantSemitones);
        const int channels = input.getNumChannels();
        const int sourceLength = input.getNumSamples();
        // Cap output allocation before converting to int; oversized jobs
        // must be rejected instead of overflowing the sample count.
        const double requestedSamples = (double)sourceLength * durationRatio;
        if (!std::isfinite(requestedSamples) || requestedSamples > 50000000.0)
            return juce::AudioBuffer<float>(juce::jmax(1, channels), 1);
        const int outputLength = juce::jmax(1, (int)std::llround(requestedSamples));
        juce::AudioBuffer<float> output(juce::jmax(1, channels), outputLength);
        output.clear();
        if (channels == 0 || sourceLength < fftSize) return output;

        juce::dsp::FFT fft(fftOrder);
        const double synthesisHop = analysisHop * durationRatio;
        const int bins = fftSize / 2;
        const double twoPi = juce::MathConstants<double>::twoPi;
        std::vector<float> window((size_t)fftSize);
        for (int n = 0; n < fftSize; ++n)
            window[(size_t)n] = (float)(0.5 - 0.5 * std::cos(twoPi * n / fftSize));
        const double pitchRatio = std::pow(2.0, pitchSemitones / 12.0);
        const double formantRatio = std::pow(2.0, formantSemitones / 12.0);
        for (int channel = 0; channel < channels; ++channel)
        {
            std::vector<float> fftData((size_t)fftSize * 2, 0.0f);
            std::vector<float> inverseData((size_t)fftSize * 2, 0.0f);
            std::vector<double> previousPhase((size_t)bins + 1, 0.0);
            std::vector<double> accumulatedPhase((size_t)bins + 1, 0.0);
            std::vector<double> magnitudes((size_t)bins + 1, 0.0);
            std::vector<double> frequencies((size_t)bins + 1, 0.0);
            std::vector<double> normalisation((size_t)outputLength, 0.0);
            const float* source = input.getReadPointer(channel);
            float* target = output.getWritePointer(channel);
            for (int frame = 0; frame * analysisHop + fftSize <= sourceLength; ++frame)
            {
                const int sourceStart = frame * analysisHop;
                const int targetStart = (int)std::llround(frame * synthesisHop);
                if (targetStart >= outputLength) break;
                std::fill(fftData.begin(), fftData.end(), 0.0f);
                for (int n = 0; n < fftSize; ++n)
                    fftData[(size_t)n] = source[sourceStart + n] * window[(size_t)n];
                fft.performRealOnlyForwardTransform(fftData.data());
                std::fill(inverseData.begin(), inverseData.end(), 0.0f);
                for (int k = 0; k <= bins; ++k)
                {
                    const double real = fftData[(size_t)2*k];
                    const double imag = fftData[(size_t)2*k+1];
                    magnitudes[(size_t)k] = std::hypot(real, imag);
                    const double phase = std::atan2(imag, real);
                    const double expected = twoPi * k * analysisHop / fftSize;
                    double delta = phase - previousPhase[(size_t)k] - expected;
                    delta -= twoPi * std::round(delta / twoPi);
                    frequencies[(size_t)k] = (expected + delta) / analysisHop;
                    previousPhase[(size_t)k] = phase;
                }
                // Spectral remapping changes pitch without altering the
                // synthesis hop. A smoothed magnitude envelope is used for
                // coarse formant compensation; detailed LPC/cepstral formant
                // modelling is a subsequent quality upgrade.
                for (int k = 0; k <= bins; ++k)
                {
                    const double sourceBin = k / pitchRatio;
                    const int lo = (int)sourceBin;
                    if (lo > bins) continue;
                    const int hi = juce::jmin(bins, lo + 1);
                    const double fraction = sourceBin - lo;
                    const double magnitude = juce::jmap(fraction,
                        magnitudes[(size_t)lo], magnitudes[(size_t)hi]);
                    const int envelopeBin = juce::jlimit(0, bins,
                        (int)std::llround(sourceBin / formantRatio));
                    const int radius = 8;
                    double envelope = 0.0;
                    double shiftedEnvelope = 0.0;
                    for (int d = -radius; d <= radius; ++d)
                    {
                        envelope += magnitudes[(size_t)juce::jlimit(0, bins, lo+d)];
                        shiftedEnvelope += magnitudes[(size_t)juce::jlimit(0, bins, envelopeBin+d)];
                    }
                    const double formantGain = juce::jlimit(0.25, 4.0,
                        (shiftedEnvelope + 0.00001) / (envelope + 0.00001));
                    accumulatedPhase[(size_t)k] += frequencies[(size_t)lo]
                        * synthesisHop * pitchRatio;
                    const double angle = accumulatedPhase[(size_t)k];
                    inverseData[(size_t)2*k] = (float)(magnitude * formantGain * std::cos(angle));
                    inverseData[(size_t)2*k+1] = (float)(magnitude * formantGain * std::sin(angle));
                }
                fft.performRealOnlyInverseTransform(inverseData.data());
                for (int n = 0; n < fftSize && targetStart+n < outputLength; ++n)
                {
                    const int index = targetStart+n;
                    target[index] += inverseData[(size_t)n] * window[(size_t)n];
                    normalisation[(size_t)index] += (double)window[(size_t)n] * window[(size_t)n];
                }
            }
            for (int n = 0; n < outputLength; ++n)
                if (normalisation[(size_t)n] > 0.000001)
                    target[n] /= (float)normalisation[(size_t)n];
        }
        return output;
    }
};
