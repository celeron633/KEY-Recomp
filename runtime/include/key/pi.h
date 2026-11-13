// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include "key/interrupt_progress.h"
namespace key {
class ProcessorInterface {
public:
    void reset() { cause_=0x10100; mask_=0; fifo_base_=fifo_end_=fifo_write_=0; }
    uint32_t read(uint32_t reg) const {
        if (reg==0) return cause_;
        if (reg==4) return mask_;
        if (reg==0x24) return 0; // Cold reset; guest-initiated reset is not implemented.
        if(reg==0x0C) return fifo_base_;
        if(reg==0x10) return fifo_end_;
        if(reg==0x14) return fifo_write_&~31u;
        throw std::runtime_error("Unimplemented PI register read");
    }
    void write(uint32_t reg, uint32_t value) {
        if (reg==0) { if(cause_&value)note_interrupt_ack();cause_ &= ~value; return; }
        if (reg==4) { mask_=value; return; }
        if(reg==0x0C) {fifo_base_=value&0x1FFFFFE0;return;}
        if(reg==0x10) {fifo_end_=value&0x1FFFFFE0;return;}
        if(reg==0x14) {fifo_write_=value&0x1FFFFFE0;return;}
        throw std::runtime_error("Unimplemented PI register write");
    }
    void source(uint32_t bits, bool asserted) { if(asserted) cause_|=bits; else {if(cause_&bits)note_interrupt_ack();cause_&=~bits;} }
    bool pending() const { return (cause_ & mask_) != 0; }
    uint32_t fifo_address()const {
        if(fifo_end_<fifo_base_ || fifo_write_<fifo_base_ || uint64_t(fifo_write_)>=uint64_t(fifo_end_)+32)throw std::runtime_error("PI FIFO pointer outside configured ring");
        return fifo_write_;
    }
    void advance_fifo(size_t count) {
        if(fifo_end_<fifo_base_) throw std::runtime_error("Invalid PI FIFO bounds");
        const uint64_t size=uint64_t(fifo_end_)-fifo_base_+32;
        fifo_write_=fifo_base_+static_cast<uint32_t>((uint64_t(fifo_write_-fifo_base_)+count)%size);
    }
private:
    uint32_t cause_=0x10100, mask_=0;
    uint32_t fifo_base_=0,fifo_end_=0,fifo_write_=0;
};
}
