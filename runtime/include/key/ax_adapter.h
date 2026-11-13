// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "key/memory.h"
#include "isa/big_endian.h"
#include <array>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <type_traits>
namespace AxDspHle {
constexpr uint32_t kAxSamplesPerFrame=96;
constexpr size_t kResamplingCoefficientCount=0x800;
struct Memory {
    static uint16_t Read16(uint32_t a){return static_cast<uint16_t>(key::memory().read(a,2));}
    static uint32_t Read32(uint32_t a){return static_cast<uint32_t>(key::memory().read(a,4));}
};
namespace MemoryInline {inline uint16_t ByteSwap16(uint16_t v){return static_cast<uint16_t>((v>>8)|(v<<8));}}
inline uint16_t MixRead16(uint32_t a){return Memory::Read16(a);}
inline uint32_t MixRead32(uint32_t a){return Memory::Read32(a);}
inline void MixWrite16(uint32_t a,uint16_t v){key::memory().write(a,v,2);}
// Decline optional direct pointers: checked fallbacks preserve write tracking and code guards.
inline uint8_t* MixResolveRange(uint32_t,size_t){return nullptr;}
inline uint8_t ReadAramByte(uint32_t a){return static_cast<uint8_t>(key::memory().read(a,1));}
inline void WriteGuestS32Buffer(uint32_t a,const int* b,size_t n){key::memory().gpu_writable(a,n*4);for(size_t i=0;i<n;++i)key::memory().write(a+static_cast<uint32_t>(i*4),static_cast<uint32_t>(b[i]),4);}
inline void ReadGuestS32Buffer(uint32_t a,int* b,size_t n){key::memory().gpu_view(a,n*4);for(size_t i=0;i<n;++i)b[i]=static_cast<int32_t>(Memory::Read32(a+static_cast<uint32_t>(i*4)));}
}
