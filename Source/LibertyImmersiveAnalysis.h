#pragma once
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstddef>

namespace LibertyImmersive
{
struct Frame { float l=0.f,r=0.f; };
constexpr std::size_t fftSize=4096;
constexpr float pi=3.14159265358979323846f;
// One real-time producer, one UI consumer. Never wait, allocate or overwrite
// unread slots. A closed analyzer performs only one relaxed atomic load.
class Tap
{
    static constexpr std::uint32_t capacity=32768;
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,"SPSC counters must be lock-free");
    static_assert(std::atomic<bool>::is_always_lock_free,"Enable flag must be lock-free");
    std::array<Frame,capacity> frames{};
    alignas(64) std::atomic<std::uint32_t> head{0};
    alignas(64) std::atomic<std::uint32_t> tail{0};
    std::atomic<bool> enabled{false};
    std::atomic<std::uint32_t> losses{0};
public:
    std::uint32_t dropped() const noexcept { return losses.load(std::memory_order_acquire); }
    void setEnabled(bool b) noexcept { enabled.store(b,std::memory_order_relaxed); }
    void capture(float* const* output,int channels,int samples) noexcept
    {
        if(!enabled.load(std::memory_order_relaxed)||samples<=0||channels<=0||!output)return;
        const auto* left=output[0];
        const auto* right=channels>1?output[1]:left;
        auto h=head.load(std::memory_order_relaxed);
        const auto t=tail.load(std::memory_order_acquire);
        const auto n=std::min(static_cast<std::uint32_t>(samples),capacity-(h-t));
        for(std::uint32_t i=0;i<n;++i)
        {
            const float l=left?left[i]:0.f,r=right?right[i]:0.f;
            frames[(h+i)&(capacity-1)]={std::isfinite(l)?l:0.f,std::isfinite(r)?r:0.f};
        }
        head.store(h+n,std::memory_order_release);
        if(n<static_cast<std::uint32_t>(samples)) losses.fetch_add(static_cast<std::uint32_t>(samples)-n,std::memory_order_release);
    }
    std::size_t read(Frame* dst,std::size_t limit) noexcept
    {
        auto t=tail.load(std::memory_order_relaxed);
        const auto h=head.load(std::memory_order_acquire);
        const auto n=std::min<std::size_t>(h-t,limit);
        for(std::size_t i=0;i<n;++i)dst[i]=frames[(t+i)&(capacity-1)];
        tail.store(t+static_cast<std::uint32_t>(n),std::memory_order_release);
        return n;
    }
    void discard() noexcept { tail.store(head.load(std::memory_order_acquire),std::memory_order_release); }
};
inline float db(float amplitude) noexcept
{
    return amplitude>0.f?std::max(-100.f,20.f*std::log10(amplitude)):-100.f;
}
struct Metrics { float peak=0.f,rms=0.f,correlation=0.f,crest=0.f; bool correlated=false; };
inline Metrics measure(const Frame* p,std::size_t n) noexcept
{
    Metrics m; double ll=0,rr=0,lr=0;
    for(std::size_t i=0;i<n;++i)
    {
        const double l=p[i].l,r=p[i].r;
        ll+=l*l;rr+=r*r;lr+=l*r;
        m.peak=std::max(m.peak,std::max(std::abs(p[i].l),std::abs(p[i].r)));
    }
    if(n)m.rms=static_cast<float>(std::sqrt((ll+rr)/(2.0*n)));
    m.correlated=ll>1e-12&&rr>1e-12;
    if(m.correlated)m.correlation=std::clamp(static_cast<float>(lr/std::sqrt(ll*rr)),-1.f,1.f);
    if(m.rms>0.f)m.crest=db(m.peak)-db(m.rms);
    return m;
}
// Hann-windowed radix-2 FFT, coherent-gain calibrated; UI thread only.
using ComplexBuffer=std::array<std::complex<float>,fftSize>;
using Spectrum=std::array<float,fftSize/2+1>;
inline void fft(ComplexBuffer& a) noexcept
{
    for(std::size_t i=1,j=0;i<fftSize;++i)
    {
        std::size_t bit=fftSize>>1;
        for(;j&bit;bit>>=1)j^=bit;
        j^=bit;if(i<j)std::swap(a[i],a[j]);
    }
    for(std::size_t len=2;len<=fftSize;len<<=1)
    {
        const float angle=-2.f*pi/static_cast<float>(len);
        const std::complex<float> step(std::cos(angle),std::sin(angle));
        for(std::size_t i=0;i<fftSize;i+=len)
        {
            std::complex<float> w(1.f,0.f);
            for(std::size_t j=0;j<len/2;++j)
            {
                const auto u=a[i+j],v=a[i+j+len/2]*w;
                a[i+j]=u+v;a[i+j+len/2]=u-v;w*=step;
            }
        }
    }
}
enum class SpectrumMode { stereoMax, left, right, mid, side };
inline float analysisSample(Frame s,SpectrumMode mode,int channel) noexcept
{
    switch(mode){case SpectrumMode::left:return s.l;case SpectrumMode::right:return s.r;
    case SpectrumMode::mid:return (s.l+s.r)*.5f;case SpectrumMode::side:return (s.l-s.r)*.5f;
    default:return channel?s.r:s.l;}
}
inline std::size_t scopeSampleCount(double rate,double milliseconds) noexcept
{
    if(!std::isfinite(rate)||!std::isfinite(milliseconds)||rate<=0||milliseconds<=0)return 1;
    return static_cast<std::size_t>(std::clamp(std::round(rate*milliseconds*.001),1.0,static_cast<double>(fftSize)));
}
inline void spectrum(const std::array<Frame,fftSize>& frames,ComplexBuffer& work,Spectrum& out,SpectrumMode mode=SpectrumMode::stereoMax) noexcept
{
    out.fill(0.f);float gain=0.f;
    for(std::size_t i=0;i<fftSize;++i)gain+=.5f-.5f*std::cos(2.f*pi*static_cast<float>(i)/static_cast<float>(fftSize-1));
    for(int channel=0;channel<(mode==SpectrumMode::stereoMax?2:1);++channel)
    {
        for(std::size_t i=0;i<fftSize;++i)
        {
            const float window=.5f-.5f*std::cos(2.f*pi*static_cast<float>(i)/static_cast<float>(fftSize-1));
            work[i]={window*analysisSample(frames[i],mode,channel),0.f};
        }
        fft(work);
        for(std::size_t i=0;i<out.size();++i)
            out[i]=std::max(out[i],std::abs(work[i])*(i==0||i==fftSize/2?1.f:2.f)/gain);
    }
}
}
