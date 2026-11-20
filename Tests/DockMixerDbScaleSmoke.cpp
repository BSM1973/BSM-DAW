#include "DockMixerDbScale.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

using namespace LibertyDockMixerDbScale;

int main()
{
    assert(gainToPosition(0.0f)==0.0f);
    assert(gainToPosition(-1.0f)==0.0f);
    assert(gainToPosition(std::numeric_limits<float>::quiet_NaN())==0.0f);
    assert(positionToGain(0.0f)==0.0f);
    assert(positionToGain(-1.0f)==0.0f);
    assert(positionToGain(1.0f)==2.0f);
    assert(positionToGain(2.0f)==2.0f);
    assert(positionToGain(std::numeric_limits<float>::quiet_NaN())==0.0f);
    assert(std::abs(gainToDb(1.0f))<0.0001f);
    assert(std::abs(gainToDb(0.5f)+6.0206f)<0.001f);
    assert(std::abs(gainToPosition(1.0f)-60.f/(ceilingDb-floorDb))<0.0001f);
    assert(std::abs(positionToGain(gainToPosition(1.0f))-1.0f)<0.0001f);

    // The visual gain-fader position and an actual dB value must agree.
    for(const float db: {-60.f,-48.f,-36.f,-24.f,-18.f,-12.f,-6.f,0.f,6.f})
    {
        const float gain=std::pow(10.0f,db/20.0f);
        const float position=gainToPosition(gain);
        assert(position>=0.0f&&position<=1.0f);
        assert(std::abs(gainToDb(positionToGain(position))-db)<0.001f);
    }
    assert(ticks[0].major&&std::strcmp(ticks[0].label,"+6")==0);
    assert(ticks[1].major&&std::strcmp(ticks[1].label,"0")==0);
    assert(ticks.back().major&&std::strcmp(ticks.back().label,"-60")==0);
    // Check the real 13-inch compact fader height and extremes.
    for(const int height: {0,12,32,48,60,78,90,108,124,140,160,180,240,360})
    {
        const int top=72,bottom=top+height;
        const auto marks=atPixels(top,bottom);
        int labelled=0;
        for(std::size_t i=0;i<marks.size();++i)
        {
            const auto& mark=marks[i];
            if(height>0) assert(mark.y>=top&&mark.y<=bottom);
            if(i>0&&height>0) assert(mark.y>=marks[i-1].y);
            if(!mark.showLabel) continue;
            assert(mark.major);
            assert(mark.label[0]!='\0');
            assert(mark.y-6>=top-6&&mark.y+6<=bottom+6);
            ++labelled;
            for(std::size_t j=0;j<i;++j)
                if(marks[j].showLabel)
                    assert(std::abs(mark.y-marks[j].y)>=minLabelSpacing);
        }
        if(height>=90)assert(labelled>=5);
        if(height>=140)
        {
            // Enough negative values remain visible at common dock heights.
            for(auto required: {"0","-12","-24","-36","-48","-60"})
            {
                bool found=false;
                for(const auto& mark:marks)
                    if(mark.showLabel&&std::strcmp(mark.label,required)==0)found=true;
                assert(found);
            }
        }
        if(height<=0)assert(labelled==0);
    }
    std::cout<<"Dock Mixer dB: accurate fader conversions, clear minus labels, no text overlaps OK\n";
}
