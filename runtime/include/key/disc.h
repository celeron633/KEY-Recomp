// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>
namespace key {
class ExtractedDisc {
public:
    explicit ExtractedDisc(const std::filesystem::path& data_root,const std::filesystem::path& layout_root);
    std::vector<uint8_t> read(uint64_t offset,uint32_t length) const;
    std::vector<uint8_t> read_system(uint64_t offset,uint32_t length) const;
    uint64_t capacity() const { return capacity_; }
    uint64_t partition_offset() const { return partition_offset_; }
    const std::vector<uint8_t>& ticket_view() const { return ticket_view_; }
private:
    struct Segment { uint64_t begin,size; std::filesystem::path file; };
    std::vector<Segment> segments_;
    uint64_t capacity_=0,partition_offset_=0;
    std::vector<uint8_t> system_area_;
    std::vector<uint8_t> ticket_view_;
};
}
