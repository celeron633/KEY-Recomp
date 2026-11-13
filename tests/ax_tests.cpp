// SPDX-License-Identifier: GPL-3.0-only
#include "key/ax_processor.h"
#include "key/memory.h"
#include "ax_types.h"
#include <array>
#include <cstdio>
#include <stdexcept>
void check(bool v){if(!v)throw std::runtime_error("AX synthetic test failed");}
int main(){
    auto& m=key::memory();m.reset();
    std::array<int16_t,2048> coefficients{};AxDspHle::initialize_processor(coefficients);
    // All command data and PCM are synthesized, with no game/DSP input.
    AxDspHle::AXPBWii pb{};pb.running=1;pb.src_type=2;pb.mixer_control_lo=3;
    pb.mixer.main_left.volume=pb.mixer.main_right.volume=0x8000;
    pb.vol_env.cur_volume=static_cast<int16_t>(0x8000);pb.audio_addr.sample_format=10;pb.adpcm.gain=0x800;
    pb.audio_addr.cur_addr_lo=pb.audio_addr.loop_addr_lo=0x4000;pb.audio_addr.end_addr_lo=0x405F;
    for(unsigned i=0;i<96;++i)m.write(0x8000+i*2,static_cast<uint16_t>(int(i)*100-4800),2);
    const auto* bytes=reinterpret_cast<const uint8_t*>(&pb);
    size_t dest=0;
    for(size_t i=0;i<sizeof(pb);i+=2){
        if(i>=offsetof(AxDspHle::AXPBWii,updates) && i<offsetof(AxDspHle::AXPBWii,updates)+sizeof(AxDspHle::PBUpdatesWii))continue;
        uint16_t v;std::memcpy(&v,bytes+i,2);m.write(0x6000+static_cast<uint32_t>(dest),v,2);dest+=2;
    }
    const std::array<uint16_t,13> commands={0,0,0x5000,4,0,0x6000,11,0x8000,0,0x7000,0,0x7400,14};
    for(unsigned i=0;i<commands.size();++i)m.write(0x4000+i*2,commands[i],2);
    check(AxDspHle::process_commands(0x4000,commands.size())==1);
    for(unsigned i=0;i<96;++i){check(static_cast<int16_t>(m.read(0x7400+i*4,2))==int(i)*100-4800);check(m.read(0x7400+i*4,2)==m.read(0x7402+i*4,2));}
    bool failed=false;try{AxDspHle::process_commands(0x4000,12);}catch(const std::runtime_error&){failed=true;}check(failed);
    m.write(0x4000,0xFFFF,2);failed=false;try{AxDspHle::process_commands(0x4000,13);}catch(const std::runtime_error&){failed=true;}check(failed);
    std::puts("PASS: AX PCM16 voice -> stereo samples, PB translation, malformed/unknown commands rejected");
}
