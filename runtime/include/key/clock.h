// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <stdexcept>
namespace key {
// Wii time-base: 729 MHz core / 12. Epoch is explicit and separate from duration.
constexpr uint64_t timebase_hz = 60750000;
constexpr uint64_t rtc_to_ticks(uint64_t local_unix_seconds) {
    if (local_unix_seconds < 946684800 || local_unix_seconds - 946684800 > 0xFFFFFFFFull)
        throw std::runtime_error("RTC outside supported 2000 epoch range");
    return (local_unix_seconds - 946684800) * timebase_hz;
}
constexpr uint64_t nanoseconds_to_ticks(uint64_t ns) {
    return (ns / 1000000000) * timebase_hz + ((ns % 1000000000) * timebase_hz) / 1000000000;
}
void reset_clock(uint64_t initial_ticks = 0);
uint64_t timebase();
// Only the guest/producer thread owns the clock. Do not dispatch guest code
// while this scope is active; window event collection is safe.
class ClockPause {
public:
    ClockPause();
    ~ClockPause();
    ClockPause(const ClockPause&)=delete;
    ClockPause& operator=(const ClockPause&)=delete;
};
class Decrementer {
public:
    void reset(uint64_t now=0){value_=0xFFFFFFFF;epoch_=now;deadline_=0;armed_=pending_=false;}
    uint32_t read(uint64_t now)const{return value_-static_cast<uint32_t>(now-epoch_);}
    void tick(uint64_t now){
        if(armed_ && now>=deadline_){pending_=true;deadline_+=((now-deadline_)/0x100000000ull+1)*0x100000000ull;}
    }
    void write(uint32_t value,uint64_t now){
        tick(now);if(!(read(now)&0x80000000) && (value&0x80000000))pending_=true;
        value_=value;epoch_=now;deadline_=now+uint64_t(value)+1;armed_=true;
    }
    bool pending(uint64_t now){tick(now);return pending_;}
    void acknowledge(){pending_=false;}
private:
    uint32_t value_=0xFFFFFFFF;
    uint64_t epoch_=0,deadline_=0;
    bool armed_=false,pending_=false;
};
Decrementer& decrementer();
}
