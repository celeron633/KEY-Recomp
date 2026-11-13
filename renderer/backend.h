// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <string>
#include <span>
namespace key::graphics {
void initialize(const std::string& private_root,bool hidden);
void shutdown();
bool active();
void sync();
bool empty();
void present();
void poll_events();
void submit_audio(std::span<const uint8_t> data,uint32_t rate);
void stop_audio();
void set_dimming(bool enabled);
void configure_mode(uint32_t guest_address);
uint64_t frames();
uint64_t draws();
}
