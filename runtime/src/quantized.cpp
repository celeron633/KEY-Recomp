// SPDX-License-Identifier: GPL-3.0-only
#include "guest_abi/guest_abi.h"
extern "C" double PPC_PsqL(uint32_t address,uint32_t w,uint32_t index) {
    auto* ctx=TryGetCpuContext();
    if(!ctx || w>1 || index>7) throw std::runtime_error("Invalid PSQ load context/encoding");
    return w?PPC_PsqLStateFallback<1,false>(ctx->gqr[index],address):PPC_PsqLStateFallback<0,false>(ctx->gqr[index],address);
}
extern "C" void PPC_PsqSt(uint32_t address,double value,uint32_t w,uint32_t index) {
    auto* ctx=TryGetCpuContext();
    if(!ctx || w>1 || index>7) throw std::runtime_error("Invalid PSQ store context/encoding");
    if(w) PPC_PsqStStateFallback<1,false>(ctx->gqr[index],address,value);
    else PPC_PsqStStateFallback<0,false>(ctx->gqr[index],address,value);
}
