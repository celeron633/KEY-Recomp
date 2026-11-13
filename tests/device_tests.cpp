// SPDX-License-Identifier: GPL-3.0-only
#include "key/memory.h"
#include "key/clock.h"
#include "key/audio_interface.h"
#include <array>
#include <functional>
#include <cstdio>
static void check(bool ok) { if(!ok) throw std::runtime_error("Device test failed"); }
static void rejects(const std::function<void()>& f) { bool caught=false; try{f();}catch(const std::runtime_error&){caught=true;} check(caught); }
static size_t fifo_received=0;
static void fifo_sink(const uint8_t*,size_t size){fifo_received+=size;}
int main() {
    key::AudioInterface ai;ai.reset();
    ai.write(12,480,0);ai.write(0,7,0);
    check(ai.read(8,key::timebase_hz/100)==480 && ai.pending());
    ai.write(0,15,key::timebase_hz/100);check(!ai.pending());
    ai.write(0,6,key::timebase_hz/100);
    check(ai.read(8,key::timebase_hz)==480);
    ai.write(0,0x26,key::timebase_hz);check(ai.read(8,key::timebase_hz)==0);
    ai.write(4,0xFFFF,key::timebase_hz);
    rejects([&]{ai.write(0,7,key::timebase_hz);});
    key::Memory m;
    m.reset();m.set_fifo_sink(&fifo_sink);
    m.write(0xCC00300C,0x10000,4);m.write(0xCC003010,0x10020,4);m.write(0xCC003014,0x10000,4);
    m.write(0xCC008000,0x12345678,4);check(m.read(0x10000,4)==0x12345678 && fifo_received==0);
    m.write(0xCC000002,0x11,2);m.write(0xCC008000,0xAABBCCDD,4);
    check(m.read(0x10004,4)==0xAABBCCDD && fifo_received==4);
    m.write(0xCC003014,0x10020,4);std::array<uint8_t,40> stream{};stream.fill(0x55);m.fifo_bytes(stream.data(),stream.size());
    check(m.read(0x10000,4)==0x55555555 && m.read(0xCC003014,4)==0x10000 && fifo_received==44);
    m.reset();
    check(m.read(0xCC003000,4)==0x10100);
    m.write(0xCC003004,0x100,4); check(m.interrupt_pending());
    m.write(0xCC003000,0x100,4); check(!m.interrupt_pending());
    rejects([&]{m.write(0xCC003004,0,2);});
    m.reset();
    m.write(0xCD006434,0x40000001,4);
    check((m.read(0xCD006434,4)&0xA0000001)==0xA0000000);
    check((m.read(0xCD006438,4)&0x08000000)!=0);
    m.write(0xCC003004,8,4); check(m.interrupt_pending());
    m.write(0xCD006434,0xC0000000,4); check(!m.interrupt_pending());
    m.write(0xCD006480,0x12345678,4); check(m.read(0xCD006481,1)==0x34);
    rejects([&]{m.read(0xCD0064FF,4);});
    m.reset();
    m.write(0xCD006800,0x200,4); // Explicitly absent slot 2 on channel 0.
    m.write(0xCD00680C,0x31,4); // Four-byte immediate read.
    check(m.read(0xCD006810,4)==0 && !(m.read(0xCD00680C,4)&1));
    check(m.read(0xCD006800,4)&8);
    m.write(0xCD006800,0x208,4); check(!(m.read(0xCD006800,4)&8));
    rejects([&]{m.write(0xCD00680C,3,4);});
    key::reset_clock(key::rtc_to_ticks(946771200));
    m.reset();
    m.write(0xCD006800,0x100,4); // IPL, command for SRAM settings excluding RTC word.
    m.write(0xCD006810,0x20000100,4); m.write(0xCD00680C,0x35,4);
    m.write(0xCD006804,0x5000,4); m.write(0xCD006808,64,4); m.write(0xCD00680C,3,4);
    uint16_t sum=0,inverse=0;
    for(unsigned i=12;i<20;i+=2) { auto v=static_cast<uint16_t>(m.read(0x5000+i,2)); sum+=v; inverse+=static_cast<uint16_t>(~v); }
    check(m.read(0x5000,2)==sum && m.read(0x5002,2)==inverse);
    check(m.read(0x5013,1)==0x0C);
    m.reset();
    std::array<uint8_t,128> synthetic{}; for(unsigned i=0;i<128;++i) synthetic[i]=static_cast<uint8_t>(i);
    m.load(0x80005000,synthetic.data(),synthetic.size(),false); m.capture_dsp_boot_image(0x80005000);
    m.load(0x81000000,synthetic.data(),synthetic.size(),false);
    m.write(0xCC00500A,0x8AD,2); check(!(m.read(0xCC00500A,2)&1));
    rejects([&]{m.write(0xCC00500A,0,2);});
    for(unsigned i=0;i<2;++i) {
        m.write(0xCC005020,0x01000000,4); m.write(0xCC005024,0,4); m.write(0xCC005028,32,4);
        check(m.read(0xCC00500A,2)&0x20);
        m.write(0xCC00500A,m.read(0xCC00500A,2),2);
    }
    m.write(0xCC00500A,0,2); check(m.read(0xCC005004,2)==0x8054);
    check(m.read(0xCC005006,2)==0x4348); check(!(m.read(0xCC005004,2)&0x8000));
    // DSPInit resets into the ROM task loader, not the earlier RAM INIT image.
    m.write(0xCC00500A,0x805,2);m.write(0xCC00500A,0x800,2);
    check(m.read(0xCC005004,2)==0x8071 && m.read(0xCC005006,2)==0xFEED);
    m.reset();
    for(auto a:{0x80003100u,0x80003104u}) m.write(a,0x01800000,4);
    m.write(0x8000311C,0x04000000,4); m.write(0x80003120,0x93600000,4);
    m.configure_retail_mapping();
    m.write(0x90001000,0x12345678,4); check(m.read(0x10001000,4)==0x12345678 && m.read(0xD0001000,4)==0x12345678);
    rejects([&]{m.read(0x93800000,4);});
    m.write(0xD3FFFFFC,0xABCD,4); check(m.read(0x13FFFFFC,4)==0xABCD);
    rejects([&]{m.read(0x13FFFFFC,8);});
    m.reset(); rejects([&]{m.read(0x90001000,4);});
    std::puts("PASS: PI/SI/EXI/IPL checksums/DSP bootstrap/MEM2 aliases and boundaries");
}
