// SPDX-License-Identifier: GPL-3.0-only
#include "key/host_timeline.h"
#include "key/clock.h"
#include <cstdio>
static void check(bool ok){if(!ok)throw std::runtime_error("Host timeline test failed");}
template<class F>void rejects(F fn){bool failed=false;try{fn();}catch(const std::runtime_error&){failed=true;}check(failed);}
int main(){
    key::HostTimeline clock;clock.reset(1000);
    check(clock.elapsed(1100)==100);
    clock.pause(1100);check(clock.elapsed(1000000)==100);
    clock.pause(2000);clock.resume(3000);check(clock.elapsed(1000000)==100);
    rejects([&]{clock.reset(5000);});clock.resume(1000000);
    check(clock.elapsed(1000100)==200);rejects([&]{clock.resume(1000200);});
    clock.pause(1000200);clock.resume(3000000);check(clock.elapsed(3000100)==400);
    // A guest deadline does not expire while the host drains audio, and does
    // expire after the remaining active time. Neither rate nor phase changes.
    clock.reset(0);key::Decrementer alarm;alarm.reset();
    alarm.write(static_cast<uint32_t>(key::nanoseconds_to_ticks(150000000)-1),0);
    clock.pause(100000000);
    check(!alarm.pending(key::nanoseconds_to_ticks(clock.elapsed(5000000000ull))));
    clock.resume(5000000000ull);
    check(!alarm.pending(key::nanoseconds_to_ticks(clock.elapsed(5049999999ull))));
    check(alarm.pending(key::nanoseconds_to_ticks(clock.elapsed(5050000000ull))));
    std::puts("PASS: monotonic host backpressure pause, nesting, resume and unchanged guest alarm deadline");
}
