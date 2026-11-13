// Synthetic translated-function ABI check; contains no game data.
#include <cstdint>
#include <cstdio>
#include "guest_abi/guest_abi.h"

extern "C" void func_80004000(CpuContext*);

int main() {
    uint32_t seed = 0x12345678u;
    const uint32_t edges[] = {0u, 1u, 0x7fffffffu, 0x80000000u, 0xffffffffu};
    unsigned cases = 0;
    auto check = [&](uint32_t a, uint32_t b) {
        CpuContext ctx{};
        for (unsigned r = 0; r < 32; ++r) ctx.gpr[r] = 0x10203040u + r;
        ctx.gpr[3] = a;
        ctx.gpr[4] = b;
        func_80004000(&ctx);
        const uint32_t expected = static_cast<uint32_t>(uint64_t(a) + uint64_t(b));
        if (ctx.gpr[3] != expected) return false;
        for (unsigned r = 0; r < 32; ++r) {
            if (r != 3 && ctx.gpr[r] != (r == 4 ? b : 0x10203040u + r)) return false;
        }
        ++cases;
        return true;
    };
    for (auto a : edges) for (auto b : edges) if (!check(a, b)) return 1;
    for (unsigned i = 0; i < 10000; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const uint32_t a = seed;
        seed = seed * 1664525u + 1013904223u;
        if (!check(a, seed)) return 2;
    }
    std::printf("PASS: %u synthetic PPC add cases and GPR preservation checks\n", cases);
}
