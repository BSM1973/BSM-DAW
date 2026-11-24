#include "LibertySampleStretch.h"
#include <signalsmith-stretch/signalsmith-stretch.h>
#include <cmath>

namespace LibertySampleLab {
bool renderStretch(const float* const* inputs,int inputSamples,float* const* outputs,int outputSamples,int channels,double rate,int pitch)
{
    if(!inputs||!outputs||inputSamples<=0||outputSamples<=0||channels<1||channels>2||
       !std::isfinite(rate)||rate<8000||rate>192000||pitch < -6||pitch>6)
        return false;
    for(int c=0;c<channels;++c)
        if(!inputs[c]||!outputs[c])return false;
    // Keep Accelerate isolated from JUCE's global using directive, which
    // otherwise introduces ambiguous Apple Point and Component typedefs.
    signalsmith::stretch::SignalsmithStretch<float> stretch;
    stretch.presetDefault(channels,(float)rate);
    stretch.setTransposeSemitones((float)pitch);
    stretch.process(inputs,inputSamples,outputs,outputSamples);
    return true;
}
}
