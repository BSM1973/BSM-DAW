#pragma once
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
namespace LibertyCpu {
using Clock=std::chrono::steady_clock;
struct Snapshot {
    std::uint64_t blocks{},elapsedNs{},budgetNs{},peakBasisPoints{},overruns{};
    double averagePercent() const noexcept {return budgetNs?100.0*double(elapsedNs)/double(budgetNs):0.0;}
    double peakPercent() const noexcept {return double(peakBasisPoints)/100.0;}
};
inline std::uint64_t budgetFor(double rate,int frames) noexcept {
    if(!std::isfinite(rate)||rate<=0||frames<=0)return 0;
    const double ns=1.e9*double(frames)/rate;
    return ns>=1.0&&ns<double((std::numeric_limits<std::uint64_t>::max)())?static_cast<std::uint64_t>(ns):0;
}
class Meter {
    // Single callback writer; approximate atomic snapshots; reset only when quiescent.
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,"64-bit realtime atomics required");
    std::atomic<std::uint64_t> blocks{0},elapsed{0},budget{0},peak{0},overruns{0};
public:
    void add(std::uint64_t ns,std::uint64_t limit) noexcept {
        if(!limit)return;
        elapsed.fetch_add(ns,std::memory_order_relaxed);budget.fetch_add(limit,std::memory_order_relaxed);
        const double scaled=10000.0*double(ns)/double(limit);
        const auto max=(std::numeric_limits<std::uint64_t>::max)();
        const auto bp=scaled>=double(max)?max:static_cast<std::uint64_t>(scaled);
        if(bp>peak.load(std::memory_order_relaxed))peak.store(bp,std::memory_order_relaxed);
        if(ns>limit)overruns.fetch_add(1,std::memory_order_relaxed);
        blocks.fetch_add(1,std::memory_order_release);
    }
    Snapshot snapshot() const noexcept {
        return {blocks.load(std::memory_order_acquire),elapsed.load(std::memory_order_relaxed),budget.load(std::memory_order_relaxed),peak.load(std::memory_order_relaxed),overruns.load(std::memory_order_relaxed)};
    }
    void reset() noexcept {blocks.store(0);elapsed.store(0);budget.store(0);peak.store(0);overruns.store(0);}
};
class Scope {
    Meter* meter;std::uint64_t budget;Clock::time_point start;
public:
    Scope(Meter* target,std::uint64_t limit) noexcept:meter(limit?target:nullptr),budget(limit),start(meter?Clock::now():Clock::time_point{}){}
    ~Scope() noexcept {
        if(meter){const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();meter->add(ns>0?static_cast<std::uint64_t>(ns):0,budget);}
    }
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
};
}
