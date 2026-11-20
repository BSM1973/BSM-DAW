#pragma once
// Deterministic, offline MIDI clip transformations. Never called by the audio callback.
// The user's source notes remain unchanged; output is a derived performance layer.
#include <algorithm>
#include <cstdint>
#include <vector>

namespace LibertyPerformGenerator
{
constexpr std::size_t maxNotes=8192;
constexpr std::int64_t ticksPerBeat=960;
struct Parameters
{
    int kind=0;        // 0 arpeggio, 1 variation, 2 drum rhythm
    int division=1;    // 0 eighth, 1 sixteenth, 2 thirty-second
    int steps=16;      // pattern length (8, 16, 32, 64)
    int direction=0;   // 0 up, 1 down, 2 up/down, 3 random
    int octaves=1;     // 1..4
    int density=75;    // 0..100
    int swing=0;       // 0..75 %
    int variation=30;  // 0..100 %
    int velocity=100;  // 1..127
    int rhythmStyle=0; // 0 house, 1 techno, 2 hip-hop, 3 break
    std::uint32_t seed=0x38adf15bu;
};
inline Parameters validated(Parameters p) noexcept
{
    p.kind=std::clamp(p.kind,0,2);
    p.division=std::clamp(p.division,0,2);
    if(p.steps!=8 && p.steps!=16 && p.steps!=32 && p.steps!=64)p.steps=16;
    p.direction=std::clamp(p.direction,0,3);
    p.octaves=std::clamp(p.octaves,1,4);
    p.density=std::clamp(p.density,0,100);
    p.swing=std::clamp(p.swing,0,75);
    p.variation=std::clamp(p.variation,0,100);
    p.velocity=std::clamp(p.velocity,1,127);
    p.rhythmStyle=std::clamp(p.rhythmStyle,0,3);
    return p;
}
inline std::uint32_t next(std::uint32_t& s) noexcept
{
    s=1664525u*s+1013904223u;
    return s;
}
inline bool chance(std::uint32_t& seed,int percent) noexcept
{
    return (int)(next(seed)%100u)<std::clamp(percent,0,100);
}
inline std::int64_t stepTicks(int division) noexcept
{
    return division==0?480:division==2?120:240;
}
inline std::int64_t swung(std::int64_t base,int index,
                           std::int64_t step,int percent) noexcept
{
    return base+((index&1)!=0?step*std::clamp(percent,0,75)/200:0);
}
template<class Note>
bool append(std::vector<Note>& dest,Note note,std::int64_t clipLength) {
    if(dest.size()>=maxNotes || clipLength<=0 || note.startTick>=clipLength ||
       note.startTick<0)return false;
    note.lengthTicks=std::clamp<std::int64_t>(note.lengthTicks,1,clipLength-note.startTick);
    note.pitch=(std::uint8_t)std::clamp((int)note.pitch,0,127);
    note.velocity=(std::uint8_t)std::clamp((int)note.velocity,1,127);
    dest.push_back(note);
    return true;
}
template<class Note>
std::vector<Note> render(const std::vector<Note>& source,
                         std::int64_t clipTicks,Parameters parameters)
{
    const auto p=validated(parameters);
    std::vector<Note> result;
    if(clipTicks<=0)return result;
    result.reserve(std::min<std::size_t>(maxNotes,
        p.kind==1?source.size()*2+4:(std::size_t)(clipTicks/120+32)));
    auto seed=p.seed;
    const auto step=stepTicks(p.division);

    const auto maxIterations=std::min<std::int64_t>(
        (clipTicks-1)/step+1,(std::int64_t)maxNotes*16);

    if(p.kind==0)
    {
        if(p.density==0)return result;
        // Arpeggiate the distinct played pitches, not an arbitrary fixed key.
        std::vector<Note> chord;
        for(const auto& note:source)
            if(!note.muted && note.velocity>0) chord.push_back(note);
        std::sort(chord.begin(),chord.end(),[](const Note& a,const Note& b)
            {return a.pitch<b.pitch;});
        chord.erase(std::unique(chord.begin(),chord.end(),
            [](const Note& a,const Note& b){return a.pitch==b.pitch;}),chord.end());
        if(chord.empty())return result;
        const int pool=(int)chord.size()*p.octaves;
        if(pool<=0)return result;
        for(std::int64_t i=0;i<maxIterations && result.size()<maxNotes;++i)
        {
            auto start=swung(i*step,(int)i,step,p.swing);
            if(start>=clipTicks)continue;
            if(p.variation>0)
            {
                const auto deviation=std::max<std::int64_t>(1,step*p.variation/600);
                const auto jitter=(std::int64_t)(next(seed)%
                    (std::uint32_t)(deviation*2+1))-deviation;
                start=std::clamp<std::int64_t>(start+jitter,0,clipTicks-1);
            }
            // Density is a true probability of emitting each step.
            if(!chance(seed,p.density))continue;
            const int row=(int)(i%std::max<std::int64_t>(1,p.steps));
            int index=row%pool;
            if(p.direction==1)index=pool-1-index;
            else if(p.direction==2 && pool>1)
            {
                const int cycle=pool*2-2;
                const int pos=row%cycle;
                index=pos<pool?pos:cycle-pos;
            }
            else if(p.direction==3)index=(int)(next(seed)%(std::uint32_t)pool);
            Note note=chord[(std::size_t)(index%chord.size())];
            note.pitch=(std::uint8_t)std::clamp(
                (int)note.pitch+12*(index/(int)chord.size()),0,127);
            note.startTick=start;
            note.lengthTicks=std::min<std::int64_t>(
                clipTicks-start,std::max<std::int64_t>(1,step*85/100));
            const int humanVelocity=p.variation>0?
                (int)(next(seed)%(std::uint32_t)(1+p.variation/4))-
                p.variation/8:0;
            note.velocity=(std::uint8_t)std::clamp(
                ((int)note.velocity*p.velocity)/100+humanVelocity,1,127);
            note.muted=false;
            append(result,note,clipTicks);
        }
    }
    else if(p.kind==1)
    {
        // Never alter the original notes. Perturb note times/lengths, velocity
        // and optionally choose another pitch already present in the motif.
        std::vector<int> scale;
        for(const auto& note:source)
            if(!note.muted)scale.push_back((int)note.pitch);
        std::sort(scale.begin(),scale.end());
        scale.erase(std::unique(scale.begin(),scale.end()),scale.end());
        const auto maxShift=step*p.variation/200;
        const int velocityShift=p.variation*28/100;
        const int skipProbability=p.variation/12;
        for(const auto& original:source)
        {
            if(result.size()>=maxNotes)break;
            if(original.muted){append(result,original,clipTicks);continue;}
            if(chance(seed,skipProbability) || !chance(seed,p.density))continue;
            Note note=original;
            const int shiftRange=(int)std::max<std::int64_t>(0,maxShift);
            const int timeDelta=shiftRange>0?
                (int)(next(seed)%(std::uint32_t)(2*shiftRange+1))-shiftRange:0;
            note.startTick=std::clamp<std::int64_t>(
                original.startTick+timeDelta+
                (((original.startTick/step)&1)!=0?
                    step*p.swing/200:0),0,clipTicks-1);
            const int dv=velocityShift>0?
                (int)(next(seed)%(std::uint32_t)(2*velocityShift+1))-velocityShift:0;
            note.velocity=(std::uint8_t)std::clamp(
                ((int)original.velocity*p.velocity)/100+dv,1,127);
            const int lengthDelta=shiftRange>0?
                (int)(next(seed)%(std::uint32_t)(2*shiftRange+1))-shiftRange:0;
            note.lengthTicks=std::clamp<std::int64_t>(
                original.lengthTicks+lengthDelta,1,clipTicks-note.startTick);
            if(scale.size()>1 && chance(seed,p.variation/5))
                note.pitch=(std::uint8_t)scale[(std::size_t)(next(seed)%scale.size())];
            append(result,note,clipTicks);
            if(p.variation>=50 && chance(seed,(p.variation-40)/6) &&
               result.size()<maxNotes)
            {
                Note echo=note;
                echo.startTick+=step;
                echo.velocity=(std::uint8_t)std::clamp((int)note.velocity*7/10,1,127);
                if(echo.startTick<clipTicks)append(result,echo,clipTicks);
            }
        }
    }
    else
    {
        // Common drum maps: kick 36, snare 38, closed/open hats 42/46.
        // Style changes the anchored pattern, density adds/subtracts hits.
        if(p.density==0)return result;
        const std::int64_t pattern=std::max<std::int64_t>(1,p.steps);
        for(std::int64_t i=0;i<maxIterations && result.size()<maxNotes;++i)
        {
            const int pos=(int)(i%pattern)%16;
            const auto start=swung(i*step,(int)i,step,p.swing);
            if(start>=clipTicks)continue;
            const bool kick=p.rhythmStyle==0?(pos%4==0):
                p.rhythmStyle==1?(pos%4==0 || pos==14):
                p.rhythmStyle==2?(pos==0 || pos==7 || pos==10):
                                    (pos==0 || pos==3 || pos==10);
            const bool snare=p.rhythmStyle==0?(pos==4 || pos==12):
                p.rhythmStyle==1?(pos==4 || pos==12):
                p.rhythmStyle==2?(pos==4 || pos==12):
                                    (pos==4 || pos==11 || pos==14);
            const bool hat=p.rhythmStyle==1?(pos%2==0 || pos==15):
                p.rhythmStyle==2?(pos%2==1):
                                    (pos%2==0);
            const int pitches[3]={36,38,42};
            const bool hits[3]={kick,snare,hat};
            for(int voice=0;voice<3 && result.size()<maxNotes;++voice)
            {
                const int baseChance=hits[voice]?
                    (voice==2?p.density:std::min(100,p.density+20)):
                    (p.variation*15/100);
                if(!chance(seed,baseChance))continue;
                Note note{};
                note.startTick=start;
                note.lengthTicks=std::max<std::int64_t>(1,std::min<std::int64_t>(
                    clipTicks-start,voice==2?step/2:step*4/5));
                note.pitch=(std::uint8_t)pitches[voice];
                note.velocity=(std::uint8_t)std::clamp(p.velocity+
                    (int)(next(seed)%17u)-8-(voice==2?13:0),1,127);
                note.channel=10;
                note.muted=false;
                append(result,note,clipTicks);
            }
        }
    }
    std::stable_sort(result.begin(),result.end(),[](const Note& a,const Note& b)
    {
        if(a.startTick!=b.startTick)return a.startTick<b.startTick;
        return a.pitch<b.pitch;
    });
    return result;
}
}
