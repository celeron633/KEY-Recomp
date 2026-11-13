// SPDX-License-Identifier: GPL-3.0-only
// GX command formats follow the hardware definitions and Aurora's MIT GX_AURORA interface.
#include "command_bridge.h"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <bit>

namespace key::graphics {
namespace {
uint32_t be32(std::span<const uint8_t> b,size_t p=0) { return (uint32_t(b[p])<<24)|(uint32_t(b[p+1])<<16)|(uint32_t(b[p+2])<<8)|b[p+3]; }
uint16_t be16(std::span<const uint8_t> b,size_t p=0) { return uint16_t((b[p]<<8)|b[p+1]); }
void u32(std::vector<uint8_t>& b,uint32_t v) { for(int i=24;i>=0;i-=8) b.push_back(static_cast<uint8_t>(v>>i)); }
void u64(std::vector<uint8_t>& b,uint64_t v) { u32(b,static_cast<uint32_t>(v>>32)); u32(b,static_cast<uint32_t>(v)); }
void u16(std::vector<uint8_t>& b,uint16_t v) { b.push_back(v>>8); b.push_back(static_cast<uint8_t>(v)); }
size_t scalar_size(unsigned type) {
    constexpr unsigned sizes[]={1,1,2,2,4};
    if(type>=5) throw std::runtime_error("Invalid GX scalar vertex type");
    return sizes[type];
}
size_t texture_size(unsigned format,unsigned width,unsigned height,unsigned max_lod=0) {
    unsigned bw,bh,block;
    switch(format) {
    case 0: case 8: case 14: bw=8;bh=8;block=32;break;
    case 1: case 2: case 9: bw=8;bh=4;block=32;break;
    case 3: case 4: case 5: case 10: bw=4;bh=4;block=32;break;
    case 6: bw=4;bh=4;block=64;break;
    default: throw std::runtime_error("Unsupported GX texture format");
    }
    size_t total=0;
    for(unsigned level=0;level<=std::min(max_lod,10u);++level) {
        total+=((width+bw-1)/bw)*((height+bh-1)/bh)*block;
        if(width==1 && height==1) break;
        width=std::max(width/2,1u); height=std::max(height/2,1u);
    }
    return total;
}
}
CommandBridge::CommandBridge(Emit emit,Resolve resolve,Writable writable)
    :emit_(std::move(emit)),resolve_(std::move(resolve)),writable_(std::move(writable)),palette_memory_(0x80000),palette_valid_(0x80000/32) {}
void CommandBridge::extension(uint16_t command,std::span<const uint8_t> payload) {
    std::vector<uint8_t> bytes{0x50}; u16(bytes,command); bytes.insert(bytes.end(),payload.begin(),payload.end()); emit_(bytes);
}
size_t CommandBridge::vertex_size(unsigned fmt) const {
    const auto lo=cp_[0x50],hi=cp_[0x60],a=cp_[0x70+fmt],b=cp_[0x80+fmt],c=cp_[0x90+fmt];
    size_t size=0;
    for(unsigned i=0;i<9;++i) size+=(lo>>i)&1;
    auto field=[&](unsigned desc,size_t direct,unsigned indices=1) {
        if(desc==1) size+=direct;
        else if(desc==2) size+=indices;
        else if(desc==3) size+=indices*2;
    };
    if((lo>>9)&3) field((lo>>9)&3,(((a&1)?3:2)*scalar_size((a>>1)&7)));
    if((lo>>11)&3) field((lo>>11)&3,((a&(1u<<9))?9:3)*scalar_size((a>>10)&7),((a&(1u<<9))&&(a>>31))?3:1);
    constexpr unsigned colors[]={2,3,4,2,3,4};
    for(unsigned i=0;i<2;++i) {
        auto desc=(lo>>(13+i*2))&3,type=(a>>(14+i*4))&7;
        if(desc) { if(type>=6) throw std::runtime_error("Invalid GX packed color type"); field(desc,colors[type]); }
    }
    const std::array<uint32_t,8> formats{a>>21,b,b>>9,b>>18,b>>27,c>>5,c>>14,c>>23};
    for(unsigned i=0;i<8;++i) if((hi>>(i*2))&3) field((hi>>(i*2))&3,((formats[i]&1)?2:1)*scalar_size((formats[i]>>1)&7));
    return size;
}
size_t CommandBridge::packet_size(std::span<const uint8_t> data) const {
    if(data.empty()) return 0;
    const auto op=data[0]&0xF8;
    if(op==0 || op==0x48) return 1;
    if(op==8) return 6;
    if(op==0x60 || (op>=0x20 && op<=0x38)) return 5;
    if(op==0x40) return 9;
    if(op==0x10) return data.size()<5?0:5+size_t((be32(data,1)>>16)+1)*4;
    if(op>=0x80 && op<=0xB8) return data.size()<3?0:3+be16(data,1)*vertex_size(data[0]&7);
    throw std::runtime_error("Unsupported guest GX opcode "+std::to_string(data[0]));
}
void CommandBridge::feed(std::span<const uint8_t> bytes) {
    if(pending_.size()+bytes.size()>0x1000000) throw std::runtime_error("GX FIFO packet buffer exceeded 16 MiB");
    pending_.insert(pending_.end(),bytes.begin(),bytes.end());
    size_t cursor=0;
    while(cursor<pending_.size()) {
        const auto remaining=std::span<const uint8_t>(pending_).subspan(cursor);
        auto size=packet_size(remaining);
        if(!size || size>remaining.size()) break;
        packet(remaining.first(size),0); cursor+=size;
    }
    if(cursor) pending_.erase(pending_.begin(),pending_.begin()+cursor);
}
void CommandBridge::display_list(std::span<const uint8_t> bytes,unsigned depth) {
    if(depth>16) throw std::runtime_error("Nested GX display-list recursion exceeded bound");
    size_t p=0;
    while(p<bytes.size()) {
        auto size=packet_size(bytes.subspan(p));
        if(!size || size>bytes.size()-p) throw std::runtime_error("Truncated guest GX display-list command");
        packet(bytes.subspan(p,size),depth); p+=size;
    }
}
void CommandBridge::packet(std::span<const uint8_t> data,unsigned depth) {
    const auto op=data[0]&0xF8;
    if(op==0) return;
    if(op==8) {
        const auto reg=data[1]; const auto value=be32(data,2); cp_[reg]=value;
        if(reg>=0xA0 && reg<=0xAF) {
            array_assigned_[reg-0xA0]=true;array_sizes_[reg-0xA0]=0;return;
        }
    } else if(op==0x60) { bp_write(be32(data,1)); return; }
    else if(op==0x40) {
        const auto address=be32(data,1),size=be32(data,5);
        if(size>0x1000000) throw std::runtime_error("GX display-list extent exceeded bound");
        display_list(resolve_(address,size).first(size),depth+1); return;
    } else if(op>=0x20 && op<=0x38) {
        const unsigned array=12+(op-0x20)/8;
        const unsigned index=be16(data,1),length=(be16(data,3)>>12)+1;
        bind_array(array,size_t(index)*(cp_[0xB0+array]&255)+length*4);
    } else if(op>=0x80 && op<=0xB8) { prepare_arrays(data);prepare_textures(); ++draws_; }
    emit_(data);
}
void CommandBridge::bind_array(unsigned index,size_t required) {
    if(!required)return;
    if(!array_assigned_[index])throw std::runtime_error("Indexed GX read without an array base");
    if(required<=array_sizes_[index])return;
    const auto view=resolve_(cp_[0xA0+index],required);
    // GX supplies a base and stride, not a byte length. Bound the upload by
    // indices actually consumed; modest geometric growth avoids repeated uploads.
    const auto size=std::min(std::bit_ceil(required),view.size());array_sizes_[index]=size;
    std::vector<uint8_t> payload;u64(payload,reinterpret_cast<uintptr_t>(view.data()));u32(payload,static_cast<uint32_t>(size));payload.push_back(0);
    extension(0x10+index,payload);
}
void CommandBridge::prepare_arrays(std::span<const uint8_t> draw) {
    const uint32_t fmt=draw[0]&7,lo=cp_[0x50],hi=cp_[0x60],a=cp_[0x70+fmt],b=cp_[0x80+fmt],c=cp_[0x90+fmt];
    std::array<size_t,12> required{};size_t cursor=3;
    const auto matrices=std::popcount(lo&511u);
    const std::array<uint32_t,8> tex{a>>21,b,b>>9,b>>18,b>>27,c>>5,c>>14,c>>23};
    constexpr unsigned colors[]={2,3,4,2,3,4};
    for(unsigned vertex=0;vertex<be16(draw,1);++vertex) {
        cursor+=matrices;bool skipped=false;
        auto field=[&](unsigned array,unsigned descriptor,size_t element,unsigned indices=1) {
            if(!descriptor)return;
            if(descriptor==1){cursor+=element;return;}
            for(unsigned i=0;i<indices;++i) {
                const auto index=descriptor==2?uint32_t(draw[cursor]):uint32_t(be16(draw,cursor));cursor+=descriptor==2?1:2;
                if(array==0 && index==(descriptor==2?255u:65535u))skipped=true;
                if(!skipped)required[array]=std::max(required[array],size_t(index)*(cp_[0xB0+array]&255)+element);
            }
        };
        const auto pos=(lo>>9)&3;if(pos)field(0,pos,((a&1)?3:2)*scalar_size((a>>1)&7));
        const auto normal=(lo>>11)&3;if(normal){const bool nbt=a&(1u<<9),three=nbt&&(a>>31);field(1,normal,(nbt&&(normal==1 || !three)?9:3)*scalar_size((a>>10)&7),three?3:1);}
        for(unsigned i=0;i<2;++i){const auto desc=(lo>>(13+i*2))&3,type=(a>>(14+i*4))&7;if(desc){if(type>=6)throw std::runtime_error("Invalid indexed color format");field(2+i,desc,colors[type]);}}
        for(unsigned i=0;i<8;++i){const auto desc=(hi>>(i*2))&3;if(desc)field(4+i,desc,((tex[i]&1)?2:1)*scalar_size((tex[i]>>1)&7));}
    }
    if(cursor!=draw.size())throw std::runtime_error("GX vertex descriptor/packet extent mismatch");
    for(unsigned i=0;i<12;++i)bind_array(i,required[i]);
}
void CommandBridge::bp_write(uint32_t value) {
    const unsigned reg=value>>24;
    if(reg==0xFE) { bp_mask_=value&0xFFFFFF; return; }
    const uint32_t merged=(bp_[reg]&~bp_mask_)|(value&bp_mask_);
    bp_[reg]=merged; bp_mask_=0xFFFFFF;
    if(reg==0x65) {
        const uint32_t target=(merged&0x3FF)*512, size=((merged>>10)&0x7FF)*32;
        const uint32_t source=(bp_[0x64]&0x1FFFFF)<<5;
        if(!size || target>palette_memory_.size() || size>palette_memory_.size()-target) throw std::runtime_error("Invalid TLUT upload extent");
        const auto view=resolve_(source,size);
        std::copy_n(view.begin(),size,palette_memory_.begin()+target);
        for(unsigned i=target/32;i<(target+size)/32;++i) palette_valid_[i]=true;
        return;
    }
    uint32_t forwarded=merged;
    const bool texture_register=(reg>=0x80 && reg<=0x9B)||(reg>=0xA0 && reg<=0xBB);
    unsigned map=0,kind=0;
    if(texture_register) {
        const auto index=reg-0x80;
        map=(index&3)+(index>=0x20?4:0); kind=(index&0x1F)/4;
        if(kind==6) { tlut_binding_[map]=merged; forwarded=(merged&~0x3FFu)|map; }
    }
    if(reg==0x52 && !(merged&(1u<<14))) {
        const auto src=bp_[0x49],extent=bp_[0x4A];
        const uint32_t width=(extent&0x3FF)+1,height=((extent>>10)&0x3FF)+1;
        const bool half=(merged&(1u<<9))!=0;
        const auto dw=std::max(width/(half?2:1),1u),dh=std::max(height/(half?2:1),1u);
        unsigned format=(merged>>3)&15,storage=format;
        const bool depth=(bp_[0x43]&7)==3;
        if(depth) {
            if(format==0) {format=0x30;storage=0;}
            else if(format==1 || format==9 || format==10) {format|=0x10; if(format>=0x19) format|=0x20;storage=1;}
            else if(format==3 || format==12) {format|=0x10;if(format==0x1C) format|=0x20;storage=3;}
            else if(format==6) {format=0x16;storage=6;}
            else throw std::runtime_error("Unsupported depth-copy encoding");
        } else if(!(merged&(1u<<15)) && (format<4 || format>=7)) {
            if(format==1) format=8;
            format|=0x20;
            storage=(format==0x20?0:format==0x22?2:format==0x23||format==0x2B||format==0x2C?3:1);
        }
        std::vector<uint8_t> payload;
        u32(payload,src&0x3FF);u32(payload,(src>>10)&0x3FF);u32(payload,width);u32(payload,height);extension(0x35,payload);
        payload.clear();u32(payload,dw);u32(payload,dh);u32(payload,format);payload.push_back(half);extension(0x36,payload);
        const uint32_t address=(bp_[0x4B]&0xFFFFFF)<<5;
        auto* pointer=writable_(address,texture_size(storage,dw,dh));
        payload.clear();u64(payload,reinterpret_cast<uintptr_t>(pointer));extension(0x37,payload);
    }
    if(reg==0x52 && (merged&(1u<<14))) {
        const auto extent=bp_[0x4A];
        const auto source_height=((extent>>10)&0x3FF)+1;
        auto scale=bp_[0x4E]&0x1FF;
        if(!scale)throw std::runtime_error("Zero XFB copy scale");
        auto lines=(source_height-1)*256/scale+1;
        if(scale>128 && scale<256){while(!(scale&1))scale>>=1;if(source_height%scale==0)++lines;}
        lines=std::min(lines,1024u);
        const auto stride=bp_[0x4D]&0x3FF;
        const auto width=stride?stride*16:((extent&0x3FF)+16)&~15u;
        writable_((bp_[0x4B]&0xFFFFFF)<<5,size_t(width)*lines*2);
    }
    std::vector<uint8_t> command{0x61};u32(command,(reg<<24)|forwarded);emit_(command);
    if(reg==0x52 && (merged&(1u<<14)))++display_copies_;
    if(texture_register && kind==5) {
        const auto address=(merged&0xFFFFFF)<<5;
        const auto view=resolve_(address,1);
        std::vector<uint8_t> payload{static_cast<uint8_t>(map)};u64(payload,reinterpret_cast<uintptr_t>(view.data()));extension(0x3B,payload);
        texture_bound_[map]=true;
    }
    if(reg==0x66) extension(0x43,{});
}
void CommandBridge::prepare_textures() {
    std::array<bool,8> used{};
    const auto stages=((bp_[0]>>10)&15)+1;
    for(unsigned i=0;i<stages;++i) { const auto word=bp_[0x28+i/2]>>((i%2)*12); if(word&64) used[word&7]=true; }
    for(unsigned i=0;i<((bp_[0]>>16)&7);++i) { if(i>=4) throw std::runtime_error("Invalid indirect texture stage count"); used[(bp_[0x27]>>(i*6))&7]=true; }
    for(unsigned map=0;map<8;++map) if(used[map]) {
        if(!texture_bound_[map]) throw std::runtime_error("GX draw uses texture without a guest address");
        const auto base=0x80+(map>=4?0x20:0)+(map&3);
        const auto image=bp_[base+8], format=(image>>20)&15;
        const auto width=(image&1023)+1,height=((image>>10)&1023)+1;
        const auto max_lod=(((bp_[base+4]>>8)&255)+15)/16;
        resolve_((bp_[base+20]&0xFFFFFF)<<5,texture_size(format,width,height,max_lod));
        if(format==8 || format==9 || format==10) {
            const unsigned entries=format==8?16:format==9?256:16384;
            const auto offset=(tlut_binding_[map]&0x3FF)*512;
            const auto fmt=(tlut_binding_[map]>>10)&3;
            if(fmt>2 || offset>palette_memory_.size() || entries*2>palette_memory_.size()-offset) throw std::runtime_error("Invalid palette binding");
            for(unsigned i=offset/32;i<(offset+entries*2+31)/32;++i) if(!palette_valid_[i]) throw std::runtime_error("GX draw reads palette memory not uploaded");
            std::vector<uint8_t> key(palette_memory_.begin()+offset,palette_memory_.begin()+offset+entries*2);
            auto [it,inserted]=palette_snapshots_.try_emplace(key,nullptr);
            if(inserted) {
                palette_bytes_+=key.size();
                if(palette_bytes_>64*1024*1024) throw std::runtime_error("Palette snapshot cache reached bound");
                it->second=std::make_unique<std::vector<uint8_t>>(key);
            }
            std::vector<uint8_t> payload{static_cast<uint8_t>(map)};
            u64(payload,reinterpret_cast<uintptr_t>(it->second->data()));u32(payload,fmt);u16(payload,static_cast<uint16_t>(entries));extension(0x3C,payload);
        }
    }
}
}
