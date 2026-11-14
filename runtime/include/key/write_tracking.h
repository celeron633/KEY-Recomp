// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>
namespace key {
// Writers publish dirty pages after changing bytes. Readers serialize version
// assignment, so overlapping texture queries cannot consume each other's
// invalidation before it has been assigned a visible, monotonic version.
class WriteTracking {
public:
    static constexpr size_t page_size=4096,page_count=0x5800;
    std::atomic<uint8_t>* dirty_pages() noexcept{return dirty_.data();}
    void mark(size_t first,size_t last) {
        validate(first,last);
        for(size_t page=first;page<=last;++page)dirty_[page].store(1,std::memory_order_release);
    }
    uint64_t generation(size_t first,size_t last) {
        validate(first,last);
        std::lock_guard lock(mutex_);
        uint64_t latest=0;
        for(size_t page=first;page<=last;++page){
            if(dirty_[page].load(std::memory_order_acquire) && dirty_[page].exchange(0,std::memory_order_acq_rel))
                versions_[page]=advance();
            latest=std::max(latest,versions_[page]);
        }
        return latest;
    }
    // Memory reset requires writers to be stopped, just as replacing RAM does.
    void reset() {
        std::lock_guard lock(mutex_);
        versions_.fill(advance());
        for(auto& page:dirty_)page.store(0,std::memory_order_relaxed);
    }
private:
    alignas(64) std::array<std::atomic<uint8_t>,page_count> dirty_{};
    std::array<uint64_t,page_count> versions_{};
    uint64_t revision_=1;
    std::mutex mutex_;
    static void validate(size_t first,size_t last){
        if(first>last || last>=page_count)throw std::runtime_error("Write tracking page range outside RAM");
    }
    uint64_t advance(){
        if(revision_>=std::numeric_limits<uint64_t>::max()-1)throw std::runtime_error("Write tracking generation exhausted");
        return ++revision_;
    }
};
}
