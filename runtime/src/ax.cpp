// SPDX-License-Identifier: GPL-3.0-only
#include "key/ax.h"
#include "key/ax_processor.h"
#include "key/memory.h"
#include <array>
#include <cstdio>
namespace key::ax {
namespace {
std::array<int16_t,2048> coefficients{};
bool configured=false,running=false;
uint32_t expected_words=0;
}
void configure_coefficients(std::span<const uint8_t> bytes) {
    if(bytes.size()!=4096)throw std::runtime_error("Invalid private DSP coefficient size");
    for(size_t i=0;i<2048;++i)coefficients[i]=static_cast<int16_t>((bytes[2*i]<<8)|bytes[2*i+1]);
    configured=true;
}
void reset(){running=false;expected_words=0;}
bool active(){return running;}
void boot(uint32_t address,uint32_t size,uint32_t entry) {
    if(!configured)throw std::runtime_error("AX requires locally prepared DSP coefficients");
    if(size!=8192 || entry!=16)throw std::runtime_error("Unrecognized AX microcode task extent/entry");
    const auto program=memory().gpu_view(address,size).first(size);
    uint32_t crc=0;for(auto b:program){crc^=b;crc=(crc<<3)|(crc>>29);}
    if(crc!=0x4CC52064)throw std::runtime_error("Unknown DSP microcode; refusing mismatched AX layout");
    AxDspHle::initialize_processor(coefficients);running=true;expected_words=0;
    memory().post_dsp_mail(0xDCD10000,true);
    std::fprintf(stderr,"AUDIO AX task verified: CRC=4CC52064, synchronous native mixer\n");
}
void send_mail(uint32_t mail) {
    if(!running)throw std::runtime_error("AX mail without initialized DSP task");
    if(expected_words) {
        const auto outputs=AxDspHle::process_commands(mail,expected_words);expected_words=0;
        for(unsigned i=0;i<outputs;++i)memory().post_dsp_mail(0xDCD10004,true);
        memory().post_dsp_mail(0xDCD10002,true);
        return;
    }
    if((mail&0xFFFF0000)==0xBABE0000) {
        expected_words=mail&0xFFFF;
        if(!expected_words || expected_words>512)throw std::runtime_error("Invalid AX command-list mail length");
        return;
    }
    if(mail==0xCDD10003)return; // Original task scheduler acknowledges YIELD and calls its resume callback.
    if(mail==0xCDD10000){memory().post_dsp_mail(0xDCD10001,true);return;}
    if(mail==0xCDD10002){reset();return;}
    throw std::runtime_error("Unsupported AX task-switch/mail protocol");
}
}
