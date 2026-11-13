// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <span>
#include <cstdint>
namespace AxDspHle {
void initialize_processor(std::span<const int16_t> coefficients);
unsigned process_commands(uint32_t address,uint32_t words);
}
