#pragma once

#include <algorithm>
#include <cmath>

// Crossfade level laws shared by the real-time audio engine and Arrangement
// preview. Curves are calculated from the normalized overlap position.
// 0: linear, 1: equal power, 2: smooth S (complementary levels).
namespace LibertyAudioCrossfade
{
inline constexpr int customFirst=10, customLast=30, fineFirst=31, maxCurve=231;
inline bool validCurve(int c) noexcept {return c>=0&&c<=maxCurve;}
inline double advancedShape(int curve,double p) noexcept
{
    if(p<=0.0)return 0.0;
    if(p>=1.0)return 1.0;
    switch(curve){
        case 3:return std::log1p(9.0*p)/std::log(10.0);
        case 4:return std::expm1(3.0*p)/std::expm1(3.0);
        case 5:return .5-.5*std::cos(3.14159265358979323846*p);
        case 6:return .5*p+.5*p*p*(3.0-2.0*p);
        case 7:return p*p*p*(10.0+p*(-15.0+6.0*p));
        case 8:return p*p*p;
        case 9:return 1.0-(1.0-p)*(1.0-p)*(1.0-p);
        default:
            if(curve>=customFirst&&curve<=customLast){
                const double bias=(curve-20)*.075;
                // Monotonic rational response, bounded away from zero denominator.
                return p*(1.0+bias)/(1.0+bias*(2.0*p-1.0));
            }
            if(curve>=fineFirst&&curve<=maxCurve){
                const double bias=(curve-131)*.0075;
                return p*(1.0+bias)/(1.0+bias*(2.0*p-1.0));
            }
            return p;
    }
}
// Normalized gain for independent clip fades. The existing linear law is
// preserved as the default; additional laws can be selected per clip later.
inline double manualFadeLevel(int curve, double progress, bool incoming) noexcept
{
    const double p = std::clamp(progress, 0.0, 1.0);
    if (curve >= 3 && validCurve(curve)){
        const double shaped=advancedShape(curve,p);
        return incoming?shaped:1.0-shaped;
    }
    if (curve == 1)
        return incoming ? p * p : (1.0 - p) * (1.0 - p);
    if (curve == 2)
    {
        const double smooth = p * p * (3.0 - 2.0 * p);
        return incoming ? smooth : (1.0 - smooth);
    }
    return incoming ? p : (1.0 - p);
}
inline double level(int curve, double progress, bool incoming) noexcept
{
    const double p = std::clamp(progress, 0.0, 1.0);
    if (curve >= 3 && validCurve(curve)){
        const double shaped=advancedShape(curve,p);
        return incoming?shaped:1.0-shaped;
    }
    if (curve == 1)
        return std::sqrt(incoming ? p : (1.0 - p));
    if (curve == 2)
    {
        const double smooth = p * p * (3.0 - 2.0 * p);
        return incoming ? smooth : 1.0 - smooth;
    }
    return incoming ? p : (1.0 - p);
}
// Optional constant sum-of-squares gain for independent curve pairs.
inline double studioLevel(int curve,int peer,double progress,bool incoming) noexcept
{
    const double g=level(curve,progress,incoming);
    if(peer<0)return g;
    const double other=level(peer,progress,!incoming);
    const double norm=std::sqrt(g*g+other*other);
    return norm>1.e-12?g/norm:0.0;
}
// Diagnostic only: worst adjacent gain change over a bounded transition.
inline double studioGainStep(int incoming,int outgoing,bool energy,int samples) noexcept
{
    if(samples<2)return 1.;
    double peak=0.,lastIn=0.,lastOut=1.;
    const int count=std::min(samples,4096);
    for(int i=1;i<=count;++i){
        const double p=double(i)/count;
        const double in=studioLevel(incoming,energy?outgoing:-1,p,true);
        const double out=studioLevel(outgoing,energy?incoming:-1,p,false);
        peak=std::max(peak,std::max(std::abs(in-lastIn),std::abs(out-lastOut)));
        lastIn=in;lastOut=out;
    }
    return peak;
}
// Diagnostic measures for actual sampled audio, not a click classifier.
struct SignalMetrics {
    double peak=0.,rms=0.,maxStep=0.;
    int zeroCrossings=0;
};
inline SignalMetrics measureSignal(const float* data,int count) noexcept
{
    SignalMetrics m;
    if(!data||count<=0)return m;
    double energy=0.;
    for(int i=0;i<count;++i){
        const double x=data[i];
        if(!std::isfinite(x)){m.peak=m.rms=m.maxStep=INFINITY;return m;}
        m.peak=std::max(m.peak,std::abs(x));energy+=x*x;
        if(i>0){
            m.maxStep=std::max(m.maxStep,std::abs(x-data[i-1]));
            if((data[i-1]<0&&x>=0)||(data[i-1]>0&&x<=0))++m.zeroCrossings;
        }
    }
    m.rms=std::sqrt(energy/count);return m;
}
inline double signalCorrelation(const float* a,const float* b,int count) noexcept
{
    if(!a||!b||count<2)return 0.;
    double aa=0.,bb=0.,ab=0.,sa=0.,sb=0.;
    for(int i=0;i<count;++i){const double x=a[i],y=b[i];aa+=x*x;bb+=y*y;ab+=x*y;sa+=x;sb+=y;}
    aa-=sa*sa/count;bb-=sb*sb/count;ab-=sa*sb/count;
    if(aa<=1.e-20||bb<=1.e-20)return 0.;
    return std::clamp(ab/std::sqrt(aa*bb),-1.,1.);
}
// Diagnostic only: do not apply correlation-based gain automatically to
// existing projects. Negative correlation can make inverse gain unstable.
inline double rmsDb(double rms) noexcept
{
    return 20.*std::log10(std::max(1.e-9,rms));
}
inline int nearestZeroCrossing(const float* data,int count,int center,int radius) noexcept
{
    if(!data||count<2||center<0||center>=count||radius<0)return -1;
    int best=-1;
    for(int i=std::max(1,center-radius);i<=std::min(count-1,center+radius);++i){
        const float a=data[i-1],b=data[i];
        if((a<0&&b>=0)||(a>0&&b<=0)||b==0.f)
            if(best<0||std::abs(i-center)<std::abs(best-center))best=i;
    }
    return best;
}
// Audition-only matching: attenuation only, with one common peak ceiling.
inline double auditionMatchGain(double rms,double target,double peak) noexcept
{
    if(!std::isfinite(rms)||!std::isfinite(target)||!std::isfinite(peak)||rms<=1.e-9||target<0.||peak<0.)return 1.;
    return std::min({1.,target/rms,peak>0.? .95/peak:1.});
}
// Predicted midpoint power with correlation, for diagnostic advice only.
// Do not boost anti-phase material to compensate for cancellation.
inline double predictedPower(double in,double out,double rho) noexcept
{
    return std::max(0.,in*in+out*out+2.*std::clamp(rho,-1.,1.)*in*out);
}
// Stereo alignment seeks a low-amplitude joint crossing. Never infer a
// crossing from a mono sum, which could hide opposite-polarity channels.
inline int nearestStereoZero(const float* l,const float* r,int count,int center,int radius) noexcept
{
    if(!l||!r||count<2||center<0||center>=count||radius<0)return -1;
    int best=-1;double score=1.e30;
    for(int i=std::max(1,center-radius);i<=std::min(count-1,center+radius);++i){
        const auto crossing=[](float a,float b){return (a<=0&&b>=0)||(a>=0&&b<=0);};
        if(!crossing(l[i-1],l[i])||!crossing(r[i-1],r[i]))continue;
        const double v=std::max(std::abs(double(l[i])),std::abs(double(r[i])));
        if(v>.05)continue;
        const double cost=v+.01*std::abs(i-center)/std::max(1,radius);
        if(cost<score){score=cost;best=i;}
    }
    return best;
}
inline bool validPreset(int in,int out,double center,double duration) noexcept
{
    return validCurve(in)&&validCurve(out)&&std::isfinite(center)&&center>=0.&&center<=1.
        &&std::isfinite(duration)&&duration>=0.&&duration<=86400.;
}
// Positive correlation can raise the level of an equal-power fade.
// Attenuation only: never boost anti-phase cancellation. Audition opt-in.
inline double correlationAttenuation(double in,double out,double rho) noexcept
{
    if(!std::isfinite(rho)||!std::isfinite(in)||!std::isfinite(out))return 1.;
    const double base=in*in+out*out;
    if(base<=1.e-12||rho<=0.)return 1.;
    return std::sqrt(base/std::max(base,predictedPower(in,out,rho)));
}
}
