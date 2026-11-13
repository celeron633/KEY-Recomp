// SPDX-License-Identifier: GPL-3.0-only
#include "key/guest_memory_abi.h"
extern "C" void GX_HLE_FIFO_Write8(uint8_t value) { key::memory().write(0xCC008000,value,1); }
extern "C" void GX_HLE_FIFO_Write16(uint16_t value) { key::memory().write(0xCC008000,value,2); }
extern "C" void GX_HLE_FIFO_Write32(uint32_t value) { key::memory().write(0xCC008000,value,4); }
extern "C" void GX_HLE_FIFO_WriteFloat(double value) { key::memory().write(0xCC008000,key::single_store_bits(value),4); }
extern "C" void GX_HLE_FIFO_WriteBurst(const uint8_t* bytes,uint32_t size) { key::memory().fifo_bytes(bytes,size); }
