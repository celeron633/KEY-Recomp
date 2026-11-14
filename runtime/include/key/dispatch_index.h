// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>
namespace key {
// Immutable function registrations, indexed by the full 32-bit guest address.
// Sparse 64 KiB address pages keep lookup constant-time without a 4 GiB table.
template<class Record> class DispatchIndex {
    using Page=std::array<uint16_t,16384>;
    std::vector<std::unique_ptr<Page>> pages_{65536};
    std::span<const Record> records_;
public:
    explicit DispatchIndex(std::span<const Record> records):records_(records) {
        if(records.size()>65535)throw std::runtime_error("Native dispatch index exceeds registration capacity");
        for(size_t i=0;i<records.size();++i){
            const uint32_t address=records[i].address;
            if(address&3)throw std::runtime_error("Unaligned native function registration");
            auto& page=pages_[address>>16];
            if(!page)page=std::make_unique<Page>();
            auto& entry=(*page)[(address&65535)>>2];
            if(entry)throw std::runtime_error("Duplicate native function registration");
            entry=static_cast<uint16_t>(i+1);
        }
    }
    [[gnu::always_inline]] const Record* find(uint32_t address) const noexcept {
        if(address&3)return nullptr;
        const auto& page=pages_[address>>16];
        if(!page)return nullptr;
        const auto entry=(*page)[(address&65535)>>2];
        return entry?&records_[entry-1]:nullptr;
    }
};
}
