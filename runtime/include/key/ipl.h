// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
namespace key {
class IplSettingsDevice {
public:
    IplSettingsDevice() { reset(); }
    void reset() {
        sram_.fill(0);
        sram_[0x17]=0x0C; // Newly generated English/stereo/configured preferences, no ROM bytes.
        uint16_t sum=0, inverse=0;
        for(unsigned i=0x10;i<0x18;i+=2) { auto v=uint16_t((sram_[i]<<8)|sram_[i+1]); sum+=v; inverse+=static_cast<uint16_t>(~v); }
        sram_[4]=sum>>8; sram_[5]=sum; sram_[6]=inverse>>8; sram_[7]=inverse;
        deselect();
    }
    void deselect() { command_=count_=cursor_=0; }
    uint8_t exchange(uint8_t byte, uint32_t rtc_seconds) {
        if(count_<4) {
            command_=(command_<<8)|byte;
            if(++count_==4) for(unsigned i=0;i<4;++i) sram_[i]=static_cast<uint8_t>(rtc_seconds>>(24-i*8));
            return 0xFF;
        }
        const auto address=(command_>>6)&0x1FFFFFF;
        if(address<0x800000 || address>=0x800044) throw std::runtime_error("IPL address requires additional device/ROM implementation");
        const auto index=(address-0x800000+cursor_++)%sram_.size();
        if(command_&0x80000000u) { sram_[index]=byte; return byte; }
        return sram_[index];
    }
private:
    std::array<uint8_t,0x44> sram_{};
    uint32_t command_=0,count_=0,cursor_=0;
};
}
