// SPDX-License-Identifier: GPL-3.0-only
// Adapted from WiiCompiled guest_abi/memory_access.h at references.lock.json's revision.
#pragma once
#include <bit>
#include <cstdint>
namespace key {
inline uint32_t single_store_bits(double value) {
    const auto bits=std::bit_cast<uint64_t>(value);
    const auto exponent=static_cast<uint32_t>((bits>>52)&0x7FF);
    if(exponent-874u<=22u) {
        uint32_t narrowed=static_cast<uint32_t>(0x80000000ull|((bits&0x000FFFFFFFFFFFFFull)>>21));
        narrowed>>=(905u-exponent);
        return narrowed|static_cast<uint32_t>((bits>>32)&0x80000000ull);
    }
    return static_cast<uint32_t>(((bits>>32)&0xC0000000ull)|((bits>>29)&0x3FFFFFFFull));
}
}
