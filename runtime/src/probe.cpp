// SPDX-License-Identifier: GPL-3.0-only
#include "guest_abi/guest_abi.h"
#include "key/clock.h"
#include "key/report.h"
#include "key/ios.h"
#include "key/scheduler.h"
#include "key/interrupt_progress.h"
#include "key/dispatch_index.h"
#include "key/dispatch_context.h"
#include "key/dispatch_depth.h"
#include "key/timing_metrics.h"
#include "key/poll_clock.h"
#ifdef KEY_WITH_AX
#include "key/ax.h"
#endif
#include <map>
#include <filesystem>
#include <cstring>
#include <algorithm>
#include <charconv>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#include <windows.h>
#include <bcrypt.h>
#include <ctime>
#include <cstdlib>
#include <array>
#ifdef KEY_WITH_GRAPHICS
#include "backend.h"
#include "key/input.h"
#endif

static void validate_digest(const std::vector<uint8_t>& bytes,const char* expected="a73cd2f5b1768268451aa28efa649bcf95461b25a481fc4b6cd053c941cf9079",const char* label="DOL") {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("SHA-256 provider unavailable");
    unsigned char digest[32];
    BCRYPT_HASH_HANDLE hash = nullptr;
    auto status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    if (status >= 0) status = BCryptHashData(hash, const_cast<unsigned char*>(bytes.data()), static_cast<ULONG>(bytes.size()), 0);
    if (status >= 0) status = BCryptFinishHash(hash, digest, sizeof(digest), 0);
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) throw std::runtime_error("DOL hash calculation failed");
    char hex[65];
    for (unsigned i = 0; i < 32; ++i) std::snprintf(hex + i*2, 3, "%02x", digest[i]);
    if (std::string(hex) != expected)
        throw std::runtime_error(std::string("Unsupported ")+label+" SHA-256");
}

struct Registration { uint32_t address; void (*entry)(CpuContext*); const char* name; };
#include "probe_registry.inc"
struct LoadedData { uint32_t address; std::vector<uint8_t> bytes; };
static std::vector<LoadedData> initialized_data;
static uint32_t bss_address = 0, bss_size = 0;
static std::vector<std::pair<uint32_t, uint32_t>> bss_ranges;
static void verify_data_init() {
    size_t preserved = 0, cleared = 0;
    for (const auto& section : initialized_data) {
        for (size_t i = 0; i < section.bytes.size(); ++i)
            if (key::memory().read(section.address + static_cast<uint32_t>(i), 1) != section.bytes[i])
                throw std::runtime_error("Initialized data changed during startup BSS clearing");
        preserved += section.bytes.size();
    }
    for (auto [begin, size] : bss_ranges) {
        for (uint32_t offset = 0; offset < size; ++offset) {
            if (key::memory().read(begin + offset, 1) != 0) throw std::runtime_error("Declared BSS not zero after __init_data");
            ++cleared;
        }
    }
    std::fprintf(stderr, "CHECK data preserved=%zu bytes, BSS zero=%zu bytes\n", preserved, cleared);
}

