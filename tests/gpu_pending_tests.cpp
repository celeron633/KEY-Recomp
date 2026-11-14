// SPDX-License-Identifier: GPL-3.0-only
#include "key/gpu_pending.h"
#include <cstdio>
#include <stdexcept>
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::runtime_error&){failed=true;}if(!failed)throw std::runtime_error("Expected pending-GPU rejection");}
int main(){
    key::PendingGpuCopies p;p.mark(0x1FFF0,32);
    p.read(0x10000,8);p.read(0x20010,4);
    rejects([&]{p.read(0x20000,1);});rejects([&]{p.read(0x1FFEC,8);});
    rejects([&]{p.overwrite(0x1FFF0,4);});
    p.overwrite(0x1FFF0,32);p.read(0x20000,4);
    p.mark(0x5000,128);p.mark(0x5040,16);rejects([&]{p.overwrite(0x5040,16);});
    p.overwrite(0x4000,0x2000);p.read(0x5000,128);
    p.mark(0x5000,8);p.clear();p.read(0x5000,8);
    // Unrelated writes on the same page cannot clear either pending interval.
    p.mark(0x5100,32);p.mark(0x5300,32);
    for(unsigned i=0;i<100;++i){p.overwrite(0x5200,4);p.read(0x5200,4);}
    rejects([&]{p.read(0x5100,1);});rejects([&]{p.read(0x5300,1);});
    p.overwrite(0x5100,32);p.read(0x5100,32);
    rejects([&]{p.read(0x5300,1);});
    // Exact intervals and cross-page accesses still decide hazards, regardless
    // of the page index granularity used to reject unrelated accesses quickly.
    p.mark(0x6FF0,32);p.read(0x6FE0,16);p.read(0x7010,16);
    rejects([&]{p.read(0x6FEF,2);});rejects([&]{p.overwrite(0x7000,16);});
    p.overwrite(0x6FF0,32);p.read(0x6FF0,32);
    rejects([&]{p.read(0x5300,1);});p.clear();
    p.mark(88*1024*1024-4,4);rejects([&]{p.read(88*1024*1024-1,1);});
    p.overwrite(88*1024*1024-4,4);p.read(88*1024*1024-4,4);
    // Exercise every region bit, including both ends of a page, and all
    // accesses crossing a 64-byte region boundary. Compare with exact spans.
    for(unsigned bit=0;bit<64;++bit){
        const uint32_t start=0x10000+bit*64+31;
        p.mark(start,34);
        p.read(start-1,1);p.read(start+34,1);
        rejects([&]{p.read(start-1,2);});rejects([&]{p.read(start+33,2);});
        rejects([&]{p.overwrite(start,33);});
        p.overwrite(start,34);p.read(start,34);
    }
    rejects([&]{p.mark(88*1024*1024-1,2);});
    std::puts("PASS: EFB pending-copy hazards, page crossings and complete CPU overwrites");
}
