// SPDX-License-Identifier: GPL-3.0-only
#include "key/memory.h"
#include "key/pi.h"
#include "key/ipl.h"
#include "key/video.h"
#include "key/audio_interface.h"
#include "key/gpu_pending.h"
#include <vector>
#include <array>
#include <atomic>
#include <deque>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "key/clock.h"
#ifdef KEY_WITH_AX
#include "key/ax.h"
#endif

namespace key {
struct Memory::State {
    State():bytes_(Memory::mem1_size){}
    std::vector<uint8_t> bytes_;
    bool mem2_mapped_=false;
    std::array<bool, 0x4000> low_known_{};
    std::vector<std::pair<uint32_t, uint32_t>> executable_;
    std::vector<uint32_t> template_words_;
    bool template_access_ = false;
    ProcessorInterface pi_;
    uint32_t ipc_mask_=0;
    uint32_t ipc_control_=0, ipc_message_=0, ipc_irq_flags_=0;
    uint16_t mi_mask_=0, dsp_control_=4;
    uint16_t mi_protection_=0, mi_unknown_=0;
    mutable AudioInterface ai_;
    std::array<uint32_t,3> exi_status_{0x800,0x880,0};
    std::array<std::array<uint32_t,4>,3> exi_registers_{};
    uint32_t si_poll_=0x01EC0000, si_csr_=0, si_status_=0;
    std::array<uint8_t,128> si_buffer_{};
    uint16_t vi_clock_=1;
    IplSettingsDevice ipl_;
    std::array<uint8_t,128> dsp_boot_image_{};
    std::array<uint8_t,65536> boot_aram_{};
    std::array<uint32_t,3> ar_dma_{};
    bool dsp_image_valid_=false;
    mutable bool dsp_mail_valid_=false;
    uint32_t dsp_mail_value_=0,dsp_cpu_mail_=0,dsp_boot_command_=0;
    bool dsp_rom_=true;
    std::array<uint32_t,5> dsp_task_{};
    uint32_t dsp_task_fields_=0;
    uint32_t audio_dma_source_=0,audio_dma_cursor_=0;
    uint16_t audio_dma_control_=0,audio_dma_remaining_=0;
    uint64_t audio_dma_tick_=0,audio_dma_start_irq_=0;
    uint32_t audio_dma_fraction_=0,audio_dma_rate_=32000;
    AudioSink audio_sink_=nullptr;
    mutable std::deque<std::pair<uint32_t,bool>> dsp_mail_queue_;
    unsigned boot_dma_count_=0;
    uint16_t ar_info_=0;
    std::array<uint32_t,3> audio_pll_{};
    FifoSink fifo_sink_=nullptr;
    bool fifo_record_logged_=false;
    void(*fifo_sync_)()=nullptr;
    bool(*fifo_empty_)()=nullptr;
    std::array<uint16_t,0x34> cp_registers_{};
    std::array<uint16_t,8> pe_registers_{};
    bool pe_token_pending_=false,pe_finish_pending_=false;
    std::atomic<uint32_t> gpu_token_event_{0};
    std::atomic<bool> gpu_finish_event_{false};
    std::atomic<uint64_t> write_revision_{1};
    std::array<std::atomic<uint64_t>,0x5800> page_versions_{};
    PendingGpuCopies gpu_copies_;
    VideoInterface video_;
    std::array<std::vector<uint8_t>,256> ave_registers_;
};
Memory::Memory():state_(std::make_unique<State>()){}
Memory::~Memory()=default;
bool Memory::template_access(bool enabled) { bool old = state_->template_access_; state_->template_access_ = enabled; return old; }
bool Memory::interrupt_pending() const { return state_->pi_.pending(); }
void Memory::set_fifo_sink(FifoSink sink) { state_->fifo_sink_=sink; }
void Memory::set_fifo_sync(void(*sync)(),bool(*empty)()) { state_->fifo_sync_=sync; state_->fifo_empty_=empty; }
void Memory::poll_devices() { state_->video_.tick(); state_->pi_.source(0x100,state_->video_.pending()); state_->ai_.tick(timebase()); state_->pi_.source(0x20,state_->ai_.pending()); sync_gpu_events(); poll_audio_dma(timebase()); }
std::span<const uint8_t> Memory::ave_register(uint8_t reg) const { return state_->ave_registers_[reg]; }
void Memory::gpu_finish() { state_->gpu_finish_event_.store(true,std::memory_order_release); }
void Memory::gpu_token(uint16_t token) { state_->gpu_token_event_.store(0x10000u|token,std::memory_order_release); }
void Memory::set_audio_sink(AudioSink sink) {state_->audio_sink_=sink;}
static std::runtime_error access_error(const char* reason, uint32_t address) {
    char message[128];
    std::snprintf(message, sizeof(message), "%s at guest address 0x%08X", reason, address);
    return std::runtime_error(message);
}
uint32_t Memory::offset(uint32_t address, size_t size) const {
    uint32_t off;
    if (address < mem1_size) off = address;
    else if (address >= 0x80000000u && address < 0x80000000u + mem1_size) off = address - 0x80000000u;
    else if (address >= 0xC0000000u && address < 0xC0000000u + mem1_size) off = address - 0xC0000000u;
    else if(state_->mem2_mapped_) {
        uint32_t local, limit;
        if(address>=0x10000000 && address<0x14000000) { local=address-0x10000000; limit=0x04000000; }
        else if(address>=0x90000000 && address<0x93800000) { local=address-0x90000000; limit=0x03800000; }
        else if(address>=0xD0000000 && address<0xD4000000) { local=address-0xD0000000; limit=0x04000000; }
        else throw access_error("Unmapped/MMIO access",address);
        if(size>limit-local) throw access_error("MEM2 access crosses mapping boundary",address);
        return mem1_size+local;
    } else throw access_error("Unmapped/MMIO/MEM2 access", address);
    if (size > mem1_size - off) throw access_error("Guest access crosses memory boundary", address);
    return off;
}
void Memory::configure_retail_mapping() {
    if(read(0x80003100,4)!=mem1_size || read(0x80003104,4)!=mem1_size || read(0x8000311C,4)!=0x04000000 ||
        read(0x80003120,4)!=0x93600000) throw std::runtime_error("Unsupported BAT memory profile");
    state_->bytes_.resize(mem1_size+0x04000000,0);
    state_->mem2_mapped_=true;
}
void Memory::reset() { state_->gpu_copies_.clear(); std::fill(state_->bytes_.begin(), state_->bytes_.end(), 0); state_->executable_.clear(); state_->low_known_.fill(false); state_->template_words_.clear(); state_->template_access_=false; state_->mem2_mapped_=false; state_->pi_.reset(); state_->ipc_mask_=0; state_->mi_mask_=0; state_->dsp_control_=4; state_->ai_.reset(); state_->exi_status_={0x800,0x880,0}; state_->exi_registers_={}; state_->si_poll_=0x01EC0000; state_->si_csr_=state_->si_status_=0; state_->si_buffer_.fill(0); state_->vi_clock_=1; state_->ipl_.reset(); state_->dsp_image_valid_=state_->dsp_mail_valid_=false; state_->dsp_mail_value_=state_->dsp_cpu_mail_=state_->dsp_boot_command_=state_->dsp_task_fields_=0; state_->dsp_rom_=true; state_->dsp_task_={}; state_->dsp_mail_queue_.clear(); state_->audio_dma_source_=state_->audio_dma_cursor_=state_->audio_dma_control_=state_->audio_dma_remaining_=0; state_->audio_dma_tick_=state_->audio_dma_start_irq_=0; state_->audio_sink_=nullptr; state_->boot_dma_count_=0; state_->ar_info_=0; state_->ar_dma_={}; state_->audio_pll_={}; state_->boot_aram_.fill(0); state_->video_.reset(); for(auto& reg:state_->ave_registers_) reg.clear(); state_->fifo_sink_=nullptr; state_->mi_protection_=state_->mi_unknown_=0; state_->ipc_control_=state_->ipc_message_=state_->ipc_irq_flags_=0; state_->fifo_sync_=nullptr; state_->fifo_empty_=nullptr; state_->cp_registers_={}; state_->pe_registers_={}; state_->pe_token_pending_=state_->pe_finish_pending_=false; state_->gpu_token_event_=0; state_->gpu_finish_event_=false; const auto epoch=state_->write_revision_.fetch_add(1)+1; for(auto& page:state_->page_versions_) page.store(epoch); }
void Memory::capture_dsp_boot_image(uint32_t address) {
    for(unsigned i=0;i<128;++i) state_->dsp_boot_image_[i]=static_cast<uint8_t>(read(address+i,1));
    state_->dsp_image_valid_=true;
}
void Memory::ave_write(uint32_t device,uint32_t source,uint32_t size) {
    if(device!=0xE0 || size<2 || size>65 || uint64_t(source)+size>0x100000000ull)
        throw std::runtime_error("Unsupported AVE I2C transaction");
    const auto reg=static_cast<uint8_t>(read(source,1));
    std::vector<uint8_t> value;
    for(uint32_t i=1;i<size;++i) value.push_back(static_cast<uint8_t>(read(source+i,1)));
    state_->ave_registers_[reg]=std::move(value);
}
void Memory::add_template_word(uint32_t address) {
    if (address % 4) throw access_error("Unaligned code template word", address);
    auto off = offset(address, 4);
    if (!std::any_of(state_->executable_.begin(), state_->executable_.end(), [off](auto r) { return off >= r.first && off + 4 <= r.second; }))
        throw access_error("Template word is not inside loaded code", address);
    state_->template_words_.push_back(off);
}
uint64_t Memory::read(uint32_t address, unsigned width) const {
    auto physical = address >= 0xC0000000u ? address - 0xC0000000u : address;
    if(physical>=0x0C000000 && physical<0x0C000068 && width==2 && !(physical%2)) {
        const auto reg=physical-0x0C000000;
        if(reg==0) {
            if(state_->fifo_sync_) state_->fifo_sync_();
            return state_->fifo_empty_ && !state_->fifo_empty_()?4:12;
        }
        if(reg==0x30 || reg==0x32) { if(state_->fifo_sync_) state_->fifo_sync_(); return 0; }
        if(reg==0x38 || reg==0x3A) {
            if(state_->fifo_sync_) state_->fifo_sync_();
            if(!(state_->cp_registers_[1]&0x10))return state_->cp_registers_[reg/2];
            return (state_->pi_.read(0x14)>>(reg==0x38?0:16))&0xFFFF;
        }
        if(reg==2 || reg==4 || reg==6 || reg==0xA || (reg>=0x20 && reg<=0x3E)) return state_->cp_registers_[reg/2];
        throw access_error("Unimplemented CP metric register",address);
    }
    if(physical>=0x0C001000 && physical<0x0C001010 && width==2 && !(physical%2)) {
        const_cast<Memory*>(this)->sync_gpu_events();
        return state_->pe_registers_[(physical-0x0C001000)/2];
    }
    if(physical>=0x0C002000 && physical<0x0C002076) return state_->video_.read(physical-0x0C002000,width);
    if(physical==0x0D000004 && width==4) return state_->ipc_control_;
    if(width==4 && (physical==0x0D800180 || physical==0x0D8001CC || physical==0x0D8001D0)) return 0; // Host audio-clock setup, matches Dolphin's explicit compatibility registers.
    if(width==2 && physical==0x0C005004) return ((state_->dsp_mail_queue_.empty()?state_->dsp_mail_value_:state_->dsp_mail_queue_.front().first)>>16)&((state_->dsp_mail_valid_ || !state_->dsp_mail_queue_.empty()) && !(state_->dsp_control_&4)?0xFFFF:0x7FFF);
    if(width==2 && physical==0x0C005006) {
        auto& self=*const_cast<Memory*>(this);
        if(!state_->dsp_mail_queue_.empty()) {
            self.state_->dsp_mail_value_=state_->dsp_mail_queue_.front().first;state_->dsp_mail_queue_.pop_front();
            if(!state_->dsp_mail_queue_.empty() && state_->dsp_mail_queue_.front().second) {
                self.state_->dsp_control_|=0x80;self.state_->pi_.source(0x40,((state_->dsp_control_>>1)&state_->dsp_control_&0xA8)!=0);
            }
        }
        state_->dsp_mail_valid_=false;return state_->dsp_mail_value_&0xFFFF;
    }
    if(width==2 && physical==0x0C005000) return state_->dsp_cpu_mail_>>16;
    if(width==2 && physical==0x0C005002) return state_->dsp_cpu_mail_&0xFFFF;
    if(width==2 && physical==0x0C005030)return state_->audio_dma_source_>>16;
    if(width==2 && physical==0x0C005032)return state_->audio_dma_source_&0xFFFF;
    if(width==2 && physical==0x0C005034)return 0; // Upper half of the 32-bit length register is reserved.
    if(width==2 && physical==0x0C005036)return state_->audio_dma_control_;
    if(width==2 && physical==0x0C00503A){const_cast<Memory*>(this)->poll_audio_dma(timebase());return state_->audio_dma_remaining_?state_->audio_dma_remaining_-1:0;}
    if(width==2 && physical==0x0C005012) return state_->ar_info_;
    if(physical>=0x0D006480 && physical<0x0D006500) {
        if((width!=1 && width!=2 && width!=4) || physical%width || width>0x0D006500-physical)
            throw access_error("Invalid SI buffer read",address);
        uint64_t result=0;
        for(unsigned i=0;i<width;++i) result=(result<<8)|state_->si_buffer_[physical-0x0D006480+i];
        return result;
    }
    if(physical==0x0C00206C && width==2) return state_->vi_clock_;
    if(physical==0x0D006430 && width==4) return state_->si_poll_;
    if(physical==0x0D006434 && width==4) return state_->si_csr_;
    if(physical==0x0D006438 && width==4) return state_->si_status_;
    if(physical==0x0D006024 && width==4) return 1; // DI_CONFIG: boot ROM descrambler disabled, read-only.
    if (physical>=0x0D006C00 && physical<0x0D006C10 && width==4 && !(physical%4)) return state_->ai_.read(physical-0x0D006C00,timebase());
    for(unsigned i=0;i<3;++i) if(physical==0x0D006800+i*20 && width==4) return state_->exi_status_[i];
    if(physical>=0x0D006800 && physical<0x0D00683C && width==4 && !(physical%4)) {
        auto index=(physical-0x0D006800)/20, reg=(physical-0x0D006800)%20;
        if(reg) return state_->exi_registers_[index][reg/4-1];
    }
    if (physical==0x0C00401C && width==2) return state_->mi_mask_;
    if (physical==0x0C004010 && width==2) return state_->mi_protection_;
    if (physical==0x0C004020 && width==2) return state_->mi_unknown_;
    if (physical==0x0C00500A && width==2) return state_->dsp_control_;
    if (physical >= 0x0C003000 && physical < 0x0C003100) {
        if(width!=4 || address%4) throw access_error("PI requires aligned 32-bit read", address);
        return state_->pi_.read(physical-0x0C003000);
    }
    if (width != 1 && width != 2 && width != 4 && width != 8) throw std::runtime_error("Invalid read width");
    // Gekko scalar integer loads support unaligned RAM addresses. MMIO keeps
    // its device-specific alignment checks; FP instruction adapters check theirs.
    if (width==8 && address%4) throw access_error("Unaligned doubleword guest read", address);
    auto off = offset(address, width);
    state_->gpu_copies_.read(off,width);
    for (unsigned i = 0; i < width && off + i < state_->low_known_.size(); ++i)
        if (!state_->low_known_[off + i]) throw access_error("Boot low-memory contract not established", address);
    uint64_t result = 0;
    for (unsigned i = 0; i < width; ++i) result = (result << 8) | state_->bytes_[off + i];
    return result;
}
void Memory::write(uint32_t address, uint64_t value, unsigned width) {
    auto physical = address >= 0xC0000000u ? address - 0xC0000000u : address;
    if(physical>=0x0C000000 && physical<0x0C000068 && width==2 && !(physical%2)) {
        const auto reg=physical-0x0C000000;
        if(reg==2 || reg==4 || reg==6 || reg==0xA || (reg>=0x20 && reg<=0x3E)) {
            if(reg==2 && (value&0x22)) throw access_error("CP breakpoint mode requires native command tracing",address);
            state_->cp_registers_[reg/2]=static_cast<uint16_t>(value); return;
        }
        throw access_error("Unimplemented CP register write",address);
    }
    if(physical>=0x0C001000 && physical<0x0C00100C && width==2 && !(physical%2)) {
        const auto reg=(physical-0x0C001000)/2;
        sync_gpu_events();
        if(reg==5) {
            if(value&4) state_->pe_token_pending_=false;
            if(value&8) state_->pe_finish_pending_=false;
            state_->pe_registers_[reg]=value&3;
        } else state_->pe_registers_[reg]=static_cast<uint16_t>(value);
        sync_gpu_events(); return;
    }
    if(physical>=0x0C002000 && physical<0x0C002076) {
        state_->video_.write(physical-0x0C002000,static_cast<uint32_t>(value),width);
        state_->pi_.source(0x100,state_->video_.pending()); return;
    }
    if((physical&~31u)==0x0C008000) {
        if(width!=1 && width!=2 && width!=4 && width!=8) throw access_error("Invalid GX FIFO write width",address);
        uint8_t bytes[8];
        for(unsigned i=0;i<width;++i) bytes[i]=static_cast<uint8_t>(value>>(8*(width-1-i)));
        fifo_bytes(bytes,width); return;
    }
    if(physical==0x0D000000 && width==4) { state_->ipc_message_=static_cast<uint32_t>(value); return; }
    if(physical==0x0D000004 && width==4) {
        if(value&~0x3Full) throw access_error("Unknown IPC control bits",address);
        if(value&1) throw access_error("IPC request requires IOS service backend",address);
        state_->ipc_control_=(static_cast<uint32_t>(value)&0x39)|((state_->ipc_control_&6)&~static_cast<uint32_t>(value));
        if(((value&4)&&(state_->ipc_control_&0x10))||((value&2)&&(state_->ipc_control_&0x20))) state_->ipc_irq_flags_|=0x40000000;
        state_->pi_.source(0x4000,(state_->ipc_irq_flags_&state_->ipc_mask_)!=0); return;
    }
    if(physical==0x0D000030 && width==4) { state_->ipc_irq_flags_&=~static_cast<uint32_t>(value); state_->pi_.source(0x4000,(state_->ipc_irq_flags_&state_->ipc_mask_)!=0); return; }
    if(width==4 && (physical==0x0D800180 || physical==0x0D8001CC || physical==0x0D8001D0)) {
        const auto index=physical==0x0D800180?0u:physical==0x0D8001CC?1u:2u;
        const uint32_t masks[]={0x180,0x07FFFFFF,0xD0000000};
        if(value & ~uint64_t(masks[index])) throw access_error("Unvalidated audio PLL configuration",address);
        state_->audio_pll_[index]=static_cast<uint32_t>(value); return;
    }
    if(width==2 && physical==0x0C005000) {state_->dsp_cpu_mail_=(state_->dsp_cpu_mail_&0xFFFF)|(static_cast<uint32_t>(value&0xFFFF)<<16);return;}
    if(width==2 && physical==0x0C005030){state_->audio_dma_source_=(state_->audio_dma_source_&0xFFFF)|(static_cast<uint32_t>(value&0x1FFF)<<16);return;}
    if(width==2 && physical==0x0C005032){state_->audio_dma_source_=(state_->audio_dma_source_&0xFFFF0000)|static_cast<uint32_t>(value&0xFFE0);return;}
    if(width==2 && physical==0x0C005036){
        const bool was_enabled=(state_->audio_dma_control_&0x8000)!=0;
        state_->audio_dma_control_=static_cast<uint16_t>(value);
        if((value&0x8000) && !was_enabled){
            if(!(value&0x7FFF))throw std::runtime_error("Zero-sized audio DMA");
            if(!state_->audio_sink_)throw std::runtime_error("Audio DMA requires native PCM output");
            gpu_view(state_->audio_dma_source_,(value&0x7FFF)*32);
            state_->audio_dma_cursor_=state_->audio_dma_source_;state_->audio_dma_remaining_=value&0x7FFF;
            state_->audio_dma_tick_=timebase();state_->audio_dma_start_irq_=state_->audio_dma_tick_+17;state_->audio_dma_fraction_=0;
        }
        if(!(value&0x8000))state_->audio_dma_start_irq_=0;
        return;
    }
    if(width==2 && physical==0x0C005002) {
        state_->dsp_cpu_mail_=(state_->dsp_cpu_mail_&0xFFFF0000)|static_cast<uint32_t>(value&0xFFFF);
        send_dsp_mail(state_->dsp_cpu_mail_);state_->dsp_cpu_mail_&=0x7FFFFFFF;return;
    }
    if(width==2 && physical==0x0C005012 && value==0x43) { state_->ar_info_=0x43; return; }
    if(physical>=0x0C005020 && physical<0x0C00502C && (width==2 || width==4) && !(physical%width)) {
        const unsigned slot=(physical-0x0C005020)/4;
        if(width==4) state_->ar_dma_[slot]=static_cast<uint32_t>(value);
        else if(physical&2) state_->ar_dma_[slot]=(state_->ar_dma_[slot]&0xFFFF0000)|static_cast<uint16_t>(value);
        else state_->ar_dma_[slot]=(state_->ar_dma_[slot]&0xFFFF)|(uint32_t(value)<<16);
        if(slot==2 && (width==4 || physical&2)) {
            const auto n=state_->ar_dma_[2]&0x7FFFFFFF, a=state_->ar_dma_[1];
            if((n%32)||(a%32)||(state_->ar_dma_[0]%32)||n>state_->boot_aram_.size()||a>state_->boot_aram_.size()-n)
                throw access_error("Unsupported bootstrap ARAM DMA extent",address);
            for(unsigned i=0;i<n;++i) {
                if(state_->ar_dma_[2]&0x80000000u) write(state_->ar_dma_[0]+i,state_->boot_aram_[a+i],1);
                else state_->boot_aram_[a+i]=static_cast<uint8_t>(read(state_->ar_dma_[0]+i,1));
            }
            if(state_->ar_dma_[0]==0x01000000 && state_->ar_dma_[1]==0 && state_->ar_dma_[2]==32) ++state_->boot_dma_count_;
            state_->dsp_control_|=0x20;
            state_->pi_.source(0x40,((state_->dsp_control_>>1)&state_->dsp_control_&0xA8)!=0);
        }
        return;
    }
    if(physical>=0x0D006480 && physical<0x0D006500) {
        if((width!=1 && width!=2 && width!=4) || physical%width || width>0x0D006500-physical)
            throw access_error("Invalid SI buffer write",address);
        for(unsigned i=0;i<width;++i) state_->si_buffer_[physical-0x0D006480+i]=static_cast<uint8_t>(value>>(8*(width-1-i)));
        return;
    }
    if(physical==0x0C00206C && width==2 && value<=1) { state_->vi_clock_=static_cast<uint16_t>(value); return; }
    if(physical==0x0D006430 && width==4) { state_->si_poll_=static_cast<uint32_t>(value); return; }
    if(physical==0x0D006434 && width==4) {
        state_->si_csr_=(static_cast<uint32_t>(value)&0x487F7F06)|((state_->si_csr_&0xB0000000)&~static_cast<uint32_t>(value&0x90000000));
        if(value&1) {
            // Explicit four disconnected GameCube ports: Null device returns no response.
            const unsigned channel=(value>>1)&3;
            state_->si_status_|=1u<<(27-channel*8);
            state_->si_csr_|=0xA0000000u; // COMERR + transfer complete; TSTART remains clear.
        }
        state_->pi_.source(8,((state_->si_csr_&0xC0000000)==0xC0000000)||((state_->si_csr_&0x18000000)==0x18000000)); return;
    }
    if(physical==0x0D006438 && width==4) {
        if(value&0x80000000) throw access_error("SI output command requires controller protocol",address);
        state_->si_status_&=~static_cast<uint32_t>(value&0x0F0F0F0F); return;
    }
    if(physical>=0x0D006C00 && physical<0x0D006C10 && width==4 && !(physical%4)) {
        state_->ai_.write(physical-0x0D006C00,static_cast<uint32_t>(value),timebase());
        state_->pi_.source(0x20,state_->ai_.pending()); return;
    }
    for(unsigned i=0;i<3;++i) if(physical==0x0D006800+i*20 && width==4) {
        if(value & ~0x3FFFull) throw access_error("Unknown EXI status bits",address);
        if(i==0 && ((state_->exi_status_[0]^value)&0x380)) state_->ipl_.deselect();
        const uint32_t flags=(state_->exi_status_[i]&0x80A)&~static_cast<uint32_t>(value);
        state_->exi_status_[i]=flags|(static_cast<uint32_t>(value)&0x27F5);
        bool pending=false;
        for(auto s:state_->exi_status_) pending|=((s&1)&&(s&2))||((s&4)&&(s&8))||((s&0x400)&&(s&0x800));
        state_->pi_.source(0x10,pending); return;
    }
    if(physical>=0x0D006800 && physical<0x0D00683C && width==4 && !(physical%4)) {
        auto index=(physical-0x0D006800)/20, reg=(physical-0x0D006800)%20;
        if(reg==12 && (value&1)) {
            const auto selected=(state_->exi_status_[index]>>7)&7;
            if(index==0 && selected==2) {
                const auto mode=(value>>2)&3;
                if(mode==3 || value&~0x3Full) throw access_error("Invalid IPL transfer control",address);
                const auto rtc=static_cast<uint32_t>(key::timebase()/key::timebase_hz);
                if(value&2) {
                    auto a=state_->exi_registers_[0][0], n=state_->exi_registers_[0][1];
                    if(mode==2 || (a%32) || (n%32) || n>4096) throw access_error("Unsupported IPL DMA extent",address);
                    for(uint32_t j=0;j<n;++j) {
                        if(a+j<a) throw access_error("IPL DMA wrap",a);
                        if(mode==0) write(a+j,state_->ipl_.exchange(0,rtc),1);
                        else state_->ipl_.exchange(static_cast<uint8_t>(read(a+j,1)),rtc);
                    }
                } else {
                    auto n=((value>>4)&3)+1;
                    uint32_t result=0;
                    for(unsigned j=0;j<n;++j) {
                        auto sent=mode==0?uint8_t(0):static_cast<uint8_t>(state_->exi_registers_[0][3]>>(24-j*8));
                        result|=uint32_t(state_->ipl_.exchange(sent,rtc))<<(24-j*8);
                    }
                    if(mode!=1) state_->exi_registers_[0][3]=result;
                }
                state_->exi_registers_[0][2]=static_cast<uint32_t>(value)&~1u;
                state_->exi_status_[0]|=8;
                bool pending=false;
                for(auto s:state_->exi_status_) pending|=((s&1)&&(s&2))||((s&4)&&(s&8))||((s&0x400)&&(s&0x800));
                state_->pi_.source(0x10,pending); return;
            }
            if((value&~0x3Full) || (value&2) || (((value>>2)&3)==3) || (selected!=1 && selected!=4))
                throw access_error("Unsupported EXI selection or DMA transfer",address);
            // Explicitly absent optional devices, matching Dolphin's None immediate semantics.
            if(((value>>2)&3)==0) state_->exi_registers_[index][3]=0;
            state_->exi_registers_[index][2]=static_cast<uint32_t>(value)&~1u;
            state_->exi_status_[index]|=8;
            bool pending=false;
            for(auto s:state_->exi_status_) pending|=((s&1)&&(s&2))||((s&4)&&(s&8))||((s&0x400)&&(s&0x800));
            state_->pi_.source(0x10,pending); return;
        }
        if(reg) { state_->exi_registers_[index][reg/4-1]=static_cast<uint32_t>(value); return; }
    }
    if (physical==0x0D000034 && width==4) {
        if(value!=0x40000000 && value!=0) throw access_error("Unimplemented IPC interrupt mask bits",address);
        state_->ipc_mask_=static_cast<uint32_t>(value);
        state_->pi_.source(0x4000,false); // No IPC requests have been submitted; IPC transactions still trap.
        return;
    }
    if (physical==0x0C00401C && width==2) {
        if(value & ~0x1Full) throw access_error("Unknown MI interrupt mask",address);
        state_->mi_mask_=static_cast<uint16_t>(value); return;
    }
    if(physical==0x0C004010 && width==2 && value==0xFF) { state_->mi_protection_=0xFF; return; }
    if(physical==0x0C004020 && width==2 && value==0) { state_->mi_unknown_=0; return; }
    if (physical==0x0C00500A && width==2) {
        if(state_->dsp_control_&value&0xA8)note_interrupt_ack();
        if(value & ~0x9FDull) throw access_error("Unsupported DSP control transition",address);
        if(value&1) { state_->dsp_mail_queue_.clear();
#ifdef KEY_WITH_AX
            ax::reset();
#endif
            state_->dsp_rom_=true;state_->dsp_mail_value_=0x8071FEED;state_->dsp_mail_valid_=true;state_->dsp_boot_command_=state_->dsp_task_fields_=0;state_->boot_dma_count_=0; }
        if((state_->dsp_control_&0x800) && !(value&0x800)) {
            if(!state_->dsp_image_valid_ || state_->boot_dma_count_<2) throw access_error("DSP boot protocol incomplete",address);
            for(unsigned i=0;i<128;++i) if(read(0x81000000+i,1)!=state_->dsp_boot_image_[i])
                throw access_error("Unknown DSP microcode: no matching HLE",address);
            state_->dsp_rom_=false;state_->dsp_mail_value_=0x80544348;state_->dsp_mail_valid_=true; // Validated INIT microcode acknowledgement.
        }
        state_->dsp_control_=static_cast<uint16_t>((value & 0x954) | ((state_->dsp_control_ & 0xA8) & ~value));
        state_->pi_.source(0x40, ((state_->dsp_control_>>1)&state_->dsp_control_&0xA8)!=0);
        return;
    }
    if (physical >= 0x0C003000 && physical < 0x0C003100) {
        if(width!=4 || address%4) throw access_error("PI requires aligned 32-bit write", address);
        state_->pi_.write(physical-0x0C003000, static_cast<uint32_t>(value));
        return;
    }
    if (width != 1 && width != 2 && width != 4 && width != 8) throw std::runtime_error("Invalid write width");
    if (width==8 && address%4) throw access_error("Unaligned doubleword guest write", address);
    auto off = offset(address, width);
    const bool template_write = state_->template_access_ && width == 4 &&
        std::find(state_->template_words_.begin(), state_->template_words_.end(), off) != state_->template_words_.end();
    for (auto [begin, end] : state_->executable_)
        if (off < end && off + width > begin && !template_write) throw access_error("Executable write requires explicit code invalidation model", address);
    cpu_overwrite(off,width);
    for (unsigned i = 0; i < width; ++i) state_->bytes_[off + i] = static_cast<uint8_t>(value >> (8 * (width - 1 - i)));
    touch(off,width);
    for (unsigned i = 0; i < width && off + i < state_->low_known_.size(); ++i) state_->low_known_[off + i] = true;
}
void Memory::fifo_bytes(const uint8_t* data,size_t length) {
    const bool linked=(state_->cp_registers_[1]&0x10)!=0;
    if(!linked && !state_->fifo_record_logged_){state_->fifo_record_logged_=true;std::fprintf(stderr,"GX guest display-list recording through RAM FIFO\n");}
    if(linked && !(state_->cp_registers_[1]&1))throw std::runtime_error("Linked GX FIFO writes while GP reader is disabled require queue scheduling");
    if(linked && !state_->fifo_sink_)throw std::runtime_error("GX FIFO requires active native renderer");
    size_t cursor=0;
    while(cursor<length){
        const auto address=state_->pi_.fifo_address();
        const auto remaining=uint64_t(state_->pi_.read(0x10))+32-address;
        const auto count=std::min<size_t>(length-cursor,remaining);
        if(!linked && count==remaining)throw std::runtime_error("Display-list recording FIFO reached capacity");
        write_bytes(address,{data+cursor,count});state_->pi_.advance_fifo(count);cursor+=count;
    }
    if(linked){
        state_->fifo_sink_(data,length);
        const auto pointer=state_->pi_.read(0x14);
        state_->cp_registers_[0x34/2]=state_->cp_registers_[0x38/2]=pointer&0xFFFF;
        state_->cp_registers_[0x36/2]=state_->cp_registers_[0x3A/2]=pointer>>16;
    }
}
void Memory::load(uint32_t address, const uint8_t* source, size_t size, bool executable) {
    auto off = offset(address, size);
    std::copy_n(source, size, state_->bytes_.begin() + off);
    touch(off,size);
    for (size_t i = 0; i < size && off + i < state_->low_known_.size(); ++i) state_->low_known_[off + i] = true;
    if (executable) state_->executable_.emplace_back(off, off + static_cast<uint32_t>(size));
}
void Memory::write_bytes(uint32_t address,std::span<const uint8_t> data) {
    const auto off=offset(address,data.size());
    for(auto [begin,end]:state_->executable_) if(off<end && uint64_t(off)+data.size()>begin)
        throw access_error("Bulk data write overlaps executable memory",address);
    cpu_overwrite(off,data.size());
    std::copy(data.begin(),data.end(),state_->bytes_.begin()+off);
    touch(off,data.size());
    for(size_t i=0;i<data.size() && off+i<state_->low_known_.size();++i) state_->low_known_[off+i]=true;
}
void Memory::fill(uint32_t address, size_t size, uint8_t value) {
    auto off = offset(address, size);
    for (auto [begin, end] : state_->executable_)
        if (off < end && uint64_t(off) + size > begin) throw access_error("Fill overlaps executable memory", address);
    cpu_overwrite(off,size);
    std::fill_n(state_->bytes_.begin() + off, size, value);
    touch(off,size);
    for (size_t i = 0; i < size && off + i < state_->low_known_.size(); ++i) state_->low_known_[off + i] = true;
}
void Memory::send_dsp_mail(uint32_t mail) {
#ifdef KEY_WITH_AX
    if(ax::active()){ax::send_mail(mail);return;}
#endif
    if(!state_->dsp_rom_)throw std::runtime_error("Mail to DSP initialization microcode is unsupported");
    if(!state_->dsp_boot_command_) {
        switch(mail) {
        case 0x80F3A001:case 0x80F3A002:case 0x80F3B002:case 0x80F3C002:case 0x80F3D001:state_->dsp_boot_command_=mail;return;
        default:throw std::runtime_error("Unknown DSP ROM mailbox command");
        }
    }
    unsigned field=0;
    switch(state_->dsp_boot_command_) {case 0x80F3A001:field=0;break;case 0x80F3A002:field=1;break;case 0x80F3B002:field=2;break;case 0x80F3C002:field=3;break;case 0x80F3D001:field=4;break;}
    state_->dsp_task_[field]=mail;state_->dsp_task_fields_|=1u<<field;state_->dsp_boot_command_=0;
    if(field==4) {
        if(state_->dsp_task_fields_!=31 || !state_->dsp_task_[1] || state_->dsp_task_[1]>0x2000 || state_->dsp_task_[2] || state_->dsp_task_[3]>=0x1000 || state_->dsp_task_[4]>=0x1000)
            throw std::runtime_error("Invalid DSP ROM boot task descriptor");
        gpu_view(state_->dsp_task_[0],state_->dsp_task_[1]);
#ifdef KEY_WITH_AX
        ax::boot(state_->dsp_task_[0],state_->dsp_task_[1],state_->dsp_task_[4]);state_->dsp_rom_=false;
#else
        throw std::runtime_error("DSP task requires AX backend");
#endif
    }
}
void Memory::copy(uint32_t destination,uint32_t source,size_t size,bool allow_overlap) {
    if(!size)return;
    const auto src=offset(source,size),dst=offset(destination,size);
    state_->gpu_copies_.read(src,size);
    if(!allow_overlap && src!=dst && src<uint64_t(dst)+size && dst<uint64_t(src)+size)
        throw std::runtime_error("Overlapping guest memcpy requires reviewed semantics");
    for(size_t i=0;i<size && src+i<state_->low_known_.size();++i)if(!state_->low_known_[src+i])throw access_error("Unknown boot memory in bulk copy",source);
    for(auto [begin,end]:state_->executable_)if(dst<end && uint64_t(dst)+size>begin)throw access_error("Bulk copy overlaps executable code",destination);
    cpu_overwrite(dst,size);
    std::memmove(state_->bytes_.data()+dst,state_->bytes_.data()+src,size);touch(dst,size);
    for(size_t i=0;i<size && dst+i<state_->low_known_.size();++i)state_->low_known_[dst+i]=true;
}
void Memory::post_dsp_mail(uint32_t value,bool interrupt) {
    if(state_->dsp_mail_queue_.size()>=64)throw std::runtime_error("DSP output mailbox overflow");
    state_->dsp_mail_queue_.push_back({value,interrupt});
    if(state_->dsp_mail_queue_.size()==1 && interrupt){state_->dsp_control_|=0x80;state_->pi_.source(0x40,((state_->dsp_control_>>1)&state_->dsp_control_&0xA8)!=0);}
}
void Memory::poll_audio_dma(uint64_t now) {
    if(!(state_->audio_dma_control_&0x8000))return;
    if(state_->audio_dma_start_irq_ && now>=state_->audio_dma_start_irq_){state_->dsp_control_|=8;state_->audio_dma_start_irq_=0;}
    const uint32_t rate=(state_->ai_.read(0,now)&0x40)?32000:48000;
    if(rate!=state_->audio_dma_rate_){state_->audio_dma_rate_=rate;state_->audio_dma_fraction_=0;}
    for(unsigned n=0;!(state_->dsp_control_&8);++n){
        const auto numerator=timebase_hz*8+state_->audio_dma_fraction_;
        const auto period=numerator/rate;
        if(now<state_->audio_dma_tick_+period)break;
        if(n>=1024)throw std::runtime_error("Audio DMA backlog exceeds native scheduling bound");
        state_->audio_sink_(gpu_view(state_->audio_dma_cursor_,32).first(32),rate);
        state_->audio_dma_fraction_=numerator%rate;
        state_->audio_dma_tick_+=period;state_->audio_dma_cursor_+=32;
        if(state_->audio_dma_remaining_)--state_->audio_dma_remaining_;
        if(!state_->audio_dma_remaining_){
            state_->audio_dma_cursor_=state_->audio_dma_source_;state_->audio_dma_remaining_=state_->audio_dma_control_&0x7FFF;
            if(!state_->audio_dma_remaining_)throw std::runtime_error("Audio DMA reloaded zero-sized buffer");
            state_->dsp_control_|=8;
        }
    }
    state_->pi_.source(0x40,((state_->dsp_control_>>1)&state_->dsp_control_&0xA8)!=0);
}
Memory& memory() { static Memory value; return value; }
void Memory::sync_gpu_events() {
    if(state_->gpu_finish_event_.exchange(false,std::memory_order_acq_rel)) state_->pe_finish_pending_=true;
    const auto token=state_->gpu_token_event_.exchange(0,std::memory_order_acq_rel);
    if(token&0x10000) { state_->pe_registers_[7]=token&0xFFFF; state_->pe_token_pending_=true; }
    state_->pi_.source(0x200,state_->pe_token_pending_&&(state_->pe_registers_[5]&1));
    state_->pi_.source(0x400,state_->pe_finish_pending_&&(state_->pe_registers_[5]&2));
}
void Memory::touch(uint32_t off,size_t size) {
    if(!size) return;
    const auto revision=state_->write_revision_.fetch_add(1,std::memory_order_relaxed)+1;
    for(size_t p=off/4096;p<=(uint64_t(off)+size-1)/4096;++p) state_->page_versions_[p].store(revision,std::memory_order_release);
}
std::span<const uint8_t> Memory::gpu_view(uint32_t address,size_t minimum) const {
    const auto off=offset(address,minimum);
    size_t available=off<mem1_size?mem1_size-off:state_->bytes_.size()-off;
    if(address>=0x90000000 && address<0x93800000) available=std::min<size_t>(available,0x93800000-address);
    return {state_->bytes_.data()+off,available};
}
void* Memory::gpu_writable(uint32_t address,size_t size) {
    const auto off=offset(address,size);
    for(auto [begin,end]:state_->executable_) if(off<end && uint64_t(off)+size>begin) throw access_error("GPU destination overlaps executable code",address);
    return state_->bytes_.data()+off;
}
uint64_t Memory::generation(const void* host,size_t size) const {
    auto base=reinterpret_cast<uintptr_t>(state_->bytes_.data()),p=reinterpret_cast<uintptr_t>(host);
    if(!size || p<base || p-base>state_->bytes_.size() || size>state_->bytes_.size()-(p-base)) return 0;
    uint64_t latest=0;
    for(size_t page=(p-base)/4096;page<=(p-base+size-1)/4096;++page) latest=std::max(latest,state_->page_versions_[page].load(std::memory_order_acquire));
    return latest;
}
void Memory::notify_gpu_write(const void* host,size_t size) {
    auto base=reinterpret_cast<uintptr_t>(state_->bytes_.data()),p=reinterpret_cast<uintptr_t>(host);
    if(!size || p<base || p-base>state_->bytes_.size() || size>state_->bytes_.size()-(p-base)) throw std::runtime_error("GPU write notification outside guest RAM");
    state_->gpu_copies_.overwrite(static_cast<uint32_t>(p-base),size);
    touch(static_cast<uint32_t>(p-base),size);
}
void Memory::mark_gpu_copy(uint32_t address,size_t size){gpu_writable(address,size);state_->gpu_copies_.mark(offset(address,size),size);}
void Memory::cpu_overwrite(uint32_t off,size_t size){
    if(state_->gpu_copies_.overlaps(off,size) && state_->fifo_sync_)state_->fifo_sync_();
    state_->gpu_copies_.overwrite(off,size);
}
}
