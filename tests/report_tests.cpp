// SPDX-License-Identifier: GPL-3.0-only
#include "key/report.h"
#include "key/memory.h"
#include <cstdio>
static void put(uint32_t a,const char* s) { do{key::memory().write(a++,static_cast<unsigned char>(*s),1);}while(*s++); }
int main() {
    CpuContext c{}; c.gpr[1]=0x80009000; c.gpr[3]=0x80005000;
    put(c.gpr[3],"%s %08x %d %%"); put(0x80006000,"test");
    c.gpr[4]=0x80006000; c.gpr[5]=0x1234; c.gpr[6]=0xFFFFFFFF;
    if(key::format_report(c)!="test 00001234 -1 %") return 1;
    put(c.gpr[3],"%n");
    try { key::format_report(c); return 2; } catch(const std::runtime_error&) {}
    put(c.gpr[3],"%u%u%u%u%u%u%u:%u");
    for(unsigned i=4;i<=10;++i) c.gpr[i]=i;
    key::memory().write(c.gpr[1]+8,99,4);
    if(key::format_report(c)!="45678910:99") return 3;
    std::puts("PASS: guest report formatting, stack arguments and unsafe format rejection");
}
