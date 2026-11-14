// SPDX-License-Identifier: GPL-3.0-only
#include "key/input.h"
#include "key/memory.h"
#include "backend.h"
#include "controls.h"
#include <revolution/wpad.h>
#include <aurora/wpad.h>
#include <dolphin/pad.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_events.h>
#include <mutex>
#include <array>
#include <chrono>
#include <deque>
#include <cstdio>
#include <utility>

namespace key::input {
namespace {
struct Channel {
    uint32_t connect=0,extension=0,sampling=0,buffer=0,count=0,index=0,completion=0,info_output=0;
    WPADInfo info{};
    std::chrono::steady_clock::time_point last_sample{};
};
std::array<Channel,4> channels;
std::deque<Completion> completions;
bool initialized=false,motor_enabled=true;
uint32_t allocator=0,free_function=0;
auto last_poll=std::chrono::steady_clock::time_point{};
auto input_started=std::chrono::steady_clock::time_point{};
bool test_pulse_active=false,test_pulse_done=false;
struct Key {uint32_t scancode;uint16_t mask;bool down=false;std::chrono::steady_clock::time_point until{};};
std::array<Key,17> keys{{{SDL_SCANCODE_LEFT,PAD_BUTTON_UP},{SDL_SCANCODE_RIGHT,PAD_BUTTON_DOWN},{SDL_SCANCODE_UP,PAD_BUTTON_RIGHT},{SDL_SCANCODE_DOWN,PAD_BUTTON_LEFT},{SDL_SCANCODE_Z,PAD_BUTTON_X},{SDL_SCANCODE_X,PAD_BUTTON_Y},{SDL_SCANCODE_RETURN,PAD_BUTTON_A},{SDL_SCANCODE_C,PAD_BUTTON_B},{SDL_SCANCODE_SPACE,PAD_BUTTON_START},{SDL_SCANCODE_A,PAD_BUTTON_UP},{SDL_SCANCODE_D,PAD_BUTTON_DOWN},{SDL_SCANCODE_W,PAD_BUTTON_RIGHT},{SDL_SCANCODE_S,PAD_BUTTON_LEFT},{SDL_SCANCODE_KP_4,PAD_BUTTON_UP},{SDL_SCANCODE_KP_6,PAD_BUTTON_DOWN},{SDL_SCANCODE_KP_8,PAD_BUTTON_RIGHT},{SDL_SCANCODE_KP_2,PAD_BUTTON_LEFT}}};
std::mutex key_mutex;
bool SDLCALL keyboard_watch(void*,SDL_Event* event){
    if(event->type==SDL_EVENT_KEY_DOWN && std::getenv("KEY_TRACE_INPUT"))std::fprintf(stderr,"INPUT SDL scancode=%u\n",unsigned(event->key.scancode));
    if(event->type==SDL_EVENT_KEY_DOWN || event->type==SDL_EVENT_KEY_UP)key_event(event->key.scancode,event->type==SDL_EVENT_KEY_DOWN);
    return true;
}
void enqueue(uint32_t callback,int32_t channel,int32_t result) {
    if(callback) {if(completions.size()>=1024)throw std::runtime_error("WPAD callback queue overflow");completions.push_back({callback,channel,result});}
}
void connection(int32_t c,int32_t result) {enqueue(channels.at(c).connect,c,result);}
void extension(int32_t c,int32_t device) {enqueue(channels.at(c).extension,c,device);}
void report(int32_t c,uint32_t address) {
    const auto format=WPADGetDataFormat(c);
    if(format>2 && format!=9)throw std::runtime_error("Native input profile supports core Wii Remote reports only");
    WPADStatusEx sample{};WPADRead(c,&sample);
    if(c==0 && std::getenv("KEY_TRACE_INPUT")){static uint16_t previous=0;if(previous!=sample.button){previous=sample.button;std::fprintf(stderr,"INPUT WPAD buttons=%04X error=%d format=%u\n",sample.button,sample.err,format);}}
    auto& m=memory();m.fill(address,format==9?0x5A:0x2A,0);
    m.write(address,sample.button,2);m.write(address+2,sample.accX,2);m.write(address+4,sample.accY,2);m.write(address+6,sample.accZ,2);
    for(unsigned i=0;i<4;++i) {
        const auto p=address+8+i*8;const auto& o=sample.obj[i];
        m.write(p,o.x,2);m.write(p+2,o.y,2);m.write(p+4,o.size,2);m.write(p+6,o.traceId,1);
        if(format==9) {
            const auto e=address+0x2A+i*12;const auto& x=sample.exp[i];
            m.write(e,x.range_x1,2);m.write(e+2,x.range_y1,2);m.write(e+4,x.range_x2,2);m.write(e+6,x.range_y2,2);m.write(e+8,x.pixel,2);m.write(e+10,x.radius,1);
        }
    }
    m.write(address+40,sample.dev,1);m.write(address+41,sample.err,1);
}
void sampling(int32_t c) {
    auto& ch=channels.at(c);
    const auto now=std::chrono::steady_clock::now();
    if(now-ch.last_sample<std::chrono::milliseconds(5))return;
    ch.last_sample=now;
    WPADStatusEx activity{};WPADRead(c,&activity);
    if(activity.err==0 && activity.button)enqueue(0x8067B7E0,c,0); // Original VI RF-idle reset normally issued by WPAD's hardware report path.
    if(ch.buffer && ch.count) {const auto size=WPADGetDataFormat(c)==9?0x5A:0x2A;report(c,ch.buffer+ch.index*size);ch.index=(ch.index+1)%ch.count;}
    enqueue(ch.sampling,c,0);
}
void complete(int32_t c,int32_t result) {
    auto& ch=channels.at(c);
    if(ch.info_output && !result) {
        const auto p=ch.info_output;const auto& info=ch.info;auto& m=memory();
        m.write(p,info.dpd,4);m.write(p+4,info.speaker,4);m.write(p+8,info.attach,4);m.write(p+12,info.lowBat,4);m.write(p+16,info.nearempty,4);
        m.write(p+20,info.battery,1);m.write(p+21,info.led,1);m.write(p+22,info.protocol,1);m.write(p+23,info.firmware,1);
    }
    enqueue(std::exchange(ch.completion,0),c,result);ch.info_output=0;
}
}
void key_event(uint32_t scancode,bool down){
    std::lock_guard lock(key_mutex);
    if(controls::captured())return;
    const auto now=std::chrono::steady_clock::now();
    for(auto& key:keys)if(key.scancode==scancode){
        if(down && !key.down)key.until=now+std::chrono::milliseconds(40);
        key.down=down;
        if(std::getenv("KEY_TRACE_INPUT"))std::fprintf(stderr,"INPUT keyboard scancode=%u down=%d\n",scancode,down);
    }
}
void release_keyboard(){std::lock_guard lock(key_mutex);for(auto& key:keys){key.down=false;key.until={};}controls::set_keyboard(0);}
void shutdown(){if(initialized){SDL_RemoveEventWatch(&keyboard_watch,nullptr);release_keyboard();aurora_wpad_shutdown();initialized=false;channels={};completions.clear();}}
void poll() {
    poll(std::chrono::steady_clock::now());
}
bool poll(std::chrono::steady_clock::time_point now) {
    if(!initialized)return false;
    if(now-last_poll<std::chrono::milliseconds(5))return false;
    if(std::getenv("KEY_TEST_BUTTON_2") && !test_pulse_done){
        const auto elapsed=now-input_started;
        if(elapsed>=std::chrono::seconds(30) && !test_pulse_active){test_pulse_active=true;std::fprintf(stderr,"INPUT synthetic test: Wii 2 held for 250 ms\n");}
        if(test_pulse_active && elapsed>=std::chrono::milliseconds(30250)){test_pulse_active=false;test_pulse_done=true;}
    }
    uint16_t mask=test_pulse_active?PAD_BUTTON_Y:0;
    {std::lock_guard lock(key_mutex);for(const auto& key:keys)if(key.down || now<key.until)mask|=key.mask;}
    controls::set_keyboard(mask);
    last_poll=now;graphics::poll_events();
    return true;
}
std::optional<Completion> next_completion() {
    if(completions.empty())return std::nullopt;
    auto result=completions.front();completions.pop_front();return result;
}
bool invoke(uint32_t address,CpuContext& c) {
    if(address==0x806B4400) {allocator=c.gpr[3];free_function=c.gpr[4];return true;} // Host WPAD storage uses the host heap, not these guest allocators.
    if(address==0x806B4360) {
        if(!graphics::active())throw std::runtime_error("WPAD requires initialized native platform events");
        if(!initialized) {
            WPADInit();
            uint32_t binding_count=0;
            if(!PADGetKeyButtonBindings(0,&binding_count)) {
                for(size_t i=0;i<9;++i)if(!PADSetKeyButtonBinding(0,{static_cast<int32_t>(keys[i].scancode),keys[i].mask}))throw std::runtime_error("Cannot configure default keyboard input");
                PADSetKeyboardActive(0,TRUE);
            }
            if(!SDL_AddEventWatch(&keyboard_watch,nullptr))throw std::runtime_error("Cannot install native game keyboard event watch");
            for(int i=0;i<4;++i) {
                if(!aurora_wpad_set_device(i,WPAD_DEV_CORE))throw std::runtime_error("Cannot select core Wii Remote host profile");
                WPADSetConnectCallback(i,&connection);WPADSetExtensionCallback(i,&extension);WPADSetSamplingCallback(i,&sampling);
            }
            initialized=true;input_started=std::chrono::steady_clock::now();std::fprintf(stderr,"INPUT native WPAD initialized: core Wii Remote via SDL devices\n");
        }
        return true;
    }
    if(address==0x806B4410) {c.gpr[3]=WPADGetStatus();return true;}
    if(address==0x806B7F60) {c.gpr[3]=WPADGetDpdSensitivity();return true;}
    if(address==0x806B4470) {c.gpr[3]=WPADGetSensorBarPosition();return true;}
    if(address==0x806B5D60) {motor_enabled=c.gpr[3]!=0;if(!motor_enabled)for(int i=0;i<4;++i)WPADControlMotor(i,0);return true;}
    if(address==0x806B5DA0) {c.gpr[3]=motor_enabled;return true;}
    switch(address) {
    case 0x806B56F0:case 0x806B5780:case 0x806B5810:case 0x806B58A0:case 0x806B5900:case 0x806B5950:
    case 0x806B6080:case 0x806B6120:case 0x806B5520:case 0x806B5C90:case 0x806B5A70:case 0x806B7FA0:
    case 0x806B8030:case 0x806B92E0:case 0x806B5630:case 0x806B64A0:case 0x806B64F0:break;
    default:
        if(address>=0x806B4360 && address<0x806C2C80) {
            char message[100];std::snprintf(message,sizeof(message),"Native WPAD service requires implementation at 0x%08X",address);throw std::runtime_error(message);
        }
        return false;
    }
    const auto channel=static_cast<int32_t>(c.gpr[3]);
    if(channel<0 || channel>=4)throw std::runtime_error("Invalid guest WPAD channel");
    auto& ch=channels[channel];
    switch(address) {
    case 0x806B56F0: {uint32_t type;const auto result=WPADProbe(channel,&type);if(c.gpr[4])memory().write(c.gpr[4],type,4);c.gpr[3]=result;break;}
    case 0x806B5810: c.gpr[3]=std::exchange(ch.connect,c.gpr[4]);WPADSetConnectCallback(channel,&connection);break;
    case 0x806B58A0: c.gpr[3]=std::exchange(ch.extension,c.gpr[4]);WPADSetExtensionCallback(channel,&extension);break;
    case 0x806B5780: c.gpr[3]=std::exchange(ch.sampling,c.gpr[4]);break;
    case 0x806B5900:c.gpr[3]=WPADGetDataFormat(channel);break;
    case 0x806B5950: {
        if(c.gpr[4]>2 && c.gpr[4]!=9)throw std::runtime_error("Unsupported native WPAD extension format");
        c.gpr[3]=WPADSetDataFormat(channel,c.gpr[4]);break;
    }
    case 0x806B6080:report(channel,c.gpr[4]);break;
    case 0x806B6120:
        if(c.gpr[5]>4096)throw std::runtime_error("WPAD sampling buffer count exceeded bound");
        ch.buffer=c.gpr[4];ch.count=c.gpr[5];ch.index=0;
        if(ch.buffer && ch.count)memory().gpu_writable(ch.buffer,ch.count*(WPADGetDataFormat(channel)==9?0x5A:0x2A));break;
    case 0x806B5520: {
        if(c.gpr[4]>1)throw std::runtime_error("Unsupported WPAD accelerometer calibration type");
        WPADAcc acc;WPADGetAccGravityUnit(channel,c.gpr[4],&acc);
        memory().write(c.gpr[5],acc.x,2);memory().write(c.gpr[5]+2,acc.y,2);memory().write(c.gpr[5]+4,acc.z,2);break;
    }
    case 0x806B5C90:if(c.gpr[4]>1)throw std::runtime_error("Unknown motor command");WPADControlMotor(channel,motor_enabled?c.gpr[4]:0);break;
    case 0x806B7FA0:c.gpr[3]=WPADIsDpdEnabled(channel);break;
    case 0x806B92E0:c.gpr[3]=0;break; // Core remote has no MotionPlus extension.
    case 0x806B5630:WPADDisconnect(channel);break;
    case 0x806B64A0:c.gpr[3]=0;break; // Host profile does not expose remote speaker output.
    case 0x806B64F0:
        if(c.gpr[4]!=0 && c.gpr[4]!=2)throw std::runtime_error("Wii Remote speaker playback has no native audio route yet");
        enqueue(c.gpr[5],channel,0);c.gpr[3]=0;break;
    case 0x806B8030:
        if(ch.completion || ch.info_output){c.gpr[3]=static_cast<uint32_t>(-2);break;}
        if(c.gpr[4]>3)throw std::runtime_error("Extended IR mode not yet supported by host WPAD");
        ch.completion=c.gpr[5];c.gpr[3]=WPADControlDpd(channel,c.gpr[4],&complete);break;
    case 0x806B5A70:
        if(ch.completion || ch.info_output){c.gpr[3]=static_cast<uint32_t>(-2);break;}
        memory().gpu_writable(c.gpr[4],24);ch.info_output=c.gpr[4];ch.completion=c.gpr[5];
        c.gpr[3]=WPADGetInfoAsync(channel,&ch.info,&complete);break;
    }
    return true;
}
}
