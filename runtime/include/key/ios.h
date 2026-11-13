// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "guest_abi/cpu_context.h"
#include <cstdint>
#include <optional>
#include <string>
namespace key::ios {
struct Completion { uint32_t callback, argument; int32_t result; };
void reset();
bool invoke(uint32_t address, CpuContext& ctx);
std::optional<Completion> next_completion();
void stm_event(uint32_t event);
void set_filesystem_root(std::string path);
void set_disc_root(std::string path,std::string layout_path);
bool video_dimming();
}
