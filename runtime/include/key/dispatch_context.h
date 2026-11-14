// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "guest_abi/cpu_context.h"

namespace key {
// Dispatch owns this scope on the guest thread. Keep the reference scope's
// host-FP boundary contract, including nested calls using the same context.
class DispatchContextScope {
    CpuContext* previous_;
    uint32_t saved_control_;

    [[gnu::always_inline]] static inline void apply_ni(uint32_t fpscr,uint32_t control) noexcept {
        const bool enabled=(fpscr&4)!=0;
        const uint32_t desired=(control&~kMkwFpControlFlushToZeroBits) |
            (enabled?kMkwFpControlFlushToZeroBits:0);
        if(desired!=control)MkwSetHostFpControl(desired);
        g_mkwHostNiActive=enabled;
        g_mkwNiFlushThreshold=enabled?kMkwNiFlushThreshold:0.0;
    }
public:
    [[gnu::always_inline]] explicit DispatchContextScope(CpuContext* ctx) noexcept
        :previous_(g_currentCpuContext),saved_control_(MkwGetHostFpControl()) {
        g_currentCpuContext=ctx;
        // Reuse the control word already saved at entry instead of reading
        // MXCSR a second time. No guest/host work occurs between these steps.
        if(ctx)apply_ni(ctx->fpscr,saved_control_);
    }
    [[gnu::always_inline]] ~DispatchContextScope() noexcept {
        g_currentCpuContext=previous_;
        // Read the live FPSCR and host control: callees may change either.
        // Pointer equality alone cannot justify skipping this restoration.
        if(previous_)apply_ni(previous_->fpscr,MkwGetHostFpControl());
        else MkwRestoreHostMxcsr(saved_control_);
    }
    DispatchContextScope(const DispatchContextScope&)=delete;
    DispatchContextScope& operator=(const DispatchContextScope&)=delete;
};
}
