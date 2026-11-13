// SPDX-License-Identifier: GPL-3.0-only
#include "guest_abi/guest_abi.h"
#include <cstdio>
#include "key/clock.h"
#include <atomic>
// Dolphin's interpreter stores L2CR writes without asynchronous cache work.
// This probe has coherent backing RAM, no DMA and rejects executable writes.
static thread_local uint32_t l2cr = 0;
static thread_local uint32_t wpar = 0;
static thread_local uint32_t hid4 = 0x83900000;
static thread_local uint32_t counters[4]{};
static int counter_index(uint32_t spr) {
    switch (spr) { case 953: return 0; case 954: return 1; case 957: return 2; case 958: return 3; default: return -1; }
}
extern "C" void PPC_TrapWord(uint32_t options,uint32_t lhs,uint32_t rhs) {
    if(options>31)throw std::runtime_error("Invalid PowerPC trap option mask");
    const auto a=static_cast<int32_t>(lhs),b=static_cast<int32_t>(rhs);
    if(((options&16)&&a<b)||((options&8)&&a>b)||((options&4)&&lhs==rhs)||((options&2)&&lhs<rhs)||((options&1)&&lhs>rhs))
        throw std::runtime_error("Guest PowerPC conditional trap raised");
}
extern "C" void PPC_StoreHalfwordByteReverse(uint32_t address,uint32_t value) {
    key::memory().write(address,static_cast<uint16_t>(((value&255)<<8)|((value>>8)&255)),2);
}
extern "C" void KEY_ResetHardware() { l2cr = 0; wpar = 0; hid4 = 0x83900000; for (auto& v : counters) v=0; key::reset_clock(); }
extern "C" uint32_t PPC_Mftb() { return static_cast<uint32_t>(key::timebase()); }
extern "C" uint32_t PPC_Mftbu() { return static_cast<uint32_t>(key::timebase() >> 32); }
extern "C" int32_t memset_zero_32(int32_t address) {
    const auto aligned=static_cast<uint32_t>(address)&~31u;
    key::memory().fill(aligned,32,0);
    return address;
}
extern "C" uint32_t OSSystemCall() {
    auto* ctx=TryGetCpuContext();
    if(!ctx) throw std::runtime_error("System-call helper without CPU context");
    for(unsigned i=0;i<28;++i)
        if(key::memory().read(0x80000C00+i,1)!=key::memory().read(0x8066AB80+i,1))
            throw std::runtime_error("System call vector differs from validated cache-sync handler");
    // Native service for this one known cache-sync handler, not a generic sc/rfi interpreter.
    ctx->gpr[9]=ctx->hid0;
    ctx->gpr[10]=ctx->hid0|8;
    ctx->hid0=ctx->gpr[10];
    std::atomic_thread_fence(std::memory_order_seq_cst);
    ctx->hid0=ctx->gpr[9];
    return ctx->gpr[3];
}
extern "C" void PPC_Mtfsb1(uint32_t bit) {
    auto* ctx = TryGetCpuContext();
    if (!ctx || bit != 29) throw std::runtime_error("Unvalidated FPSCR bit set");
    ctx->fpscr |= 4;
    MkwApplyHostNiMode(ctx->fpscr);
}
extern "C" double PPC_Mffs() {
    const auto* ctx=TryGetCpuContext();
    if(!ctx)throw std::runtime_error("FPSCR read without CPU context");
    // Gekko mffs observed upper bits, as implemented by Dolphin's interpreter.
    return std::bit_cast<double>(0xFFF8000000000000ull|ctx->fpscr);
}

extern "C" void PPC_Mtfsf(uint32_t fields, double source) {
    auto* ctx = TryGetCpuContext();
    if (!ctx) throw std::runtime_error("FPSCR update without CPU context");
    const auto bits = static_cast<uint32_t>(std::bit_cast<uint64_t>(source));
    if (fields != 255 || bits != 0) throw std::runtime_error("Only full FPSCR zero initialization is validated");
    // Probe targets x64. CpuContextScope saves/restores MXCSR around dispatch.
    // Avoid changing the x87 control word, which that scope does not own.
    MkwRestoreHostMxcsr(MkwGetHostFpControl() & ~(3u << 13));
    ctx->fpscr = 0;
    MkwApplyHostNiMode(0);
}
extern "C" uint32_t PPC_ReadSpr(uint32_t spr) {
    if(spr==22)return key::decrementer().read(key::timebase());
    if (spr == 921) return wpar; // Writes enter the native command bridge synchronously; no CPU gather queue remains.
    if (spr == 1017) return l2cr;
    if (spr == 1011) return hid4;
    if (spr == 952 || spr == 956) return 0; // Event counting is disabled; enabling traps below.
    if (const int i = counter_index(spr); i >= 0) return counters[i];
    char error[100];
    std::snprintf(error, sizeof(error), "Unimplemented SPR read %u (hardware contract required)", spr);
    throw std::runtime_error(error);
}
extern "C" void PPC_WriteSpr(uint32_t spr, uint32_t value) {
    if(spr==22){key::decrementer().write(value,key::timebase());return;}
    if (spr == 921) {
        if(value!=0x0C008000) throw std::runtime_error("Unsupported write-gather pipe destination");
        wpar=value;
        return;
    }
    if (spr == 952 || spr == 956) {
        if (value != 0) throw std::runtime_error("Performance counter event selection not implemented");
        return;
    }
    if (const int i = counter_index(spr); i >= 0) { counters[i] = value; return; }
    if (spr == 1011) {
        if (value != 0x83900000) throw std::runtime_error("Unvalidated HID4 mode");
        hid4 = value;
        return;
    }
    if (spr == 1017) {
        if (value & ~0x80200000u) throw std::runtime_error("Unvalidated L2CR configuration bits");
        l2cr = value; // L2IP is zero: invalidation completes immediately in coherent probe RAM.
        return;
    }
    char error[100];
    std::snprintf(error, sizeof(error), "Unimplemented SPR write %u (hardware contract required)", spr);
    throw std::runtime_error(error);
}
