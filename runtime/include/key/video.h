// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "key/clock.h"
#include "key/timing_metrics.h"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <algorithm>
namespace key {
class VideoInterface {
public:
    VideoInterface() { reset(); }
    void reset() { registers_.fill(0); registers_[0x6C/2]=1; epoch_=last_halfline_=0; }
    uint64_t read(uint32_t reg,unsigned width) const {
        validate(reg,width);
        const auto now=timebase();
        if(width==2) return read16(reg,now);
        return (uint32_t(read16(reg,now))<<16)|read16(reg+2,now);
    }
    void write(uint32_t reg,uint32_t value,unsigned width) {
        validate(reg,width);
        if(width==4) { write(reg,value>>16,2); write(reg+2,value&0xFFFF,2); return; }
        if(reg==0x2C || reg==0x2E) throw std::runtime_error("VI beam position writes are not supported");
        if(reg==2) {
            if(value&2) { for(unsigned i=0;i<4;++i) registers_[(0x30+i*4)/2]=0; epoch_=timebase(); last_halfline_=0; }
            registers_[1]=static_cast<uint16_t>(value&~2u);
        } else {
            if((reg<=0x12 || reg==0x6C) && registers_[reg/2]!=value) { epoch_=timebase(); last_halfline_=0; }
            registers_[reg/2]=static_cast<uint16_t>(value);
        }
    }
    void tick() { tick(timebase()); }
    void tick(uint64_t now) {
        const auto width=registers_[3]&0x3FF;
        const auto total=total_halflines();
        if(!(registers_[1]&1) || !width || !total) return;
        const auto current=pixels(now)/width;
        // No interrupt target can be crossed while the beam stays in the same
        // halfline. Register writes still take effect at the next crossing.
        if(current==last_halfline_)return;
        for(unsigned i=0;i<4;++i) {
            auto& hi=registers_[(0x30+i*4)/2];
            const auto vertical=hi&0x7FF;
            if(!vertical) continue;
            const auto target=uint64_t((vertical-1)*2)+(registers_[(0x32+i*4)/2]>width?1:0);
            if(target>=total) continue;
            const auto last_period=last_halfline_/total;
            const auto next=(last_halfline_%total<target?last_period:last_period+1)*total+target;
            if(next<=current) {
                if(hi&0x1000)timing_counters.video_events+=(current-next)/total+1;
                hi|=0x8000;
            }
        }
        last_halfline_=current;
    }
    bool pending() const {
        for(unsigned i=0;i<4;++i) if((registers_[(0x30+i*4)/2]&0x9000)==0x9000) return true;
        return false;
    }
private:
    std::array<uint16_t,0x3B> registers_{};
    uint64_t epoch_=0,last_halfline_=0;
    static void validate(uint32_t reg,unsigned width) {
        if((width!=2 && width!=4)||reg%2||reg>=0x76||width>0x76-reg) throw std::runtime_error("Invalid VI register access");
    }
    uint32_t total_halflines() const {
        const auto common=3*(registers_[0]&15)+2*((registers_[0]>>4)&1023);
        return 2*common+(registers_[6]&1023)+(registers_[7]&1023)+(registers_[8]&1023)+(registers_[9]&1023);
    }
    uint64_t pixels(uint64_t now) const {
        const auto elapsed=now>=epoch_?now-epoch_:0;
        const uint64_t rate=(registers_[0x6C/2]&1)?27000000:13500000;
        return (elapsed/timebase_hz)*rate+(elapsed%timebase_hz)*rate/timebase_hz;
    }
    uint16_t read16(uint32_t reg,uint64_t now) const {
        if(reg==0x2C || reg==0x2E) {
            const uint32_t width=registers_[3]&1023,total=total_halflines();
            if(!(registers_[1]&1)||!width||!total) return 1;
            const auto value=pixels(now);
            return reg==0x2C?static_cast<uint16_t>(1+((value/width)%total)/2):static_cast<uint16_t>(1+value%(width*2));
        }
        return registers_[reg/2];
    }
};
}
