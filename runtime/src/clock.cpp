// SPDX-License-Identifier: GPL-3.0-only
#include "key/clock.h"
#include "key/host_timeline.h"
#include <chrono>
namespace key {
static uint64_t host_now() {return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
static HostTimeline timeline=[] {HostTimeline value;value.reset(host_now());return value;}();
static uint64_t epoch = 0;
void reset_clock(uint64_t initial_ticks) { timeline.reset(host_now());epoch=initial_ticks;decrementer().reset(initial_ticks); }
ClockPause::ClockPause(){timeline.pause(host_now());}
ClockPause::~ClockPause(){timeline.resume(host_now());}
Decrementer& decrementer(){static Decrementer value;return value;}
uint64_t timebase() {
    return epoch + nanoseconds_to_ticks(timeline.elapsed(host_now()));
}
}
