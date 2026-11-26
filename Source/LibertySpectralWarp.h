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
    static constexpr int analysisHop = fftSize / 8; // at 4x stretch, synthesis hop stays <= half a window

    // Constant-ratio phase vocoder with phase accumulation and weighted
    // overlap-add. Output duration is independent of pitch/formant settings.
    // The caller must schedule this on a worker thread and publish a completed
    // immutable audio buffer atomically.
    static juce::AudioBuffer<float> render(const juce::AudioBuffer<float>& input,
                                            double durationRatio,
                                            double pitchSemitones = 0.0,
                                            double formantSemitones = 0.0)
    {
        if(!std::isfinite(durationRatio)||!std::isfinite(pitchSemitones)||!std::isfinite(formantSemitones))
            return juce::AudioBuffer<float>(1,0);
        durationRatio = juce::jlimit(0.25, 4.0, durationRatio);
        pitchSemitones = juce::jlimit(-12.0, 12.0, pitchSemitones);
        formantSemitones = juce::jlimit(-12.0, 12.0, formantSemitones);
        const int channels = input.getNumChannels();
        const int sourceLength = input.getNumSamples();
        // Cap output allocation before converting to int; oversized jobs
        // must be rejected instead of overflowing the sample count.
        const double requestedSamples = (double)sourceLength * durationRatio;
        // Bound total output + shared overlap normalisation to 128 MiB.
        if (!std::isfinite(requestedSamples)||channels<1||channels>2||
            requestedSamples*(channels*sizeof(float)+sizeof(float))>128ull*1024*1024)
            return juce::AudioBuffer<float>(juce::jmax(1, channels), 0);
        const int outputLength = juce::jmax(1, (int)std::llround(requestedSamples));
        juce::AudioBuffer<float> output(juce::jmax(1, channels), outputLength);
        output.clear();
        if (sourceLength < fftSize) return juce::AudioBuffer<float>(channels,0);

        juce::dsp::FFT fft(fftOrder);
        const double synthesisHop = analysisHop * durationRatio;
        const int bins = fftSize / 2;
        const double twoPi = juce::MathConstants<double>::twoPi;
        std::vector<float> window((size_t)fftSize);
        for (int n = 0; n < fftSize; ++n)
            window[(size_t)n] = (float)(0.5 - 0.5 * std::cos(twoPi * n / fftSize));
        const double pitchRatio = std::pow(2.0, pitchSemitones / 12.0);
        const double formantRatio = std::pow(2.0, formantSemitones / 12.0);
        // Exact unity path: don't run a phase vocoder if nothing changed.
        if(durationRatio==1.0&&pitchSemitones==0.0&&formantSemitones==0.0){
            for(int ch=0;ch<channels;++ch)for(int i=0;i<sourceLength;++i)
                if(!std::isfinite(input.getSample(ch,i)))return juce::AudioBuffer<float>(channels,0);
            return juce::AudioBuffer<float>(input);
        }
        // The synthesis coverage is identical for both channels.
        std::vector<float> normalisation((size_t)outputLength,0.f);
        for (int channel = 0; channel < channels; ++channel)
        {
            std::vector<float> fftData((size_t)fftSize * 2, 0.0f);
            std::vector<float> inverseData((size_t)fftSize * 2, 0.0f);
            std::vector<double> previousPhase((size_t)bins + 1, 0.0);
            std::vector<double> accumulatedPhase((size_t)bins + 1, 0.0);
            std::vector<double> magnitudes((size_t)bins + 1, 0.0);
            std::vector<double> frequencies((size_t)bins + 1, 0.0);
            std::vector<double> envelopePrefix((size_t)bins + 2, 0.0);
            const float* source = input.getReadPointer(channel);
            float* target = output.getWritePointer(channel);
            float inputPeak=0;
            for(int i=0;i<sourceLength;++i){
                if(!std::isfinite(source[i]))return juce::AudioBuffer<float>(channels,0);
                inputPeak=std::max(inputPeak,std::abs(source[i]));
            }
            int previousCenter=0;
            // Centre windows on the first and last samples with zero padding.
            // Avoid dividing incoherent edge bins by a vanishing Hann weight.
            for (int frame=0; (long long)frame*analysisHop < (long long)sourceLength+analysisHop; ++frame)
            {
                const int sourceStart = frame*analysisHop-fftSize/2;
                const int center = (int)std::llround(frame*synthesisHop);
                const int targetStart=center-fftSize/2;
                const int actualHop=center-previousCenter;previousCenter=center;
                if(targetStart>=outputLength)break;
                std::fill(fftData.begin(),fftData.end(),0.0f);
                for(int n=0;n<fftSize;++n){
                    const int index=sourceStart+n;
                    if(index>=0&&index<sourceLength)fftData[(size_t)n]=source[index]*window[(size_t)n];
                }
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
                // Prefix sum of magnitudes, preserving replicated edge bins.
                envelopePrefix[0]=0;
                for(int k=0;k<=bins;++k)envelopePrefix[(size_t)k+1]=envelopePrefix[(size_t)k]+magnitudes[(size_t)k];
                auto envelopeAt=[&](int center){
                    constexpr int radius=8;
                    const int low=juce::jmax(0,center-radius),high=juce::jmin(bins,center+radius);
                    return envelopePrefix[(size_t)high+1]-envelopePrefix[(size_t)low]
                        +juce::jmax(0,radius-center)*magnitudes.front()
                        +juce::jmax(0,center+radius-bins)*magnitudes.back();
                };
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
                    const double envelope=envelopeAt(lo);
                    const double shiftedEnvelope=envelopeAt(envelopeBin);
                    const double formantGain = juce::jlimit(0.25, 4.0,
                        (shiftedEnvelope + 0.00001) / (envelope + 0.00001));
                    if(frame==0)accumulatedPhase[(size_t)k]=previousPhase[(size_t)lo];
                    else accumulatedPhase[(size_t)k]+=frequencies[(size_t)lo]*actualHop*pitchRatio;
                    const double angle = accumulatedPhase[(size_t)k];
                    inverseData[(size_t)2*k] = (float)(magnitude * formantGain * std::cos(angle));
                    inverseData[(size_t)2*k+1] = (k==0||k==bins)?0.f:(float)(magnitude*formantGain*std::sin(angle));
                }
                fft.performRealOnlyInverseTransform(inverseData.data());
                for(int n=0;n<fftSize&&targetStart+n<outputLength;++n){
                    const int index=targetStart+n;if(index<0)continue;
                    target[index]+=inverseData[(size_t)n]*window[(size_t)n];
                    if(channel==0)normalisation[(size_t)index]+=window[(size_t)n]*window[(size_t)n];
                }
            }
            // A bounded denominator protects any uncovered edge. Reject
            // pathological renders rather than clipping or publishing them.
            const float peakLimit=std::max(0.001f,inputPeak*4.f);
            for(int n=0;n<outputLength;++n){
                target[n]/=std::max(.25f,normalisation[(size_t)n]);
                if(!std::isfinite(target[n])||std::abs(target[n])>peakLimit)
                    return juce::AudioBuffer<float>(channels,0);
            }
        }
        return output;
    }
};
