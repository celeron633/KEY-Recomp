// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
namespace key {
// Diagnostic counters owned by the guest thread; they do not drive scheduling.
struct TimingCounters {
    uint64_t scheduler_wait_ns=0;
    uint64_t video_events=0;
    uint64_t game_root_polls=0;
};
inline TimingCounters timing_counters;
}
