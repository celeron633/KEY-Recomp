// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "key/memory.h"
#include "key/memory_fast.h"
#include "key/fp_store.h"
#include <bit>
#include <cstdint>
#include <type_traits>
extern "C" void GX_HLE_FIFO_Write8(uint8_t);
extern "C" void GX_HLE_FIFO_Write16(uint16_t);
extern "C" void GX_HLE_FIFO_Write32(uint32_t);
extern "C" void GX_HLE_FIFO_WriteFloat(double);
extern "C" void GX_HLE_FIFO_WriteBurst(const uint8_t*,uint32_t);
// Out-of-line access spelling emitted for volatile/MMIO loads; same checked policy.
class Memory {
public:
    [[gnu::always_inline]] static uint8_t Read8(uint32_t a) { return static_cast<uint8_t>(key::fast_memory::read<uint8_t>(a)); }
    [[gnu::always_inline]] static uint16_t Read16(uint32_t a) { return static_cast<uint16_t>(key::fast_memory::read<uint16_t>(a)); }
    [[gnu::always_inline]] static uint32_t Read32(uint32_t a) { return static_cast<uint32_t>(key::fast_memory::read<uint32_t>(a)); }
    [[gnu::always_inline]] static uint64_t Read64(uint32_t a) { return key::fast_memory::read<uint64_t>(a); }
    [[gnu::always_inline]] static void Write8(uint32_t a,uint8_t v) { key::fast_memory::write<uint8_t>(a,v); }
    [[gnu::always_inline]] static void Write16(uint32_t a,uint16_t v) { key::fast_memory::write<uint16_t>(a,v); }
    [[gnu::always_inline]] static void Write32(uint32_t a,uint32_t v) { key::fast_memory::write<uint32_t>(a,v); }
    [[gnu::always_inline]] static void Write64(uint32_t a,uint64_t v) { key::fast_memory::write<uint64_t>(a,v); }
};
namespace GuestFlat {
[[gnu::always_inline]] inline constexpr bool RequiresCheckedAccess() noexcept { return true; }
[[gnu::always_inline]] inline uint8_t* DisabledFastBase() { return nullptr; }
}
#define MKW_FLAT_GUEST_BASE (::GuestFlat::DisabledFastBase())
namespace MemoryInline {
[[gnu::always_inline]] inline void RequireFloatAlignment(uint32_t a) { if(a%4) throw std::runtime_error("Unaligned scalar floating-point access"); }
[[gnu::always_inline]] inline bool FlatWriteNeedsPolicy(uint32_t) { return true; }
[[gnu::always_inline]] inline bool TryGetPointerFast(uint32_t,size_t,uint8_t*& p) { p=nullptr; return false; }
[[gnu::always_inline]] inline bool TryGetWritablePointerFast(uint32_t,size_t,uint8_t*& p) { p=nullptr; return false; }
template<class T> [[gnu::always_inline]] inline T ReadResolvedFallback(uint32_t a) { static_assert(std::is_integral_v<T>); return static_cast<T>(key::fast_memory::read<std::make_unsigned_t<T>>(a)); }
template<class T> [[gnu::always_inline]] inline void WriteResolvedFallback(uint32_t a,T v) { static_assert(std::is_integral_v<T>); key::fast_memory::write(a,static_cast<std::make_unsigned_t<T>>(v)); }
[[gnu::always_inline]] inline uint8_t ReadStack8(uint32_t a) { return Memory::Read8(a); }
[[gnu::always_inline]] inline uint16_t ReadStack16(uint32_t a) { return Memory::Read16(a); }
[[gnu::always_inline]] inline uint32_t ReadStack32(uint32_t a) { return Memory::Read32(a); }
[[gnu::always_inline]] inline uint64_t ReadStack64(uint32_t a) { return Memory::Read64(a); }
[[gnu::always_inline]] inline void WriteStack8(uint32_t a,uint8_t v) { Memory::Write8(a,v); }
[[gnu::always_inline]] inline void WriteStack16(uint32_t a,uint16_t v) { Memory::Write16(a,v); }
[[gnu::always_inline]] inline void WriteStack32(uint32_t a,uint32_t v) { Memory::Write32(a,v); }
[[gnu::always_inline]] inline void WriteStack64(uint32_t a,uint64_t v) { Memory::Write64(a,v); }
// Decline fast-pointer optimization; generated code executes checked fallback.
[[gnu::always_inline]] inline uint8_t* ResolveRangeHost(uint32_t, int32_t, uint32_t, bool, bool) { return nullptr; }
[[gnu::always_inline]] inline bool WriteResolvedPair32(uint8_t*, uint32_t, uint64_t) { return false; }
[[gnu::always_inline]] inline bool WriteResolvedPair16(uint8_t*, uint32_t, uint32_t) { return false; }
struct CheckedPairFallback { bool valid; uint32_t first; uint32_t second; };
[[gnu::always_inline]] inline CheckedPairFallback ReadResolvedPair32(const uint8_t*, uint32_t) { return {false, 0, 0}; }
[[gnu::always_inline]] inline CheckedPairFallback ReadResolvedPair16(const uint8_t*, uint32_t) { return {false, 0, 0}; }
[[gnu::always_inline]] inline uint32_t ReadResolved32(const uint8_t*, uint32_t, uint32_t address) { return static_cast<uint32_t>(key::fast_memory::read<uint32_t>(address)); }
[[gnu::always_inline]] inline uint8_t ReadResolved8(const uint8_t*, uint32_t, uint32_t address) { return static_cast<uint8_t>(key::fast_memory::read<uint8_t>(address)); }
[[gnu::always_inline]] inline uint16_t ReadResolved16(const uint8_t*, uint32_t, uint32_t address) { return static_cast<uint16_t>(key::fast_memory::read<uint16_t>(address)); }
[[gnu::always_inline]] inline uint64_t ReadResolved64(const uint8_t*, uint32_t, uint32_t address) { return key::fast_memory::read<uint64_t>(address); }
[[gnu::always_inline]] inline float ReadResolvedFloat32(const uint8_t*,uint32_t,uint32_t a) { RequireFloatAlignment(a); return std::bit_cast<float>(Memory::Read32(a)); }
[[gnu::always_inline]] inline double ReadResolvedFloat64(const uint8_t*,uint32_t,uint32_t a) { return std::bit_cast<double>(Memory::Read64(a)); }
[[gnu::always_inline]] inline void WriteResolved32(uint8_t*, uint32_t, uint32_t address, uint32_t value) { key::fast_memory::write<uint32_t>(address,value); }
[[gnu::always_inline]] inline void WriteResolved8(uint8_t*, uint32_t, uint32_t address, uint8_t value) { key::fast_memory::write<uint8_t>(address,value); }
[[gnu::always_inline]] inline void WriteResolved16(uint8_t*, uint32_t, uint32_t address, uint16_t value) { key::fast_memory::write<uint16_t>(address,value); }
[[gnu::always_inline]] inline void WriteResolved64(uint8_t*, uint32_t, uint32_t address, uint64_t value) { key::fast_memory::write<uint64_t>(address,value); }
[[gnu::always_inline]] inline void WriteResolvedFloat32(uint8_t*,uint32_t,uint32_t a,double v) { RequireFloatAlignment(a); Memory::Write32(a,key::single_store_bits(v)); }
[[gnu::always_inline]] inline void WriteResolvedFloat64(uint8_t*,uint32_t,uint32_t a,double v) { Memory::Write64(a,std::bit_cast<uint64_t>(v)); }
[[gnu::always_inline]] inline uint8_t FlatRead8(uint32_t a) { return static_cast<uint8_t>(key::fast_memory::read<uint8_t>(a)); }
[[gnu::always_inline]] inline uint16_t FlatRead16(uint32_t a) { return static_cast<uint16_t>(key::fast_memory::read<uint16_t>(a)); }
[[gnu::always_inline]] inline uint32_t FlatRead32(uint32_t a) { return static_cast<uint32_t>(key::fast_memory::read<uint32_t>(a)); }
[[gnu::always_inline]] inline uint64_t FlatRead64(uint32_t a) { return key::fast_memory::read<uint64_t>(a); }
[[gnu::always_inline]] inline float FlatReadFloat32(uint32_t a) { RequireFloatAlignment(a); return std::bit_cast<float>(FlatRead32(a)); }
[[gnu::always_inline]] inline double FlatReadFloat64(uint32_t a) { return std::bit_cast<double>(key::fast_memory::read<uint64_t>(a)); }
[[gnu::always_inline]] inline void FlatWrite8(uint32_t a, uint8_t v) { key::fast_memory::write<uint8_t>(a,v); }
[[gnu::always_inline]] inline void FlatWrite16(uint32_t a, uint16_t v) { key::fast_memory::write<uint16_t>(a,v); }
[[gnu::always_inline]] inline void FlatWriteRam8(uint32_t a, uint8_t v) { FlatWrite8(a, v); }
[[gnu::always_inline]] inline void FlatWriteRam16(uint32_t a, uint16_t v) { FlatWrite16(a, v); }
[[gnu::always_inline]] inline void FlatWrite32(uint32_t a, uint32_t v) { key::fast_memory::write<uint32_t>(a,v); }
[[gnu::always_inline]] inline void FlatWriteRam32(uint32_t a, uint32_t v) { FlatWrite32(a, v); }
[[gnu::always_inline]] inline void FlatWriteFloat64(uint32_t a, double v) { key::fast_memory::write<uint64_t>(a,std::bit_cast<uint64_t>(v)); }
[[gnu::always_inline]] inline void FlatWrite64(uint32_t a,uint64_t v) { key::fast_memory::write<uint64_t>(a,v); }
[[gnu::always_inline]] inline void FlatWriteRam64(uint32_t a,uint64_t v) { FlatWrite64(a,v); }
[[gnu::always_inline]] inline void FlatWriteFloat32(uint32_t a,double v) { RequireFloatAlignment(a); key::fast_memory::write<uint32_t>(a,key::single_store_bits(v)); }
[[gnu::always_inline]] inline void FlatWriteRamFloat32(uint32_t a,double v) { FlatWriteFloat32(a,v); }
[[gnu::always_inline]] inline void FlatWriteRamFloat64(uint32_t a,double v) { FlatWriteFloat64(a,v); }
}
