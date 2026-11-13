// SPDX-License-Identifier: GPL-3.0-only
#include "key/memory.h"
#include <cstdio>
#include <functional>
#include <stdexcept>

static void require(bool condition) { if (!condition) throw std::runtime_error("Test assertion failed"); }
static void fails(const std::function<void()>& operation) {
    bool failed = false;
    try { operation(); } catch (const std::runtime_error&) { failed = true; }
    require(failed);
}
int main() {
    key::Memory memory;
    memory.write(0x80005000, 0x12345678, 4);
    require(memory.read(0x5000, 1) == 0x12);
    require(memory.read(0xC0005002, 2) == 0x5678);
    memory.write(0xC0005000, 0xAABBCCDD, 4);
    require(memory.read(0x80005000, 4) == 0xAABBCCDD);
    memory.write(0x80005004, 0x123456789ABCDEF0ull, 8);
    require(memory.read(0x5004, 8) == 0x123456789ABCDEF0ull);
    require(memory.read(0x80005001,4)==0xBBCCDD12);
    memory.write(0x8000501F,0x12345678,4);
    require(memory.read(0x8000501F,4)==0x12345678);
    require(memory.read(0x80005020,2)==0x3456);
    fails([&] { memory.read(0xCC003001,4); });
    fails([&] { memory.write(0xFFFFFFFF, 1, 4); });
    fails([&] { memory.read(0x817FFFFC, 8); });
    fails([&] { memory.read(0xCC000000, 4); });
    fails([&] { memory.read(0x90000000, 4); });
    fails([&] { memory.read(0x800000F4, 4); });
    memory.write(0x800000F4, 0x1234, 2);
    require(memory.read(0xF4, 2) == 0x1234);
    fails([&] { memory.read(0x800000F4, 4); });
    memory.write(0x800000F6, 0x5678, 2);
    require(memory.read(0xC00000F4, 4) == 0x12345678);
    const uint8_t code[] = {1, 2, 3, 4};
    memory.load(0x80006000, code, 4, true);
    require(memory.read(0x6000, 4) == 0x01020304);
    fails([&] { memory.write(0xC0006000, 0, 4); });
    memory.add_template_word(0x80006000);
    fails([&] { memory.write(0x6000, 2, 4); });
    memory.template_access(true);
    memory.write(0xC0006000, 0x10203040, 4);
    require(memory.read(0x6000, 4) == 0x10203040);
    fails([&] { memory.write(0x6000, 0, 1); });
    memory.template_access(false);
    fails([&] { memory.write(0x6000, 0, 4); });
    fails([&] { memory.fill(0x80005FFF, 2, 0); });
    memory.fill(0x80005000, 4, 0xFF);
    require(memory.read(0x5000, 4) == 0xFFFFFFFF);
    memory.write(0x80005000,0x01020304,4);
    memory.copy(0x80005001,0x80005000,4,true);
    require(memory.read(0x80005001,4)==0x01020304);
    fails([&]{memory.copy(0x80005002,0x80005001,4,false);});
    fails([&]{memory.copy(0x80006000,0x80005000,4,true);});
    memory.copy(0x80005020,0x80006000,4,false);
    require(memory.read(0x80005020,4)==0x10203040);
    fails([&]{memory.copy(0x80005020,0x80000100,4,false);});
    fails([&]{memory.copy(0x817FFFFE,0x80005000,4,true);});
    memory.mark_gpu_copy(0x80007000,64);
    fails([&]{memory.read(0xC0007000,4);});
    fails([&]{memory.write(0x80007000,1,4);});
    memory.gpu_view(0x7000,64); // GPU-only texture use keeps the GPU copy authoritative.
    memory.fill(0xC0007000,64,1);require(memory.read(0x80007000,4)==0x01010101);
    memory.reset();
    require(memory.read(0x5000, 4) == 0);
    memory.write(0x6000, 1, 4);
    require(memory.read(0x6000, 4) == 1);
    std::puts("PASS: endian/aliases/alignment/bounds/MMIO/lowmem/code-write/reset");
}
