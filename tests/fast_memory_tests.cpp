// SPDX-License-Identifier: GPL-3.0-only
#include "key/guest_memory_abi.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
static void check(bool ok){if(!ok)throw std::runtime_error("Checked inline RAM test failed");}
template<class F>static void rejects(F fn){bool failed=false;try{fn();}catch(const std::runtime_error&){failed=true;}check(failed);}
template<class T>static void scalars(uint32_t base){
    auto& m=key::memory();
    uint64_t pattern=0x9E3779B97F4A7C15ull;
    for(unsigned i=0;i<256;++i){
        const uint32_t address=base+i;
        if(sizeof(T)==8 && address%4)continue;
        pattern=pattern*6364136223846793005ull+1;
        const T value=static_cast<T>(pattern);
        key::fast_memory::write<T>(address,value);
        for(unsigned byte=0;byte<sizeof(T);++byte)
            check(m.read(address+byte,1)==((value>>(8*(sizeof(T)-byte-1)))&255));
        check(key::fast_memory::read<T>(address)==value);
        m.write(address,static_cast<T>(~value),sizeof(T));
        check(key::fast_memory::read<T>(address)==static_cast<T>(~value));
    }
}
int main(){
    // The first fast access must initialize the canonical memory safely.
    check(MemoryInline::FlatRead32(0x5000)==0);
    auto& m=key::memory();m.reset();
    check(key::fast_memory_view.owner==&m);
    for(auto base:{0x8FE0u,0x80008FE0u,0xC0008FE0u}){
        scalars<uint8_t>(base);scalars<uint16_t>(base);scalars<uint32_t>(base);scalars<uint64_t>(base);
    }
    for(auto address:{0x40005000u,0x60005000u,0xE0005000u,0x81800000u,0x90005000u,0xFFFFFFFFu}){
        rejects([&]{Memory::Read32(address);});rejects([&]{Memory::Write32(address,1);});
    }
    rejects([&]{Memory::Read64(0x80005002);});
    rejects([&]{Memory::Write64(0x80005002,1);});
    rejects([&]{Memory::Read64(0x817FFFFC);});
    rejects([&]{Memory::Read32(0x800000F4);});
    Memory::Write16(0x800000F4,0x1234);
    rejects([&]{Memory::Read32(0x800000F4);});
    Memory::Write16(0x800000F6,0x5678);check(Memory::Read32(0xF4)==0x12345678);
    const std::array<uint8_t,4> code{1,2,3,4};m.load(0x80006004,code.data(),code.size(),true);
    check(Memory::Read32(0x6004)==0x01020304);
    rejects([&]{Memory::Write32(0xC0006004,0);});
    rejects([&]{Memory::Write32(0x80006003,0);});
    Memory::Write32(0x80006000,7);check(Memory::Read32(0x6000)==7);
    m.add_template_word(0x80006004);m.template_access(true);
    Memory::Write32(0x80006004,0x10203040);m.template_access(false);
    check(Memory::Read32(0x6004)==0x10203040);
    rejects([&]{Memory::Write32(0x6004,0);});
    // Exact GPU hazards remain authoritative even on bitmap false positives.
    m.mark_gpu_copy(0x80007000,8);
    rejects([&]{Memory::Read32(0xC0007000);});
    rejects([&]{Memory::Write32(0x80007000,0);});
    Memory::Write32(0x80007010,11);check(Memory::Read32(0x7010)==11);
    Memory::Write64(0x80007000,0x1020304050607080ull);
    check(Memory::Read64(0x7000)==0x1020304050607080ull);
    m.mark_gpu_copy(0x8000A000,4);
    rejects([&]{Memory::Read64(0x80009FFC);});
    rejects([&]{Memory::Write16(0x80009FFF,0);});m.fill(0x80009FFC,8,0);
    // CPU writes must invalidate both GPU generation pages on a crossing store.
    const auto first=m.gpu_view(0xB000,1).data(),second=m.gpu_view(0xC000,1).data();
    const auto before1=m.generation(first,1),before2=m.generation(second,1);
    Memory::Write64(0x8000BFFC,0x1122334455667788ull);
    check(m.generation(first,1)>before1 && m.generation(second,1)>before2);
    // Every writer route must invalidate an already queried overlapping range.
    const auto* region=m.gpu_view(0xD000,0x2000).data();
    auto version=m.generation(region,0x2000);
    auto changed=[&]{const auto next=m.generation(region,0x2000);check(next>version);version=next;check(m.generation(region,0x2000)==version);};
    m.fill(0xDFFC,8,0x55);changed();
    const std::array<uint8_t,8> bytes{9,8,7,6,5,4,3,2};
    m.load(0xE000,bytes.data(),bytes.size(),false);changed();
    m.write_bytes(0xDFFC,bytes);changed();m.copy(0xD010,0xDFFC,8,false);changed();
    // Consuming the later page's flag must not hide a write on the older page.
    Memory::Write32(0xE004,42);m.generation(region+4096,1);changed();
    Memory::Write32(0xD004,43);changed();
    auto* gpu=static_cast<uint8_t*>(m.gpu_writable(0xDFFC,8));
    m.mark_gpu_copy(0xDFFC,8);std::memcpy(gpu,bytes.data(),bytes.size());
    m.notify_gpu_write(gpu,8);changed();check(Memory::Read32(0xDFFC)==0x09080706);
    // Existing MMIO semantics are exercised through the same fast ABI entry.
    Memory::Write32(0xCC003004,0x40);check(Memory::Read32(0xCC003004)==0x40);
    rejects([&]{Memory::Read32(0xCC003001);});
    Memory::Write32(0x80003100,0x01800000);Memory::Write32(0x80003104,0x01800000);
    Memory::Write32(0x8000311C,0x04000000);Memory::Write32(0x80003120,0x93600000);
    m.configure_retail_mapping();
    const auto* reference=std::getenv("KEY_DISABLE_INLINE_RAM");
    const bool disabled=reference && std::strcmp(reference,"1")==0;
    check(disabled?key::fast_memory_view.bytes==nullptr:key::fast_memory_view.bytes==m.gpu_view(0,1).data());
    check(Memory::Read32(0xBFFC)==0x11223344);
    for(auto base:{0x10008FE0u,0x90008FE0u,0xD0008FE0u}){
        scalars<uint8_t>(base);scalars<uint16_t>(base);scalars<uint32_t>(base);scalars<uint64_t>(base);
    }
    Memory::Write32(0xD3FFFFFC,0xABCD);check(Memory::Read32(0x13FFFFFC)==0xABCD);
    rejects([&]{Memory::Read64(0x13FFFFFC);});rejects([&]{Memory::Read64(0x937FFFFC);});
    rejects([&]{Memory::Read32(0x93800000);});
    m.reset();rejects([&]{Memory::Read32(0x90005000);});
    Memory::Write32(0x80006004,13);check(Memory::Read32(0x6004)==13);
    std::puts("PASS: inline checked RAM endian/aliases, MMIO, lowmem, code, GPU hazards, generation, mapping changes and reset");
}
