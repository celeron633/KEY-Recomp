// SPDX-License-Identifier: GPL-3.0-only
#include "backend.h"
#include "command_bridge.h"
#include "key/memory.h"
#include "key/input.h"
#include "key/clock.h"
#include "key/timing_metrics.h"
#include "audio_output.h"
#include "controller/controls.h"
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/gfx.h>
#include <dolphin/vi.h>
#include <dolphin/gx/GXAurora.h>
#include "gx/fifo.hpp"
#include <filesystem>
#include <memory>
#include <optional>
#include <cstdio>
#include <stdexcept>
#include <array>
#include <chrono>
#include "gx/gx.hpp"
#include "webgpu/gpu.hpp"
#include <string_view>
#include <imgui.h>

namespace key::graphics {
namespace {
struct State {
    bool initialized=false,frame_active=false;
    bool dimmed=false;
    std::unique_ptr<CommandBridge> bridge;
    std::optional<GXRenderModeObj> pending_mode;
    std::string user,cache,resources;
    uint64_t frame_count=0;
    uint64_t presented_copy=0;
    AuroraReadbackStream probes=0;
    std::vector<AuroraReadbackStream> draw_probes;
    bool draw_probe_reported=false;
    std::chrono::steady_clock::time_point timing_start{};
    uint64_t timing_guest=0,timing_frames=0,timing_audio=0,timing_audio_wait=0;
    uint64_t boundary_ns=0,sync_ns=0;
    TimingCounters timing_previous{};
    ~State() { if(initialized) shutdown(); }
};
State& state() { static State value; return value; }
void report_timing() {
    auto& s=state();
    const auto now=std::chrono::steady_clock::now();
    const auto guest=timebase();const auto audio=audio_output_stats();
    if(s.timing_start.time_since_epoch().count()) {
        const double wall=std::chrono::duration<double>(now-s.timing_start).count();
        if(wall<5)return;
        const double guest_seconds=double(guest-s.timing_guest)/timebase_hz;
        std::fprintf(stderr,"TIMING wall=%.3fs guest_ratio=%.4f copies_fps=%.2f game_root_hz=%.2f vi_events_hz=%.2f pcm_KiB_s=%.2f idle_ms=%.2f boundary_ms=%.2f fifo_sync_ms=%.2f audio_wait_ms=%.2f\n",
            wall,guest_seconds/wall,(s.frame_count-s.timing_frames)/wall,
            (timing_counters.game_root_polls-s.timing_previous.game_root_polls)/wall,
            (timing_counters.video_events-s.timing_previous.video_events)/wall,
            (audio.submitted_bytes-s.timing_audio)/wall/1024,
            (timing_counters.scheduler_wait_ns-s.timing_previous.scheduler_wait_ns)/1e6,
            s.boundary_ns/1e6,s.sync_ns/1e6,(audio.waited_ns-s.timing_audio_wait)/1e6);
    }
    s.timing_start=now;s.timing_guest=guest;s.timing_frames=s.frame_count;
    s.timing_audio=audio.submitted_bytes;s.timing_audio_wait=audio.waited_ns;
    s.timing_previous=timing_counters;s.boundary_ns=s.sync_ns=0;
}
void log(AuroraLogLevel level,const char* module,const char* message,unsigned int length) {
    if(level>=LOG_WARNING) std::fprintf(stderr,"GRAPHICS %s: %.*s\n",module,static_cast<int>(length),message);
}
void feed(const uint8_t* data,size_t size) {
    auto& s=state();
    if(!s.initialized || !s.frame_active) throw std::runtime_error("GX FIFO arrived outside a native frame");
    s.bridge->feed({data,size});
    aurora::gx::fifo::publish();
    // The original game can copy the previous EFB at the start of its CPU
    // frame and then draw the next one. Finish the host frame at that copy,
    // before those following draws, so they survive until their own copy.
    if(s.bridge->display_copies()!=s.presented_copy && s.bridge->empty())present();
}
void draw_done() { memory().gpu_finish(); }
void draw_sync(uint16_t token) { memory().gpu_token(token); }
uint64_t generation(const void* p,size_t size) { const auto value=memory().generation(p,size); return value?value:AURORA_GUEST_WRITE_UNTRACKED; }
void notify_write(const void* p,size_t size) { memory().notify_gpu_write(p,size); }
}
void initialize(const std::string& private_root,bool hidden) {
    auto& s=state();
    if(s.initialized) return;
    const auto root=std::filesystem::path(private_root);
    s.user=(root/"user/graphics").string(); s.cache=(root/"cache/graphics").string(); s.resources=(root/"resources").string();
    std::filesystem::create_directories(s.user); std::filesystem::create_directories(s.cache); std::filesystem::create_directories(s.resources);
    AuroraConfig config{};
    config.appName="KEY-Recomp"; config.userPath=s.user.c_str(); config.cachePath=s.cache.c_str(); config.resourcesPath=s.resources.c_str();
    config.desiredBackend=BACKEND_D3D12; config.windowWidth=1280; config.windowHeight=720;
    config.allowCpuAdapter=false;
    config.vsync=true; config.msaa=1; config.maxTextureAnisotropy=8; config.startHidden=hidden;
    config.pauseOnFocusLost=false; config.logCallback=&log; config.logLevel=LOG_WARNING;
    char name[]="KEY-Recomp"; char* argv[]={name};
    const auto info=aurora_initialize(1,argv,&config);
    if(info.initializationStatus!=AURORA_INITIALIZATION_SUCCESS) throw std::runtime_error(info.initializationError?info.initializationError:"Native graphics initialization failed");
    s.initialized=true;
    controls::initialize(private_root);
    const auto& adapter=aurora::webgpu::g_adapterInfo;
    const auto device=adapter.device.IsUndefined()?std::string_view{"Unknown"}:std::string_view{adapter.device};
    const auto driver=adapter.description.IsUndefined()?std::string_view{"Unknown"}:std::string_view{adapter.description};
    std::fprintf(stderr,"GRAPHICS adapter: backend=%u type=%u device=%.*s driver=%.*s vendor=%04X device_id=%04X cpu_adapter_allowed=0\n",
        unsigned(info.backend),unsigned(adapter.adapterType),int(device.size()),device.data(),int(driver.size()),driver.data(),adapter.vendorID,adapter.deviceID);
    if(info.backend!=BACKEND_D3D12 || adapter.adapterType==wgpu::AdapterType::CPU)
        throw std::runtime_error("Native renderer requires a hardware Direct3D 12 adapter");
    if(std::getenv("KEY_DEBUG_PIXELS"))s.probes=AuroraReadbackCreateStream();
    aurora_set_skip_unready_pipelines(false);
    aurora_set_frame_interpolation_fps(0);
    aurora_set_guest_write_hooks(&generation,&notify_write);
    AuroraSetViewportPolicy(AURORA_VIEWPORT_16_9);
    if(s.pending_mode) { VIConfigure(&*s.pending_mode); s.pending_mode.reset(); }
    s.bridge=std::make_unique<CommandBridge>(
        [](std::span<const uint8_t> data){
            auto& current=state();
            if(current.probes && current.frame_count==3000 && data.size()==5 && data[0]==0x61 && data[1]==0x52){
                aurora::gx::fifo::drain();const auto& gx=aurora::gx::g_gxState;
                const uint32_t control=(uint32_t(data[2])<<16)|(uint32_t(data[3])<<8)|data[4];
                const auto source=gx.bpRegCache[0x49],extent=gx.bpRegCache[0x4A];
                std::fprintf(stderr,"COPYTRACE display=%d clear=%d dst=%08X src=%u,%u,%u,%u scale=%u filter=%06X/%06X drawings=%zu vi=%08X/%08X\n",bool(control&0x4000),bool(control&0x800),(gx.bpRegCache[0x4B]&0xFFFFFF)<<5,source&1023,(source>>10)&1023,(extent&1023)+1,((extent>>10)&1023)+1,gx.bpRegCache[0x4E]&511,gx.bpRegCache[0x53],gx.bpRegCache[0x54],current.draw_probes.size(),uint32_t(memory().read(0xCC00201C,4)),uint32_t(memory().read(0xCC002024,4)));
            }
            if(current.probes && current.frame_count%60==0 && data.size()==5 && data[0]==0x61 && data[1]==0x52 && (data[3]&0x40)){
                const AuroraReadbackPeekRequest points[]={{1,AURORA_READBACK_PEEK_ARGB,80,60},{2,AURORA_READBACK_PEEK_ARGB,560,60},{3,AURORA_READBACK_PEEK_ARGB,320,240},{4,AURORA_READBACK_PEEK_ARGB,80,420},{5,AURORA_READBACK_PEEK_ARGB,560,420}};
                AuroraReadbackCapturePeeks(current.probes,points,5);
            }
            aurora::gx::fifo::write_data(data.data(),static_cast<uint32_t>(data.size()));
            if(current.probes && current.frame_count==3000 && current.draw_probes.size()<256 && data.size()>=3 && (data[0]&0xF8)>=0x80 && (data[0]&0xF8)<=0xB8){
                const auto stream=AuroraReadbackCreateStream();current.draw_probes.push_back(stream);
                const AuroraReadbackPeekRequest point{1,AURORA_READBACK_PEEK_ARGB,320,240};AuroraReadbackCapturePeeks(stream,&point,1);
                aurora::gx::fifo::drain();const auto& gx=aurora::gx::g_gxState;
                std::array<float,16> matrix{};static_assert(sizeof(gx.proj)==sizeof(matrix));std::memcpy(matrix.data(),&gx.proj,sizeof(matrix));
                std::fprintf(stderr,"DRAWSTATE index=%zu count=%u cull=%u depth=%d/%u/%d blend=%u,%u,%u color=%d alpha=%d chans=%u tev=%u matrix=%u projection=%u diag=%.4g,%.4g,%.4g,%.4g\n",current.draw_probes.size()-1,unsigned((data[1]<<8)|data[2]),unsigned(gx.cullMode),gx.depthCompare,unsigned(gx.depthFunc),gx.depthUpdate,unsigned(gx.blendMode),unsigned(gx.blendFacSrc),unsigned(gx.blendFacDst),gx.colorUpdate,gx.alphaUpdate,gx.numChans,gx.numTevStages,gx.currentPnMtx,unsigned(gx.projType),matrix[0],matrix[5],matrix[10],matrix[15]);
            }
        },
        [](uint32_t address,size_t minimum){return memory().gpu_view(address,minimum);},
        [](uint32_t address,size_t length){auto* data=memory().gpu_writable(address,length);memory().mark_gpu_copy(address,length);return data;});
    aurora::gx::fifo::set_draw_done_callback(&draw_done);
    aurora::gx::fifo::set_draw_sync_callback(&draw_sync);
    s.frame_active=aurora_begin_frame();
    if(!s.frame_active) throw std::runtime_error("Native graphics frame did not begin");
    memory().set_fifo_sink(&feed); memory().set_fifo_sync(&sync,&empty);
    memory().set_audio_sink(&submit_audio);
    std::fprintf(stderr,"GRAPHICS native D3D12 renderer initialized (hidden=%d)\n",hidden);
}
void sync() { if(state().initialized) aurora::gx::fifo::drain(); }
bool empty() { return !state().bridge || state().bridge->empty(); }
bool active() { return state().initialized; }
uint64_t frames() { return state().frame_count; }
uint64_t draws() { return state().bridge?state().bridge->draw_count():0; }
void present() {
    auto& s=state();
    if(!s.initialized) throw std::runtime_error("Present requested before native graphics initialization");
    // Several game threads wait for the same VI retrace. Only an actual XFB
    // copy completes a display frame; an unrelated wait must not split a draw.
    if(s.presented_copy==s.bridge->display_copies() || !s.bridge->empty()){poll_events();return;}
    s.presented_copy=s.bridge->display_copies();
    const auto boundary_start=std::chrono::steady_clock::now();
    sync();
    s.sync_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-boundary_start).count();
    if(!s.draw_probe_reported && s.frame_count>=3005 && !s.draw_probes.empty()){
        unsigned found=0;
        for(size_t i=0;i<s.draw_probes.size();++i){uint32_t pixel=0;AuroraReadbackDest out{1,&pixel,4,GX_FALSE};if(AuroraReadbackGet(s.draw_probes[i],&out,1)&&out.found){++found;std::fprintf(stderr,"DRAWPIXEL index=%zu nonblack=%d\n",i,(pixel&0xFFFFFF)!=0);}}
        if(found==s.draw_probes.size())s.draw_probe_reported=true;
    }
    if(s.probes && s.frame_count%60==0){
        std::array<uint32_t,5> pixels{};std::array<AuroraReadbackDest,5> outputs{};
        for(unsigned i=0;i<5;++i)outputs[i]={i+1,&pixels[i],4,GX_FALSE};
        AuroraReadbackGet(s.probes,outputs.data(),5);unsigned found=0,lit=0;
        for(unsigned i=0;i<5;++i)if(outputs[i].found){++found;if(pixels[i]&0xFFFFFF)++lit;}
        const auto& gx=aurora::gx::g_gxState;
        std::fprintf(stderr,"PIXELS frame=%llu visible probes=%u/%u viewport=%.1f,%.1f,%.1f,%.1f scissor=%d,%d,%d,%d color=%d tev=%u\n",static_cast<unsigned long long>(s.frame_count),lit,found,gx.logicalViewport.left,gx.logicalViewport.top,gx.logicalViewport.width,gx.logicalViewport.height,gx.logicalScissor.x,gx.logicalScissor.y,gx.logicalScissor.width,gx.logicalScissor.height,gx.colorUpdate,gx.numTevStages);
    }
    if(s.dimmed){if(!ImGui::GetCurrentContext())throw std::runtime_error("No native presentation overlay for video dimming");ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(0,0),ImGui::GetIO().DisplaySize,IM_COL32(0,0,0,128));}
    controls::draw_overlay(s.frame_count);
    aurora_end_frame(); s.frame_active=false;
    ++s.frame_count;
    if(s.frame_count<=5 || s.frame_count%60==0) std::fprintf(stderr,"GRAPHICS presented frame=%llu guest draws=%llu\n",static_cast<unsigned long long>(s.frame_count),static_cast<unsigned long long>(draws()));
    poll_events();
    if(s.pending_mode) { VIConfigure(&*s.pending_mode); s.pending_mode.reset(); }
    s.frame_active=aurora_begin_frame();
    if(!s.frame_active) throw std::runtime_error("Native renderer paused unexpectedly");
    s.boundary_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-boundary_start).count();
    report_timing();
}
void poll_events() {
    controls::update();
    for(auto* event=aurora_update();event && event->type!=AURORA_NONE;++event){
        if(event->type==AURORA_EXIT) throw std::runtime_error("Native window closed by user");
        if(event->type==AURORA_SDL_EVENT){
            if(event->sdl.type==SDL_EVENT_WINDOW_FOCUS_LOST)input::release_keyboard();
        }
    }
}
void set_dimming(bool enabled){state().dimmed=enabled;}
void configure_mode(uint32_t address) {
    auto& m=memory(); GXRenderModeObj mode{};
    mode.viTVmode=static_cast<VITVMode>(m.read(address,4));
    mode.fbWidth=m.read(address+4,2); mode.efbHeight=m.read(address+6,2); mode.xfbHeight=m.read(address+8,2);
    mode.viXOrigin=m.read(address+10,2);mode.viYOrigin=m.read(address+12,2);mode.viWidth=m.read(address+14,2);mode.viHeight=m.read(address+16,2);
    mode.xFBmode=static_cast<VIXFBMode>(m.read(address+20,4));mode.field_rendering=m.read(address+24,1);mode.aa=m.read(address+25,1);
    for(unsigned i=0;i<24;++i) mode.sample_pattern[i/2][i%2]=m.read(address+26+i,1);
    for(unsigned i=0;i<7;++i) mode.vfilter[i]=m.read(address+50+i,1);
    if(!mode.fbWidth || !mode.efbHeight || mode.fbWidth>1024 || mode.efbHeight>1024) throw std::runtime_error("Invalid guest render mode extent");
    state().pending_mode=mode;
}
void shutdown() {
    auto& s=state();
    if(!s.initialized) return;
    memory().set_fifo_sink(nullptr); memory().set_fifo_sync(nullptr,nullptr);
    memory().set_audio_sink(nullptr);stop_audio();
    controls::shutdown();
    input::shutdown();
    sync();
    if(s.frame_active) { aurora_end_frame(); s.frame_active=false; }
    if(s.probes){AuroraReadbackDestroyStream(s.probes);s.probes=0;}
    for(auto stream:s.draw_probes)AuroraReadbackDestroyStream(stream);s.draw_probes.clear();
    aurora_shutdown(); s.initialized=false; s.bridge.reset();
}
}
