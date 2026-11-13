// SPDX-License-Identifier: GPL-3.0-only
#include "backend.h"
#include "key/memory.h"
#include <aurora/aurora.h>
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <cstdio>
#include <bit>
#include <vector>
#include <filesystem>
#include <string>
#include <thread>
#include <chrono>

int main(int argc,char** argv) {
    try {
        key::memory().reset();
        key::memory().write(0xCC00300C,0x10000,4);key::memory().write(0xCC003010,0x1FFE0,4);key::memory().write(0xCC003014,0x10000,4);
        key::memory().write(0xCC000002,0x11,2);
        const bool visible=argc>1 && std::string(argv[1])=="--visible";
        key::graphics::initialize((std::filesystem::current_path()/"private").string(),!visible);
        alignas(32) std::array<uint8_t,65536> native_fifo{};
        GXInit(native_fifo.data(),native_fifo.size());
        float projection[4][4]{}; projection[0][0]=projection[1][1]=projection[2][2]=projection[3][3]=1;
        float model[3][4]{}; model[0][0]=model[1][1]=model[2][2]=1;
        GXSetProjection(projection,GX_ORTHOGRAPHIC);
        GXLoadPosMtxImm(model,GX_PNMTX0);GXSetCurrentMtx(GX_PNMTX0);
        GXSetViewport(0,0,640,480,0,1);GXSetScissor(0,0,640,480);
        GXSetNumChans(1);GXSetChanCtrl(GX_COLOR0A0,GX_FALSE,GX_SRC_REG,GX_SRC_VTX,0,GX_DF_NONE,GX_AF_NONE);
        GXSetNumTexGens(0);GXSetNumTevStages(1);GXSetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD_NULL,GX_TEXMAP_NULL,GX_COLOR0A0);GXSetTevOp(GX_TEVSTAGE0,GX_PASSCLR);
        GXSetZMode(GX_FALSE,GX_ALWAYS,GX_FALSE);GXSetCullMode(GX_CULL_NONE);GXSetCopyClear(GXColor{0,0,0,255},GX_MAX_Z24);
        GXFlush(); // Flush producer-side dirty state before bypassing GXBegin with raw guest commands.
        const auto readback=AuroraReadbackCreateStream();
        bool color_verified=false;
        for(unsigned frame=0;frame<(visible?6000u:60u);++frame) {
            auto byte=[](uint8_t v){key::memory().write(0xCC008000,v,1);};
            auto word=[](uint32_t v){key::memory().write(0xCC008000,v,4);};
            byte(8);byte(0x50);word(0x2200);
            byte(8);byte(0x60);word(0);
            byte(8);byte(0x70);word(0x16009);
            byte(0x90);key::memory().write(0xCC008000,3,2);
            const float positions[9]={-0.8f,-0.8f,0.0f,0.8f,-0.8f,0.0f,0.0f,0.8f,0.0f};
            const uint32_t colors[3]={0xFF0000FF,0x00FF00FF,0x0000FFFF};
            for(unsigned v=0;v<3;++v) {for(unsigned c=0;c<3;++c) word(std::bit_cast<uint32_t>(positions[v*3+c]));word(colors[v]);}
            uint32_t pixel=0;
            AuroraReadbackDest result{1,&pixel,4,GX_FALSE};
            if(AuroraReadbackGet(readback,&result,1) && result.found) {
                if(frame==30) std::fprintf(stderr,"Synthetic center pixel=%08X\n",pixel);
                const auto r=(pixel>>16)&255,g=(pixel>>8)&255,b=pixel&255;
                if(r>20 && g>20 && b>20) color_verified=true;
            }
            const AuroraReadbackPeekRequest point{1,AURORA_READBACK_PEEK_ARGB,320,240};
            AuroraReadbackCapturePeeks(readback,&point,1);
            auto bp=[&](uint32_t v){byte(0x61);word(v);};
            bp(0x49000000);bp(0x4A000000|639u|(479u<<10));bp(0x4D000028);bp(0x4E000100);
            bp(0x4B001000);bp(0x52004000); // Synthetic 640x480 XFB copy into guest RAM at 0x20000.
            key::graphics::present();
            std::this_thread::sleep_for(std::chrono::milliseconds(visible?16:2));
        }
        AuroraReadbackDestroyStream(readback);
        if(!color_verified) throw std::runtime_error("Synthetic triangle center pixel did not contain interpolated RGB color");
        std::printf("PASS: synthetic guest GX draws=%llu frames=%llu\n",static_cast<unsigned long long>(key::graphics::draws()),static_cast<unsigned long long>(key::graphics::frames()));
        key::graphics::shutdown();return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"Graphics smoke failed: %s\n",e.what()); return 1; }
}
