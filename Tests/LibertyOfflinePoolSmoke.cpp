#include "LibertyOfflinePool.h"
#include <array>
#include <cassert>
#include <atomic>
#include <thread>
struct Batch {
    std::array<std::atomic<int>,128> visits{};
    std::thread::id coordinator;
    std::atomic<int> remote{0};
};
static void job(void* p,int i) noexcept {
    auto& b=*static_cast<Batch*>(p);
    b.visits[static_cast<unsigned>(i)].fetch_add(1,std::memory_order_relaxed);
    if(std::this_thread::get_id()!=b.coordinator)b.remote.fetch_add(1,std::memory_order_relaxed);
}
int main(){
    LibertyMulticore::OfflinePool pool(3);
    for(int n:{0,16,32,64,128})
        for(int repeat=0;repeat<32;++repeat){
            Batch b;b.coordinator=std::this_thread::get_id();
            for(auto& v:b.visits)v.store(0);
            const auto remote=pool.run(n,&job,&b);
            assert(remote==b.remote.load() && remote>=0 && remote<=n);
            for(int i=0;i<128;++i)assert(b.visits[static_cast<unsigned>(i)].load()==(i<n?1:0));
        }
}