namespace key {
static DispatchDepths<CpuContext> call_depths;
static uint64_t calls = 0;
static uint64_t call_limit = 50000000;
static std::string private_root;
struct InterruptReturn {};
static bool delivering_interrupt=false;
static bool delivering_ios=false;
static bool delivering_input=false;
static uint32_t interrupt_context=0;
static uint64_t interrupt_count=0,system_time_checks=0;
static void deliver_interrupt(uint32_t resume_address, CpuContext* ctx,bool timer=false) {
    if(timer && memory().read(0x80003020,4)!=0x80663110)throw std::runtime_error("Unrecognized decrementer exception handler");
    CpuContext saved=*ctx;
    saved.pc=resume_address;
    const auto context=static_cast<uint32_t>(memory().read(0x800000D4,4));
    const bool cached_context=(context>=0x80004000 && context<=0x817FFD38) || (context>=0x90000000 && context<=0x937FFD38);
    if(!cached_context || context%4) throw std::runtime_error("Invalid OS current context for external interrupt");
    memory().gpu_writable(context,0x2C8); // Includes mapped MEM2 and executable-write checks.
    for(unsigned i=0;i<32;++i) memory().write(context+i*4,saved.gpr[i],4);
    memory().write(context+0x80,saved.cr,4); memory().write(context+0x84,saved.lr,4);
    memory().write(context+0x88,saved.ctr,4); memory().write(context+0x8C,saved.xer,4);
    for(unsigned i=0;i<32;++i) { memory().write(context+0x90+i*8,saved.fpr[i].raw,8); memory().write(context+0x1C8+i*8,saved.fpr[i].raw,8); }
    memory().write(context+0x194,saved.fpscr,4); memory().write(context+0x198,resume_address,4); memory().write(context+0x19C,saved.msr,4);
    memory().write(context+0x1A0,0,2); memory().write(context+0x1A2,3,2);
    for(unsigned i=0;i<8;++i) memory().write(context+0x1A4+i*4,saved.gqr[i],4);
    delivering_interrupt=true; interrupt_context=context;
    ctx->msr&=~0x8000u; ctx->gpr[3]=timer?8:4; ctx->gpr[4]=context;
    const bool trace=++interrupt_count<=32 || interrupt_count%500==0;
    if(trace)std::fprintf(stderr,"IRQ enter: context=%08X resume=%08X pi=%08X mask=%08X dsp=%04X\n",context,resume_address,static_cast<uint32_t>(memory().read(0xCC003000,4)),static_cast<uint32_t>(memory().read(0xCC003004,4)),static_cast<uint32_t>(memory().read(0xCC00500A,2)));
    try {
        if(timer)decrementer().acknowledge();
        // The native exception bridge has already saved the context normally
        // saved by the assembly entry shim; run the original C callback.
        dispatch(timer?0x80662EE0:0x80668650,ctx);
        throw std::runtime_error("Original external interrupt dispatcher returned without OSLoadContext");
    } catch(const InterruptReturn&) {
        for(unsigned i=0;i<32;++i) if(memory().read(context+i*4,4)!=saved.gpr[i])
            throw std::runtime_error("Interrupt callback modified saved GPR context; continuation support required");
        const auto restored_msr=static_cast<uint32_t>(memory().read(context+0x19C,4));
        // OSSetCurrentContext may enable FP for the current FPU owner. All other
        // execution-control changes still require an explicit continuation model.
        if(memory().read(context+0x198,4)!=resume_address || ((restored_msr^saved.msr)&~0x2000u) ||
           ((restored_msr^saved.msr)&0x2000u && memory().read(0x800000D8,4)!=context)) {
            std::fprintf(stderr,"IRQ restore mismatch pc=%08X expected=%08X msr=%08X expected=%08X\n",static_cast<uint32_t>(memory().read(context+0x198,4)),resume_address,static_cast<uint32_t>(memory().read(context+0x19C,4)),saved.msr);
            throw std::runtime_error("Interrupt callback redirected execution; continuation support required");
        }
        if(memory().read(context+0x80,4)!=saved.cr || memory().read(context+0x84,4)!=saved.lr ||
           memory().read(context+0x88,4)!=saved.ctr || memory().read(context+0x8C,4)!=saved.xer ||
           memory().read(context+0x194,4)!=saved.fpscr)throw std::runtime_error("IRQ changed saved CPU status context");
        for(unsigned i=0;i<32;++i)if(memory().read(context+0x90+i*8,8)!=saved.fpr[i].raw || memory().read(context+0x1C8+i*8,8)!=saved.fpr[i].raw)
            throw std::runtime_error("IRQ changed saved FP context");
        for(unsigned i=0;i<8;++i)if(memory().read(context+0x1A4+i*4,4)!=saved.gqr[i])throw std::runtime_error("IRQ changed saved quantization registers");
        saved.msr=restored_msr;
        memory().write(context+0x1A2,1,2);
        *ctx=saved;
        delivering_interrupt=false; interrupt_context=0;
        MkwApplyHostNiMode(ctx->fpscr);
        if(trace)std::fprintf(stderr,"IRQ return: original context preserved\n");
    } catch(...) { *ctx=saved; delivering_interrupt=false; interrupt_context=0; throw; }
}
static void service_ios(CpuContext* ctx) {
    if(!delivering_interrupt && !delivering_ios && (ctx->msr&0x8000)) {
        for(unsigned count=0;count<64;++count) {
            auto event=key::ios::next_completion();
            if(!event) break;
            CpuContext saved=*ctx;
            delivering_ios=true; ctx->msr&=~0x8000u;
            try {
                dispatch(0x8066AEF0,ctx);
                ctx->gpr[3]=static_cast<uint32_t>(event->result); ctx->gpr[4]=event->argument;
                std::fprintf(stderr,"IOS completion callback=%08X result=%d\n",event->callback,event->result);
                dispatch(event->callback,ctx);
                dispatch(0x8066AF30,ctx);
                dispatch(0x8066B450,ctx);
            } catch(...) { *ctx=saved; delivering_ios=false; MkwApplyHostNiMode(ctx->fpscr); throw; }
            *ctx=saved; delivering_ios=false; MkwApplyHostNiMode(ctx->fpscr);
        }
    }
}
static void service_input(CpuContext* ctx,PollClock& clock) {
#ifdef KEY_WITH_GRAPHICS
    if(delivering_interrupt || delivering_ios || delivering_input)return;
    if(input::poll(clock.get().host))clock.invalidate();
    if(!(ctx->msr&0x8000))return;
    for(unsigned n=0;n<64;++n) {
        const auto event=input::next_completion();if(!event)break;
        const auto saved=*ctx;delivering_input=true;ctx->msr&=~0x8000u;
        try {
            dispatch(0x8066AEF0,ctx);
            ctx->gpr[3]=static_cast<uint32_t>(event->channel);ctx->gpr[4]=static_cast<uint32_t>(event->result);
            dispatch(event->callback,ctx);
            dispatch(0x8066AF30,ctx);
        } catch(...) {*ctx=saved;delivering_input=false;throw;}
        *ctx=saved;delivering_input=false;MkwApplyHostNiMode(ctx->fpscr);
        clock.invalidate();
    }
#endif
}
static void idle_pump(CpuContext& ctx) {
    service_ios(&ctx);
    PollClock clock;
    service_input(&ctx,clock);
    if(memory().poll_devices(clock.get().guest))clock.invalidate();
    for(unsigned n=0,stalled=0;n<16 && (ctx.msr&0x8000) && memory().interrupt_pending();++n) {
        const auto acknowledged=interrupt_acks();
        deliver_interrupt(ctx.lr,&ctx);
        clock.invalidate();
        if(memory().interrupt_pending() && interrupt_acks()==acknowledged){if(++stalled>=4)throw std::runtime_error("Idle device interrupt was not acknowledged");}else stalled=0;
    }
    if((ctx.msr&0x8000) && decrementer().pending(clock.get().guest))deliver_interrupt(ctx.lr,&ctx,true);
}
void dispatch(uint32_t address, CpuContext* ctx) {
    auto& depth=call_depths.get(ctx);
    service_ios(ctx);
    PollClock clock;
    service_input(ctx,clock);
    if(!delivering_interrupt && !delivering_ios && memory().poll_devices(clock.get().guest))clock.invalidate();
    for(unsigned attempt=0,stalled=0; attempt<16 && !delivering_interrupt && (ctx->msr&0x8000) && memory().interrupt_pending(); ++attempt) {
        const auto acknowledged=interrupt_acks();
        deliver_interrupt(address,ctx);
        clock.invalidate();
        if(memory().interrupt_pending() && interrupt_acks()==acknowledged){if(++stalled>=4)throw std::runtime_error("External device interrupt was not acknowledged");}else stalled=0;
    }
    if(!delivering_interrupt && !delivering_ios && !delivering_input && (ctx->msr&0x8000) && decrementer().pending(clock.get().guest))deliver_interrupt(address,ctx,true);
    if(!delivering_interrupt && !delivering_ios && !delivering_input)scheduler::poll(*ctx);
    if (++calls > call_limit) throw std::runtime_error("Probe call budget exceeded during active guest execution");
    if(address==0x806459E0){
        // Hash-pinned symbol metadata identifies Task::PollTask and the game
        // root singleton. Count traversals without changing task execution.
        const auto root=memory().read(0x808E5D84,4);
        if(root && ctx->gpr[3]==root)++timing_counters.game_root_polls;
    }
    if(depth>=256)throw std::runtime_error("Guest call depth exceeded 256");
    if(calls<=5000) std::fprintf(stderr, "CALL %08X lr=%08X sp=%08X\n", address, ctx->lr, ctx->gpr[1]);
    else if(calls%100000==0) std::fprintf(stderr,"PROGRESS guest calls=%llu target=%08X\n",static_cast<unsigned long long>(calls),address);
    if(address==0x806649C0) { key::report(*ctx); return; }
    if(address==0x8022C7B8 && std::getenv("KEY_TRACE_SAVE")) {
        const auto p=ctx->gpr[3];std::array<uint32_t,9> values{};
        const uint32_t offsets[]={0,4,8,0x18,0x1C,0x78,0x7C,0x80,0x2D0};
        for(unsigned i=0;i<9;++i)values[i]=static_cast<uint32_t>(memory().read(p+offsets[i],4));
        static std::array<uint32_t,9> previous{};
        if(values!=previous){previous=values;std::fprintf(stderr,"SAVE op=%u load=%u save=%u prompt=%u answer=%u nand=%u/%u result=%u banner=%u\n",values[0],values[1],values[2],values[3],values[4],values[5],values[6],values[7],values[8]);}
    }
    if(address==0x80004714 || address==0x80004660) {if(ctx->gpr[5])memory().fill(ctx->gpr[3],ctx->gpr[5],static_cast<uint8_t>(ctx->gpr[4]));return;}
    if(address==0x800043C4 || address==0x80691B78) {memory().copy(ctx->gpr[3],ctx->gpr[4],ctx->gpr[5],address==0x80691B78);return;}
    if(address==0x8066B220) {
        if(delivering_interrupt || delivering_ios || delivering_input) {ctx->gpr[3]=0;return;} // Reschedule after the callback restores its context.
        scheduler::select(*ctx,ctx->gpr[3]!=0);return;
    }
#ifdef KEY_WITH_GRAPHICS
    if(address==0x80684DF0) graphics::initialize(private_root,std::getenv("KEY_VISIBLE")==nullptr);
    if(address==0x8067A590) graphics::configure_mode(ctx->gpr[3]);
    if(address==0x8067A000 && graphics::active()) graphics::poll_events();
    if(input::invoke(address,*ctx)) return;
#endif
    if(address==0x8067BBD0) {
        key::memory().ave_write(ctx->gpr[3],ctx->gpr[4],ctx->gpr[5]);
        ctx->gpr[3]=1; return; // Acknowledged transaction to the native AVE configuration model.
    }
    if(key::ios::invoke(address,*ctx)) {
#ifdef KEY_WITH_GRAPHICS
        graphics::set_dimming(ios::video_dimming());
#endif
        service_ios(ctx);return;
    }
    if(address==0x80664450) {
        if(delivering_interrupt && ctx->gpr[3]==interrupt_context) throw InterruptReturn{};
        throw std::runtime_error("OSLoadContext requires native thread/fiber continuation");
    }
    if(address==0x806691B0) {
        key::memory().configure_retail_mapping();
        ctx->msr|=0x30;
        std::fprintf(stderr,"HLE BATConfig: MEM1=24 MiB, MEM2=64 MiB physical/56 MiB cached\n");
        return;
    }
    if (address == 0x60) throw std::runtime_error("Unimplemented low-memory debug vector 0x60 (__OSDBJump boundary)");
    if (address >= 0x806627D0 && address < 0x80662868)
        throw std::runtime_error("Exception template is data-only until native exception dispatch exists");
    static const DispatchIndex<Registration> index(registrations);
    const auto found=index.find(address);
    if(found) {
        const auto& registration=*found;
        const auto created_thread=address==0x8066B4B0?ctx->gpr[3]:0;
        ++depth;
        if(address==0x80647640) std::fprintf(stderr,"CHECK entering original game main\n");
        ctx->pc = address;
        DispatchContextScope context(ctx);
        const auto system_time_begin = address == 0x8066C2C0 ? key::timebase() + 0xFFFFFFFF00000000ull : 0;
        struct TemplateScope {
            bool active, old = false;
            TemplateScope(bool enabled) : active(enabled) { if (active) old=key::memory().template_access(true); }
            void finish() { if(active) { key::memory().template_access(old); active=false; } }
            ~TemplateScope() { finish(); }
        } template_scope(address == 0x80662540);
        try { registration.entry(ctx); }
        catch(const InterruptReturn&) { --depth; throw; }
        catch (...) { std::fprintf(stderr, "UNWIND %08X %s\n", address, registration.name); --depth; throw; }
        --depth;
        if(created_thread && ctx->gpr[3])scheduler::created(created_thread);
        if (address == 0x800042A0) verify_data_init();
        if (address == 0x8066C2C0) {
            const uint64_t actual = (uint64_t(ctx->gpr[3]) << 32) | ctx->gpr[4];
            const auto end = key::timebase() + 0xFFFFFFFF00000000ull;
            if (actual - system_time_begin > end - system_time_begin)
                throw std::runtime_error("Original system time result violates TB-plus-offset contract");
            if(++system_time_checks<=5 || system_time_checks%1000==0)std::fprintf(stderr, "CHECK original system time within sampled TB-plus-offset interval\n");
        }
        if (address == 0x80662540) {
            unsigned checked = 0;
            const auto base_instruction = key::memory().read(0x80662838, 4);
            for (unsigned i=0; i<15; ++i) {
                auto dst = static_cast<uint32_t>(key::memory().read(0x808B8DD0+i*4, 4)) | 0x80000000u;
                for (unsigned off=0; off<0x98; off+=4) {
                    auto expected = off==0x68 ? base_instruction | i : key::memory().read(0x806627D0+off, 4);
                    if (key::memory().read(dst+off,4) != expected) throw std::runtime_error("Installed exception vector differs from patched template");
                }
                ++checked;
            }
            std::fprintf(stderr, "CHECK exception vector copies=%u (installation only; execution unsupported)\n", checked);
        }
        if(calls<=5000) std::fprintf(stderr, "RETURN %08X %s\n", address, registration.name);
        if(address==0x8066E800) std::fprintf(stderr,"CHECK original C++ static initialization completed\n");
        if(address==0x8063ADA4)std::fprintf(stderr,"CHECK original GFL initialization completed\n");
        if(address==0x80006B78)std::fprintf(stderr,"CHECK original Game::Init completed\n");
        template_scope.finish();
        service_ios(ctx);
        return;
    }
    char error[128];
    std::snprintf(error, sizeof(error), "Unregistered guest function/service 0x%08X", address);
    throw std::runtime_error(error);
}
}

