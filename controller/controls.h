// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <string>

namespace key::controls {
void initialize(const std::string& private_root);
void shutdown();
void update();
void draw_overlay(uint64_t presented_frames);
void set_keyboard(uint16_t buttons);
bool captured();
bool menu_open();
}
