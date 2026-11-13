// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <stdexcept>
namespace key {
// Active host duration, with explicit suspension for host-output backpressure.
// It changes neither the guest oscillator rate nor the ordering of guest events.
class HostTimeline {
public:
    void reset(uint64_t now) {
        if(depth_)throw std::runtime_error("Cannot reset a suspended guest clock");
        origin_=now;excluded_=frozen_=0;
    }
    uint64_t elapsed(uint64_t now)const {
        const auto effective=depth_?frozen_:now;
        if(effective<origin_ || effective-origin_<excluded_)throw std::runtime_error("Host clock moved backwards");
        return effective-origin_-excluded_;
    }
    void pause(uint64_t now) {
        if(!depth_){elapsed(now);frozen_=now;}
        ++depth_;
    }
    void resume(uint64_t now) {
        if(!depth_ || now<frozen_)throw std::runtime_error("Unbalanced guest clock resume");
        if(--depth_==0)excluded_+=now-frozen_;
    }
private:
    uint64_t origin_=0,excluded_=0,frozen_=0;
    uint32_t depth_=0;
};
}
