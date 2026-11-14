// SPDX-License-Identifier: GPL-3.0-only
#include "guest_abi/guest_abi.h"
#include "key/dispatch_context.h"
#include "key/dispatch_depth.h"
#include <array>
#include <cstdio>
#include <stdexcept>
#include <vector>

static void check(bool ok){if(!ok)throw std::runtime_error("Dispatch context contract failed");}
struct Snapshot {
    CpuContext* context;
    uint32_t control;
    bool ni;
    double threshold;
    bool operator==(const Snapshot&) const = default;
};

template<class Scope> std::vector<Snapshot> exercise(std::array<CpuContext,2>& contexts,uint32_t initial) {
    contexts={};g_currentCpuContext=nullptr;MkwRestoreHostMxcsr(initial);
    std::vector<Snapshot> result;
    auto observe=[&]{result.push_back({TryGetCpuContext(),MkwGetHostFpControl(),g_mkwHostNiActive,g_mkwNiFlushThreshold});};
    observe();
    {
        Scope outer(&contexts[0]);
        check(TryGetCpuContext()==&contexts[0]);
        check((MkwGetHostFpControl()&kMkwFpControlFlushToZeroBits)==0);
        observe();
        {
            Scope same(&contexts[0]);
            PPC_Mtfsb1(29);
            check((MkwGetHostFpControl()&kMkwFpControlFlushToZeroBits)==kMkwFpControlFlushToZeroBits);
            observe();
        }
        observe();
        {
            Scope other(&contexts[1]);observe();
            // The parent's live FPSCR, not its entry value, controls return.
            contexts[0].fpscr=0;
            MkwSetHostFpControl((MkwGetHostFpControl()&~0x6000u)|0x4000u|0x21u);
            observe();
        }
        check(TryGetCpuContext()==&contexts[0]);
        check((MkwGetHostFpControl()&kMkwFpControlFlushToZeroBits)==0);
        check((MkwGetHostFpControl()&0x6000u)==0x4000u);
        observe();
        // A host callee can alter FTZ/DAZ without updating the software mirror.
        // Even a same-context nested dispatch must repair this at its boundary.
        MkwSetHostFpControl(MkwGetHostFpControl()|kMkwFpControlFlushToZeroBits);
        {
            Scope same(&contexts[0]);
            check((MkwGetHostFpControl()&kMkwFpControlFlushToZeroBits)==0);
            observe();PPC_Mtfsf(255,0.0);observe();
        }
        observe();
        {
            Scope no_context(nullptr);check(TryGetCpuContext()==nullptr);observe();
        }
        observe();
        try {
            Scope other(&contexts[1]);PPC_Mtfsb1(29);observe();
            throw 1;
        } catch(int) {observe();}
    }
    check(TryGetCpuContext()==nullptr && MkwGetHostFpControl()==initial);
    observe();
    try {
        Scope outer(&contexts[0]);PPC_Mtfsb1(29);throw 2;
    } catch(int) {observe();}
    check(TryGetCpuContext()==nullptr && MkwGetHostFpControl()==initial);
    {Scope null_root(nullptr);observe();}
    observe();return result;
}

static void depths() {
    key::DispatchDepths<CpuContext> store;
    std::array<CpuContext,3> contexts{};
    auto& first=store.get(&contexts[0]);
    auto& second=store.get(&contexts[1]);
    check(first==0 && second==0 && &first==&store.get(&contexts[0]));
    first=255;second=7;
    auto& third=store.get(&contexts[2]);third=2;
    check(&first==&store.get(&contexts[0]) && first==255 && second==7);
    ++first;check(first==256);--first;
    // A suspended caller retains its node reference while nested dispatches
    // change the last-context cache and unwind through another context.
    try {
        ++first;
        try {++second;throw 3;}catch(...){--second;throw;}
    } catch(int) {--first;}
    check(first==255 && second==7 && third==2);
}

int main() {
    const auto original=MkwGetHostFpControl();
    try {
        std::array<CpuContext,2> contexts{};
        const uint32_t clean=(original|0x1F80u)&~(0x6000u|0x3Fu|kMkwFpControlFlushToZeroBits);
        for(uint32_t rounding=0;rounding<4;++rounding)
            for(uint32_t flush:{0u,0x40u,0x8000u,0x8040u})
                for(uint32_t status:{0u,0x21u}) {
                    const auto initial=clean|(rounding<<13)|flush|status;
                    const auto reference=exercise<CpuContextScope>(contexts,initial);
                    const auto candidate=exercise<key::DispatchContextScope>(contexts,initial);
                    check(reference==candidate);
                }
        depths();MkwRestoreHostMxcsr(original);
        std::puts("PASS: nested/null contexts, FPSCR changes, host FP repair/restoration, exception unwind, reference equivalence and per-context depth stability");
    } catch(const std::exception& e) {
        g_currentCpuContext=nullptr;MkwRestoreHostMxcsr(original);
        std::fprintf(stderr,"%s\n",e.what());return 1;
    }
}
