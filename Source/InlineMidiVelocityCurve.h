#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>
#include <map>

namespace LibertyInlineVelocity
{
enum class Curve : int { linear = 0, exponential = 1, sCurve = 2 };
enum class Mode : int { absolute = 0, relative = 1 };
enum class AccentPattern : int {
    strongBeats=0, everyBeat=1, offbeat=2, backbeat=3,
    phrase2=4, phrase4=5, phrase8=6,
    pickup2=7, pickup4=8, pickup8=9,
    earlyPickup2=10, earlyPickup4=11, earlyPickup8=12,
    fullPhrase2=13, fullPhrase4=14, fullPhrase8=15,
    buildup2=16, buildup4=17, buildup8=18,
    longBuildup2=19, longBuildup4=20, longBuildup8=21,
    longFade2=22, longFade4=23, longFade8=24,
    arc2=25, arc4=26, arc8=27
};
// A phrase accent falls only on measure 1 of each 2, 4 or 8-bar cycle.
// All cycles begin at the project's musical zero, never at clip selection.
inline int accentPhraseBars(AccentPattern pattern) noexcept
{
    switch(pattern)
    {
        case AccentPattern::phrase2:return 2;
        case AccentPattern::phrase4:return 4;
        case AccentPattern::phrase8:return 8;
        default:return 0;
    }
}
// Pickup emphasizes the final project pulse before the next phrase;
// the notes themselves remain at their recorded MIDI timing.
inline int accentPickupBars(AccentPattern pattern) noexcept
{
    switch(pattern)
    {
        case AccentPattern::pickup2:return 2;
        case AccentPattern::pickup4:return 4;
        case AccentPattern::pickup8:
        case AccentPattern::earlyPickup8:return 8;
        case AccentPattern::earlyPickup2:return 2;
        case AccentPattern::earlyPickup4:return 4;
        default:return 0;
    }
}
// Combined profile: strong first beat and a lighter eighth-note pickup.
inline int accentFullPhraseBars(AccentPattern pattern) noexcept
{
    switch(pattern)
    {
        case AccentPattern::fullPhrase2:return 2;
        case AccentPattern::fullPhrase4:return 4;
        case AccentPattern::fullPhrase8:return 8;
        default:return 0;
    }
}
// Rising rhythmic fill on the final measure of a 2, 4 or 8-bar phrase.
// Earlier measures and unchanged profiles retain their original behavior.
inline int accentBuildupBars(AccentPattern pattern) noexcept
{
    switch(pattern)
    {
        case AccentPattern::buildup2:return 2;
        case AccentPattern::buildup4:return 4;
        case AccentPattern::buildup8:return 8;
        default:return 0;
    }
}
// Long crescendo: all beats of an entire 2/4/8-bar phrase rise gradually,
// with the highest accent on the last eighth before the next phrase.
inline int accentLongBuildupBars(AccentPattern pattern) noexcept
{
    switch(pattern)
    {
        case AccentPattern::longBuildup2:return 2;
        case AccentPattern::longBuildup4:return 4;
        case AccentPattern::longBuildup8:return 8;
        default:return 0;
    }
}
// Gradual musical fade over every pulse of 2, 4 or 8 measures.
// Zero remains a pure no-op when both accent and ghost intensity are zero.
inline int accentLongFadeBars(AccentPattern pattern) noexcept
{
    switch(pattern)
    {
        case AccentPattern::longFade2:return 2;
        case AccentPattern::longFade4:return 4;
        case AccentPattern::longFade8:return 8;
        default:return 0;
    }
}
// An expressive arc grows to the exact central pulse of a phrase and
// recedes toward the final eighth. The project grid defines its phase.
inline int accentArcBars(AccentPattern pattern) noexcept
{
    switch(pattern)
    {
        case AccentPattern::arc2:return 2;
        case AccentPattern::arc4:return 4;
        case AccentPattern::arc8:return 8;
        default:return 0;
    }
}
inline bool isEarlyPickup(AccentPattern pattern) noexcept
{
    return pattern==AccentPattern::earlyPickup2
        ||pattern==AccentPattern::earlyPickup4
        ||pattern==AccentPattern::earlyPickup8;
}
// One eighth note before the next phrase: half a quarter-note beat in
// simple meters, but one actual eighth in compound 6/8, 9/8 or 12/8.
inline std::int64_t earlyPickupOffset(std::int64_t beatTicks,
                                     bool compound) noexcept
{
    return compound?beatTicks:juce::jmax<std::int64_t>(1,beatTicks/2);
}

struct PreviewNote
{
    std::int64_t startTick = 0;
    int velocity = 100;
};

// Derive initial values from the actual selected notes. A chord is
// represented by its average velocity, not an arbitrary pitch index.
inline std::pair<int,int> boundaryVelocities(
    const std::vector<PreviewNote>& selected)
{
    if(selected.empty())return {64,100};
    std::int64_t firstTick=selected.front().startTick;
    std::int64_t lastTick=firstTick;
    for(const auto& note:selected){
        firstTick=std::min(firstTick,note.startTick);
        lastTick=std::max(lastTick,note.startTick);
    }
    std::int64_t firstSum=0,lastSum=0;
    int firstCount=0,lastCount=0;
    for(const auto& note:selected){
        const int velocity=juce::jlimit(1,127,note.velocity);
        if(note.startTick==firstTick){firstSum+=velocity;++firstCount;}
        if(note.startTick==lastTick){lastSum+=velocity;++lastCount;}
    }
    const int first=juce::jlimit(1,127,(int)std::lround(
        (double)firstSum/std::max(1,firstCount)));
    const int last=juce::jlimit(1,127,(int)std::lround(
        (double)lastSum/std::max(1,lastCount)));
    return {first,last};
}

// Real preview positions use the same musical-time interpolation as Apply.
inline double musicalPosition(std::int64_t tick,
                              std::int64_t first,
                              std::int64_t last) noexcept
{
    if(last<=first)return 0.0;
    return juce::jlimit(0.0,1.0,
        ((double)tick-(double)first)/((double)last-(double)first));
}

inline double progress(double position, Curve curve) noexcept
{
    const double t = juce::jlimit(0.0, 1.0, position);
    switch (curve)
    {
        case Curve::exponential: return t * t;
        case Curve::sCurve:      return t * t * (3.0 - 2.0 * t);
        case Curve::linear:
        default:                 return t;
    }
}
inline int velocityAt(int first, int last, double position, Curve curve) noexcept
{
    const double curved = progress(position, curve);
    return juce::jlimit(1, 127, (int)std::lround(first + (last - first) * curved));
}

// Shared by the non-destructive preview and the actual MIDI operation.
// Absolute replaces velocities with the envelope. Relative offsets the old
// velocities around their original (linear) phrase envelope, retaining local
// accents and chord dynamics. A common clamp per onset keeps chord intervals.
inline std::vector<int> transformVelocities(
    const std::vector<PreviewNote>& selected, int first, int last,
    Curve curve, Mode mode, int intensityPercent=100)
{
    std::vector<int> result;
    result.reserve(selected.size());
    if(selected.empty())return result;
    intensityPercent=juce::jlimit(0,100,intensityPercent);
    if(intensityPercent==0){
        for(const auto& note:selected)
            result.push_back(juce::jlimit(1,127,note.velocity));
        return result;
    }
    first=juce::jlimit(1,127,first);
    last=juce::jlimit(1,127,last);
    auto minTick=selected.front().startTick;
    auto maxTick=minTick;
    struct Range { int lowest=127, highest=1; };
    std::map<std::int64_t,Range> chordRanges;
    for(const auto& note:selected)
    {
        minTick=std::min(minTick,note.startTick);
        maxTick=std::max(maxTick,note.startTick);
        const int vel=juce::jlimit(1,127,note.velocity);
        auto& range=chordRanges[note.startTick];
        range.lowest=std::min(range.lowest,vel);
        range.highest=std::max(range.highest,vel);
    }
    const auto originalEndpoints=boundaryVelocities(selected);
    for(const auto& note:selected)
    {
        const double t=musicalPosition(note.startTick,minTick,maxTick);
        const int target=velocityAt(first,last,t,curve);
        if(mode==Mode::absolute)
        {
            const int oldValue=juce::jlimit(1,127,note.velocity);
            const int value=oldValue+(int)std::lround(
                ((double)target-oldValue)*intensityPercent/100.0);
            result.push_back(juce::jlimit(1,127,value));
            continue;
        }
        const int originalEnvelope=velocityAt(originalEndpoints.first,
            originalEndpoints.second,t,Curve::linear);
        const int requested=target-originalEnvelope;
        const auto& range=chordRanges.at(note.startTick);
        const int delta=juce::jlimit(1-range.lowest,127-range.highest,
                                   requested);
        // Chord notes share one rounded intensity-scaled delta.
        const int scaled=(int)std::lround((double)delta*intensityPercent/100.0);
        result.push_back(juce::jlimit(1,127,note.velocity+scaled));
    }
    return result;
}

// Deterministic humanisation is keyed by musical onset and an explicit seed.
// Every note in a chord receives the same bounded velocity offset. The result
// is independent of note order and remains identical in preview and Apply.
inline std::vector<int> humanizeVelocities(
    const std::vector<PreviewNote>& notes,
    const std::vector<int>& transformed, int amount, std::uint32_t seed)
{
    if(notes.size()!=transformed.size())return transformed;
    amount=juce::jlimit(0,20,amount);
    if(amount==0)return transformed;
    struct Extent {int low=127,high=1;};
    std::map<std::int64_t,Extent> chords;
    for(size_t i=0;i<notes.size();++i){
        const int velocity=juce::jlimit(1,127,transformed[i]);
        auto& chord=chords[notes[i].startTick];
        chord.low=std::min(chord.low,velocity);
        chord.high=std::max(chord.high,velocity);
    }
    std::vector<int> result;
    result.reserve(notes.size());
    for(size_t i=0;i<notes.size();++i){
        // SplitMix64 finalizer: fixed 64-bit arithmetic, no random global
        // state. Notes with the same startTick always share a variation.
        std::uint64_t x=static_cast<std::uint64_t>(notes[i].startTick)
            ^(static_cast<std::uint64_t>(seed)<<32)
            ^0x9e3779b97f4a7c15ULL;
        x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
        x=(x^(x>>27))*0x94d049bb133111ebULL;
        x^=(x>>31);
        const int proposed=(int)(x%(std::uint64_t)(2*amount+1))-amount;
        const auto& chord=chords.at(notes[i].startTick);
        const int offset=juce::jlimit(1-chord.low,127-chord.high,proposed);
        result.push_back(juce::jlimit(1,127,transformed[i]+offset));
    }
    return result;
}

// Accentuate the strong beats of the PROJECT time signature, not the
// beginning of the selected phrase. clipStartTick maps clip-local MIDI
// positions to the Arrangement timeline. Simultaneous notes are shifted
// together and clamped as a chord, preserving their dynamics.
inline std::vector<int> accentVelocities(
    const std::vector<PreviewNote>& notes,
    const std::vector<int>& transformed, int strength,
    int numerator, int denominator, std::int64_t ticksPerQuarter,
    std::int64_t clipStartTick,
    AccentPattern pattern=AccentPattern::strongBeats,
    int ghostAmount=0,
    int tolerancePercent=0,
    int swingPercent=50,
    bool progressiveAccents=false,
    Curve falloffCurve=Curve::linear)
{
    if(notes.size()!=transformed.size())return transformed;
    strength=juce::jlimit(0,20,strength);
    ghostAmount=juce::jlimit(0,20,ghostAmount);
    tolerancePercent=juce::jlimit(0,25,tolerancePercent);
    swingPercent=juce::jlimit(50,75,swingPercent);
    if((strength==0&&ghostAmount==0)||numerator<1||numerator>32
       ||denominator<1||denominator>32
       ||ticksPerQuarter<1||ticksPerQuarter>1000000
       ||clipStartTick<0)return transformed;
    const auto beatTicks=(ticksPerQuarter*4)/denominator;
    const auto barTicks=beatTicks*numerator;
    if(beatTicks<=0||barTicks<=0)return transformed;
    const bool compound=denominator==8&&numerator>=6&&numerator%3==0;
    const auto pulseTicks=compound?beatTicks*3:beatTicks;
    // Compare with nearby accented positions, including across bar edges.
    // Integer tick arithmetic is deterministic for preview, Apply and Undo.
    const auto toleranceTicks=pulseTicks*tolerancePercent/100;
    const auto nearGrid=[toleranceTicks](std::int64_t position,
                                          std::int64_t target,
                                          std::int64_t period,
                                          std::int64_t& deviation) noexcept
    {
        const auto d=position>=target?position-target:target-position;
        deviation=std::min(d,period-d);
        return deviation<=toleranceTicks;
    };
    struct Extent {int low=127,high=1;};
    std::map<std::int64_t,Extent> chords;
    for(size_t i=0;i<notes.size();++i){
        const int velocity=juce::jlimit(1,127,transformed[i]);
        auto& chord=chords[notes[i].startTick];
        chord.low=std::min(chord.low,velocity);
        chord.high=std::max(chord.high,velocity);
    }
    std::vector<int> result;
    result.reserve(notes.size());
    for(size_t i=0;i<notes.size();++i){
        const auto tick=notes[i].startTick;
        const int oldValue=juce::jlimit(1,127,transformed[i]);
        if(tick<0||tick>std::numeric_limits<std::int64_t>::max()-clipStartTick){
            result.push_back(oldValue);
            continue;
        }
        const auto inBar=(tick+clipStartTick)%barTicks;
        int requested=0;
        bool emphasized=false;
        std::int64_t deviation=0;
        if(pattern==AccentPattern::everyBeat)
        {
            emphasized=nearGrid(inBar%pulseTicks,0,pulseTicks,deviation);
            if(emphasized)requested=strength;
        }
        else if(pattern==AccentPattern::offbeat)
        {
            // Compound: second eighth of each dotted quarter.
            // Simple: shifted from straight (50%) to swung (75%).
            // Compound 6/8 etc. remains on the natural triplet grid.
            const auto displacement=compound?beatTicks
                :(pulseTicks*swingPercent+50)/100;
            emphasized=displacement>0
                       &&nearGrid(inBar%pulseTicks,displacement,pulseTicks,deviation);
            if(emphasized)requested=strength;
        }
        else if(accentArcBars(pattern)>0)
        {
            const auto cycleTicks=barTicks*accentArcBars(pattern);
            const auto local=(tick+clipStartTick)%cycleTicks;
            const auto totalPulses=cycleTicks/pulseTicks;
            const auto halfPulses=totalPulses/2;
            const auto lastEighth=cycleTicks-
                earlyPickupOffset(beatTicks,compound);
            // Two symmetrical phases: zero at the first pulse, full
            // accent in the centre, then back to zero at phrase end.
            emphasized=nearGrid(local,lastEighth,cycleTicks,deviation);
            if(emphasized)requested=0;
            else
            {
                for(std::int64_t pulse=0;pulse<totalPulses;++pulse)
                {
                    std::int64_t pulseDeviation=0;
                    if(nearGrid(local,pulse*pulseTicks,
                                cycleTicks,pulseDeviation))
                    {
                        emphasized=true;
                        deviation=pulseDeviation;
                        const auto distance=pulse>=halfPulses
                            ?pulse-halfPulses:halfPulses-pulse;
                        requested=(int)std::lround(
                            (double)strength*
                            (double)(halfPulses-distance)/
                            (double)halfPulses);
                        break;
                    }
                }
            }
        }
        else if(accentLongFadeBars(pattern)>0)
        {
            const auto cycleTicks=barTicks*accentLongFadeBars(pattern);
            const auto local=(tick+clipStartTick)%cycleTicks;
            const auto totalPulses=cycleTicks/pulseTicks;
            const auto lastEighth=cycleTicks-
                earlyPickupOffset(beatTicks,compound);
            // Strong opening downbeat, fading evenly toward a delicate
            // eighth-note ending. MIDI note timing is left untouched.
            emphasized=nearGrid(local,lastEighth,cycleTicks,deviation);
            if(emphasized)
                requested=(int)std::lround(
                    (double)strength/(double)(totalPulses+1));
            else
            {
                for(std::int64_t pulse=0;pulse<totalPulses;++pulse)
                {
                    std::int64_t pulseDeviation=0;
                    if(nearGrid(local,pulse*pulseTicks,
                                cycleTicks,pulseDeviation))
                    {
                        emphasized=true;
                        deviation=pulseDeviation;
                        requested=(int)std::lround(
                            (double)strength*(double)(totalPulses-pulse)
                            /(double)totalPulses);
                        break;
                    }
                }
            }
        }
        else if(accentLongBuildupBars(pattern)>0)
        {
            const auto cycleTicks=barTicks*accentLongBuildupBars(pattern);
            const auto local=(tick+clipStartTick)%cycleTicks;
            const auto totalPulses=cycleTicks/pulseTicks;
            const auto pickup=cycleTicks-
                earlyPickupOffset(beatTicks,compound);
            // One crescendo across the complete phrase, with a final
            // eighth-note culmination. Ignore non-pulse notes as ghosts.
            emphasized=nearGrid(local,pickup,cycleTicks,deviation);
            if(emphasized)requested=strength;
            else
            {
                for(std::int64_t pulse=0;pulse<totalPulses;++pulse)
                {
                    std::int64_t pulseDeviation=0;
                    if(nearGrid(local,pulse*pulseTicks,
                                cycleTicks,pulseDeviation))
                    {
                        emphasized=true;
                        deviation=pulseDeviation;
                        requested=(int)std::lround(
                            (double)strength*(double)(pulse+1)
                            /(double)(totalPulses+1));
                        break;
                    }
                }
            }
        }
        else if(accentBuildupBars(pattern)>0)
        {
            const auto cycleTicks=barTicks*accentBuildupBars(pattern);
            const auto local=(tick+clipStartTick)%cycleTicks;
            const auto lastBarStart=cycleTicks-barTicks;
            const auto pulseCount=barTicks/pulseTicks;
            // The last eighth is the strongest culmination. The preceding
            // pulses build gradually, never affecting MIDI note positions.
            const auto pickup=cycleTicks-
                earlyPickupOffset(beatTicks,compound);
            emphasized=nearGrid(local,pickup,cycleTicks,deviation);
            if(emphasized)requested=strength;
            else
            {
                for(std::int64_t pulse=0;pulse<pulseCount;++pulse)
                {
                    std::int64_t pulseDeviation=0;
                    if(nearGrid(local,lastBarStart+pulse*pulseTicks,
                                cycleTicks,pulseDeviation))
                    {
                        emphasized=true;
                        deviation=pulseDeviation;
                        requested=(int)std::lround(
                            (double)strength*(double)(pulse+1)
                            /(double)(pulseCount+1));
                        break;
                    }
                }
            }
        }
        else if(accentFullPhraseBars(pattern)>0)
        {
            const auto cycleTicks=barTicks*accentFullPhraseBars(pattern);
            const auto local=(tick+clipStartTick)%cycleTicks;
            const auto pickupTarget=cycleTicks-
                earlyPickupOffset(beatTicks,compound);
            emphasized=nearGrid(local,0,cycleTicks,deviation);
            if(emphasized)
                requested=strength;
            else
            {
                emphasized=nearGrid(local,pickupTarget,cycleTicks,deviation);
                if(emphasized)requested=(strength+1)/2;
            }
        }
        else if(accentPhraseBars(pattern)>0)
        {
            const auto cycleTicks=barTicks*accentPhraseBars(pattern);
            emphasized=nearGrid((tick+clipStartTick)%cycleTicks,
                                0,cycleTicks,deviation);
            if(emphasized)requested=strength;
        }
        else if(accentPickupBars(pattern)>0)
        {
            const auto cycleTicks=barTicks*accentPickupBars(pattern);
            const auto target=cycleTicks-(isEarlyPickup(pattern)
                ?earlyPickupOffset(beatTicks,compound):pulseTicks);
            emphasized=nearGrid((tick+clipStartTick)%cycleTicks,
                                target,cycleTicks,deviation);
            if(emphasized)requested=strength;
        }
        else if(pattern==AccentPattern::backbeat)
        {
            // Check only odd-indexed pulses, keeping the final downbeat
            // of the next bar separate from the final backbeat.
            const auto pulseCount=barTicks/pulseTicks;
            for(std::int64_t pulse=1;pulse<pulseCount;pulse+=2)
                if(nearGrid(inBar,pulse*pulseTicks,barTicks,deviation))
                {
                    emphasized=true;
                    break;
                }
            if(emphasized)requested=strength;
        }
        else
        {
            // Exact 0% setting reproduces the original beat classifier.
            if(nearGrid(inBar,0,barTicks,deviation))
            {
                emphasized=true;
                requested=strength;
            }
            else if(numerator>=4&&numerator%2==0
                    &&nearGrid(inBar,barTicks/2,barTicks,deviation))
            {
                emphasized=true;
                requested=(strength+1)/2;
            }
        }
        // Independent of accent strength: turning ACCENTS to zero still
        // allows ghost notes on the complementary rhythmic positions.
        if(!emphasized)requested=-ghostAmount;
        else if(progressiveAccents&&toleranceTicks>0&&deviation>0)
        {
            // Smoothly fade each recognized accent toward the adjacent
            // ghost-note level. At the tolerance boundary there is no
            // velocity discontinuity when the next note is off-grid.
            const double proximity=1.0
                -(double)deviation/(double)toleranceTicks;
            requested=(int)std::lround(-ghostAmount
                +(requested+ghostAmount)*progress(proximity,falloffCurve));
        }
        const auto& chord=chords.at(tick);
        const int delta=juce::jlimit(1-chord.low,127-chord.high,requested);
        result.push_back(juce::jlimit(1,127,oldValue+delta));
    }
    return result;
}

// Rhythmic landmarks for the velocity preview. Project time signature and
// the clip's Arrangement position anchor all positions. This helper is read
// only: it never changes MIDI starts, lengths, velocities or playback.
struct AccentGuideMark
{
    std::int64_t localTick=0;
    bool primary=true;
};
inline std::vector<AccentGuideMark> rhythmicAccentGuide(
    std::int64_t firstTick,std::int64_t lastTick,
    int numerator,int denominator,std::int64_t ticksPerQuarter,
    std::int64_t clipStartTick,AccentPattern pattern,int swingPercent)
{
    std::vector<AccentGuideMark> marks;
    if(firstTick<0||lastTick<=firstTick||clipStartTick<0
       ||numerator<1||numerator>32||denominator<1||denominator>32
       ||ticksPerQuarter<1||ticksPerQuarter>1000000
       ||lastTick>std::numeric_limits<std::int64_t>::max()-clipStartTick)
        return marks;
    const auto beatTicks=ticksPerQuarter*4/denominator;
    const auto barTicks=beatTicks*numerator;
    if(beatTicks<1||barTicks<1)return marks;
    const bool compound=denominator==8&&numerator>=6&&numerator%3==0;
    const auto pulseTicks=compound?beatTicks*3:beatTicks;
    const auto pulseCount=barTicks/pulseTicks;
    const auto firstAbsolute=firstTick+clipStartTick;
    const auto lastAbsolute=lastTick+clipStartTick;
    const auto firstBar=firstAbsolute/barTicks;
    const auto lastBar=lastAbsolute/barTicks;
    // Keep rendering bounded even if thousands of measures are selected.
    // A sampled bar remains tied to the original project grid.
    const auto barStep=juce::jmax<std::int64_t>(
        1,(lastBar-firstBar)/40+1);
    swingPercent=juce::jlimit(50,75,swingPercent);
    auto append=[&](std::int64_t absoluteTick,bool primary)
    {
        if(absoluteTick>=firstAbsolute&&absoluteTick<=lastAbsolute
           &&marks.size()<160)
            marks.push_back({absoluteTick-clipStartTick,primary});
    };
    const auto arcBars=accentArcBars(pattern);
    if(arcBars>0)
    {
        // Reuse the checked full-phrase grid and long-selection sampling.
        // Highlight the peak at the central pulse, not the cycle edges.
        const auto correspondingRise=arcBars==2?AccentPattern::longBuildup2
            :arcBars==4?AccentPattern::longBuildup4
            :AccentPattern::longBuildup8;
        marks=rhythmicAccentGuide(firstTick,lastTick,numerator,denominator,
            ticksPerQuarter,clipStartTick,correspondingRise,swingPercent);
        const auto cycleTicks=barTicks*arcBars;
        for(auto& mark:marks)
            mark.primary=(mark.localTick+clipStartTick)%cycleTicks
                ==cycleTicks/2;
        return marks;
    }
    const auto fadeBars=accentLongFadeBars(pattern);
    if(fadeBars>0)
    {
        // Reuse crescendo's exact grid, project offset and bounded
        // sampling. Only accent hierarchy is reversed for the preview.
        const auto correspondingRise=fadeBars==2?AccentPattern::longBuildup2
            :fadeBars==4?AccentPattern::longBuildup4
            :AccentPattern::longBuildup8;
        marks=rhythmicAccentGuide(firstTick,lastTick,numerator,denominator,
            ticksPerQuarter,clipStartTick,correspondingRise,swingPercent);
        const auto cycleTicks=barTicks*fadeBars;
        for(auto& mark:marks)
            mark.primary=(mark.localTick+clipStartTick)%cycleTicks==0;
        return marks;
    }
    const auto longBars=accentLongBuildupBars(pattern);
    if(longBars>0)
    {
        // Sparse, deterministic sampling of complete cycles is cheaper
        // than drawing every bar on a very long MIDI selection.
        // Secondary beat guides remain dim; last-eighth peaks are bright.
        // The selection may start halfway through an existing phrase:
        // draw visible beats from that phrase rather than skipping ahead
        // to the following phrase boundary.
        const auto firstAligned=firstBar-firstBar%longBars;
        const auto pulsesPerCycle=(barTicks*longBars)/pulseTicks;
        const auto cycleSampling=juce::jmax<std::int64_t>(
            1,((lastBar-firstBar+1)*pulsesPerCycle)
                /(120*longBars)+1);
        const auto step=longBars*cycleSampling;
        for(auto bar=firstAligned;bar<=lastBar&&marks.size()<160;)
        {
            const auto cycleStart=bar*barTicks;
            for(std::int64_t pulse=0;pulse<pulsesPerCycle
                &&marks.size()<160;++pulse)
                append(cycleStart+pulse*pulseTicks,false);
            append(cycleStart+barTicks*longBars-
                   earlyPickupOffset(beatTicks,compound),true);
            if(bar>lastBar-step)break;
            bar+=step;
        }
        std::stable_sort(marks.begin(),marks.end(),
            [](const AccentGuideMark& a,const AccentGuideMark& b)
            {
                return a.localTick<b.localTick;
            });
        return marks;
    }
    const auto buildupBars=accentBuildupBars(pattern);
    if(buildupBars>0)
    {
        // The graphic represents the exact engine positions: lighter
        // pulse lines rise toward the last (gold-strength) eighth.
        const auto firstAligned=firstBar+
            (buildupBars-1+buildupBars-firstBar%buildupBars)%buildupBars;
        const auto step=juce::jmax<std::int64_t>(
            buildupBars,((barStep+buildupBars-1)/buildupBars)*buildupBars);
        for(auto bar=firstAligned;bar<=lastBar&&marks.size()<160;)
        {
            const auto barStart=bar*barTicks;
            for(std::int64_t pulse=0;pulse<pulseCount&&marks.size()<160;++pulse)
                append(barStart+pulse*pulseTicks,false);
            append(barStart+barTicks-
                   earlyPickupOffset(beatTicks,compound),true);
            if(bar>lastBar-step)break;
            bar+=step;
        }
        std::stable_sort(marks.begin(),marks.end(),
            [](const AccentGuideMark& a,const AccentGuideMark& b)
            {
                return a.localTick<b.localTick;
            });
        return marks;
    }
    const auto fullBars=accentFullPhraseBars(pattern);
    if(fullBars>0)
    {
        // Compose the exact same two grids used by the MIDI engine.
        // Secondary pickup marks are dimmer than the phrase downbeats.
        const auto phrase=fullBars==2?AccentPattern::phrase2
            :fullBars==4?AccentPattern::phrase4:AccentPattern::phrase8;
        const auto pickup=fullBars==2?AccentPattern::earlyPickup2
            :fullBars==4?AccentPattern::earlyPickup4
            :AccentPattern::earlyPickup8;
        marks=rhythmicAccentGuide(firstTick,lastTick,numerator,denominator,
                                 ticksPerQuarter,clipStartTick,phrase,swingPercent);
        auto secondary=rhythmicAccentGuide(
            firstTick,lastTick,numerator,denominator,ticksPerQuarter,
            clipStartTick,pickup,swingPercent);
        for(auto& mark:secondary)mark.primary=false;
        marks.insert(marks.end(),secondary.begin(),secondary.end());
        std::stable_sort(marks.begin(),marks.end(),
            [](const AccentGuideMark& a,const AccentGuideMark& b)
            {
                return a.localTick<b.localTick;
            });
        if(marks.size()>160)marks.resize(160);
        return marks;
    }
    const auto phraseBars=accentPhraseBars(pattern);
    if(phraseBars>0)
    {
        // First eligible bar is aligned to global project zero. When a
        // selection spans many bars, sample in multiples of phrase length
        // to prevent long selections skipping all accented measures.
        const auto firstAligned=firstBar+
            (phraseBars-firstBar%phraseBars)%phraseBars;
        const auto step=juce::jmax<std::int64_t>(
            phraseBars,((barStep+phraseBars-1)/phraseBars)*phraseBars);
        for(auto bar=firstAligned;bar<=lastBar&&marks.size()<160;)
        {
            append(bar*barTicks,true);
            if(bar>lastBar-step)break;
            bar+=step;
        }
        return marks;
    }
    const auto pickupBars=accentPickupBars(pattern);
    if(pickupBars>0)
    {
        // Normal relance: last pulse in the group. Anticipated relance:
        // one eighth before the next phrase. Both use project-wide timing.
        const auto firstAligned=firstBar+
            (pickupBars-1+pickupBars-firstBar%pickupBars)%pickupBars;
        const auto step=juce::jmax<std::int64_t>(
            pickupBars,((barStep+pickupBars-1)/pickupBars)*pickupBars);
        for(auto bar=firstAligned;bar<=lastBar&&marks.size()<160;)
        {
            const auto offset=isEarlyPickup(pattern)
                ?earlyPickupOffset(beatTicks,compound):pulseTicks;
            append(bar*barTicks+(barTicks-offset),true);
            if(bar>lastBar-step)break;
            bar+=step;
        }
        return marks;
    }
    for(auto bar=firstBar;bar<=lastBar&&marks.size()<160;)
    {
        const auto barStart=bar*barTicks;
        if(pattern==AccentPattern::strongBeats)
        {
            append(barStart,true);
            if(numerator>=4&&numerator%2==0)
                append(barStart+barTicks/2,false);
        }
        else
        {
            for(std::int64_t pulse=0;pulse<pulseCount&&marks.size()<160;++pulse)
            {
                if(pattern==AccentPattern::backbeat)
                {
                    if(pulse%2==1)append(barStart+pulse*pulseTicks,true);
                }
                else if(pattern==AccentPattern::everyBeat)
                    append(barStart+pulse*pulseTicks,true);
                else
                {
                    const auto displacement=compound?beatTicks
                        :(pulseTicks*swingPercent+50)/100;
                    if(displacement>0)
                        append(barStart+pulse*pulseTicks+displacement,true);
                }
            }
        }
        if(bar>lastBar-barStep)break;
        bar+=barStep;
    }
    return marks;
}

// Standalone JUCE window content. No Piano Roll or instrument engine state is
// stored here. The callback is invoked only by explicit user confirmation.
class DialogContent final : public juce::Component
{
public:
    explicit DialogContent(int first, int last, Curve shape,
                           std::function<void(int, int, Curve, Mode, int, int,
                                              std::uint32_t, int, AccentPattern, int, int, int, bool, Curve)> apply,
                           std::vector<PreviewNote> selectedNotes = {},
                           int signatureNumerator = 4,
                           int signatureDenominator = 4,
                           std::int64_t quarterTicks = 960,
                           std::int64_t clipOffsetTick = 0)
        : onApply(std::move(apply)), samples(std::move(selectedNotes)),
          meterNumerator(signatureNumerator),
          meterDenominator(signatureDenominator),
          ticksPerQuarter(quarterTicks),clipStartTick(clipOffsetTick)
    {
        setOpaque(true);
        setSize(530, 846);
        std::stable_sort(samples.begin(),samples.end(),
            [](const PreviewNote& a,const PreviewNote& b){
                return a.startTick<b.startTick;
            });

        initialFirst=juce::jlimit(1,127,first);
        initialLast=juce::jlimit(1,127,last);
        initialCurve=shape;
        configureNumber(start, initialFirst, juce::String::fromUTF8("Vélocité de départ (1 à 127)"));
        configureNumber(end, initialLast, juce::String::fromUTF8("Vélocité d'arrivée (1 à 127)"));
        // Both controls must belong to the JUCE component tree: assigning
        // bounds alone does not display a TextEditor in a dialog.
        addAndMakeVisible(start);
        addAndMakeVisible(end);

        curve.addItem(juce::String::fromUTF8("Linéaire"), 1);
        curve.addItem(juce::String::fromUTF8("Exponentielle"), 2);
        curve.addItem(juce::String::fromUTF8("En S — lissage progressif"), 3);
        curve.setSelectedId((int)shape + 1, juce::dontSendNotification);
        curve.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff233747));
        curve.setColour(juce::ComboBox::textColourId, juce::Colour(0xfff2f7fc));
        curve.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff426278));
        curve.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xffffd28a));
        curve.onChange = [this] { repaint(); };
        addAndMakeVisible(curve);

        applicationMode.addItem(juce::String::fromUTF8(
            "Absolu — nouvelles vélocités"),1);
        applicationMode.addItem(juce::String::fromUTF8(
            "Relatif — préserver les nuances"),2);
        applicationMode.setSelectedId(1,juce::dontSendNotification);
        applicationMode.setTooltip(juce::String::fromUTF8(
            "Absolu remplace les vélocités. Relatif conserve les accents et écarts des accords."));
        applicationMode.setColour(juce::ComboBox::backgroundColourId,
                                  juce::Colour(0xff233747));
        applicationMode.setColour(juce::ComboBox::textColourId,
                                  juce::Colour(0xfff2f7fc));
        applicationMode.setColour(juce::ComboBox::outlineColourId,
                                  juce::Colour(0xff426278));
        applicationMode.setColour(juce::ComboBox::arrowColourId,
                                  juce::Colour(0xffffd28a));
        applicationMode.onChange=[this]{repaint();};
        addAndMakeVisible(applicationMode);

        intensity.setSliderStyle(juce::Slider::LinearHorizontal);
        intensity.setTextBoxStyle(juce::Slider::TextBoxRight,false,69,27);
        intensity.setRange(0.0,100.0,1.0);
        intensity.setValue(100.0,juce::dontSendNotification);
        intensity.setTextValueSuffix(" %");
        intensity.setTooltip(juce::String::fromUTF8(
            "0 % : courbe inactive, rythme et humanisation indépendants"));
        intensity.setColour(juce::Slider::backgroundColourId,juce::Colour(0xff27445a));
        intensity.setColour(juce::Slider::trackColourId,juce::Colour(0xff65c4d6));
        intensity.setColour(juce::Slider::thumbColourId,juce::Colour(0xffffcc73));
        intensity.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour(0xff233747));
        intensity.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xffffe6b5));
        intensity.setColour(juce::Slider::textBoxOutlineColourId,juce::Colour(0xff426278));
        intensity.onValueChange=[this]{repaint();};
        addAndMakeVisible(intensity);

        humanize.setSliderStyle(juce::Slider::LinearHorizontal);
        humanize.setTextBoxStyle(juce::Slider::TextBoxRight,false,48,27);
        humanize.setRange(0.0,20.0,1.0);
        humanize.setValue(0.0,juce::dontSendNotification);
        humanize.setTooltip(juce::String::fromUTF8(
            "Variation déterministe de ±0 à ±20 points MIDI par accord"));
        humanize.setColour(juce::Slider::backgroundColourId,juce::Colour(0xff27445a));
        humanize.setColour(juce::Slider::trackColourId,juce::Colour(0xff65c4d6));
        humanize.setColour(juce::Slider::thumbColourId,juce::Colour(0xffffcc73));
        humanize.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour(0xff233747));
        humanize.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xffffe6b5));
        humanize.setColour(juce::Slider::textBoxOutlineColourId,juce::Colour(0xff426278));
        humanize.onValueChange=[this]{repaint();};
        addAndMakeVisible(humanize);
        newVariation.setButtonText(juce::String::fromUTF8("VARIER"));
        newVariation.setTooltip(juce::String::fromUTF8(
            "Créer une autre variation, sans modifier les notes du clip"));
        newVariation.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff35516a));
        newVariation.setColour(juce::TextButton::textColourOffId,juce::Colour(0xffffd68d));
        newVariation.onClick=[this]{rerollVariation();};
        addAndMakeVisible(newVariation);

        accents.setSliderStyle(juce::Slider::LinearHorizontal);
        accents.setTextBoxStyle(juce::Slider::TextBoxRight,false,48,27);
        accents.setRange(0.0,20.0,1.0);
        accents.setValue(0.0,juce::dontSendNotification);
        accents.setTooltip(juce::String::fromUTF8(
            "Renforcer le premier temps (et le temps fort secondaire) de chaque mesure"));
        accents.setColour(juce::Slider::backgroundColourId,juce::Colour(0xff27445a));
        accents.setColour(juce::Slider::trackColourId,juce::Colour(0xff65c4d6));
        accents.setColour(juce::Slider::thumbColourId,juce::Colour(0xffffcc73));
        accents.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour(0xff233747));
        accents.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xffffe6b5));
        accents.setColour(juce::Slider::textBoxOutlineColourId,juce::Colour(0xff426278));
        accents.onValueChange=[this]{repaint();};
        addAndMakeVisible(accents);
        accentPattern.addItem(juce::String::fromUTF8("Temps forts"),1);
        accentPattern.addItem(juce::String::fromUTF8("Chaque pulsation"),2);
        accentPattern.addItem(juce::String::fromUTF8("Contretemps"),3);
        accentPattern.addItem(juce::String::fromUTF8("Backbeat (2 et 4)"),4);
        accentPattern.addSeparator();
        accentPattern.addItem(juce::String::fromUTF8("Phrasé — 2 mesures"),5);
        accentPattern.addItem(juce::String::fromUTF8("Phrasé — 4 mesures"),6);
        accentPattern.addItem(juce::String::fromUTF8("Phrasé — 8 mesures"),7);
        accentPattern.addSeparator();
        accentPattern.addItem(juce::String::fromUTF8("Relance — 2 mesures"),8);
        accentPattern.addItem(juce::String::fromUTF8("Relance — 4 mesures"),9);
        accentPattern.addItem(juce::String::fromUTF8("Relance — 8 mesures"),10);
        accentPattern.addSeparator();
        accentPattern.addItem(juce::String::fromUTF8("Relance croche — 2 mesures"),11);
        accentPattern.addItem(juce::String::fromUTF8("Relance croche — 4 mesures"),12);
        accentPattern.addItem(juce::String::fromUTF8("Relance croche — 8 mesures"),13);
        accentPattern.addSeparator();
        accentPattern.addItem(juce::String::fromUTF8("Phrasé complet — 2 mesures"),14);
        accentPattern.addItem(juce::String::fromUTF8("Phrasé complet — 4 mesures"),15);
        accentPattern.addItem(juce::String::fromUTF8("Phrasé complet — 8 mesures"),16);
        accentPattern.addSeparator();
        accentPattern.addItem(juce::String::fromUTF8("Montée rythmique — 2 mesures"),17);
        accentPattern.addItem(juce::String::fromUTF8("Montée rythmique — 4 mesures"),18);
        accentPattern.addItem(juce::String::fromUTF8("Montée rythmique — 8 mesures"),19);
        accentPattern.addSeparator();
        accentPattern.addItem(juce::String::fromUTF8("Montée longue — 2 mesures"),20);
        accentPattern.addItem(juce::String::fromUTF8("Montée longue — 4 mesures"),21);
        accentPattern.addItem(juce::String::fromUTF8("Montée longue — 8 mesures"),22);
        accentPattern.addSeparator();
        accentPattern.addItem(juce::String::fromUTF8("Descente longue — 2 mesures"),23);
        accentPattern.addItem(juce::String::fromUTF8("Descente longue — 4 mesures"),24);
        accentPattern.addItem(juce::String::fromUTF8("Descente longue — 8 mesures"),25);
        accentPattern.addSeparator();
        accentPattern.addItem(juce::String::fromUTF8("Arc dynamique — 2 mesures"),26);
        accentPattern.addItem(juce::String::fromUTF8("Arc dynamique — 4 mesures"),27);
        accentPattern.addItem(juce::String::fromUTF8("Arc dynamique — 8 mesures"),28);
        accentPattern.setSelectedId(1,juce::dontSendNotification);
        accentPattern.setTooltip(juce::String::fromUTF8("Choisir la grille d'accentuation"));
        accentPattern.setColour(juce::ComboBox::backgroundColourId,juce::Colour(0xff233747));
        accentPattern.setColour(juce::ComboBox::textColourId,juce::Colour(0xfff2f7fc));
        accentPattern.setColour(juce::ComboBox::outlineColourId,juce::Colour(0xff426278));
        accentPattern.setColour(juce::ComboBox::arrowColourId,juce::Colour(0xffffd28a));
        accentPattern.onChange=[this]{
            swing.setEnabled(selectedAccentPattern()==AccentPattern::offbeat
                &&!(meterDenominator==8&&meterNumerator>=6
                     &&meterNumerator%3==0));
            repaint();
        };
        addAndMakeVisible(accentPattern);

        ghostNotes.setSliderStyle(juce::Slider::LinearHorizontal);
        ghostNotes.setTextBoxStyle(juce::Slider::TextBoxRight,false,48,27);
        ghostNotes.setRange(0.0,20.0,1.0);
        ghostNotes.setValue(0.0,juce::dontSendNotification);
        ghostNotes.setTooltip(juce::String::fromUTF8(
            "Réduire les notes hors accents de 0 à 20 points MIDI"));
        ghostNotes.setColour(juce::Slider::backgroundColourId,juce::Colour(0xff27445a));
        ghostNotes.setColour(juce::Slider::trackColourId,juce::Colour(0xff65c4d6));
        ghostNotes.setColour(juce::Slider::thumbColourId,juce::Colour(0xffffcc73));
        ghostNotes.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour(0xff233747));
        ghostNotes.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xffffe6b5));
        ghostNotes.setColour(juce::Slider::textBoxOutlineColourId,juce::Colour(0xff426278));
        ghostNotes.onValueChange=[this]{repaint();};
        addAndMakeVisible(ghostNotes);

        tolerance.setSliderStyle(juce::Slider::LinearHorizontal);
        tolerance.setTextBoxStyle(juce::Slider::TextBoxRight,false,59,27);
        tolerance.setRange(0.0,25.0,1.0);
        tolerance.setValue(0.0,juce::dontSendNotification);
        tolerance.setTextValueSuffix(" %");
        tolerance.setTooltip(juce::String::fromUTF8(
            "Reconnaître les temps joués légèrement en avance ou en retard sans quantifier"));
        tolerance.setColour(juce::Slider::backgroundColourId,juce::Colour(0xff27445a));
        tolerance.setColour(juce::Slider::trackColourId,juce::Colour(0xff65c4d6));
        tolerance.setColour(juce::Slider::thumbColourId,juce::Colour(0xffffcc73));
        tolerance.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour(0xff233747));
        tolerance.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xffffe6b5));
        tolerance.setColour(juce::Slider::textBoxOutlineColourId,juce::Colour(0xff426278));
        tolerance.onValueChange=[this]{
            progressive.setEnabled(getTolerance()>0);
            accentFalloff.setEnabled(getTolerance()>0&&getProgressiveAccents());
            repaint();
        };
        addAndMakeVisible(tolerance);

        swing.setSliderStyle(juce::Slider::LinearHorizontal);
        swing.setTextBoxStyle(juce::Slider::TextBoxRight,false,59,27);
        swing.setRange(50.0,75.0,1.0);
        swing.setValue(50.0,juce::dontSendNotification);
        swing.setTextValueSuffix(" %");
        swing.setTooltip(juce::String::fromUTF8(
            "50 % : contretemps binaires ; 67 % : ternaire ; 75 % : swing accentué. "
            "Ne déplace aucune note MIDI. Actif pour Contretemps hors mesure composée."));
        swing.setColour(juce::Slider::backgroundColourId,juce::Colour(0xff27445a));
        swing.setColour(juce::Slider::trackColourId,juce::Colour(0xff65c4d6));
        swing.setColour(juce::Slider::thumbColourId,juce::Colour(0xffffcc73));
        swing.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour(0xff233747));
        swing.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xffffe6b5));
        swing.setColour(juce::Slider::textBoxOutlineColourId,juce::Colour(0xff426278));
        swing.onValueChange=[this]{repaint();};
        addAndMakeVisible(swing);
        swing.setEnabled(false);

        progressive.setButtonText(juce::String::fromUTF8("ACTIVER"));
        progressive.setTooltip(juce::String::fromUTF8(
            "Fondre progressivement les accents vers les notes fantômes "
            "en fonction de leur écart à la grille, sans déplacer les notes"));
        progressive.setColour(juce::ToggleButton::textColourId,
                              juce::Colour(0xffffdf9b));
        progressive.setColour(juce::ToggleButton::tickColourId,
                              juce::Colour(0xff7fe0eb));
        progressive.setToggleState(false,juce::dontSendNotification);
        progressive.onClick=[this]{
            accentFalloff.setEnabled(getTolerance()>0&&getProgressiveAccents());
            repaint();
        };
        progressive.setEnabled(false);
        addAndMakeVisible(progressive);

        accentFalloff.addItem(juce::String::fromUTF8("Linéaire"),1);
        accentFalloff.addItem(juce::String::fromUTF8("Exponentielle"),2);
        accentFalloff.addItem(juce::String::fromUTF8("Courbe en S"),3);
        accentFalloff.setSelectedId(1,juce::dontSendNotification);
        accentFalloff.setTooltip(juce::String::fromUTF8(
            "Choisir la forme du dégradé rythmique entre accent et note fantôme"));
        accentFalloff.setColour(juce::ComboBox::backgroundColourId,juce::Colour(0xff233747));
        accentFalloff.setColour(juce::ComboBox::textColourId,juce::Colour(0xfff2f7fc));
        accentFalloff.setColour(juce::ComboBox::outlineColourId,juce::Colour(0xff426278));
        accentFalloff.setColour(juce::ComboBox::arrowColourId,juce::Colour(0xffffd28a));
        accentFalloff.onChange=[this]{repaint();};
        accentFalloff.setEnabled(false);
        addAndMakeVisible(accentFalloff);

        resetButton.setButtonText(juce::String::fromUTF8("RÉINITIALISER"));
        resetButton.setTooltip(juce::String::fromUTF8(
            "Retrouver les vélocités d'origine et la courbe initiale"));
        resetButton.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff253949));
        resetButton.setColour(juce::TextButton::textColourOffId,juce::Colour(0xffbcd5e8));
        resetButton.onClick=[this]{restoreInitialSettings();};
        addAndMakeVisible(resetButton);
        invertButton.setButtonText(juce::String::fromUTF8("INVERSER"));
        invertButton.setTooltip(juce::String::fromUTF8(
            "Échanger le départ et l'arrivée sans modifier les notes MIDI"));
        invertButton.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff3f5470));
        invertButton.setColour(juce::TextButton::textColourOffId,juce::Colour(0xffffe0a2));
        invertButton.onClick=[this]{invertEndpoints();};
        addAndMakeVisible(invertButton);
        applyButton.setButtonText(juce::String::fromUTF8("APPLIQUER"));
        applyButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff25697e));
        applyButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffffffff));
        cancelButton.setButtonText(juce::String::fromUTF8("ANNULER"));
        cancelButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff263746));
        cancelButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe9f3fc));
        applyButton.onClick = [this] { confirm(); };
        cancelButton.onClick = [this]
        {
            if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
                window->exitModalState(0);
        };
        addAndMakeVisible(cancelButton);
        addAndMakeVisible(applyButton);
        updateValidation();
    }

    // Endpoints are the only mouse-active part of the preview. Moving a
    // handle updates the same numeric editors as manual typing and never
    // invokes the MIDI callback until APPLIQUER is pressed.
    static juce::Rectangle<float> previewPlot() noexcept
    {
        return {58.0f,633.0f,440.0f,100.0f};
    }
    juce::Point<float> endpointPosition(int which) const
    {
        int value=which==0?64:100;
        parseValue(which==0?start:end,value);
        const auto rect=previewPlot();
        return {which==0?rect.getX():rect.getRight(),
            rect.getBottom()-((float)(value-1)/126.0f)*rect.getHeight()};
    }
    int endpointAt(juce::Point<float> p) const
    {
        // A compact 15 px circular hit target does not block the plot.
        if(!error.isEmpty())return -1;
        for(int which=0;which<2;++which)
            if(p.getDistanceFrom(endpointPosition(which))<=15.0f)return which;
        return -1;
    }
    bool beginEndpointDragAt(juce::Point<float> p)
    {
        draggedEndpoint=endpointAt(p);
        if(draggedEndpoint<0)return false;
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        return true;
    }
    bool dragEndpointTo(float y)
    {
        if(draggedEndpoint<0)return false;
        const auto rect=previewPlot();
        const int value=juce::jlimit(1,127,(int)std::lround(
            1.0+(rect.getBottom()-y)*126.0/rect.getHeight()));
        auto& editor=draggedEndpoint==0?start:end;
        if(editor.getText()!=juce::String(value))
            editor.setText(juce::String(value),juce::sendNotificationSync);
        updateValidation();
        return true;
    }
    void endEndpointDrag()
    {
        draggedEndpoint=-1;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        beginEndpointDragAt(e.position);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if(draggedEndpoint>=0)dragEndpointTo(e.position.y);
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if(draggedEndpoint>=0)endEndpointDrag();
    }
    void mouseMove(const juce::MouseEvent& e) override
    {
        setMouseCursor(endpointAt(e.position)>=0
            ?juce::MouseCursor::UpDownResizeCursor
            :juce::MouseCursor::NormalCursor);
    }
    void mouseExit(const juce::MouseEvent&) override
    {
        if(draggedEndpoint<0)
            setMouseCursor(juce::MouseCursor::NormalCursor);
    }
    // These actions change only the UI preview. No engine callbacks occur
    // until the user explicitly presses APPLIQUER.
    void invertEndpoints()
    {
        int first=0,last=0;
        if(!parseValue(start,first)||!parseValue(end,last))return;
        start.setText(juce::String(last),juce::sendNotificationSync);
        end.setText(juce::String(first),juce::sendNotificationSync);
        updateValidation();
    }
    void restoreInitialSettings()
    {
        start.setText(juce::String(initialFirst),juce::sendNotificationSync);
        end.setText(juce::String(initialLast),juce::sendNotificationSync);
        curve.setSelectedId((int)initialCurve+1,juce::sendNotificationSync);
        applicationMode.setSelectedId(1,juce::sendNotificationSync);
        intensity.setValue(100.0,juce::sendNotificationSync);
        humanize.setValue(0.0,juce::sendNotificationSync);
        accents.setValue(0.0,juce::sendNotificationSync);
        accentPattern.setSelectedId(1,juce::sendNotificationSync);
        ghostNotes.setValue(0.0,juce::sendNotificationSync);
        tolerance.setValue(0.0,juce::sendNotificationSync);
        swing.setValue(50.0,juce::sendNotificationSync);
        progressive.setToggleState(false,juce::sendNotificationSync);
        accentFalloff.setSelectedId(1,juce::sendNotificationSync);
        accentFalloff.setEnabled(false);
        variationSeed=initialVariationSeed;
        updateValidation();
    }
    void resized() override
    {
        start.setBounds(284, 78, 214, 33);
        end.setBounds(284, 124, 214, 33);
        curve.setBounds(284, 170, 214, 34);
        applicationMode.setBounds(284, 216, 214, 34);
        intensity.setBounds(278, 259, 220, 34);
        humanize.setBounds(276, 305, 141, 34);
        newVariation.setBounds(423, 307, 75, 30);
        accents.setBounds(278, 352, 220, 34);
        accentPattern.setBounds(284, 395, 214, 32);
        ghostNotes.setBounds(278, 439, 220, 34);
        tolerance.setBounds(278, 483, 220, 34);
        swing.setBounds(278, 527, 220, 34);
        progressive.setBounds(284, 568, 88, 31);
        accentFalloff.setBounds(375, 568, 123, 31);
        resetButton.setBounds(26, 801, 117, 33);
        invertButton.setBounds(151, 801, 117, 33);
        cancelButton.setBounds(277, 801, 104, 33);
        applyButton.setBounds(389, 801, 109, 33);
    }

    void paint(juce::Graphics& g) override
    {
        const auto canvas = juce::Colour(0xff101c2b);
        g.fillAll(canvas);
        g.setColour(juce::Colour(0xff3b647e));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(9.0f), 11.0f, 1.2f);

        g.setColour(juce::Colour(0xffffd28a));
        g.setFont(juce::Font(18.0f, juce::Font::bold));
        g.drawText(juce::String::fromUTF8("COURBE DE VÉLOCITÉ MIDI"),
                   26, 17, 470, 27, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff9eb8cd));
        g.setFont(juce::Font(11.5f));
        g.drawText(juce::String::fromUTF8("Arrangement  /  notes sélectionnées"),
                   26, 45, 470, 20, juce::Justification::centredLeft);

        g.setColour(juce::Colour(0xffe6f1fa));
        g.setFont(juce::Font(13.0f));
        g.drawText(juce::String::fromUTF8("Vélocité de départ"), 26, 78, 241, 33,
                   juce::Justification::centredLeft);
        g.drawText(juce::String::fromUTF8("Vélocité d'arrivée"), 26, 124, 241, 33,
                   juce::Justification::centredLeft);
        g.drawText(juce::String::fromUTF8("Courbe"), 26, 170, 241, 33,
                   juce::Justification::centredLeft);
        g.drawText(juce::String::fromUTF8("Application"), 26, 216, 241, 33,
                   juce::Justification::centredLeft);
        g.drawText(juce::String::fromUTF8("Intensité"), 26, 259, 241, 34,
                   juce::Justification::centredLeft);
        g.drawText(juce::String::fromUTF8("Humanisation (±)"), 26, 305, 241, 34,
                   juce::Justification::centredLeft);
        g.drawText(juce::String::fromUTF8("Accents rythmiques"), 26, 352, 241, 34,
                   juce::Justification::centredLeft);
        g.drawText(juce::String::fromUTF8("Profil d’accents"), 26, 395, 241, 32,
                   juce::Justification::centredLeft);
        g.drawText(juce::String::fromUTF8("Notes fantômes (−)"), 26, 439, 241, 34,
                   juce::Justification::centredLeft);
        g.drawText(juce::String::fromUTF8("Tolérance rythmique"), 26, 483, 241, 34,
                   juce::Justification::centredLeft);
        g.drawText(juce::String::fromUTF8("Swing des contretemps"), 26, 527, 241, 34,
                   juce::Justification::centredLeft);
        g.drawText(juce::String::fromUTF8("Accents progressifs"), 26, 568, 241, 31,
                   juce::Justification::centredLeft);

        g.setColour(juce::Colour(0xffc1d7e9));
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(juce::String::fromUTF8("APERÇU  ·  ")
                       +juce::String((int)samples.size())
                       +juce::String::fromUTF8(" NOTE(S)"),
                   26, 607, 200, 20, juce::Justification::centredLeft);
        g.setFont(juce::Font(10.0f));
        g.setColour(juce::Colour(0xff8fb0c7));
        g.fillEllipse(276.0f,613.0f,7.0f,7.0f);
        g.drawText(juce::String::fromUTF8("Avant"),286,606,56,21,
                   juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xffffd28a));
        g.fillEllipse(359.0f,613.0f,7.0f,7.0f);
        g.drawText(juce::String::fromUTF8("Après"),369,606,58,21,
                   juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff7fe0eb));
        g.drawLine(438.0f,616.0f,450.0f,616.0f,1.5f);
        g.drawText(juce::String::fromUTF8("Temps"),454,606,43,21,
                   juce::Justification::centredLeft);

        const auto plot=previewPlot();
        g.setColour(juce::Colour(0xff172b3e));
        g.fillRoundedRectangle(plot.expanded(3.0f), 5.0f);
        g.setColour(juce::Colour(0xff4c687d));
        for (int i=0;i<=4;++i)
        {
            const float y=plot.getY()+plot.getHeight()*i/4.0f;
            g.drawHorizontalLine((int)std::lround(y),plot.getX(),plot.getRight());
        }
        for (int i=0;i<=4;++i)
        {
            const float x=plot.getX()+plot.getWidth()*i/4.0f;
            g.drawVerticalLine((int)std::lround(x),plot.getY(),plot.getBottom());
        }
        // Overlay real rhythmic target positions. Pale windows display the
        // recognition tolerance and fine cyan lines indicate exact accents.
        // Markers live under the velocity curve and note handles.
        if(samples.size()>1&&(getAccents()>0||getGhostNotes()>0))
        {
            const auto guide=getRhythmicGuide();
            const auto firstTick=samples.front().startTick;
            const auto lastTick=samples.back().startTick;
            const auto beatTicks=ticksPerQuarter*4/meterDenominator;
            const auto pulseTicks=(meterDenominator==8&&meterNumerator>=6
                                   &&meterNumerator%3==0)
                ?beatTicks*3:beatTicks;
            const float halfWidth=lastTick>firstTick
                ?(float)((double)(pulseTicks*getTolerance()/100)
                          /(double)(lastTick-firstTick)*plot.getWidth())
                :0.0f;
            for(const auto& mark:guide)
            {
                const double position=musicalPosition(
                    mark.localTick,firstTick,lastTick);
                const float x=plot.getX()+(float)position*plot.getWidth();
                if(halfWidth>0.0f)
                {
                    const auto window=juce::Rectangle<float>(
                        x-halfWidth,plot.getY(),2.0f*halfWidth,plot.getHeight())
                        .getIntersection(plot);
                    g.setColour(juce::Colour(0xff7fe0eb).withAlpha(
                        mark.primary?0.085f:0.055f));
                    g.fillRect(window);
                }
                g.setColour(juce::Colour(0xff7fe0eb).withAlpha(
                    mark.primary?0.52f:0.30f));
                g.drawLine(x,plot.getY(),x,plot.getBottom(),
                           mark.primary?1.2f:1.0f);
            }
        }
        g.setColour(juce::Colour(0xff9cb5c9));
        g.setFont(juce::Font(10.0f));
        g.drawText("127", 22, 627, 32, 17, juce::Justification::centredRight);
        g.drawText("1", 22, 718, 32, 17, juce::Justification::centredRight);

        int first=64,last=100;
        if (!parseValue(start,first)) first=64;
        if (!parseValue(end,last)) last=100;
        const auto shape=selectedCurve();
        const auto mode=selectedMode();
        const auto curved=transformVelocities(
            samples,first,last,shape,mode,getIntensity());
        const auto humanized=humanizeVelocities(
            samples,curved,getHumanization(),variationSeed);
        const auto transformed=accentVelocities(samples,humanized,
            getAccents(),meterNumerator,meterDenominator,
            ticksPerQuarter,clipStartTick,selectedAccentPattern(),
            getGhostNotes(),getTolerance(),getSwing(),getProgressiveAccents(),
            getAccentFalloff());
        juce::Path line;
        for (int i=0;i<=160;++i)
        {
            const double t=i/160.0;
            const float x=plot.getX()+(float)t*plot.getWidth();
            const float y=plot.getBottom()-
                ((velocityAt(first,last,t,shape)-1)/126.0f)*plot.getHeight();
            if (i==0)line.startNewSubPath(x,y);
            else line.lineTo(x,y);
        }
        g.setColour(juce::Colour(0xff09111d));
        g.strokePath(line,juce::PathStrokeType(5.0f));
        g.setColour(juce::Colour(0xff7fe0eb));
        g.strokePath(line,juce::PathStrokeType(2.6f));
        // Real note positions and their current / prospective velocities.
        // Cap on-screen markers to 120 for very dense selections; the
        // underlying note selection and Apply continue to use every note.
        if(!samples.empty())
        {
            const auto firstTick=samples.front().startTick;
            const auto lastTick=samples.back().startTick;
            const size_t stride=juce::jmax<size_t>(1,(samples.size()+119)/120);
            for(size_t i=0;i<samples.size();i+=stride)
            {
                const double t=musicalPosition(samples[i].startTick,
                                               firstTick,lastTick);
                const float x=plot.getX()+(float)t*plot.getWidth();
                const float oldY=plot.getBottom()-
                    ((juce::jlimit(1,127,samples[i].velocity)-1)/126.0f)*plot.getHeight();
                const int target=transformed[i];
                const float newY=plot.getBottom()-
                    ((target-1)/126.0f)*plot.getHeight();
                g.setColour(juce::Colour(0xff8fb0c7).withAlpha(.72f));
                g.drawLine(x-2.0f,plot.getBottom(),x-2.0f,oldY,1.2f);
                g.fillEllipse(x-5.0f,oldY-3.0f,6.0f,6.0f);
                g.setColour(juce::Colour(0xffffd28a).withAlpha(.91f));
                g.drawLine(x+2.0f,plot.getBottom(),x+2.0f,newY,1.5f);
                g.fillEllipse(x-1.0f,newY-3.0f,6.0f,6.0f);
            }
        }

        // Large gold endpoint handles stay above the note markers so they
        // remain draggable even when a note lies exactly at the boundary.
        for(int which=0;which<2;++which)
        {
            const auto p=endpointPosition(which);
            g.setColour(juce::Colour(0xff091827).withAlpha(.95f));
            g.fillEllipse(p.x-9.0f,p.y-9.0f,18.0f,18.0f);
            g.setColour(juce::Colour(0xfff4f9ff));
            g.fillEllipse(p.x-7.0f,p.y-7.0f,14.0f,14.0f);
            g.setColour(juce::Colour(0xffffc95e));
            g.fillEllipse(p.x-5.2f,p.y-5.2f,10.4f,10.4f);
        }

        if (!error.isEmpty())
        {
            g.setColour(juce::Colour(0xffffa9a1));
            g.setFont(juce::Font(11.0f));
            g.drawFittedText(error,25,745,472,18,juce::Justification::centredLeft,1);
        }
        else
        {
            g.setColour(juce::Colour(0xffa9bfd1));
            g.setFont(juce::Font(10.5f));
            g.drawText(getIntensity()==0
                ?juce::String::fromUTF8("0 % : courbe inactive ; rythme indépendant.")
                :mode==Mode::relative
                    ?juce::String::fromUTF8("Relatif : nuances et accents conservés.")
                    :juce::String::fromUTF8("Absolu : dosage progressif de la courbe."),
                26,745,470,17,juce::Justification::centredLeft);
        }
    }

