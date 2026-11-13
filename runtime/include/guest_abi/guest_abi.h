// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "isa/ppc_isa_config.h"
#include "guest_abi/cpu_context.h"
#include "isa/ppc_isa_int.h"
#include "isa/ppc_isa_cr.h"
#include "guest_abi/guest_cr.h"
#include "isa/ppc_isa_float.h"
#include "key/guest_memory_abi.h"
#include <tuple>
namespace key { void dispatch(uint32_t address, CpuContext* ctx); }
// Disable optional typed-call optimization; execute the full CpuContext path.
template<uint32_t> struct KnownTranslatedCpuCall {
    static constexpr bool kAvailable = false;
    static constexpr bool kMustRemainDynamicallyDispatchable = true;
};
template<uint32_t> inline bool IsBaseTranslatedCpuTargetActive() { return false; }
template<uint32_t Address> inline void InvokeDirectCpu(CpuContext* ctx) { key::dispatch(Address, ctx); }
inline void InvokeIndirectCpu(uint32_t address, CpuContext* ctx) { key::dispatch(address, ctx); }
inline void InvokeIndirectJump(uint32_t address, CpuContext* ctx) { key::dispatch(address, ctx); }


#include "isa/ppc_isa_quantized.h"
extern "C" void KEY_ResetHardware();
