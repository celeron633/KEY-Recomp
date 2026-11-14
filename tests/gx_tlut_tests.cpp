// SPDX-License-Identifier: GPL-3.0-only
#include "command_bridge.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

static void require(bool value, const char* message) {
    if(!value) throw std::runtime_error(message);
}
template<class F> static void rejects(F fn) {
    bool caught=false;
    try { fn(); } catch(const std::runtime_error&) { caught=true; }
    require(caught,"Invalid TLUT transfer was accepted");
}
static void bp(key::graphics::CommandBridge& bridge,unsigned reg,uint32_t value) {
    const std::array<uint8_t,5> packet{0x61,static_cast<uint8_t>(reg),
        static_cast<uint8_t>(value>>16),static_cast<uint8_t>(value>>8),static_cast<uint8_t>(value)};
    bridge.feed(packet);
}

int main() {
    // Independent palettes with identical low address bits detect silent MEM1
    // aliasing as well as the unmapped-address failure for higher MEM2 offsets.
    constexpr std::array<uint32_t,4> bases{0x00123000,0x10123000,0x12ABC000,0x13FF8000};
    std::array<std::vector<uint8_t>,4> palettes;
    for(size_t bank=0;bank<palettes.size();++bank) {
        palettes[bank].resize(32768);
        for(size_t i=0;i<palettes[bank].size();++i)
            palettes[bank][i]=static_cast<uint8_t>(i*13+bank*37);
    }
    std::array<uint8_t,64> texture{};
    uint32_t resolved_source=0;
    size_t resolved_size=0;
    auto resolve=[&](uint32_t address,size_t size)->std::span<const uint8_t> {
        if(address==0x4000 && size<=texture.size())return texture;
        for(size_t bank=0;bank<bases.size();++bank) {
            if(address>=bases[bank] && address-bases[bank]<palettes[bank].size()) {
                const auto offset=address-bases[bank];
                if(size>palettes[bank].size()-offset)throw std::runtime_error(
                    "Synthetic TLUT source crosses mapping: bank="+std::to_string(bank)+
                    " offset="+std::to_string(offset)+" size="+std::to_string(size));
                resolved_source=address;resolved_size=size;
                return std::span<const uint8_t>(palettes[bank]).subspan(offset);
            }
        }
        throw std::runtime_error("Unmapped synthetic TLUT source");
    };
    const uint8_t* snapshot=nullptr;
    uint32_t format=0,entries=0;
    auto emit=[&](std::span<const uint8_t> packet) {
        if(packet.size()!=18 || packet[0]!=0x50 || packet[1]!=0 || packet[2]!=0x3C)return;
        require(packet[3]==0,"Unexpected palette texture unit");
        uintptr_t pointer=0;
        for(unsigned i=4;i<12;++i)pointer=(pointer<<8)|packet[i];
        snapshot=reinterpret_cast<const uint8_t*>(pointer);
        format=0;for(unsigned i=12;i<16;++i)format=(format<<8)|packet[i];
        entries=(packet[16]<<8)|packet[17];
    };
    auto writable=[](uint32_t,size_t)->void* {throw std::runtime_error("Unexpected synthetic GPU write");};
    key::graphics::CommandBridge bridge(emit,resolve,writable);
    bp(bridge,0x28,64); // One TEV stage consumes texture unit zero.
    bp(bridge,0x94,0x4000>>5);
    const std::array<uint8_t,3> draw{0x90,0,1};
    for(unsigned texture_format=8;texture_format<=10;++texture_format) {
        const unsigned count=texture_format==8?16:texture_format==9?256:16384;
        bp(bridge,0x88,(texture_format<<20)|7|(3<<10));
        for(size_t bank=0;bank<bases.size();++bank) {
            bp(bridge,0x64,bases[bank]>>5);
            bp(bridge,0x65,((count*2/32)<<10)|1);
            require(resolved_source==bases[bank] && resolved_size==count*2,
                    "TLUT upload lost Wii physical address bits or transfer extent");
            bp(bridge,0x98,((bank%3)<<10)|1);
            snapshot=nullptr;bridge.feed(draw);
            require(snapshot && entries==count && format==bank%3,"Incorrect palette binding");
            require(std::equal(snapshot,snapshot+count*2,palettes[bank].begin()),"Palette content differs from selected memory bank");
            // Later source writes must not mutate an already queued GPU palette.
            const auto first=snapshot[0];palettes[bank][0]^=0xFF;
            require(snapshot[0]==first,"Queued palette aliases mutable guest memory");
            palettes[bank][0]^=0xFF;
        }
    }
    // BP masking preserves the MEM2 selector across a low-bit-only update.
    bp(bridge,0x64,bases[1]>>5);
    bp(bridge,0xFE,1);
    bp(bridge,0x64,1);
    bp(bridge,0x65,1<<10);
    require(resolved_source==bases[1]+32 && resolved_size==32,"Masked TLUT address lost MEM2 selector");
    bp(bridge,0x64,0x13FFFFE0>>5);
    bp(bridge,0x65,1<<10);
    require(resolved_source==0x13FFFFE0,"Last MEM2 line was not resolved");
    // A failed FIFO packet is terminal; each rejection uses a fresh bridge.
    auto reject_transfer=[&](uint32_t source,uint32_t destination) {
        rejects([&]{
            key::graphics::CommandBridge invalid(emit,resolve,writable);
            bp(invalid,0x64,source>>5);bp(invalid,0x65,destination);
        });
    };
    reject_transfer(0x13FFFFE0,2<<10);
    reject_transfer(0x14000000,1<<10);
    reject_transfer(bases[0],(17<<10)|1023);
    std::puts("PASS: Wii MEM1/MEM2 TLUT addresses, indexed formats, snapshots, BP masks and bounds");
}
