// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <span>
#include <memory>

namespace key {
class Memory {
public:
    static constexpr uint32_t mem1_size = 24 * 1024 * 1024;
    Memory();
    ~Memory();
    void reset();
    uint64_t read(uint32_t address, unsigned width) const;
    void write(uint32_t address, uint64_t value, unsigned width);
    void load(uint32_t address, const uint8_t* source, size_t size, bool executable);
    void fill(uint32_t address, size_t size, uint8_t value);
    void copy(uint32_t destination,uint32_t source,size_t size,bool allow_overlap);
    void add_template_word(uint32_t address);
    bool template_access(bool enabled);

    bool interrupt_pending() const;

    void capture_dsp_boot_image(uint32_t address);
    void configure_retail_mapping();
    void write_bytes(uint32_t address,std::span<const uint8_t> data);
    using FifoSink=void(*)(const uint8_t*,size_t);
    void set_fifo_sink(FifoSink sink);

    void set_fifo_sync(void(*sync)(),bool(*empty)());

    void fifo_bytes(const uint8_t* data,size_t length);
    void poll_devices();

    void ave_write(uint32_t device,uint32_t source,uint32_t size);
    std::span<const uint8_t> ave_register(uint8_t reg) const;

    std::span<const uint8_t> gpu_view(uint32_t address,size_t minimum) const;
    void* gpu_writable(uint32_t address,size_t size);
    void mark_gpu_copy(uint32_t address,size_t size);
    uint64_t generation(const void* host,size_t size) const;
    void notify_gpu_write(const void* host,size_t size);
    void gpu_finish();

    void gpu_token(uint16_t token);

    void post_dsp_mail(uint32_t value,bool interrupt);
    using AudioSink=void(*)(std::span<const uint8_t>,uint32_t);
    void set_audio_sink(AudioSink sink);

    void poll_audio_dma(uint64_t now);
    // Host loading is explicit. Guest accesses cannot modify executable bytes.
private:
    struct State;
    std::unique_ptr<State> state_;
    uint32_t offset(uint32_t address,size_t size)const;
    void send_dsp_mail(uint32_t mail);
    void sync_gpu_events();
    void touch(uint32_t offset,size_t size);
    void cpu_overwrite(uint32_t offset,size_t size);
};
Memory& memory();
}
