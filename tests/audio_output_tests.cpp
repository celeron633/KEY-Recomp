// SPDX-License-Identifier: GPL-3.0-only
#include "backend.h"
#include "audio_output.h"
#include "key/clock.h"
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
static uint64_t polls=0;
namespace key::graphics {void poll_events(){++polls;}}
static void check(bool ok){if(!ok)throw std::runtime_error("Audio output backpressure test failed");}
int main(){
    try{
        // CTest selects SDL's dummy backend, never the user's physical device.
        std::array<uint8_t,32> pcm{};key::reset_clock();
        key::graphics::submit_audio(pcm,32000);
        const auto start=std::chrono::steady_clock::now();const auto guest=key::timebase();
        constexpr unsigned blocks=12000; // Three seconds submitted as fast as possible; old code failed at two.
        for(unsigned i=0;i<blocks;++i)key::graphics::submit_audio(pcm,32000);
        const auto stats=key::graphics::audio_output_stats();
        const auto host_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        const auto guest_seconds=double(key::timebase()-guest)/key::timebase_hz;
        check(stats.submitted_bytes==uint64_t(blocks+1)*32 && stats.backpressure_waits>0 && polls>0);
        check(stats.max_queued_bytes<=key::graphics::audio_queue_high_bytes);
        check(stats.waited_ns>1000000000ull && std::abs(host_seconds-guest_seconds-double(stats.waited_ns)/1e9)<0.15);
        // A stopped logical device is resumed instead of turning its queue into
        // a fatal overflow. Every PCM block must still be submitted exactly once.
        check(SDL_PauseAudioDevice(stats.device));
        for(unsigned i=0;i<1000;++i)key::graphics::submit_audio(pcm,32000);
        const auto resumed=key::graphics::audio_output_stats();
        check(resumed.resumes==1 && !SDL_AudioDevicePaused(resumed.device));
        check(resumed.submitted_bytes==uint64_t(blocks+1001)*32);
        // Format transitions remain bounded without clearing old-format audio.
        for(unsigned i=0;i<1000;++i)key::graphics::submit_audio(pcm,48000);
        const auto changed=key::graphics::audio_output_stats();
        check(changed.submitted_bytes==uint64_t(blocks+2001)*32 && changed.max_queued_bytes<=key::graphics::audio_queue_high_bytes);
        // A genuinely lost binding must still be diagnosed, not acknowledged.
        SDL_CloseAudioDevice(changed.device);bool failed=false;
        try{for(unsigned i=0;i<1000;++i)key::graphics::submit_audio(pcm,48000);}catch(const std::runtime_error&){failed=true;}
        check(failed);key::graphics::stop_audio();SDL_QuitSubSystem(SDL_INIT_AUDIO);
        std::puts("PASS: >2s PCM burst retained, bounded queue, paused-device recovery, rate change and lost-device diagnostic");
    }catch(const std::exception& e){std::fprintf(stderr,"Audio output test: %s\n",e.what());key::graphics::stop_audio();return 1;}
}
