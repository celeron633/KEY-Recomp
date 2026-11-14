// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <map>

namespace key {
// The diagnostic limit needs a depth, not an address vector. Map nodes remain
// stable across reentrant calls and suspended fibers using other contexts.
template<class Context> class DispatchDepths {
    std::map<Context*,uint32_t> depths_;
    Context* cached_context_=nullptr;
    uint32_t* cached_depth_=nullptr;
public:
    [[gnu::always_inline]] uint32_t& get(Context* context) {
        if(cached_depth_ && cached_context_==context)return *cached_depth_;
        cached_depth_=&depths_[context];
        cached_context_=context;
        return *cached_depth_;
    }
};
}
