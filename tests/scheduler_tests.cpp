// SPDX-License-Identifier: GPL-3.0-only
#include "key/scheduler.h"
#include "key/memory.h"
#include <cstdio>
#include <stdexcept>
#include <string>
namespace {
constexpr uint32_t main_thread=0x80005000,worker=0x80006000;
constexpr uint32_t queues=0x808FE410,bits=0x808E5F40;
unsigned progress=0,pumps=0;
void require(bool v) {if(!v)throw std::runtime_error("Native continuation test failed");}
void wr(uint32_t a,uint32_t v){key::memory().write(a,v,4);}
void enqueue(uint32_t t,uint32_t p) {
    const auto q=queues+p*8;
    wr(t+0x2D0,p);wr(t+0x2CC,0);key::memory().write(t+0x2C8,1,2);
    wr(t+0x2DC,q);wr(t+0x2E0,0);wr(t+0x2E4,0);wr(q,t);wr(q+4,t);
    wr(bits,static_cast<uint32_t>(key::memory().read(bits,4))|(1u<<(31-p)));
}
void pump(CpuContext& c) {
    ++pumps;require(c.msr&0x8000);require(key::memory().read(0x800000E4,4)==0);
    enqueue(main_thread,31);
}
void dispatch(uint32_t address,CpuContext* c) {
    CpuContextScope scope(c);
    if(address==0x80009000)return; // Synthetic switch callback.
    if(address==0x80009200)throw std::runtime_error("synthetic worker failure");
    if(address==0x8066B720) {
        key::memory().write(worker+0x2C8,8,2);
        key::scheduler::select(*c,false);
        throw std::runtime_error("Exited worker resumed");
    }
    require(address==0x80009100 && c->gpr[3]==123 && c->gpr[1]==0x80012000);
    volatile uint32_t local=0x12345678;
    c->gpr[20]=0xAABBCCDD;c->fpr[14].raw=0x4004000000000000ull;c->gqr[3]=0x1234;
    progress=1;wr(worker+0x2D0,24);
    key::scheduler::select(*c,false);
    require(local==0x12345678 && c->gpr[20]==0xAABBCCDD && c->fpr[14].raw==0x4004000000000000ull && c->gqr[3]==0x1234);
    require(TryGetCpuContext()==c);progress=2;
}
}
int main() {
    try {
        key::memory().reset();key::memory().fill(0x80000000,0x4000,0);
        key::scheduler::configure(&dispatch,&pump);
        wr(0x800000E4,main_thread);wr(0x800000D4,main_thread);wr(0x808E4590,0x80009000);
        key::memory().write(main_thread+0x2C8,2,2);wr(main_thread+0x2D0,16);
        wr(worker+0x198,0x80009100);wr(worker+4,0x80012000);wr(worker+12,123);wr(worker+0x19C,0xA032);
        enqueue(worker,8);
        CpuContext root{};root.gpr[1]=0x80010000;root.gpr[20]=0x5678;root.msr=0x2032;
        CpuContextScope scope(&root);
        key::scheduler::select(root,false);
        require(progress==1 && root.gpr[20]==0x5678 && TryGetCpuContext()==&root);
        wr(main_thread+0x2D0,31);
        key::scheduler::select(root,false);
        require(progress==2 && key::memory().read(worker+0x2C8,2)==8);
        key::memory().write(main_thread+0x2C8,4,2);
        key::scheduler::select(root,false);
        require(pumps==1 && key::memory().read(main_thread+0x2C8,2)==2 && key::memory().read(0x800000D4,4)==main_thread);
        key::scheduler::created(worker);
        wr(worker+0x198,0x80009200);enqueue(worker,8);
        bool caught=false;
        try {key::scheduler::select(root,false);}catch(const std::runtime_error& e){caught=std::string(e.what())=="synthetic worker failure";}
        require(caught);key::scheduler::shutdown();
        std::puts("PASS: guest priorities, fiber locals/GPR/FP/GQR, idle wakeup, exit, reuse and worker failure propagation");
    } catch(const std::exception& e) {std::fprintf(stderr,"Scheduler test: %s\n",e.what());key::scheduler::shutdown();return 1;}
}
