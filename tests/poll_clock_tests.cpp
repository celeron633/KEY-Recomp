// SPDX-License-Identifier: GPL-3.0-only
#include "key/poll_clock.h"
#include "key/clock.h"
#include <stdexcept>
#include <cstdio>
static void check(bool ok){if(!ok)throw std::runtime_error("Event polling clock test failed");}
static unsigned samples=0;
static key::ClockSample sample(){++samples;return {std::chrono::steady_clock::time_point(std::chrono::nanoseconds(samples*100)),samples*10};}
int main(){
    key::PollClock clock(sample,true);
    check(samples==0);check(clock.get().guest==10);check(clock.get().guest==10);
    check(clock.get().host.time_since_epoch()==std::chrono::nanoseconds(100));check(samples==1);
    key::Decrementer alarm;alarm.reset();alarm.write(14,0);
    check(!alarm.pending(clock.get().guest));
    // Callback/interrupt/output completion invalidates the old polling instant.
    clock.invalidate();check(alarm.pending(clock.get().guest));check(samples==2);
    // Nested dispatch gets its own sample, without corrupting its caller.
    {key::PollClock nested(sample,true);check(nested.get().guest==30);}
    check(clock.get().guest==20);clock.invalidate();check(clock.get().guest==40);
    key::PollClock reference(sample,false);check(reference.get().guest==50);check(reference.get().guest==60);
    key::reset_clock(1234);
    const auto before=key::timebase(), snapshot=key::sample_clock().guest, after=key::timebase();
    check(before<=snapshot && snapshot<=after);
    {key::ClockPause pause;const auto frozen=key::sample_clock();
     for(unsigned i=0;i<1000;++i){const auto next=key::sample_clock();check(next.guest==frozen.guest && next.host>=frozen.host);}}
    std::puts("PASS: dispatch-local clock sampling, refresh, nested dispatch, reference mode and paused guest time");
}
