#pragma once

// Copy precisely one comparison row between CANAL A/B drafts.
// Row indices follow LibertyChannelSectionComparison::rows().
// This helper does not access the audio engine or change the active bank.
namespace LibertyChannelParameterTransfer
{
template<class Settings>
bool copy(Settings& destination,const Settings& source,int section,int row) noexcept
{
    switch(section)
    {
        case 0: // INPUT: gain, high-pass
            if(row==0){destination.inputGainDb=source.inputGainDb;return true;}
            if(row==1){destination.highPassHz=source.highPassHz;return true;}
            return false;
        case 1: // GATE: enabled, threshold
            if(row==0){destination.gateEnabled=source.gateEnabled;return true;}
            if(row==1){destination.gateThresholdDb=source.gateThresholdDb;return true;}
            return false;
        case 2: // COMPRESSOR: enabled, threshold, ratio
            if(row==0){destination.compressorEnabled=source.compressorEnabled;return true;}
            if(row==1){destination.compressorThresholdDb=source.compressorThresholdDb;return true;}
            if(row==2){destination.compressorRatio=source.compressorRatio;return true;}
            return false;
        case 3: // EQUALIZER: enabled, low, mid, high
            if(row==0){destination.eqEnabled=source.eqEnabled;return true;}
            if(row==1){destination.eqLowDb=source.eqLowDb;return true;}
            if(row==2){destination.eqMidDb=source.eqMidDb;return true;}
            if(row==3){destination.eqHighDb=source.eqHighDb;return true;}
            return false;
        default:
            return false;
    }
}
}
