#pragma once
#include "LibertyImmersiveAnalysis.h"
#include <limits>
namespace LibertyReference
{
constexpr int bands=6,points=60;
inline constexpr std::array<double,7> edges{{20.,120.,350.,1000.,3000.,8000.,20000.}};
inline double dbPower(double power) noexcept {return power>0.?10.*std::log10(power):-std::numeric_limits<double>::infinity();}
inline double matchGain(double own,double other,double ownPeak,double otherPeak) noexcept
{
    if(!std::isfinite(own)||!std::isfinite(other)||!std::isfinite(ownPeak)||!std::isfinite(otherPeak))return 0.;
    // Attenuation only, shared loudness ceiling with 6 dB monitor headroom.
    const double common=std::min({own,other,own-6.-ownPeak,other-6.-otherPeak});
    return std::clamp(common-own,-120.,0.);
}
struct Band {double power=0.,mid=0.,side=0.,maximum=0.;std::uint64_t count=0;};
struct Profile
{
    std::array<Band,bands> band{};
    std::array<double,points> spectral{};
    std::uint64_t windows=0;
    double rate=0.;
    double width(int i) const noexcept
    {
        const auto& b=band[(size_t)i];return b.mid+b.side>1e-20?100.*b.side/(b.mid+b.side):-1.;
    }
    double dynamics(int i) const noexcept
    {
        const auto& b=band[(size_t)i];return b.power>0.&&b.count?dbPower(b.maximum/(b.power/double(b.count))):-1.;
    }
    double spectralDb(int i) const noexcept{return windows?dbPower(spectral[(size_t)i]/double(windows)):-std::numeric_limits<double>::infinity();}
};
class Analyzer
{
    static constexpr auto size=LibertyImmersive::fftSize;
    using Buffer=LibertyImmersive::ComplexBuffer;
    std::array<LibertyImmersive::Frame,size> samples{};
    std::array<double,size> hann{};
    std::array<float,size/2+1> leftReal{},leftImag{};
    Buffer work{};
    Profile result{};
    size_t filled=0;
    double norm=1.;
    void block() noexcept
    {
        using namespace LibertyImmersive;
        for(size_t i=0;i<size;++i)work[i]={float(samples[i].l*hann[i]),0.f};
        fft(work);for(size_t i=0;i<leftReal.size();++i){leftReal[i]=work[i].real();leftImag[i]=work[i].imag();}
        for(size_t i=0;i<size;++i)work[i]={float(samples[i].r*hann[i]),0.f};
        fft(work);
        std::array<double,bands> powers{},mids{},sides{};
        for(size_t i=1;i<leftReal.size();++i)
        {
            const double hz=result.rate*i/double(size);if(hz<20.||hz>20000.)continue;
            const double lr=leftReal[i],li=leftImag[i],rr=work[i].real(),ri=work[i].imag();
            const double scale=(i==size/2?1.:2.)/norm;
            const double mid=((lr+rr)*(lr+rr)+(li+ri)*(li+ri))*.25*scale;
            const double side=((lr-rr)*(lr-rr)+(li-ri)*(li-ri))*.25*scale;
            const double power=mid+side;
            const auto logBin=(size_t)std::clamp(int(std::log(hz/20.)/std::log(1000.)*points),0,points-1);
            result.spectral[logBin]+=power;
            for(int b=0;b<bands;++b)if(hz>=edges[(size_t)b]&&(hz<edges[(size_t)b+1]||b==bands-1))
            {powers[(size_t)b]+=power;mids[(size_t)b]+=mid;sides[(size_t)b]+=side;break;}
        }
        for(int i=0;i<bands;++i)
        {
            if(edges[(size_t)i+1]>result.rate*.5)continue;
            auto& b=result.band[(size_t)i];b.power+=powers[(size_t)i];b.mid+=mids[(size_t)i];b.side+=sides[(size_t)i];b.maximum=std::max(b.maximum,powers[(size_t)i]);++b.count;
        }
        ++result.windows;filled=0;
    }
public:
    explicit Analyzer(double rate){result.rate=rate;double sum=0.;for(size_t i=0;i<size;++i){hann[i]=.5-.5*std::cos(2.*LibertyImmersive::pi*i/double(size-1));sum+=hann[i]*hann[i];}norm=sum*size;}
    void push(const LibertyImmersive::Frame* frames,size_t n) noexcept
    {for(size_t i=0;i<n;++i){samples[filled++]=frames[i];if(filled==size)block();}}
    // Fractional final block excluded from window statistics, not from loudness/peak.
    Profile get() const noexcept{return result;}
};
}
