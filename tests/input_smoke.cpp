// SPDX-License-Identifier: GPL-3.0-only
#include "backend.h"
#include "key/input.h"
#include "key/memory.h"
#include "controller/controls.h"
#include <dolphin/pad.h>
#include <aurora/wpad.h>
#include <filesystem>
#include <stdexcept>
#include <cstdio>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL.h>
#include <fstream>
#include <source_location>
#include <thread>
#include <chrono>
void check(bool v,const std::source_location at=std::source_location::current()){if(!v)throw std::runtime_error("Native input ABI test failed at line "+std::to_string(at.line()));}
int main() {
    try {
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
        const auto root=std::filesystem::current_path()/"private/reports"/("input-smoke-"+std::to_string(SDL_GetTicksNS()));
        key::memory().reset();key::graphics::initialize(root.string(),true);
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
        const auto sampled=std::chrono::steady_clock::now();
        check(key::input::poll(sampled));
        check(!key::input::poll(sampled));
        check(!key::input::poll(sampled+std::chrono::milliseconds(4)));
        c.gpr[3]=0;c.gpr[4]=0x80005100;key::input::invoke(0x806B6080,c);check((key::memory().read(0x80005100,2)&1)!=0);
        std::this_thread::sleep_for(std::chrono::milliseconds(45));key::input::poll();
        c.gpr[3]=0;c.gpr[4]=0x80005100;key::input::invoke(0x806B6080,c);check(key::memory().read(0x80005100,2)==0);
        // Exercise SDL sensor delivery and the original guest WPAD ABI without game data.
        SDL_VirtualJoystickSensorDesc sensors[]={{SDL_SENSOR_GYRO,200},{SDL_SENSOR_ACCEL,200}};
        SDL_VirtualJoystickDesc desc{};SDL_INIT_INTERFACE(&desc);
        desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.name="KEY synthetic motion controller";
        desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;desc.naxes=SDL_GAMEPAD_AXIS_COUNT;
        desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;
        desc.nsensors=2;desc.sensors=sensors;desc.SetSensorsEnabled=[](void*,bool){return true;};
        const auto id=SDL_AttachVirtualJoystick(&desc);check(id!=0);
        auto* joystick=SDL_OpenJoystick(id);check(joystick!=nullptr);
        auto tick=[&]{std::this_thread::sleep_for(std::chrono::milliseconds(6));key::input::poll();};
        tick();
        for(unsigned i=0;i<PADCount();++i)if(SDL_GetGamepadID(PADGetSDLGamepadForIndex(i))==id)PADSetPortForIndex(i,0);
        tick();tick();
        const auto read=[&]{c.gpr[3]=0;c.gpr[4]=0x80005100;key::input::invoke(0x806B6080,c);};
        const auto event=[&](SDL_Scancode code,bool down){SDL_Event e{};e.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;e.key.scancode=code;e.key.down=down;check(SDL_PushEvent(&e));};
        check(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,true));tick();tick();read();
        check(key::memory().read(0x80005100,2)==0x100); // Cross produces Wii 2 only, never the default GC A too.
        check(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,false));tick();tick();
        check(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_DPAD_LEFT,true));tick();tick();read();
        check(key::memory().read(0x80005100,2)==8);
        check(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_DPAD_LEFT,false));
        const float gyro[3]={0,-.5f,0},accel[3]={9.80665f,0,0};
        c.gpr[3]=0;c.gpr[4]=1;c.gpr[5]=0;key::input::invoke(0x806B8030,c);check(c.gpr[3]==0);
        for(int i=0;i<50;++i){check(SDL_SendJoystickVirtualSensorData(joystick,SDL_SENSOR_GYRO,SDL_GetTicksNS(),gyro,3));check(SDL_SendJoystickVirtualSensorData(joystick,SDL_SENSOR_ACCEL,SDL_GetTicksNS(),accel,3));tick();}
        read();
        check(static_cast<int16_t>(key::memory().read(0x80005104,2)) < -240);
        check(key::memory().read(0x80005108,2)<470 && key::memory().read(0x8000510E,1)==0); // Negative yaw moves the valid IR pair left.
        event(SDL_SCANCODE_F1,true);event(SDL_SCANCODE_F1,false);tick();tick();check(key::controls::menu_open());
        check(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,true));tick();tick();read();
        check(key::memory().read(0x80005100,2)==0 && key::memory().read(0x8000510E,1)==255);
        event(SDL_SCANCODE_F1,true);event(SDL_SCANCODE_F1,false);tick();tick();read();check(!key::controls::menu_open());
        check(key::memory().read(0x80005100,2)==0); // Held menu buttons must be released before gameplay resumes.
        check(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,false));tick();tick();
        event(SDL_SCANCODE_Q,true);tick();tick();read();check(static_cast<int16_t>(key::memory().read(0x80005104,2))>100);
        event(SDL_SCANCODE_Q,false);
        SDL_Event focus{};focus.type=SDL_EVENT_WINDOW_FOCUS_LOST;check(SDL_PushEvent(&focus));tick();tick();read();
        check(key::memory().read(0x80005100,2)==0 && key::memory().read(0x80005106,2)==256);
        focus.type=SDL_EVENT_WINDOW_FOCUS_GAINED;check(SDL_PushEvent(&focus));tick();tick();
        event(SDL_SCANCODE_F2,true);event(SDL_SCANCODE_F2,false);tick();
        check(std::filesystem::exists(root/"user/controls.ini"));
        event(SDL_SCANCODE_F2,true);event(SDL_SCANCODE_F2,false);tick(); // Exercise replacement of an existing configuration.
        std::ifstream saved(root/"user/controls.ini");const std::string text((std::istreambuf_iterator<char>(saved)),{});check(text.find("fps 0\n")!=std::string::npos);
        saved.close();
        key::controls::shutdown();
        auto remapped=text;
        const auto binding=remapped.find("button0 0\n");check(binding!=std::string::npos);
        remapped.replace(binding,10,"button0 10\n"); // Wii 2 on right shoulder after reload.
        {std::ofstream out(root/"user/controls.ini");out<<remapped;}
        key::controls::initialize(root.string());tick();tick();
        check(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,true));tick();tick();read();check(key::memory().read(0x80005100,2)==0);
        check(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,false));
        check(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,true));tick();tick();read();check(key::memory().read(0x80005100,2)==0x100);
        check(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,false));tick();tick();
        key::controls::shutdown();
        {std::ofstream out(root/"user/controls.ini");out<<remapped<<"sensitivity\n";} // A truncated value must reject the whole candidate.
        key::controls::initialize(root.string());tick();tick();
        check(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,true));tick();tick();read();check(key::memory().read(0x80005100,2)==0x100);
        check(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,false));tick();tick();
        SDL_CloseJoystick(joystick);check(SDL_DetachVirtualJoystick(id));tick();tick();read();
        check(key::memory().read(0x80005100,2)==0 && key::memory().read(0x80005106,2)==256 && key::memory().read(0x8000510E,1)==255);
        key::graphics::shutdown();
        std::puts("PASS: SDL controller/sensors -> sideways WPAD buttons, gravity and IR; menu capture, focus, disconnect, hotkeys, settings replacement/reload/rejection, keyboard, calibration and callbacks");
    }catch(const std::exception& e){std::fprintf(stderr,"INPUT smoke: %s\n",e.what());key::graphics::shutdown();return 1;}
}
