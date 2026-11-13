// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
namespace key::graphics {
// Byte watermarks use the slowest supported input rate (32 kHz, stereo S16).
// Thus even a rate transition cannot exceed this duration bound.
inline constexpr int audio_queue_high_bytes=32000*4*80/1000;
inline constexpr int audio_queue_low_bytes=32000*4*40/1000;
struct AudioOutputStats {
    uint64_t submitted_bytes=0,backpressure_waits=0,waited_ns=0,resumes=0;
    uint32_t max_queued_bytes=0,device=0;
};
AudioOutputStats audio_output_stats();
}
