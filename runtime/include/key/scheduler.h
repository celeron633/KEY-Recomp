// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "guest_abi/cpu_context.h"
namespace key::scheduler {
using Dispatch=void(*)(uint32_t,CpuContext*);
using Pump=void(*)(CpuContext&);
void configure(Dispatch dispatch,Pump pump);
void select(CpuContext& ctx,bool yielding);
void poll(CpuContext& ctx);
void created(uint32_t thread);
void shutdown();
bool active();
}
