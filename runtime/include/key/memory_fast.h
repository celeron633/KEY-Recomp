// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "key/memory.h"
#include <atomic>
#include <bit>
#include <cstring>
#include <type_traits>
namespace key {
// Host-owned backing and policy metadata. Only the guest thread uses this view;
// boot/reset/mapping changes refresh it before translated execution resumes.
struct FastMemoryView {
    Memory* owner=nullptr;
    uint8_t* bytes=nullptr;
    const bool* mem2_mapped=nullptr;
    const uint8_t* executable_pages=nullptr;
    const std::atomic<uint64_t>* gpu_regions=nullptr;
    std::atomic<uint64_t>* revision=nullptr;
    std::atomic<uint64_t>* page_versions=nullptr;
};
extern FastMemoryView fast_memory_view;
namespace fast_memory {
template<size_t Width> [[gnu::always_inline]] inline bool offset(uint32_t address,uint32_t& result) noexcept {
    static_assert(Width==1 || Width==2 || Width==4 || Width==8);
    const auto& view=fast_memory_view;
    if(!view.bytes)return false;
    if constexpr(Width==8)if(address&3)return false;
    const auto segment=address&0xE0000000u;
    if(segment!=0 && segment!=0x80000000u && segment!=0xC0000000u)return false;
    const uint32_t physical=address&0x3FFFFFFFu;
    if(physical<Memory::mem1_size){
        if(physical<0x4000 || Width>Memory::mem1_size-physical)return false;
        result=physical;return true;
    }
    const uint32_t local=physical-0x10000000u;
    const uint32_t limit=(address&0xF0000000u)==0x90000000u?0x03800000u:0x04000000u;
    if(!*view.mem2_mapped || local>=limit || Width>limit-local)return false;
    result=Memory::mem1_size+local;return true;
}
template<size_t Width> [[gnu::always_inline]] inline bool gpu_pending(uint32_t off) noexcept {
    const auto* pages=fast_memory_view.gpu_regions;
    const uint32_t end=off+Width-1;
    const unsigned first=(off&4095)/64,last=(end&4095)/64;
    if(off/4096==end/4096)
        return (pages[off/4096].load(std::memory_order_acquire)&(UINT64_MAX<<first)&(UINT64_MAX>>(63-last)))!=0;
    return (pages[off/4096].load(std::memory_order_acquire)&(UINT64_MAX<<first)) ||
           (pages[end/4096].load(std::memory_order_acquire)&(UINT64_MAX>>(63-last)));
}
template<class T> [[gnu::always_inline]] inline T guest_order(T value) noexcept {
    static_assert(std::is_unsigned_v<T>);
    if constexpr(std::endian::native==std::endian::little){
        if constexpr(sizeof(T)==2)return __builtin_bswap16(value);
        if constexpr(sizeof(T)==4)return __builtin_bswap32(value);
        if constexpr(sizeof(T)==8)return __builtin_bswap64(value);
    }
    return value;
}
template<class T> [[gnu::always_inline]] inline T read(uint32_t address) {
    static_assert(std::is_unsigned_v<T>);
    uint32_t off;
    if(!offset<sizeof(T)>(address,off) || gpu_pending<sizeof(T)>(off))
        return static_cast<T>(memory().read(address,sizeof(T)));
    T value;std::memcpy(&value,fast_memory_view.bytes+off,sizeof(T));
    return guest_order(value);
}
template<class T> [[gnu::always_inline]] inline void write(uint32_t address,T value) {
    static_assert(std::is_unsigned_v<T>);
    uint32_t off;
    const auto& view=fast_memory_view;
    if(!offset<sizeof(T)>(address,off) || view.executable_pages[off/4096] ||
       view.executable_pages[(off+sizeof(T)-1)/4096] || gpu_pending<sizeof(T)>(off)) {
        memory().write(address,value,sizeof(T));return;
    }
    const auto stored=guest_order(value);
    std::memcpy(view.bytes+off,&stored,sizeof(T));
    const auto revision=view.revision->fetch_add(1,std::memory_order_relaxed)+1;
    view.page_versions[off/4096].store(revision,std::memory_order_release);
    if(off/4096!=(off+sizeof(T)-1)/4096)
        view.page_versions[(off+sizeof(T)-1)/4096].store(revision,std::memory_order_release);
}
}
}
