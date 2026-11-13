// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <vector>

namespace key::graphics {
class CommandBridge {
public:
    using Emit=std::function<void(std::span<const uint8_t>)>;
    using Resolve=std::function<std::span<const uint8_t>(uint32_t,size_t)>;
    using Writable=std::function<void*(uint32_t,size_t)>;
    CommandBridge(Emit emit,Resolve resolve,Writable writable);
    void feed(std::span<const uint8_t> bytes);
    bool empty() const { return pending_.empty(); }
    uint64_t draw_count() const { return draws_; }
    uint64_t display_copies() const { return display_copies_; }
private:
    Emit emit_; Resolve resolve_; Writable writable_;
    std::vector<uint8_t> pending_;
    std::array<uint32_t,256> cp_{},bp_{};
    std::array<size_t,16> array_sizes_{};
    std::array<bool,16> array_assigned_{};
    uint32_t bp_mask_=0xFFFFFF;
    std::array<bool,8> texture_bound_{};
    std::array<uint32_t,8> tlut_binding_{};
    std::vector<uint8_t> palette_memory_;
    std::vector<bool> palette_valid_;
    std::map<std::vector<uint8_t>,std::unique_ptr<std::vector<uint8_t>>> palette_snapshots_;
    size_t palette_bytes_=0;
    uint64_t draws_=0;
    uint64_t display_copies_=0;
    size_t packet_size(std::span<const uint8_t> data) const;
    size_t vertex_size(unsigned fmt) const;
    void packet(std::span<const uint8_t> data,unsigned depth);
    void display_list(std::span<const uint8_t> data,unsigned depth);
    void prepare_textures();
    void prepare_arrays(std::span<const uint8_t> draw);
    void bind_array(unsigned index,size_t required);
    void bp_write(uint32_t value);
    void extension(uint16_t command,std::span<const uint8_t> payload);
};
}
