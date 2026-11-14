// SPDX-License-Identifier: GPL-3.0-only
#include "key/clock.h"
#include "key/host_timeline.h"
#include "key/poll_clock.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
namespace key {
const bool reuse_poll_clock=[] {const auto* value=std::getenv("KEY_DISABLE_POLL_CLOCK");return !value || std::strcmp(value,"1")!=0;}();
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
ClockSample sample_clock() {
    const auto host=std::chrono::steady_clock::now();
    const auto ns=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(host.time_since_epoch()).count());
    return {host,epoch+nanoseconds_to_ticks(timeline.elapsed(ns))};
}
}
