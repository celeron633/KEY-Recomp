// SPDX-License-Identifier: GPL-3.0-only
#include "backend.h"
#include "key/input.h"
#include "key/memory.h"
#include <dolphin/pad.h>
#include <aurora/wpad.h>
#include <filesystem>
#include <stdexcept>
#include <cstdio>
#include <SDL3/SDL_events.h>
#include <thread>
#include <chrono>
void check(bool v){if(!v)throw std::runtime_error("Native input ABI test failed");}
int main() {
    try {
        key::memory().reset();key::graphics::initialize((std::filesystem::current_path()/"private").string(),true);
        CpuContext c{};check(key::input::invoke(0x806B4360,c));
        auto completion=[&](uint32_t expected){
            for(unsigned i=0;i<64;++i){auto event=key::input::next_completion();if(!event || event->callback==expected)return event;check(event->callback==0x8067B7E0);}
            throw std::runtime_error("Too many input activity callbacks");
        };
        PADStatus sample{};sample.err=PAD_ERR_NONE;sample.button=PAD_BUTTON_X|PAD_BUTTON_Y;
        PADSetVirtualStatus(0,&sample);
        c.gpr[3]=0;c.gpr[4]=0x80009000;key::input::invoke(0x806B5810,c);
        aurora_wpad_update();
        auto callback=completion(0x80009000);check(callback && callback->callback==0x80009000 && callback->channel==0 && callback->result==0);
        c.gpr[3]=0;c.gpr[4]=0x80005000;key::input::invoke(0x806B56F0,c);
        check(c.gpr[3]==0 && key::memory().read(0x80005000,4)==0);
        c.gpr[3]=0;c.gpr[4]=2;key::input::invoke(0x806B5950,c);check(c.gpr[3]==0);
        aurora_wpad_update();
        c.gpr[3]=0;c.gpr[4]=0x80005100;key::input::invoke(0x806B6080,c);
        check(key::memory().read(0x80005100,2)==0x0300 && key::memory().read(0x80005128,2)==0);
        check(key::memory().read(0x80005102,2)==0 && key::memory().read(0x80005106,2)==256);
        c.gpr[3]=0;c.gpr[4]=0x80005200;c.gpr[5]=0x80009004;key::input::invoke(0x806B5A70,c);check(c.gpr[3]==0);
        aurora_wpad_update();callback=completion(0x80009004);
        check(callback && callback->callback==0x80009004 && callback->result==0);
        check(key::memory().read(0x80005214,1)<=4);
        c.gpr[3]=0;c.gpr[4]=0;c.gpr[5]=0x80005300;key::input::invoke(0x806B5520,c);
        check(key::memory().read(0x80005300,2)==256);
        check(!key::input::invoke(0x80009008,c));
        PADClearVirtualStatus(0);
        SDL_Event key{};key.type=SDL_EVENT_KEY_DOWN;key.key.scancode=SDL_SCANCODE_DOWN;key.key.down=true;check(SDL_PushEvent(&key));
        key.type=SDL_EVENT_KEY_UP;key.key.down=false;check(SDL_PushEvent(&key));
        key::input::poll();
        c.gpr[3]=0;c.gpr[4]=0x80005100;key::input::invoke(0x806B6080,c);check((key::memory().read(0x80005100,2)&1)!=0);
        std::this_thread::sleep_for(std::chrono::milliseconds(45));key::input::poll();
        c.gpr[3]=0;c.gpr[4]=0x80005100;key::input::invoke(0x806B6080,c);check(key::memory().read(0x80005100,2)==0);
        key::graphics::shutdown();
        std::puts("PASS: virtual host controller -> big-endian WPAD report, calibration, connection and async info callbacks");
    }catch(const std::exception& e){std::fprintf(stderr,"INPUT smoke: %s\n",e.what());key::graphics::shutdown();return 1;}
}
