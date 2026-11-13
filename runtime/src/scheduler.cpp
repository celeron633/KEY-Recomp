// SPDX-License-Identifier: GPL-3.0-only
// Native continuations for RK5E01's scheduler boundary. Guest queue operations,
// mutex inheritance, creation and termination remain in translated game code.
#include "key/scheduler.h"
#include "key/memory.h"
#include <windows.h>
#include <bit>
#include <chrono>
#include <cstdio>
#include <exception>
#include <map>
#include <memory>
#include <thread>

namespace key::scheduler {
namespace {
constexpr uint32_t current_thread=0x800000E4,current_context=0x800000D4;
constexpr uint32_t inhibit=0x808E5F38,hint=0x808E5F3C,bits=0x808E5F40;
constexpr uint32_t queues=0x808FE410,idle_context=0x808FE510,switch_callback=0x808E4590;
struct Fiber { uint32_t guest=0; void* native=nullptr; CpuContext storage{}; CpuContext* cpu=nullptr; };
std::map<uint32_t,std::unique_ptr<Fiber>> fibers;
Fiber* root=nullptr;
Dispatch dispatch_guest=nullptr;
Pump pump_events=nullptr;
std::exception_ptr failure;
bool transitioning=false;
uint64_t switches=0;
uint32_t rd(uint32_t a) {return static_cast<uint32_t>(memory().read(a,4));}
void wr(uint32_t a,uint32_t v) {memory().write(a,v,4);}
void snapshot(uint32_t address,const CpuContext& c) {
    for(unsigned i=0;i<32;++i) wr(address+i*4,c.gpr[i]);
    wr(address+0x80,c.cr);wr(address+0x84,c.lr);wr(address+0x88,c.ctr);wr(address+0x8C,c.xer);
    for(unsigned i=0;i<32;++i) {memory().write(address+0x90+i*8,c.fpr[i].raw,8);memory().write(address+0x1C8+i*8,c.fpr[i].raw,8);}
    wr(address+0x194,c.fpscr);wr(address+0x198,c.lr);wr(address+0x19C,c.msr);
    memory().write(address+0x1A2,1,2);
    for(unsigned i=0;i<8;++i) wr(address+0x1A4+i*4,c.gqr[i]);
}
void set_current(uint32_t next,CpuContext& c) {
    const auto previous=rd(current_thread);
    if(previous!=next) {
        const auto callback=rd(switch_callback);
        if(!callback) throw std::runtime_error("Null thread-switch callback");
        const auto saved=c;c.gpr[3]=previous;c.gpr[4]=next;
        try {dispatch_guest(callback,&c);}catch(...) {c=saved;throw;}
        c=saved;
    }
    wr(current_thread,next);
    const auto context=next?next:idle_context;
    wr(current_context,context);wr(0x800000C0,context&0x3FFFFFFF);
    // Eager host FPU context preservation replaces the hardware's lazy FP trap.
    wr(0x800000D8,context);
}
void attach_root(uint32_t thread,CpuContext& ctx) {
    if(root) return;
    if(!thread || !dispatch_guest || !pump_events) throw std::runtime_error("Scheduler initialized without a current guest thread");
    auto f=std::make_unique<Fiber>();f->guest=thread;f->cpu=&ctx;
    if(IsThreadAFiber()) throw std::runtime_error("Host thread already belongs to a foreign fiber scheduler");
    f->native=ConvertThreadToFiberEx(nullptr,FIBER_FLAG_FLOAT_SWITCH);
    if(!f->native) throw std::runtime_error("ConvertThreadToFiberEx failed");
    root=f.get();fibers.emplace(thread,std::move(f));
}
void WINAPI run_fiber(void* arg) {
    auto& f=*static_cast<Fiber*>(arg);
    g_currentCpuContext=f.cpu;
    try {
        const auto entry=f.cpu->pc;
        std::fprintf(stderr,"THREAD start=%08X entry=%08X\n",f.guest,entry);
        dispatch_guest(entry,f.cpu);
        dispatch_guest(0x8066B720,f.cpu); // Original OSExitThread handles joins and active-list removal.
        throw std::runtime_error("Terminated guest thread returned from OSExitThread");
    } catch(...) {
        failure=std::current_exception();
        g_currentCpuContext=root->cpu;
        SwitchToFiber(root->native);
        std::terminate();
    }
}
Fiber& continuation(uint32_t thread,const CpuContext& hardware) {
    if(auto it=fibers.find(thread);it!=fibers.end())return *it->second;
    if(fibers.size()>=128)throw std::runtime_error("Native guest thread count exceeded 128");
    auto f=std::make_unique<Fiber>();f->guest=thread;f->storage=hardware;f->cpu=&f->storage;
    auto& c=*f->cpu;
    for(unsigned i=0;i<32;++i)c.gpr[i]=rd(thread+i*4);
    c.cr=rd(thread+0x80);c.lr=rd(thread+0x84);c.ctr=rd(thread+0x88);c.xer=rd(thread+0x8C);
    c.pc=rd(thread+0x198);c.msr=rd(thread+0x19C);c.fpscr=rd(thread+0x194);
    for(unsigned i=0;i<32;++i)c.fpr[i].raw=memory().read(thread+0x90+i*8,8);
    for(unsigned i=0;i<8;++i)c.gqr[i]=rd(thread+0x1A4+i*4);
    if(!c.pc || !c.gpr[1])throw std::runtime_error("Thread has no initialized entry or guest stack");
    f->native=CreateFiberEx(65536,8*1024*1024,FIBER_FLAG_FLOAT_SWITCH,&run_fiber,f.get());
    if(!f->native)throw std::runtime_error("CreateFiberEx failed");
    auto& value=*f;fibers.emplace(thread,std::move(f));return value;
}
void enqueue(uint32_t thread) {
    const auto priority=rd(thread+0x2D0);
    if(priority>31 || rd(thread+0x2CC)!=0)throw std::runtime_error("Invalid runnable thread priority/suspend count");
    const auto queue=queues+priority*8,tail=rd(queue+4);
    wr(thread+0x2DC,queue);wr(thread+0x2E0,0);wr(thread+0x2E4,tail);
    if(tail)wr(tail+0x2E0,thread);else wr(queue,thread);
    wr(queue+4,thread);wr(bits,rd(bits)|(1u<<(31-priority)));wr(hint,1);
}
}
void configure(Dispatch dispatch,Pump pump) {dispatch_guest=dispatch;pump_events=pump;}
bool active() {return root!=nullptr;}
void select(CpuContext& ctx,bool yielding) {
    if(transitioning) {ctx.gpr[3]=0;return;}
    if(static_cast<int32_t>(rd(inhibit))>0 || rd(current_context)!=rd(current_thread)) {ctx.gpr[3]=0;return;}
    const auto current=rd(current_thread);
    if(!current) throw std::runtime_error("Scheduler entered without an active continuation");
    const auto state=memory().read(current+0x2C8,2);
    if(state==2 && !yielding && rd(current+0x2D0)<=std::countl_zero(rd(bits))) {ctx.gpr[3]=0;return;}
    attach_root(current,ctx);
    struct Transition { Transition(){transitioning=true;}~Transition(){transitioning=false;} } transition;
    if(fibers.at(current)->cpu!=&ctx)throw std::runtime_error("CPU context changed under native continuation");
    if(state==2) {memory().write(current+0x2C8,1,2);enqueue(current);}
    snapshot(current,ctx);
    if(!rd(bits)) {
        set_current(0,ctx);
        const auto saved=ctx;
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!rd(bits)) {
            ctx.msr|=0x8000;
            pump_events(ctx);
            if(std::chrono::steady_clock::now()>until)throw std::runtime_error("Native scheduler idle: no runnable thread after 10 seconds");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ctx=saved;
    }
    const auto priority=std::countl_zero(rd(bits));
    if(priority>31)throw std::runtime_error("Scheduler ready bitmap is empty");
    const auto queue=queues+priority*8,next=rd(queue);
    if(!next || rd(next+0x2DC)!=queue || memory().read(next+0x2C8,2)!=1 || rd(next+0x2CC)!=0)
        throw std::runtime_error("Guest ready queue/bitmap mismatch");
    const auto successor=rd(next+0x2E0);
    wr(queue,successor);if(successor)wr(successor+0x2E4,0);else {wr(queue+4,0);wr(bits,rd(bits)&~(1u<<(31-priority)));}
    wr(next+0x2DC,0);memory().write(next+0x2C8,2,2);wr(hint,0);
    set_current(next,ctx);
    if(next!=current) {
        auto& target=continuation(next,ctx);
        if(++switches<=64 || switches%500==0)std::fprintf(stderr,"THREAD switch %08X -> %08X\n",current,next);
        g_currentCpuContext=target.cpu;
        transitioning=false;
        SwitchToFiber(target.native);
        transitioning=true;
        g_currentCpuContext=&ctx;
        if(failure)std::rethrow_exception(failure);
        if(rd(current_thread)!=current)throw std::runtime_error("Resumed fiber is not the selected guest thread");
        MkwApplyHostNiMode(ctx.fpscr);
    }
    ctx.gpr[3]=0;
}
void created(uint32_t thread) {
    const auto it=fibers.find(thread);if(it==fibers.end())return;
    if(it->second.get()==root || GetCurrentFiber()==it->second->native)throw std::runtime_error("Recreated active native thread");
    DeleteFiber(it->second->native);fibers.erase(it);
}
void poll(CpuContext& ctx) {
    if(!root || transitioning || !(ctx.msr&0x8000) || !rd(hint))return;
    const auto saved=ctx;ctx.msr&=~0x8000u;
    try {select(ctx,false);}catch(...){ctx=saved;throw;}
    ctx=saved;
}
void shutdown() {
    if(!root)return;
    if(GetCurrentFiber()!=root->native)throw std::runtime_error("Scheduler cleanup outside root continuation");
    for(auto& [address,f]:fibers)if(f.get()!=root)DeleteFiber(f->native);
    ConvertFiberToThread();fibers.clear();root=nullptr;failure=nullptr;
}
}
