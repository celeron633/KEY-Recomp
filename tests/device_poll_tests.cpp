// SPDX-License-Identifier: GPL-3.0-only
#include "key/video.h"
#include "key/memory.h"
#include <atomic>
#include <thread>
#include <cstdio>
static void check(bool ok){if(!ok)throw std::runtime_error("Device polling test failed");}
int main(){
    key::reset_clock();
    {
        key::ClockPause pause;
        const auto epoch=key::timebase();
        key::VideoInterface vi;
        // Synthetic 100-halfline frame, 100 pixels/halfline = 225 TB ticks.
        vi.write(0,25u<<4,2);vi.write(6,100,2);vi.write(2,1,2);
        vi.write(0x30,0x1002,2);vi.write(0x32,0,2); // Target halfline 2.
        const auto initial=key::timing_counters.video_events;
        for(uint64_t t=0;t<450;++t){vi.tick(epoch+t);check(!vi.pending());}
        vi.tick(epoch+450);check(vi.pending());check(key::timing_counters.video_events==initial+1);
        vi.write(0x30,0x1002,2); // Acknowledge inside the current halfline.
        for(uint64_t t=450;t<675;++t){vi.tick(epoch+t);check(!vi.pending());}
        // Reprogramming a target within that halfline must not retrigger it.
        vi.write(0x30,0x1003,2);vi.tick(epoch+674);check(!vi.pending());
        vi.tick(epoch+900);check(vi.pending());
        check(key::timing_counters.video_events==initial+2);
        vi.write(0x30,0x1003,2);
        vi.tick(epoch+225*304); // Cross three further frames without dropping events.
        check(vi.pending());check(key::timing_counters.video_events==initial+5);
        vi.tick(epoch+225*304);check(key::timing_counters.video_events==initial+5);
    }
    key::Memory m;m.reset();
    m.write(0xCC003000,0x10100,4);m.write(0xCC003004,0x600,4);m.write(0xCC00100A,3,2);
    for(unsigned i=0;i<1000;++i){m.poll_devices();check(!m.interrupt_pending());}
    // One GPU producer and one CPU consumer. Each token must survive empty
    // polls, expose the complete payload, and clear only on guest acknowledgment.
    std::atomic<unsigned> consumed{0};
    std::jthread producer([&](std::stop_token stop){for(unsigned i=1;i<=1000;++i){
        while(consumed.load(std::memory_order_acquire)!=i-1){if(stop.stop_requested())return;std::this_thread::yield();}
        m.gpu_token(static_cast<uint16_t>(i));m.gpu_finish();
    }});
    for(unsigned i=1;i<=1000;++i){
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while((m.read(0xCC003000,4)&0x600)!=0x600){m.poll_devices();check(std::chrono::steady_clock::now()<deadline);}
        check(m.interrupt_pending());check(m.read(0xCC00100E,2)==i);
        m.write(0xCC00100A,15,2);check(!m.interrupt_pending());
        consumed.store(i,std::memory_order_release);
    }
    std::puts("PASS: VI sub-halfline polls, acknowledgment, reprogramming, catch-up and concurrent GPU events");
}
