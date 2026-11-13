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
    rejects([&]{p.mark(88*1024*1024-1,2);});
    std::puts("PASS: EFB pending-copy hazards, page crossings and complete CPU overwrites");
}
