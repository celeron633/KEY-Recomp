// SPDX-License-Identifier: GPL-3.0-only
#include "guest_abi/guest_abi.h"
#include <functional>
#include <cstdio>
#include "key/clock.h"
static void require(bool pass) { if (!pass) throw std::runtime_error("Helper test failed"); }
static void fails(const std::function<void()>& op) {
    bool failure = false;
    try { op(); } catch (const std::runtime_error&) { failure = true; }
    require(failure);
}
int main() {
    key::Decrementer timer;timer.reset(100);
    timer.write(4,100);require(timer.read(104)==0 && !timer.pending(104));
    require(timer.read(105)==0xFFFFFFFF && timer.pending(105));timer.acknowledge();require(!timer.pending(105));
    timer.write(10,106);timer.write(0x80000000,107);require(timer.pending(107));timer.acknowledge();
    require(!timer.pending(108));timer.write(0,108);require(timer.pending(109));timer.acknowledge();
    require(timer.pending(109+0x100000000ull));
    CpuContext ctx{};
    CpuContextScope scope(&ctx);
    PPC_StoreHalfwordByteReverse(0x80005001,0xABCD1234);
    require(key::memory().read(0x80005001,1)==0x34 && key::memory().read(0x80005002,1)==0x12);
    PPC_TrapWord(0,1,1);PPC_TrapWord(4,1,2);PPC_TrapWord(2,0xFFFFFFFF,0);
    fails([&]{PPC_TrapWord(4,1,1);});fails([&]{PPC_TrapWord(16,0xFFFFFFFF,0);});
    fails([&]{PPC_TrapWord(1,0xFFFFFFFF,0);});fails([&]{PPC_TrapWord(8,1,0);});
    key::memory().write(0x80005000, 0x3F80000040000000ull, 8);
    PPC_FPR pair{};
    PpcSetPairedFprInline(pair, PPC_PsqLGqrInline<0, 0>(&ctx, 0, 0x80005000));
    require(pair.paired.ps0 == 1.0f && pair.paired.ps1 == 2.0f);
    ctx.gqr[0]=0x00010000;
    fails([&] { PPC_PsqLGqrInline<0, 0>(&ctx, ctx.gqr[0], 0x80005000); });
    ctx.gqr[0]=0;
    key::memory().write(0x80005000, 0x7F80000100000000ull, 8);
    require(std::bit_cast<uint64_t>(PPC_PsqLGqrInline<0, 0>(&ctx, 0, 0x80005000))==0x7FC0000100000000ull);
    key::memory().write(0x80005000,0x7FFF,2);
    PpcSetPairedFprInline(pair,PPC_PsqLGqrInline<0,0>(&ctx,0x00040000,0x80005000));
    require(pair.paired.ps0==127 && pair.paired.ps1==255);
    MemoryInline::FlatWriteFloat32(0x80005000,1.0+3.0/33554432.0);
    require(key::memory().read(0x80005000,4)==0x3F800000);
    ctx.fpscr = 0xFFFFFFFF;
    require(std::bit_cast<uint64_t>(PPC_Mffs())==0xFFF80000FFFFFFFFull);
    PPC_Mtfsf(255, 0.0);
    require(ctx.fpscr == 0);
    fails([&] { PPC_Mtfsf(1, 0.0); });
    KEY_ResetHardware();
    require(PPC_ReadSpr(921)==0);
    PPC_WriteSpr(921,0x0C008000);
    require(PPC_ReadSpr(921)==0x0C008000);
    fails([&] { PPC_WriteSpr(921,0x1000); });
    require(PPC_ReadSpr(1017) == 0);
    PPC_WriteSpr(1017, 0x00200000);
    require(PPC_ReadSpr(1017) == 0x00200000 && !(PPC_ReadSpr(1017) & 1));
    PPC_WriteSpr(1017, 0);
    PPC_WriteSpr(1017, 0x80000000);
    require(PPC_ReadSpr(1017) == 0x80000000);
    fails([&] { PPC_WriteSpr(1017, 1); });
    KEY_ResetHardware();
    require(PPC_ReadSpr(1017) == 0);
    fails([&] { PPC_ReadSpr(999); });
    fails([&] { PPC_WriteSpr(999, 0); });
    for (auto spr : {952u, 956u}) {
        PPC_WriteSpr(spr, 0);
        require(PPC_ReadSpr(spr) == 0);
        fails([&] { PPC_WriteSpr(spr, 1); });
    }
    for (auto spr : {953u, 954u, 957u, 958u}) {
        PPC_WriteSpr(spr, 123);
        require(PPC_ReadSpr(spr) == 123);
    }
    PPC_WriteSpr(1011, 0x83900000);
    require(PPC_ReadSpr(1011) == 0x83900000);
    fails([&] { PPC_WriteSpr(1011, 0); });
    KEY_ResetHardware();
    require(PPC_ReadSpr(953) == 0);
    require(key::nanoseconds_to_ticks(1000000000) == 60750000);
    require(key::nanoseconds_to_ticks(500000000) == 30375000);
    require(key::nanoseconds_to_ticks(1000000000000ull) == 60750000000ull);
    require(key::nanoseconds_to_ticks(1) == 0);
    require(key::rtc_to_ticks(946684800) == 0);
    require(key::rtc_to_ticks(946771200) == 86400ull * key::timebase_hz);
    fails([&] { key::rtc_to_ticks(946684799); });
    fails([&] { key::rtc_to_ticks(946684800ull + 0x100000000ull); });
    key::reset_clock(1ull << 32);
    require(PPC_Mftbu() == 1);
    const auto before = key::timebase();
    require(key::timebase() >= before);
    PPC_Mtfsb1(29);
    require((ctx.fpscr & 4) != 0);
    fails([&] { PPC_Mtfsb1(32); });
    std::puts("PASS: float-pair order/FPSCR startup/unsupported-mode traps");
}
