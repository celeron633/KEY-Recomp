// SPDX-License-Identifier: GPL-3.0-only
#include "key/ios.h"
#include "key/memory.h"
#include "key/disc.h"
#include <deque>
#include <map>
#include <string>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>
#include <memory>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <sstream>
namespace key::ios {
static std::map<int32_t,std::string> devices;
static std::deque<Completion> completions;
struct EventHook { uint32_t output, size, callback, argument; int32_t fd; };
static std::optional<EventHook> hook;
static int32_t next_fd=1;
struct OpenFile { std::fstream stream; std::filesystem::path path; uint64_t position=0; uint32_t mode=0; };
static std::map<int32_t,OpenFile> files;
static std::filesystem::path fs_root;
static std::unique_ptr<ExtractedDisc> disc;
static uint32_t cover_register=4, drive_error=0;
void set_disc_root(std::string path,std::string layout_path) { disc=std::make_unique<ExtractedDisc>(path,layout_path); cover_register=4; drive_error=0; }
static bool network_scheduler_suspended=true;
static uint32_t idle_led_mode=0;
static uint32_t network_rtc=0;
static bool dimmed=false;
bool video_dimming(){return dimmed;}
struct Attributes {uint32_t owner=0x1000;uint16_t group=0;uint8_t owner_mode=3,group_mode=3,other_mode=3,attribute=0;};
static std::map<std::string,Attributes> attributes;
static std::filesystem::path checked_path(const std::string& path) {
    if(fs_root.empty() || path.empty() || path[0]!='/' || path.size()>=64 || path.find('\\')!=std::string::npos || path.find(':')!=std::string::npos)
        throw std::runtime_error("Invalid native NAND path");
    auto relative=std::filesystem::path(path.substr(1));
    for(const auto& component:relative) {
        auto name=component.string();
        if(name.empty())continue;
        if(name==".." || name=="." || name.size()>12 || name.back()=='.' || name.back()==' ' || std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32;}))throw std::runtime_error("Invalid NAND path component");
        auto base=name.substr(0,name.find('.'));std::transform(base.begin(),base.end(),base.begin(),[](unsigned char c){return static_cast<char>(std::toupper(c));});
        if(base=="CON" || base=="NUL" || base=="AUX" || base=="PRN" || (base.size()==4 && (base.starts_with("COM")||base.starts_with("LPT")) && base[3]>='0' && base[3]<='9'))throw std::runtime_error("Reserved host device name in NAND path");
    }
    const auto host=std::filesystem::weakly_canonical(fs_root/relative);
    auto a=fs_root.begin(),b=host.begin();
    for(;a!=fs_root.end();++a,++b)if(b==host.end() || *a!=*b)throw std::runtime_error("NAND path escaped user root");
    return host;
}
static void save_attributes(){
    const auto path=fs_root.parent_path()/"nand-attributes.txt";
    std::ofstream out(path,std::ios::trunc);if(!out)throw std::runtime_error("Cannot persist native NAND attributes");
    for(auto& [name,a]:attributes)out<<std::quoted(name)<<' '<<a.owner<<' '<<a.group<<' '<<unsigned(a.owner_mode)<<' '<<unsigned(a.group_mode)<<' '<<unsigned(a.other_mode)<<' '<<unsigned(a.attribute)<<'\n';
    out.flush();if(!out)throw std::runtime_error("Native NAND metadata write failed");
}
void set_filesystem_root(std::string path) {
    fs_root=std::filesystem::weakly_canonical(path);std::filesystem::create_directories(fs_root);
    std::filesystem::create_directories(checked_path("/tmp"));
    std::filesystem::create_directories(checked_path("/title/00010000/524b3545/data"));
    attributes.clear();std::ifstream in(fs_root.parent_path()/"nand-attributes.txt");
    std::string name,line;unsigned owner,group,om,gm,pm,attr;
    while(std::getline(in,line)){
        std::istringstream record(line);
        if(!(record>>std::quoted(name)>>owner>>group>>om>>gm>>pm>>attr))throw std::runtime_error("Malformed NAND attributes");
        std::string extra;if(record>>extra)throw std::runtime_error("Trailing NAND attribute fields");
        checked_path(name);if(group>65535 || om>3 || gm>3 || pm>3 || attr>255)throw std::runtime_error("Invalid NAND attribute record");
        attributes[name]={owner,static_cast<uint16_t>(group),static_cast<uint8_t>(om),static_cast<uint8_t>(gm),static_cast<uint8_t>(pm),static_cast<uint8_t>(attr)};
    }
    if(in.bad())throw std::runtime_error("Cannot read NAND attributes");
}
// Deletion is represented by a reversible namespace move outside the mounted
// NAND. Both canonical endpoints stay inside this private user-data parent.
static int32_t archive_path(const std::string& guest) {
    const auto host=checked_path(guest);
    if(host==fs_root)return -102;
    if(!std::filesystem::exists(host))return -106;
    const auto parent=std::filesystem::weakly_canonical(fs_root.parent_path());
    const auto trash=std::filesystem::weakly_canonical(parent/"nand-deleted");
    if(trash.parent_path()!=parent)throw std::runtime_error("NAND archive directory escaped private user root");
    std::filesystem::create_directories(trash);
    static uint64_t serial=0;
    const auto name=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++serial);
    const auto destination=trash/name;
    std::filesystem::rename(host,destination);
    std::erase_if(attributes,[&](const auto& item){return item.first==guest || item.first.starts_with(guest+"/");});save_attributes();
    return 0;
}
static std::pair<uint32_t,uint32_t> usage(const std::filesystem::path& host) {
    uint64_t blocks=0,inodes=1;
    auto count=[&](const auto& p){if(std::filesystem::is_symlink(p))throw std::runtime_error("NAND usage cannot follow symlinks");if(std::filesystem::is_regular_file(p))blocks+=(std::filesystem::file_size(p)+16383)/16384;};
    count(host);
    if(std::filesystem::is_directory(host))for(auto& entry:std::filesystem::recursive_directory_iterator(host)){count(entry.path());if(++inodes>32768)throw std::runtime_error("Native NAND inode quota exceeded");}
    if(blocks>32768)throw std::runtime_error("Native NAND 512 MiB quota exceeded");
    return {static_cast<uint32_t>(blocks),static_cast<uint32_t>(inodes)};
}
void reset() { files.clear(); devices.clear(); completions.clear(); hook.reset(); next_fd=1; network_scheduler_suspended=true; idle_led_mode=0; network_rtc=0; dimmed=false; }
static std::string path_at(uint32_t p,unsigned maximum=256) {
    std::string s;
    for(unsigned i=0;i<maximum;++i) { if(p+i<p) break; char c=static_cast<char>(memory().read(p+i,1)); if(!c) return s; s+=c; }
    throw std::runtime_error("IOS path unterminated or too long");
}
static int32_t open(uint32_t p,uint32_t mode) {
    auto path=path_at(p);
    if(!path.starts_with("/dev/")) {
        auto host=checked_path(path);
        if(mode<1 || mode>3) return -4;
        if(!std::filesystem::is_regular_file(host)) return -106;
        const auto fd=next_fd++;
        OpenFile file; file.mode=mode;file.path=host;
        auto flags=std::ios::binary|std::ios::in;
        if(mode&2) flags|=std::ios::out;
        file.stream.open(host,flags);
        if(!file.stream) throw std::runtime_error("Native NAND open failed");
        files.emplace(fd,std::move(file));
        std::fprintf(stderr,"IOS file open fd=%d\n",fd);
        return fd;
    }
    if(path!="/dev/stm/immediate" && path!="/dev/stm/eventhook" && path!="/dev/fs" && path!="/dev/es" && path!="/dev/net/kd/request" && path!="/dev/net/kd/time" && path!="/dev/di")
        throw std::runtime_error("IOS device not implemented: "+path);
    if(mode!=(path=="/dev/net/kd/request"?1u:0u)) return -4;
    if(devices.size()>=24) return -5;
    auto fd=next_fd++; devices.emplace(fd,path);
    std::fprintf(stderr,"IOS open %s fd=%d\n",path.c_str(),fd);
    return fd;
}
void stm_event(uint32_t event) {
    if(!hook) return;
    memory().write(hook->output,event,4);
    if(hook->callback) completions.push_back({hook->callback,hook->argument,0});
    hook.reset();
}
std::optional<Completion> next_completion() {
    if(completions.empty()) return {};
    auto c=completions.front(); completions.pop_front(); return c;
}
bool invoke(uint32_t address,CpuContext& c) {
    constexpr uint64_t title_id=0x00010000524B3545ull; // Metadata for the hash-pinned RK5E01 title.
    if(address==0x806FF6A0) { memory().write(c.gpr[3],title_id,8); c.gpr[3]=0; return true; }
    if(address==0x806FF600) {
        if(((uint64_t(c.gpr[3])<<32)|c.gpr[4])!=title_id) throw std::runtime_error("ES data directory requested for unsupported title");
        char path[64]; std::snprintf(path,sizeof(path),"/title/%08x/%08x/data",uint32_t(title_id>>32),uint32_t(title_id));
        for(unsigned i=0;;++i) { memory().write(c.gpr[5]+i,static_cast<uint8_t>(path[i]),1); if(!path[i]) break; }
        c.gpr[3]=0; return true;
    }
    if(address==0x806A9330) { c.gpr[3]=static_cast<uint32_t>(open(c.gpr[3],c.gpr[4])); return true; }
    if(address==0x806A9210) {
        auto result=open(c.gpr[3],c.gpr[4]);
        if(c.gpr[5]) completions.push_back({c.gpr[5],c.gpr[6],result});
        c.gpr[3]=0; return true;
    }
    if(address==0x806A95D0 || address==0x806A96D0) {
        auto it=files.find(static_cast<int32_t>(c.gpr[3])); int32_t result=-4;
        if(it!=files.end() && (it->second.mode&1)) {
            if(c.gpr[5]>0x1000000) throw std::runtime_error("NAND read exceeds bounded request size");
            std::vector<uint8_t> data(c.gpr[5]);
            auto& f=it->second;
            f.stream.clear(); f.stream.seekg(static_cast<std::streamoff>(f.position));
            f.stream.read(reinterpret_cast<char*>(data.data()),data.size());
            result=static_cast<int32_t>(f.stream.gcount());
            if(f.stream.bad()) throw std::runtime_error("Native NAND read failed");
            for(int32_t i=0;i<result;++i) {
                if(uint64_t(c.gpr[4])+i>0xFFFFFFFFull) throw std::runtime_error("NAND read buffer wraps guest memory");
                memory().write(c.gpr[4]+i,data[i],1);
            }
            f.position+=result;
        }
        if(address==0x806A95D0) { if(c.gpr[6]) completions.push_back({c.gpr[6],c.gpr[7],result}); c.gpr[3]=0; }
        else c.gpr[3]=static_cast<uint32_t>(result);
        return true;
    }
    if(address==0x806A97E0 || address==0x806A98E0 || address==0x806A99F0 || address==0x806A9AD0) {
        const bool writing=address==0x806A97E0 || address==0x806A98E0;
        const bool async=address==0x806A97E0 || address==0x806A99F0;
        auto it=files.find(static_cast<int32_t>(c.gpr[3]));int32_t result=-4;
        if(it!=files.end()) {
            auto& f=it->second;
            if(writing && (f.mode&2)) {
                const auto size=c.gpr[5];
                if(size>0x1000000 || uint64_t(c.gpr[4])+size>0x100000000ull)throw std::runtime_error("Invalid NAND write extent");
                std::vector<uint8_t> bytes(size);for(uint32_t i=0;i<size;++i)bytes[i]=static_cast<uint8_t>(memory().read(c.gpr[4]+i,1));
                f.stream.clear();f.stream.seekp(static_cast<std::streamoff>(f.position));
                f.stream.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());f.stream.flush();
                if(!f.stream)throw std::runtime_error("Native NAND write failed");
                f.position+=size;result=static_cast<int32_t>(size);
            }else if(!writing && c.gpr[5]<=2) {
                const int64_t length=std::filesystem::file_size(f.path);
                const int64_t base=c.gpr[5]==0?0:c.gpr[5]==1?static_cast<int64_t>(f.position):length;
                const auto position=base+static_cast<int32_t>(c.gpr[4]);
                if(position>=0 && position<=length && position<=INT32_MAX){f.position=position;result=static_cast<int32_t>(position);}else result=-101;
            }
        }
        if(async){if(c.gpr[6])completions.push_back({c.gpr[6],c.gpr[7],result});c.gpr[3]=0;}else c.gpr[3]=static_cast<uint32_t>(result);
        return true;
    }
    if(address==0x806A9520 || address==0x806A9460) {
        const bool async=address==0x806A9460;
        const auto fd=static_cast<int32_t>(c.gpr[3]);
        int32_t result=0;
        if(!devices.erase(fd) && !files.erase(fd)) result=-4;
        else if(hook && hook->fd==fd) hook.reset();
        if(async) { if(c.gpr[4]) completions.push_back({c.gpr[4],c.gpr[5],result}); c.gpr[3]=0; }
        else c.gpr[3]=static_cast<uint32_t>(result);
        return true;
    }
    if(address==0x806AA060 || address==0x806A9F70) {
        const bool async=address==0x806A9F70;
        auto finish=[&](int32_t result) {
            if(async) { if(c.gpr[8]) completions.push_back({c.gpr[8],c.gpr[9],result}); c.gpr[3]=0; }
            else c.gpr[3]=static_cast<uint32_t>(result);
        };
        auto device=devices.find(static_cast<int32_t>(c.gpr[3]));
        if(device==devices.end()) { finish(-4); return true; }
        if(device->second=="/dev/fs") {
            const auto command=c.gpr[4],inputs=c.gpr[5],outputs=c.gpr[6],vectors=c.gpr[7];
            if(inputs+outputs>4 || uint64_t(vectors)+(inputs+outputs)*8>0x100000000ull){finish(-101);return true;}
            auto vector=[&](unsigned index){return std::pair<uint32_t,uint32_t>{static_cast<uint32_t>(memory().read(vectors+index*8,4)),static_cast<uint32_t>(memory().read(vectors+index*8+4,4))};};
            if(command==12 && inputs==1 && outputs==2) {
                auto [path,n]=vector(0);auto [blocks,bs]=vector(1);auto [nodes,ns]=vector(2);
                if(n!=64 || bs!=4 || ns!=4){finish(-101);return true;}
                const auto host=checked_path(path_at(path,64));if(!std::filesystem::exists(host)){finish(-106);return true;}
                auto [used,count]=usage(host);memory().write(blocks,used,4);memory().write(nodes,count,4);finish(0);return true;
            }
            if(command==4 && inputs==outputs && (inputs==1 || inputs==2)) {
                auto [path,n]=vector(0);if(n!=64){finish(-101);return true;}
                const auto host=checked_path(path_at(path,64));if(!std::filesystem::is_directory(host)){finish(-106);return true;}
                std::vector<std::string> names;for(const auto& entry:std::filesystem::directory_iterator(host)){
                    auto name=entry.path().filename().string();if(name.size()>12 || entry.is_symlink())throw std::runtime_error("Unsupported host entry in native NAND");names.push_back(name);
                }
                std::sort(names.begin(),names.end());
                if(inputs==1){auto [count,size]=vector(1);if(size!=4){finish(-101);return true;}memory().write(count,names.size(),4);}
                else {
                    auto [max_address,max_size]=vector(1);auto [out,size]=vector(2);auto [count,count_size]=vector(3);
                    if(max_size!=4 || count_size!=4){finish(-101);return true;}
                    const auto max_count=static_cast<uint32_t>(memory().read(max_address,4));
                    if(max_count>32768 || size!=max_count*13){finish(-101);return true;}
                    std::vector<uint8_t> bytes(size);uint32_t offset=0,actual=std::min<uint32_t>(max_count,names.size());
                    for(unsigned i=0;i<actual;++i){std::copy(names[i].begin(),names[i].end(),bytes.begin()+offset);offset+=names[i].size()+1;}
                    memory().write_bytes(out,bytes);memory().write(count,actual,4);
                }
                finish(0);return true;
            }
        }
        if(device->second=="/dev/es" && c.gpr[4]==0x1B) {
            if(!disc || c.gpr[5]!=1 || c.gpr[6]!=1 || c.gpr[7]%4 || uint64_t(c.gpr[7])+16>0x100000000ull) { finish(-4); return true; }
            const auto input_length=memory().read(c.gpr[7]+4,4);
            const auto output=static_cast<uint32_t>(memory().read(c.gpr[7]+8,4));
            const auto output_length=memory().read(c.gpr[7]+12,4);
            if(input_length!=0 || output_length!=disc->ticket_view().size()) { finish(-4); return true; }
            memory().write_bytes(output,disc->ticket_view());
            std::fprintf(stderr,"ES supplied mounted disc ticket view (no title key)\n");
            finish(0); return true;
        }
        if(device->second=="/dev/es" && c.gpr[4]==0x16) {
            if(!disc || c.gpr[5]!=1 || c.gpr[6]!=2 || c.gpr[7]%4 || uint64_t(c.gpr[7])+24>0x100000000ull) { finish(-4); return true; }
            const auto& view=disc->ticket_view();
            for(unsigned i=0;i<8;++i) {
                const auto p=0x98+i*8;
                if(view[p]||view[p+1]||view[p+2]||view[p+3]) throw std::runtime_error("Timed title requires persistent consumption accounting");
            }
            const auto count_out=static_cast<uint32_t>(memory().read(c.gpr[7]+16,4));
            if(memory().read(c.gpr[7]+20,4)!=4) { finish(-4); return true; }
            memory().write(count_out,0,4); // No consumption entries exist for this unlimited mounted ticket.
            finish(0); return true;
        }
        throw std::runtime_error("IOS ioctlv not implemented for "+device->second+" request="+std::to_string(c.gpr[4]));
    }
    if(address==0x806A9BC0 || address==0x806A9D00) {
        const bool async=address==0x806A9BC0;
        auto finish=[&](int32_t result) {
            if(async) { if(c.gpr[9]) completions.push_back({c.gpr[9],c.gpr[10],result}); c.gpr[3]=0; }
            else c.gpr[3]=static_cast<uint32_t>(result);
        };
        auto found=devices.find(static_cast<int32_t>(c.gpr[3]));
        if(auto file=files.find(static_cast<int32_t>(c.gpr[3]));file!=files.end()) {
            if(c.gpr[4]!=11)throw std::runtime_error("Unsupported file-descriptor ioctl");
            if(c.gpr[6]!=0 || c.gpr[8]<8){finish(-101);return true;}
            const auto size=std::filesystem::file_size(file->second.path);
            if(size>UINT32_MAX)throw std::runtime_error("NAND file exceeds guest size range");
            memory().write(c.gpr[7],size,4);memory().write(c.gpr[7]+4,file->second.position,4);finish(0);return true;
        }
        if(found==devices.end()) { finish(-4); return true; }
        if(found->second=="/dev/fs") {
            const auto command=c.gpr[4],input=c.gpr[5],size=c.gpr[6],output=c.gpr[7],out_size=c.gpr[8];
            if(command==2){
                if(out_size<28){finish(-101);return true;}auto [blocks,nodes]=usage(fs_root);
                const auto free=std::min<uint64_t>(32768-blocks,std::filesystem::space(fs_root).available/16384);
                const uint32_t values[]={16384,static_cast<uint32_t>(free),blocks,0,0,32768-nodes,nodes};
                for(unsigned i=0;i<7;++i)memory().write(output+i*4,values[i],4);finish(0);return true;
            }
            if(command==3 || command==9 || command==5){
                if(size<76){finish(-101);return true;}
                const auto path=path_at(input+6,64);const auto host=checked_path(path);
                Attributes meta;
                meta.owner_mode=memory().read(input+70,1);meta.group_mode=memory().read(input+71,1);meta.other_mode=memory().read(input+72,1);meta.attribute=memory().read(input+73,1);
                if(meta.owner_mode>3 || meta.group_mode>3 || meta.other_mode>3){finish(-101);return true;}
                if(command==5){if(!std::filesystem::exists(host)){finish(-106);return true;}meta.owner=memory().read(input,4);meta.group=memory().read(input+4,2);}
                else {
                    if(std::filesystem::exists(host)){finish(-105);return true;}
                    if(!std::filesystem::is_directory(host.parent_path())){finish(-106);return true;}
                    if(command==3)std::filesystem::create_directory(host);
                    else {std::ofstream file(host,std::ios::binary);if(!file)throw std::runtime_error("NAND file creation failed");}
                }
                attributes[path]=meta;save_attributes();std::fprintf(stderr,"NAND create/attributes %s\n",path.c_str());finish(0);return true;
            }
            if(command==6 || command==7 || command==8){
                if(size<(command==8?128u:64u)){finish(-101);return true;}
                const auto path=path_at(input,64);const auto host=checked_path(path);
                if(!std::filesystem::exists(host)){finish(-106);return true;}
                if(command==6){
                    if(out_size<76){finish(-101);return true;}
                    const auto meta=attributes.contains(path)?attributes.at(path):Attributes{};
                    memory().fill(output,76,0);memory().write(output,meta.owner,4);memory().write(output+4,meta.group,2);
                    for(size_t i=0;i<path.size();++i)memory().write(output+6+static_cast<uint32_t>(i),static_cast<uint8_t>(path[i]),1);
                    memory().write(output+70,meta.owner_mode,1);memory().write(output+71,meta.group_mode,1);memory().write(output+72,meta.other_mode,1);memory().write(output+73,meta.attribute,1);finish(0);return true;
                }
                for(const auto& [fd,file]:files)if(file.path==host){finish(-111);return true;}
                if(command==7){finish(archive_path(path));return true;}
                const auto target=path_at(input+64,64);const auto destination=checked_path(target);
                if(host==fs_root || destination==fs_root || target.starts_with(path+"/")){finish(-101);return true;}
                if(host==destination){finish(0);return true;}
                if(!std::filesystem::is_directory(destination.parent_path())){finish(-106);return true;}
                if(std::filesystem::exists(destination)){const auto result=archive_path(target);if(result){finish(result);return true;}}
                std::filesystem::rename(host,destination);
                std::map<std::string,Attributes> moved;
                for(const auto& [name,meta]:attributes)if(name==path || name.starts_with(path+"/"))moved[target+name.substr(path.size())]=meta;
                std::erase_if(attributes,[&](const auto& item){return item.first==path || item.first.starts_with(path+"/");});attributes.insert(moved.begin(),moved.end());save_attributes();finish(0);return true;
            }
            if(command==13){for(auto& [fd,file]:files)if(file.mode&2)file.stream.flush();save_attributes();finish(0);return true;}
        }
        if(found->second=="/dev/di") {
            if(!disc) throw std::runtime_error("DI has no mounted local data partition");
            if(c.gpr[6]!=32 || !c.gpr[5]) { finish(32); return true; }
            const auto command=c.gpr[4];
            auto output_word=[&](uint32_t value) { if(c.gpr[8]<4) throw std::runtime_error("DI output buffer too small"); memory().write(c.gpr[7],value,4); };
            if(command==0x70 || command==0x71) {
                const uint32_t length=command==0x70?32:static_cast<uint32_t>(memory().read(c.gpr[5]+4,4));
                const uint64_t offset=command==0x70?0:memory().read(c.gpr[5]+8,4)*4;
                if(c.gpr[8]<length) { finish(32); return true; }
                auto bytes=disc->read(offset,length); memory().write_bytes(c.gpr[7],bytes);
                std::fprintf(stderr,"DI read offset=%llu length=%u\n",static_cast<unsigned long long>(offset),length);
                finish(1); return true;
            }
            if(command==0x12) {
                if(c.gpr[8]<32) { finish(32); return true; }
                memory().fill(c.gpr[7],32,0);
                memory().write(c.gpr[7],2,4);
                memory().write(c.gpr[7]+4,0x20060526,4); memory().write(c.gpr[7]+8,0x41000000,4);
                finish(1); return true;
            }
            if(command==0x7A) { output_word(cover_register); finish(1); return true; }
            if(command==0x86) { cover_register&=~4u; finish(1); return true; }
            if(command==0x85) { cover_register&=~2u; finish(1); return true; }
            if(command==0x89) { cover_register|=2u; finish(1); return true; }
            if(command==0xE0) { output_word(drive_error); drive_error=0; finish(1); return true; }
            if(command==0xA4) { drive_error=0x52000; finish(2); return true; } // Retail Wii drive rejects DVD-video key reporting.
            if(command==0x8D) {
                const auto length=memory().read(c.gpr[5]+4,4),position=memory().read(c.gpr[5]+8,4);
                const auto offset=position*4;
                if(length>disc->capacity() || offset>disc->capacity()-length) {
                    drive_error=0x52100;
                    finish(2); return true;
                }
                if(offset>0x50000 || length>0x50000-offset || c.gpr[8]<length) { finish(32); return true; }
                const auto bytes=disc->read_system(offset,static_cast<uint32_t>(length));
                memory().write_bytes(c.gpr[7],bytes); finish(1); return true;
            }
            throw std::runtime_error("DI command not implemented: "+std::to_string(command));
        }
        if(found->second=="/dev/stm/eventhook" && c.gpr[4]==0x1000) {
            if(!async) throw std::runtime_error("Blocking STM event wait requires scheduler integration");
            if(hook) { c.gpr[3]=static_cast<uint32_t>(-2); return true; }
            if(c.gpr[8]<4 || !c.gpr[7]) { c.gpr[3]=static_cast<uint32_t>(-4); return true; }
            memory().read(c.gpr[7],4); // Validate output before accepting a persistent asynchronous request.
            hook=EventHook{c.gpr[7],c.gpr[8],c.gpr[9],c.gpr[10],static_cast<int32_t>(c.gpr[3])};
            c.gpr[3]=0;
            std::fprintf(stderr,"IOS STM event hook pending (no fabricated completion)\n");
            return true;
        }
        if(found->second=="/dev/stm/immediate" && c.gpr[4]==0x6002) {
            if(c.gpr[6]<4 || !c.gpr[5]) { finish(-4); return true; }
            auto mode=static_cast<uint32_t>(memory().read(c.gpr[5],4));
            if(mode>2) { finish(-4); return true; }
            idle_led_mode=mode; finish(0); return true;
        }
        if(found->second=="/dev/stm/immediate" && c.gpr[4]==0x5001) {
            if(c.gpr[6]<20 || c.gpr[8]<20 || !c.gpr[5] || !c.gpr[7]) { finish(-4); return true; }
            const auto mode=memory().read(c.gpr[5],4);
            if(mode!=0 && mode!=0x12 && mode!=0x92) throw std::runtime_error("Unsupported STM video dimming parameters");
            dimmed=(mode&0x80)!=0;
            memory().fill(c.gpr[7],c.gpr[8],0);
            std::fprintf(stderr,"IOS video dimming %s\n",dimmed?"enabled (native display attenuation)":"disabled");
            finish(0); return true;
        }
        if(found->second=="/dev/net/kd/request") {
            const auto command=c.gpr[4];
            if(command==1 || command==2 || command==3 || command==0x28) {
                if(c.gpr[8]<4 || !c.gpr[7]) { finish(-4); return true; }
                network_scheduler_suspended=command!=3;
                memory().write(c.gpr[7],0,4);
                std::fprintf(stderr,"IOS offline background scheduler state=%s\n",network_scheduler_suspended?"suspended":"idle");
                finish(0); return true;
            }
        }
        if(found->second=="/dev/net/kd/time" && c.gpr[4]==0x17) {
            if(c.gpr[6]<8 || c.gpr[8]<4 || !c.gpr[5] || !c.gpr[7]) { finish(-4); return true; }
            network_rtc=static_cast<uint32_t>(memory().read(c.gpr[5],4));
            const auto flags=memory().read(c.gpr[5]+4,4);
            if(flags>1) throw std::runtime_error("Unknown network RTC flags");
            memory().write(c.gpr[7],0,4);
            std::fprintf(stderr,"IOS network RTC counter synchronized\n");
            finish(0); return true;
        }
        throw std::runtime_error("IOS ioctl not implemented for "+found->second+" request="+std::to_string(c.gpr[4]));
    }
    return false;
}
}
