#pragma once
// Deliberately contains no JUCE or Apple SDK headers. The DSP backend is
// compiled in its own translation unit, not in browser/UI translation units.
namespace LibertySampleLab {
bool renderStretch(const float* const* inputs,int inputSamples,float* const* outputs,int outputSamples,int channels,double rate,int pitch);
}