private:
    static void configureNumber(juce::TextEditor& box,int initial,
                                const juce::String& tooltip)
    {
        box.setInputRestrictions(3,"0123456789");
        box.setText(juce::String(juce::jlimit(1,127,initial)),
                    juce::dontSendNotification);
        box.setJustification(juce::Justification::centred);
        box.setColour(juce::TextEditor::backgroundColourId,juce::Colour(0xff233747));
        box.setColour(juce::TextEditor::outlineColourId,juce::Colour(0xff426278));
        box.setColour(juce::TextEditor::textColourId,juce::Colour(0xffffe6b5));
        box.setTooltip(tooltip);
    }
    static bool parseValue(const juce::TextEditor& box,int& value)
    {
        const auto raw=box.getText().trim();
        if(raw.isEmpty()||raw.length()>3)return false;
        for(int i=0;i<raw.length();++i)
            if(!juce::CharacterFunctions::isDigit(raw[i]))return false;
        const int candidate=raw.getIntValue();
        if(candidate<1||candidate>127)return false;
        value=candidate;
        return true;
    }
    Curve selectedCurve() const noexcept
    {
        switch(curve.getSelectedId())
        {
            case 2:return Curve::exponential;
            case 3:return Curve::sCurve;
            default:return Curve::linear;
        }
    }
    Mode selectedMode() const noexcept
    {
        return applicationMode.getSelectedId()==2
            ?Mode::relative:Mode::absolute;
    }
    AccentPattern selectedAccentPattern() const noexcept
    {
        switch(accentPattern.getSelectedId())
        {
            case 2:return AccentPattern::everyBeat;
            case 3:return AccentPattern::offbeat;
            case 4:return AccentPattern::backbeat;
            case 5:return AccentPattern::phrase2;
            case 6:return AccentPattern::phrase4;
            case 7:return AccentPattern::phrase8;
            case 8:return AccentPattern::pickup2;
            case 9:return AccentPattern::pickup4;
            case 10:return AccentPattern::pickup8;
            case 11:return AccentPattern::earlyPickup2;
            case 12:return AccentPattern::earlyPickup4;
            case 13:return AccentPattern::earlyPickup8;
            case 14:return AccentPattern::fullPhrase2;
            case 15:return AccentPattern::fullPhrase4;
            case 16:return AccentPattern::fullPhrase8;
            case 17:return AccentPattern::buildup2;
            case 18:return AccentPattern::buildup4;
            case 19:return AccentPattern::buildup8;
            case 20:return AccentPattern::longBuildup2;
            case 21:return AccentPattern::longBuildup4;
            case 22:return AccentPattern::longBuildup8;
            case 23:return AccentPattern::longFade2;
            case 24:return AccentPattern::longFade4;
            case 25:return AccentPattern::longFade8;
            case 26:return AccentPattern::arc2;
            case 27:return AccentPattern::arc4;
            case 28:return AccentPattern::arc8;
            default:return AccentPattern::strongBeats;
        }
    }
    void updateValidation()
    {
        int a=0,b=0;
        error=(parseValue(start,a)&&parseValue(end,b))
            ? juce::String{}
            : juce::String::fromUTF8("Les deux valeurs doivent être comprises entre 1 et 127.");
        applyButton.setEnabled(error.isEmpty());
        invertButton.setEnabled(error.isEmpty());
        repaint();
    }
    void confirm()
    {
        int first=0,last=0;
        if(!parseValue(start,first)||!parseValue(end,last))
        {
            updateValidation();
            return;
        }
        if(onApply)onApply(first,last,selectedCurve(),selectedMode(),
                           getIntensity(),getHumanization(),variationSeed,
                           getAccents(),selectedAccentPattern(),getGhostNotes(),
                           getTolerance(),getSwing(),getProgressiveAccents(),
                           getAccentFalloff());
        if(auto* window=findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState(1);
    }

    juce::TextEditor start,end;
    juce::ComboBox curve,applicationMode,accentPattern;
    juce::Slider intensity,humanize,accents,ghostNotes,tolerance,swing;
    juce::ToggleButton progressive;
    juce::ComboBox accentFalloff;
    juce::TextButton newVariation;
    int draggedEndpoint=-1;
    std::vector<PreviewNote> samples;
    juce::TextButton applyButton,cancelButton,resetButton,invertButton;
    int initialFirst=64,initialLast=100;
    Curve initialCurve=Curve::linear;
    static constexpr std::uint32_t initialVariationSeed=0x61b3e729U;
    std::uint32_t variationSeed=initialVariationSeed;
    int meterNumerator=4,meterDenominator=4;
    std::int64_t ticksPerQuarter=960,clipStartTick=0;
    juce::String error;
    std::function<void(int,int,Curve,Mode,int,int,std::uint32_t,int,AccentPattern,int,int,int,bool,Curve)> onApply;
public:
    // Read-only access for automated validation; never edits any MIDI note.
    size_t getPreviewNoteCount() const noexcept { return samples.size(); }
    int getStartValue() const
    {
        int value=0;
        return parseValue(start,value)?value:0;
    }
    int getEndValue() const
    {
        int value=0;
        return parseValue(end,value)?value:0;
    }
    int getCurrentPreviewVelocity(size_t index) const
    {
        return getPreviewResultVelocity(index,getStartValue(),getEndValue(),
            selectedCurve(),selectedMode(),getIntensity(),getHumanization(),
            variationSeed,getAccents(),selectedAccentPattern(),getGhostNotes(),
            getTolerance(),getSwing(),getProgressiveAccents(),
            getAccentFalloff());
    }
    void setAccentPattern(AccentPattern pattern)
    {
        accentPattern.setSelectedId(juce::jlimit(1,28,(int)pattern+1),
                                    juce::sendNotificationSync);
    }
    AccentPattern getAccentPattern() const noexcept {return selectedAccentPattern();}
    void setProgressiveAccents(bool enabled)
    {
        progressive.setToggleState(enabled,juce::sendNotificationSync);
        accentFalloff.setEnabled(enabled&&getTolerance()>0);
        repaint();
    }
    void setAccentFalloff(Curve shape)
    {
        accentFalloff.setSelectedId(juce::jlimit(1,3,(int)shape+1),
                                   juce::sendNotificationSync);
    }
    Curve getAccentFalloff() const noexcept
    {
        switch(accentFalloff.getSelectedId())
        {
            case 2:return Curve::exponential;
            case 3:return Curve::sCurve;
            default:return Curve::linear;
        }
    }
    bool isAccentFalloffEnabled() const noexcept
    {
        return accentFalloff.isEnabled();
    }
    bool getProgressiveAccents() const noexcept
    {
        return progressive.getToggleState();
    }
    bool areProgressiveAccentsAvailable() const noexcept
    {
        return progressive.isEnabled();
    }
    void setSwing(int percent)
    {
        swing.setValue(juce::jlimit(50,75,percent),juce::sendNotificationSync);
    }
    int getSwing() const noexcept
    {
        return juce::jlimit(50,75,(int)std::lround(swing.getValue()));
    }
    bool isSwingEnabled() const noexcept {return swing.isEnabled();}
    void setTolerance(int percent)
    {
        tolerance.setValue(juce::jlimit(0,25,percent),juce::sendNotificationSync);
    }
    int getTolerance() const noexcept
    {
        return juce::jlimit(0,25,(int)std::lround(tolerance.getValue()));
    }
    void setGhostNotes(int amount)
    {
        ghostNotes.setValue(juce::jlimit(0,20,amount),juce::sendNotificationSync);
    }
    int getGhostNotes() const noexcept
    {
        return juce::jlimit(0,20,(int)std::lround(ghostNotes.getValue()));
    }
    void setAccents(int strength)
    {
        accents.setValue(juce::jlimit(0,20,strength),juce::sendNotificationSync);
    }
    int getAccents() const noexcept
    {
        return juce::jlimit(0,20,(int)std::lround(accents.getValue()));
    }
    void setHumanization(int amount)
    {
        humanize.setValue(juce::jlimit(0,20,amount),juce::sendNotificationSync);
    }
    int getHumanization() const noexcept
    {
        return juce::jlimit(0,20,(int)std::lround(humanize.getValue()));
    }
    std::uint32_t getVariationSeed() const noexcept {return variationSeed;}
    void rerollVariation()
    {
        variationSeed+=0x9e3779b9U;
        repaint();
    }
    void setIntensity(int percent)
    {
        intensity.setValue(juce::jlimit(0,100,percent),juce::sendNotificationSync);
    }
    int getIntensity() const noexcept
    {
        return juce::jlimit(0,100,(int)std::lround(intensity.getValue()));
    }
    void setMode(Mode mode)
    {
        applicationMode.setSelectedId((int)mode+1,juce::sendNotificationSync);
    }
    Mode getMode() const noexcept { return selectedMode(); }
    bool hasVisibleActionButtons() const noexcept
    {
        return resetButton.isVisible()&&invertButton.isVisible()
            &&applicationMode.isVisible()
            &&resetButton.getWidth()>0&&invertButton.getWidth()>0
            &&applicationMode.getWidth()>0
            &&intensity.isVisible()&&intensity.getWidth()>0
            &&humanize.isVisible()&&humanize.getWidth()>0
            &&newVariation.isVisible()&&newVariation.getWidth()>0
            &&accents.isVisible()&&accents.getWidth()>0
            &&accentPattern.isVisible()&&accentPattern.getWidth()>0
            &&ghostNotes.isVisible()&&ghostNotes.getWidth()>0
            &&tolerance.isVisible()&&tolerance.getWidth()>0
            &&swing.isVisible()&&swing.getWidth()>0
            &&progressive.isVisible()&&progressive.getWidth()>0
            &&accentFalloff.isVisible()&&accentFalloff.getWidth()>0;
    }
    std::vector<AccentGuideMark> getRhythmicGuide() const
    {
        if(samples.size()<2||meterDenominator<1
           ||(getAccents()==0&&getGhostNotes()==0))
            return {};
        return rhythmicAccentGuide(
            samples.front().startTick,samples.back().startTick,
            meterNumerator,meterDenominator,ticksPerQuarter,clipStartTick,
            selectedAccentPattern(),getSwing());
    }
    int getPreviewOriginalVelocity(size_t index) const
    {
        return index<samples.size()?samples[index].velocity:0;
    }
    int getPreviewResultVelocity(size_t index,int first,int last,Curve shape,
                                 Mode mode=Mode::absolute,int percent=100,
                                 int humanizeAmount=0,
                                 std::uint32_t seed=0x61b3e729U,
                                 int accentStrength=0,
                                 AccentPattern pattern=AccentPattern::strongBeats,
                                 int ghostAmount=0,
                                 int tolerancePercent=0,
                                 int swingPercent=50,
                                 bool progressiveAccents=false,
                                 Curve falloffCurve=Curve::linear) const
    {
        if(index>=samples.size())return 0;
        const auto transformed=transformVelocities(
            samples,first,last,shape,mode,percent);
        const auto variations=humanizeVelocities(
            samples,transformed,humanizeAmount,seed);
        const auto accented=accentVelocities(samples,variations,
            accentStrength,meterNumerator,meterDenominator,
            ticksPerQuarter,clipStartTick,pattern,ghostAmount,tolerancePercent,
            swingPercent,progressiveAccents,falloffCurve);
        return accented[index];
    }
    bool hasVisibleNumberFields() const noexcept
    {
        return start.isVisible()&&end.isVisible()
            &&start.getWidth()>0&&end.getWidth()>0;
    }
    // Attach callbacks only once all child components have been created.
    void bindInputs()
    {
        start.onTextChange=[this]{updateValidation();};
        end.onTextChange=[this]{updateValidation();};
        start.onReturnKey=[this]{confirm();};
        end.onReturnKey=[this]{confirm();};
    }
};
} // namespace LibertyInlineVelocity
