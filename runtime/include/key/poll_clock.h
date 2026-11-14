// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <chrono>
#include <cstdint>
namespace key {
struct ClockSample {
    std::chrono::steady_clock::time_point host;
    uint64_t guest;
};
ClockSample sample_clock();
extern const bool reuse_poll_clock;
// A dispatch boundary is one event-polling instant. Resample after executing
// callbacks or host output, never carry a sample into another dispatch.
class PollClock {
    ClockSample sample_{};
    bool valid_=false;
    ClockSample (*sample_now_)();
    bool reuse_;
public:
    explicit PollClock(ClockSample (*sample_now)()=sample_clock,bool reuse=reuse_poll_clock):sample_now_(sample_now),reuse_(reuse){}
    const ClockSample& get() {
        if(!valid_ || !reuse_){sample_=sample_now_();valid_=true;}
        return sample_;
    }
    void invalidate(){valid_=false;}
};
}
