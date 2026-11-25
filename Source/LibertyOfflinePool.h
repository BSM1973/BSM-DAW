#pragma once
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace LibertyMulticore {
// Conservative offline admission heuristic from phase 1.4 benchmarks.
// Not a calibrated CPU topology or realtime scheduling guarantee.
inline unsigned recommendedWorkers(unsigned logical,int tracks,int frames,unsigned limit=8) noexcept
{
    if(logical<2||tracks<16||frames<128||limit==0)return 0;
    if(static_cast<std::int64_t>(tracks)*frames<8192)return 0;
    const unsigned byWork=static_cast<unsigned>(tracks/16);
    return (std::min)({logical-1,byWork,limit,8u});
}
struct Cooldown {
    unsigned remaining=0;
    bool admit() noexcept {
        if(!remaining)return true;
        --remaining;return false;
    }
    void observe(std::uint64_t elapsed,std::uint64_t budget) noexcept {
        if(budget&&elapsed>budget)remaining=32;
    }
};
// Offline executor only. Live audio never waits on this barrier.
class OfflinePool {
    using Task=void(*)(void*,int) noexcept;
    std::vector<std::thread> workers;
    std::mutex mutex;
    std::condition_variable wake,done;
    std::atomic<int> next{0},workerJobs{0};
    int count=0,remaining=0,chunk=1;
    unsigned long long generation=0;
    bool stopping=false;
    Task task=nullptr;
    void* context=nullptr;
    void drain(bool worker) noexcept {
        int local=0;
        for(;;){
            const int begin=next.fetch_add(chunk,std::memory_order_relaxed);
            if(begin>=count)break;
            const int end=(std::min)(count,begin+chunk);
            for(int i=begin;i<end;++i)task(context,i);
            local+=end-begin;
        }
        if(worker&&local>0)workerJobs.fetch_add(local,std::memory_order_relaxed);
    }
    void workerLoop() {
        unsigned long long seen=0;
        std::unique_lock<std::mutex> lock(mutex);
        for(;;){
            wake.wait(lock,[&]{return stopping||generation!=seen;});
            if(stopping)return;
            seen=generation;
            lock.unlock();drain(true);lock.lock();
            if(--remaining==0)done.notify_one();
        }
    }
    void stop() noexcept {
        {std::lock_guard<std::mutex> lock(mutex);stopping=true;}
        wake.notify_all();
        for(auto& w:workers)if(w.joinable())w.join();
    }
public:
    explicit OfflinePool(unsigned n) {
        try {for(unsigned i=0;i<n;++i)workers.emplace_back([this]{workerLoop();});}
        catch(...){stop();throw;}
    }
    ~OfflinePool(){stop();}
    OfflinePool(const OfflinePool&)=delete;
    OfflinePool& operator=(const OfflinePool&)=delete;
    // Single coordinator. Independent jobs are dynamically claimed once.
    int run(int n,Task fn,void* ctx,int jobsPerClaim=1) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            count=n;chunk=(std::max)(1,jobsPerClaim);task=fn;context=ctx;next.store(0,std::memory_order_relaxed);
            workerJobs.store(0,std::memory_order_relaxed);
            remaining=static_cast<int>(workers.size());++generation;
        }
        wake.notify_all();drain(false);
        std::unique_lock<std::mutex> lock(mutex);
        done.wait(lock,[&]{return remaining==0;});
        return workerJobs.load(std::memory_order_relaxed);
    }
};
}
