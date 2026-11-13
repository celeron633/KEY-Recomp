// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <span>
#include <cstdint>
namespace key::ax {
void configure_coefficients(std::span<const uint8_t> bytes);
void reset();
void boot(uint32_t address,uint32_t size,uint32_t entry);
void send_mail(uint32_t mail);
bool active();
}
