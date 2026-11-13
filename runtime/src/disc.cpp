// SPDX-License-Identifier: GPL-3.0-only
#include "key/disc.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>
namespace key {
static std::vector<uint8_t> small_file(const std::filesystem::path& p,size_t max) {
    const auto n=std::filesystem::file_size(p);
    if(n>max) throw std::runtime_error("Disc metadata exceeds size limit");
    std::vector<uint8_t> bytes(n);
    std::ifstream file(p,std::ios::binary);
    file.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
    if(!file) throw std::runtime_error("Disc metadata read failed");
    return bytes;
}
static uint32_t word(const std::vector<uint8_t>& b,size_t p) {
    if(p>b.size() || b.size()-p<4) throw std::runtime_error("Truncated disc metadata");
    return (uint32_t(b[p])<<24)|(uint32_t(b[p+1])<<16)|(uint32_t(b[p+2])<<8)|b[p+3];
}
ExtractedDisc::ExtractedDisc(const std::filesystem::path& data_root,const std::filesystem::path& layout_root) {
    const auto geometry=small_file(layout_root/"geometry.bin",24);
    if(geometry.size()!=24 || word(geometry,0)!=0x4B524430 || word(geometry,4)!=1) throw std::runtime_error("Invalid private disc geometry");
    capacity_=(uint64_t(word(geometry,8))<<32)|word(geometry,12);
    partition_offset_=(uint64_t(word(geometry,16))<<32)|word(geometry,20);
    if(capacity_!=4699979776ull && capacity_!=8511160320ull) throw std::runtime_error("Unsupported disc geometry");
    system_area_=small_file(layout_root/"system-area.bin",0x50000);
    const auto root=std::filesystem::weakly_canonical(data_root);
    const auto ticket=small_file(root/"ticket.bin",0x2A4);
    if(ticket.size()!=0x2A4 || word(ticket,0)!=0x10001 || word(ticket,0x1DC)!=0x00010000 || word(ticket,0x1E0)!=0x524B3545)
        throw std::runtime_error("Mounted disc ticket metadata mismatch");
    ticket_view_.assign(0xD8,0);
    ticket_view_[0]=ticket[0x1BC];
    std::copy(ticket.begin()+0x1D0,ticket.end(),ticket_view_.begin()+4);
    const auto boot=small_file(root/"sys/boot.bin",0x440);
    const auto fst=small_file(root/"sys/fst.bin",0x100000);
    auto add=[&](uint64_t offset,uint64_t size,const std::filesystem::path& relative) {
        if(!size) return;
        const auto host=std::filesystem::weakly_canonical(root/relative);
        auto a=root.begin(),b=host.begin();
        for(;a!=root.end();++a,++b) if(b==host.end() || *a!=*b) throw std::runtime_error("Disc filename escaped data root");
        if(std::filesystem::file_size(host)!=size) throw std::runtime_error("Extracted file size differs from FST: "+relative.string());
        segments_.push_back({offset,size,host});
    };
    add(0,boot.size(),"sys/boot.bin");
    add(0x440,0x2000,"sys/bi2.bin");
    add(0x2440,std::filesystem::file_size(root/"sys/apploader.img"),"sys/apploader.img");
    add(uint64_t(word(boot,0x420))*4,std::filesystem::file_size(root/"sys/main.dol"),"sys/main.dol");
    add(uint64_t(word(boot,0x424))*4,fst.size(),"sys/fst.bin");
    const auto count=word(fst,8);
    if(!count || uint64_t(count)*12>fst.size() || word(fst,0)>>24!=1) throw std::runtime_error("Invalid FST count");
    struct Directory { uint32_t index,end; std::filesystem::path path; };
    std::vector<Directory> stack{{0,count,"files"}};
    for(uint32_t i=1;i<count;++i) {
        while(!stack.empty() && i>=stack.back().end) stack.pop_back();
        if(stack.empty()) throw std::runtime_error("Invalid FST directory nesting");
        const auto descriptor=word(fst,i*12);
        const auto name_pos=uint64_t(count)*12+(descriptor&0xFFFFFF);
        if(name_pos>=fst.size()) throw std::runtime_error("FST name outside string table");
        std::string name;
        size_t p=name_pos;
        for(;p<fst.size() && fst[p];++p) name+=static_cast<char>(fst[p]);
        if(p==fst.size() || name.empty() || name=="." || name==".." || name.find_first_of("/\\:")!=std::string::npos)
            throw std::runtime_error("Invalid FST component");
        auto relative=stack.back().path/std::filesystem::u8path(name);
        const auto a=word(fst,i*12+4),b=word(fst,i*12+8);
        if(descriptor>>24) {
            if(descriptor>>24!=1 || a!=stack.back().index || b<=i || b>stack.back().end) throw std::runtime_error("Invalid FST directory range");
            stack.push_back({i,b,relative});
        } else add(uint64_t(a)*4,b,relative);
    }
    std::sort(segments_.begin(),segments_.end(),[](auto& a,auto& b){return a.begin<b.begin;});
    for(size_t i=1;i<segments_.size();++i)
        if(segments_[i-1].begin+segments_[i-1].size>segments_[i].begin) throw std::runtime_error("Overlapping disc file extents");
}
std::vector<uint8_t> ExtractedDisc::read_system(uint64_t offset,uint32_t length) const {
    if(offset>system_area_.size() || length>system_area_.size()-offset) throw std::runtime_error("Disc system read outside extracted unencrypted region");
    return std::vector<uint8_t>(system_area_.begin()+offset,system_area_.begin()+offset+length);
}
std::vector<uint8_t> ExtractedDisc::read(uint64_t offset,uint32_t length) const {
    if(length>0x1000000 || offset+length<offset) throw std::runtime_error("Disc read extent rejected");
    std::vector<uint8_t> result(length);
    size_t done=0;
    while(done<length) {
        const auto cursor=offset+done;
        auto next=std::upper_bound(segments_.begin(),segments_.end(),cursor,[](uint64_t a,const Segment& b){return a<b.begin;});
        if(next==segments_.begin()) throw std::runtime_error("Disc read outside extracted extents");
        const auto& seg=*std::prev(next);
        const auto end=seg.begin+seg.size;
        if(cursor<end) {
            const auto count=std::min<uint64_t>(length-done,end-cursor);
            std::ifstream file(seg.file,std::ios::binary);
            file.seekg(static_cast<std::streamoff>(cursor-seg.begin));
            file.read(reinterpret_cast<char*>(result.data()+done),count);
            if(!file) throw std::runtime_error("Local disc file read failed");
            done+=count;
        } else {
            auto pad_end=(end+31)&~uint64_t(31);
            if(next!=segments_.end()) pad_end=std::min(pad_end,next->begin);
            if(cursor>=pad_end) throw std::runtime_error("Disc read needs data outside extracted files");
            done+=std::min<uint64_t>(length-done,pad_end-cursor); // Defined zero padding to DVD's transfer alignment only.
        }
    }
    return result;
}
}
