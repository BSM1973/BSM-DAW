#pragma once
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace LibertyMulticore {
// Offline executor only. Live audio never waits on this barrier.
class OfflinePool {
    using Task=void(*)(void*,int) noexcept;
    std::vector<std::thread> workers;
    std::mutex mutex;
    std::condition_variable wake,done;
    std::atomic<int> next{0},workerJobs{0};
    int count=0,remaining=0;
    unsigned long long generation=0;
    bool stopping=false;
    Task task=nullptr;
    void* context=nullptr;
    void drain(bool worker) noexcept {
        for(;;){
            const int i=next.fetch_add(1,std::memory_order_relaxed);
            if(i>=count)break;
            task(context,i);
            if(worker)workerJobs.fetch_add(1,std::memory_order_relaxed);
        }
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
    int run(int n,Task fn,void* ctx) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            count=n;task=fn;context=ctx;next.store(0,std::memory_order_relaxed);
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
