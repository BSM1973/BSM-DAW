#pragma once
#include "LibertyImmersiveAnalysis.h"
#include <limits>
namespace LibertyMastering
{
constexpr double unavailable=-std::numeric_limits<double>::infinity();
inline double loudness(double energy) noexcept {return energy>0.?-.691+10.*std::log10(energy):unavailable;}
inline double amplitudeDb(double amplitude) noexcept {return amplitude>0.?20.*std::log10(amplitude):unavailable;}
struct Biquad
{
    double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
    double process(double x) noexcept {const auto y=b0*x+z1;z1=b1*x-a1*y+z2;z2=b2*x-a2*y;return y;}
};
// Bilinear transform of the two BS.1770 K-weighting stages at the device rate.
inline Biquad shelf(double rate) noexcept
{
    const double k=std::tan(3.141592653589793*1681.974450955533/rate),q=.7071752369554196;
    const double vh=std::pow(10.,3.999843853973347/20.),vb=std::pow(vh,.4996667741545416);
    const double d=1.+k/q+k*k;
    return {(vh+vb*k/q+k*k)/d,2.*(k*k-vh)/d,(vh-vb*k/q+k*k)/d,2.*(k*k-1.)/d,(1.-k/q+k*k)/d,0,0};
}
inline Biquad highpass(double rate) noexcept
{
    const double k=std::tan(3.141592653589793*38.13547087602444/rate),q=.5003270373238773,d=1.+k/q+k*k;
    return {1.,-2.,1.,2.*(k*k-1.)/d,(1.-k/q+k*k)/d,0,0};
}
// Fixed storage, 0.01 LU bins. Retains actual energy sums for integrated loudness;
// only gate boundaries and percentiles are quantised. No duration-dependent memory.
class Distribution
{
    static constexpr int count=11001;
    struct Bin {double sum=0.;std::uint64_t n=0;};
    std::array<Bin,count> bins{};
    static int index(double lu) noexcept {return static_cast<int>(std::clamp(std::round((lu+70.)*100.),0.,double(count-1)));}
    static double centre(int i) noexcept {return -70.+i*.01;}
public:
    void clear() noexcept {bins.fill({});}
    void add(double energy) noexcept {const double lu=loudness(energy);if(!std::isfinite(lu)||lu< -70.)return;auto& b=bins[(size_t)index(lu)];b.sum+=energy;++b.n;}
    double mean(double threshold) const noexcept
    {
        double sum=0.;std::uint64_t n=0;
        for(int i=0;i<count;++i)if(centre(i)>=threshold-.005){const auto& b=bins[(size_t)i];sum+=b.sum;n+=b.n;}
        return n?sum/double(n):0.;
    }
    double integrated() const noexcept
    {
        const auto threshold=std::max(-70.,loudness(mean(-70.))-10.);
        return loudness(mean(threshold));
    }
    double range() const noexcept
    {
        const double threshold=std::max(-70.,loudness(mean(-70.))-20.);
        std::uint64_t n=0;for(int i=0;i<count;++i)if(centre(i)>=threshold-.005)n+=bins[(size_t)i].n;
        if(n<2)return unavailable;
        const auto lo=static_cast<std::uint64_t>(std::llround(double(n-1)*.10));
        const auto hi=static_cast<std::uint64_t>(std::llround(double(n-1)*.95));
        std::uint64_t cursor=0;double low=unavailable;
        for(int i=0;i<count;++i){if(centre(i)<threshold-.005)continue;cursor+=bins[(size_t)i].n;if(!std::isfinite(low)&&cursor>lo)low=centre(i);if(cursor>hi)return centre(i)-low;}
        return unavailable;
    }
};
// Four-phase band-limited interpolator, on the UI consumer only.
// This is an estimated intersample peak, not a certified compliance meter.
class TruePeak
{
    static constexpr int taps=32;
    std::array<std::array<double,taps>,3> coeff{};
    std::array<std::array<double,taps>,2> delay{};
    int cursor=0;
    double maximum=0.;
public:
    TruePeak()
    {
        constexpr double p=3.14159265358979323846;
        for(int phase=0;phase<3;++phase){double sum=0.;for(int i=0;i<taps;++i){const double x=i-15.-(phase+1)*.25;const double w=.42-.5*std::cos(2*p*i/(taps-1))+.08*std::cos(4*p*i/(taps-1));const double c=w*(std::abs(x)<1e-12?1.:std::sin(p*x)/(p*x));coeff[(size_t)phase][(size_t)i]=c;sum+=c;}for(auto& c:coeff[(size_t)phase])c/=sum;}
    }
    void clear() noexcept {maximum=0.;cursor=0;for(auto& d:delay)d.fill(0.);}
    void push(LibertyImmersive::Frame s) noexcept
    {
        delay[0][(size_t)cursor]=s.l;delay[1][(size_t)cursor]=s.r;
        maximum=std::max({maximum,std::abs(double(s.l)),std::abs(double(s.r))});
        for(const auto& d:delay)for(const auto& phase:coeff){double y=0.;for(int i=0;i<taps;++i)y+=phase[(size_t)i]*d[(size_t)((cursor-i+taps)%taps)];maximum=std::max(maximum,std::abs(y));}
        cursor=(cursor+1)%taps;
    }
    double dbtp() const noexcept {return amplitudeDb(maximum);}
};
struct Reading {double momentary=unavailable,shortTerm=unavailable,integrated=unavailable,lra=unavailable,truePeak=unavailable;};
class Meter
{
    std::array<Biquad,2> shelving{},cut{};
    std::array<double,30> blocks{};
    std::size_t cursor=0,valid=0;
    int blockSamples=0,filled=0;
    double energy=0.,rate=0.;
    Distribution integratedBlocks,shortBlocks;
    TruePeak peak;
    Reading reading{};
    std::array<double,600> history{};
    std::size_t histCursor=0,histCount=0;
    std::uint64_t samples=0,updates=0;
    double average(std::size_t n) const noexcept
    {
        double sum=0.;for(std::size_t i=0;i<n;++i)sum+=blocks[(cursor+30-1-i)%30];
        return sum/double(n);
    }
    void completeBlock() noexcept
    {
        blocks[cursor]=energy/filled;cursor=(cursor+1)%30;valid=std::min<std::size_t>(30,valid+1);energy=0.;filled=0;
        if(valid>=4){const auto e=average(4);reading.momentary=loudness(e);integratedBlocks.add(e);}
        if(valid>=30){const auto e=average(30);reading.shortTerm=loudness(e);shortBlocks.add(e);}
        history[histCursor]=reading.momentary;histCursor=(histCursor+1)%history.size();histCount=std::min(history.size(),histCount+1);
        ++updates;
        // Distribution scans are bounded and run ten times per second at most.
        reading.integrated=integratedBlocks.integrated();reading.lra=shortBlocks.range();
    }
public:
    void configure(double sr) noexcept
    {
        rate=std::isfinite(sr)&&sr>=8000.&&sr<=384000.?sr:0.;
        blockSamples=rate>0.?static_cast<int>(std::round(rate*.1)):0;
        resetLoudness();resetPeak();resetHistory();
    }
    void resetLoudness() noexcept
    {
        for(auto& b:shelving)b=rate>0.?shelf(rate):Biquad{};
        for(auto& b:cut)b=rate>0.?highpass(rate):Biquad{};
        cursor=valid=0;filled=0;energy=0.;samples=updates=0;blocks.fill(0.);
        integratedBlocks.clear();shortBlocks.clear();
        reading.momentary=reading.shortTerm=reading.integrated=reading.lra=unavailable;
    }
    void resetPeak() noexcept {peak.clear();reading.truePeak=unavailable;}
    void resetHistory() noexcept {history.fill(unavailable);histCursor=histCount=0;}
    void push(const LibertyImmersive::Frame* data,std::size_t n) noexcept
    {
        if(blockSamples<=0)return;
        for(std::size_t i=0;i<n;++i)
        {
            const auto s=data[i];peak.push(s);
            const double l=cut[0].process(shelving[0].process(s.l)),r=cut[1].process(shelving[1].process(s.r));
            energy+=l*l+r*r;++filled;++samples;
            if(filled==blockSamples)completeBlock();
        }
        reading.truePeak=peak.dbtp();
    }
    Reading get() const noexcept {return reading;}
    double duration() const noexcept {return rate>0.?double(samples)/rate:0.;}
    std::size_t historyCount() const noexcept{return histCount;}
    double historyAt(std::size_t i) const noexcept{return i<histCount?history[(histCursor+history.size()-histCount+i)%history.size()]:unavailable;}
    static constexpr std::size_t historyCapacity=600;
};
}
