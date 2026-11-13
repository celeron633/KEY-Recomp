// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <cstdint>
namespace key {
// Host diagnostic sequencing only: this is not a guest-visible register.
inline std::atomic<uint64_t> interrupt_ack_serial{0};
inline void note_interrupt_ack(){interrupt_ack_serial.fetch_add(1,std::memory_order_relaxed);}
inline uint64_t interrupt_acks(){return interrupt_ack_serial.load(std::memory_order_relaxed);}
}
