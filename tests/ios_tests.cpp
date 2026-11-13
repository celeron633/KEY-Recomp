// SPDX-License-Identifier: GPL-3.0-only
#include "key/ios.h"
#include "key/memory.h"
#include <filesystem>
#include <chrono>
#include <fstream>
#include <string>
#include <cstdio>
static void check(bool v){if(!v)throw std::runtime_error("Native IOS filesystem test failed");}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::runtime_error&){caught=true;}check(caught);}
int main(){
    auto& m=key::memory();m.reset();key::ios::reset();
    const auto root=std::filesystem::current_path()/"private/tests/nand"/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())/"nand";
    key::ios::set_filesystem_root(root.string());CpuContext c{};
    auto string=[&](uint32_t p,const std::string& s){for(size_t i=0;i<=s.size();++i)m.write(p+static_cast<uint32_t>(i),i<s.size()?static_cast<uint8_t>(s[i]):0,1);};
    auto call=[&](uint32_t fn){check(key::ios::invoke(fn,c));return static_cast<int32_t>(c.gpr[3]);};
    string(0x80005000,"/dev/fs");c.gpr[3]=0x80005000;c.gpr[4]=0;const auto fd=call(0x806A9330);check(fd>0);
    auto ioctl=[&](uint32_t command,uint32_t input,uint32_t size,uint32_t output=0,uint32_t out_size=0){c={};c.gpr[3]=fd;c.gpr[4]=command;c.gpr[5]=input;c.gpr[6]=size;c.gpr[7]=output;c.gpr[8]=out_size;return call(0x806A9D00);};
    auto create=[&](const std::string& path,uint32_t command){m.fill(0x80005100,76,0);string(0x80005106,path);m.write(0x80005146,3,1);m.write(0x80005147,1,1);return ioctl(command,0x80005100,76);};
    check(create("/tmp/check",3)==0);check(create("/tmp/check",3)==-105);
    check(create("/tmp/check/a.bin",9)==0);check(std::filesystem::file_size(root/"tmp/check/a.bin")==0);
    string(0x80005000,"/tmp/check/a.bin");c={};c.gpr[3]=0x80005000;c.gpr[4]=3;const auto file=call(0x806A9330);check(file>0);
    m.write(0x80006000,0x12345678,4);c={};c.gpr[3]=file;c.gpr[4]=0x80006000;c.gpr[5]=4;check(call(0x806A98E0)==4);
    c={};c.gpr[3]=file;c.gpr[4]=1;c.gpr[5]=0;check(call(0x806A9AD0)==1);
    c={};c.gpr[3]=file;c.gpr[4]=0x80006100;c.gpr[5]=2;check(call(0x806A96D0)==2 && m.read(0x80006100,2)==0x3456);
    c={};c.gpr[3]=file;c.gpr[4]=11;c.gpr[7]=0x80006200;c.gpr[8]=8;check(call(0x806A9D00)==0 && m.read(0x80006200,4)==4 && m.read(0x80006204,4)==3);
    c={};c.gpr[3]=file;check(call(0x806A9520)==0);
    string(0x80005000,"/tmp/check/a.bin");check(ioctl(6,0x80005000,64,0x80006300,76)==0 && m.read(0x80006346,1)==3 && m.read(0x80006347,1)==1);
    m.fill(0x80005200,128,0);string(0x80005200,"/tmp/check/a.bin");string(0x80005240,"/tmp/check/b.bin");check(ioctl(8,0x80005200,128)==0);
    check(!std::filesystem::exists(root/"tmp/check/a.bin") && std::filesystem::exists(root/"tmp/check/b.bin"));
    string(0x80005000,"/tmp/check/b.bin");c={};c.gpr[3]=fd;c.gpr[4]=7;c.gpr[5]=0x80005000;c.gpr[6]=64;c.gpr[9]=0x80009900;c.gpr[10]=42;
    check(call(0x806A9BC0)==0);auto done=key::ios::next_completion();check(done && done->callback==0x80009900 && done->argument==42 && done->result==0);
    check(!std::filesystem::exists(root/"tmp/check/b.bin") && !std::filesystem::is_empty(root.parent_path()/"nand-deleted"));
    check(ioctl(7,0x80005000,64)==-106);
    string(0x80005000,"/../outside");c={};c.gpr[3]=0x80005000;c.gpr[4]=1;rejects([&]{call(0x806A9330);});
    string(0x80005000,"/tmp/NUL");c={};c.gpr[3]=0x80005000;c.gpr[4]=1;rejects([&]{call(0x806A9330);});
    check(ioctl(2,0,0,0x80006400,28)==0 && m.read(0x80006400,4)==16384);
    key::ios::set_filesystem_root(root.string());string(0x80005000,"/tmp/check");check(ioctl(6,0x80005000,64,0x80006300,76)==0 && m.read(0x80006347,1)==1);
    string(0x80005000,"/dev/stm/immediate");c={};c.gpr[3]=0x80005000;const auto stm=call(0x806A9330);check(stm>0);
    auto dim=[&](uint32_t mode){m.fill(0x80006500,32,0);m.write(0x80006500,mode,4);c={};c.gpr[3]=stm;c.gpr[4]=0x5001;c.gpr[5]=0x80006500;c.gpr[6]=32;c.gpr[7]=0x80006600;c.gpr[8]=32;return call(0x806A9D00);};
    check(dim(0x92)==0 && key::ios::video_dimming());check(dim(0x12)==0 && !key::ios::video_dimming());
    check(dim(0)==0 && !key::ios::video_dimming());rejects([&]{dim(0x80);});
    std::ofstream(root.parent_path()/"nand-attributes.txt",std::ios::trunc)<<"broken metadata\n";
    rejects([&]{key::ios::set_filesystem_root(root.string());});
    std::puts("PASS: native NAND create/read/write/seek/stat/rename/archive, async callbacks, metadata persistence and path boundaries");
}
