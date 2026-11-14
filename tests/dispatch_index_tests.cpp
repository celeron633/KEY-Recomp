// SPDX-License-Identifier: GPL-3.0-only
#include "key/dispatch_index.h"
#include <cstdio>
struct Record {uint32_t address;unsigned value;};
static void check(bool ok){if(!ok)throw std::runtime_error("Dispatch index test failed");}
template<class F>static void rejects(F fn){bool failed=false;try{fn();}catch(const std::runtime_error&){failed=true;}check(failed);}
int main(){
    const std::array<Record,6> records{{{0,1},{0x80004000,2},{0x90004000,3},{0x8000FFFC,4},{0x80010000,5},{0xFFFFFFFC,6}}};
    key::DispatchIndex<Record> index(records);
    for(const auto& record:records)check(index.find(record.address)==&record);
    for(uint32_t address:{1u,0x80004001u,0x80004004u,0xC0004000u,0xFFFFFFFFu,0x10000u})check(!index.find(address));
    const std::array<Record,2> duplicate{{{0x80004000,1},{0x80004000,2}}};
    rejects([&]{key::DispatchIndex<Record> invalid(duplicate);});
    const std::array<Record,1> unaligned{{{3,1}}};
    rejects([&]{key::DispatchIndex<Record> invalid(unaligned);});
    std::vector<Record> many;
    for(unsigned i=0;i<65535;++i)many.push_back({0x80000000u+i*4,i});
    key::DispatchIndex<Record> full(many);
    for(unsigned i=0;i<many.size();++i)check(full.find(many[i].address)==&many[i]);
    many.push_back({0x80000000u+65535*4,65535});
    rejects([&]{key::DispatchIndex<Record> invalid(many);});
    std::puts("PASS: full guest addresses, sparse pages, alignment, missing targets and registration bounds");
}
