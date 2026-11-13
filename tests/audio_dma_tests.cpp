// SPDX-License-Identifier: GPL-3.0-only
#include "key/memory.h"
#include "key/clock.h"
#include <cstdio>
#include <stdexcept>
static unsigned blocks=0;
static void check(bool v){if(!v)throw std::runtime_error("Audio DMA test failed");}
static void sink(std::span<const uint8_t> data,uint32_t rate){check(rate==32000 && data.size()==32 && data[0]==blocks);++blocks;}
int main(){
    key::reset_clock(100000);key::Memory m;m.reset();m.set_audio_sink(&sink);
    m.fill(0x6000,32,0);m.fill(0x6020,32,1);
    m.write(0xCC003000,0x10100,4);m.write(0xCC003004,0x40,4);
    m.write(0xCC00500A,0x14,2);m.write(0xCC005030,0,2);m.write(0xCC005032,0x6000,2);m.write(0xCC005036,0x8002,2);
    const auto start=key::timebase();m.poll_audio_dma(start+1000);
    check(m.interrupt_pending() && blocks==0);
    m.write(0xCC00500A,0x1C,2);check(!m.interrupt_pending());
    m.poll_audio_dma(start+key::timebase_hz*16/32000+1000);
    check(blocks==2 && m.interrupt_pending());
    m.write(0xCC005036,0,2);m.write(0xCC00500A,0x1C,2);
    m.poll_audio_dma(start+key::timebase_hz);check(blocks==2 && !m.interrupt_pending());
    m.reset();m.write(0xCC003000,0x10100,4);m.write(0xCC003004,0x40,4);m.write(0xCC00500A,0x100,2);
    m.post_dsp_mail(0xDCD10004,true);m.post_dsp_mail(0xDCD10002,true);check(m.interrupt_pending());
    m.write(0xCC00500A,0x180,2);check(!m.interrupt_pending());
    check(m.read(0xCC005004,2)==0xDCD1 && m.read(0xCC005006,2)==4 && m.interrupt_pending());
    m.write(0xCC00500A,0x180,2);check(m.read(0xCC005006,2)==2 && !m.interrupt_pending());
    check(!(m.read(0xCC005004,2)&0x8000));
    std::puts("PASS: audio DMA start/completion IRQ, block order, stop and ordered DSP mail interrupts");
}
