// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <vector>
namespace key {
// A GPU-only EFB copy cannot silently look like the previous contents of RAM.
// Until synchronous readback exists, fail only accesses that need those bytes.
class PendingGpuCopies {
    static constexpr uint32_t ram_size=88*1024*1024u;
    static constexpr uint32_t page_size=4096;
    struct Range{uint32_t begin,end;};
    std::vector<Range> ranges_;
    mutable std::mutex mutex_;
    // Each page has one bit per 64-byte region. Small copies should not send
    // unrelated heap objects through the mutex-protected interval checks.
    std::array<std::atomic<uint64_t>,ram_size/page_size> pages_{};
    static uint32_t end(uint32_t address,size_t size){
        if(size>ram_size || uint64_t(address)+size>ram_size)throw std::runtime_error("GPU copy outside RAM backing");
        return address+static_cast<uint32_t>(size);
    }
    [[gnu::always_inline]] bool maybe(uint32_t a,uint32_t b)const{
        if(a==b)return false;
        for(unsigned p=a/page_size;p<=(b-1)/page_size;++p)
            if(pages_[p].load(std::memory_order_acquire)&mask(a,b,p*page_size))return true;
        return false;
    }
    static uint64_t mask(uint32_t a,uint32_t b,uint32_t page){
        const unsigned first=(std::max(a,page)-page)/64;
        const unsigned last=(std::min(b,page+page_size)-1-page)/64;
        return (UINT64_MAX<<first)&(UINT64_MAX>>(63-last));
    }
    void refresh(uint32_t a,uint32_t b){
        if(a==b)return;
        for(unsigned p=a/page_size;p<=(b-1)/page_size;++p){
            const uint32_t page=p*page_size;uint64_t flags=0;
            for(auto r:ranges_)if(r.begin<page+page_size && r.end>page)flags|=mask(r.begin,r.end,page);
            pages_[p].store(flags,std::memory_order_release);
        }
    }
public:
    const std::atomic<uint64_t>* page_masks() const noexcept {return pages_.data();}
    [[gnu::always_inline]] bool overlaps(uint32_t a,size_t size)const{
        const auto b=end(a,size);if(!maybe(a,b))return false;
        std::lock_guard lock(mutex_);
        for(auto r:ranges_)if(a<r.end && b>r.begin)return true;
        return false;
    }
    void clear(){std::lock_guard lock(mutex_);ranges_.clear();for(auto& page:pages_)page.store(0,std::memory_order_release);}
    void mark(uint32_t a,size_t size){
        const auto b=end(a,size);if(a==b)return;
        std::lock_guard lock(mutex_);
        std::erase_if(ranges_,[&](Range r){return r.begin>=a && r.end<=b;});
        if(ranges_.size()>=4096)throw std::runtime_error("Too many pending EFB copies");
        ranges_.push_back({a,b});refresh(a,b);
    }
    [[gnu::always_inline]] void read(uint32_t a,size_t size)const{
        const auto b=end(a,size);if(!maybe(a,b))return;
        std::lock_guard lock(mutex_);
        for(auto r:ranges_)if(a<r.end && b>r.begin)throw std::runtime_error("CPU read of EFB copy requires synchronous GPU readback");
    }
    [[gnu::always_inline]] void overwrite(uint32_t a,size_t size){
        const auto b=end(a,size);if(!maybe(a,b))return;
        std::lock_guard lock(mutex_);
        for(auto r:ranges_)if(a<r.end && b>r.begin && !(a<=r.begin && b>=r.end))
            throw std::runtime_error("Partial CPU overwrite of EFB copy requires synchronous GPU readback");
        // Most writes sharing a tracked page do not touch the copy itself.
        // Rebuild the index only if the set of pending intervals changed.
        if(std::erase_if(ranges_,[&](Range r){return a<=r.begin && b>=r.end;}))refresh(a,b);
    }
};
}
