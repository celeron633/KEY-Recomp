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
    struct Range{uint32_t begin,end;};
    std::vector<Range> ranges_;
    mutable std::mutex mutex_;
    std::array<std::atomic<bool>,1408> pages_{}; // 88 MiB, 64 KiB coarse pages.
    static uint32_t end(uint32_t address,size_t size){
        if(size>88*1024*1024u || uint64_t(address)+size>88*1024*1024u)throw std::runtime_error("GPU copy outside RAM backing");
        return address+static_cast<uint32_t>(size);
    }
    bool maybe(uint32_t a,uint32_t b)const{
        if(a==b)return false;
        for(unsigned p=a/65536;p<=(b-1)/65536;++p)if(pages_[p].load(std::memory_order_acquire))return true;
        return false;
    }
    void refresh(){
        std::array<bool,1408> flags{};
        for(auto r:ranges_)for(unsigned p=r.begin/65536;p<=(r.end-1)/65536;++p)flags[p]=true;
        for(size_t i=0;i<flags.size();++i)pages_[i].store(flags[i],std::memory_order_release);
    }
public:
    bool overlaps(uint32_t a,size_t size)const{
        const auto b=end(a,size);if(!maybe(a,b))return false;
        std::lock_guard lock(mutex_);
        for(auto r:ranges_)if(a<r.end && b>r.begin)return true;
        return false;
    }
    void clear(){std::lock_guard lock(mutex_);ranges_.clear();refresh();}
    void mark(uint32_t a,size_t size){
        const auto b=end(a,size);if(a==b)return;
        std::lock_guard lock(mutex_);
        std::erase_if(ranges_,[&](Range r){return r.begin>=a && r.end<=b;});
        if(ranges_.size()>=4096)throw std::runtime_error("Too many pending EFB copies");
        ranges_.push_back({a,b});refresh();
    }
    void read(uint32_t a,size_t size)const{
        const auto b=end(a,size);if(!maybe(a,b))return;
        std::lock_guard lock(mutex_);
        for(auto r:ranges_)if(a<r.end && b>r.begin)throw std::runtime_error("CPU read of EFB copy requires synchronous GPU readback");
    }
    void overwrite(uint32_t a,size_t size){
        const auto b=end(a,size);if(!maybe(a,b))return;
        std::lock_guard lock(mutex_);
        for(auto r:ranges_)if(a<r.end && b>r.begin && !(a<=r.begin && b>=r.end))
            throw std::runtime_error("Partial CPU overwrite of EFB copy requires synchronous GPU readback");
        std::erase_if(ranges_,[&](Range r){return a<=r.begin && b>=r.end;});refresh();
    }
};
}
