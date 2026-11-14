// SPDX-License-Identifier: GPL-3.0-only
#include "key/write_tracking.h"
#include <barrier>
#include <thread>
#include <vector>
#include <cstdio>
static void check(bool ok){if(!ok)throw std::runtime_error("Write tracking test failed");}
template<class F>static void rejects(F fn){bool failed=false;try{fn();}catch(const std::runtime_error&){failed=true;}check(failed);}
int main(){
    key::WriteTracking tracking;tracking.reset();
    const auto initial=tracking.generation(0,10);check(initial!=0);
    for(unsigned i=0;i<10000;++i)tracking.mark(1,1);
    const auto first=tracking.generation(0,10);check(first>initial);
    check(tracking.generation(0,10)==first);check(tracking.generation(0,0)==initial);
    check(tracking.generation(1,1)==first);
    // A write to a page with an older stamp must still invalidate a wide range
    // whose newest stamp came from another page; per-page counters cannot do this.
    tracking.mark(0,0);const auto second=tracking.generation(0,10);check(second>first);
    tracking.mark(4,5);const auto left=tracking.generation(4,4),right=tracking.generation(5,5);
    check(left>second && right>second);check(tracking.generation(0,10)==std::max(left,right));
    tracking.reset();check(tracking.generation(0,10)>right);
    const auto reset=tracking.generation(0,key::WriteTracking::page_count-1);
    tracking.mark(key::WriteTracking::page_count-1,key::WriteTracking::page_count-1);
    check(tracking.generation(0,key::WriteTracking::page_count-1)>reset);
    rejects([&]{tracking.mark(5,4);});rejects([&]{tracking.generation(0,key::WriteTracking::page_count);});
    // Two consumers query overlapping ranges concurrently. Both must observe
    // each completed batch, even when the other consumer consumes its dirty bit.
    std::barrier phases(3);std::atomic<bool> valid{true};
    auto reader=[&](size_t last){
        uint64_t previous=tracking.generation(20,last);
        phases.arrive_and_wait();
        for(unsigned i=0;i<10000;++i){
            phases.arrive_and_wait();
            const auto current=tracking.generation(20,last);
            if(current<=previous || tracking.generation(20,last)!=current)valid=false;
            previous=current;phases.arrive_and_wait();
        }
    };
    std::jthread a(reader,20),b(reader,21);phases.arrive_and_wait();
    for(unsigned i=0;i<10000;++i){
        // Exercise the exact inline ABI's release store as well as bulk marks.
        tracking.dirty_pages()[20].store(1,std::memory_order_release);
        phases.arrive_and_wait();phases.arrive_and_wait();
    }
    a.join();b.join();check(valid.load());
    // Concurrent publishers are followed by a final query after join; no final
    // notification may disappear into a reader's exchange.
    const auto before=tracking.generation(30,31);
    std::jthread w1([&]{for(unsigned i=0;i<100000;++i)tracking.mark(30,30);});
    std::jthread w2([&]{for(unsigned i=0;i<100000;++i)tracking.mark(31,31);});
    for(unsigned i=0;i<1000;++i)tracking.generation(30,31);
    w1.join();w2.join();check(tracking.generation(30,30)>before && tracking.generation(31,31)>before);
    std::puts("PASS: coalesced writes, overlapping ranges, cross-page invalidation, reset and concurrent publishers/readers");
}
