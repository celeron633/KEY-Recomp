// SPDX-License-Identifier: GPL-3.0-only
#include "backend.h"
#include "audio_output.h"
#include "key/clock.h"
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_error.h>
#include <array>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <cstdio>
#include <chrono>
#include <thread>
namespace key::graphics {
namespace {
SDL_AudioStream* stream=nullptr;uint32_t active_rate=0;
AudioOutputStats stats{};
int queued_bytes(){
    const auto queued=SDL_GetAudioStreamQueued(stream);
    if(queued<0)throw std::runtime_error(std::string("Cannot query native PCM queue: ")+SDL_GetError());
    stats.max_queued_bytes=std::max(stats.max_queued_bytes,static_cast<uint32_t>(queued));
    return queued;
}
void wait_for_output(int queued){
    if(queued+32<=audio_queue_high_bytes)return;
    const int initial=queued;
    const auto start=std::chrono::steady_clock::now();
    auto progress=start;int last=queued;
    key::ClockPause pause;
    ++stats.backpressure_waits;
    // Paused/migrating output is different from a queue-full condition. Resume
    // a valid logical device and wait without throwing away any queued samples.
    const auto device=SDL_GetAudioStreamDevice(stream);
    if(!device)throw std::runtime_error("PCM stream is no longer bound to an audio device");
    if(SDL_AudioDevicePaused(device)){
        if(!SDL_ResumeAudioStreamDevice(stream))throw std::runtime_error(std::string("Cannot resume native PCM device: ")+SDL_GetError());
        ++stats.resumes;
    }
    while(queued>audio_queue_low_bytes){
        poll_events(); // Collect window/input events; no guest callbacks run here.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        queued=queued_bytes();const auto now=std::chrono::steady_clock::now();
        if(queued<last){last=queued;progress=now;}
        if(now-progress>std::chrono::seconds(2)){
            char error[160];std::snprintf(error,sizeof(error),"Native audio device stopped consuming PCM (device=%u, queued=%d bytes)",device,queued);
            throw std::runtime_error(error);
        }
    }
    const auto waited=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    stats.waited_ns+=static_cast<uint64_t>(waited);
    if(stats.backpressure_waits<=4 || stats.backpressure_waits%64==0)
        std::fprintf(stderr,"AUDIO output paced: queue %d -> %d bytes, host wait %.2f ms (guest clock held; no samples dropped)\n",initial,queued,waited/1e6);
}
}
AudioOutputStats audio_output_stats(){return stats;}
void submit_audio(std::span<const uint8_t> data,uint32_t rate){
    if(data.size()!=32 || (rate!=32000 && rate!=48000))throw std::runtime_error("Invalid AI PCM block");
    if(!stream){
        if(!SDL_InitSubSystem(SDL_INIT_AUDIO))throw std::runtime_error(std::string("SDL audio initialization: ")+SDL_GetError());
        SDL_AudioSpec format{SDL_AUDIO_S16,2,static_cast<int>(rate)};
        stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&format,nullptr,nullptr);
        if(!stream || !SDL_ResumeAudioStreamDevice(stream))throw std::runtime_error(std::string("Native audio device: ")+SDL_GetError());
        active_rate=rate;stats={};stats.device=SDL_GetAudioStreamDevice(stream);std::fprintf(stderr,"AUDIO native stereo PCM device initialized at %u Hz\n",rate);
    }
    if(active_rate!=rate){SDL_AudioSpec format{SDL_AUDIO_S16,2,static_cast<int>(rate)};if(!SDL_SetAudioStreamFormat(stream,&format,nullptr))throw std::runtime_error("Audio rate transition failed");active_rate=rate;}
    wait_for_output(queued_bytes());
    std::array<int16_t,16> pcm{};
    for(unsigned i=0;i<8;++i){
        // Wii AI DMA is big-endian right/left; SDL expects host-endian left/right.
        pcm[i*2]=static_cast<int16_t>((data[i*4+2]<<8)|data[i*4+3]);
        pcm[i*2+1]=static_cast<int16_t>((data[i*4]<<8)|data[i*4+1]);
    }
    if(!SDL_PutAudioStreamData(stream,pcm.data(),32))throw std::runtime_error(std::string("PCM submission failed: ")+SDL_GetError());
    stats.submitted_bytes+=32;
    queued_bytes();
}
void stop_audio(){if(stream){std::fprintf(stderr,"AUDIO output totals: submitted=%llu bytes max_queue=%u bytes waits=%llu\n",static_cast<unsigned long long>(stats.submitted_bytes),stats.max_queued_bytes,static_cast<unsigned long long>(stats.backpressure_waits));SDL_DestroyAudioStream(stream);stream=nullptr;active_rate=0;}}
}
