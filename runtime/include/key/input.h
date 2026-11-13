// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "guest_abi/cpu_context.h"
#include <optional>
namespace key::input {
struct Completion {uint32_t callback;int32_t channel,result;};
bool invoke(uint32_t address,CpuContext& ctx);
void poll();
void key_event(uint32_t scancode,bool down);
void release_keyboard();
void shutdown();
std::optional<Completion> next_completion();
}
