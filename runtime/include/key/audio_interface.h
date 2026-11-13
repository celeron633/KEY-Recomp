// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "key/clock.h"
#include <cstdint>
#include <stdexcept>
namespace key {
// AI streaming clock/control. Audible DTK streaming remains unsupported;
// the muted oscillator is used by the original SDK's calibration routine.
class AudioInterface {
public:
    void reset() {control_=0x42;volume_=counter_=target_=0;last_=fraction_=0;}
    void tick(uint64_t now) {
        const auto elapsed=now>=last_?now-last_:0;last_=now;
        if(!(control_&1))return;
        const uint64_t rate=(control_&2)?48000:32000;
        const auto numerator=(elapsed%timebase_hz)*rate+fraction_;
        const auto samples=(elapsed/timebase_hz)*rate+numerator/timebase_hz;
        fraction_=numerator%timebase_hz;
        if(samples && uint32_t(target_-(counter_+1))<samples)control_|=8;
        counter_+=static_cast<uint32_t>(samples);
    }
    uint32_t read(uint32_t reg,uint64_t now) {
        tick(now);
        switch(reg) {case 0:return control_;case 4:return volume_;case 8:return counter_;case 12:return target_;}
        throw std::runtime_error("Unknown AI register");
    }
    void write(uint32_t reg,uint32_t value,uint64_t now) {
        tick(now);
        switch(reg) {
        case 0:
            if(value&~0x7Fu)throw std::runtime_error("Unknown AI control bits");
            if((value&1) && volume_)throw std::runtime_error("Audible disc audio stream requires DTK decoder");
            control_=(value&0x57)|(control_&8&~value);
            if(value&32){counter_=0;fraction_=0;}
            return;
        case 4:
            if(value&~0xFFFFu)throw std::runtime_error("Unknown AI volume bits");
            if(value && (control_&1))throw std::runtime_error("Audible disc audio stream requires DTK decoder");
            volume_=value;return;
        case 8:counter_=value;fraction_=0;return;
        case 12:target_=value;return;
        }
        throw std::runtime_error("Unknown AI register");
    }
    bool pending()const{return (control_&12)==12;}
private:
    uint32_t control_=0x42,volume_=0,counter_=0,target_=0;
    uint64_t last_=0,fraction_=0;
};
}
