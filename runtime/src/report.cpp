// SPDX-License-Identifier: GPL-3.0-only
#include "key/report.h"
#include "key/memory.h"
#include <cstdio>
#include <fstream>
#include <cctype>
namespace key {
static std::string sink;
void set_report_path(std::string path) { sink=std::move(path); }
static std::string guest_string(uint32_t address) {
    if(!address) return "(null)";
    std::string text;
    for(unsigned i=0;i<16384;++i) {
        if(address+i<address) throw std::runtime_error("OSReport string address overflow");
        char ch=static_cast<char>(memory().read(address+i,1));
        if(!ch) return text;
        text+=ch;
    }
    throw std::runtime_error("OSReport unterminated string");
}
std::string format_report(const CpuContext& ctx) {
    auto format=guest_string(ctx.gpr[3]);
    unsigned reg=4;
    auto argument=[&]() -> uint32_t {
        const unsigned index=reg++;
        if(index<=10) return ctx.gpr[index];
        if(index>40) throw std::runtime_error("OSReport argument limit");
        return static_cast<uint32_t>(memory().read(ctx.gpr[1]+8+(index-11)*4,4));
    };
    std::string output;
    for(size_t i=0;i<format.size();++i) {
        if(format[i]!='%') { output+=format[i]; continue; }
        if(++i==format.size()) throw std::runtime_error("OSReport truncated conversion");
        if(format[i]=='%') { output+='%'; continue; }
        std::string flags;
        while(i<format.size() && std::string("-+ #0").find(format[i])!=std::string::npos) flags+=format[i++];
        unsigned width=0;
        while(i<format.size() && std::isdigit(static_cast<unsigned char>(format[i]))) {
            width=width*10+(format[i++]-'0');
            if(width>128) throw std::runtime_error("OSReport field width limit");
        }
        if(i<format.size() && format[i]=='l') { ++i; if(i<format.size() && format[i]=='l') throw std::runtime_error("OSReport 64-bit arguments not implemented"); }
        if(i==format.size()) throw std::runtime_error("OSReport truncated conversion");
        const char type=format[i];
        if(std::string("sdiuxXcp").find(type)==std::string::npos) throw std::runtime_error("OSReport unsupported conversion");
        const auto value=argument();
        std::string piece;
        if(type=='s') {
            piece=guest_string(value);
            if(piece.size()<width) {
                const std::string pad(width-piece.size(),' ');
                if(flags.find('-')!=std::string::npos) piece+=pad; else piece=pad+piece;
            }
        } else {
            char buffer[256];
            const std::string fmt=type=='p'?"0x%08x":"%"+flags+(width?std::to_string(width):"")+type;
            const int n=(type=='d'||type=='i'||type=='c') ? std::snprintf(buffer,sizeof(buffer),fmt.c_str(),static_cast<int32_t>(value)) :
                std::snprintf(buffer,sizeof(buffer),fmt.c_str(),value);
            if(n<0 || n>=static_cast<int>(sizeof(buffer))) throw std::runtime_error("OSReport formatting failure");
            piece.assign(buffer,n);
        }
        output+=piece;
    }
    return output;
}
void report(const CpuContext& ctx) {
    if(sink.empty()) throw std::runtime_error("OSReport sink missing");
    auto text=format_report(ctx);
    std::ofstream file(sink,std::ios::binary|std::ios::app);
    file.write(text.data(),static_cast<std::streamsize>(text.size()));
    if(!file) throw std::runtime_error("OSReport sink write failed");
    std::fprintf(stderr,"HLE OSReport bytes=%zu\n",text.size());
}
}
