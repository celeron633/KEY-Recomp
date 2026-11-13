// SPDX-License-Identifier: GPL-3.0-only
#include "command_bridge.h"
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <array>
static void require(bool value) { if(!value) throw std::runtime_error("GX bridge contract failed"); }
template<class F> void rejects(F fn) { bool caught=false;try {fn();}catch(const std::runtime_error&){caught=true;}require(caught); }
int main() {
    std::vector<uint8_t> output;
    std::array<uint8_t,4096> ram{};
    auto resolve=[&](uint32_t a,size_t n)->std::span<const uint8_t> {
        if(a<0x1000 || a>0x2000 || n>0x2000-a) throw std::runtime_error("Unmapped synthetic GX address");
        return std::span<const uint8_t>(ram).subspan(a-0x1000);
    };
    auto writable=[&](uint32_t a,size_t n)->void* {return const_cast<uint8_t*>(resolve(a,n).data());};
    auto emit=[&](std::span<const uint8_t> data) {output.insert(output.end(),data.begin(),data.end());};
    key::graphics::CommandBridge bridge(emit,resolve,writable);
    // XYZ float direct vertices: deliberately split each command into individual writes.
    const std::vector<uint8_t> cp{8,0x50,0,0,2,0,8,0x70,0,0,0,9};
    for(auto b:cp) bridge.feed({&b,1});
    require(bridge.empty() && output==cp);
    output.clear();
    std::vector<uint8_t> draw(3+36,0);draw[0]=0x90;draw[2]=3;
    bridge.feed(std::span<const uint8_t>(draw).first(20));
    require(!bridge.empty() && output.empty());
    bridge.feed(std::span<const uint8_t>(draw).subspan(20));
    require(bridge.empty() && output==draw && bridge.draw_count()==1);
    // Array addresses become bounded host-pointer extensions, never raw guest pointers.
    output.clear(); const std::array<uint8_t,6> array{8,0xA0,0,0,0x10,0};bridge.feed(array);
    require(output.empty());
    const std::array<uint8_t,12> indexed_setup{8,0x50,0,0,4,0,8,0xB0,0,0,0,12};bridge.feed(indexed_setup);output.clear();
    const std::array<uint8_t,6> indexed_draw{0x90,0,3,0,1,2};bridge.feed(indexed_draw);
    require(output.size()==22 && output[0]==0x50 && output[1]==0 && output[2]==0x10);
    uint64_t pointer=0;for(unsigned i=3;i<11;++i)pointer=(pointer<<8)|output[i];
    require(pointer==reinterpret_cast<uintptr_t>(ram.data()));
    uint32_t upload=0;for(unsigned i=11;i<15;++i)upload=(upload<<8)|output[i];require(upload==64);
    output.clear();const std::array<uint8_t,6> skipped{0x90,0,3,0,255,2};bridge.feed(skipped);require(output==std::vector<uint8_t>(skipped.begin(),skipped.end()));
    // BP one-shot mask merges values and emits an unmasked downstream write.
    output.clear();
    const std::array<uint8_t,15> mask{0x61,0x42,0x12,0x34,0x56,0x61,0xFE,0,0,0xFF,0x61,0x42,0xAB,0xCD,0xEF};
    bridge.feed(mask);require(output==std::vector<uint8_t>({0x61,0x42,0x12,0x34,0x56,0x61,0x42,0x12,0x34,0xEF}));
    // Display lists are read through the resolver and expanded in stream order.
    output.clear(); std::copy(cp.begin(),cp.end(),ram.begin());
    const std::array<uint8_t,9> list{0x40,0,0,0x10,0,0,0,0,12};bridge.feed(list);require(output==cp);
    const std::array<uint8_t,9> truncated{0x40,0,0,0x10,0,0,0,0,1};
    rejects([&] {key::graphics::CommandBridge b(emit,resolve,writable);b.feed(truncated);});
    const std::array<uint8_t,1> unknown{0xFF};
    rejects([&] {key::graphics::CommandBridge b(emit,resolve,writable);b.feed(unknown);});
    // Recursive DLs must fail at the depth bound instead of exhausting the host stack.
    const std::array<uint8_t,9> recursive{0x40,0,0,0x10,0,0,0,0,9};std::copy(recursive.begin(),recursive.end(),ram.begin());
    rejects([&] {key::graphics::CommandBridge b(emit,resolve,writable);b.feed(recursive);});
    std::puts("PASS: split GX packets, vertex extents, address translation, BP masks, bounded display lists");
}