static uint32_t be32(const std::vector<uint8_t>& file, size_t off) {
    if (off > file.size() || file.size() - off < 4) throw std::runtime_error("Truncated DOL header");
    return (uint32_t(file[off]) << 24) | (uint32_t(file[off+1]) << 16) | (uint32_t(file[off+2]) << 8) | file[off+3];
}
static std::vector<uint8_t> read_boot_file(const std::filesystem::path& path,size_t min_size,size_t max_size) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input || input.tellg()<static_cast<std::streamoff>(min_size) || input.tellg()>static_cast<std::streamoff>(max_size))
        throw std::runtime_error("Missing or invalid extracted boot file: "+path.filename().string());
    const auto size=static_cast<size_t>(input.tellg()); input.seekg(0);
    std::vector<uint8_t> bytes(size);
    input.read(reinterpret_cast<char*>(bytes.data()),size);
    if(!input) throw std::runtime_error("Extracted boot file read failed");
    return bytes;
}
static std::filesystem::path find_private_root(const char* executable) {
    auto directory=std::filesystem::absolute(executable).parent_path();
    for(unsigned depth=0;depth<6 && !directory.empty();++depth){
        if(std::filesystem::is_regular_file(directory/"extracted/RK5E01/DATA/sys/main.dol") && std::filesystem::is_regular_file(directory/"boot/geometry.bin"))return std::filesystem::weakly_canonical(directory);
        const auto parent=directory.parent_path();if(parent==directory)break;directory=parent;
    }
    throw std::runtime_error("Cannot find prepared local game data beside this build");
}
int main(int argc, char** argv) {
    const bool automatic_launch=argc==1;
    std::vector<std::string> automatic_arguments;
    std::vector<char*> automatic_argv;
    try {
        key::private_root=find_private_root(argv[0]).string();
        if(automatic_launch){
            const auto root=std::filesystem::path(key::private_root);
            std::filesystem::create_directories(root/"reports");
            if(!std::freopen((root/"reports/native-app.log").string().c_str(),"w",stderr))throw std::runtime_error("Cannot open the private application log");
            const auto utc=std::time(nullptr);std::tm local{};
            if(localtime_s(&local,&utc))throw std::runtime_error("Cannot read the local RTC");
            const auto rtc=_mkgmtime64(&local);
            automatic_arguments={argv[0],(root/"extracted/RK5E01/DATA/sys/main.dol").string(),std::to_string(rtc)};
            for(auto& argument:automatic_arguments)automatic_argv.push_back(argument.data());
            argc=static_cast<int>(automatic_argv.size());argv=automatic_argv.data();
            _putenv_s("KEY_VISIBLE","1");
        }
        if(std::getenv("KEY_VISIBLE"))key::call_limit=UINT64_MAX;
        if (argc != 3 && !(argc == 4 && std::string(argv[3]) == "--poison-bss"))
            throw std::runtime_error("Usage: key-boot-probe <validated main.dol> <local-civil Unix seconds> [--poison-bss]");
        uint64_t rtc_seconds = 0;
        const std::string rtc_argument = argv[2];
        const auto parsed = std::from_chars(rtc_argument.data(), rtc_argument.data()+rtc_argument.size(), rtc_seconds);
        if (parsed.ec != std::errc() || parsed.ptr != rtc_argument.data()+rtc_argument.size())
            throw std::runtime_error("Invalid RTC seconds");
        const auto initial_tb = key::rtc_to_ticks(rtc_seconds);
        std::ifstream input(argv[1], std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open local DOL");
        input.seekg(0, std::ios::end);
        if (input.tellg() != 9340736) throw std::runtime_error("Unsupported DOL size");
        input.seekg(0);
        std::vector<uint8_t> file((std::istreambuf_iterator<char>(input)), {});
        if (file.size() < 0x100) throw std::runtime_error("Truncated DOL");
        validate_digest(file);
        std::fprintf(stderr,"TIMING dispatch clock reuse %s\n",key::reuse_poll_clock?"enabled":"disabled (diagnostic reference mode)");
        const auto sys_directory=std::filesystem::path(argv[1]).parent_path();
        const auto boot=read_boot_file(sys_directory/"boot.bin",0x440,0x440);
        if(std::memcmp(boot.data(),"RK5E01",6)!=0 || boot[7]!=0) throw std::runtime_error("Boot header version mismatch");
        const auto bi2=read_boot_file(sys_directory/"bi2.bin",0x2000,0x2000);
        const auto fst=read_boot_file(sys_directory/"fst.bin",12,0x100000);
        if((be32(fst,0)>>24)!=1 || uint64_t(be32(fst,8))*12>fst.size()) throw std::runtime_error("Invalid FST root table");
        const uint32_t fst_address=0x81800000u-static_cast<uint32_t>((fst.size()+31)&~size_t(31));
        const uint32_t bi2_address=fst_address-0x2000;
        key::set_report_path((std::filesystem::absolute(argv[0]).parent_path()/"guest-report.log").string());
        key::memory().reset();
        key::memory().load(0x80000000,boot.data(),0x20,false);
        key::memory().load(bi2_address,bi2.data(),bi2.size(),false);
        key::memory().load(fst_address,fst.data(),fst.size(),false);
        bss_address = be32(file, 0xD8);
        bss_size = be32(file, 0xDC);
        if (argc == 4) key::memory().fill(bss_address, bss_size, 0xA5);
        // Whole MEM1 starts zero; initialized sections take precedence over BSS envelope.
        for (unsigned i = 0; i < 18; ++i) {
            const auto off = be32(file, i*4), address = be32(file, 0x48+i*4), size = be32(file, 0x90+i*4);
            if (!size) continue;
            if (off < 0x100 || off > file.size() || size > file.size()-off) throw std::runtime_error("Invalid DOL section");
            key::memory().load(address, file.data()+off, size, i < 7);
            if (i >= 7) initialized_data.push_back({address, std::vector<uint8_t>(file.begin()+off, file.begin()+off+size)});
        }
        bool terminated = false;
        for (unsigned i = 0; i < 32; ++i) {
            auto address = static_cast<uint32_t>(key::memory().read(bss_init_table + i*8, 4));
            auto size = static_cast<uint32_t>(key::memory().read(bss_init_table + i*8 + 4, 4));
            if (!size) { terminated = true; break; }
            if (address < bss_address || uint64_t(address) + size > uint64_t(bss_address) + bss_size)
                throw std::runtime_error("BSS initializer outside envelope");
            bss_ranges.emplace_back(address, size);
        }
        if (!terminated) throw std::runtime_error("Unterminated BSS initialization table");
        CpuContext ctx{};
        key::memory().add_template_word(0x80662828); // __DBVECTOR
        key::memory().add_template_word(0x80662838); // __OSEVSetNumber
        key::memory().capture_dsp_boot_image(0x808B8E20); // DSPInitCode in hash-pinned RK5E01 DOL.
        // Dolphin SetupWiiMemory source-model baseline; not a captured boot trace.
        key::memory().write(0x800000F4, bi2_address, 4);
        key::memory().write(0x80000020,0x0D15EA5E,4);
        key::memory().write(0x80000024,1,4);
        key::memory().write(0x80000038, fst_address, 4);
        key::memory().write(0x8000003C, fst.size(), 4);
        key::memory().write(0x80003184, 0x80000000, 4);
        key::memory().write(0x800000F8, 243000000, 4);
        key::memory().write(0x800000FC, 729000000, 4);
        key::memory().write(0x800000E4, 0x8008F7B8, 4); // Prior current-thread bootstrap pointer, replaced by __OSThreadInit.
        key::memory().write(0x800000DC, 0, 4);
        key::memory().write(0x800000E0, 0, 4);
        key::memory().write(0x800030D8, 0xFFFFFFFF, 4);
        key::memory().write(0x800030DC, 0, 4);
        key::memory().write(0x800030F0, 0, 4);
        // IOS56 memory metadata (physical MEM2 remains unavailable to this probe).
        key::memory().write(0x8000310C, 0, 4);
        key::memory().write(0x80003110, bi2_address, 4);
        key::memory().write(0x80003124, 0x90000800, 4);
        key::memory().write(0x80003128, 0x935E0000, 4);
        key::memory().write(0x80003130, 0x935E0000, 4);
        key::memory().write(0x80003134, 0x93600000, 4);
        for(auto address : {0x80000028u,0x800000F0u,0x80003100u,0x80003104u}) key::memory().write(address,0x01800000,4);
        key::memory().write(0x80003108,0x81800000,4);
        key::memory().write(0x80003118,0x04000000,4);
        key::memory().write(0x8000311C,0x04000000,4);
        key::memory().write(0x80003120,0x93600000,4);
        key::memory().write(0x8000002C,0x23,4); // Dolphin RVL_Retail3 profile.
        key::memory().write(0x800030E6,0x8201,2);
        key::memory().write(0x80003138,0x11,4);
        key::memory().write(0x80003140,0x0038161E,4);
        key::memory().write(0x80003188,0x0038161E,4); // DOL bootstrap reports the active IOS version, as Dolphin's direct-DOL path does.
        key::memory().write(0x8000319C,0,1); // Cleared IOS boot metadata; selects the single-layer device bounds probe.
        key::memory().write(0x80003144,0x00030110,4);
        key::memory().write(0x80003148,0x93600000,4);
        key::memory().write(0x8000314C,0x93620000,4);
        key::memory().write(0x8000315C,0x80000113,4);
        key::memory().write(0x800000CC,0,4); // NTSC.
        key::memory().write(0x80000030, 0, 4);
        key::memory().write(0x80000034, bi2_address, 4);
        // IOS SetupMemory clears low RAM. No apploader exception-vector snapshot supplied.
        key::memory().write(0x80000060, 0, 4);
        KEY_ResetHardware();
        key::ios::reset();
        key::scheduler::configure(&key::dispatch,&key::idle_pump);
#ifdef KEY_WITH_AX
        const auto coefficients=read_boot_file(std::filesystem::path(key::private_root)/"boot/dsp_coef.bin",4096,4096);
        validate_digest(coefficients,"d7741279c2e8ec5c5fb318f8fbdd6de6bf583520d288e836a5383233a4238179","DSP coefficient table");
        key::ax::configure_coefficients(coefficients);
#endif
        key::ios::set_filesystem_root((std::filesystem::path(key::private_root)/"user/nand").string());
        key::ios::set_disc_root(sys_directory.parent_path().string(),(std::filesystem::path(key::private_root)/"boot").string());
        key::reset_clock(initial_tb);
        ctx.msr = 0x2032;
        ctx.hid0 = 0x0011C664;
        ctx.hid1 = 0x80000000;
        ctx.hid2 = 0xE0000000;
        key::dispatch(be32(file, 0xE0), &ctx);
        throw std::runtime_error("Guest entry returned unexpectedly");
    } catch (const std::exception& error) {
        const bool closed=std::string(error.what())=="Native window closed by user";
        std::fprintf(stderr, "STOP: %s\n", error.what());
#ifdef KEY_WITH_GRAPHICS
        try { key::graphics::shutdown(); } catch(const std::exception& e) { std::fprintf(stderr,"GRAPHICS shutdown: %s\n",e.what()); }
#endif
        try { key::scheduler::shutdown(); } catch(const std::exception& e) { std::fprintf(stderr,"SCHEDULER shutdown: %s\n",e.what()); }
        if(automatic_launch && !closed)MessageBoxA(nullptr,error.what(),"KEY-Recomp: see private/reports/native-app.log",MB_OK|MB_ICONERROR);
        return closed?0:2;
    }
}
